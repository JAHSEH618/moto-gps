#pragma once

#include <array>
#include <cstdint>

#include "gnss_input.hpp"
#include "lean_estimator.hpp"

// v2 speed fusion (DESIGN_V2 §3.7).
//
// The phone's GNSS speed arrives about once a second, already some hundreds of
// milliseconds old. Between fixes the speed follows the longitudinal specific
// force projected onto the horizontal plane:
//
//   x_h   = normalize(x - (x·û)û)        forward, levelled by the Lean state
//   a_lon = a·x_h - bias_lon             gravity has no horizontal component
//   v    += a_lon·dt                     and v = 0 while parked (ZUPT)
//
// A fix corrects the speed the way it was *when the fix was taken*: the
// residual is taken against the recorded trajectory at fix_ms, and the
// correction is carried forward through everything integrated since. So a
// fix that arrives 600 ms late in a hard stop still pulls the right way.
//
// The gain is a scalar Kalman gain: the speed's variance grows with time since
// the last correction and the fix brings its own accuracy. bias_lon learns
// only slowly, from residuals that keep the same sign across fixes (pitch
// error, the accelerometer's offset) and from the residual force while parked.
namespace moto::motion {

class SpeedFusion {
 public:
  // The caller's parked gate: the Lean estimator's stillness held this long
  // (§3.4). The firmware and the replay must use the same value.
  static constexpr float kParkedForS = 0.5F;

  void reset();

  // One IMU step. accel_body is the specific force in the ISO body frame
  // (m/s^2, x forward); up_body is world up in that frame, from the Lean
  // estimator; stationary is the parked gate (§3.4). now_ms is this sample's
  // time on the device clock.
  void step(const Vec3f& accel_body, const Vec3f& up_body, bool stationary,
            float dt, std::uint32_t now_ms);

  // Applies one GNSS speed. False when it cannot be used: not finite, no
  // newer than a fix already applied, or older than the recorded trajectory.
  bool add_fix(const SpeedFix& fix);

  // What the Lean estimator sees. Valid for 5 s after the newest fix was
  // taken (§2.8; extrapolated by the IMU after 2.5 s), with that fix's age.
  SpeedSample sample(std::uint32_t now_ms) const;

  bool has_fix() const { return has_fix_; }
  float speed_mps() const { return speed_mps_; }
  float bias_mps2() const { return bias_mps2_; }
  float sigma_mps() const;
  std::uint32_t last_fix_ms() const { return last_fix_ms_; }
  std::uint32_t rejected_fixes() const { return rejected_; }

 private:
  struct HistoryPoint {
    std::uint32_t t_ms = 0;
    float mps = 0.0F;
  };

  // 64 points 50 ms apart: 3.2 s of trajectory, more than the 2.5 s after
  // which a phone speed no longer counts as fresh.
  static constexpr std::size_t kHistorySize = 64;
  static constexpr std::uint32_t kHistoryStepMs = 50;

  bool speed_at(std::uint32_t t_ms, float* mps) const;
  void shift_since(std::uint32_t t_ms, float delta_mps);
  void record(std::uint32_t now_ms);

  float speed_mps_ = 0.0F;
  float bias_mps2_ = 0.0F;
  float variance_ = 0.0F;
  bool has_time_ = false;
  bool has_fix_ = false;
  std::uint32_t now_ms_ = 0;
  std::uint32_t last_fix_ms_ = 0;
  std::uint32_t rejected_ = 0;
  std::array<HistoryPoint, kHistorySize> history_{};
  std::size_t history_count_ = 0;
  std::size_t history_next_ = 0;
};

}  // namespace moto::motion
