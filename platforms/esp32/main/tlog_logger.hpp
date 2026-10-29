#pragma once

#include <cstddef>
#include <cstdint>

#include "attitude_estimator.hpp"
#include "dyn_record.hpp"
#include "motion_heading_sensor.h"

namespace moto::device {

struct TlogSessionInfo {
  std::uint8_t mode = 0;  // 0 poll, 1 fifo
  std::uint16_t odr_hz = 0;
  std::uint16_t sample_hz = 0;
  std::int16_t lever_arm_mm = 0;
  std::uint32_t firmware_hash = 0;
  MountRecord mount;
};

// Self-describing block recorder for the `tlog` partition (v2 r1 §11.2).
//
// Each 4 KiB sector holds one block: erased, payload written as a whole block,
// then the header (with payload length and CRC32) is part of that same write.
// Nothing is stored in NVS: the append point is the first sector whose first
// four bytes are 0xFF, found by binary search at boot. A torn block fails CRC
// and is skipped by the replay.
class TlogLogger {
 public:
  // Erases the complete partition. Called only from app_main before the
  // display/BLE/IMU tasks exist, after `tlog clear` set the deferred flag;
  // the erase stalls the flash cache for seconds and must never run mid-ride.
  bool erase_all();
  // Opens the partition for recording. With CONFIG_MOTO_TLOG_DISABLE (the
  // default, see sdkconfig.defaults) it
  // returns false without touching flash, and erase_all() does the same.
  bool begin(const TlogSessionInfo& session);
  bool ready() const { return ready_; }
  bool full() const { return full_; }
  // A sector at the append point was not erased. Recording stopped. The
  // deferred `tlog clear` path (or a host parttool erase) resets the partition;
  // the logger never erases a sector while a ride is running.
  bool dirty() const { return dirty_; }

  void append(std::uint64_t now_ms, const RawImuSample& raw,
              const moto::motion::AttitudeEstimate& attitude);
  void flush();
  // Appends a fresh session record (mode changed by FIFO fallback or A/B).
  void note_mode(std::uint8_t mode, std::uint16_t odr_hz,
                 std::uint16_t sample_hz);
  // Acquisition health, written every 10 s. This is the on-disk half of the
  // `imu stats` contract; replay prints it as `# imu ...`.
  void append_stats(const ImuStatsRecord& stats);
  // One phone GNSS speed as SpeedFusion received it. Sensor task only.
  void append_gnss(const GnssRecord& record);

  std::uint32_t records() const { return seq_; }
  std::uint32_t write_errors() const { return write_errors_; }
  std::uint32_t blocks() const { return block_seq_; }

 private:
  bool ensure_partition();
  bool run_selftest();
  bool erase_sector(std::uint32_t offset);
  bool sector_is_erased(std::uint32_t offset);
  bool write_block();
  bool start_block();
  std::uint32_t find_append_offset();

  const void* partition_ = nullptr;  // esp_partition_t*
  TlogSessionInfo session_;
  std::uint32_t capacity_ = 0;
  std::uint32_t block_base_ = 0;
  std::uint32_t block_seq_ = 0;
  std::uint32_t boot_id_ = 0;
  std::uint32_t seq_ = 0;
  std::uint32_t first_sample_seq_ = 0;
  std::uint16_t payload_used_ = 0;
  std::uint32_t write_errors_ = 0;
  std::uint64_t last_flush_ms_ = 0;
  bool wrote_session_meta_ = false;
  bool block_dirty_ = false;
  bool ready_ = false;
  bool full_ = false;
  bool dirty_ = false;
  alignas(4) std::uint8_t block_[kBlockSize] = {};
};

}  // namespace moto::device
