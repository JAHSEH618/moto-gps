#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "moto/ble_protocol/ble_protocol.hpp"

namespace moto::device {

enum class DeviceThemeMode : std::uint8_t {
  Official = 0,
  Auto = 1,
  Day = 2,
  Night = 3,
};

enum class DeviceUnits : std::uint8_t {
  Kph = 0,
  Mph = 1,
};

// Navigation is always visible; the remaining bits let Settings hide optional
// instrument pages without changing the page enum or the BLE snapshot.
enum PageVisibility : std::uint8_t {
  PageNavigation = 1U << 0U,
  PagePerformance = 1U << 1U,
  PageLean = 1U << 2U,
  PageSpeed = 1U << 3U,
  PageCompass = 1U << 4U,
  PageMusic = 1U << 5U,
};

constexpr std::uint8_t kAllPages =
    PageNavigation | PagePerformance | PageLean | PageSpeed | PageCompass |
    PageMusic;

constexpr std::uint8_t kSettingsVersion = 1;
constexpr std::size_t kEncodedSettingsSize = 9;
constexpr std::int16_t kMaximumLeanOffsetCdeg = 30 * 100;

struct DeviceSettings {
  DeviceThemeMode theme = DeviceThemeMode::Auto;
  std::uint8_t brightness_percent = 80;
  DeviceUnits units = DeviceUnits::Kph;
  std::uint8_t lean_full_scale_deg = 45;
  std::uint16_t performance_target_kph = 100;
  std::uint8_t visible_pages = kAllPages;
  // Calibration offset applied to the measured Lean, in centidegrees.
  std::int16_t lean_zero_offset_cdeg = 0;

  // Snap every field to a supported value. Safe to call on decoded or
  // externally supplied settings before applying them.
  void clamp();

  // Serialise a clamped copy into the fixed NVS blob layout.
  [[nodiscard]] std::array<std::uint8_t, kEncodedSettingsSize> encoded() const;

  // Unknown versions, short buffers and missing data fall back to defaults.
  [[nodiscard]] static DeviceSettings decode(const std::uint8_t* data,
                                             std::size_t size);

  bool operator==(const DeviceSettings& rhs) const noexcept;
  bool operator!=(const DeviceSettings& rhs) const noexcept {
    return !(*this == rhs);
  }
};

// Wire adapter for the shared SettingsState contract. The device owns the
// applied revision; the phone only supplies a request. `to_settings_state`
// asserts nothing (call `clamp()` first) while `from_settings_state` clamps,
// because a decoded or replayed message is external input.
//
// device_settings_tests pins this struct and the wire struct field by field,
// so adding a field to one without the other fails the native suite.
[[nodiscard]] moto::ble::SettingsState to_settings_state(
    const DeviceSettings& settings, std::uint32_t revision);
[[nodiscard]] DeviceSettings from_settings_state(
    const moto::ble::SettingsState& state);

}  // namespace moto::device
