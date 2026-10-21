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

}  // namespace

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
