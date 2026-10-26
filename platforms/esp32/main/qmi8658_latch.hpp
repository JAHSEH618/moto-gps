#pragma once

#include <cstddef>
#include <cstdint>

#include "qmi8658_vendor.hpp"

namespace moto::motion {

// QMI8658B Rev D latch-mode reader (DESIGN_V2 r2 §3.2).
//
// The sample chain this replaces was a FIFO read window: every read window
// dropped whatever the sensor produced while the bus was busy, so "lost
// samples" were structural, not a bug. Here the sensor keeps sampling at
// 448.4 Hz behind its own low-pass filter and holds the newest sample in a
// latch. The host reads every 4464 us; the lock guarantees the six axes in one
// 14-byte burst belong to one instant. Sampling is therefore continuous even
// though the host only consumes 224.2 Hz.
//
// Required by the datasheet (§11.2.1): with I2C and syncSmpl enabled, AHB
// clock gating must be disabled or two consecutive samples can be misaligned.
struct LatchStats {
  std::uint32_t cycles = 0;
  std::uint32_t samples = 0;
  std::uint32_t avail_zero = 0;
  std::uint32_t lock_waits = 0;
  std::uint32_t lock_timeouts = 0;
  std::uint32_t i2c_errors = 0;
  std::uint32_t missed_reads = 0;
  std::uint32_t flash_skips = 0;
  // An over-two-period interval that contains a tlog page write. Counted
  // separately because the acceptance allows it; `missed_reads` is "not
  // explained by flash".
  std::uint32_t flash_gaps = 0;
  std::uint32_t invalid_samples = 0;
  std::uint32_t interval_us_max = 0;
  std::uint64_t interval_us_sum = 0;
  std::uint32_t intervals = 0;
  bool active = false;
};

struct LatchSample {
  float accel_mps2[3] = {0.0F, 0.0F, 0.0F};
  float gyro_dps[3] = {0.0F, 0.0F, 0.0F};
  float temp_c = 0.0F;
  std::int64_t timestamp_us = 0;
};

class Qmi8658Latch {
 public:
  // 4464 us is two periods of the 448.4 Hz 6DOF ODR plus the phase budget for
  // a two-sample average; the exact average interval is measured and reported.
  static constexpr std::uint32_t kPeriodUs = 4464;
  static constexpr std::uint32_t kLockDelayUs = 12;

  bool begin(qmi8658_dev_t* device);
  void shutdown();

  bool active() const { return active_; }
  const char* fallback_reason() const { return fallback_reason_; }
  const LatchStats& stats() const { return stats_; }
  void reset_stats();
  // Bench diagnostic: log the raw configuration and data-ready registers.
  void log_status(const char* when);

  // One latch read. Returns false when the sensor had no new sample, the flash
  // guard was busy, or the transfer failed; every reason is counted.
  bool read(LatchSample* sample);

  // Called by the owner after a synchronous tlog write. The next successful
  // read attributes an over-budget interval to flash instead of the scheduler.
  void note_flash_blocking();

 private:
  bool read_register(std::uint8_t reg, std::uint8_t* out, std::size_t len);
  bool write_register(std::uint8_t reg, std::uint8_t value);
  bool control9(std::uint8_t command);
  bool disable_ahb_clock_gating();
  void fall_back(const char* reason);
  void count_i2c_error();
  bool decode_sample(const std::uint8_t* raw, LatchSample* sample);
  bool plausible_accel(const float accel_mps2[3]) const;

  qmi8658_dev_t* device_ = nullptr;
  LatchStats stats_{};
  const char* fallback_reason_ = nullptr;
  bool active_ = false;
  bool big_endian_ = false;
  bool endian_checked_ = false;
  int consecutive_i2c_errors_ = 0;
  bool have_last_timestamp_ = false;
  bool flash_gap_pending_ = false;
  std::int64_t last_timestamp_us_ = 0;
};

}  // namespace moto::motion
