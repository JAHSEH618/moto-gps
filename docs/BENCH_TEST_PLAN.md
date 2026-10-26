# v2 r1 上板测试清单

按顺序执行。每一步给出：操作/命令、串口关键字、通过标准。串口为 USB-Serial/JTAG（115200）。命令从同一个口输入：固件同时读 UART0 和 USB-Serial/JTAG（后者在 sdkconfig 里只是输出用的副控制台，2026-10-26 之前的固件收不到从 USB 口输入的命令）。打开串口会让芯片复位，开机约 10 s 后再发命令。

前置：`cd` 到仓库根，已 `. ~/esp/esp-idf/export.sh`。分区表已换，**首次刷必须 USB**：

```bash
idf.py -C platforms/esp32 erase-otadata    # 旧 otadata 位置现为 phy_init，先清
idf.py -C platforms/esp32 flash
```

`erase-otadata` 不触碰 nvs(0x9000)，`att.rmount` 保留。串口工具另开一个窗口跟日志。

---

## 0. 板子确认

```bash
esptool.py --chip esp32s3 -p <PORT> flash_id
```

| 抓 | 期望 |
|---|---|
| 关键字 | `Flash size: 32MB` |
| 关键字 | `flash: 33554432 bytes (32 MB)`（开机日志） |
| 关键字 | `i2c device at 0x6b` / `0x5a` / `0x34` / `0x20` / `0x18` / `0x40` |
| 关键字 | `PCF85063 RTC ... 0x51: present|absent` |
| 通过 | flash 为 32 MB；IMU/触摸/PMIC/扩展器在；RTC 结论（有/无）记入 §1.1 |
| 备注 | 若 0x51 absent，r1 §1.1 的 RTC 行填"无"；时间只靠手机对时 |

## 1. tlog 16 MB 以上自测

首次开机执行一次（结果存 NVS `tlog.ok`）。

| 抓 | 期望 |
|---|---|
| 关键字 | `above-16-MB self-test passed at 0x1ff000` |
| 关键字 | `recording at 0x0 of 0x1000000`（首块从 0 开始） |
| 通过 | 自测通过；若失败，记录 `above-16-MB ... failed` 并停止后续记录测试 |
| 备注 | 失败时记录器禁用，`LOG FULL` 不出现；把完整日志发回 |

## 2. FIFO 验收

### 2.1 初始化

| 抓 | 期望 |
|---|---|
| 关键字 | `CTRL5=0x99` |
| 关键字 | `FIFO stream/128, watermark 4` |
| 关键字 | `imu mode=fifo`（`imu stats`） |
| 通过 | 无 `FIFO disabled`；若回退，记录原因并跳到 2.5 |

### 2.2 静置 10 分钟

静置（熄火、无振动），每 60 s 发一次 `imu stats`。

| 抓 | 期望 |
|---|---|
| 字段 | `lost=0`、`overflow=0` |
| 字段 | `window_max_us < 2000` |
| 通过 | 10 分钟内丢样/溢出均为 0；最大值 <2.0 ms |
| 备注 | 若丢样连续 3 批，应出现 `three consecutive batches lost samples` 并回退；这算失败 |

### 2.3 tlog 擦写同时进行

让记录器持续写（正常开机即写），静置 5 分钟并 `imu stats`。

| 抓 | 期望 |
|---|---|
| 关键字 | 无 `flash guard timeout` |
| 字段 | `lost=0`、`overflow=0` |
| 通过 | 擦写期间丢样仍为 0 |

### 2.4 串口 A/B

```text
imu fifo off
imu stats        -> imu mode=poll
imu fifo on
imu stats        -> imu mode=fifo
```

| 抓 | 期望 |
|---|---|
| 关键字 | `FIFO disabled; restarting acquisition` / `FIFO enabled` |
| 通过 | 两次切换后都能出数；NVS `imu.fifo` 在重启后保持最后一次选择 |

### 2.5 回退可见性（仅在失败时）

| 抓 | 期望 |
|---|---|
| 关键字 | `FIFO disabled: <原因>` |
| UI | 倾角页状态槽显示 `IMU POLL` |
| 文件 | 会话元数据（`moto_replay` 输出）`# meta mode=0 odr=125` |
| 通过 | 三处一致，不允许"静默 125 Hz" |

## 3. CAL 交互

### 3.1 正常校准

| 操作 | 抓 | 通过 |
|---|---|---|
| 熄火、扶正，长按倾角页 3 s 松手 | `CAL 3/2/1` → `CAL...` → 串口 `static calibration captured` | 屏幕出现 `CAL OK` 约 1 s |
| 再发 `imu lean` | `lean≈0.0` | 竖直时 \|lean\| ≤0.5° |
| 重启 | `mount basis restored` 或 `mount basis loaded` | 不再显示常驻 `CAL` |

### 3.2 移动中拒绝

| 操作 | 抓 | 通过 |
|---|---|---|
| 推车走动（>1 km/h）时长按 3 s | `static calibration rejected (reason 1)` | 屏幕 `CAL FAIL MOVING` |
| 在倾斜/振动台面上校准 | reason 2 | 屏幕 `CAL FAIL RANGE` |
| 通过 | 两种拒绝都能在 1 s 内显示对应原因，旧基不变 |

### 3.3 2 s 清峰值

| 操作 | 抓 | 通过 |
|---|---|---|
| 先骑出峰值；长按 2.4 s 松手 | 屏幕 L/R 峰值变 `--` | 不触发校准（无 `CAL` 倒计时） |

### 3.4 串口校准与轴向核对

| 命令 | 作用 |
|---|---|
| `imu cal` | 与长按倾角页相同的静态校准（板子静止 2 s） |
| `imu raw` | 最近一帧原始 accel/gyro、偏置、`fwd`（前向轴）、静止计时与 lean/pitch |
| `imu int reset` → 转动 → `imu int` | 每轴陀螺积分与重力变化推算的转角对比：`gyro=` 与 `accel=` 应一致 |

轴向核对（2026-10-26 实测）：平放后抬起左边立在右边上，`imu int` 应为 **+90° 绕 x**；抬起上边立在下边上，应为 **−90° 绕 y**。即屏幕顶部 = QMI +x，屏幕左边 = +y，出屏 = +z。平放 CAL 后 `imu raw` 的 `fwd` 应为 `1.00 0.00 0.00` 左右。

## 4. H1 三组倾角测试

每组前都先做一次 §3.1 校准（对应安装姿态）。用倾角台/数字水平仪，每个角度停 10 s，发 `imu lean` 记录。

### H1-A 竖直安装

| 角度 | 通过 |
|---|---|
| 0 / ±15 / ±30 / ±45° | 误差 ≤0.5°；右倾为正；地平线右端抬高；blip 顺时针 |

### H1-B 后仰 45° 安装

重复 A 的角度。验证 `R_mount` 对任意俯仰成立。

| 通过 | 误差 ≤0.5°，与 A 组同一角度读数差 ≤0.5°；若未做步骤 2，偏航安装误差允许带来 ≤1° 偏差 |
|---|---|

### H1-C 偏航安装误差

在 A 的基准上分别故意加 5° 和 10° 偏航（步骤 2 未跑），各测 ±30°。

| 记录 | 目的 |
|---|---|
| ±30° 的 lean 偏差 | 量化"只做步骤 1"时偏航误差对倾角的影响，决定步骤 2 优先级 |
| 通过 | 10° 偏航下 \|偏差\| ≤1.5°；否则步骤 2 必须优先 |

## 5. 导出、回放、误差表

```bash
python $IDF_PATH/components/partition_table/parttool.py -p <PORT> \
  read_partition --partition-name tlog --output /tmp/tlog.bin
./build/native/tests/native/moto_replay /tmp/tlog.bin > /tmp/ride.csv
```

**擦除 tlog 分区。** 以下三种情况需要清一次（先 `read_partition` 备份，擦除不可恢复）：

- 首次刷入这版分区表之后；
- 一次中断的写之后（倾角页状态槽显示 `LOG DIRTY`，录制保持关闭）；
- 分区写满之后（开机日志 `tlog partition is full`，状态槽 `LOG FULL`）。

主机路径：

```bash
python $IDF_PATH/components/partition_table/parttool.py -p <PORT> \
  erase_partition --partition-name tlog
```

或在串口输入 `tlog clear`：固件写 NVS 标志并重启，`app_main` 在显示、BLE、IMU 任务启动前擦除整个 16 MB 分区，然后从 0x0 重新录制。擦除期间不要断电；失败时标志保留，下次开机会重试。

| 抓 | 期望 |
|---|---|
| `moto_replay` 首行 | `# mt r1 blocks` |
| `# meta` | `mode=1 odr=448 sample=112 h_mm=...`（FIFO 正常时） |
| `# session` | 每次重启一个新 boot id |
| 通过 | 同一文件跑两次 `sha256` 相同；无 `truncated` / `unknown record type` |

把 H1 各角度对应的时间段从 CSV 中取 `lean_deg` 均值，填入：

| 组 | 设定角 | 读数均值 | 误差 |
|---|---|---|---|
| A | 0 / +15 / −15 / +30 / −30 / +45 / −45 | | |
| B | 同上 | | |
| C | +30 / −30（5° 偏航、10° 偏航） | | |

## 6. 通过后回填

- r1 §1.1：RTC 有/无、扩展焊盘 GPIO。
- 附录 B：CTRL5 读回、窗口最大值/均值、丢样、H2 结论。
- 倾角页/速度·G 页的几何 gate 实测坐标。
