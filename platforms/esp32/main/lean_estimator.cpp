#include "lean_estimator.hpp"

#include <algorithm>
#include <cmath>

namespace moto::motion {
namespace {

constexpr float kGravity = 9.80665F;
constexpr float kDegToRad = 0.017453292519943295F;
constexpr float kRadToDeg = 57.29577951308232F;
constexpr float kTwoPi = 6.283185307179586F;

bool finite(const Vec3f& v) {
  return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

float dot(const Vec3f& a, const Vec3f& b) {
  return a.x * b.x + a.y * b.y + a.z * b.z;
}

Vec3f cross(const Vec3f& a, const Vec3f& b) {
  return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z,
          a.x * b.y - a.y * b.x};
}

float length(const Vec3f& v) { return std::sqrt(dot(v, v)); }

Vec3f scale(const Vec3f& v, float s) {
  return {v.x * s, v.y * s, v.z * s};
}

Vec3f sub(const Vec3f& a, const Vec3f& b) {
  return {a.x - b.x, a.y - b.y, a.z - b.z};
}

Vec3f add(const Vec3f& a, const Vec3f& b) {
  return {a.x + b.x, a.y + b.y, a.z + b.z};
}

Vec3f normalise(const Vec3f& v) {
  const float magnitude = length(v);
  if (!std::isfinite(magnitude) || magnitude < 1.0e-4F) return {};
  return scale(v, 1.0F / magnitude);
}

// Rotate the world-up vector in the body frame about the body's forward axis.
// Right lean is a positive rotation about +x, so this is the same axis the
// kinematic observation corrects.
Vec3f rotate_about_x(const Vec3f& v, float angle_rad) {
  const float c = std::cos(angle_rad);
  const float s = std::sin(angle_rad);
  return {v.x, v.y * c + v.z * s, -v.y * s + v.z * c};
}

Vec3f rotate_about_axis(const Vec3f& v, const Vec3f& axis, float angle_rad) {
  const float c = std::cos(angle_rad);
  const float s = std::sin(angle_rad);
  const Vec3f k = normalise(axis);
  const Vec3f kxv = cross(k, v);
  const float kdv = dot(k, v);
  return {v.x * c + kxv.x * s + k.x * kdv * (1.0F - c),
          v.y * c + kxv.y * s + k.y * kdv * (1.0F - c),
          v.z * c + kxv.z * s + k.z * kdv * (1.0F - c)};
}

}  // namespace

void MountCalibration::begin_static() {
  acc_sum_ = {};
  acc_count_ = 0;
  acc_time_s_ = 0.0F;
}

bool MountCalibration::observe_static(const Vec3f& accel_mps2,
                                      const Vec3f& gyro_dps, float dt) {
  if (stage1) return true;
  if (!finite(accel_mps2) || !finite(gyro_dps) || dt <= 0.0F) return false;
  const float magnitude = length(accel_mps2);
  if (!std::isfinite(magnitude)) return false;
  if (length(gyro_dps) > 2.0F) return false;
  if (acc_count_ == 0) {
    // The first sample defines this unit's stationary specific force. A
    // consumer IMU with 30-50 mg of zero-g offset would fail a fixed |a|=1 g
    // gate forever (v2 review item 5).
    static_magnitude_ = magnitude;
    last_magnitude_ = magnitude;
  } else {
    // A parked bike is quiet; a moving one is not. One disturbed sample
    // restarts the 2 s window rather than accumulating through motion.
    if (std::fabs(magnitude - last_magnitude_) > 0.01F * kGravity) {
      acc_sum_ = {};
      acc_count_ = 0;
      acc_time_s_ = 0.0F;
      last_magnitude_ = magnitude;
      return false;
    }
    last_magnitude_ = magnitude;
    if (std::fabs(magnitude - static_magnitude_) > 0.02F * kGravity) {
      return false;
    }
  }
  acc_sum_ = add(acc_sum_, accel_mps2);
  ++acc_count_;
  acc_time_s_ += dt;
  if (acc_time_s_ < 2.0F || acc_count_ < 8) return false;
  const Vec3f up = normalise(acc_sum_);
  if (length(up) < 0.5F) return false;
  up_ = up;
  stage1 = true;
  stage2 = false;
  return true;
}

void MountCalibration::begin_forward() {
  fwd_sum_ = {};
  fwd_weight_ = 0.0F;
}

bool MountCalibration::observe_forward(const Vec3f& accel_mps2,
                                       bool accelerating, float dt) {
  if (!stage1 || dt <= 0.0F || !finite(accel_mps2)) return false;
  // Remove gravity: what is left is the horizontal specific force, which
  // points along the bike's forward axis during a straight acceleration.
  const float along_up = dot(accel_mps2, up_);
  Vec3f horizontal = sub(accel_mps2, scale(up_, along_up));
  if (length(horizontal) < 0.2F * kGravity) return false;
  if (!accelerating) horizontal = scale(horizontal, -1.0F);
  const Vec3f unit = normalise(horizontal);
  if (length(unit) < 0.5F) return false;
  fwd_sum_ = add(fwd_sum_, scale(unit, dt));
  fwd_weight_ += dt;
  if (fwd_weight_ < 0.5F) return false;

  const Vec3f candidate =
      normalise(sub(normalise(fwd_sum_), scale(up_, dot(normalise(fwd_sum_), up_))));
  fwd_sum_ = {};
  fwd_weight_ = 0.0F;
  if (length(candidate) < 0.5F) return false;

  const Vec3f reference =
      normalise(sub(forward_, scale(up_, dot(forward_, up_))));
  const float yaw = std::atan2(dot(up_, cross(reference, candidate)),
                               dot(reference, candidate));
  // Step 2 may only trim residual yaw. Beyond 10° the bracket is wrong and
  // the rider should redo step 1, not have the basis bent silently.
  if (std::fabs(yaw) > 10.0F * kDegToRad) return false;

  if (!has_pending_forward_) {
    pending_forward_ = candidate;
    has_pending_forward_ = true;
    return false;
  }
  const float agreement =
      std::atan2(dot(up_, cross(pending_forward_, candidate)),
                 dot(pending_forward_, candidate));
  if (std::fabs(agreement) > 3.0F * kDegToRad) {
    // Two segments disagree: keep the newer one and wait for a third.
    pending_forward_ = candidate;
    return false;
  }
  forward_ = normalise(
      rotate_about_axis(forward_, up_, std::clamp(yaw, -10.0F * kDegToRad,
                                                  10.0F * kDegToRad)));
  forward_ = normalise(sub(forward_, scale(up_, dot(forward_, up_))));
  has_pending_forward_ = false;
  stage2 = true;
  return true;
}

void MountCalibration::set_trim_deg(float degrees) {
  trim_deg = std::clamp(degrees, -5.0F, 5.0F);
}

void MountCalibration::set_basis(const Vec3f& sensor_forward,
                                 const Vec3f& sensor_up,
                                 bool calibrated) {
  Vec3f up = normalise(sensor_up);
  Vec3f forward = normalise(sensor_forward);
  if (length(up) > 0.5F && length(forward) > 0.5F) {
    forward = normalise(sub(forward, scale(up, dot(forward, up))));
  }
  if (length(up) < 0.5F || length(forward) < 0.5F) {
    up = {0.0F, 0.0F, 1.0F};
    forward = {1.0F, 0.0F, 0.0F};
    stage1 = false;
    stage2 = false;
  } else {
    stage1 = calibrated;
    stage2 = calibrated;
  }
  up_ = up;
  forward_ = forward;
}

Vec3f MountCalibration::to_body(const Vec3f& sensor) const {
  const Vec3f left = normalise(cross(up_, forward_));
  const Vec3f body = {dot(sensor, forward_), dot(sensor, left),
                      dot(sensor, up_)};
  return rotate_about_x(body, trim_deg * kDegToRad);
}

void LeanEstimator::reset() {
  up_body_ = {0.0F, 0.0F, 1.0F};
  bias_rads_ = {};
  lean_deg_ = 0.0F;
  lean_prev_deg_ = 0.0F;
  lean_rate_dps_ = 0.0F;
  yaw_rate_dps_ = 0.0F;
  lean_eff_deg_ = 0.0F;
  static_for_s_ = 0.0F;
  straight_for_s_ = 0.0F;
  in_corner_ = false;
  az_ref_ = kGravity;
  lean_rate_prev_dps_ = 0.0F;
  roll_accel_ema_ = 0.0F;
  accel_delta_ema_ = 0.0F;
  last_accel_magnitude_ = kGravity;
  out_ = LeanOutput{};
}

void LeanEstimator::set_mount(const MountCalibration& mount) {
  mount_ = mount;
  // The bias lives in the body frame; a new basis invalidates it.
  bias_rads_ = {};
}

Vec3f LeanEstimator::bias_dps() const {
  return scale(bias_rads_, kRadToDeg);
}

bool LeanEstimator::is_static(const Vec3f& accel, const Vec3f& gyro,
                              const SpeedSample& speed) const {
  if (!finite(accel) || !finite(gyro)) return false;
  if (length(gyro) > 3.0F) return false;
  if (std::fabs(length(accel) - kGravity) > 0.03F * kGravity) return false;
  if (!speed.valid && accel_delta_ema_ > 0.004F * kGravity) return false;
  return !speed.valid || speed.mps < 1.0F;
}

void LeanEstimator::update_straight(const Vec3f& accel, float yaw_rate_dps,
                                    float lean_rate_dps, float dt) {
  const bool straight = std::fabs(yaw_rate_dps) < 1.5F &&
                        std::fabs(lean_rate_dps) < 2.0F &&
                        std::fabs(accel.y) < 0.05F * kGravity;
  if (straight) {
    straight_for_s_ += dt;
  } else {
    straight_for_s_ = 0.0F;
  }
}

void LeanEstimator::step(const Vec3f& gyro_dps, const Vec3f& accel_mps2,
                         float dt, const SpeedSample& speed) {
  if (!finite(gyro_dps) || !finite(accel_mps2)) return;
  if (!std::isfinite(dt) || dt <= 0.0F) return;
  dt = std::clamp(dt, 0.0005F, 0.05F);

  const Vec3f accel = mount_.to_body(accel_mps2);
  const Vec3f gyro_raw = mount_.to_body(gyro_dps);
  const Vec3f omega = sub(scale(gyro_raw, kDegToRad), bias_rads_);

  // High-frequency path: integrate the world-up vector expressed in the body
  // frame. u_dot = -omega x u (body rates rotate the frame, not the vector).
  up_body_ = normalise(
      sub(up_body_, scale(cross(omega, up_body_), dt)));
  if (length(up_body_) < 0.5F) up_body_ = {0.0F, 0.0F, 1.0F};

  lean_deg_ = std::atan2(up_body_.y, up_body_.z) * kRadToDeg;
  const float delta = std::atan2(
      std::sin((lean_deg_ - lean_prev_deg_) * kDegToRad),
      std::cos((lean_deg_ - lean_prev_deg_) * kDegToRad));
  lean_rate_dps_ = std::clamp(delta * kRadToDeg / dt, -1000.0F, 1000.0F);
  lean_prev_deg_ = lean_deg_;
  yaw_rate_dps_ = dot(omega, up_body_) * kRadToDeg;

  // Lever arm: the IMU rides h above the roll axis, so its lateral specific
  // force contains h*lean_accel and its vertical one h*lean_rate^2. Left in,
  // those terms look like real lean while the bike is flicking in.
  // Use the gyro's roll rate for the lever terms: it is the physical rate, and
  // differencing the derived Lean angle instead amplifies the correction's own
  // step changes. A 20 ms low-pass keeps the second derivative usable.
  const float roll_rate_dps = omega.x * kRadToDeg;
  const float roll_accel_raw = (roll_rate_dps - lean_rate_prev_dps_) / dt;
  lean_rate_prev_dps_ = roll_rate_dps;
  roll_accel_ema_ +=
      (roll_accel_raw - roll_accel_ema_) * std::clamp(dt / 0.02F, 0.0F, 1.0F);
  const float lean_accel_dps2 = roll_accel_ema_;
  Vec3f accel_compensated = accel;
  if (lever_arm_m_ > 0.0F) {
    accel_compensated.y += lever_arm_m_ * lean_accel_dps2 * kDegToRad;
    const float rate_rad = roll_rate_dps * kDegToRad;
    accel_compensated.z += lever_arm_m_ * rate_rad * rate_rad;
  }

  // Road excitation keeps this above the standstill floor. With an expired
  // GPS fix, |a| ~ 1 g alone would call a smooth straight "parked" (review §3).
  const float accel_magnitude = length(accel);
  if (std::isfinite(accel_magnitude)) {
    const float jitter = std::fabs(accel_magnitude - last_accel_magnitude_);
    accel_delta_ema_ +=
        (jitter - accel_delta_ema_) * std::clamp(dt / 0.5F, 0.0F, 1.0F);
    last_accel_magnitude_ = accel_magnitude;
  }

  // Stationary window: only here may the bias follow the gyro.
  const bool stationary = is_static(accel, gyro_dps, speed);
  static_for_s_ = stationary ? static_for_s_ + dt : 0.0F;
  const float bias_hold =
      speed.valid ? kStaticHoldS : kStaticHoldNoSpeedS;
  if (static_for_s_ >= bias_hold) {
    // tau = 10 s, applied to the raw body-frame rate.
    const float blend = dt / 10.0F;
    bias_rads_ = add(bias_rads_, scale(sub(scale(gyro_raw, kDegToRad),
                                            bias_rads_),
                                       blend));
  }

  update_straight(accel, yaw_rate_dps_, lean_rate_dps_, dt);

  // Slow observation. The accelerometer cannot be the estimate itself: in a
  // steady corner it measures along the bike's own axis (DESIGN_V2 §3.5.1).
  LeanSource source = LeanSource::Gyro;
  bool static_observation = false;
  float lean_meas = 0.0F;
  float gain = 0.0F;
  float lean_eff = std::nanf("");
  const float yaw_rad = yaw_rate_dps_ * kDegToRad;
  if (speed.valid && speed.mps > kKinematicMinSpeedMps &&
      speed.age_s < 1.5F) {
    lean_eff = std::atan(-speed.mps * yaw_rad / kGravity) * kRadToDeg;
    const float lean_align =
        std::atan2(accel_compensated.y, accel_compensated.z) * kRadToDeg;
    lean_meas = lean_eff + lean_align;
    gain = adapt_gain(1.0F / kKinematicTauS, lean_rate_dps_,
                      lean_accel_dps2, accel_compensated);
    source = LeanSource::Kinematic;
  } else if (stationary) {
    lean_meas =
        std::atan2(accel_compensated.y, accel_compensated.z) * kRadToDeg;
    gain = 1.0F / kStaticTauS;
    source = LeanSource::Static;
    static_observation = true;
  } else if (straight_for_s_ >= kStraightHoldS) {
    lean_meas = 0.0F;
    gain = 1.0F / kZeroTauS;
    source = LeanSource::Zero;
  }

  if (gain > 0.0F) {
    if (static_observation) {
      // At rest the specific force is the full up vector, so this is also the
      // only path that can observe pitch. Blending the vector (rather than
      // rotating about x) keeps nose-up from being silently dropped.
      const Vec3f measured = normalise(accel_compensated);
      const float w = std::clamp(gain * dt, 0.0F, 1.0F);
      up_body_ = normalise(add(scale(up_body_, 1.0F - w), scale(measured, w)));
    } else {
      const float correction = gain * (lean_meas - lean_deg_) * dt;
      up_body_ = normalise(rotate_about_x(up_body_, correction * kDegToRad));
    }
    lean_deg_ = std::atan2(up_body_.y, up_body_.z) * kRadToDeg;
  }

  // Slow reference for the bump gate: a steady corner must not look like a
  // pothole, so the gate compares against a 2 s running mean instead of 1 g.
  az_ref_ += (accel.z - az_ref_) * (dt / 2.0F);

  const bool saturated = std::fabs(lean_deg_) > kMaxLeanDeg;
  if (std::fabs(lean_deg_) > 8.0F) {
    in_corner_ = true;
  } else if (std::fabs(lean_deg_) < 5.0F) {
    in_corner_ = false;
  }

  std::uint16_t flags = 0;
  switch (source) {
    case LeanSource::Kinematic:
      flags |= kLeanSrcKin;
      break;
    case LeanSource::Static:
      flags |= kLeanSrcStatic;
      break;
    case LeanSource::Zero:
      flags |= kLeanSrcZero;
      break;
    case LeanSource::Gyro:
      break;
  }
  if (!speed.valid || speed.age_s > 2.5F) flags |= kLeanSpdStale;
  if (mount_.stage1) flags |= kLeanCalOk;
  if (in_corner_) flags |= kLeanInCorner;
  if (saturated) flags |= kLeanSaturated;

  float quality = 50.0F;
  switch (source) {
    case LeanSource::Kinematic:
      quality = 90.0F;
      break;
    case LeanSource::Static:
      quality = 95.0F;
      break;
    case LeanSource::Zero:
      quality = 70.0F;
      break;
    case LeanSource::Gyro:
      quality = 50.0F;
      break;
  }
  if (speed.valid) {
    quality *= std::clamp(1.0F - speed.age_s / 2.5F, 0.4F, 1.0F);
  }
  if (!mount_.stage1) quality *= 0.5F;
  if (saturated) quality *= 0.5F;

  // Horizontal-frame lateral G, right positive. In a coordinated corner the
  // body-frame y axis reads almost zero, so this is the only useful form.
  float lateral_g;
  if (speed.valid && speed.mps > kKinematicMinSpeedMps) {
    lateral_g = -speed.mps * yaw_rad / kGravity;
  } else {
    const float c = std::cos(lean_deg_ * kDegToRad);
    const float s = std::sin(lean_deg_ * kDegToRad);
    lateral_g =
        (s * accel_compensated.z - c * accel_compensated.y) / kGravity;
  }
  lateral_g = std::clamp(lateral_g, -4.0F, 4.0F);

  out_.lean_deg = lean_deg_;
  out_.lean_eff_deg = lean_eff;
  out_.lean_rate_dps = lean_rate_dps_;
  out_.pitch_deg =
      std::asin(std::clamp(-up_body_.x, -1.0F, 1.0F)) * kRadToDeg;
  out_.yaw_rate_dps = yaw_rate_dps_;
  out_.lateral_g = lateral_g;
  out_.source = source;
  out_.quality = static_cast<std::uint8_t>(std::clamp(quality, 0.0F, 100.0F));
  out_.flags = flags;
  out_.valid = true;
  out_.saturated = saturated;
}

float LeanEstimator::adapt_gain(float base, float lean_rate_dps,
                                float lean_accel_dps2,
                                const Vec3f& accel) const {
  float gain = base;
  if (std::fabs(lean_rate_dps) > 30.0F) gain *= 0.3F;
  // Lean acceleration peaks at the start and end of a flick, where the rate is
  // still small; gating on rate alone never sees it (review item 1).
  if (std::fabs(lean_accel_dps2) > 300.0F) gain *= 0.3F;
  if (std::fabs(accel.z - az_ref_) > 0.3F * kGravity) gain *= 0.2F;
  return gain;
}

}  // namespace moto::motion
