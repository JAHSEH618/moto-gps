# 上板首轮结果（2026-10-23，SKU 33691，MAC b8:1f:3f:cb:af:9c）

刷机：`erase-otadata` 失败（新表下该 target 报错），但 `idf.py flash` 成功写入了新分区表、otadata 与 app0；nvs 偏移未动。

## 板子

| 项 | 结果 |
|---|---|
| flash | 32 MB（`flash_id` 与开机 `esp_flash_get_size` 一致） |
| I2C 应答 | 0x18 ES8311、0x34 AXP2101、0x40 ES7210、0x5A CST9217、0x6B QMI8658 |
| **0x51 RTC** | **absent**（PCF85063 未贴） |
| **0x20 TCA9554** | **absent**（PWR 直接走 GPIO3） |
| 分区表 | app0 0x20000/6 MB、app1 0x620000/6 MB、tlog 0x1000000/16 MB 全部被 bootloader 接受 |

## tlog

| 项 | 结果 |
|---|---|
| 16 MB 以上自测 | `above-16-MB self-test passed at 0xfff000`（首次），之后跳过 |
| 追加点 | 跨 4 次重启依次 0x0 → 0x6f000 → 0xa4000 → 0xcb000 → 0x116000，二分查找有效 |
| 块/CRC | `read_flash 0x1000000 0x100000` 后 `moto_replay` 输出 4 个 `# session`，坏块未出现 |
| 会话元数据 | `# meta mode=1 odr=448 sample=112`，FIFO 回退后出现 `mode=0 odr=125` |
| 回放 | 21377 行 CSV，无 truncated/unknown |

## FIFO

| 项 | 结果 |
|---|---|
| CTRL1 / CTRL5 读回 | 0x70 / 0x99（写入即生效） |
| 字节序 | **little-endian**（BE 两种取值试过，只有 LE 通过 1 g 校验） |
| 水位 | 4 → 3 → 2：4/3 时窗口 2.4–2.8 ms，2 时 1.36–1.39 ms 均值 |
| 窗口最大值 | 4231 µs（一次，发生在 tlog 4 KB 擦写后的积压批次） |
| 丢样 | 约 10–12%（1426 批 / 3577 帧 / 460 lost，10 s） |
| TIMESTAMP | 0x30–0x32 读回 **0**，没有递增 |
| 结果 | 连续丢样触发回退；`imu mode=poll reason=three consecutive batches lost samples` |
| 可见性 | 串口 reason、会话 meta mode=0、`IMU POLL` 状态槽（待人工确认屏幕）均生效 |

**解释**：读模式窗口约 1.4 ms，每 8.9 ms 一次，占空比 16%；窗口期间到达的样本被丢弃，实测约 10–12%，与占空比同量级。把窗口压到 2.23 ms 以内并不能消除丢弃，只能保证不丢整帧。水位加大只会等比放大窗口，比例不变。

**待决策**：① ODR 降到 224.2 Hz（丢样比例约减半，握手开销占比不变）；② 接受丢样并改 §2.7 验收/回退规则；③ 默认关闭 FIFO，用 125 Hz 轮询（无窗口丢样）参赛，FIFO 留 A/B。需要产品侧拍板。

## 倾角 / CAL / H1

未执行：需要在设备上长按屏幕并物理倾斜，本轮只确认了 `imu lean` 通道可用（无校准时 `quality=25`、`flags=0x0008`，读数为桌面姿态）。按 `docs/BENCH_TEST_PLAN.md` §3–§5 继续。
