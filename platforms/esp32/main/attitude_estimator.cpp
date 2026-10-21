#include "attitude_estimator.hpp"

#include <algorithm>
#include <cmath>

namespace moto::motion {
namespace {

constexpr float kRadiansToDegrees = 57.29577951308232F;
constexpr float kDegreesToRadians = 0.017453292519943295F;

bool finite(const Vec3f& value) {
  return std::isfinite(value.x) && std::isfinite(value.y) &&
         std::isfinite(value.z);
}

Vec3f cross(const Vec3f& a, const Vec3f& b) {
  return {a.y * b.z - a.z * b.y,
          a.z * b.x - a.x * b.z,
          a.x * b.y - a.y * b.x};
}

float dot(const Vec3f& a, const Vec3f& b) {
  return a.x * b.x + a.y * b.y + a.z * b.z;
}

float length(const Vec3f& value) {
  return std::sqrt(value.x * value.x + value.y * value.y +
                   value.z * value.z);
}

Vec3f normalise(const Vec3f& value) {
  const float magnitude = length(value);
  if(!std::isfinite(magnitude) || magnitude < 0.01F) return {};
  return {value.x / magnitude, value.y / magnitude, value.z / magnitude};
}

}  // namespace

Vec3f sensor_to_bike(const Vec3f& sensor, const Vec3f& right,
                     const Vec3f& up) {
  if(!finite(sensor) || !finite(right) || !finite(up)) return {};
  const Vec3f unit_right = normalise(right);
  const Vec3f unit_up = normalise(up);
  if(length(unit_right) < 0.5F || length(unit_up) < 0.5F) return {};
  const Vec3f forward = normalise(cross(unit_up, unit_right));
  if(length(forward) < 0.5F) return {};
  return {dot(sensor, unit_right), dot(sensor, forward),
          dot(sensor, unit_up)};
}

AttitudeEstimate estimate_attitude(const Vec3f& accel,
                                   const Vec3f& gravity_up,
                                   const AttitudeCalibration& calibration) {
  AttitudeEstimate estimate;
  if(!finite(accel) || !finite(gravity_up) ||
     !std::isfinite(calibration.lean_zero_deg) ||
     !std::isfinite(calibration.pitch_zero_deg)) {
    return estimate;
  }

  const float magnitude =
      std::sqrt(gravity_up.x * gravity_up.x +
                gravity_up.y * gravity_up.y +
                gravity_up.z * gravity_up.z);
  if(!std::isfinite(magnitude) || magnitude < kMinimumGravity ||
     magnitude > kMaximumGravity) {
    return estimate;
  }

  // gravity_up is the measured up vector. Leaning right tilts that vector
  // towards the bike's left axis, hence the negated x term.
  const float raw_lean =
      std::atan2(-gravity_up.x, gravity_up.z) * kRadiansToDegrees;
  const float raw_pitch =
      std::atan2(gravity_up.y, gravity_up.z) * kRadiansToDegrees;

  estimate.lean_deg = raw_lean - calibration.lean_zero_deg;
  estimate.pitch_deg = raw_pitch - calibration.pitch_zero_deg;
  // Specific force minus the filtered gravity component along the bike's
  // forward axis is the actual longitudinal acceleration.
  estimate.longitudinal_g =
      (accel.y - gravity_up.y) / kStandardGravity;
  // In a coordinated corner the horizontal centripetal acceleration is
  // g * tan(Lean). Clamp the extreme tail so a noisy near-90 degree estimate
  // cannot produce an absurd number on the instrument.
  const float lateral =
      std::tan(estimate.lean_deg * kDegreesToRadians);
  estimate.lateral_g =
      std::clamp(lateral, -kMaximumLateralG, kMaximumLateralG);
  estimate.valid = true;
  return estimate;
}

}  // namespace moto::motion
