> **Language:** English · [中文](README.md)

> English edition of the Chinese document. The Chinese file is authoritative if the two differ.

# Vercel Functions deployment

Same `backend/src/gateway.js`, different host. The API, protocol and error codes are identical to the
[Node.js server](../README.en.md) and the [Cloudflare Workers + R2 deployment](../cloudflare/README.en.md);
iOS and the ESP32 firmware need no changes.

```text
iPhone → https://doupaw.site/api → Vercel Function (hkg1)
                                → AMap Web Service (search / route / cities)
                                → Protomaps PMTiles (roads / buildings on demand, HTTP Range)
```

## How it differs from the other two hosts

| | Node server | Cloudflare Workers | **Vercel** |
| --- | --- | --- | --- |
| Process | long-running | per request | per request (warm instance reused) |
| Adapter | none | `handleAsNodeRequest` from `cloudflare:node` | `lib/vercel-handler.js` (`server.emit` feeds one request) |
| Tile cache | disk LRU (1 GiB default) | R2, shared across instances | in-memory LRU (300 tiles / instance, **not shared**) |
| Rate limiting | in-process | platform Ratelimit bindings | in-process (`**each instance counts separately**`) |
| Map source refresh | daily timer + refresh on hit | Cron + `waitUntil` | read on cold start, no timer |
| State directory | writable directory required | none | none (serverless filesystem is read-only) |
| Route prefix | yours, e.g. `/moto-gps/api` | `MOTO_BASE_PATH` | fixed `/api` |

## Layout

```
backend/
  api/healthz.js                /api/healthz
  api/v1/places.js              /api/v1/places
  api/v1/routes.js              /api/v1/routes
  api/v1/route-options.js       /api/v1/route-options
  api/v1/map/cities.js          /api/v1/map/cities
  api/v1/map/tiles.js           /api/v1/map/tiles?z=&x=&y=
  lib/vercel-handler.js         adapter: feeds the Function request to one gateway instance
  lib/vercel-app.js             builds provider / map / limiter identity from the environment
  lib/memory-storage.js         in-memory tile cache (same async storage contract as disk and R2)
  public/firmware/              OTA image and latest.json (Vercel static hosting)
  vercel.json                   region, duration, tile rewrite, firmware cache headers
  vercel/dev-server.js          local stand-in, no Vercel account needed
```

Sessions, the key and rider searches are never persisted. Tiles only live in the warm instance's heap
and disappear with it.

## 1. Local run (no account needed)

```sh
cd backend
npm ci
MOTO_PROVIDER=fixture MOTO_MAP_PMTILES_URL=disabled npm run vercel:dev
```

In another terminal:

```sh
curl --fail-with-body http://127.0.0.1:3000/api/healthz
curl --fail-with-body --get 'http://127.0.0.1:3000/api/v1/places' --data-urlencode 'keywords=济南西站'
curl --fail-with-body -H 'Content-Type: application/json' \
  --data-binary @fixtures/route-request-v1.json http://127.0.0.1:3000/api/v1/routes
curl -o /dev/null -w '%{http_code}\n' http://127.0.0.1:3000/api/v1/map/tiles/15/27044/12791
```

Fixture mode replays a recorded AMap response, returns empty search results and forces the surrounding
map off, so the tile route answers `503 MAP_DISABLED`. To reach the real map source (still no AMap key):

```sh
MOTO_PROVIDER=fixture MOTO_MAP_PMTILES_URL=auto npm run vercel:dev
```

`dev-server.js` rewrites `/api/v1/map/tiles/15/x/y` into the query form exactly like `vercel.json`
does, so a passing local run means the deployed rewrite also reaches the function.

## 2. Deploy

### Dashboard

1. Push to GitHub / GitLab, then **Add New → Project** in Vercel and pick the repository.
2. Set **Root Directory to `backend`**. The tracked repository is about 317 MB (firmware, iOS assets)
   while `backend/` is 420 KB; skipping this uploads hundreds of megabytes for nothing.
3. Choose framework preset **Other**; leave Build Command and Output Directory empty.
4. Add environment variables for Production and Preview:
   - `MOTO_PROVIDER=amap`
   - `AMAP_WEB_SERVICE_KEY=<AMap Web Service key>`
   Optional: `MOTO_MAP_PMTILES_URL`, `WEB_ORIGIN`, `MOTO_MAP_CACHE_TILES`, `MOTO_MAP_CACHE_MAX_BYTES`,
   see [`.env.vercel.example`](../.env.vercel.example).
5. Deploy.

### CLI

```sh
cd backend
npx vercel link
npx vercel env add AMAP_WEB_SERVICE_KEY production
npx vercel --prod
```

The key must be an AMap **Web Service** key (POI search + driving route + district search), not an
iOS SDK key and not a web JavaScript key. Keep it in server environment variables only — never in the
repository, an issue or a screenshot.

When `MOTO_PROVIDER` is unset, a present key means `amap` and a missing key means `disabled`.
The `provider` field of `/healthz` always reports the mode that actually took effect, so the gateway
never silently falls back to a demo route.

## 3. Acceptance (`/healthz` does not count)

`/healthz` reports configuration intent; it does not call AMap or the map source. Run every command:

```sh
BASE=https://YOUR-DOMAIN/api

curl --fail-with-body $BASE/healthz
curl --fail-with-body --get $BASE/v1/places --data-urlencode 'keywords=济南西站'
curl --fail-with-body -H 'Content-Type: application/json' \
  --data-binary @fixtures/route-request-v1.json $BASE/v1/routes
curl --fail-with-body -H 'Content-Type: application/json' \
  --data-binary @fixtures/route-request-v1.json $BASE/v1/route-options
curl --fail-with-body --get $BASE/v1/map/cities --data-urlencode 'keywords=历下区'
curl --fail-with-body -o /tmp/tile.json -w 'HTTP %{http_code}\n' $BASE/v1/map/tiles/15/27044/12791
```

Expected:

- `healthz`: `provider: amap`, `ready_for_live_navigation: true`,
  `capabilities.surrounding_map: true`, `map_source.cache.storage: "memory"`.
- Search returns real places; both route endpoints return `routes` / `route`; cities return valid
  `bounds_wgs84`.
- The tile returns 200 with `roads` / `buildings`; asking for the same tile again hits the in-memory
  cache (`map_source.cache.tiles` stops growing, `last_success_at` becomes non-null).

Troubleshooting:

| Symptom | Check first |
| --- | --- |
| Tile 404 | Did this deployment pick up `vercel.json`? Is the function `api/v1/map/tiles.js`? |
| Tile 503 `MAP_DISABLED` | `MOTO_PROVIDER` is not `amap` and `MOTO_MAP_PMTILES_URL` is unset |
| Tile 503 `MAP_UPSTREAM_*` | Egress to `build.protomaps.com` / the manifest host; retry with a fixed `.pmtiles` URL |
| Search 503 `AMAP_*` | Key type, permissions, quota, egress IP allowlist |
| Everything 503 `SERVER_MISCONFIGURED` | Variables missing from Production, or changed without redeploying |
| POST 400 `INVALID_JSON` | The function received no body; retest with the `--data-binary` command above |

## 4. Domain and HTTPS

`doupaw.site` currently delegates to DNSPod (`leif.dnspod.net` / `mangnolia.dnspod.net`), its apex has
no A record and `www` is NXDOMAIN, so binding it to a project conflicts with nothing.

**A. Keep DNSPod (recommended, smallest change)**

1. Vercel project → **Settings → Domains**, add `doupaw.site` and `www.doupaw.site`. The dashboard
   shows the values for your project (the current docs example is `CNAME cname.vercel-dns-0.com`;
   **trust your own dashboard**).
2. Add the `A @` and `CNAME www` records in DNSPod and remove conflicting ones.
3. Wait for Valid; the certificate is issued automatically (Let's Encrypt, auto-renewed, no certbot).
4. Enable **Redirect to …** (www → apex) and the HTTPS redirect for the domain.

**B. Hand DNS to Vercel**

Change the nameservers at the registrar to the two values Vercel provides
(`ns1.vercel-dns.com` / `ns2.vercel-dns.com`) and keep all records in Vercel. Cleaner, but existing
MX / TXT records must be migrated first.

Verify:

```sh
curl -sSI https://doupaw.site/api/healthz | head -3     # 200, and no http:// redirect back
dig +short doupaw.site                                  # Vercel addresses, not 198.18.x
```

`http://doupaw.site` should redirect to `https://` in a browser. The iOS app has no ATS exception and
`GatewayConfiguration` accepts `https://` only, so the certificate must work before pointing the app at it.

## 5. Point the app at the new gateway

- Installed app: home screen → **Gateway settings**, enter `https://doupaw.site/api/`. Takes effect
  immediately, no rebuild.
- Source build: change `MOTOGPSGatewayBaseURL` in `platforms/ios/project.yml`. Unlike the other hosts,
  the base URL **keeps the extra `/api/` segment**.

The ESP32 firmware needs no change.

## 6. Firmware OTA hosting

`backend/public/` is Vercel's static root, so the OTA image and its manifest need no Function:

```text
https://www.doupaw.site/firmware/moto-gps.bin  ESP32 app image
https://www.doupaw.site/firmware/latest.json   version, URL, SHA-256, size
```

What gets published is the release build: `platforms/esp32/sdkconfig.release` on top of
`sdkconfig.defaults`, which sets `CONFIG_MOTO_TLOG_DISABLE=y` so the round screen stops writing
the tlog partition. The bench build in `build/` keeps recording and is for USB flashing only.
Commit first and build from a clean tree, or the image and the manifest version both carry
`-dirty`. From the repository root:

```sh
rm -f platforms/esp32/build-release/sdkconfig   # a saved sdkconfig overrides the overlay
idf.py -C platforms/esp32 -B platforms/esp32/build-release \
  -DSDKCONFIG=build-release/sdkconfig \
  -DSDKCONFIG_DEFAULTS='sdkconfig.defaults;sdkconfig.release' \
  reconfigure build
node scripts/publish_firmware.mjs
node scripts/publish_firmware.mjs --check
cd backend && npx vercel --prod
```

The script reads `build-release/moto_gps_esp32.bin` by default and checks the
`config/sdkconfig.h` the same build directory generated: a bench image without
`CONFIG_MOTO_TLOG_DISABLE` is refused. It accepts only 1 MiB–7 MiB images (the OTA slot), computes the SHA-256, and writes
both `moto-gps.bin` and `latest.json` into `backend/public/firmware/`. The repository ignores
`*.bin` by default; this directory carries an explicit exception so a Git deployment uploads
the file. `--check` catches a manifest that is behind the image, in CI or before a release.

Verify:

```sh
curl -sS https://www.doupaw.site/firmware/latest.json
curl -sSI https://www.doupaw.site/firmware/moto-gps.bin   # 200 + application/octet-stream
```

The app's 05 Firmware → "Get latest from doupaw.site" reads the manifest and fills the URL and
digest. The round screen downloads over the phone hotspot with HTTPS, verifies SHA-256, and only
then switches the boot partition. The manifest is cached for 60 s; the image is revalidated after
60 s so a fresh release does not stay hidden behind an old edge copy.

## 7. Known limitations

- **No shared cache.** Tiles live in one warm instance; a cold instance re-reads the PMTiles source.
  A ride keeps one instance busy, so the first tile is slow and later ones are fast; a new area or a
  cold start pays again.
- **Rate limiting is per instance.** After scaling out, every instance counts separately. It stops
  runaway loops and mistakes, not a public key being abused. Real protection needs a shared counter
  such as Upstash Redis, which this version does not add.
- **Single region.** Vercel Hobby allows one region; `hkg1` (Hong Kong) is configured in
  `vercel.json`. Adjust in `regions` or in project settings.
- **Cold start.** The first invocation reads the Protomaps build manifest (0.3–6 s depending on the
  network); reused instances do not repeat it.
- **No scheduler.** The map source revision refreshes on cold start, so it can trail the official
  build by a day.
- **Mainland China access.** Vercel has no mainland region and no ICP filing; cross-border latency and
  packet loss apply. That is the price of choosing Vercel, not a setting.
- **Hobby is for personal, non-commercial use.** Concurrency, duration and bandwidth limits follow the
  Vercel plan page.

## 8. Rollback

Change the in-app gateway address back to the previous value. The Node, Workers and Vercel hosts can
run side by side without affecting each other.
