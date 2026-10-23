#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace moto::device {

// Self-describing development telemetry (v2 r1 §11.2).
//
// The tlog partition is a sequence of 4 KiB blocks. A block is erased, its
// payload written first, then its header is written last with the payload
// length and CRC32. A crash therefore leaves at most one block with no header,
// which a reader skips; nothing depends on NVS to find the append point.
constexpr std::size_t kBlockSize = 4096;
constexpr std::size_t kBlockHeaderSize = 32;
constexpr std::uint8_t kBlockVersion = 1;
constexpr std::size_t kDynRecordSize = 32;
constexpr std::size_t kGnssRecordSize = 20;
constexpr std::size_t kMountRecordSize = 14;
constexpr std::size_t kSessionMetaSize = 40;

extern const std::array<std::uint8_t, 4> kBlockMagic;

enum RecordType : std::uint8_t {
  kRecordDyn = 1,
  kRecordGnss = 2,
  kRecordMount = 3,
  kRecordSession = 4,
};

struct BlockHeader {
  std::uint32_t block_seq = 0;
  std::uint32_t boot_id = 0;
  std::uint32_t first_sample_seq = 0;
  std::uint16_t payload_len = 0;
  std::uint32_t crc32 = 0;  // of the full 4 KiB block with this field zeroed
};

struct DynRecord {
  std::uint32_t t_ms = 0;
  std::uint16_t seq = 0;
  std::int16_t ax_cms2 = 0;
  std::int16_t ay_cms2 = 0;
  std::int16_t az_cms2 = 0;
  std::int16_t gx_dds = 0;
  std::int16_t gy_dds = 0;
  std::int16_t gz_dds = 0;
  std::int16_t temp_cdeg = 0;
  std::int16_t lean_cdeg = 0;
  std::int16_t lean_rate_dds = 0;
  std::int16_t lean_eff_cdeg = 0;  // INT16_MIN = unavailable
  std::int16_t lateral_g_cg = 0;
  std::uint8_t flags = 0;
  std::uint8_t quality = 0;
};

struct GnssRecord {
  std::uint32_t t_rx_ms = 0;
  std::uint32_t fix_ms = 0;
  std::uint16_t speed_cms = 0;
  std::uint16_t course_cdeg = 0;
  std::uint16_t speed_acc_cms = 0;
  std::uint8_t flags = 0;
};

struct MountRecord {
  std::int16_t forward[3] = {10000, 0, 0};
  std::int16_t up[3] = {0, 0, 10000};
  std::int16_t trim_cdeg = 0;
};

// Replay needs the acquisition conditions to reproduce a ride: mode, actual
// ODR, mount, and the estimator's tuning at the time.
struct SessionMeta {
  std::uint8_t mode = 0;  // 0 poll, 1 FIFO
  std::uint16_t odr_hz = 0;
  std::uint16_t sample_hz = 0;
  std::int16_t lever_arm_mm = 0;
  std::uint16_t kinematic_tau_ms = 1500;
  std::uint16_t static_tau_ms = 500;
  std::uint16_t zero_tau_ms = 3000;
  std::uint16_t phi_ddot_gate_dps2 = 300;
  std::uint32_t firmware_hash = 0;
  MountRecord mount;
};

std::array<std::uint8_t, kBlockHeaderSize> encode_block_header(
    const BlockHeader& header);
bool decode_block_header(const std::uint8_t* data,
                         BlockHeader* header);

std::array<std::uint8_t, kDynRecordSize> encode_dyn(const DynRecord& record);
bool decode_dyn(const std::uint8_t* data, std::size_t size, DynRecord* record);

std::array<std::uint8_t, kGnssRecordSize> encode_gnss(const GnssRecord& record);
bool decode_gnss(const std::uint8_t* data, std::size_t size, GnssRecord* record);

std::array<std::uint8_t, kMountRecordSize> encode_mount_record(
    const MountRecord& record);
bool decode_mount_record(const std::uint8_t* data, std::size_t size,
                         MountRecord* record);

std::array<std::uint8_t, kSessionMetaSize> encode_session_meta(
    const SessionMeta& meta);
bool decode_session_meta(const std::uint8_t* data, std::size_t size,
                         SessionMeta* meta);

// CRC32 (IEEE 802.3), used over a whole block with the CRC field zeroed.
std::uint32_t block_crc32(const std::uint8_t* block);

}  // namespace moto::device
