#pragma once

#include <cstdint>

namespace moto::device {

// One lock for "flash erase/write" and "IMU FIFO read window". Erasing a flash
// sector stalls the cache and therefore I2C; if that lands inside the QMI8658
// read-mode window the samples in that window are dropped. Taking this lock
// around both is the only ordering that cannot interleave (DESIGN_V2 r1 §2.5).
void flash_guard_init();
bool flash_guard_take(std::uint32_t timeout_ms);
void flash_guard_give();

}  // namespace moto::device
