# 微雪 1.75C：性能优化、WiFi 与新功能方案

> 2026-10-27。依据：微雪官方仓库 `waveshareteam/ESP32-S3-Touch-AMOLED-1.75C`（提交 `4bfdc80`，2026-10-20）的 README、5 个 ESP-IDF 示例、原理图，以及本仓库当前固件与 iOS 代码。
> 文中“估算”均来自代码常量推算，未经实测的数字都标了出来；上板以 `ui perf`、`imu stats`、慢动作视频为准。

## 0. 结论

| 优先级 | 事项 | 一句话 |
|---|---|---|
| **P0** | 停车时手机转动→圆屏航向延迟 | 当前链路估算均值约 380 ms：5 Hz 节流 + 两级一阶平滑 + 两个不同步的 25 ms 时钟。三阶段可压到约 100 ms（§2） |
| P0 | 小地图 GTA 化 | **本次已改**：炭灰雷达底盘、按真实比例加粗的分级街道、硬黑描边、更宽的路线带（§6）。需上板复核帧率 |
| P1 | 帧节拍与 TE 对齐、LVGL 双核绘制、任务绑核 | 官方示例都开了 `LV_OS_FREERTOS` + 2 个绘制单元；本项目还是单线程 `LV_OS_NONE`（§3） |
| P1 | WiFi：连 iPhone 个人热点 | ESP32 做 STA，凭据走已加密绑定的 BLE 下发；WiFi 只做“数据面”（OTA、tlog 下载、地图批量），实时链路仍是 BLE（§4） |
| P2 | 新功能 | ANCS 来电/通知、CTS 读手机墙钟、防烧屏与静止降亮（DESIGN_V2 §2.6 已规定但未实现）、IMU 车动唤醒、防盗报警、外接 GNSS/磁力计（§5） |

## 1. 硬件盘点：官方资料对照本项目

### 1.1 板上有什么、用了什么

| 器件 / 接口 | 官方资料 | 本项目现状 |
|---|---|---|
| ESP32-S3，32 MB Flash，8 MB 八线 PSRAM | README | 已用：6 MB ×2 OTA 槽 + 16 MB tlog 分区 |
| CO5300 QSPI AMOLED 466×466 | README、BSP | 已用：自管初始化，TE 同步（GPIO13） |
| CST9217 触摸 | BSP | 已用 |
| QMI8658 六轴 | 原理图：`QMI_INT1` 网络标号接 **GPIO21** | 已用（轮询/锁存链路）；**中断脚未用** |
| AXP2101 电源 | 示例 01 | 已用：长按关机、电量环 |
| ES8311 编解码 + 功放（`PA_CTRL` = GPIO46）+ 喇叭 | 原理图、BSP `bsp_audio_codec_speaker_init()` | **未用** |
| ES7210 双数字麦 | 示例 05 频谱分析 | **未用** |
| 2.4 GHz WiFi | 芯片自带 | **未用**（sdkconfig 里 WiFi 组件默认开启但无代码） |
| 扩展排针：GPIO17/18/39/40/41/42、U0TXD/U0RXD、I2C SDA/SCL | 原理图 GPIO 表“OUT”列 | **未用** |
| RTC PCF85063、TCA9554、TF 卡 | 1.75C 不贴（上板扫描 0x51/0x20 无应答，BSP `BSP_CAPS_SDCARD 0`） | — |

> 原理图 GPIO 表把 `QMI_INT1` 画在 GPIO38 那一行，但 GPIO38 是屏幕 QSPI 时钟（BSP `BSP_LCD_PCLK`），QMI8658 旁的网络标号是 GPIO21。以网络标号为准，用之前上板确认。GPIO21 是 RTC GPIO，可以做深睡唤醒源。

### 1.2 官方示例配置 vs 本项目

| 配置 | 官方示例（02/03/05） | 本项目 | 说明 |
|---|---|---|---|
| `LV_USE_OS` / 绘制单元 | `LV_OS_FREERTOS`，`DRAW_UNIT_CNT=2`（04 用 1） | `LV_OS_NONE`，单线程 | §3.2 |
| 刷新周期 | `LV_DEF_REFR_PERIOD=15` | 25 ms | §3.1 |
| QSPI 时钟 | `esp_lcd_co5300` 默认 40 MHz | 30 MHz（40 MHz 时 PSRAM 直连 DMA 欠载） | §3.3 |
| 传输队列 | BSP 默认 10 | 1（刻意串行） | 保持 |
| 绘制缓冲 | 50 行 ×2，PSRAM | 全高 466 行 ×2，PSRAM 直连 DMA | 全高是为减少地图重复遍历，保持 |
| `LV_ATTRIBUTE_FAST_MEM_USE_IRAM` | 02/04 开 | 未开 | 热点混合函数进 IRAM，代价是内部 RAM |
| 样式缓存 / C 库内存函数 | `LV_OBJ_STYLE_CACHE`、`LV_USE_CLIB_*` | 内置 | 小幅 CPU 收益 |
| PSRAM | 80 MHz，`XIP_FROM_PSRAM` | 80 MHz，`FETCH_INSTRUCTIONS`+`RODATA` | IDF 5.5.5 里两者等价，无需改 |

**不建议**：八线 PSRAM 120 MHz。IDF 把它标为实验特性：上电后温度变化约 20 °C 就可能随机崩溃。车把在太阳下正是这种工况。

## 2. P0：手机转动 → 圆屏航向延迟

今天的提交 `e45e1e5` 已修两处（罗盘事件触发重发；设备端改为 80 ms 滑移），提交说明写明**尚未上板**。先刷机实测，再按下面的阶段推进。

### 2.1 延迟预算（停车、手机匀速转动，估算）

| 环节 | 代码位置 | 机制 | 估算 |
|---|---|---|---|
| iPhone 罗盘 | `CoreLocationNavigationSource.swift:51–55` | `CLHeading`，`headingFilter = 1`，系统内部滤波 | 50–150 ms（待测） |
| 发送节流 | `ESP32BLECentral.swift:241`（200 ms）、`compassHeadingDidChange()` | 罗盘事件重发上一帧快照，最快 5 Hz | 0–200，均值约 100 ms |
| BLE 传输 | 15 ms 分片节拍；外设未申请连接参数 | `NavigationSnapshot` 多分片 | 30–60 ms |
| 设备航向滑移 | `motion_heading_fusion.hpp` `kCompassSlewTauMs = 80` | 一阶滤波；斜坡输入稳态滞后 ≈ τ | 约 80 ms |
| 运动帧节拍 | `phone_nav_bridge.cpp` `on_imu_sample()` | 25 ms 相位呈现 | 均值约 12 ms |
| **UI 点位二次平滑** | `moto_nav_ui_gta.cpp` `route_motion_tick()`，每 25 ms 混合 0.39 | 又一级一阶滤波，τ ≈ 50 ms | 约 50 ms |
| LVGL 刷新 | `config/lv_conf.h` 25 ms | 与运动帧节拍互不同步 | 均值约 12 ms |
| 渲染 + 等 TE + QSPI | `board_port_waveshare_1_75c.cpp` | 渲染 11–13 ms；整盘 434 KB ÷ 15 MB/s ≈ 29 ms | 约 45 ms |
| **合计** | | | **均值约 380 ms，最差约 600 ms** |

直接操作类的跟手感，一般要压到 100 ms 左右才不觉得“拖”。

### 2.2 修复分三阶段

**A：只改固件（1–2 天，风险低）**

1. 罗盘驱动的纯旋转帧跳过 UI 的第二级平滑：融合层已经在滑移，`route_motion_tick()` 对这类帧直接落到目标（blend = 1）。预计 −40~50 ms。
2. `kCompassSlewTauMs` 80 → 40 ms（B 阶段采样到 20 Hz 后可再降到 30 ms）。预计 −40 ms。
3. 渲染任务应用运动帧后调用 `esp_lv_adapter_refresh_now()`，不等下一个 25 ms 定时器。预计 −12 ms 均值。

**B：手机 + 协议（3–5 天）**

1. iOS 停车罗盘改用 `CMMotionManager.startDeviceMotionUpdates(using: .xTrueNorthZVertical)`，50–100 Hz，陀螺融合，延迟远低于 `CLHeading`；从姿态矩阵取航向和偏航角速度，`CLHeading` 作回退。
2. 新增单分片小消息 `CompassSample`（`heading_cdeg u16`、`yaw_rate_cdps i16`、`sample_age_ms u8`），20–25 Hz，不占 5 Hz 快照额度，用新能力位协商；C++/Swift 双端 golden fixture。冻结的 v1 快照不动。
3. 设备端用角速度外推到“预计上屏时刻”：`shown = heading + rate × min(age + 管线延迟, 150 ms)`，静止时加死区防抖，取代纯低通。

**C：链路参数**

外设主动申请 15–30 ms 连接间隔（`ble_gap_update_params`，按 Apple 配件设计指南取值）、请求 2M PHY（NimBLE 已编译 `LE_2M_PHY`，代码未请求）和 DLE。同时提升地图窗口吞吐。

**预期**：A 后约 250 ms；A+B+C 约 80–120 ms。

**测量**：手机和圆屏并排，用 240 fps 慢动作录像，数“手机开始转”到“圆屏开始转”的帧数；B 阶段起把 `CompassSample` 到达时间写进 tlog。

## 3. 性能优化

### 3.1 帧节拍对齐 TE（P1，先做实验）

- 现状：刷新 25 ms（40 Hz），CO5300 的 TE 一般约 16.7 ms（以开机日志 `CO5300 TE locked: period=` 为准）。每帧首次 flush 要等下一个 TE 沿，于是出帧间隔在 16.7/33.3 ms 之间交替，转弯时会有不均匀的顿挫。整盘变化时 30 MHz QSPI 一帧要传约 29 ms，本来就到不了 40 fps。
- 方案：地图类整盘运动页改为 2 个 TE 周期（33.3 ms，均匀 30 fps），或改成 TE 驱动出帧；倾角页这类局部刷新可以试 16.7 ms。要同步改 `kRouteMotionFrameMs` 的 `static_assert`、桥接层 25 ms 节拍、demo tick 和插值系数。
- `lv_conf.h` 注释里明确拒绝过“把 40 Hz 降到 30 Hz”，所以这里是 A/B 实验：慢动作录像对比 40 Hz 不均匀与 30 Hz 均匀，哪个看起来更顺再定。

### 3.2 LVGL 双核绘制 + 任务绑核（P1）

- 现状：`LV_USE_OS LV_OS_NONE`，一个核渲染；IMU 任务（优先级 7）和 LVGL 适配任务（6）都没绑核（`xTaskCreate`、`task_core_id = -1`），NimBLE 主机和控制器固定在核 0。DESIGN_V2 §13 规定 imu/lvgl 在核 1、nimble/logger 在核 0，代码没落实。
- 方案：`lv_conf.h` 里只对 `ESP_PLATFORM` 开 `LV_OS_FREERTOS` + `LV_DRAW_SW_DRAW_UNIT_CNT 2`（Web 仍是 `LV_OS_NONE`，输出像素不变）；绘制线程优先级低于 IMU 和 BLE 接收；LVGL 与 IMU 绑核 1。官方 02/03/05 示例在同一 BSP 3.0 + 适配器上就是这么配的。
- 收益：复杂帧（首次绑定地图约 0.5 s 那一帧、凹多边形建筑扫描线）明显变快；本次加粗街道后的笔画开销也有余量。上板看 `ui perf` 和 `imu stats` 的 missed/lockwait 有没有变差。

### 3.3 QSPI 30 → 40 MHz（P2）

- 现状：40 MHz 时 GDMA 从 PSRAM 读帧缓冲、CPU 同时往 PSRAM 画，出现 `DMA TX underflow`，所以降到 30 MHz。
- 方案：把 DMA 源换成内部 RAM 条带缓冲，或在 PSRAM 方案下只对局部刷新页试 40 MHz。内部堆现在空闲 96 KiB、最低 47 KiB，条带缓冲和 WiFi 会抢同一块内存，要先做 §3.6。
- 收益：整盘传输 29 → 22 ms，降延迟；30 fps 的 TE 量化不变，要上 60 fps 整盘刷新得低于 16.7 ms。

### 3.4 绘制层小优化（P3，逐项实测）

| 项 | 做法 | 备注 |
|---|---|---|
| S3 SIMD 混合 | LVGL 9.5 `LV_USE_DRAW_SW_ASM = LV_DRAW_SW_ASM_CUSTOM`，移植 esp_lvgl_port 的 S3 PIE 填充/RGB565 混合 | 适配器没带；主要加速实色填充 |
| 热点函数进 IRAM | `LV_ATTRIBUTE_FAST_MEM_USE_IRAM` | 代码现在从 PSRAM 经 32 KB I-cache 执行；占内部 RAM |
| 样式缓存、C 库 memcpy | `LV_OBJ_STYLE_CACHE`、`LV_USE_CLIB_*` | 共享 `lv_conf.h`，Web 同步 |
| 只传圆内像素 | flush 时按弦长分带，省约 15–20% QSPI 字节 | 子矩形不连续要拷贝，复杂度高 |

### 3.5 Flash 与日志

tlog 已经做成运行时不擦除、只写预擦扇区，4 KB 页写在传感器任务里。若 `imu stats` 的 `flashgaps` 不为零，再把页写挪到核 0 的低优先级写盘任务（双缓冲）。

### 3.6 内存（WiFi 的前置条件）

- 内部堆：空闲 96 KiB、最低 47 KiB（status.md 2026-10-27 01:30）。最大的内部静态对象是 `phone_bridge` 25,208 B，计划中要挪 PSRAM。
- 发布前删掉 `app_main.cpp` 里的 `HEAPPROBE` 和台架命令（TODO 已列）。

### 3.7 功耗与 AMOLED（P2）

- `CONFIG_PM_ENABLE` 未开。停车且无运动时可以降刷新、降亮度（CO5300 `0x51`，`esp_lcd_panel_co5300_set_brightness`），息屏用 `esp_lv_adapter_pause()` / `sleep_prepare()`。
- DESIGN_V2 §2.6 规定的“120 s ±2 px 轨道偏移、静止 5 min 降亮 30%、15 min 息屏（导航中除外）”在代码里搜不到实现，属于 AMOLED 寿命问题，建议尽早做。
- AMOLED 功耗随点亮像素和亮度增长。Liberty Day 是整盘浅色纸面，电池供电时比夜间主题耗电得多，写进使用说明。

## 4. WiFi：连手机热点

### 4.1 原则

TODO 已定：“不依赖手机热点维持终端连接”。所以 **BLE = 控制面 + 实时数据**（导航快照、罗盘、触摸、AMS），**WiFi = 按需打开的数据面**，用完就关；没有 WiFi 时一切照常。

### 4.2 拓扑选择

| 方案 | 谁连谁 | 适合 | iOS 限制 |
|---|---|---|---|
| **A. iPhone 个人热点（推荐）** | 圆屏作 STA 连手机热点 | 骑行中/出发前 OTA、地图批量、日志上传 | 热点只能用户手动开；iPhone 12 及以后要打开“最大兼容性”才是 2.4 GHz；无设备连接一段时间后热点会自动关闭（时长随 iOS 版本，需实测）；网段 `172.20.10.0/28`，手机是 `172.20.10.1` |
| B. 圆屏 SoftAP | 手机加入圆屏的 AP | 车库/离线：下载 16 MB tlog、上传固件、网页设置 | App 内自动加入要 `NEHotspotConfiguration`（Hotspot 能力，个人免费签名大概率不可用）；免授权做法：圆屏显示 `WIFI:` 二维码，用系统相机扫码入网 |
| C. 家里 WiFi | 圆屏连家用路由 | 插电充电时自动检查 OTA、同步日志 | 无 |
| D. ESP-NOW | 圆屏之间直连 | 组队骑行“队友雷达” | 不经过手机；必须固定信道，与连热点互斥 |

### 4.3 方案 A 的流程

1. 手机：设置 → 个人热点 → 打开“允许其他人加入”和“最大兼容性”。
2. App → 设备 → WiFi：填热点名和密码（iOS 不给 App 读自己的热点名和密码，只能手填一次）。
3. App 通过**已加密、已绑定**的 BLE 链路发 `WifiConfig{ssid, psk, policy}`（新消息类型 + 新能力位）；设备存 NVS，日志里不打印密码。
4. 有任务时（OTA、tlog、地图预取）设备才 `esp_wifi_start()` 连接，阈值设 WPA2-PSK，拿到 DHCP 后用 BLE 回报 `WifiStatus{state, ip, rssi}`；App 直接连这个 IP，不需要 Bonjour。
5. 设备端 HTTP 服务每次会话用 BLE 下发的随机令牌鉴权（同一热点下的其他设备也能访问这个 IP）。
6. 任务结束 `esp_wifi_stop()` / `deinit()`，归还内存和功耗。

iOS 侧：`Info.plist` 加 `NSLocalNetworkUsageDescription`（首次访问局域网会弹权限），用 `NWConnection` 连设备 IP。

### 4.4 用途

| 用途 | 做法 | 收益 |
|---|---|---|
| **OTA** | 手机经 BLE 下发固件 URL + SHA-256，设备经热点用 `esp_https_ota` 下载到另一 OTA 槽；开 `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE`，新固件完成一次 BLE 握手才标记有效 | 现在必须 USB + ESP-IDF；固件约 5.1 MB，BLE 传要几分钟，WiFi 十几秒级 |
| **tlog 下载** | `GET /tlog?from=&to=` 分块读分区；手机或电脑直接拿 | 取代 `read_flash` + 主机脚本 |
| **地图批量** | 手机经 TCP 推更大半径（如 2 km）的道路/建筑/水面/绿地，设备缓存到 PSRAM 或 3 MB `assets` 分区，自己按位置取窗口 | BLE 不再持续推窗口，实时消息更快；为水/绿地图层铺路 |
| 骑行记录 | 高频 IMU 原始数据会后同步到手机分析 | 解开 TODO 里 `MotoRideCore` 接入 |
| 网页控制台（方案 B） | `esp_http_server`：设置、诊断、固件上传 | 车库调试不用 USB |

### 4.5 资源与共存

| 项 | 设置 |
|---|---|
| WiFi/LwIP 缓冲放 PSRAM | `CONFIG_SPIRAM_TRY_ALLOCATE_WIFI_LWIP=y`（当前未开） |
| TLS 内存放 PSRAM | `CONFIG_MBEDTLS_EXTERNAL_MEM_ALLOC=y` |
| 静态接收缓冲 | `CONFIG_ESP_WIFI_STATIC_RX_BUFFER_NUM` 10 → 4–6 |
| IRAM 不够时 | 关 `ESP_WIFI_IRAM_OPT` / `RX_IRAM_OPT`（降吞吐） |
| 共存 | `CONFIG_ESP_COEX_SW_COEXIST_ENABLE` 已开；上板测 WiFi 传输时 5 Hz 快照、AMS 和触摸回传有没有变慢 |
| 回滚 | `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE` 当前未开 |

### 4.6 分阶段

| 阶段 | 内容 | 验收 |
|---|---|---|
| W0 | 内存腾挪（§3.6）+ 上述 sdkconfig | 开 WiFi 后内部堆最低值仍有余量，BLE 连接与地图帧率不变 |
| W1 | BLE 下发凭据、按需连热点、回报 IP/RSSI | 连上 iPhone 热点并在 App 显示 IP；断开后 BLE 不受影响 |
| W2 | tlog HTTP 下载 + 会话令牌 | 16 MB 下载完成并通过 CRC/回放 |
| W3 | OTA + 回滚 + 哈希校验 | 故意刷坏的镜像能自动回滚 |
| W4 | 地图批量 + 设备端取窗口 | BLE 地图消息量下降，罗盘/快照延迟不升 |
| W5 | SoftAP + 二维码车库模式 | 相机扫码入网，网页可下载日志、上传固件 |
| W6 | ESP-NOW 队友雷达 | 两台设备互相显示 blip，过期即消失 |

## 5. 还能做的功能（按性价比）

| # | 功能 | 依赖 | 工作量 | 说明 |
|---|---|---|---|---|
| F1 | **ANCS 来电/通知** | 已绑定 BLE，复用 AMS 客户端写法 | 中 | 停车时显示来电人并可接/拒；行驶中只显示应用图标，不显示正文 |
| F2 | **CTS 读手机墙钟** | iOS 向已连接（通常需绑定）的外设提供 Current Time Service | 小 | tlog 有绝对时间；日夜/亮度可按太阳高度角。需上板确认能读到 |
| F3 | **防烧屏 + 静止降亮** | CO5300 亮度命令 | 小–中 | DESIGN_V2 §2.6 已规定，未实现 |
| F4 | **车动唤醒、停车深睡** | QMI8658 运动唤醒 → `QMI_INT1`（GPIO21，RTC） | 中 | 现在深睡只能按 PWR 唤醒；先上板确认引脚 |
| F5 | 防盗报警 | F4 + ES8311 喇叭（GPIO46 功放） | 中 | 停车布防后被挪动：喇叭报警、BLE 通知手机、记 tlog 事件 |
| F6 | 提示音 | ES8311 + `assets` 分区 PCM | 小–中 | 转向前提示、低电量、充电；车速高时喇叭基本听不见，偏向电动车低速场景 |
| F7 | 外接 GNSS | 扩展排针 UART（GPIO17/18） | 中–大 | 解开 DESIGN_V2 推迟的 TRACK 模式/圈速、10 Hz 0–100 |
| F8 | 外接磁力计 | 扩展排针 I2C | 中 | 停车真北航向不再依赖手机罗盘，从根上解决 §2 的停车场景；需硬铁/软铁校准，远离电机和电池 |
| F9 | 队友雷达 | ESP-NOW（§4 W6） | 中 | 用 GTA 彩色方块 blip 显示队友，出圈贴边 |
| F10 | 语音命令 | ES7210 双麦 + ESP-SR | 大 | 风噪大，仅停车可用，占 PSRAM 与 CPU，优先级最低 |

## 6. 小地图（本次已改）

改动只在导航页地图层，文件 `shared/nav_ui/src/moto_nav_ui_gta.cpp`，规格写进 `DESIGN_V2.md` §4“雷达底盘”。

| 层 | 改前 | 改后 |
|---|---|---|
| 地面 | 夜 `#161616` 近乎全黑 | 夜 `#222222`（DESIGN.md 夜间雷达街区炭灰）；昼保持 `#4E4E4A` 灰雷达 |
| 街道宽度（设计 px） | 5/4/4/3/2/2 | 8/7/6/5/3/3，按 0.57 px/m 的真实路宽 |
| 街道描边 | 两侧各 1.5 px，夜里几乎看不见 | 两侧各 2 px 黑描边，在炭灰底上清楚，路口一次合并 |
| 街道色阶 | 主干道与数字同为奶白 | 整体低一档，数字永远最亮；六级仍各不相同 |
| 路线 | 6 d 橙 / 13 d 黑边 | 9 d 橙 / 15 d 黑边，比任何街道都宽 |
| 建筑 | 与天际线共用色槽 | 独立 `map_block` 色槽：夜间抬起、白天压下 |

验证：原生测试 17/17 通过（本地 `tests/` 的颜色常量同步更新）；固件编译通过，`moto_gps_esp32.bin` 0x50b900，6 MB 槽剩 16%。非导航页的评审帧与改前逐像素一致（夜间连接封面仅画布最外圈 235 个玻璃外抗锯齿像素差一个色阶）。

待办：

1. 上板 `ui page nav` + `ui perf`，在 36 条路/24 栋楼的真实窗口下确认导航页 ≥27 fps（笔画变宽会增加绘制量）。
2. 手机端“显示建筑”默认关（`AppModel.swift:39`）。GTA 的城市块感很依赖建筑层，要不要默认打开由你决定。
3. 水面/绿地面要素（TODO 已列）是 GTA 雷达最强的识别点之一，需要协议加面要素和手机端提取。
4. 目的地在雷达外时贴边显示 blip（同安全屋的做法），需要协议带目的地方位。

## 7. 建议顺序

| 里程碑 | 内容 |
|---|---|
| M0（本周） | 刷入本次小地图 + e45e1e5，实测航向延迟基线；航向 A 阶段；`ui perf` 复核 |
| M1 | 航向 B/C 阶段（CMDeviceMotion、`CompassSample`、连接间隔/2M PHY） |
| M2 | 双核绘制、绑核、TE 节拍实验、40 MHz QSPI 实验 |
| M3 | 防烧屏/降亮、CTS、ANCS |
| M4 | WiFi W0–W3（热点、tlog、OTA） |
| M5 | 车动唤醒、防盗、提示音；按需 W4–W6 与外接 GNSS/磁力计 |
