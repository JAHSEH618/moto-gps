#include "dyn_record.hpp"

#include <algorithm>

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

std::uint16_t crc16(const std::uint8_t* data, std::size_t size) {
  std::uint16_t crc = 0xFFFFU;
  for (std::size_t i = 0; i < size; ++i) {
    crc ^= static_cast<std::uint16_t>(data[i]) << 8U;
    for (int bit = 0; bit < 8; ++bit) {
      crc = (crc & 0x8000U) != 0U
                ? static_cast<std::uint16_t>((crc << 1U) ^ 0x1021U)
                : static_cast<std::uint16_t>(crc << 1U);
    }
  }
  return crc;
}

}  // namespace

const std::array<std::uint8_t, 4> kTelemetryMagic = {'M', 'T', 'R', '1'};

std::array<std::uint8_t, kTelemetryHeaderSize> encode_header(
    const TelemetryHeader& header) {
  std::array<std::uint8_t, kTelemetryHeaderSize> out{};
  out[0] = kTelemetryMagic[0];
  out[1] = kTelemetryMagic[1];
  out[2] = kTelemetryMagic[2];
  out[3] = kTelemetryMagic[3];
  out[4] = kTelemetryVersion;
  out[5] = static_cast<std::uint8_t>(kTelemetryHeaderSize);
  out[6] = static_cast<std::uint8_t>(kDynRecordSize);
  out[7] = static_cast<std::uint8_t>(kGnssRecordSize);
  put_u32(&out[8], header.session_epoch);
  put_u32(&out[12], header.start_ms);
  put_u16(&out[16], header.sample_hz);
  put_u16(&out[18], header.flags);
  put_u16(&out[20], 0);
  put_u16(&out[22], crc16(out.data(), 22));
  return out;
}

bool decode_header(const std::uint8_t* data, std::size_t size,
                   TelemetryHeader* header) {
  if (data == nullptr || header == nullptr || size < kTelemetryHeaderSize) {
    return false;
  }
  if (data[0] != kTelemetryMagic[0] || data[1] != kTelemetryMagic[1] ||
      data[2] != kTelemetryMagic[2] || data[3] != kTelemetryMagic[3]) {
    return false;
  }
  if (data[4] != kTelemetryVersion ||
      data[5] != kTelemetryHeaderSize || data[6] != kDynRecordSize ||
      data[7] != kGnssRecordSize) {
    return false;
  }
  if (get_u16(data + 22) != crc16(data, 22)) return false;
  header->session_epoch = get_u32(data + 8);
  header->start_ms = get_u32(data + 12);
  header->sample_hz = get_u16(data + 16);
  header->flags = get_u16(data + 18);
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
  out[30] = 0;
  out[31] = 0;
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

}  // namespace moto::device
