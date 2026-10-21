#include "attitude_estimator.hpp"

#include <cmath>
#include <cstdlib>
#include <iostream>

namespace {

using moto::motion::AttitudeCalibration;
using moto::motion::AttitudeEstimate;
using moto::motion::Vec3f;
using moto::motion::estimate_attitude;

int failures = 0;

#define CHECK(condition)                                                   \
  do {                                                                     \
    if (!(condition)) {                                                    \
      std::cerr << __FILE__ << ':' << __LINE__                             \
                << " CHECK failed: " #condition << '\n';                   \
      ++failures;                                                          \
    }                                                                      \
  } while (false)

void check_near(float actual, float expected, float tolerance = 0.01F) {
  CHECK(std::abs(actual - expected) <= tolerance);
}

void test_upright_at_rest() {
  const Vec3f gravity{0.0F, 0.0F, 9.80665F};
  const AttitudeEstimate estimate =
      estimate_attitude(gravity, gravity, AttitudeCalibration{});
  CHECK(estimate.valid);
  check_near(estimate.lean_deg, 0.0F);
  check_near(estimate.pitch_deg, 0.0F);
  check_near(estimate.longitudinal_g, 0.0F);
  check_near(estimate.lateral_g, 0.0F);
}

void test_right_lean_is_positive() {
  const Vec3f gravity{-4.903325F, 0.0F, 8.492808F};
  const AttitudeEstimate estimate =
      estimate_attitude(gravity, gravity, AttitudeCalibration{});
  CHECK(estimate.valid);
  check_near(estimate.lean_deg, 30.0F);
  check_near(estimate.pitch_deg, 0.0F);
  check_near(estimate.longitudinal_g, 0.0F);
  // A coordinated 30 degree right corner is tan(30) = 0.577 g.
  check_near(estimate.lateral_g, 0.577350F);
}

void test_left_lean_is_negative() {
  const Vec3f gravity{4.903325F, 0.0F, 8.492808F};
  const AttitudeEstimate estimate =
      estimate_attitude(gravity, gravity, AttitudeCalibration{});
  CHECK(estimate.valid);
  check_near(estimate.lean_deg, -30.0F);
  check_near(estimate.lateral_g, -0.577350F);
}

void test_braking_and_acceleration() {
  const Vec3f gravity{0.0F, 0.0F, 9.80665F};
  const AttitudeEstimate braking =
      estimate_attitude({0.0F, -3.0F, 9.80665F}, gravity, AttitudeCalibration{});
  CHECK(braking.valid);
  check_near(braking.longitudinal_g, -0.305915F);

  const AttitudeEstimate accelerating =
      estimate_attitude({0.0F, 2.0F, 9.80665F}, gravity, AttitudeCalibration{});
  CHECK(accelerating.valid);
  check_near(accelerating.longitudinal_g, 0.203943F);
}

void test_nose_up_pitch_is_positive() {
  const Vec3f gravity{0.0F, 1.702907F, 9.657665F};
  const AttitudeEstimate estimate =
      estimate_attitude(gravity, gravity, AttitudeCalibration{});
  CHECK(estimate.valid);
  check_near(estimate.lean_deg, 0.0F);
  check_near(estimate.pitch_deg, 10.0F);
  check_near(estimate.longitudinal_g, 0.0F);
}

void test_calibration_offsets_are_applied() {
  const Vec3f right_lean{-4.903325F, 0.0F, 8.492808F};
  AttitudeCalibration calibration;
  calibration.lean_zero_deg = 10.0F;
  const AttitudeEstimate leaned =
      estimate_attitude(right_lean, right_lean, calibration);
  CHECK(leaned.valid);
  check_near(leaned.lean_deg, 20.0F);
  check_near(leaned.lateral_g, 0.363970F);

  const Vec3f nose_up{0.0F, 1.702907F, 9.657665F};
  calibration = AttitudeCalibration{};
  calibration.pitch_zero_deg = 5.0F;
  const AttitudeEstimate pitched =
      estimate_attitude(nose_up, nose_up, calibration);
  CHECK(pitched.valid);
  check_near(pitched.pitch_deg, 5.0F);
}

void test_invalid_inputs_are_rejected() {
  const AttitudeEstimate weak_gravity = estimate_attitude(
      {0.0F, 0.0F, 2.0F}, {0.0F, 0.0F, 2.0F}, AttitudeCalibration{});
  CHECK(!weak_gravity.valid);

  const float nan = std::nanf("");
  const AttitudeEstimate non_finite = estimate_attitude(
      {0.0F, nan, 9.80665F}, {0.0F, 0.0F, 9.80665F},
      AttitudeCalibration{});
  CHECK(!non_finite.valid);
}

void test_lateral_g_is_clamped() {
  const Vec3f gravity{-9.769333F, 0.0F, 0.854706F};
  const AttitudeEstimate estimate =
      estimate_attitude(gravity, gravity, AttitudeCalibration{});
  CHECK(estimate.valid);
  check_near(estimate.lean_deg, 85.0F);
  check_near(estimate.lateral_g, 4.0F);
}

}  // namespace

int main() {
  test_upright_at_rest();
  test_right_lean_is_positive();
  test_left_lean_is_negative();
  test_braking_and_acceleration();
  test_nose_up_pitch_is_positive();
  test_calibration_offsets_are_applied();
  test_invalid_inputs_are_rejected();
  test_lateral_g_is_clamped();

  if (failures != 0) {
    std::cerr << failures << " attitude_estimator checks failed\n";
    return EXIT_FAILURE;
  }
  std::cout << "All attitude_estimator checks passed\n";
  return EXIT_SUCCESS;
}
