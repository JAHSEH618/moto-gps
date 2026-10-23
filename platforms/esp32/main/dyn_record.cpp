#include "dyn_record.hpp"

#include <cstring>

namespace moto::device {
namespace {

void put_u16(std::uint8_t* out, std::uint16_t value) {
  out[0] = static_cast<std::uint8_t>(value & 0xFFU);
  out[1] = static_cast<std::uint8_t>((value >> 8U) & 0xFFU);
}

void put_u32(std::uint8_t* out, std::uint32_t value) {
  out[0] = static_cast<std::uint8_t>(value & 0xFFU);
  out[1] = static_cast<std::uint8_t>((value >> 8U) & 0xFFU);
  out[2] = static_cast<std::uint8_t>((value >> 16U) & 0xFFU);
  out[3] = static_cast<std::uint8_t>((value >> 24U) & 0xFFU);
}

std::uint16_t get_u16(const std::uint8_t* in) {
  return static_cast<std::uint16_t>(in[0]) |
         static_cast<std::uint16_t>(static_cast<std::uint16_t>(in[1]) << 8U);
}

std::uint32_t get_u32(const std::uint8_t* in) {
  return static_cast<std::uint32_t>(in[0]) |
         (static_cast<std::uint32_t>(in[1]) << 8U) |
         (static_cast<std::uint32_t>(in[2]) << 16U) |
         (static_cast<std::uint32_t>(in[3]) << 24U);
}

std::uint32_t crc32_table_entry(std::uint32_t index) {
  std::uint32_t crc = index;
  for (int bit = 0; bit < 8; ++bit) {
    crc = (crc & 1U) != 0U ? (crc >> 1U) ^ 0xEDB88320U : crc >> 1U;
  }
  return crc;
}

}  // namespace

const std::array<std::uint8_t, 4> kBlockMagic = {'M', 'T', 'L', 'G'};

std::uint32_t block_crc32(const std::uint8_t* block) {
  static std::uint32_t table[256];
  static bool ready = false;
  if (!ready) {
    for (std::uint32_t i = 0; i < 256; ++i) table[i] = crc32_table_entry(i);
    ready = true;
  }
  std::uint32_t crc = 0xFFFFFFFFU;
  for (std::size_t i = 0; i < kBlockSize; ++i) {
    // The CRC field itself is part of the block but must read as zero while
    // the checksum is computed.
    const std::uint8_t byte =
        (i >= 24 && i < 28) ? 0U : block[i];
    crc = table[(crc ^ byte) & 0xFFU] ^ (crc >> 8U);
  }
  return crc ^ 0xFFFFFFFFU;
}

std::array<std::uint8_t, kBlockHeaderSize> encode_block_header(
    const BlockHeader& header) {
  std::array<std::uint8_t, kBlockHeaderSize> out{};
  out[0] = kBlockMagic[0];
  out[1] = kBlockMagic[1];
  out[2] = kBlockMagic[2];
  out[3] = kBlockMagic[3];
  out[4] = kBlockVersion;
  out[5] = static_cast<std::uint8_t>(kBlockHeaderSize);
  put_u16(&out[6], static_cast<std::uint16_t>(kBlockSize));
  put_u32(&out[8], header.block_seq);
  put_u32(&out[12], header.boot_id);
  put_u32(&out[16], header.first_sample_seq);
  put_u16(&out[20], header.payload_len);
  put_u16(&out[22], 0);
  put_u32(&out[24], header.crc32);
  return out;
}

bool decode_block_header(const std::uint8_t* data, BlockHeader* header) {
  if (data == nullptr || header == nullptr) return false;
  if (data[0] != kBlockMagic[0] || data[1] != kBlockMagic[1] ||
      data[2] != kBlockMagic[2] || data[3] != kBlockMagic[3]) {
    return false;
  }
  if (data[4] != kBlockVersion || data[5] != kBlockHeaderSize ||
      get_u16(data + 6) != kBlockSize) {
    return false;
  }
  header->block_seq = get_u32(data + 8);
  header->boot_id = get_u32(data + 12);
  header->first_sample_seq = get_u32(data + 16);
  header->payload_len = get_u16(data + 20);
  header->crc32 = get_u32(data + 24);
  if (header->payload_len > kBlockSize - kBlockHeaderSize) return false;
  return true;
}

std::array<std::uint8_t, kDynRecordSize> encode_dyn(const DynRecord& r) {
  std::array<std::uint8_t, kDynRecordSize> out{};
  put_u32(&out[0], r.t_ms);
  put_u16(&out[4], r.seq);
  put_u16(&out[6], static_cast<std::uint16_t>(r.ax_cms2));
  put_u16(&out[8], static_cast<std::uint16_t>(r.ay_cms2));
  put_u16(&out[10], static_cast<std::uint16_t>(r.az_cms2));
  put_u16(&out[12], static_cast<std::uint16_t>(r.gx_dds));
  put_u16(&out[14], static_cast<std::uint16_t>(r.gy_dds));
  put_u16(&out[16], static_cast<std::uint16_t>(r.gz_dds));
  put_u16(&out[18], static_cast<std::uint16_t>(r.temp_cdeg));
  put_u16(&out[20], static_cast<std::uint16_t>(r.lean_cdeg));
  put_u16(&out[22], static_cast<std::uint16_t>(r.lean_rate_dds));
  put_u16(&out[24], static_cast<std::uint16_t>(r.lean_eff_cdeg));
  put_u16(&out[26], static_cast<std::uint16_t>(r.lateral_g_cg));
  out[28] = r.flags;
  out[29] = r.quality;
  return out;
}

bool decode_dyn(const std::uint8_t* data, std::size_t size, DynRecord* r) {
  if (data == nullptr || r == nullptr || size < kDynRecordSize) return false;
  r->t_ms = get_u32(data + 0);
  r->seq = get_u16(data + 4);
  r->ax_cms2 = static_cast<std::int16_t>(get_u16(data + 6));
  r->ay_cms2 = static_cast<std::int16_t>(get_u16(data + 8));
  r->az_cms2 = static_cast<std::int16_t>(get_u16(data + 10));
  r->gx_dds = static_cast<std::int16_t>(get_u16(data + 12));
  r->gy_dds = static_cast<std::int16_t>(get_u16(data + 14));
  r->gz_dds = static_cast<std::int16_t>(get_u16(data + 16));
  r->temp_cdeg = static_cast<std::int16_t>(get_u16(data + 18));
  r->lean_cdeg = static_cast<std::int16_t>(get_u16(data + 20));
  r->lean_rate_dds = static_cast<std::int16_t>(get_u16(data + 22));
  r->lean_eff_cdeg = static_cast<std::int16_t>(get_u16(data + 24));
  r->lateral_g_cg = static_cast<std::int16_t>(get_u16(data + 26));
  r->flags = data[28];
  r->quality = data[29];
  return true;
}

std::array<std::uint8_t, kGnssRecordSize> encode_gnss(const GnssRecord& r) {
  std::array<std::uint8_t, kGnssRecordSize> out{};
  put_u32(&out[0], r.t_rx_ms);
  put_u32(&out[4], r.fix_ms);
  put_u16(&out[8], r.speed_cms);
  put_u16(&out[10], r.course_cdeg);
  put_u16(&out[12], r.speed_acc_cms);
  out[14] = r.flags;
  return out;
}

bool decode_gnss(const std::uint8_t* data, std::size_t size, GnssRecord* r) {
  if (data == nullptr || r == nullptr || size < kGnssRecordSize) return false;
  r->t_rx_ms = get_u32(data + 0);
  r->fix_ms = get_u32(data + 4);
  r->speed_cms = get_u16(data + 8);
  r->course_cdeg = get_u16(data + 10);
  r->speed_acc_cms = get_u16(data + 12);
  r->flags = data[14];
  return true;
}

std::array<std::uint8_t, kMountRecordSize> encode_mount_record(
    const MountRecord& r) {
  std::array<std::uint8_t, kMountRecordSize> out{};
  for (int i = 0; i < 3; ++i) {
    put_u16(&out[i * 2], static_cast<std::uint16_t>(r.forward[i]));
    put_u16(&out[6 + i * 2], static_cast<std::uint16_t>(r.up[i]));
  }
  put_u16(&out[12], static_cast<std::uint16_t>(r.trim_cdeg));
  return out;
}

bool decode_mount_record(const std::uint8_t* data, std::size_t size,
                         MountRecord* r) {
  if (data == nullptr || r == nullptr || size < kMountRecordSize) return false;
  for (int i = 0; i < 3; ++i) {
    r->forward[i] = static_cast<std::int16_t>(get_u16(data + i * 2));
    r->up[i] = static_cast<std::int16_t>(get_u16(data + 6 + i * 2));
  }
  r->trim_cdeg = static_cast<std::int16_t>(get_u16(data + 12));
  return true;
}

std::array<std::uint8_t, kSessionMetaSize> encode_session_meta(
    const SessionMeta& m) {
  std::array<std::uint8_t, kSessionMetaSize> out{};
  out[0] = 1;  // meta version
  out[1] = m.mode;
  put_u16(&out[2], m.odr_hz);
  put_u16(&out[4], m.sample_hz);
  put_u16(&out[6], static_cast<std::uint16_t>(m.lever_arm_mm));
  put_u16(&out[8], m.kinematic_tau_ms);
  put_u16(&out[10], m.static_tau_ms);
  put_u16(&out[12], m.zero_tau_ms);
  put_u16(&out[14], m.phi_ddot_gate_dps2);
  put_u32(&out[16], m.firmware_hash);
  const auto mount = encode_mount_record(m.mount);
  std::memcpy(&out[20], mount.data(), mount.size());
  return out;
}

bool decode_session_meta(const std::uint8_t* data, std::size_t size,
                         SessionMeta* m) {
  if (data == nullptr || m == nullptr || size < kSessionMetaSize) return false;
  if (data[0] != 1) return false;
  m->mode = data[1];
  m->odr_hz = get_u16(data + 2);
  m->sample_hz = get_u16(data + 4);
  m->lever_arm_mm = static_cast<std::int16_t>(get_u16(data + 6));
  m->kinematic_tau_ms = get_u16(data + 8);
  m->static_tau_ms = get_u16(data + 10);
  m->zero_tau_ms = get_u16(data + 12);
  m->phi_ddot_gate_dps2 = get_u16(data + 14);
  m->firmware_hash = get_u32(data + 16);
  return decode_mount_record(data + 20, size - 20, &m->mount);
}

}  // namespace moto::device
