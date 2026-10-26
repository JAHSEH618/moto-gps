#include "battery_ring.hpp"

#include <cstdlib>
#include <iostream>

namespace {

int failures = 0;

#define CHECK(condition)                                                   \
  do {                                                                     \
    if (!(condition)) {                                                    \
      std::cerr << __FILE__ << ':' << __LINE__                             \
                << " CHECK failed: " #condition << '\n';                  \
      ++failures;                                                          \
    }                                                                      \
  } while (false)

using moto::device::battery_ring;

void test_the_pmic_reading_maps_to_the_ring() {
  // No reading: no ring.
  CHECK(battery_ring(false, true, true, 80).source == MOTO_UI_POWER_UNKNOWN);

  // On the battery: its charge.
  auto ring = battery_ring(true, false, true, 62);
  CHECK(ring.source == MOTO_UI_POWER_BATTERY);
  CHECK(ring.percent == 62);

  // On USB, charging: the cell's charge, with the bolt.
  ring = battery_ring(true, true, true, 15);
  CHECK(ring.source == MOTO_UI_POWER_EXTERNAL);
  CHECK(ring.percent == 15);

  // On USB with no cell fitted: a full ring with the bolt.
  ring = battery_ring(true, true, false, 0);
  CHECK(ring.source == MOTO_UI_POWER_EXTERNAL);
  CHECK(ring.percent == 100);

  // A gauge past 100 is clamped, not drawn as more than a ring.
  CHECK(battery_ring(true, false, true, 0xFF).percent == 100);

  // Neither supply: the PMIC is wrong, so nothing is claimed.
  CHECK(battery_ring(true, false, false, 50).source == MOTO_UI_POWER_UNKNOWN);
}

}  // namespace

int main() {
  test_the_pmic_reading_maps_to_the_ring();
  if (failures != 0) {
    std::cerr << failures << " battery ring checks failed\n";
    return EXIT_FAILURE;
  }
  std::cout << "All battery ring checks passed\n";
  return EXIT_SUCCESS;
}
