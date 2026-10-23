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
  if (std::fabs(magnitude - kGravity) > 0.02F * kGravity) return false;
  if (length(gyro_dps) > 2.0F) return false;
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
  fwd_sum_ = add(fwd_sum_, scale(normalise(horizontal), dt));
  fwd_weight_ += dt;
  if (fwd_weight_ < 0.5F) return false;
  // Orthogonalise against the already trusted up axis.
  Vec3f forward = normalise(
      sub(normalise(fwd_sum_), scale(up_, dot(normalise(fwd_sum_), up_))));
  if (length(forward) < 0.5F) return false;
  forward_ = forward;
  stage2 = true;
  return true;
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
  return {dot(sensor, forward_), dot(sensor, left), dot(sensor, up_)};
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

  // Stationary window: only here may the bias follow the gyro.
  const bool stationary = is_static(accel, gyro_dps, speed);
  static_for_s_ = stationary ? static_for_s_ + dt : 0.0F;
  if (static_for_s_ >= kStaticHoldS) {
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
    const float lean_align = std::atan2(accel.y, accel.z) * kRadToDeg;
    lean_meas = lean_eff + lean_align;
    gain = adapt_gain(1.0F / kKinematicTauS, lean_rate_dps_, accel);
    source = LeanSource::Kinematic;
  } else if (stationary) {
    lean_meas = std::atan2(accel.y, accel.z) * kRadToDeg;
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
      const Vec3f measured = normalise(accel);
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
    lateral_g = (s * accel.z - c * accel.y) / kGravity;
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
                                const Vec3f& accel) const {
  float gain = base;
  if (std::fabs(lean_rate_dps) > 30.0F) gain *= 0.3F;
  if (std::fabs(accel.z - az_ref_) > 0.3F * kGravity) gain *= 0.2F;
  return gain;
}

}  // namespace moto::motion
