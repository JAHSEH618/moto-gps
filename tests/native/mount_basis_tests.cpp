// att.rmount persistence (DESIGN_V2 §3.3). Pure encode/decode, so the NVS
// wrapper stays a thin read/write and this suite can prove the format.
#include "mount_basis.hpp"

#include <cmath>
#include <cstdlib>
#include <iostream>

namespace {

using moto::device::decode_mount;
using moto::device::encode_mount;
using moto::device::kEncodedMountSize;
using moto::device::StoredMount;

int failures = 0;

#define CHECK(condition)                                                   \
  do {                                                                     \
    if (!(condition)) {                                                    \
      std::cerr << __FILE__ << ':' << __LINE__                             \
                << " CHECK failed: " #condition << '\n';                   \
      ++failures;                                                          \
    }                                                                      \
  } while (false)

void check_near(float actual, float expected, float tolerance) {
  CHECK(std::fabs(actual - expected) <= tolerance);
}

void test_round_trip() {
  StoredMount mount;
  mount.forward = {0.0F, 0.0F, -1.0F};
  mount.up = {0.0F, 1.0F, 0.0F};
  mount.trim_cdeg = -125;
  mount.valid = true;

  const auto blob = encode_mount(mount);
  const StoredMount decoded =
      decode_mount(blob.data(), blob.size());
  CHECK(decoded.valid);
  check_near(decoded.forward.x, 0.0F, 0.0001F);
  check_near(decoded.forward.y, 0.0F, 0.0001F);
  check_near(decoded.forward.z, -1.0F, 0.0001F);
  check_near(decoded.up.x, 0.0F, 0.0001F);
  check_near(decoded.up.y, 1.0F, 0.0001F);
  check_near(decoded.up.z, 0.0F, 0.0001F);
  CHECK(decoded.trim_cdeg == -125);

  const auto calibration = decoded.calibration();
  CHECK(calibration.stage1);
  check_near(calibration.trim_deg, -1.25F, 0.001F);
}

void test_corrupt_blob_is_rejected() {
  StoredMount mount;
  mount.valid = true;
  auto blob = encode_mount(mount);
  blob[5] ^= 0x40U;
  const StoredMount decoded = decode_mount(blob.data(), blob.size());
  CHECK(!decoded.valid);
}

void test_short_and_wrong_version_are_rejected() {
  StoredMount mount;
  mount.valid = true;
  const auto blob = encode_mount(mount);
  CHECK(!decode_mount(blob.data(), kEncodedMountSize - 1).valid);

  auto wrong_version = blob;
  wrong_version[0] = 2;
  CHECK(!decode_mount(wrong_version.data(), wrong_version.size()).valid);
}

void test_degenerate_basis_is_rejected() {
  StoredMount mount;
  mount.forward = {0.0F, 0.0F, 0.0F};
  mount.up = {0.0F, 0.0F, 0.0F};
  mount.valid = true;
  const auto blob = encode_mount(mount);
  CHECK(!decode_mount(blob.data(), blob.size()).valid);
}

void test_trim_out_of_range_is_rejected() {
  StoredMount mount;
  mount.trim_cdeg = 501;
  mount.valid = true;
  const auto blob = encode_mount(mount);
  CHECK(!decode_mount(blob.data(), blob.size()).valid);
}

}  // namespace

int main() {
  test_round_trip();
  test_corrupt_blob_is_rejected();
  test_short_and_wrong_version_are_rejected();
  test_degenerate_basis_is_rejected();
  test_trim_out_of_range_is_rejected();

  if (failures != 0) {
    std::cerr << failures << " mount_basis checks failed\n";
    return EXIT_FAILURE;
  }
  std::cout << "All mount_basis checks passed\n";
  return EXIT_SUCCESS;
}
