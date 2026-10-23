// v2 lean estimator tests (DESIGN_V2 §3.3-§3.5, §14.1).
//
// These are synthetic but physically consistent: every input is built from the
// attitude/kinematics it is supposed to measure. The coordinated-turn case is
// the regression that the old accelerometer-angle estimator cannot pass.
#include "lean_estimator.hpp"

#include <cmath>
#include <cstdlib>
#include <iostream>

namespace {

using moto::motion::LeanEstimator;
using moto::motion::LeanFlag;
using moto::motion::LeanSource;
using moto::motion::MountCalibration;
using moto::motion::SpeedSample;
using moto::motion::Vec3f;

constexpr float kGravity = 9.80665F;
constexpr float kDt = 1.0F / 112.0F;
constexpr float kDegToRad = 0.017453292519943295F;
constexpr float kRadToDeg = 57.29577951308232F;

int failures = 0;

#define CHECK(condition)                                                   \
  do {                                                                     \
    if (!(condition)) {                                                    \
      std::cerr << __FILE__ << ':' << __LINE__                             \
                << " CHECK failed: " #condition << '\n';                   \
      ++failures;                                                          \
    }                                                                      \
  } while (false)

void check_near(float actual, float expected, float tolerance,
                const char* what) {
  if (std::fabs(actual - expected) > tolerance) {
    std::cerr << what << ": expected " << expected << ", got " << actual
              << " (tolerance " << tolerance << ")\n";
    ++failures;
  }
}

// Specific force of a stationary bike leaned by `lean` (right positive), in
// the ISO body frame: x forward, y left, z up.
Vec3f static_accel(float lean_deg) {
  const float phi = lean_deg * kDegToRad;
  return {0.0F, kGravity * std::sin(phi), kGravity * std::cos(phi)};
}

// Body rates for a steady turn: roll rate zero, yaw rate `psi_dot` (left
// positive) about world up. With bank phi the body components are
// (0, psi*sin(phi), psi*cos(phi)). Returned in deg/s, the estimator's input.
Vec3f turn_rates(float lean_deg, float psi_dot) {
  const float phi = lean_deg * kDegToRad;
  return {0.0F, psi_dot * std::sin(phi) * kRadToDeg,
          psi_dot * std::cos(phi) * kRadToDeg};
}

// Specific force during a steady coordinated turn: centripetal acceleration
// and gravity, resolved in the body frame.
Vec3f coordinated_accel(float lean_deg, float lean_eff_deg) {
  const float phi = lean_deg * kDegToRad;
  const float phiv = lean_eff_deg * kDegToRad;
  const float tan_v = std::tan(phiv);
  return {0.0F,
          kGravity * (-tan_v * std::cos(phi) + std::sin(phi)),
          kGravity * (tan_v * std::sin(phi) + std::cos(phi))};
}

void run(LeanEstimator& estimator, const Vec3f& accel, const Vec3f& rates,
         const SpeedSample& speed, float seconds) {
  const int steps = static_cast<int>(seconds / kDt);
  for (int i = 0; i < steps; ++i) {
    estimator.step(rates, accel, kDt, speed);
  }
}

void test_static_tilt() {
  const float tilts[] = {0.0F, 15.0F, -15.0F, 30.0F, -30.0F, 45.0F, -45.0F};
  for (const float tilt : tilts) {
    LeanEstimator estimator;
    run(estimator, static_accel(tilt), {0.0F, 0.0F, 0.0F}, SpeedSample{}, 5.0F);
    check_near(estimator.output().lean_deg, tilt, 0.3F, "static tilt");
    check_near(estimator.output().pitch_deg, 0.0F, 0.3F, "static pitch");
    CHECK(estimator.output().source == LeanSource::Static);
  }
}

void test_pitch_is_nose_up_positive() {
  LeanEstimator estimator;
  const float theta = 10.0F * kDegToRad;
  const Vec3f accel{-kGravity * std::sin(theta), 0.0F,
                    kGravity * std::cos(theta)};
  run(estimator, accel, {0.0F, 0.0F, 0.0F}, SpeedSample{}, 4.0F);
  check_near(estimator.output().lean_deg, 0.0F, 0.3F, "pitch keeps lean");
  check_near(estimator.output().pitch_deg, 10.0F, 0.3F, "nose-up pitch");
}

// The regression that matters: a steady 40 degree corner reads 40, not the
// fraction the accelerometer angle reports (which would be ~0 here, because
// the specific force is aligned with the bike's own vertical).
void test_coordinated_turn() {
  const float lean = 40.0F;
  const float speed_mps = 20.0F;
  const float psi_dot = -kGravity * std::tan(lean * kDegToRad) / speed_mps;
  LeanEstimator estimator;
  run(estimator, coordinated_accel(lean, lean),
      turn_rates(lean, psi_dot), SpeedSample{true, speed_mps, 0.0F}, 20.0F);
  check_near(estimator.output().lean_deg, lean, 2.0F, "coordinated lean");
  check_near(estimator.output().lean_eff_deg, lean, 2.0F, "effective lean");
  check_near(estimator.output().lateral_g, std::tan(lean * kDegToRad), 0.03F,
             "horizontal lateral G");
  CHECK(estimator.output().source == LeanSource::Kinematic);
  CHECK((estimator.output().flags & moto::motion::kLeanSrcKin) != 0);
}

// Hang-off: the bike leans 35 while the rider/system needs 42. The page shows
// the frame angle and the effective angle separately.
void test_hang_off() {
  const float lean = 35.0F;
  const float lean_eff = 42.0F;
  const float speed_mps = 20.0F;
  const float psi_dot =
      -kGravity * std::tan(lean_eff * kDegToRad) / speed_mps;
  LeanEstimator estimator;
  run(estimator, coordinated_accel(lean, lean_eff),
      turn_rates(lean, psi_dot), SpeedSample{true, speed_mps, 0.0F}, 25.0F);
  check_near(estimator.output().lean_deg, lean, 2.0F, "hang-off frame lean");
  check_near(estimator.output().lean_eff_deg, lean_eff, 2.0F,
             "hang-off effective lean");
}

// No speed available, riding straight: the zero pseudo-observation has to hold
// the estimate even though the gyro is the only fast path.
void test_no_speed_straight_holds_zero() {
  LeanEstimator estimator;
  run(estimator, static_accel(10.0F), {0.0F, 0.0F, 0.0F}, SpeedSample{}, 5.0F);
  check_near(estimator.output().lean_deg, 10.0F, 0.3F, "seeded lean");
  run(estimator, {0.0F, 0.0F, kGravity}, {0.0F, 0.0F, 0.0F}, SpeedSample{},
      30.0F);
  CHECK(std::fabs(estimator.output().lean_deg) <= 1.0F);
  // With no speed source a smooth straight ride is indistinguishable from a
  // standstill; either correction is acceptable as long as it holds the zero.
  CHECK(estimator.output().source != LeanSource::Gyro);
}

void test_bias_is_frozen_while_moving() {
  LeanEstimator estimator;
  const float psi_dot = 30.0F * kDegToRad;
  run(estimator, {0.0F, 0.0F, kGravity}, {0.0F, 0.0F, psi_dot},
      SpeedSample{true, 20.0F, 0.0F}, 60.0F);
  const Vec3f bias = estimator.bias_dps();
  check_near(bias.x, 0.0F, 0.001F, "bias x frozen");
  check_near(bias.y, 0.0F, 0.001F, "bias y frozen");
  check_near(bias.z, 0.0F, 0.001F, "bias z frozen");
  CHECK(!estimator.bias_learning());
}

void test_bias_learns_only_when_static() {
  LeanEstimator estimator;
  const Vec3f accel{0.0F, 0.0F, kGravity};
  const Vec3f rates{1.0F, 0.0F, 0.0F};  // deg/s
  run(estimator, accel, rates, SpeedSample{}, 40.0F);
  CHECK(estimator.bias_learning());
  check_near(estimator.bias_dps().x, 1.0F, 0.15F, "static bias x");
  check_near(estimator.bias_dps().y, 0.0F, 0.01F, "static bias y");
}

void test_saturation_flag() {
  LeanEstimator estimator;
  run(estimator, static_accel(80.0F), {0.0F, 0.0F, 0.0F}, SpeedSample{}, 5.0F);
  CHECK(estimator.output().saturated);
  CHECK((estimator.output().flags & moto::motion::kLeanSaturated) != 0);
}

void test_stale_speed_disables_kinematic() {
  LeanEstimator estimator;
  run(estimator, static_accel(0.0F), {0.0F, 0.0F, 0.0F},
      SpeedSample{true, 20.0F, 3.0F}, 3.0F);
  CHECK(estimator.output().source != LeanSource::Kinematic);
  CHECK((estimator.output().flags & moto::motion::kLeanSpdStale) != 0);
}

// Static calibration against a bracket that rotates and tilts the sensor.
void test_rmount_static_stage() {
  MountCalibration mount;
  // Board mounted with sensor +y up and sensor -z forward.
  const Vec3f up{0.0F, 1.0F, 0.0F};
  const Vec3f forward{0.0F, 0.0F, -1.0F};
  mount.begin_static();
  for (int i = 0; i < 300; ++i) {
    mount.observe_static({up.x * kGravity, up.y * kGravity, up.z * kGravity},
                         {0.0F, 0.0F, 0.0F}, 0.01F);
  }
  CHECK(mount.stage1);
  check_near(mount.to_body({up.x * kGravity, up.y * kGravity, up.z * kGravity})
                 .z,
             kGravity, 0.01F, "R_mount up");

  mount.begin_forward();
  for (int i = 0; i < 100; ++i) {
    // 2 m/s^2 along sensor forward plus gravity up.
    mount.observe_forward({forward.x * 2.0F + up.x * kGravity,
                           forward.y * 2.0F + up.y * kGravity,
                           forward.z * 2.0F + up.z * kGravity},
                          true, 0.01F);
  }
  CHECK(mount.stage2);
  const Vec3f mapped_forward = mount.to_body(forward);
  check_near(mapped_forward.x, 1.0F, 0.02F, "R_mount forward x");
  check_near(mapped_forward.y, 0.0F, 0.02F, "R_mount forward y");
  check_near(mapped_forward.z, 0.0F, 0.02F, "R_mount forward z");
}

// The estimator must read zero when the bike is level regardless of how the
// sensor is bolted on, and a real lean must survive the rotation.
void test_rmount_removes_bracket_lean() {
  MountCalibration mount;
  // Arbitrary orthonormal basis: the bracket is rotated 20 degrees and tilted.
  const float yaw = 20.0F * kDegToRad;
  const float tilt = 8.0F * kDegToRad;
  const Vec3f forward{std::cos(tilt) * std::cos(yaw),
                      std::cos(tilt) * std::sin(yaw), std::sin(tilt)};
  const Vec3f up{-std::sin(tilt) * std::cos(yaw),
                 -std::sin(tilt) * std::sin(yaw), std::cos(tilt)};
  mount.set_basis(forward, up);
  const Vec3f left = [&] {
    const Vec3f l{up.y * forward.z - up.z * forward.y,
                  up.z * forward.x - up.x * forward.z,
                  up.x * forward.y - up.y * forward.x};
    return l;
  }();

  LeanEstimator estimator;
  estimator.set_mount(mount);
  run(estimator, {up.x * kGravity, up.y * kGravity, up.z * kGravity},
      {0.0F, 0.0F, 0.0F}, SpeedSample{}, 4.0F);
  check_near(estimator.output().lean_deg, 0.0F, 0.3F, "bracket zero");
  CHECK((estimator.output().flags & moto::motion::kLeanCalOk) != 0);

  // A real 30 degree right lean: body up is (0, sin30, cos30), resolved into
  // sensor coordinates through the basis.
  const float phi = 30.0F * kDegToRad;
  const Vec3f lean_accel{
      left.x * kGravity * std::sin(phi) + up.x * kGravity * std::cos(phi),
      left.y * kGravity * std::sin(phi) + up.y * kGravity * std::cos(phi),
      left.z * kGravity * std::sin(phi) + up.z * kGravity * std::cos(phi)};
  run(estimator, lean_accel, {0.0F, 0.0F, 0.0F}, SpeedSample{}, 5.0F);
  check_near(estimator.output().lean_deg, 30.0F, 0.5F, "bracket lean");
}

void test_quality_is_halved_without_calibration() {
  LeanEstimator estimator;
  run(estimator, static_accel(0.0F), {0.0F, 0.0F, 0.0F}, SpeedSample{}, 3.0F);
  CHECK((estimator.output().flags & moto::motion::kLeanCalOk) == 0);
  CHECK(estimator.output().quality <= 50);
}

}  // namespace

int main() {
  test_static_tilt();
  test_pitch_is_nose_up_positive();
  test_coordinated_turn();
  test_hang_off();
  test_no_speed_straight_holds_zero();
  test_bias_is_frozen_while_moving();
  test_bias_learns_only_when_static();
  test_saturation_flag();
  test_stale_speed_disables_kinematic();
  test_rmount_static_stage();
  test_rmount_removes_bracket_lean();
  test_quality_is_halved_without_calibration();

  if (failures != 0) {
    std::cerr << failures << " lean_estimator checks failed\n";
    return EXIT_FAILURE;
  }
  std::cout << "All lean_estimator checks passed\n";
  return EXIT_SUCCESS;
}
