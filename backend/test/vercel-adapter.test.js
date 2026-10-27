import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import { createServer } from "node:http";
import test from "node:test";

import { createVercelGateway } from "../lib/vercel-app.js";
import { handle, logicalRequestUrl } from "../lib/vercel-handler.js";

const routeRequest = JSON.parse(
  await readFile(new URL("../fixtures/route-request-v1.json", import.meta.url), "utf8"),
);

// The singleton reads process.env once per instance, exactly like a warm function.
process.env.MOTO_PROVIDER = "fixture";
process.env.MOTO_MAP_PMTILES_URL = "disabled";

const TILE = /^\/api\/v1\/map\/tiles\/(\d+)\/(\d+)\/(\d+)$/;

async function listen(t, server) {
  await new Promise((resolve, reject) => {
    server.once("error", reject);
    server.listen(0, "127.0.0.1", resolve);
  });
  t.after(() => new Promise((resolve) => server.close(resolve)));
  return `http://127.0.0.1:${server.address().port}`;
}

// Same shape as a deployed function: /api/... path, tile segments moved to the query.
async function startFunctions(t) {
  const server = createServer((request, response) => {
    const url = new URL(request.url ?? "/", "http://localhost");
    const tile = TILE.exec(url.pathname);
    if (tile) request.url = `/api/v1/map/tiles?z=${tile[1]}&x=${tile[2]}&y=${tile[3]}`;
    void handle(request, response);
  });
  return listen(t, server);
}

test("request URLs collapse to the gateway's own routes", () => {
  assert.equal(logicalRequestUrl("/api/healthz"), "/healthz");
  assert.equal(logicalRequestUrl("/api"), "/");
  assert.equal(
    logicalRequestUrl("/api/v1/places?keywords=%E6%B5%8E%E5%8D%97&region=370100"),
    "/v1/places?keywords=%E6%B5%8E%E5%8D%97&region=370100",
  );
  // Rewrite that moves the tile segments into the query...
  assert.equal(
    logicalRequestUrl("/api/v1/map/tiles?z=15&x=27044&y=12791"),
    "/v1/map/tiles/15/27044/12791",
  );
  // ...and a rewrite that keeps them in the path produce the same clean route.
  assert.equal(logicalRequestUrl("/api/v1/map/tiles/15/27044/12791"), "/v1/map/tiles/15/27044/12791");
  // A missing segment stays a plain tile request so the gateway rejects it itself.
  assert.equal(logicalRequestUrl("/api/v1/map/tiles?z=15&x=27044"), "/v1/map/tiles?z=15&x=27044");
});

test("a deployed function answers the whole navigation protocol", async (t) => {
  const base = await startFunctions(t);

  const health = await fetch(`${base}/api/healthz`);
  assert.equal(health.status, 200);
  const healthBody = await health.json();
  assert.equal(healthBody.status, "ok");
  assert.equal(healthBody.provider, "fixture");
  assert.equal(healthBody.ready_for_live_navigation, false);
  assert.equal(healthBody.capabilities.surrounding_map, false);
  assert.equal(health.headers.get("cache-control"), "no-store");

  const route = await fetch(`${base}/api/v1/routes`, {
    method: "POST",
    headers: { "content-type": "application/json" },
    body: JSON.stringify(routeRequest),
  });
  assert.equal(route.status, 200);
  const routeBody = await route.json();
  assert.equal(routeBody.protocol_version, 1);
  assert.equal(routeBody.request_id, routeRequest.request_id);
  assert.equal(routeBody.route.schema_version, 1);
  assert.ok(routeBody.route.polyline.length > 1);
  assert.ok(routeBody.route.maneuvers.length > 0);

  const options = await fetch(`${base}/api/v1/route-options`, {
    method: "POST",
    headers: { "content-type": "application/json" },
    body: JSON.stringify(routeRequest),
  });
  assert.equal(options.status, 200);
  assert.ok((await options.json()).routes.length >= 1);

  const places = await fetch(`${base}/api/v1/places?keywords=${encodeURIComponent("济南西站")}`);
  assert.equal(places.status, 200);
  assert.deepEqual((await places.json()).places, []);

  // Coordinates are validated before the map provider is consulted.
  const invalidTile = await fetch(`${base}/api/v1/map/tiles/14/0/0`);
  assert.equal(invalidTile.status, 400);
  assert.equal((await invalidTile.json()).error.code, "INVALID_REQUEST");

  const disabledTile = await fetch(`${base}/api/v1/map/tiles/15/27044/12791`);
  assert.equal(disabledTile.status, 503);
  assert.equal((await disabledTile.json()).error.code, "MAP_DISABLED");
});

test("rate limits follow the address the platform injected, not a caller header", async (t) => {
  const base = await startFunctions(t);
  const cities = `${base}/api/v1/map/cities?keywords=${encodeURIComponent("济南")}`;

  for (let i = 0; i < 30; i += 1) {
    const response = await fetch(cities, { headers: { "x-real-ip": "203.0.113.9" } });
    assert.equal(response.status, 503, `request ${i + 1} should still be answered`);
  }
  const limited = await fetch(cities, { headers: { "x-real-ip": "203.0.113.9" } });
  assert.equal(limited.status, 429);
  assert.equal(limited.headers.get("retry-after"), "60");

  // A different rider keeps their own budget.
  const other = await fetch(cities, { headers: { "x-real-ip": "198.51.100.4" } });
  assert.equal(other.status, 503);
});

test("a project without a key refuses live navigation instead of faking it", async (t) => {
  const { handler, mode } = createVercelGateway({ MOTO_PROVIDER: "disabled" });
  assert.equal(mode, "disabled");
  const base = await listen(t, createServer((request, response) => { void handler(request, response); }));

  const health = await (await fetch(`${base}/healthz`)).json();
  assert.equal(health.provider, "disabled");
  assert.equal(health.ready_for_live_navigation, false);

  const places = await fetch(`${base}/v1/places?keywords=${encodeURIComponent("济南西站")}`);
  assert.equal(places.status, 503);
  assert.equal((await places.json()).error.code, "SERVER_MISCONFIGURED");

  const route = await fetch(`${base}/v1/routes`, {
    method: "POST",
    headers: { "content-type": "application/json" },
    body: JSON.stringify(routeRequest),
  });
  assert.equal(route.status, 503);
});

test("a key alone selects live navigation, and an explicit mode still wins", () => {
  // Maps stay off: this test is about mode resolution, not the PMTiles source.
  const live = { MOTO_MAP_PMTILES_URL: "disabled" };
  assert.equal(createVercelGateway({ ...live, AMAP_WEB_SERVICE_KEY: "test-key" }).mode, "amap");
  assert.equal(
    createVercelGateway({ ...live, MOTO_PROVIDER: "disabled", AMAP_WEB_SERVICE_KEY: "k" }).mode,
    "disabled",
  );
  assert.throws(() => createVercelGateway({ MOTO_PROVIDER: "amap" }), /AMAP_WEB_SERVICE_KEY/);
  assert.throws(() => createVercelGateway({ MOTO_PROVIDER: "nonsense" }), /unsupported MOTO_PROVIDER/);
});
