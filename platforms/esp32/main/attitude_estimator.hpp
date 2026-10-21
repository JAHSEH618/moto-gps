#pragma once

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
  bool valid = false;
};

constexpr float kStandardGravity = 9.80665F;
constexpr float kMinimumGravity = 6.8F;
constexpr float kMaximumGravity = 12.8F;
constexpr float kMaximumLateralG = 4.0F;

// accel is the raw specific force at 125 Hz. gravity_up is the low-passed
// accelerometer vector (the measured up direction) in the same bike frame.
// The caller owns filtering; this function is deliberately stateless so the
// same inputs always produce the same AttitudeEstimate.
AttitudeEstimate estimate_attitude(const Vec3f& accel,
                                   const Vec3f& gravity_up,
                                   const AttitudeCalibration& calibration);

}  // namespace moto::motion
