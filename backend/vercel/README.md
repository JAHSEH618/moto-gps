> **语言 / Language:** 中文 · [English](README.en.md)

# Vercel Functions 部署

复用同一份 `backend/src/gateway.js`，只更换宿主。接口、协议、错误码与
[Node.js 服务器](../README.md)、[Cloudflare Workers + R2](../cloudflare/README.md)完全一致，
iOS / ESP32 不需要改动。

```text
iPhone → https://doupaw.site/api → Vercel Function（hkg1）
                                → 高德 Web 服务（搜索 / 路线 / 城市）
                                → Protomaps PMTiles（按需 Range 读取道路 / 建筑）
```

## 与另外两套宿主的差别

| | Node 服务器 | Cloudflare Workers | **Vercel** |
| --- | --- | --- | --- |
| 进程 | 常驻 | 请求时 | 请求时（温实例复用） |
| 适配层 | 无 | `cloudflare:node` 的 `handleAsNodeRequest` | `lib/vercel-handler.js`（`server.emit` 喂一次请求） |
| 瓦片缓存 | 磁盘 LRU（默认 1 GiB） | R2（跨实例持久） | 内存 LRU（默认 300 张 / 实例，**跨实例不共享**） |
| 限流 | 进程内计数 | 平台 Ratelimit 绑定 | 进程内计数（**每个实例各算各的**） |
| 地图源刷新 | 每日定时 + 命中即刷 | Cron + `waitUntil` | 冷启动时读取，无定时器 |
| 状态目录 | 需要可写目录 | 不需要 | 不需要（serverless 文件系统只读） |
| 接口前缀 | 自定，如 `/moto-gps/api` | `MOTO_BASE_PATH` | 固定 `/api` |

## 目录

```
backend/
  api/healthz.js                /api/healthz
  api/v1/places.js              /api/v1/places
  api/v1/routes.js              /api/v1/routes
  api/v1/route-options.js       /api/v1/route-options
  api/v1/map/cities.js          /api/v1/map/cities
  api/v1/map/tiles.js           /api/v1/map/tiles?z=&x=&y=
  lib/vercel-handler.js         适配器：把 Function 请求喂给同一个网关实例
  lib/vercel-app.js             按环境变量组装 provider / 地图 / 限流来源
  lib/memory-storage.js         内存瓦片缓存（实现与磁盘、R2 相同的异步存储契约）
  public/firmware/              OTA 镜像与 latest.json（Vercel 静态托管）
  vercel.json                   区域、时长、瓦片 rewrite、固件缓存头
  vercel/dev-server.js          本地预演，不需要 Vercel 账号
```

登录状态、Key 与用户搜索都不落盘。地图瓦片只在温实例内存里停留，随实例销毁。

## 1. 本地预演（不需要账号）

```sh
cd backend
npm ci
MOTO_PROVIDER=fixture MOTO_MAP_PMTILES_URL=disabled npm run vercel:dev
```

另一个终端：

```sh
curl --fail-with-body http://127.0.0.1:3000/api/healthz
curl --fail-with-body --get 'http://127.0.0.1:3000/api/v1/places' --data-urlencode 'keywords=济南西站'
curl --fail-with-body -H 'Content-Type: application/json' \
  --data-binary @fixtures/route-request-v1.json http://127.0.0.1:3000/api/v1/routes
curl -o /dev/null -w '%{http_code}\n' http://127.0.0.1:3000/api/v1/map/tiles/15/27044/12791
```

fixture 模式返回录制的高德响应、空搜索结果，并强制关闭周边地图：
瓦片应返回 `503 MAP_DISABLED`。要连真实地图源（仍然不需要高德 Key）：

```sh
MOTO_PROVIDER=fixture MOTO_MAP_PMTILES_URL=auto npm run vercel:dev
```

`dev-server.js` 会像 `vercel.json` 一样把 `/api/v1/map/tiles/15/x/y` 改写成带 query 的形式，
所以本地跑通就意味着线上那条 rewrite 也通。

## 2. 部署

### 控制台

1. 推送到 GitHub / GitLab，Vercel **Add New → Project** 选择该仓库。
2. **Root Directory 填 `backend`**。仓库 tracked 体积约 317 MB（固件、iOS 资源），
   而 `backend/` 只有 420 KB；不设这一项会白传几百 MB。
3. **Framework Preset 选 Other**，Build Command 与 Output Directory 都留空。
4. Environment Variables 添加（Production 与 Preview 都勾上）：
   - `MOTO_PROVIDER=amap`
   - `AMAP_WEB_SERVICE_KEY=<高德 Web 服务 Key>`
   可选：`MOTO_MAP_PMTILES_URL`、`WEB_ORIGIN`、`MOTO_MAP_CACHE_TILES`、`MOTO_MAP_CACHE_MAX_BYTES`，
   见 [`.env.vercel.example`](../.env.vercel.example)。
5. Deploy。

### 命令行

```sh
cd backend
npx vercel link
npx vercel env add AMAP_WEB_SERVICE_KEY production
npx vercel --prod
```

Key 必须是**高德开放平台的「Web 服务」类型 Key**（POI 搜索 + 驾车路线 + 行政区划），
不是 iOS SDK Key，也不是网页 JS Key。Key 只存在服务端环境变量里，不要写进仓库、Issue 或截图。

`MOTO_PROVIDER` 留空时：有 Key 就按 `amap` 运行，没 Key 就是 `disabled`。
`/healthz` 的 `provider` 字段始终报告最终生效的模式，不会悄悄降级成演示路线。

## 3. 验收（`/healthz` 不算数）

`/healthz` 只报告配置意图，不请求高德和地图源。以下每一条都要真的跑：

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

期望：

- `healthz`：`provider: amap`、`ready_for_live_navigation: true`、
  `capabilities.surrounding_map: true`、`map_source.cache.storage: "memory"`。
- 搜索返回真实地点；两条路线接口返回 `routes` / `route`；城市返回有效 `bounds_wgs84`。
- 瓦片 200 且含 `roads` / `buildings`；再次请求同一瓦片应命中内存缓存
  （`map_source.cache.tiles` 不再增长，`last_success_at` 非空）。

排错对照：

| 现象 | 先查 |
| --- | --- |
| 瓦片 404 | `vercel.json` 的 rewrite 是否随本次部署生效；函数是否叫 `api/v1/map/tiles.js` |
| 瓦片 503 `MAP_DISABLED` | `MOTO_PROVIDER` 不是 `amap`，且没显式设 `MOTO_MAP_PMTILES_URL` |
| 瓦片 503 `MAP_UPSTREAM_*` | 出网到 `build.protomaps.com` / 清单域名；换固定 `.pmtiles` 地址复测 |
| 搜索 503 `AMAP_*` | Key 类型、权限、配额、出口 IP 白名单 |
| 全部 503 `SERVER_MISCONFIGURED` | 环境变量没进 Production，或改完没重新部署 |
| POST 400 `INVALID_JSON` | 函数没收到请求体；用上面的 `--data-binary` 复测 |

### 如果 `/api/healthz` 直接返回 FUNCTION_INVOCATION_FAILED

这是 Vercel 的兜底错误页，不带任何信息。按顺序打两个探针，把输出贴出来即可定位：

```sh
curl -sS $BASE/ping     # 只证明运行时和 ESM 默认导出可用
curl -sS $BASE/diag     # 逐个 import，坏在哪个模块会自己报名
```

`diag` 只回显 Node 版本、区域、环境变量**是否存在**（不返回值）和每个模块的加载结果。
更细的堆栈在 Vercel 项目 → Deployments → 该部署 → Logs（适配器出错时会 `console.error`
打完整堆栈）。部署稳定后这两个文件可以删掉。

## 4. 域名与 HTTPS

`doupaw.site` 当前的 DNS 在 DNSPod（`leif.dnspod.net` / `mangnolia.dnspod.net`），
apex 没有 A 记录、`www` 是 NXDOMAIN，属于空域名，绑定不会冲突。

**A. 保留 DNSPod（推荐，改动最小）**

1. Vercel 项目 → **Settings → Domains** → 添加 `doupaw.site` 与 `www.doupaw.site`。
   面板会给出该项目的记录值（当前文档示例是 `CNAME cname.vercel-dns-0.com`，**以自己面板为准**）。
2. DNSPod 里按面板提示加 `A @` 与 `CNAME www`，删掉同名冲突记录。
3. 等状态变成 Valid，证书自动签发（Let's Encrypt，自动续期，不用 certbot）。
4. 在该域名设置里开启 **Redirect to …**（www → apex）和强制 HTTPS。

**B. DNS 交给 Vercel**

在注册商处把 NS 改为 Vercel 给的两个值（`ns1.vercel-dns.com` / `ns2.vercel-dns.com`），
记录全部在 Vercel 里维护。一次到位，但 DNSPod 上已有的 MX / TXT 要先迁过去。

验证：

```sh
curl -sSI https://doupaw.site/api/healthz | head -3     # 200，且不应跳回 http
dig +short doupaw.site                                  # Vercel 的地址，不再是 198.18.x
```

浏览器里 `http://doupaw.site` 应自动跳 `https://`。iOS 侧没有 ATS 例外，
`GatewayConfiguration` 只接受 `https://`，所以必须先把证书跑通再动 App。

## 5. 让 App 用上新网关

- 已安装的 App：首页 →「网关设置」填 `https://doupaw.site/api/`，保存即生效，不用重编。
- 源码构建：改 `platforms/ios/project.yml` 的 `MOTOGPSGatewayBaseURL`
  （注意和其它宿主不同，**末尾多一层 `/api/`**）。

ESP32 固件不需要改。

## 6. 固件 OTA 托管

`backend/public/` 是 Vercel 的静态站点根目录，所以 OTA 镜像和清单不需要再写一个
Function：

```text
https://doupaw.site/firmware/moto-gps.bin   ESP32 app 镜像
https://doupaw.site/firmware/latest.json   版本、URL、SHA-256、字节数
```

发布（在仓库根目录）：

```sh
node scripts/publish_firmware.mjs   --bin=platforms/esp32/build/moto_gps_esp32.bin
node scripts/publish_firmware.mjs --check
cd backend && npx vercel --prod
```

脚本只接受 1 MiB–7 MiB 的镜像（与 OTA 分区一致），计算 SHA-256，并把同一个
`moto-gps.bin` 与 `latest.json` 写进 `backend/public/firmware/`。仓库的 `.gitignore`
默认忽略 `*.bin`，这个目录有显式例外，所以 Vercel 从 Git 部署时拿得到文件。
`--check` 在 CI 或发布前确认清单没有落后于镜像。

验收：

```sh
curl -sS https://doupaw.site/firmware/latest.json
curl -sSI https://doupaw.site/firmware/moto-gps.bin   # 200 + application/octet-stream
```

App 的 05 固件 →「获取 doupaw.site 最新固件」会读取这个清单并填入下载地址与摘要；
圆屏通过手机热点用 HTTPS 下载，校验 SHA-256 后才切换启动分区。清单缓存 60 秒，
镜像同样 60 秒后重新验证，避免刚发布时长期命中旧文件。

## 7. 已知限制

- **没有共享缓存**：瓦片只在温实例内存里，冷实例要重新向 PMTiles 源取字节。
  骑行时同一实例基本都在服务，第一张瓦片慢、之后快；换区域或冷启动会再慢一次。
- **限流只在实例内有效**：并发扩容后每个实例各算各的。它能挡住误用和刷新循环，
  不能当成对公网 Key 的保护。真要防刷需要 Upstash Redis 之类的共享计数，本版没有做。
- **单一区域**：Vercel Hobby 只能一个 region，默认填 `hkg1`（香港）。
  改 `vercel.json` 的 `regions` 或在项目设置里调整。
- **冷启动**：首个请求要读 Protomaps 构建清单（实测 0.3–6 s 视网络），
  此后实例复用不再重复。
- **无定时任务**：地图源版本在冷启动时更新，可能比官方构建晚一天。
- **大陆访问**：Vercel 不在中国大陆境内，无 ICP 备案节点，跨境延迟和丢包均有。
  这是选 Vercel 的代价，不是可配置项。
- **Hobby 计划仅限个人非商业用途**；并发、时长、带宽上限以 Vercel 计划页为准。

## 8. 回滚

App 的「网关设置」改回原地址即可切换回 Node 或 Workers 部署，
服务端三套宿主可以并存，互不影响。
