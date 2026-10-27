#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

// Short-term heading fusion for the handlebar-mounted display.
//
// The phone's Core Location course is an absolute, drift-free reference while
// the motorcycle is moving. The onboard QMI8658 gyroscope fills the latency
// between phone fixes and preserves responsive relative turns at very low
// speed. Since QMI8658 has no magnetometer, this class deliberately never
// claims to create an absolute north reference by itself.
class MotionHeadingFusion {
 public:
  void reset() noexcept {
    initialized_ = false;
    has_phone_reference_ = false;
    has_moving_course_ = false;
    stationary_ = false;
    heading_deg_ = 0.0F;
    filtered_rate_dps_ = 0.0F;
    last_sample_ms_ = 0;
    has_compass_target_ = false;
    compass_slewed_ = false;
    compass_target_deg_ = 0.0F;
    slew_last_ms_ = 0;
  }

  void anchor(float phone_heading_deg, float speed_mps,
              bool usable_fix) noexcept {
    if (!usable_fix || !std::isfinite(phone_heading_deg)) {
      return;
    }
    // Hysteresis on the phone-reported speed decides whether the bike is
    // parked.  While parked, gyro integration is frozen: the QMI8658 zero
    // offset (a few dps before calibration, worsened by engine vibration)
    // otherwise leaks through the deadband and spins the heading in circles.
    if (std::isfinite(speed_mps)) {
      if (speed_mps >= kCourseAnchorSpeedMps) {
        stationary_ = false;
      } else if (speed_mps < kStationarySpeedMps) {
        stationary_ = true;
      }
    }
    const float phone = normalize(phone_heading_deg);
    const bool moving_course =
        std::isfinite(speed_mps) && speed_mps >= kCourseAnchorSpeedMps;
    // Gyro integration may start seconds before BLE supplies the first fix.
    // Its local zero must never be mistaken for an absolute phone reference.
    // Likewise, the first course after pulling away replaces a stationary
    // placeholder immediately, so an east/west route is not drawn behind us.
    if (!has_phone_reference_ || (moving_course && !has_moving_course_)) {
      heading_deg_ = phone;
      initialized_ = true;
      has_phone_reference_ = true;
      has_moving_course_ = moving_course;
      has_compass_target_ = false;
      return;
    }

    // A course is trustworthy only once the receiver is actually moving.
    // Correct drift progressively to avoid a visible snap during a bend.
    if (moving_course) {
      const float error = shortest_delta(phone, heading_deg_);
      const float correction = std::abs(error) > 80.0F ? 0.65F : 0.28F;
      heading_deg_ = normalize(heading_deg_ + error * correction);
    }
  }

  // The phone's compass (magnetometer) heading, which it sends while the
  // bike is too slow for a GNSS course (NavigationHeadingCompass). Without
  // it a parked handlebar keeps whatever course it last had: the rider saw
  // 60 degrees on the bars while the phone said due east. The compass is
  // absolute at any speed, so it replaces a gyro or stationary placeholder at
  // once and pulls an existing heading towards itself quickly; the next
  // moving course still replaces it in one update, because a course does
  // not depend on how the phone sits in its mount.
  void anchor_compass(float compass_deg, float speed_mps) noexcept {
    if (!std::isfinite(compass_deg)) return;
    if (std::isfinite(speed_mps)) {
      if (speed_mps >= kCourseAnchorSpeedMps) {
        stationary_ = false;
      } else if (speed_mps < kStationarySpeedMps) {
        stationary_ = true;
      }
    }
    const float compass = normalize(compass_deg);
    has_moving_course_ = false;
    if (!has_phone_reference_) {
      heading_deg_ = compass;
      initialized_ = true;
      has_phone_reference_ = true;
      has_compass_target_ = false;
      return;
    }
    if (stationary_) {
      // Parked, the gyro is frozen and this compass is the whole heading.
      // Applying it here moved the map once per report (once a second, then
      // 70% of the way), so a turned phone took two or three seconds to show
      // on the dial. Instead it becomes a target that integrate() glides to
      // at the sensor rate. A slew no IMU sample has advanced since the last
      // report means there is no IMU: land that one so the compass still
      // steps without a QMI8658.
      if (has_compass_target_ && !compass_slewed_) {
        heading_deg_ = compass_target_deg_;
      }
      compass_target_deg_ = compass;
      has_compass_target_ = true;
      compass_slewed_ = false;
      return;
    }
    // Rolling slowly the gyro integrates between reports; the phone filters
    // its own compass, and most of the error per report keeps a slow pivot
    // readable without a visible snap.
    const float error = shortest_delta(compass, heading_deg_);
    heading_deg_ = normalize(heading_deg_ + error * kCompassCorrection);
  }

  // yaw_rate_dps is positive for increasing compass heading (clockwise).
  // Returns true when a new display heading is available.
  bool integrate(float yaw_rate_dps, std::uint64_t sample_ms) noexcept {
    if (!std::isfinite(yaw_rate_dps)) {
      return false;
    }
    if (stationary_) {
      // Parked: the gyro is ignored and only a compass target moves the
      // heading. Keep last_sample_ms_ untouched so the first sample after
      // pulling away only sees a normal-rate gap.
      return slew_to_compass(sample_ms);
    }
    // A target left from parking is stale once the gyro integrates again.
    has_compass_target_ = false;
    slew_last_ms_ = 0;
    if (!initialized_) {
      // Relative motion remains useful before the first trustworthy course;
      // zero degrees is explicitly just a temporary local reference.
      initialized_ = true;
      heading_deg_ = 0.0F;
    }
    if (last_sample_ms_ == 0) {
      last_sample_ms_ = sample_ms;
      return false;
    }
    if (sample_ms <= last_sample_ms_) {
      return false;
    }

    const std::uint64_t elapsed_ms = sample_ms - last_sample_ms_;
    last_sample_ms_ = sample_ms;
    if (elapsed_ms > kMaximumIntegrationGapMs) {
      filtered_rate_dps_ = 0.0F;
      return false;
    }

    const float bounded = std::clamp(yaw_rate_dps, -500.0F, 500.0F);
    // About 30 ms of smoothing at the 125 Hz sensor rate: vibration is
    // suppressed without the sluggish response of phone-only course updates.
    filtered_rate_dps_ += (bounded - filtered_rate_dps_) * 0.24F;
    const float active_rate = std::abs(filtered_rate_dps_) < kDeadbandDps
                                  ? 0.0F
                                  : filtered_rate_dps_;
    if (active_rate == 0.0F) {
      return false;
    }
    heading_deg_ = normalize(
        heading_deg_ + active_rate * static_cast<float>(elapsed_ms) / 1'000.0F);
    return true;
  }

  [[nodiscard]] bool initialized() const noexcept { return initialized_; }
  [[nodiscard]] float heading_deg() const noexcept { return heading_deg_; }

 private:
  static constexpr float kCourseAnchorSpeedMps = 1.5F;
  static constexpr float kStationarySpeedMps = 0.6F;
  static constexpr float kDeadbandDps = 0.65F;
  static constexpr float kCompassCorrection = 0.7F;
  static constexpr std::uint64_t kMaximumIntegrationGapMs = 160;
  // 80 ms: a compass step is 92% drawn before the phone's next 5 Hz report,
  // and the 40 Hz map frames show a turn rather than a jump.
  static constexpr float kCompassSlewTauMs = 80.0F;
  static constexpr float kCompassSettledDeg = 0.05F;

  // Runs on every parked IMU sample. The clock advances even without a
  // target, so a new target's first step sees one sample period, not the
  // time since the last report.
  bool slew_to_compass(std::uint64_t sample_ms) noexcept {
    const std::uint64_t previous_ms = slew_last_ms_;
    if (sample_ms <= previous_ms) return false;
    slew_last_ms_ = sample_ms;
    if (!has_compass_target_ || previous_ms == 0) return false;
    compass_slewed_ = true;
    const float error = shortest_delta(compass_target_deg_, heading_deg_);
    if (std::abs(error) < kCompassSettledDeg) {
      heading_deg_ = compass_target_deg_;
      has_compass_target_ = false;
      return true;
    }
    const std::uint64_t elapsed_ms =
        std::min(sample_ms - previous_ms, kMaximumIntegrationGapMs);
    const float step = 1.0F - std::exp(-static_cast<float>(elapsed_ms) /
                                       kCompassSlewTauMs);
    heading_deg_ = normalize(heading_deg_ + error * step);
    return true;
  }

  static float normalize(float value) noexcept {
    float result = std::fmod(value, 360.0F);
    if (result < 0.0F) result += 360.0F;
    return result;
  }

  static float shortest_delta(float target, float current) noexcept {
    float delta = normalize(target) - normalize(current);
    if (delta > 180.0F) delta -= 360.0F;
    if (delta < -180.0F) delta += 360.0F;
    return delta;
  }

  bool initialized_ = false;
  bool has_phone_reference_ = false;
  bool has_moving_course_ = false;
  bool stationary_ = false;
  float heading_deg_ = 0.0F;
  float filtered_rate_dps_ = 0.0F;
  std::uint64_t last_sample_ms_ = 0;
  bool has_compass_target_ = false;
  bool compass_slewed_ = false;
  float compass_target_deg_ = 0.0F;
  std::uint64_t slew_last_ms_ = 0;
};
