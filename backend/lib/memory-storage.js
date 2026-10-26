import { isValidCachedTile, MAXIMUM_TILE_BYTES } from "../src/map-cache.js";

// Serverless hosts (Vercel) have no writable state directory: /tmp is per-instance
// and short-lived. This backend keeps decoded tiles in the instance's heap, which
// is always warm for a rider's burst of requests and thrown away with the instance.
// Nothing here is shared between instances; a cold instance re-reads the PMTiles
// source instead of inventing data.
const MAXIMUM_SOURCES = 8;

// The storage contract is asynchronous, like MapDiskCache and the R2 backend:
// map-tiles.js chains .catch() on put(), so a synchronous return would throw.
export function createMemoryStorage({ maximumTiles = 300, maximumBytes = 64 * 1024 * 1024 } = {}) {
  if (!Number.isSafeInteger(maximumTiles) || maximumTiles < 1 || maximumTiles > 100_000) {
    throw new TypeError("in-memory tile limit must be between 1 and 100000");
  }
  if (!Number.isSafeInteger(maximumBytes) || maximumBytes < 1024 || maximumBytes > 1024 ** 3) {
    throw new TypeError("in-memory byte limit must be between 1 KiB and 1 GiB");
  }

  const tiles = new Map(); // name -> { value, bytes }, oldest first
  const sources = new Map(); // namespace -> source metadata, oldest first
  let bytes = 0;
  let lastError = null;

  function evict() {
    while (tiles.size > maximumTiles || bytes > maximumBytes) {
      const oldest = tiles.keys().next().value;
      if (oldest === undefined) break;
      bytes -= tiles.get(oldest).bytes;
      tiles.delete(oldest);
    }
  }

  const cache = {
    ready: Promise.resolve(),
    lastError: null,
    get(name, { z, x, y }) {
      const entry = tiles.get(name);
      if (!entry) return null;
      // Refresh recency so a rider's current area survives the next eviction pass.
      tiles.delete(name);
      tiles.set(name, entry);
      if (!isValidCachedTile(entry.value, { z, x, y })) {
        bytes -= entry.bytes;
        tiles.delete(name);
        lastError = "MAP_CACHE_READ_FAILED";
        cache.lastError = lastError;
        return null;
      }
      return entry.value;
    },
    async put(name, value) {
      const bytes_ = Buffer.byteLength(JSON.stringify(value));
      if (bytes_ > MAXIMUM_TILE_BYTES) throw new Error("map tile too large");
      const previous = tiles.get(name);
      if (previous) {
        bytes -= previous.bytes;
        tiles.delete(name);
      }
      tiles.set(name, { value, bytes: bytes_ });
      bytes += bytes_;
      evict();
      lastError = null;
      cache.lastError = null;
    },
    status() {
      return {
        storage: "memory",
        bytes,
        maximum_bytes: maximumBytes,
        tiles: tiles.size,
        maximum_tiles: maximumTiles,
        last_error: lastError,
      };
    },
  };
  cache.lastError = lastError;

  return {
    cache,
    readSource(namespace) {
      return sources.get(namespace) ?? null;
    },
    writeSource(namespace, value) {
      sources.delete(namespace);
      sources.set(namespace, value);
      while (sources.size > MAXIMUM_SOURCES) sources.delete(sources.keys().next().value);
    },
  };
}
