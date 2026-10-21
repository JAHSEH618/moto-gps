#include "device_settings.hpp"

#include <algorithm>
#include <cstdlib>

namespace moto::device {
namespace {

constexpr std::uint8_t kLeanScales[] = {30, 40, 45, 50};
constexpr std::uint16_t kPerformanceTargets[] = {50, 60, 100};

std::uint8_t nearest_lean_scale(std::uint8_t value) {
  std::uint8_t best = kLeanScales[0];
  int best_distance = std::abs(static_cast<int>(value) - best);
  for(const std::uint8_t candidate : kLeanScales) {
    const int distance = std::abs(static_cast<int>(value) - candidate);
    if(distance < best_distance) {
      best = candidate;
      best_distance = distance;
    }
  }
  return best;
}

std::uint16_t nearest_performance_target(std::uint16_t value) {
  if(value <= kPerformanceTargets[0]) return kPerformanceTargets[0];
  std::uint16_t best = kPerformanceTargets[0];
  int best_distance = std::abs(static_cast<int>(value) - best);
  // Ties prefer the higher target: 55 -> 60, 80 -> 100.
  for(const std::uint16_t candidate : kPerformanceTargets) {
    const int distance = std::abs(static_cast<int>(value) - candidate);
    if(distance <= best_distance) {
      best = candidate;
      best_distance = distance;
    }
  }
  return best;
}

}  // namespace

void DeviceSettings::clamp() {
  if(static_cast<std::uint8_t>(theme) >
     static_cast<std::uint8_t>(DeviceThemeMode::Night)) {
    theme = DeviceThemeMode::Auto;
  }
  if(static_cast<std::uint8_t>(units) >
     static_cast<std::uint8_t>(DeviceUnits::Mph)) {
    units = DeviceUnits::Kph;
  }
  brightness_percent = std::min<std::uint8_t>(brightness_percent, 100);
  lean_full_scale_deg = nearest_lean_scale(lean_full_scale_deg);
  performance_target_kph =
      nearest_performance_target(performance_target_kph);
  visible_pages = static_cast<std::uint8_t>(visible_pages & kAllPages);
  visible_pages = static_cast<std::uint8_t>(visible_pages | PageNavigation);
  lean_zero_offset_cdeg = std::clamp<std::int16_t>(
      lean_zero_offset_cdeg, -kMaximumLeanOffsetCdeg, kMaximumLeanOffsetCdeg);
}

std::array<std::uint8_t, kEncodedSettingsSize> DeviceSettings::encoded() const {
  DeviceSettings clamped = *this;
  clamped.clamp();

  std::array<std::uint8_t, kEncodedSettingsSize> bytes{};
  bytes[0] = kSettingsVersion;
  bytes[1] = static_cast<std::uint8_t>(clamped.theme);
  bytes[2] = clamped.brightness_percent;
  bytes[3] = static_cast<std::uint8_t>(clamped.units);
  bytes[4] = clamped.lean_full_scale_deg;
  bytes[5] = static_cast<std::uint8_t>(clamped.performance_target_kph);
  bytes[6] = clamped.visible_pages;
  const std::uint16_t offset =
      static_cast<std::uint16_t>(clamped.lean_zero_offset_cdeg);
  bytes[7] = static_cast<std::uint8_t>(offset & 0xFFU);
  bytes[8] = static_cast<std::uint8_t>(offset >> 8U);
  return bytes;
}

DeviceSettings DeviceSettings::decode(const std::uint8_t* data,
                                      std::size_t size) {
  if(data == nullptr || size < kEncodedSettingsSize ||
     data[0] != kSettingsVersion) {
    return DeviceSettings{};
  }

  DeviceSettings settings;
  settings.theme = static_cast<DeviceThemeMode>(data[1]);
  settings.brightness_percent = data[2];
  settings.units = static_cast<DeviceUnits>(data[3]);
  settings.lean_full_scale_deg = data[4];
  settings.performance_target_kph = data[5];
  settings.visible_pages = data[6];
  const std::uint16_t offset = static_cast<std::uint16_t>(data[7]) |
                               (static_cast<std::uint16_t>(data[8]) << 8U);
  settings.lean_zero_offset_cdeg = static_cast<std::int16_t>(offset);
  settings.clamp();
  return settings;
}

bool DeviceSettings::operator==(const DeviceSettings& rhs) const noexcept {
  return theme == rhs.theme && brightness_percent == rhs.brightness_percent &&
         units == rhs.units &&
         lean_full_scale_deg == rhs.lean_full_scale_deg &&
         performance_target_kph == rhs.performance_target_kph &&
         visible_pages == rhs.visible_pages &&
         lean_zero_offset_cdeg == rhs.lean_zero_offset_cdeg;
}

}  // namespace moto::device
