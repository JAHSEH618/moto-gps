#pragma once

#include <cstdint>

namespace moto::motion {

// Bike frame used by the handlebar IMU adapter:
//   x = right, y = forward, z = up.
struct Vec3f {
  float x = 0.0F;
  float y = 0.0F;
  float z = 0.0F;
};

struct AttitudeCalibration {
  // Measured Lean/Pitch when the bike is upright and stationary.
  float lean_zero_deg = 0.0F;
  float pitch_zero_deg = 0.0F;
};

struct AttitudeEstimate {
  // Right lean is positive; nose-up pitch is positive.
  float lean_deg = 0.0F;
  float pitch_deg = 0.0F;
  // Forward acceleration is positive; braking is negative.
  float longitudinal_g = 0.0F;
  // Rightward centripetal acceleration is positive; leftward is negative.
  float lateral_g = 0.0F;
  // Kinematic target and provenance. The bridge only needs the angles; the
  // telemetry recorder stores these next to the raw sample.
  float lean_eff_deg = 0.0F;
  float lean_rate_dps = 0.0F;
  bool has_lean_eff = false;
  std::uint8_t quality = 0;
  std::uint16_t flags = 0;
  bool valid = false;
};

constexpr float kStandardGravity = 9.80665F;
constexpr float kMinimumGravity = 6.8F;
constexpr float kMaximumGravity = 12.8F;
constexpr float kMaximumLateralG = 4.0F;

// Rotate a sensor-frame vector into the bike frame using the calibrated
// screen-right and screen-up axes (both expressed in sensor coordinates).
// The mount basis is fixed to the bike, so the measured gravity direction
// changes with Lean instead of being forced upright by a gravity-derived
// basis. Returns zeros for a degenerate basis.
Vec3f sensor_to_bike(const Vec3f& sensor, const Vec3f& right,
                     const Vec3f& up);

// accel is the raw specific force at 125 Hz. gravity_up is the low-passed
// accelerometer vector (the measured up direction) in the same bike frame.
// The caller owns filtering; this function is deliberately stateless so the
// same inputs always produce the same AttitudeEstimate.
AttitudeEstimate estimate_attitude(const Vec3f& accel,
                                   const Vec3f& gravity_up,
                                   const AttitudeCalibration& calibration);

}  // namespace moto::motion
