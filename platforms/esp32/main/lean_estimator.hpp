#pragma once

#include <cstdint>

#include "attitude_estimator.hpp"

// v2 lean estimation (DESIGN_V2 §3.3-§3.5).
//
// Frame: ISO 8855, x forward, y left, z up. This is NOT the frame used by
// attitude_estimator.hpp, which predates this and calls x "right". Adapters
// must map with from_legacy_bike() once, at the boundary.
//
// Sign: right lean is positive. A right lean tilts the bike's up axis towards
// its right, i.e. it is a positive rotation about +x, so the world-up vector
// expressed in the body frame is (0, sin(lean), cos(lean)).
namespace moto::motion {

// Sensor-to-ISO-body mount basis. The old scalar lean_zero_offset_cdeg could
// only trim a zero, not a rotated bracket; this replaces it.
struct MountCalibration {
  bool stage1 = false;
  bool stage2 = false;
  // Residual zero trim about the forward axis, ±5°. Applied after the basis,
  // so it moves Lean and nothing else.
  float trim_deg = 0.0F;
  // Learned while stationary: this unit's own |specific force| when parked.
  // Consumer IMUs have tens of mg of zero-g offset, so the static gate and
  // ZUPT compare against this, not against 9.80665 (v2 review item 5).
  float static_magnitude_mps2 = 9.80665F;
  // Why the last static capture was refused: 0 none, 1 motion/gyro, 2 range.
  std::uint8_t reject_reason = 0;

  // Step 1 (DESIGN_V2 §3.3): the mean specific force of a stationary, upright
  // bike is the sensor-frame up axis. Samples are rejected while the bike is
  // moving; two seconds of accepted samples are required.
  void begin_static();
  bool observe_static(const Vec3f& accel_mps2, const Vec3f& gyro_dps,
                      float dt);

  // Step 2: a straight-line acceleration or braking event gives the forward
  // axis once gravity is projected out. Step 2 is only allowed to trim residual
  // yaw: the result is clamped to ±10°, and two consecutive segments must
  // agree before it is written. A bad braking-in-a-corner segment is rejected
  // instead of silently poisoning the basis. `accelerating` selects the sign.
  void begin_forward();
  bool observe_forward(const Vec3f& accel_mps2, bool accelerating, float dt);

  void set_trim_deg(float degrees);

  // Rebuild the basis from an already known pair (tests, NVS restore).
  void set_basis(const Vec3f& sensor_forward, const Vec3f& sensor_up,
                 bool calibrated = true);

  Vec3f to_body(const Vec3f& sensor) const;
  Vec3f to_sensor(const Vec3f& body) const;
  // Provisional up for an uncalibrated screen far from the assumed mount.
  // Keeps the basis orthonormal and leaves stage1 untouched.
  void adopt_up(const Vec3f& sensor_up);
  Vec3f forward_axis() const { return forward_; }
  Vec3f up_axis() const { return up_; }
  bool valid() const { return stage1; }

 private:
  Vec3f forward_{1.0F, 0.0F, 0.0F};  // sensor frame, orthogonal to up_
  Vec3f up_{0.0F, 0.0F, 1.0F};       // sensor frame, world up at rest
  Vec3f acc_sum_{};
  Vec3f gyro_sum_{};
  Vec3f gyro_sumsq_{};
  int gyro_count_ = 0;
  Vec3f fwd_sum_{};
  Vec3f pending_forward_{};
  bool has_pending_forward_ = false;
  int acc_count_ = 0;
  float fwd_weight_ = 0.0F;
  float acc_time_s_ = 0.0F;
  // Learned stationary specific-force magnitude: consumer IMUs have tens of mg
  // of zero-g offset, so "|a| == 1 g" is not a usable gate (v2 review §5).
  float static_magnitude_ = 9.80665F;
  float last_magnitude_ = 9.80665F;
};

// attitude_estimator.hpp output (x right, y forward, z up) to ISO body.
inline Vec3f from_legacy_bike(const Vec3f& legacy) {
  return {legacy.y, -legacy.x, legacy.z};
}

struct SpeedSample {
  bool valid = false;
  float mps = 0.0F;
  // Age of the fix that produced mps. The estimator stops trusting a speed
  // older than 1.5 s (§3.5.3).
  float age_s = 0.0F;
};

enum class LeanSource : std::uint8_t {
  Gyro = 0,
  Kinematic = 1,
  Static = 2,
  Zero = 3,
};

enum LeanFlag : std::uint16_t {
  kLeanSrcKin = 1U << 0,
  kLeanSrcStatic = 1U << 1,
  kLeanSrcZero = 1U << 2,
  kLeanSpdStale = 1U << 3,
  kLeanCalOk = 1U << 4,
  kLeanInCorner = 1U << 5,
  kLeanCrash = 1U << 6,
  kLeanSaturated = 1U << 7,
  kLeanHdgHold = 1U << 8,
  kLeanGnssOnboard = 1U << 9,
  // Set by the adapter, not the estimator: the FIFO chain fell back to polling.
  kLeanImuFallback = 1U << 10,
};

struct LeanOutput {
  float lean_deg = 0.0F;       // frame, right positive
  float lean_eff_deg = 0.0F;   // kinematic target; NAN when unavailable
  float lean_rate_dps = 0.0F;
  float pitch_deg = 0.0F;
  float yaw_rate_dps = 0.0F;   // left positive (ISO)
  float lateral_g = 0.0F;      // horizontal frame, right positive
  LeanSource source = LeanSource::Gyro;
  std::uint8_t quality = 0;
  std::uint16_t flags = 0;
  bool valid = false;
  bool saturated = false;
};

// Stateful complement to the stateless attitude_estimator: gyro integration is
// the high-frequency path, kinematic/static/zero observations are the slow
// corrections. The gravity-only angle is deliberately not used as the
// estimate: in a steady corner the accelerometer is aligned with the bike, so
// that construction reports a fraction of the real lean (DESIGN_V2 §3.5.1).
class LeanEstimator {
 public:
  void reset();

  void set_mount(const MountCalibration& mount);
  bool calibrated() const { return mount_.stage1; }
  // The basis in use, including a provisional up adopted before calibration.
  const MountCalibration& mount() const { return mount_; }

  // v2 review §1: the IMU sits h metres above the roll axis, so a_y picks up
  // h*lean_accel and a_z picks up h*lean_rate^2. Vehicle-profile parameter;
  // zero until the real mounting height is measured.
  void set_lever_arm_m(float meters) { lever_arm_m_ = meters; }

  // gyro in deg/s, accel in m/s^2, both in the sensor frame. dt in seconds.
  void step(const Vec3f& gyro_dps, const Vec3f& accel_mps2, float dt,
            const SpeedSample& speed);

  const LeanOutput& output() const { return out_; }

  // Gyro bias in deg/s, per axis. Static-only learning (§3.4).
  Vec3f bias_dps() const;

  // The same bias in the sensor's own axes, which is what survives a power
  // cycle or a recalibration: the part's offset does not depend on how the
  // screen is mounted. A restored bias is the starting point; static
  // learning keeps refining it.
  Vec3f bias_sensor_dps() const;
  void set_bias_sensor_dps(const Vec3f& bias_dps);
  // True once the bias came from storage or from a stationary window.
  bool bias_known() const { return bias_known_; }

  // True while the stationary window used to learn the bias is open.
  bool bias_learning() const { return static_for_s_ >= kStaticHoldS; }

 private:
  static constexpr float kDegToRad = 0.017453292519943295F;
  static constexpr float kRadToDeg = 57.29577951308232F;
  // A stationary 2 s window opens bias learning; driving never does. Without
  // a speed source the window is longer because a smooth straight can pass the
  // gyro/magnitude gate (v2 review §3).
  static constexpr float kStaticHoldS = 2.0F;
  static constexpr float kStaticHoldNoSpeedS = 3.0F;
  // Correction time constants from DESIGN_V2 appendix A.
  static constexpr float kKinematicTauS = 1.5F;
  static constexpr float kStaticTauS = 0.5F;
  static constexpr float kZeroTauS = 3.0F;
  static constexpr float kKinematicMinSpeedMps = 4.0F;
  static constexpr float kStraightHoldS = 1.5F;
  static constexpr float kMaxLeanDeg = 70.0F;
  // sin(45°): pitched further than this, the assumed mount is not how the
  // screen sits.
  static constexpr float kProvisionalPitchSin = 0.70710678F;

  bool is_static(const Vec3f& accel, const Vec3f& gyro,
                 const SpeedSample& speed) const;
  void update_straight(const Vec3f& accel, float yaw_rate_dps,
                       float lean_rate_dps, float dt);
  // Gate the kinematic correction on fast transitions and bumps so a pothole
  // cannot yank the horizon. The bump test compares against a running mean.
  float adapt_gain(float base, float lean_rate_dps,
                   float lean_accel_dps2, const Vec3f& accel) const;

  MountCalibration mount_{};
  // World-up expressed in the body frame; the whole attitude state.
  Vec3f up_body_{0.0F, 0.0F, 1.0F};
  Vec3f bias_rads_{};
  float lean_deg_ = 0.0F;
  float lean_prev_deg_ = 0.0F;
  float lean_rate_dps_ = 0.0F;
  float yaw_rate_dps_ = 0.0F;
  float lean_eff_deg_ = 0.0F;
  float static_for_s_ = 0.0F;
  float straight_for_s_ = 0.0F;
  float az_ref_ = 9.80665F;
  float lever_arm_m_ = 0.0F;
  float lean_rate_prev_dps_ = 0.0F;
  float roll_accel_ema_ = 0.0F;
  // Exponential mean of |d|a||. Road excitation keeps this well above the
  // standstill floor, which is what separates "parked" from "riding straight"
  // when the phone's GPS has expired (v2 review §3).
  float accel_delta_ema_ = 0.0F;
  float last_accel_magnitude_ = 9.80665F;
  // Stillness from the gyro's spread, not its level. A QMI8658 can sit at
  // several deg/s with the board perfectly still, so "|raw rate| < 3 dps"
  // never opened the bias window and the horizon drifted without bound.
  Vec3f gyro_mean_dps_{};
  float gyro_jitter_ema_ = 0.0F;
  // |a| reference for the stillness gate before step 1 has measured this
  // unit's own magnitude; a 30-50 mg zero-g offset fails a fixed 1 g gate.
  float accel_magnitude_ema_ = 9.80665F;
  bool gyro_seeded_ = false;
  bool bias_known_ = false;
  bool in_corner_ = false;
  LeanOutput out_{};
};

}  // namespace moto::motion
