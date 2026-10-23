#pragma once

#include <cstddef>
#include <cstdint>

#include "qmi8658.h"

namespace moto::motion {

// QMI8658B Rev D FIFO reader. The vendored component only polls, so the
// register sequence lives here. Every failure path is reported, and the caller
// falls back to polling rather than losing the IMU.
struct FifoStats {
  std::uint32_t batches = 0;
  std::uint32_t received = 0;
  std::uint32_t produced = 0;
  std::uint32_t lost = 0;
  std::uint32_t overflow = 0;
  std::uint32_t window_us_max = 0;
  std::uint64_t window_us_sum = 0;
  std::uint32_t window_samples = 0;
  bool active = false;
};

struct FifoFrame {
  float accel_mps2[3];
  float gyro_dps[3];
};

class Qmi8658Fifo {
 public:
  // Configures CTRL1/2/3/5/7/8, FIFO_WTM=4, Stream/128, validates byte order
  // on the first batch. Returns false when the caller must keep polling.
  bool begin(qmi8658_dev_t* device);
  void shutdown();

  bool active() const { return active_; }

  // Reads one batch if the watermark/full/overflow flag is set. Returns the
  // number of frames copied into `frames` (0 when no batch is ready).
  std::size_t poll(FifoFrame* frames, std::size_t capacity);

  void set_watermark(std::uint8_t samples);
  const FifoStats& stats() const { return stats_; }

  // Reason the reader fell back, for the serial log and session metadata.
  const char* fallback_reason() const { return fallback_reason_; }

 private:
  bool read_register(std::uint8_t reg, std::uint8_t* out, std::size_t len);
  bool write_register(std::uint8_t reg, std::uint8_t value);
  bool control9(std::uint8_t command);
  bool validate_endianness(const std::uint8_t* raw, std::size_t frames);
  std::size_t read_batch(FifoFrame* frames, std::size_t capacity);
  void fall_back(const char* reason);

  qmi8658_dev_t* device_ = nullptr;
  FifoStats stats_{};
  const char* fallback_reason_ = nullptr;
  std::uint8_t watermark_ = 4;
  bool big_endian_ = true;
  bool active_ = false;
  bool endian_checked_ = false;
  int consecutive_loss_ = 0;
  int slow_windows_ = 0;
  bool watermark_reduced_ = false;
  std::uint32_t last_timestamp_ = 0;
  bool have_timestamp_ = false;
};

}  // namespace moto::motion
