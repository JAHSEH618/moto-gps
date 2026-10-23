#pragma once

#include <cstddef>
#include <cstdint>

#include "attitude_estimator.hpp"
#include "dyn_record.hpp"
#include "motion_heading_sensor.h"

namespace moto::device {

// Development recorder for the `tlog` flash partition (v2 r1 §11.2).
//
// The partition content is the plain telemetry stream from dyn_record.hpp:
// header at offset 0, then typed records. That is deliberate, because the
// export path is `parttool.py read_partition --partition-name tlog` piped into
// `moto_replay`, and any block framing would have to be stripped first.
class TlogLogger {
 public:
  // Finds the partition, runs the above-16-MB self-test once, resumes from the
  // position stored in NVS, or starts a fresh file.
  bool begin();

  bool ready() const { return ready_; }
  bool full() const { return full_; }

  // Called from the IMU task with the raw sample and the estimate it produced.
  void append(std::uint64_t now_ms, const RawImuSample& raw,
              const moto::motion::AttitudeEstimate& attitude);

  // Writes the partial sector buffer. Called every 2 s and at shutdown.
  void flush();

  std::uint32_t records() const { return seq_; }
  std::uint32_t write_errors() const { return write_errors_; }

 private:
  bool ensure_partition();
  bool run_selftest();
  bool erase_sector(std::uint32_t offset);
  bool write_at(std::uint32_t offset, const std::uint8_t* data, std::size_t size);

  const void* partition_ = nullptr;  // esp_partition_t*
  std::uint32_t position_ = 0;
  std::uint32_t capacity_ = 0;
  std::uint16_t seq_ = 0;
  std::uint32_t write_errors_ = 0;
  std::uint64_t last_flush_ms_ = 0;
  alignas(4) std::uint8_t buffer_[4096] = {};
  std::size_t used_ = 0;
  bool ready_ = false;
  bool full_ = false;
};

}  // namespace moto::device
