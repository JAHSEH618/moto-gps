import assert from "node:assert/strict";
import test from "node:test";

import { createMemoryStorage } from "../lib/memory-storage.js";

function tile(z, x, y, overrides = {}) {
  return {
    schema_version: 1,
    coordinate_system: "GCJ-02",
    tile: { z, x, y },
    roads: [{ class: "residential", points_e6: [[1, 2], [3, 4]] }],
    buildings: [],
    source: {
      provider: "protomaps-openstreetmap",
      licence: "ODbL-1.0",
      attribution_url: "https://www.openstreetmap.org/copyright",
      retrieved_at: "2026-10-25T00:00:00.000Z",
      source_revision: "20261025",
      bbox_wgs84: [0, 0, 0, 0],
    },
    ...overrides,
  };
}

const name = (z, x, y) => `map-v1-0123456789abcdef-${z}-${x}-${y}.json`;

test("a cached tile round-trips and survives reading", async () => {
  const storage = createMemoryStorage();
  assert.equal(await storage.cache.get(name(15, 1, 2), { z: 15, x: 1, y: 2 }), null);
  // The shared map-tiles loader calls cache.put(...).catch(...); a synchronous return
  // would break the first tile of every new area with a TypeError.
  const written = storage.cache.put(name(15, 1, 2), tile(15, 1, 2));
  assert.equal(typeof written.then, "function");
  await written;
  const cached = storage.cache.get(name(15, 1, 2), { z: 15, x: 1, y: 2 });
  assert.deepEqual(cached.tile, { z: 15, x: 1, y: 2 });
  const status = storage.cache.status();
  assert.equal(status.storage, "memory");
  assert.equal(status.tiles, 1);
  assert.ok(status.bytes > 0);
  assert.equal(status.last_error, null);
});

test("a tile stored for another coordinate is a miss, never a borrowed map", async () => {
  const storage = createMemoryStorage();
  await storage.cache.put(name(15, 1, 2), tile(15, 1, 2));
  assert.equal(storage.cache.get(name(15, 1, 2), { z: 15, x: 9, y: 9 }), null);
  // The rejected entry is dropped so the next request re-reads the real source.
  assert.equal(storage.cache.status().tiles, 0);
  assert.equal(storage.cache.status().last_error, "MAP_CACHE_READ_FAILED");
});

test("malformed cache objects are rejected", async () => {
  const storage = createMemoryStorage();
  for (const broken of [
    tile(15, 1, 2, { schema_version: 2 }),
    tile(15, 1, 2, { coordinate_system: "WGS84" }),
    tile(15, 1, 2, { roads: "nope" }),
    tile(15, 1, 2, { source: { ...tile(15, 1, 2).source, retrieved_at: "yesterday" } }),
    tile(15, 1, 2, { source: { ...tile(15, 1, 2).source, source_revision: "" } }),
  ]) {
    await storage.cache.put(name(15, 1, 2), broken);
    assert.equal(storage.cache.get(name(15, 1, 2), { z: 15, x: 1, y: 2 }), null);
  }
});

test("the least recently used tile is evicted first", async () => {
  const storage = createMemoryStorage({ maximumTiles: 2 });
  await storage.cache.put(name(15, 1, 1), tile(15, 1, 1));
  await storage.cache.put(name(15, 2, 2), tile(15, 2, 2));
  // Touch the older one so the newer one becomes the eviction candidate.
  assert.ok(storage.cache.get(name(15, 1, 1), { z: 15, x: 1, y: 1 }));
  await storage.cache.put(name(15, 3, 3), tile(15, 3, 3));
  assert.equal(storage.cache.status().tiles, 2);
  assert.ok(storage.cache.get(name(15, 1, 1), { z: 15, x: 1, y: 1 }));
  assert.equal(storage.cache.get(name(15, 2, 2), { z: 15, x: 2, y: 2 }), null);
  assert.ok(storage.cache.get(name(15, 3, 3), { z: 15, x: 3, y: 3 }));
});

test("a single oversized tile is refused instead of filling the instance", async () => {
  const storage = createMemoryStorage({ maximumTiles: 10 });
  const huge = tile(15, 1, 2, {
    roads: [{ class: "primary", points_e6: Array.from({ length: 2_000_000 }, (_, i) => [i, i]) }],
  });
  await assert.rejects(() => storage.cache.put(name(15, 1, 2), huge), /too large/);
  assert.equal(storage.cache.status().tiles, 0);
});

test("map source metadata is remembered per source namespace", () => {
  const storage = createMemoryStorage();
  assert.equal(storage.readSource("namespace-a"), null);
  storage.writeSource("namespace-a", { url: "https://build.protomaps.com/20261025.pmtiles", refreshed_at_ms: 1 });
  storage.writeSource("namespace-b", { url: "https://build.protomaps.com/20261024.pmtiles", refreshed_at_ms: 2 });
  assert.equal(storage.readSource("namespace-a").refreshed_at_ms, 1);
  assert.equal(storage.readSource("namespace-b").refreshed_at_ms, 2);
  assert.equal(storage.readSource("namespace-c"), null);
});

test("storage limits are validated", () => {
  assert.throws(() => createMemoryStorage({ maximumTiles: 0 }), /tile limit/);
  assert.throws(() => createMemoryStorage({ maximumBytes: 10 }), /byte limit/);
});
