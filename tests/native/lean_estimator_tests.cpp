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
  // Nose-up: the body x axis tilts up, so static specific force along body x
  // is +g*sin(theta).
  const Vec3f accel{kGravity * std::sin(theta), 0.0F,
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

// The estimator must not assume a 112 Hz loop: the acquisition chain will move
// to sample timestamps, and a hard-coded dt would silently rescale time.
void test_estimator_is_dt_aware() {
  const float lean = 40.0F;
  const float speed_mps = 20.0F;
  const float psi_dot = -kGravity * std::tan(lean * kDegToRad) / speed_mps;
  const auto run_at = [&](float dt) {
    LeanEstimator estimator;
    const int steps = static_cast<int>(20.0F / dt);
    for (int i = 0; i < steps; ++i) {
      estimator.step({0.0F, turn_rates(lean, psi_dot).y,
                      turn_rates(lean, psi_dot).z},
                     coordinated_accel(lean, lean), dt,
                     SpeedSample{true, speed_mps, 0.0F});
    }
    return estimator.output().lean_deg;
  };
  const float at_112 = run_at(1.0F / 112.0F);
  const float at_125 = run_at(1.0F / 125.0F);
  check_near(at_112, lean, 2.0F, "112 Hz lean");
  check_near(at_125, lean, 2.0F, "125 Hz lean");
  check_near(at_112, at_125, 1.0F, "dt-independent lean");
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

// A consumer IMU has tens of mg of zero-g offset. The capture must learn the
// unit's own stationary magnitude instead of demanding |a| == 1 g.
void test_static_capture_tolerates_zero_g_offset() {
  MountCalibration mount;
  mount.begin_static();
  const Vec3f up{0.0F, 1.0F, 0.0F};
  const float offset_magnitude = 1.03F * kGravity;
  bool ready = false;
  for (int i = 0; i < 300; ++i) {
    ready = mount.observe_static(
        {up.x * offset_magnitude, up.y * offset_magnitude,
         up.z * offset_magnitude},
        {0.0F, 0.0F, 0.0F}, 0.01F);
  }
  CHECK(ready);
  CHECK(mount.stage1);
  check_near(mount.to_body({0.0F, offset_magnitude, 0.0F}).z,
             offset_magnitude, 0.1F, "learned static magnitude");
  check_near(mount.static_magnitude_mps2, offset_magnitude, 0.05F,
             "captured stationary magnitude");
}

// The static gate must compare against the unit's own captured magnitude, not
// the nominal 1 g: 1.04 g would otherwise keep the estimator in GYRO forever.
void test_static_gate_uses_the_learned_magnitude() {
  MountCalibration learned;
  learned.set_basis({1.0F, 0.0F, 0.0F}, {0.0F, 0.0F, 1.0F});
  learned.static_magnitude_mps2 = 1.04F * kGravity;
  LeanEstimator estimator;
  estimator.set_mount(learned);
  run(estimator, {0.0F, 0.0F, 1.04F * kGravity}, {0.0F, 0.0F, 0.0F},
      SpeedSample{}, 4.0F);
  CHECK(estimator.output().source == LeanSource::Static);
  check_near(estimator.output().lean_deg, 0.0F, 0.3F, "offset static lean");

  MountCalibration nominal;
  nominal.set_basis({1.0F, 0.0F, 0.0F}, {0.0F, 0.0F, 1.0F});
  LeanEstimator other;
  other.set_mount(nominal);
  run(other, {0.0F, 0.0F, 1.04F * kGravity}, {0.0F, 0.0F, 0.0F}, SpeedSample{},
      4.0F);
  CHECK(other.output().source != LeanSource::Static);
}

// The datasheet's initial gyro offset can be ~10 dps, so the stillness gate is
// the window standard deviation, not the raw mean. A constant offset passes; a
// noisy window does not.
void test_static_capture_uses_gyro_stddev() {
  const Vec3f up{0.0F, 1.0F, 0.0F};
  MountCalibration biased;
  biased.begin_static();
  bool ready = false;
  for (int i = 0; i < 300; ++i) {
    ready = biased.observe_static({0.0F, kGravity, 0.0F},
                                  {8.0F, 0.0F, 0.0F}, 0.01F);
  }
  CHECK(ready);
  CHECK(biased.stage1);

  MountCalibration noisy;
  noisy.begin_static();
  bool accepted = false;
  for (int i = 0; i < 300; ++i) {
    const float rate = (i % 2 == 0) ? 2.0F : -2.0F;
    accepted = noisy.observe_static({0.0F, kGravity, 0.0F},
                                    {rate, 0.0F, 0.0F}, 0.01F);
  }
  CHECK(!accepted);
  CHECK(!noisy.stage1);
  CHECK(noisy.reject_reason == 1);
}

// Step 2 only trims residual yaw: ±10°, and two agreeing segments.
void test_forward_trim_is_yaw_only_and_guarded() {
  const Vec3f up{0.0F, 1.0F, 0.0F};
  const auto rotate_y = [](float degrees) {
    const float r = degrees * kDegToRad;
    return Vec3f{std::cos(r), 0.0F, -std::sin(r)};
  };
  const auto feed_segment = [&](MountCalibration& mount, const Vec3f& forward) {
    mount.begin_forward();
    for (int i = 0; i < 100; ++i) {
      mount.observe_forward({forward.x * 2.0F, up.y * kGravity,
                             forward.z * 2.0F},
                            true, 0.01F);
    }
  };

  // 8 degrees of residual yaw: accepted on the second agreeing segment.
  MountCalibration mount;
  mount.set_basis({1.0F, 0.0F, 0.0F}, up);
  mount.stage2 = false;  // step 1 only, as after a static capture
  const Vec3f trimmed = rotate_y(8.0F);
  feed_segment(mount, trimmed);
  feed_segment(mount, trimmed);
  CHECK(mount.stage2);
  const Vec3f mapped = mount.to_body(trimmed);
  check_near(mapped.x, 1.0F, 0.02F, "yaw trim forward x");
  check_near(mapped.y, 0.0F, 0.02F, "yaw trim forward y");
  check_near(mapped.z, 0.0F, 0.02F, "yaw trim forward z");

  // 20 degrees: outside the guard, so step 2 refuses to bend the basis.
  MountCalibration far;
  far.set_basis({1.0F, 0.0F, 0.0F}, up);
  far.stage2 = false;
  const Vec3f wrong = rotate_y(20.0F);
  feed_segment(far, wrong);
  feed_segment(far, wrong);
  CHECK(!far.stage2);
}

// The zero trim is a ±5° rotation about the forward axis, applied after the
// mount basis so it moves Lean and nothing else.
void test_trim_shifts_lean() {
  MountCalibration mount;
  mount.set_basis({1.0F, 0.0F, 0.0F}, {0.0F, 0.0F, 1.0F});
  mount.set_trim_deg(2.0F);
  LeanEstimator estimator;
  estimator.set_mount(mount);
  run(estimator, {0.0F, 0.0F, kGravity}, {0.0F, 0.0F, 0.0F}, SpeedSample{},
      4.0F);
  check_near(estimator.output().lean_deg, 2.0F, 0.2F, "positive trim");

  mount.set_trim_deg(-20.0F);
  check_near(mount.trim_deg, -5.0F, 0.001F, "trim clamps to ±5");
  estimator.set_mount(mount);
  run(estimator, {0.0F, 0.0F, kGravity}, {0.0F, 0.0F, 0.0F}, SpeedSample{},
      4.0F);
  check_near(estimator.output().lean_deg, -5.0F, 0.2F, "negative trim");
}

// A flick with the IMU one metre above the roll axis. The measured lateral
// specific force carries h*lean_accel and the vertical one h*lean_rate^2;
// without the lever-arm term the estimate is pulled several degrees off.
void test_lever_arm_is_compensated() {
  const float speed_mps = 20.0F;
  const auto smooth = [](float x) {
    const float t = std::min(1.0F, std::max(0.0F, x));
    return t * t * (3.0F - 2.0F * t);
  };
  const auto scenario = [&](float lever_arm_m, bool compensate) {
    LeanEstimator estimator;
    if (compensate) estimator.set_lever_arm_m(lever_arm_m);
    float worst = 0.0F;
    const float dt = 0.002F;
    for (int i = 0; i < 8000; ++i) {
      const float t = i * dt;
      float lean = 20.0F;
      float lean_rate = 0.0F;
      if (t > 5.0F) {
        const float ramp = smooth((t - 5.0F) / 0.6F);
        lean = 20.0F + 12.0F * ramp;
        // d/dt of smoothstep
        const float u = (t - 5.0F) / 0.6F;
        if (u > 0.0F && u < 1.0F) {
          lean_rate = 12.0F * (6.0F * u * (1.0F - u)) / 0.6F;
        }
      }
      const float lean_accel =
          (t > 5.0F && t < 5.6F)
              ? 12.0F * (6.0F - 12.0F * ((t - 5.0F) / 0.6F)) /
                    (0.6F * 0.6F)
              : 0.0F;
      const Vec3f force = static_accel(lean);
      const float rate_rad = lean_rate * kDegToRad;
      Vec3f measured = force;
      if (lever_arm_m > 0.0F) {
        measured.y -= lever_arm_m * lean_accel * kDegToRad;
        measured.z -= lever_arm_m * rate_rad * rate_rad;
      }
      estimator.step({lean_rate, 0.0F, 0.0F}, measured, dt,
                     SpeedSample{true, speed_mps, 0.0F});
      // Ignore the initial convergence from 0: this test is about the flick.
      if (t > 5.0F) {
        worst = std::max(worst, std::fabs(estimator.output().lean_deg - lean));
      }

    }
    return worst;
  };
  const float compensated = scenario(1.0F, true);
  const float uncompensated = scenario(1.0F, false);
  CHECK(compensated < 1.5F);
  // Without the term the flick is measurably wrong; the exact size depends on
  // how long the corrupted observation is applied, so assert the ratio too.
  CHECK(uncompensated > 2.0F);
  CHECK(uncompensated > 2.0F * compensated);
  std::cout << "lever arm: compensated worst " << compensated
            << "°, uncompensated worst " << uncompensated << "°\n";
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
  test_estimator_is_dt_aware();
  test_no_speed_straight_holds_zero();
  test_bias_is_frozen_while_moving();
  test_bias_learns_only_when_static();
  test_saturation_flag();
  test_stale_speed_disables_kinematic();
  test_static_capture_tolerates_zero_g_offset();
  test_static_gate_uses_the_learned_magnitude();
  test_static_capture_uses_gyro_stddev();
  test_forward_trim_is_yaw_only_and_guarded();
  test_trim_shifts_lean();
  test_lever_arm_is_compensated();
  test_rmount_removes_bracket_lean();
  test_quality_is_halved_without_calibration();

  if (failures != 0) {
    std::cerr << failures << " lean_estimator checks failed\n";
    return EXIT_FAILURE;
  }
  std::cout << "All lean_estimator checks passed\n";
  return EXIT_SUCCESS;
}
