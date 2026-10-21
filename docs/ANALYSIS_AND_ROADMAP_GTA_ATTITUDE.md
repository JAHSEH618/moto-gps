# 项目分析与改造路线（GTA 地图 / 姿态页 / iPhone 功能）

> 本文是针对 fork 后的个人改造计划，不属于上游项目文档。

## 一、上游项目现状

已拉取到当前目录：`mx3353672833-debug/moto-gps-waveshare`（463★，C++，2026-10 仍在更新）。
注意：`moto-gps-waveshare/moto-gps-waveshare` 并不存在，真实仓库属于个人账号 `mx3353672833-debug`。

### 同赛道项目对比

| 项目 | Stars | 技术栈 | 许可证 | 相对本项目的差异 |
| --- | --- | --- | --- | --- |
| **本项目** moto-gps-waveshare | 463 | ESP-IDF/C++ + Swift | 非商业（NOASSERTION） | 三层共享架构、自研 PCB/外壳、离线 OSM、BLE 分片重组 + CRC |
| zyn950929/MotoNav | 44 | C + Android/iOS | 非商业，禁转售 | 功能面接近，含天气/高德；**无共享层**，双端逻辑各写一遍；许可证比本项目更严 |
| seichris/open-bike-computer | 21 | Swift + C | GPL-3.0 | 唯一 **GPL** 选项；自行车向，带 Apple Watch；架构类似但导航简化 |
| rabbihossain/BikeNavESP32 | 34 | C + Android | 无（默认版权保留） | 3.5" TFT，方形屏；**无许可证等于不可合法复用** |
| deanthecoder/SteedPilot | 13 | C + iOS | MIT | **唯一 MIT**；HUD 风格最接近 "GTA" 审美的简洁路线；功能最少，代码量适合通读借鉴 |

**结论**：本项目在同赛道中 star 最高、工程完成度显著领先（唯一有共享 C++ 层 + 协议文档 + golden fixture + 自动化测试 + 硬件资料的）。不存在"更新更活跃且更强"的直接替代。改造应在本仓库内进行，需要参考时优先读 SteedPilot（MIT，可安全借用视觉/交互思路）与 MotoNav（看功能边界，但**不要拷贝代码**，其非商业条款比本项目更严格）。

### 对本项目架构的评估

```
iPhone (Swift, 权威端)                    ESP32-S3 (显示端)
  CoreLocation ─┐                     ┌─ BLE 分片/CRC/序号
  NavCore(C++) ─┼─ HTTPS 网关 ─高德  │  PhoneNavBridge
  NavPresenter ─┴─────────────────────┴─ LVGL → CO5300 466×466
                                      └─ QMI8658 125Hz 陀螺仪 → 航向融合
```

优点（直接决定改造方式）：
- `shared/nav_presenter` 是**纯数据变换层**，不碰 LVGL，也不碰硬件。地理坐标 → 屏幕像素的投影、航向旋转、图层对齐都在这里。新增"姿态"数据只需加字段，Web 模拟器自动同步。
- `nav_ui.cpp` 已是 **LVGL 8/9 Canvas + `lv_draw_line` 自绘**，不是标准控件堆叠。GTA 风格需要的霓虹/描边/分层，改这里最直接。
- QMI8658 驱动已完整：4G 量程、125Hz、m/s² 单位、重力向量估计（`GravityEstimate`）、零偏在线学习、静止检测。**倾角和 G 值所需的原始数据已经在跑了**，只是当前只取了投影后的偏航角速度。
- `nav_core.hpp` 里 road context 的注释原文就写着 "GTA-style heading-up minimap" —— 作者本来就想做，只是没做出视觉风格。

限制（改动的硬约束）：
- `LV_COLOR_DEPTH 16`，AMOLED 真黑底。**真黑 OLED 上纯黑不发光的特性是 GTA 霓虹风的最大优势**（发光的东西才亮），但也意味着低亮度灰色会显脏。
- 显示刷新统一 40Hz（`LV_DEF_REFR_PERIOD == 25`，有 static_assert 保护）。新增动画必须挂在这个节拍上，否则会撕裂。
- 地图几何是**容量硬上限**：路线 24 点、道路 192 点/24 段、建筑 128 点/16 面。GTA 风若要更密的街区，先改容量和 BLE 协议，代价较大。
- 单精度 FPU（S3 无双精度），投影代码已刻意用 float + 先减后转。新增代码要遵守。
- BLE 协议版本固定 `1`，改帧格式必须升 `payload revision` 并同步两端 + golden fixture。
- 上游许可证为**非商业**，自己的 fork 也应视为非商业用途。

---

## 二、三个改造目标的落点

### A. GTA 风格圆形地图

#### A0. 关于 `vishalsoniindia/GTA_Map_Circle`：不可集成

已实测核查（该仓库仅 2★，不是热门项目），结论是**不能结合进本项目**：

| 项 | GTA_Map_Circle 实际 | 本项目 |
| --- | --- | --- |
| 本质 | 预渲染 GIF 播放器：101 帧 240x240 位图 | 真实矢量几何渲染 |
| 数据 | **零地理数据**，无经纬度/道路/路线 | 真实 GCJ-02 路线 + OSM 道路/建筑 |
| 体积 | `gta_map.h` 45 MB 源码 / 11.4 MB 原始像素 | 整个 app 分区才 16 MB |
| 运动驱动 | 加速度模长的帧间差 > 60mg 就推进一帧（纯抖动检测，站着抖手地图也会滚） | GPS 位置 + IMU 航向融合 |
| 渲染栈 | TFT_eSPI | LVGL 9 |
| 分辨率 | 240x240 | 466x466 |
| 圆形 | **烘死在图片里**（实测 y=0 行非黑像素仅 x=93..144，是图片自带黑框） | 需运行时裁切 |

两者架构**相反**（贴图 vs 矢量），移植等于重写；且换来的体验是退化：把真实 GPS
位移换成"摇一摇滚地图"是功能倒退。**不引入。**

#### A1. 唯一值得吸收的：真实 GTA 配色（已从帧数据提取）

从 101 帧位图实测提取，共 168 色。这是**真 GTA 地块配色**（GTA:SA / V 风格）。
注意它**不是霓虹赛博朋克** -- 常见误解是二者混为一谈：

| 语义 | 实测色值 |
| --- | --- |
| 路面 | `#F6E6C5` 奶油 / `#F6E6CD` |
| 绿地/地块 | `#73A583` / `#83AE8B` 橄榄 |
| 建筑 | `#D5A59C` 灰粉 / `#94244A` 暗红 |
| 圆外 | `#000000` |

**关键约束**：这是**亮色**配色。在 AMOLED 真黑屏上，奶油色路面 = 大面积高亮度 =
夜间刺眼 + 显著耗电。本项目现有配色是**刻意做暗**的（底 `#050607`、路 `#42474B`）。

**解法：日/夜双主题**（GTA V 本身就这么做）：

| 主题 | 底 | 地块 | 道路 | 建筑 | 路线 |
| --- | --- | --- | --- | --- | --- |
| Day（忠于 GTA，白天强光可读） | `#1A1F1A` | `#2A3428` | `#F6E6C5` | `#4A2A32` | `#C860B8` |
| Night（现有暗向，夜骑） | `#050607` | `#0B0F14` | `#46566B` | `#1A2230` | `#39E6FF` |

切换依据：手机下发的日出日落时间或本地 RTC；也可先按固定时段。

#### A2. 真正的 GTA 观感来自三件事（不靠贴图）

GTA 小地图 = **平涂矢量地图**：奶油厚路面 + 平涂地块 + 平涂建筑 + 粗路线 +
暗色圆环边框。这恰好就是本项目的渲染模型，所以能做到，成本也不算高。

1. **主题表**：把 `kRoadGray / kBuildingGray / kAmber ...` 收敛为 `MapTheme` 结构。
2. **图层重排为 GTA 顺序**：地块填充 -> 建筑填充 -> 次干道 -> 主干道 -> 路线底衬 -> 路线主色 -> 载具。
3. **面填充**（唯一的新能力，见 A3）：现在是空心描边 + 纯黑背景；GTA 必须有面。
4. **路线**：粗线 + 暗色描边（`lv_draw_line` 画两遍：先宽暗色，后窄亮色）。
   霓虹辉光用三遍递减宽度 + 递减 opacity，无需 blur。
5. **圆形视口**：`lv_obj_set_style_radius` 对正方形 layer 设 50%，`kVehicleY` 回到圆心 233。
6. **载具标记**：三角 -> 带描边箭头 + 底部投影。

#### A3. 面填充的技术验证（已确认）

- 本仓库 LVGL **没有** `lv_draw_polygon`（已搜 `third_party/lvgl/src/`，不存在）。
- ThorVG 矢量引擎被 `CMakeLists.txt` 显式关闭（`CONFIG_LV_USE_THORVG_INTERNAL OFF`）。
- **但 `lv_draw_triangle` 可用，且本项目已在用**（载具标记 `draw_vehicle_marker`）。
  `lv_draw_triangle_dsc_t` 支持 `color`、`opa`、`grad`（渐变）。
- 方案：**三角扇填充** -- 取多边形质心，向每对相邻顶点连三角形。
  建筑 footprint 基本是凸的，偶有凹多边形只会有轻微瑕疵。
  16 个 footprint x 平均 8 点 ≈ 100 个三角形，S3 上成本可忽略。
- 备选（若三角扇效果不佳）：`lv_draw_line` 扫描线填充；或开启 `LV_USE_CANVAS` 直接写像素。

#### A4. 二期（需改协议）

- 水体/公园面：复用 building footprint 通道加 `area_class`，这是 GTA 地图辨识度最高的元素。
- 道路按 class 分宽度（现在统一 3px）、路口圆角。
- 街区密度：优先改 iOS 端 `MapTilePlanner` 筛选阈值（同样 192 点覆盖更近街区），而非加容量。

**验证方式**：`platforms/web` 同一份 C++ + LVGL 编 Wasm，浏览器直接看，不用刷板。

### B. 倾角 + G 值页（新增第 5 页）

数据链路（改动小、边界清晰）：

```
motion_heading_sensor.cpp  (已有 125Hz accel/gyro + gravity)
   └─ 新增：算 roll/pitch + 线性加速度 G
        └─ 回调扩展一个 AttitudeSample 参数
             └─ PhoneNavBridge::on_imu_sample → snapshot_.attitude
                  └─ NavPresenter::update → ui_state_.attitude
                       └─ nav_ui.cpp: 新页 + 弧形表
```

算法要点：
- **侧倾角 roll**：以重力向量为参考，绕车身纵轴计算。
  `roll = atan2(gravity·right_axis, gravity·up_axis)`，单位度。
  关键是**先确定安装基**：屏幕是竖装/斜装，不能假设 `gravity.z` 就是"上"。项目已踩过这个坑（`motion_heading_axis.hpp` 的注释），必须沿用同一套重力向量做投影，不要新起一套。
- **俯仰 pitch**：同法绕横轴。加速/刹车会污染，**必须用重力估计（低通）而非瞬时加速度**，现有 `update_gravity(blend=0.025)` 已经够用。
- **G 值**：`linear = accel - gravity_normalized * 9.80665`，再取纵向/横向分量。
  `lateral_g = linear · right_axis / 9.80665`，`longitudinal_g = linear · forward_axis / 9.80665`。
  注意 4G 量程足够（摩托车横向 G 极限约 1.2G），但硬刹车 + 坑洼会到 2G 以上，别溢出。
- **滤波**：侧倾角要"实车可用"的数字，建议对 roll 用 ~150ms 时间常数的低通（125Hz 下 alpha≈0.05），G 值保留更快的响应（alpha≈0.2），否则过弯 G 值会滞后。**振动抑制是主要工作量**，单缸车怠速振动会直接污染横轴。
- **零点校准**：骑行前静止 1~2 秒取 roll 均值作为 zero offset，与现有 `bias_dps` 校准流程合并到同一次启动。
- 侧倾显示建议**带左右符号 + 正负向模拟条**，因为对骑手来说 15° 左倾和 15° 右倾语义完全不同。峰值保持（session max lean）很实用，加一个可重置的最大值。

UI 设计（复用 `create_compass_page()` 的弧形表套路，约 200 行）：
- 中央大数字 = 当前侧倾角（带 L/R 标识）
- 外圈双弧 = 左倾 / 右倾相对极限（可设 45° 满量程），超过 35° 转黄、45° 转红
- 下方两行小字 = 纵向 G / 横向 G
- 右侧小字 = 本次行程最大侧倾

页数从 4 → 5，需同步：`MOTO_UI_PAGE_COUNT`、`page_dots`、iOS 端 `DisplayPage` 枚举、BLE snapshot 的 page 字段。**这是唯一的协议触点**（page 为 u8 枚举，加值向后兼容，但仍要更新 protocol 文档）。

### C. iPhone 侧新增功能

iOS 已有：搜索、路线确认、全程预览、离线下载、Apple Music、网关设置。
**建议加的功能按"和仪表盘的协同价值"排序**：

1. **骑行记录与复盘（最高价值，直接复用姿态数据）**：圆心屏只显示当前，行程结束后在手机看最大侧倾、G 值曲线、速度/倾角叠加图。这需要 ESP32 把姿态数据**回传**给手机（现有 BLE 只有手机→屏的单向 snapshot + 触摸/音乐反向命令，要加一个低频率上行通道，如 5Hz）。这也是目前唯一缺的"数据闭环"。
2. **仪表盘配置下发**：主题选择（官方/霓虹）、倾角满量程、是否显示 G 值、页面开关。通过 BLE 配置帧下发，屏端存 NVS。让 A/B 两个改造变成"可选项"而不是硬编码。
3. **倾角校准向导**：手机上引导"把车扶正静止 2 秒"→ 触发设备端校准 → 回读确认。比在车上按屏幕安全得多。
4. **自定义警告**：侧倾超阈值 / 急刹（纵向 G 超阈值）时的手表震动或语音提示。
5. 天气、小队位置共享（MotoNav 有天气，属加分项，优先级低）。

不建议现在做：Android 移植（工程量 > 三个目标总和）、离线全国算路（网关架构限制）。

---

## 三、建议实施顺序

| 阶段 | 内容 | 触及文件 | 风险 |
| --- | --- | --- | --- |
| 1 | Web 预览跑通 | — | 无 |
| 2 | **A1 GTA 视觉重制**（圆形裁切 + 主题表 + 路线辉光 + 建筑填充） | `nav_ui.cpp`（主）/ `lv_conf.h` | 低，纯视觉，可回退 |
| 3 | **B 姿态管线**（sensor → bridge → presenter → ui_state） | `motion_heading_sensor.*`、`phone_nav_bridge.*`、`nav_core.hpp`、`moto_nav_presenter.*`、`moto_nav_ui.h` | 中，改共享结构体，需同步 3 端 |
| 4 | **B 姿态页 UI** | `nav_ui.cpp`、页枚举 | 中，页数变更 |
| 5 | **A2 地图密度/水体** | iOS `MapTilePlanner`、`nav_core.hpp` 容量 | 中高，可能触协议 |
| 6 | **C1 姿态回传 + 记录** | iOS BLE、ESP32 transport、协议 | 高，协议双向扩展 |
| 7 | C2/C3 配置下发与校准向导 | 两端 | 中 |
| 8 | 实车验证（振动、屏幕可读性、续航） | — | 必须 |

阶段 2 和 3 相互独立，可并行；建议先做 2，因为能立刻在浏览器看到效果，也验证了 Wasm 预览链路可用。

## 四、开始前需要确认的事

1. 我这边没有硬件，**Web(Wasm) 预览是否能在本机跑通**需要先试（需要 emsdk）。
2. 姿态数据的**安装基向量**必须按你的实际支架角度标定，建议先用固定角度值作为参数，实车再调。
3. 是否保留"官方视觉"作为可切换主题（推荐保留，便于对比和回退）。
4. 阶段 6 的姿态回传会扩展 BLE 协议，需确认你是否愿意动协议版本（也可以先用一次性"行程结束后批量上传"避开实时通道）。

---

## 五、开分支前的准备清单

### 已完成

- [x] 仓库克隆到 `/Users/okonma/CodeSpace/moto`（浅克隆 `--depth 1`）
- [x] LVGL 子模块已初始化（v9，commit `85aa60d`）

### 必须做（一次性）

1. **补全 git 历史**。当前是 `--depth 1` 浅克隆，无法 rebase / 对比上游：
   ```sh
   git fetch --unshallow
   ```
2. **建立自己的远端 + 分支**。不要直接在上游默认分支上开发：
   ```sh
   git remote rename origin upstream
   git remote add origin <你自己的 fork 地址>
   git checkout -b feat/gta-map-attitude upstream/main
   git push -u origin feat/gta-map-attitude
   ```
   > 上游默认分支名需确认（`git branch -r`）。
3. **确认许可证合规**。上游是**非商业**许可（NOASSERTION）+ `NOTICE` 文件。
   若 fork 公开，保留 `LICENSE.md` / `NOTICE` / `THIRD_PARTY_NOTICES.md` 原样，
   不得商用、不得移除署名。这是硬约束，先看清楚再决定是否公开。
4. **建立本地验证基线**（改动前必须知道"本来就通过什么"）：
   ```sh
   cmake -S . -B build/native -DMOTO_BUILD_WEB=OFF -DMOTO_BUILD_TESTS=ON
   cmake --build build/native --parallel 4
   ctest --test-dir build/native --output-on-failure
   swift test --package-path platforms/ios
   npm ci --prefix backend && npm --prefix backend test
   ```
   先让这些全绿，之后每一次回归才有意义。

### 建议做（影响开发效率）

5. **Web 预览链路**（视觉改动的快速反馈回路，目前**未验证**）：
   - 本机缺少 emsdk，需安装：
     ```sh
     git clone https://github.com/emscripten-core/emsdk.git ~/emsdk
     cd ~/emsdk && ./emsdk install latest && ./emsdk activate latest
     source ~/emsdk/emsdk_env.sh
     ```
   - 然后 `./scripts/build_web.sh` + `python3 -m http.server 4173 --directory .`
   - 若跑不通，退化为"每次刷板验证"，A 阶段迭代速度会慢一个数量级。
6. **ESP-IDF**（刷机必需）：确认 `idf.py` 可用及版本匹配 `platforms/esp32/dependencies.lock`。
7. **iOS 侧构建**：`platforms/ios/project.yml` 是 XcodeGen 工程，需装 `xcodegen`；
   真机还需签名配置（见 `docs/IOS_SIDELOAD.md`）。

### 分支拆分建议（对应阶段 2~4）

| 分支 | 内容 | 独立可回退 |
| --- | --- | --- |
| `feat/gta-theme` | 仅 `MapTheme` 表 + 配色替换，无结构改动 | 是 |
| `feat/gta-map-circle` | 圆形视口 + 图层重排 + 三角扇填充 | 是 |
| `feat/attitude-pipeline` | 传感器 -> bridge -> presenter -> ui_state 数据链路 | 是 |
| `feat/attitude-page` | 第 5 页 UI + 页枚举 | 是 |

先合 `gta-theme`（最快见效、零风险），再 `gta-map-circle`。
`attitude-*` 两条与地图互不依赖，可并行。
