#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace moto::device {

// Development telemetry stream (DESIGN_V2 §11.2, review: record before
// SpeedFusion so the BLE delay and the estimator parameters come from data).
//
// The file is a 24-byte header followed by fixed-size records. Every field is
// packed explicitly little-endian; nothing depends on struct padding or host
// endianness, because the same bytes are written by the ESP32 and replayed by
// a native tool.
constexpr std::size_t kTelemetryHeaderSize = 24;
constexpr std::size_t kDynRecordSize = 32;
constexpr std::size_t kGnssRecordSize = 20;
constexpr std::uint8_t kTelemetryVersion = 1;

// On disk each record is a one-byte type followed by its fixed payload, so a
// reader can walk a file that interleaves IMU and phone records.
enum RecordType : std::uint8_t {
  kRecordDyn = 1,
  kRecordGnss = 2,
  kRecordMount = 3,
};
constexpr std::size_t kMountRecordSize = 14;

extern const std::array<std::uint8_t, 4> kTelemetryMagic;

struct TelemetryHeader {
  std::uint32_t session_epoch = 0;
  std::uint32_t start_ms = 0;
  std::uint16_t sample_hz = 112;
  std::uint16_t flags = 0;
};

// 112 Hz raw IMU (sensor frame, as read) plus the estimator's output at that
// instant. Storing the raw sample, not just the result, is what lets a replay
// re-run a changed estimator on the same ride.
struct DynRecord {
  std::uint32_t t_ms = 0;
  std::uint16_t seq = 0;
  std::int16_t ax_cms2 = 0;
  std::int16_t ay_cms2 = 0;
  std::int16_t az_cms2 = 0;
  std::int16_t gx_dds = 0;    // 0.1 deg/s
  std::int16_t gy_dds = 0;
  std::int16_t gz_dds = 0;
  std::int16_t temp_cdeg = 0;  // 0.01 degC
  std::int16_t lean_cdeg = 0;
  std::int16_t lean_rate_dds = 0;
  std::int16_t lean_eff_cdeg = 0;  // INT16_MIN = unavailable
  std::int16_t lateral_g_cg = 0;   // 0.01 g, right positive
  std::uint8_t flags = 0;
  std::uint8_t quality = 0;
};

// What the phone actually sent and when the device received it. `fix_ms` is
// the fix timestamp mapped onto the device monotonic clock (0 = unknown), so
// the BLE delay distribution can be measured before `gnss_in` exists.
// The mount basis the device was using, so a replay reconstructs the same
// frame instead of silently replaying with identity axes.
struct MountRecord {
  std::int16_t forward[3] = {10000, 0, 0};
  std::int16_t up[3] = {0, 0, 10000};
  std::int16_t trim_cdeg = 0;
};

struct GnssRecord {
  std::uint32_t t_rx_ms = 0;
  std::uint32_t fix_ms = 0;
  std::uint16_t speed_cms = 0;
  std::uint16_t course_cdeg = 0;
  std::uint16_t speed_acc_cms = 0;
  std::uint8_t flags = 0;
};

std::array<std::uint8_t, kTelemetryHeaderSize> encode_header(
    const TelemetryHeader& header);
bool decode_header(const std::uint8_t* data, std::size_t size,
                   TelemetryHeader* header);

std::array<std::uint8_t, kDynRecordSize> encode_dyn(const DynRecord& record);
bool decode_dyn(const std::uint8_t* data, std::size_t size, DynRecord* record);

std::array<std::uint8_t, kGnssRecordSize> encode_gnss(const GnssRecord& record);
bool decode_gnss(const std::uint8_t* data, std::size_t size, GnssRecord* record);

std::array<std::uint8_t, kMountRecordSize> encode_mount_record(
    const MountRecord& record);
bool decode_mount_record(const std::uint8_t* data, std::size_t size,
                         MountRecord* record);

}  // namespace moto::device
