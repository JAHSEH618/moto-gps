// Import-time triage for deployments where the gateway returns
// FUNCTION_INVOCATION_FAILED and the platform log is the only other clue.
// Each module is loaded on its own so one broken dependency names itself.

// Results live per request: a warm instance would otherwise accumulate them.
async function probe(steps, name, load) {
  try {
    await load();
    steps.push({ module: name, ok: true });
  } catch (error) {
    steps.push({ module: name, ok: false, error: `${error?.name}: ${error?.message}` });
  }
}

export default async function diagnose(request, response) {
  const steps = [];
  await probe(steps, "src/gateway.js", () => import("../src/gateway.js"));
  await probe(steps, "src/amap-provider.js", () => import("../src/amap-provider.js"));
  await probe(steps, "src/amap-places.js", () => import("../src/amap-places.js"));
  await probe(steps, "src/amap-cities.js", () => import("../src/amap-cities.js"));
  await probe(steps, "src/amap-transformer.js", () => import("../src/amap-transformer.js"));
  await probe(steps, "src/map-tiles.js", () => import("../src/map-tiles.js"));
  await probe(steps, "src/map-cache.js", () => import("../src/map-cache.js"));
  await probe(steps, "lib/memory-storage.js", () => import("../lib/memory-storage.js"));
  await probe(steps, "lib/vercel-app.js", () => import("../lib/vercel-app.js"));
  await probe(steps, "lib/vercel-handler.js", () => import("../lib/vercel-handler.js"));

  // Upstream reachability from the function's own region: the map source needs both
  // the build manifest and byte ranges against the archive host.
  const upstream = [];
  async function probeUrl(name, url, init = {}) {
    const started = Date.now();
    try {
      const response = await fetch(url, { ...init, signal: AbortSignal.timeout(8000) });
      const body = await response.text();
      upstream.push({
        name,
        status: response.status,
        ms: Date.now() - started,
        bytes: body.length,
        head: body.slice(0, 60).replace(/\s+/g, " "),
      });
    } catch (error) {
      upstream.push({ name, ms: Date.now() - started, error: `${error?.name}: ${error?.message}` });
    }
  }
  await probeUrl("manifest", "https://build-metadata.protomaps.dev/builds.json");
  await probeUrl("archive", "https://build.protomaps.com/20261026.pmtiles", {
    headers: { Range: "bytes=0-63", "Accept-Encoding": "identity" },
  });

  const env = process.env;
  const body = JSON.stringify({
    ok: steps.every((step) => step.ok),
    node: process.version,
    region: env.VERCEL_REGION ?? null,
    // Presence only: never echo a key or an origin.
    env: {
      MOTO_PROVIDER: env.MOTO_PROVIDER ?? null,
      AMAP_WEB_SERVICE_KEY: typeof env.AMAP_WEB_SERVICE_KEY === "string" && env.AMAP_WEB_SERVICE_KEY.trim() !== "",
      MOTO_MAP_PMTILES_URL: env.MOTO_MAP_PMTILES_URL ?? null,
      WEB_ORIGIN: env.WEB_ORIGIN === undefined ? null : "set",
    },
    steps,
    upstream,
  }, null, 2);
  response.statusCode = 200;
  response.setHeader("Content-Type", "application/json; charset=utf-8");
  response.setHeader("Cache-Control", "no-store");
  response.end(body);
}
