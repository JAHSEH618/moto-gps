#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "lean_estimator.hpp"

namespace moto::device {

// Persisted `att.rmount` (DESIGN_V2 §3.3, review item 5). The basis is stored
// at 1e-4 resolution, which is well below the 0.3° capture accuracy, and the
// blob carries a CRC because a half-written NVS page must not become a
// silently rotated horizon.
struct StoredMount {
  moto::motion::Vec3f forward{1.0F, 0.0F, 0.0F};
  moto::motion::Vec3f up{0.0F, 0.0F, 1.0F};
  std::int16_t trim_cdeg = 0;
  // This unit's stationary |a| in m/s^2. Restored into the estimator so the
  // static/ZUPT gate does not fall back to the nominal 1 g.
  float static_magnitude_mps2 = 9.80665F;
  bool valid = false;

  moto::motion::MountCalibration calibration() const;
};

constexpr std::size_t kEncodedMountSize = 19;
constexpr std::uint8_t kMountVersion = 2;
// int16 components in 1/10000 units: 1.0 -> 10000, negative -1.0 -> -10000.
constexpr float kMountScale = 10000.0F;

std::array<std::uint8_t, kEncodedMountSize> encode_mount(
    const StoredMount& mount);

// Returns a default (valid = false) mount for a short, corrupt, unknown-version
// or degenerate blob. Decode never throws: it runs before the sensor task.
StoredMount decode_mount(const std::uint8_t* data, std::size_t size);

}  // namespace moto::device
