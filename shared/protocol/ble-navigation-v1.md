> **语言 / Language:** 中文 · [English](ble-navigation-v1.en.md)

# Moto BLE Navigation Protocol v1

状态：实现基线。规范中的常量、编码器、解码器和重组器位于
`shared/ble_protocol`，黄金字节位于
`shared/protocol/fixtures/ble-navigation-v1.golden.txt`。

## 1. 目标与职责边界

手机负责定位、高德路线、路线匹配、交通刷新与偏航重算。ESP32 只接收一个
确定性的显示投影，适配为共享 `moto::nav::NavSnapshot` 后交给
`NavPresenter`。ESP32 不再次运行高德、路线匹配或偏航状态机，也不得把 BLE
字段直接写入 LVGL。

推荐的数据路径是：

```text
iOS 定位/高德/偏航状态机
  -> NavigationSnapshot + RouteGeometry + TrafficDeviation
  -> MapScene（离线道路/建筑滚动窗口，可选）
  -> PhoneToDevice 特征
  -> v1 帧解码与重组
  -> Display Snapshot adapter
  -> moto::nav::NavSnapshot
  -> NavPresenter
  -> moto_nav_ui / LVGL

触摸 / 音乐按键
  -> DeviceCommand
  -> DeviceToPhone 特征
  -> iOS 导航页面或媒体动作
```

`NavigationSnapshot` 是显示快照，不是 `NavEvent`。路线几何只包含当前
heading-up 画面所需的局部窗口，最多 24 点，与
`nav::kRouteViewPointCapacity` 一致。手机仍保留完整 Provider 路线。

## 2. GATT 契约

ESP32 是 peripheral/GATT server，手机是 central/GATT client。UUID 不随硬件
版本、固件构建或用户变化。

| 对象 | UUID | 属性 | 权限/用途 |
| --- | --- | --- | --- |
| Navigation Service | `7e57a000-b50c-4b6a-9c57-40a54e8e1000` | primary service | 可发现 |
| PhoneToDevice / RX | `7e57a001-b50c-4b6a-9c57-40a54e8e1000` | Write、Write Without Response | encrypted write；手机下发状态、导航、路线、交通、媒体及 ACK |
| DeviceToPhone / TX | `7e57a002-b50c-4b6a-9c57-40a54e8e1000` | Notify | 只允许在加密连接上通知；设备上报命令、ACK、连接状态及心跳 |
| TX CCCD | 标准 `0x2902` | read/write | 写订阅位必须加密；读只暴露当前订阅位，不含任何业务载荷；手机写 `0x0001` 开启 Notify |

RX 不提供 Read/Notify，TX 不提供 Read/Write/Indicate。手机必须先订阅 TX，再发
握手。CoreBluetooth 用 `setNotifyValue(true)` 管理 CCCD；不要把 CCCD 当成另一条
应用特征。

EVT 的一致安全基线是 LE Secure Connections + bonding + Just Works，即 LE
Security Mode 1 Level 2：RX 使用 encrypted-write 权限，TX 只在加密连接上 Notify，
CCCD 的写操作也要求加密。首次 `setNotifyValue(true)` 或首次 RX 写会触发系统配对；
之后使用 bond 恢复加密，不在每次重连时重新做临时配对。未加密时不能订阅 TX、
接收导航内容或发送媒体命令。

若 EVT 阶段出现手机与设备的 bond 不同步，恢复动作是同时删除 iOS 侧配对记录并
擦除设备侧对应 bond 后重新配对，不能临时移除 characteristic 的加密权限来绕过。
Just Works 不提供 MITM 认证；量产边界仍需设备所有权流程，并在交互能力允许时升级
为 authenticated pairing。CCCD 的可读订阅位不是身份凭据，也不会泄露协议 payload。

每次 GATT 操作传一个完整 v1 frame。`max_frame_size` 指 characteristic value 的
最大字节数，不是 ATT MTU：

```text
max_frame_size = min(local ATT_MTU - 3, peer advertised max_frame_size, 512)
```

ATT MTU 23 时该值为 20，帧净载荷只有 8 字节。禁止依赖 Prepare Write/Long
Write；协议自己的分片在所有 MTU 下行为一致。快照、心跳与媒体状态通常使用
Write Without Response；握手或需要链路层确认的低频控制可使用 Write。发送方
必须串行发送逻辑消息的全部分片，并遵守系统的发送背压。

## 3. 会话与握手

1. BLE 连接建立，完成加密、服务发现和 TX Notify 订阅。
2. 手机生成随机、非零的 32 位 `session_id`，清空收发序号和重组状态，然后在
   RX 发送 `ConnectionStatus(role=Phone, state=Starting)`。
3. ESP32 验证版本区间，取能力交集及双方较小的 `max_frame_size`，在 TX 回送相同
   `session_id` 的 `ConnectionStatus(role=Device, state=Ready)`。
4. 手机发送 `ConnectionStatus(role=Phone, state=Ready)`，继续等待最终确认。
5. ESP32 再次回复同一 `session_id` 的 `ConnectionStatus(role=Device, state=Ready)`。
   手机确认版本、能力、帧大小和心跳间隔与首次 Ready 一致后，才进入协议就绪状态并
   发送显示数据。不能把 GATT 写成功或首次 Device Ready 当作整个握手完成。
6. 任一端收到不同 `session_id`，都必须清除旧的路线窗口、命令去重表、未完成
   分片及序号基准，再重新握手。

没有 v1 版本交集时回 `state=Closing` 并断开。能力位只表示可使用的功能；发送方
只能使用双方能力交集中的消息。

建议心跳间隔为 1000 ms。连续 3000 ms 没有收到任何 CRC 正确的帧即认为协议链路
失活，ESP32 应展示离线状态并清除未完成分片，但 BLE 的物理连接状态仍由平台层
管理。

## 4. 帧格式

所有多字节整数均为 little-endian。每个分片都有独立 CRC。

| 偏移 | 大小 | 字段 | v1 规则 |
| ---: | ---: | --- | --- |
| 0 | 1 | magic | 固定 `0xB7` |
| 1 | 1 | protocol_version | 固定 `1` |
| 2 | 1 | message_type | 见消息表 |
| 3 | 1 | flags | bit0 START、bit1 END、bit2 ACK_REQUESTED、bit3 URGENT；其余必须为 0 |
| 4 | 2 | sequence | 每个方向独立的非零逻辑消息序号 |
| 6 | 2 | fragment_offset | 本分片净载荷在完整 payload 中的字节偏移 |
| 8 | 2 | message_length | 完整 payload 字节数，不含帧头和 CRC |
| 10 | N | fragment_payload | 长度由 GATT value 总长度减 12 得出 |
| 10+N | 2 | CRC16 | 对偏移 `0..9+N` 的 CRC-16/CCITT-FALSE，低字节先发 |

CRC 参数：poly `0x1021`、init `0xFFFF`、refin=false、refout=false、xorout
`0x0000`。标准检查串 `123456789` 的结果为 `0x29B1`。

START 当且仅当 `fragment_offset == 0`。END 当且仅当
`fragment_offset + N == message_length`。单帧消息同时置 START 和 END。
`ACK_REQUESTED` 与 `URGENT` 是逻辑消息属性，所有分片上的值必须相同。

### 4.1 分片与重组

- 一个方向同一时刻只允许一个未完成逻辑消息；禁止在两个逻辑消息之间交织分片。
- BLE 保序，因此接收方只接受紧邻的 `fragment_offset`。缺口、重叠但字节不同、
  类型/总长/标志变化均拒绝。
- 完全相同的已接收分片是重复分片，不追加第二次，也不刷新超时。
- 从最后一个有效新分片起 1000 ms 未完成即丢弃。超时后的 continuation 返回
  `ReassemblyTimeout`，必须等待新的 START。
- 较新序号的 START 可原子取代未完成消息，防止丢包把链路卡死。
- 重组上限默认 4096 字节；v1 业务消息应远小于此值。

### 4.2 序号与去重

收、发方向各维护一个 `uint16` 序号，`0` 保留。发送顺序
`1..65535,1..`。模 65536 的前半区规则判断新旧，因此两次有效消息之间不能跨越
32767 个未观察序号。新 `session_id` 或 BLE 重连会重置序号比较基准。

已完成序号再次出现时返回 `DuplicateMessage`，应用副作用不能再执行。若该消息
请求 ACK，接收方从“最近完成消息 -> ACK”小缓存中重发原 ACK。

### 4.3 应用 ACK

`ACK_REQUESTED` 用于必须确认的 `DeviceCommand`，也可用于路线窗口的原子提交。
接收方在完整 payload 解码并提交后回复 `Ack`。发送方 750 ms 未见匹配 ACK 时，用
同一序号和完全相同的分片重发，最多重试 2 次。仍超时则报告 degraded，不能生成
新 `command_id` 假装成功。

普通 `NavigationSnapshot` 不请求 ACK：新快照自然替代旧快照。`Write` 的链路层
响应不能代替应用 ACK，因为它只确认 characteristic write，不确认重组和业务提交。

## 5. 消息总表

每种逻辑 payload 的第 0 字节都是 `payload_revision=1`。

| type | 名称 | 方向 | 建议频率 |
| ---: | --- | --- | --- |
| `0x01` | ConnectionStatus | 双向 | 握手或状态改变 |
| `0x02` | Heartbeat | 双向 | 空闲时每 1000 ms |
| `0x03` | Ack | 双向 | 对 ACK_REQUESTED 的响应 |
| `0x10` | NavigationSnapshot | 手机→设备 | 值变化时，最高 5 Hz；关键状态立即发 |
| `0x11` | RouteGeometry | 手机→设备 | 路线/局部窗口变化时 |
| `0x12` | TrafficDeviation | 手机→设备 | 交通或偏航变化时 |
| `0x13` | MediaState | 手机→设备 | 曲目/播放变化，进度最高 1 Hz |
| `0x14` | MapScene | 手机→设备 | 前进约 100 m、跨离线分块或路线改变时 |
| `0x15` | AttitudeSample | 设备→手机 | 手机声明 bit8 时 10 Hz，无 ACK |
| `0x16` | HomeLocation | 手机→设备 | 家地址变化时，以及每次握手后 |
| `0x17` | SettingsState | 双向 | 设置或主题变化时，以及每次握手后 |
| `0x18` | GnssInput | 手机→设备 | 每个 Core Location fix 一条（约 1 Hz），无 ACK |
| `0x19` | PerformanceRun | 设备→手机 | 0-100 计时每次状态变化，以及每次握手后；手机声明 bit13 时才发，无 ACK |
| `0x1A` | WifiConfig | 手机→设备 | 骑手保存或忘记热点时；设备声明 bit14 时才发，ACK_REQUESTED |
| `0x1B` | WifiStatus | 设备→手机 | WiFi 状态变化、连接期间的 RSSI、每次握手后；手机声明 bit14 时才发，无 ACK |
| `0x1C` | OtaCommand | 手机→设备 | 骑手点「开始更新」时；设备声明 bit15 时才发，ACK_REQUESTED，且只走加密+绑定链路 |
| `0x1D` | OtaStatus | 设备→手机 | 每次状态变化，下载中最高 2 Hz；手机声明 bit15 时才发，无 ACK |
| `0x20` | DeviceCommand | 设备→手机 | 用户动作，ACK_REQUESTED+URGENT |

## 6. Payload 编码

### 6.1 ConnectionStatus (`0x01`, 固定 17 字节)

```text
u8  revision
u8  role                 1 Phone, 2 Device
u8  state                0 Starting, 1 Ready, 2 Degraded, 3 Closing
u8  minimum_version
u8  maximum_version
u32 capabilities
u32 session_id           non-zero
u16 max_frame_size       13..512，完整 GATT value 大小
u16 heartbeat_interval_ms  >=250
```

能力位：bit0 navigation、bit1 route geometry、bit2 traffic、bit3 media state、
bit4 touch commands、bit5 music commands、bit6 command ACK、bit7 map scene、
bit8 attitude telemetry、bit9 home location、bit10 device settings、
bit11 compass heading（设备理解 NavigationSnapshot 的 HeadingCompass 标志；手机只对
声明了它的对端置该位，旧固件会拒绝未知标志）、bit12 GNSS input（设备接收 `0x18`）、
bit13 performance run（手机声明：它要显示圆屏的 0-100 成绩，设备才发 `0x19`）、
bit14 WiFi hotspot（双方声明：设备接收 `0x1A` 并回报 `0x1B`，手机显示 `0x1B`）、
bit15 OTA（双方声明：设备接收 `0x1C` 并回报 `0x1D`，手机提供固件并显示 `0x1D`）。

`0x16`、`0x17` 与 `0x18` 都是选择性消息：手机只向声明了对应能力位的对端发送。旧固件
根本不认识这些类型，而不是收到后忽略。

### 6.2 Heartbeat (`0x02`, 固定 11 字节)

```text
u8  revision
u32 session_id
u32 monotonic_ms         本会话本端单调时钟低 32 位
u16 status_flags         v1 未定义的位必须发送 0
```

`monotonic_ms` 只用于诊断和停滞检测，不用于跨设备对时。

### 6.3 Ack (`0x03`, 固定 6 字节)

```text
u8  revision
u16 acknowledged_sequence
u8  status               0 Ok, 1 Unsupported, 2 InvalidState,
                         3 Failed, 4 Duplicate
u16 command_id           非 DeviceCommand 的 ACK 为 0
```

ACK 自己使用新的本方向 `sequence`，且不再请求 ACK。

### 6.4 NavigationSnapshot (`0x10`)

```text
u8  revision
u8  state                 0 Idle, 1 Acquiring, 2 Planning,
                          3 Navigating, 4 Rerouting, 5 Arrived
u8  network               0 Offline, 1 Connecting, 2 Online
u8  display_page          0 Navigation, 1 Speed, 2 Compass, 3 Music
u8  maneuver              0..12，与 shared ManeuverType 顺序一致
u8  traffic               0 Unknown, 1 FreeFlow, 2 Slow,
                          3 Congested, 4 Severe
u16 flags
u32 route_token
u32 route_generation
u32 maneuver_id
u32 distance_to_maneuver_m
u32 remaining_distance_m
u32 remaining_duration_s
u32 route_progress_m
u32 total_distance_m
u16 speed_deci_kph        0.1 km/h
u16 speed_limit_kph       0 表示未知
u16 heading_cdeg          0..35999，0.01 degree
u16 accuracy_dm           0.1 m
u16 cross_track_dm        0.1 m，饱和到 6553.5 m
u8  roundabout_exit       0 未知，否则 1..32
u8  road_name_bytes       0..63
u8[] road_name            UTF-8，无 NUL
u8  instruction_bytes     0..95
u8[] instruction          UTF-8，无 NUL
```

固定部分连同两个字符串长度字节为 53 字节。flags：

| bit | 名称 | 对应 `nav::NavSnapshot` |
| ---: | --- | --- |
| 0 | HasDestination | `has_destination` |
| 1 | HasFix | `has_usable_fix` |
| 2 | GnssStale | `gnss_stale` |
| 3 | OffRoute | `off_route` |
| 4 | HasNextManeuver | `has_next_maneuver` |
| 5 | RouteRequestInFlight | `route_request_in_flight` |
| 6 | TrafficRequestInFlight | `traffic_request_in_flight` |
| 7 | HasRouteView | 可使用匹配的已提交 RouteGeometry |
| 8 | HeadingCompass | `heading_cdeg` 是手机罗盘（磁力计）朝向而非 GNSS 航向；仅发给声明 bit11 的对端 |

`route_token` 是完整 UTF-8 `RouteBundle::route_id` 的 FNV-1a 32-bit：offset basis
`2166136261`，逐字节 xor 后乘 `16777619`，自然按 uint32 溢出；若结果为 0 则改为
1。无路线时 token/generation 为 0；HasRouteView 时两者必须非零。

### 6.5 RouteGeometry (`0x11`)

```text
u8  revision
u8  coordinate_system     固定 2 = GCJ-02
u32 route_token
u32 route_generation
u16 chunk_index            从 0 开始
u16 chunk_count            >=1
u16 first_point_index
u16 total_point_count      1..24
i32 view_origin_lat_e6
i32 view_origin_lon_e6
u8  point_count            1..24
i32 first_lat_e6
i32 first_lon_e6
svarint remaining_lat_delta_e6[]
svarint remaining_lon_delta_e6[]
```

E6 表示 degree × 1,000,000。首点是绝对 GCJ-02 坐标，后续点相对前一点使用
ZigZag + canonical unsigned LEB128：`0 -> 0, -1 -> 1, +1 -> 2`。非 canonical、
超过 5 字节或解码溢出的 varint 必须拒绝。

同一窗口可因很小的 negotiated frame/message budget 拆成多个逻辑
`RouteGeometry`，但总点数仍不超过 24。chunk 必须按顺序到达，第一块的
`first_point_index=0`，最后一块正好结束在 `total_point_count`。接收方先放入 staging
buffer；仅在所有 chunk 的 token、generation、origin、count 都一致并完整到达后才
原子替换已提交窗口。更高 generation 的 chunk 0 可放弃旧 staging。

正常实现建议把整个 24 点窗口作为一个逻辑 payload，再交给通用 BLE frame 分片；
不要无必要地同时使用两层分块。

### 6.6 TrafficDeviation (`0x12`)

```text
u8  revision
u32 route_token
u32 route_generation
u16 flags
u32 observed_at_ms
u32 remaining_duration_s
u16 cross_track_dm
u8  segment_count         0..64
repeat segment_count:
  u24 start_offset_m
  u24 length_m            必须 >0
  u8  traffic_level
```

flags：bit0 TrafficChanged、bit1 OffRoute、bit2 Rerouting、bit3 RouteInvalidated。
segment 按 `start_offset_m` 升序、不可重叠，偏移相对完整路线起点。token/generation
不匹配当前快照时整条消息丢弃。RouteInvalidated 会立即清除已提交几何；Rerouting
会把显示状态切到 rerouting，直到后续完整 `NavigationSnapshot` 确认新状态。

### 6.7 MediaState (`0x13`)

```text
u8  revision
u8  flags                  bit0 connected, bit1 playing,
                           bit2 like available, bit3 liked
u32 track_token
u16 position_s
u16 duration_s
u8 + UTF-8 source_name     <=31 bytes
u8 + UTF-8 track_title     <=63 bytes
u8 + UTF-8 artist_name     <=47 bytes
```

`track_token` 是手机本会话内稳定的曲目标识，换曲时必须变化；它不是全局媒体 ID。

当前 iOS 适配器把本消息投影到公开的
`MPMusicPlayerController.systemMusicPlayer`，因此只控制系统“音乐”App（含其
Apple Music 播放队列）的上一首、播放/暂停和下一首。`MPRemoteCommandCenter` 是让
当前 Now Playing App **接收**耳机/系统命令的接口，不是向其他 App 注入命令的接口；
所以 v1 不宣称能从普通 iOS App 稳定控制网易云音乐等第三方播放器。Apple 也未给
system music player 暴露修改当前歌曲“喜欢”的公共接口，iOS 必须发送
`like available = 0`，设备隐藏 LIKE，而不是本地假成功。

### 6.8 MapScene (`0x14`)

`MapScene` 是可选的、完整替换的本地小地图场景，不是全市数据库。当前 iPhone
优先在线取得周边瓦片，并使用已有缓存、下载包和内置数据兜底；选出当前位置周围
500 m 的窗口，裁剪和简化以后发送。ESP32 只保留最新场景。坐标固定使用 GCJ-02，
与高德规划路线对齐。

```text
u8  revision
u8  coordinate_system       固定 2 = GCJ-02
u32 scene_revision          非零；新窗口严格递增
i32 view_origin_lat_e6
i32 view_origin_lon_e6
u16 radius_m                100..1500
u8  road_count              0..40
u8  building_count          0..24

repeat road_count:
  u8 road_class             0 motorway, 1 primary, 2 secondary,
                            3 residential, 4 service, 5 other
  u8 point_count            2..255；全帧道路点合计 <=255
  repeat point_count:
    svarint lat_delta_e6     相对 view_origin
    svarint lon_delta_e6

repeat building_count:
  u8 building_class         0 generic, 1 landmark, 2 parking
  u8 point_count            3..255；全帧建筑点合计 <=192
  repeat point_count:
    svarint lat_delta_e6     相对 view_origin
    svarint lon_delta_e6
```

建筑最后一点不得重复第一点，闭合边由渲染器补上。所有点必须在 origin 的经纬各
`±100000 E6` 安全界内；App 仍应按 `radius_m` 做实际圆/矩形裁剪。完整 payload
建议带 `ACK_REQUESTED`，在校验、解码并提交成功后才回复 Ack。更高
`scene_revision` 原子替换旧窗口；旧 revision、半包和 CRC 错误均不改变屏幕。

本编码对 255 个道路点 + 192 个建筑点通常约 1.8–2.9 KiB；在 185-byte GATT value
下约 11–18 个协议分片。它不应按定位帧率发送：前进约 100 m、靠近窗口边缘或跨
500 m 离线分块时刷新一次即可。当前链路已接入：iOS 的 `SurroundingMapStore`
选择窗口，经 `ESP32BLECentral` 和共享 C++ codec 下发；ESP32 的 `PhoneNavBridge`
校验并原子提交后，经 `NavPresenter` 交给道路与建筑 LVGL 图层。代码接入不等于
所有地区的地图覆盖、弱网或道路实测已经验收。

### 6.9 DeviceCommand (`0x20`, 固定 13 字节)

```text
u8  revision
u8  kind
u16 command_id             本 session 内非零、单调；副作用去重键
u8  page
u16 x                      触摸坐标；未知为 0xFFFF
u16 y                      触摸坐标；未知为 0xFFFF
u32 event_time_ms           设备单调时钟低 32 位
```

kind：0 PageSelected、1 Tap、2 LongPress、3 SwipeLeft、4 SwipeRight、5 SwipeUp、
6 SwipeDown、16 MusicPrevious、17 MusicTogglePlayback、18 MusicNext、19 MusicLike。
音乐命令沿用 `shared/nav_ui` 的四个动作。触摸坐标是屏幕像素；滑动方向已经归一化，
手机不应再次用坐标推断方向。

手机以 `(session_id, command_id)` 去重；成功、失败或重复都回复 Ack。UI 动作必须在
成功提交后才 ACK Ok。

### 6.10 AttitudeSample (`0x15`, 固定 13 字节)

设备→手机的低频姿态遥测。手机在 ConnectionStatus 里声明 bit8 后，设备以 10 Hz
发送，不带 ACK；只发送 `valid=true` 的估计值。没有声明 bit8 的手机永远收不到这个
类型；断线后重新以下一次握手为准。

```text
u8  revision
u32 monotonic_ms        设备单调时钟
i16 lean_cdeg           右倾为正，0.01°
i16 pitch_cdeg          抬头为正，0.01°
i16 longitudinal_mg     前向为正，毫 g；刹车为负
i16 lateral_mg          右向为正，毫 g
```

### 6.11 HomeLocation (`0x16`, 固定 15 字节)

手机保存的家地址。它故意不放在 MapScene 里：没有离线地图分块时也要能画 blip，
而且家与路线、场景版本无关。

三条约束：

1. 只接受 GCJ-02。设备不做坐标系转换，收到 WGS84 会把 blip 画偏几百米。
2. `revision` 单调递增且非零；乱序或重放不能复活旧地址。
3. `has_home=false` 是合法消息，允许手机撤销家。

```text
u8  revision
u8  coordinate_system    2 = GCJ-02；其他值拒收
u8  has_home             0 表示已清除
u32 revision             非零，单调
i32 latitude_e6
i32 longitude_e6
```

设备把家存在自己的状态里，不转发、不落盘：地址属于手机。

### 6.12 SettingsState (`0x17`, 固定 16 字节)

设备显示设置的唯一来源。手机写的是**请求**，设备是显示状态的**权威**：收到更高
revision 时应用，然后回显应用后的状态；收到不大于已应用值的 revision 时忽略。
因此回显会自然终止，重连也不会把旧主题复活。两端都可以发起修改，发起方必须用
严格大于自己见过的最大值的 revision。

```text
u8  revision             payload revision = 1
u8  theme                0 Official, 1 Auto, 2 Day, 3 Night
u8  brightness_percent   0..100
u8  units                0 kph, 1 mph
u8  lean_full_scale_deg  30 / 40 / 45 / 50
u16 performance_target_kph 50 / 60 / 100
u8  visible_pages        bit0..5，bit0 导航必须置位
u16 lean_zero_offset_cdeg  有符号，|值| <= 3000
u8  ambient_night        0/1，手机所在地现在是白天还是夜晚
u8  show_buildings       0/1，是否绘制建筑轮廓与填充
u32 settings_revision    非零，单调
```

`ambient_night` 不是偏好，而是**观测**：设备没有时钟、没有光线传感器，`Auto` 只能靠这个
字节解析。设备原样回显它，所以手机看到的是自己发来的值。`show_buildings` 是**可读性**
决策而不是布景，默认关闭——466 px 上密集的楼块会挤掉骑行中真正需要看清的道路。

这两个字节把载荷从 14 字节扩到 16 字节，**没有**升 `payload revision`。这是一次两端同时
落地的协同变更：本项目手机与固件同批发布，旧解码器会因为 `require_end()` 看到多余字节而
明确报错（`TrailingPayload`），而不是按错误语义应用一组设置；升 revision 会把其它所有消息
也一起拒掉，代价更大。旧固件收到 0x17 只会因能力位缺失而不发，不会误读。

取值是固定表而不是范围：`lean_full_scale_deg=35` 或
`performance_target_kph=75` 会被拒收，而不是被四舍五入成一个设计从未评审过的
表盘。设备对本地存量（NVS、外部输入）做 clamp，但线上值必须已经合法。

`theme` 与 `moto_ui_theme_t` 不是同一个枚举，映射必须显式：

| SettingsState.theme | 设备行为 | `moto_ui_theme_t` |
| --- | --- | --- |
| 0 Official | 原版界面：fork 前的黑白圆屏（独立的界面树，四页） | `MOTO_UI_THEME_OFFICIAL` |
| 1 Auto | 按环境光选择 | 由设备决定 DAY / NIGHT |
| 2 Day | 强制日光 | `MOTO_UI_THEME_GTA_DAY` |
| 3 Night | 强制夜间 | `MOTO_UI_THEME_GTA_NIGHT` |

手机端跟随同一个主题：Official 对应原版黑白，Night / Day 分别对应 Vice / Heat；
Auto 由手机按同一个 `ambient_night` 标志解析，和设备的结果一致。

### 6.13 GnssInput (`0x18`, 固定 12 字节)

手机的 GNSS 速度，附带 fix **是什么时候取到的**。NavigationSnapshot 的速度没有时间，
在手机发送队列里等过的 fix 到了设备上仍像是刚取到的；设备的速度融合（DESIGN_V2 §3.7）
需要的是 fix 时刻，而不是到达时刻。

```text
u8  revision
u8  flags                bit0 has_speed，bit1 has_speed_accuracy；其他位拒收
u32 phone_monotonic_ms   手机单调时钟低 32 位，编码这一条时打的戳
u16 fix_age_ms           fix 比 phone_monotonic_ms 早多少毫秒
u16 speed_cmps           cm/s，<= 10000；无 has_speed 时必须为 0
u16 speed_accuracy_cmps  cm/s，一倍标准差；无 has_speed_accuracy 时必须为 0
```

约束：有精度就必须有速度；没有对应标志的字段必须为 0，同一个 fix 只有一种编码。

时间只在各自的时钟里算，**不比较墙钟**（DESIGN_V2 §12）：手机端
`fix_age_ms = 现在 − CLLocation.timestamp`，是手机自己的一段短差值；设备端用
`到达时刻 − phone_monotonic_ms` 在最近 16 条里的最小值作为两个时钟的偏移，
`fix 时刻 = phone_monotonic_ms − fix_age_ms + 偏移`。传输延迟只会让这个差值变大，所以
最小值就是"真偏移 + 最短传输"；排队造成的延迟会如实算进 fix 的年龄。看不见的只有
最短传输本身（一两个连接间隔）。

手机端在编码时打戳并立刻发送，不缓存到下一次会话（一个旧 fix 没有用）。收到过
GnssInput 的会话里，设备不再使用 NavigationSnapshot 的速度；断开后重新学习偏移。

### 6.14 PerformanceRun (`0x19`, 固定 16 字节)

0-100 由圆屏计时，手机只记录圆屏测到的时间，两块屏的数字因此不会不一致。设备在每次
状态变化（就绪、起步、完成）时发送，并在手机握手（声明 bit13）后补发当前状态；丢一条
由下一条状态替代，所以没有 ACK。

```text
u8  revision
u8  state                0 Armed（停稳待起步）、1 Running、2 Finished
u16 target_kph           50 / 60 / 100，与 SettingsState 的表一致
u32 run_id               每次起步加一；第一次起步之前为 0
u32 elapsed_ms           Running：发出时已用时间；Finished：成绩；Armed：必须为 0
u32 best_ms              本次开机以来的最佳；没有时为 0
```

Running 与 Finished 的 `run_id` 不能为 0。握手后的补发会重复上一条 Finished，手机按
`(run_id, elapsed_ms)` 去重，只记一次成绩。

### 6.15 WifiConfig (`0x1A`, 手机→设备, 最长 100 字节)

圆屏按需连接的 iPhone 个人热点（WAVESHARE_OPTIMIZATION_PLAN §4.3）。iOS 不让 App 读取
自己的热点名和密码，所以由骑手手填一次。带 ACK_REQUESTED，只发给声明 bit14 的设备。

```text
u8  revision
u8  policy               0 Disabled（忘记网络）、1 OnDemand（保存，只在需要时连接）
u8  ssid_length          OnDemand 为 1..32，Disabled 为 0
u8[] ssid                UTF-8 字节，不含 NUL
u8  psk_length           OnDemand 为 8..64，Disabled 为 0
u8[] psk                 8..63 个可打印 ASCII（0x20..0x7E），或恰好 64 位十六进制（原始 PSK）
```

设备**只在已加密且已绑定的链路上**接受它（特征值本身已要求加密，存储前再检查一次绑定），
否则回 `InvalidState`。成功时写入 NVS 并回 `Ok`；NVS 失败回 `Failed`。PSK 不写日志、
不回传，任何消息都不回显它。内容未变时只回 Ack、不重写 flash。已经连上的链路继续使用
原网络，下次 `up` 才换新网络；Disabled 立即断开。

### 6.16 WifiStatus (`0x1B`, 设备→手机, 固定 9 字节)

每次状态变化时发送；连接期间每 5 s 查一次 RSSI，变化 ≥3 dB（至少每 30 s）时发送；
声明 bit14 的手机握手后 500 ms 补发一次（手机在收到设备最终 Ready 前会丢弃业务消息）。
无 ACK。

```text
u8  revision
u8  state                0 Off（射频关闭、内存已归还）、1 Connecting、2 Connected、3 Failed
u8  failure              0 None、1 NoCredentials、2 NetworkNotFound、3 AuthFailed、4 Timeout、
                         5 NoMemory、6 Disconnected
u8  flags                bit0 HasCredentials（设备存有网络；网络内容不回传）
u8[4] ipv4               点分顺序，ipv4[0] 是第一段
i8  rssi_dbm
```

`ipv4` 仅在 Connected 时非零，其他状态下它和 `rssi_dbm` 都为 0。`failure` 仅在 Failed
时非零；进入 Failed 时射频已经关闭。手机直接连这个地址（热点网段 `172.20.10.0/28`），
不需要 Bonjour。

### 6.17 OtaCommand (`0x1C`, 手机→设备, 最长 238 字节)

固件更新（WAVESHARE_OPTIMIZATION_PLAN §4.4，W3）。镜像**永不经过 BLE**：手机要么在它
已经共享给圆屏的热点上直接提供文件，要么给一个发布地址，设备经热点自己去取。带
ACK_REQUESTED，只发给声明 bit15 的设备，且**只接受已加密且已绑定的链路**（否则回
`InvalidState`，一个字节都不写）。

```text
u8  revision
u8  url_length           10..200
u8[] url                  绝对 http:// 或 https://，可打印 ASCII，不含空格与 NUL
u8[32] sha256              整张镜像的 SHA-256，全零表示发送方没算，直接拒绝
u32 size_bytes            0 表示未告知；否则 1 MiB..7 MiB（OTA 槽容量）
```

设备把镜像写进空闲 OTA 槽，写完才比对 SHA-256，通过后才把启动槽指过去，然后重启。
开发包带 tlog、正式包（`sdkconfig.release`）关掉 tlog，两者走同一条 OTA 路径。
开机进入新镜像后，固件在完成一次 BLE 握手时才调用 `esp_ota_mark_app_valid_cancel_rollback()`；
握手不来就重启，bootloader 自动回滚到旧槽（`CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE`）。

### 6.18 OtaStatus (`0x1D`, 设备→手机, 固定 8 字节)

每次状态变化时发送；下载中最高 2 Hz（百分比变化 ≥2% 才发）。无 ACK，下一条状态覆盖上一条。

```text
u8  revision
u8  state                0 Idle、1 Downloading、2 Verifying、3 Ready、4 Failed
u8  failure              0 None、1 NoWifi、2 NoCredentials、3 BadUrl、4 Network、5 HashMismatch、
                         6 ImageRejected、7 NoMemory、8 Busy、9 TooLarge
u8  progress_percent     0..100，总量未知时为 0
u32 bytes_received       已写入槽的字节数；Idle 时必须为 0
```

`failure` 仅在 Failed 时非零，Failed 时槽已丢弃（不会下次开机生效）。Ready 之后设备
约 1 s 内重启，手机按断链 + 重连处理，不需要额外确认。Never trust: 手机不把 Ready 当作
“新版本已跑起来”，只当作“镜像已就位”。

## 7. Display Snapshot 到共享 NavSnapshot

适配器必须做值转换，不得直接操作 `moto_ui_state_t`。推荐映射如下：

| BLE 字段 | `moto::nav::NavSnapshot` |
| --- | --- |
| state/network/display_page | 同名枚举逐项映射，不依赖裸 `static_cast` |
| flags bit0..6 | 对应七个 bool 字段 |
| `speed_deci_kph` | `speed_mps = value / 36.0F` |
| `heading_cdeg` | `heading_deg = value / 100.0F` |
| `accuracy_dm` | `horizontal_accuracy_m = value / 10.0F` |
| `cross_track_dm` | `cross_track_distance_m = value / 10.0F` |
| 距离/时长/进度/总长 | 转为对应 double/u32 字段 |
| maneuver + 文本 | `next_maneuver`；`route_offset_m = route_progress_m + distance_to_maneuver_m` |
| traffic | `traffic_ahead` |
| route_generation | 原值复制为 u32 |

只有以下条件全部满足才置 `has_route_view=true`：快照 bit7 已置位、几何已经完整
提交、几何 token/generation 与快照一致、点数至少 2。之后将 E6 origin/points 除以
1,000,000 写入 `route_view_origin` 与 `route_view_points`。不匹配时保留导航数字和
转向信息，但隐藏路线线条，绝不能沿用旧路线。

`NavSnapshot.position/destination/now_ms/last_fix_ms/last_traffic_update_ms` 不被当前
`NavPresenter` 消费，显示端适配器可置零或维护本地诊断值。手机仍是这些事实的所有
者；不能因此在 ESP32 再启动第二套 NavCore 状态机。

## 8. 错误、版本与资源边界

- magic、版本、CRC、reserved flags、长度或范围错误时整帧/整消息拒绝，不做部分
  更新。
- 字符串长度以 UTF-8 字节计；渲染层可替换无效 UTF-8，但协议层不得越界读取。
- v1 解码器拒绝未知 payload revision、未知消息类型及 trailing bytes。
- 增加可选消息类型可保留 frame v1；改变字段顺序、单位、坐标系、枚举数值或 CRC
  必须升 frame protocol version 或该消息的 payload revision。
- 端点必须对接收缓存、点数、segment 数和字符串先做上限检查再分配。
- CRC 只检测传输错误，不提供身份认证；身份和机密性依赖 BLE 加密/bonding。

## 9. 黄金帧

以下均是 lowercase hex，多字节字段 little-endian。

连接 payload：

```text
01010101017f00000078563412b900e803
```

该 payload 的单帧编码（sequence=1）：

```text
b701010301000000110001010101017f00000078563412b900e8035469
```

MusicNext 命令 payload：

```text
0112341203ffffffff40302010
```

在 20-byte characteristic value 下，使用 sequence `0x1234`、
ACK_REQUESTED+URGENT 的两个分片：

```text
b701200d341200000d000112341203ffffff6793
b701200e341208000d00ff40302010ef35
```

AttitudeSample payload：

```text
0178563412b80b06ffcefe4102
```

HomeLocation payload（GCJ-02，revision=3）：

```text
01020103000000d09b2f024027fb06
```

SettingsState payload（夜间、65%、mph、40°、0-60、隐藏音乐页、-1.25°、夜间环境、显示楼块、revision=5）：

```text
01034101283c001f83ff010105000000
```

GnssInput payload（速度与精度、phone_monotonic_ms=0x12345678、fix_age 320 ms、15.25 m/s、0.40 m/s）：

```text
0103785634124001f5052800
```

PerformanceRun payload（Finished、0-100、run_id=3、4.870 s、最佳 4.610 s）：

```text
01026400030000000613000002120000
```

机器可读的完整向量以 fixture 为准。修改任何 v1 字节都应先解释兼容性影响，并由
两端共同更新 fixture。

## 10. 本地验证

```sh
cmake -S . -B build/native -DMOTO_BUILD_WEB=OFF -DMOTO_BUILD_TESTS=ON
cmake --build build/native --target ble_protocol_tests
./build/native/tests/native/ble_protocol_tests
```

测试覆盖全部消息 round-trip、固定 UUID/属性、CRC 检错、20-byte 分片、重组、重复
与冲突分片、缺口、超时、较新 START 取代、序号回绕、输入边界和黄金字节。
