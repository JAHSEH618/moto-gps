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

}  // namespace

int main() {
  test_motorcycle_defaults();
  test_clamp_snaps_to_supported_values();
  test_navigation_page_cannot_be_hidden();
  test_encode_decode_round_trip();
  test_unknown_or_short_blobs_fall_back_to_defaults();
  test_tampered_blob_is_clamped();

  if (failures != 0) {
    std::cerr << failures << " device_settings checks failed\n";
    return EXIT_FAILURE;
  }
  std::cout << "All device_settings checks passed\n";
  return EXIT_SUCCESS;
}
