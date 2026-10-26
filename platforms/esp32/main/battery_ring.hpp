#pragma once

#include <algorithm>
#include <cstdint>

#include "moto_nav_ui.h"

// What the bezel's battery ring shows for one PMIC reading (DESIGN_V2 §2.9).
// Pure, so the host tests can pin the table; the board's PWR task reads the
// AXP2101 and posts the result to moto_nav_ui_set_battery().
namespace moto::device {

struct BatteryRing {
  moto_ui_power_source_t source = MOTO_UI_POWER_UNKNOWN;
  std::uint8_t percent = 0;
};

inline BatteryRing battery_ring(bool read_ok, bool vbus, bool battery_present,
                                std::uint8_t gauge_percent) {
  BatteryRing ring;
  if (!read_ok) return ring;
  const auto charge = std::min<std::uint8_t>(gauge_percent, 100);
  if (vbus) {
    // On USB with no cell fitted the ring is full, with the bolt: the board
    // has all the power it can use (the rider's choice, 2026-10-26).
    ring.source = MOTO_UI_POWER_EXTERNAL;
    ring.percent = battery_present ? charge : 100;
  } else if (battery_present) {
    ring.source = MOTO_UI_POWER_BATTERY;
    ring.percent = charge;
  }
  // Neither USB nor a cell: the PMIC is not telling the truth, so no ring.
  return ring;
}

}  // namespace moto::device
