#include "device_settings.hpp"

#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>

namespace {

using moto::device::DeviceSettings;
using moto::device::DeviceThemeMode;
using moto::device::DeviceUnits;
using moto::device::PageCompass;
using moto::device::PageLean;
using moto::device::PageMusic;
using moto::device::PageNavigation;
using moto::device::PagePerformance;
using moto::device::PageSpeed;
using moto::device::kAllPages;
using moto::device::kMaximumLeanOffsetCdeg;
using moto::device::kEncodedSettingsSize;

int failures = 0;

#define CHECK(condition)                                                   \
  do {                                                                     \
    if (!(condition)) {                                                    \
      std::cerr << __FILE__ << ':' << __LINE__                             \
                << " CHECK failed: " #condition << '\n';                   \
      ++failures;                                                          \
    }                                                                      \
  } while (false)

void test_motorcycle_defaults() {
  const DeviceSettings settings;
  CHECK(settings.theme == DeviceThemeMode::Auto);
  CHECK(settings.brightness_percent == 80);
  CHECK(settings.units == DeviceUnits::Kph);
  CHECK(settings.lean_full_scale_deg == 45);
  CHECK(settings.performance_target_kph == 100);
  CHECK(settings.visible_pages == kAllPages);
  CHECK(settings.lean_zero_offset_cdeg == 0);
}

void test_clamp_snaps_to_supported_values() {
  DeviceSettings settings;
  settings.theme = static_cast<DeviceThemeMode>(99);
  settings.brightness_percent = 150;
  settings.units = static_cast<DeviceUnits>(9);
  settings.lean_full_scale_deg = 37;
  settings.performance_target_kph = 80;
  settings.visible_pages = 0xFF;
  settings.lean_zero_offset_cdeg = 9'999;
  settings.clamp();

  CHECK(settings.theme == DeviceThemeMode::Auto);
  CHECK(settings.brightness_percent == 100);
  CHECK(settings.units == DeviceUnits::Kph);
  CHECK(settings.lean_full_scale_deg == 40);
  CHECK(settings.performance_target_kph == 100);
  CHECK(settings.visible_pages == kAllPages);
  CHECK(settings.lean_zero_offset_cdeg == 3'000);

  settings.lean_full_scale_deg = 43;
  settings.performance_target_kph = 55;
  settings.lean_zero_offset_cdeg = -9'999;
  settings.clamp();
  CHECK(settings.lean_full_scale_deg == 45);
  CHECK(settings.performance_target_kph == 60);
  CHECK(settings.lean_zero_offset_cdeg == -3'000);
}

void test_navigation_page_cannot_be_hidden() {
  DeviceSettings settings;
  settings.visible_pages = PageSpeed | PageMusic;
  settings.clamp();
  CHECK(settings.visible_pages == (PageSpeed | PageMusic | PageNavigation));
}

void test_encode_decode_round_trip() {
  DeviceSettings settings;
  settings.theme = DeviceThemeMode::Night;
  settings.brightness_percent = 55;
  settings.units = DeviceUnits::Mph;
  settings.lean_full_scale_deg = 50;
  settings.performance_target_kph = 60;
  settings.visible_pages =
      PageNavigation | PagePerformance | PageLean | PageSpeed;
  settings.lean_zero_offset_cdeg = -1'234;

  const auto bytes = settings.encoded();
  CHECK(bytes.size() == kEncodedSettingsSize);
  const DeviceSettings decoded =
      DeviceSettings::decode(bytes.data(), bytes.size());
  CHECK(decoded == settings);
}

void test_unknown_or_short_blobs_fall_back_to_defaults() {
  auto bytes = DeviceSettings{}.encoded();
  bytes[0] = 2;
  CHECK(DeviceSettings::decode(bytes.data(), bytes.size()) ==
        DeviceSettings{});

  CHECK(DeviceSettings::decode(nullptr, 0) == DeviceSettings{});
  CHECK(DeviceSettings::decode(bytes.data(), 4) == DeviceSettings{});
}

void test_tampered_blob_is_clamped() {
  auto bytes = DeviceSettings{}.encoded();
  bytes[2] = 255;  // brightness
  bytes[4] = 37;   // lean full scale
  bytes[5] = 80;   // performance target
  bytes[6] = 0xFF; // visible pages
  bytes[7] = 0xFF; // lean offset low byte
  bytes[8] = 0x7F; // lean offset high byte -> 32767

  const DeviceSettings decoded =
      DeviceSettings::decode(bytes.data(), bytes.size());
  CHECK(decoded.brightness_percent == 100);
  CHECK(decoded.lean_full_scale_deg == 40);
  CHECK(decoded.performance_target_kph == 100);
  CHECK(decoded.visible_pages == kAllPages);
  CHECK(decoded.lean_zero_offset_cdeg == 3'000);
}

// The BLE contract and the NVS blob are two descriptions of one thing. If a
// field is added to either one, the pin fails instead of the device silently
// dropping a setting the phone believes it applied.
void test_ble_contract_matches() {
  using moto::ble::SettingsState;
  using moto::ble::SettingsTheme;
  using moto::ble::SettingsUnits;

  CHECK(static_cast<std::uint8_t>(DeviceThemeMode::Official) ==
        static_cast<std::uint8_t>(SettingsTheme::Official));
  CHECK(static_cast<std::uint8_t>(DeviceThemeMode::Auto) ==
        static_cast<std::uint8_t>(SettingsTheme::Auto));
  CHECK(static_cast<std::uint8_t>(DeviceThemeMode::Day) ==
        static_cast<std::uint8_t>(SettingsTheme::Day));
  CHECK(static_cast<std::uint8_t>(DeviceThemeMode::Night) ==
        static_cast<std::uint8_t>(SettingsTheme::Night));
  CHECK(static_cast<std::uint8_t>(DeviceUnits::Kph) ==
        static_cast<std::uint8_t>(SettingsUnits::Kph));
  CHECK(static_cast<std::uint8_t>(DeviceUnits::Mph) ==
        static_cast<std::uint8_t>(SettingsUnits::Mph));

  CHECK(static_cast<std::uint8_t>(PageNavigation) ==
        static_cast<std::uint8_t>(moto::ble::SettingsPageNavigation));
  CHECK(static_cast<std::uint8_t>(PagePerformance) ==
        static_cast<std::uint8_t>(moto::ble::SettingsPagePerformance));
  CHECK(static_cast<std::uint8_t>(PageLean) ==
        static_cast<std::uint8_t>(moto::ble::SettingsPageLean));
  CHECK(static_cast<std::uint8_t>(PageSpeed) ==
        static_cast<std::uint8_t>(moto::ble::SettingsPageSpeed));
  CHECK(static_cast<std::uint8_t>(PageCompass) ==
        static_cast<std::uint8_t>(moto::ble::SettingsPageCompass));
  CHECK(static_cast<std::uint8_t>(PageMusic) ==
        static_cast<std::uint8_t>(moto::ble::SettingsPageMusic));
  CHECK(kAllPages == moto::ble::kSettingsAllPages);
  CHECK(kMaximumLeanOffsetCdeg == moto::ble::kMaxSettingsLeanOffsetCdeg);

  // Defaults must agree, otherwise a firmware that has never received 0x17
  // runs different settings from a phone that assumes the defaults.
  const SettingsState defaults =
      moto::device::to_settings_state(DeviceSettings{}, 1);
  const SettingsState expected{};
  CHECK(defaults.theme == expected.theme);
  CHECK(defaults.brightness_percent == expected.brightness_percent);
  CHECK(defaults.units == expected.units);
  CHECK(defaults.lean_full_scale_deg == expected.lean_full_scale_deg);
  CHECK(defaults.performance_target_kph == expected.performance_target_kph);
  CHECK(defaults.visible_pages == expected.visible_pages);
  CHECK(defaults.lean_zero_offset_cdeg == expected.lean_zero_offset_cdeg);

  // Every field survives a trip to the wire struct and back, and a clamped
  // local value encodes as something the wire validation accepts.
  DeviceSettings value;
  value.theme = DeviceThemeMode::Night;
  value.brightness_percent = 45;
  value.units = DeviceUnits::Mph;
  value.lean_full_scale_deg = 50;
  value.performance_target_kph = 60;
  value.visible_pages = static_cast<std::uint8_t>(kAllPages & ~PageMusic);
  value.lean_zero_offset_cdeg = 175;
  const SettingsState state = moto::device::to_settings_state(value, 9);
  CHECK(state.revision == 9);
  CHECK(state.theme == SettingsTheme::Night);
  CHECK(state.brightness_percent == 45);
  CHECK(state.units == SettingsUnits::Mph);
  CHECK(state.lean_full_scale_deg == 50);
  CHECK(state.performance_target_kph == 60);
  CHECK(state.visible_pages == value.visible_pages);
  CHECK(state.lean_zero_offset_cdeg == 175);
  CHECK(moto::ble::encode_message(moto::ble::Message{state}).ok());
  CHECK(moto::device::from_settings_state(state) == value);

  // The clamp and the wire validation must agree on what is legal: a value
  // the device clamps in must be encodable, and one it refuses must not be.
  const std::uint8_t scales[] = {0, 31, 35, 40, 44, 45, 49, 50, 90};
  for (const std::uint8_t scale : scales) {
    DeviceSettings candidate = value;
    candidate.lean_full_scale_deg = scale;
    candidate.clamp();
    CHECK(moto::ble::encode_message(moto::ble::Message{
              moto::device::to_settings_state(candidate, 9)})
              .ok());
    CHECK(candidate == moto::device::from_settings_state(
                           moto::device::to_settings_state(candidate, 9)));
  }
}

}  // namespace

int main() {
  test_motorcycle_defaults();
  test_clamp_snaps_to_supported_values();
  test_navigation_page_cannot_be_hidden();
  test_encode_decode_round_trip();
  test_unknown_or_short_blobs_fall_back_to_defaults();
  test_tampered_blob_is_clamped();
  test_ble_contract_matches();

  if (failures != 0) {
    std::cerr << failures << " device_settings checks failed\n";
    return EXIT_FAILURE;
  }
  std::cout << "All device_settings checks passed\n";
  return EXIT_SUCCESS;
}
