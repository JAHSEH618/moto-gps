import { createRequire } from "node:module";

import { createAmapProvider } from "../src/amap-provider.js";
import { createAmapPlacesProvider } from "../src/amap-places.js";
import { createAmapCitiesProvider } from "../src/amap-cities.js";
import { transformAmapRouteOptionsV2, transformAmapRouteV2 } from "../src/amap-transformer.js";
import { createMapTileProvider } from "../src/map-tiles.js";
import { createGatewayHandler } from "../src/gateway.js";
import { createMemoryStorage } from "./memory-storage.js";

// Vercel bundles functions with ncc; a static require is traced, a runtime readFile is not.
const require = createRequire(import.meta.url);

// Vercel's edge sets x-real-ip itself and overwrites any caller value, so it is the
// authoritative address. x-forwarded-for is appended by the platform, which makes its
// LAST entry the peer address; earlier entries are whatever the caller chose to send.
export function vercelClientAddress(request) {
  const realIp = String(request.headers["x-real-ip"] ?? "").trim();
  if (realIp !== "") return realIp;
  const forwarded = String(request.headers["x-forwarded-for"] ?? "").split(",").at(-1).trim();
  return forwarded !== "" ? forwarded : "unknown";
}

// A deployment that already holds a Web Service key intends live navigation; anything
// else stays disabled so a misconfigured project refuses instead of faking a route.
// /healthz always reports the resolved mode, so the guess is never invisible.
export function resolveProviderMode(env) {
  const configured = String(env.MOTO_PROVIDER ?? "").trim();
  if (configured !== "") return configured;
  return String(env.AMAP_WEB_SERVICE_KEY ?? "").trim() !== "" ? "amap" : "disabled";
}

function createProvider(mode, env) {
  if (mode === "amap") {
    const key = String(env.AMAP_WEB_SERVICE_KEY ?? "");
    if (key.trim() === "") {
      throw new Error("AMAP_WEB_SERVICE_KEY is required when MOTO_PROVIDER=amap");
    }
    return {
      ...createAmapProvider({ key }),
      ...createAmapPlacesProvider({ key }),
      ...createAmapCitiesProvider({ key }),
    };
  }
  if (mode === "fixture") {
    const fixture = require("../fixtures/amap-route-v2.json");
    return {
      async planRoute() {
        return transformAmapRouteV2(fixture, { generatedAtMs: Date.now() });
      },
      async planRouteOptions() {
        return transformAmapRouteOptionsV2(fixture, { generatedAtMs: Date.now() });
      },
      async searchPlaces({ keywords }) {
        return { protocol_version: 1, query: keywords, places: [] };
      },
    };
  }
  if (mode === "disabled") {
    const unavailable = async () => {
      throw Object.assign(new Error("AMap live navigation is not configured"), {
        code: "SERVER_MISCONFIGURED",
        retryable: false,
      });
    };
    return { planRoute: unavailable, planRouteOptions: unavailable, searchPlaces: unavailable };
  }
  throw new Error(`unsupported MOTO_PROVIDER: ${mode}`);
}

function positiveInteger(value, fallback) {
  const parsed = Number(value);
  return Number.isSafeInteger(parsed) && parsed > 0 ? parsed : fallback;
}

export function createVercelGateway(env = process.env) {
  const mode = resolveProviderMode(env);
  const provider = createProvider(mode, env);

  // Fixture and disabled deliberately keep the surrounding map offline, matching the
  // Node and Workers hosts, so a protocol test never reaches the public map source.
  const configuredMapUrl = String(env.MOTO_MAP_PMTILES_URL ?? "").trim();
  const mapUrl = configuredMapUrl !== "" ? configuredMapUrl
    : mode === "amap" ? "auto" : "disabled";
  const mapProvider = createMapTileProvider({
    url: mapUrl,
    scheduleRefresh: false, // No long-lived process; each cold instance refreshes on demand.
    storage: mapUrl === "disabled" ? undefined : createMemoryStorage({
      maximumTiles: positiveInteger(env.MOTO_MAP_CACHE_TILES, 300),
      maximumBytes: positiveInteger(env.MOTO_MAP_CACHE_MAX_BYTES, 64 * 1024 * 1024),
    }),
  });
  // Warm the PMTiles source without making the first rider wait for the manifest.
  void mapProvider?.initialize().catch(() => {});

  const handler = createGatewayHandler({
    provider,
    mapProvider,
    allowedOrigin: String(env.WEB_ORIGIN ?? ""),
    providerMode: mode,
    clientAddressOf: vercelClientAddress,
  });
  return { handler, mode, mapProvider };
}

// Vercel reuses a warm instance for consecutive invocations, so the gateway (and its
// in-memory tile cache and rate-limit buckets) is built once per instance.
let gateway;

export function getVercelGateway() {
  gateway ??= createVercelGateway(process.env);
  return gateway;
}
