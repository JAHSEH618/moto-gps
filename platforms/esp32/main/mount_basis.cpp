#include "mount_basis.hpp"

#include <algorithm>
#include <cmath>

namespace moto::device {
namespace {

void put_u16(std::uint8_t* out, std::uint16_t value) {
  out[0] = static_cast<std::uint8_t>(value & 0xFFU);
  out[1] = static_cast<std::uint8_t>((value >> 8U) & 0xFFU);
}

std::uint16_t get_u16(const std::uint8_t* in) {
  return static_cast<std::uint16_t>(in[0]) |
         static_cast<std::uint16_t>(static_cast<std::uint16_t>(in[1]) << 8U);
}

std::int16_t to_scaled(float value) {
  const float scaled = std::clamp(value, -1.0F, 1.0F) * kMountScale;
  return static_cast<std::int16_t>(std::lround(scaled));
}

float from_scaled(std::int16_t value) {
  return static_cast<float>(value) / kMountScale;
}

// CRC16-CCITT, the same polynomial family the BLE frame codec uses. A mount
// basis is small; this exists to reject a torn write, not to resist an attack.
std::uint16_t crc16(const std::uint8_t* data, std::size_t size) {
  std::uint16_t crc = 0xFFFFU;
  for (std::size_t i = 0; i < size; ++i) {
    crc ^= static_cast<std::uint16_t>(data[i]) << 8U;
    for (int bit = 0; bit < 8; ++bit) {
      crc = (crc & 0x8000U) != 0U ? static_cast<std::uint16_t>((crc << 1U) ^ 0x1021U)
                                  : static_cast<std::uint16_t>(crc << 1U);
    }
  }
  return crc;
}

}  // namespace

moto::motion::MountCalibration StoredMount::calibration() const {
  moto::motion::MountCalibration mount;
  mount.set_basis(forward, up, true);
  mount.set_trim_deg(static_cast<float>(trim_cdeg) / 100.0F);
  if (static_magnitude_mps2 > 5.0F && static_magnitude_mps2 < 15.0F) {
    mount.static_magnitude_mps2 = static_magnitude_mps2;
  }
  return mount;
}

std::array<std::uint8_t, kEncodedMountSize> encode_mount(
    const StoredMount& mount) {
  std::array<std::uint8_t, kEncodedMountSize> out{};
  out[0] = kMountVersion;
  put_u16(&out[1], static_cast<std::uint16_t>(mount.trim_cdeg));
  put_u16(&out[3], static_cast<std::uint16_t>(to_scaled(mount.forward.x)));
  put_u16(&out[5], static_cast<std::uint16_t>(to_scaled(mount.forward.y)));
  put_u16(&out[7], static_cast<std::uint16_t>(to_scaled(mount.forward.z)));
  put_u16(&out[9], static_cast<std::uint16_t>(to_scaled(mount.up.x)));
  put_u16(&out[11], static_cast<std::uint16_t>(to_scaled(mount.up.y)));
  put_u16(&out[13], static_cast<std::uint16_t>(to_scaled(mount.up.z)));
  put_u16(&out[15], static_cast<std::uint16_t>(std::lround(
                        std::clamp(mount.static_magnitude_mps2, 0.0F, 65.0F) *
                        100.0F)));
  put_u16(&out[17], crc16(out.data(), kEncodedMountSize - 2));
  return out;
}

StoredMount decode_mount(const std::uint8_t* data, std::size_t size) {
  StoredMount mount;
  if (data == nullptr || size < kEncodedMountSize) return mount;
  if (data[0] != kMountVersion) return mount;
  if (crc16(data, kEncodedMountSize - 2) != get_u16(data + 17)) return mount;

  StoredMount decoded;
  decoded.trim_cdeg = static_cast<std::int16_t>(get_u16(data + 1));
  decoded.forward = {from_scaled(static_cast<std::int16_t>(get_u16(data + 3))),
                     from_scaled(static_cast<std::int16_t>(get_u16(data + 5))),
                     from_scaled(static_cast<std::int16_t>(get_u16(data + 7)))};
  decoded.up = {from_scaled(static_cast<std::int16_t>(get_u16(data + 9))),
                from_scaled(static_cast<std::int16_t>(get_u16(data + 11))),
                from_scaled(static_cast<std::int16_t>(get_u16(data + 13)))};

  const float forward_length = std::sqrt(
      decoded.forward.x * decoded.forward.x +
      decoded.forward.y * decoded.forward.y +
      decoded.forward.z * decoded.forward.z);
  const float up_length =
      std::sqrt(decoded.up.x * decoded.up.x + decoded.up.y * decoded.up.y +
                decoded.up.z * decoded.up.z);
  if (forward_length < 0.5F || up_length < 0.5F) return mount;
  if (std::fabs(decoded.trim_cdeg) > 500) return mount;
  const float magnitude = static_cast<float>(get_u16(data + 15)) / 100.0F;
  if (magnitude < 5.0F || magnitude > 15.0F) return mount;
  decoded.static_magnitude_mps2 = magnitude;

  decoded.valid = true;
  return decoded;
}

}  // namespace moto::device
