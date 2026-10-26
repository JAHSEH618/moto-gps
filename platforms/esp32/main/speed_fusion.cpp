#include "speed_fusion.hpp"

#include <algorithm>
#include <cmath>

namespace moto::motion {
namespace {

// §2.8: a phone speed is fresh for 2.5 s, IMU-extrapolated after that, and
// gone at 5 s.
constexpr std::int32_t kValidForMs = 5'000;
// Growth of the speed variance between corrections, m^2/s^3: 0.5 m/s of
// doubt after a second of integration. With a 0.5 m/s fix at 1 Hz the gain
// settles near 0.6; a 0.2 m/s fix gets about 0.9.
constexpr float kProcessNoise = 0.25F;
constexpr float kMaxVariance = 1.0e4F;
// Core Location does not always report speedAccuracy. Below 0.1 m/s it is
// claiming more than Doppler on a phone delivers.
constexpr float kUnknownAccuracyMps = 0.5F;
constexpr float kMinAccuracyMps = 0.1F;
// Parked, the speed is known to be zero to within the gate's own doubt.
constexpr float kParkedVariance = 0.01F;
// bias_lon is a slow state: one fix moves it by this fraction of what its
// residual implies, so a single bad fix cannot tilt every later second.
constexpr float kBiasGain = 0.05F;
constexpr float kMaxBiasMps2 = 2.0F;
// While parked the residual force along the levelled forward axis is the
// error itself; follow it with this time constant.
constexpr float kParkedBiasTauS = 5.0F;
// Residual over a gap shorter than this is mostly fix noise, longer is
// mostly the previous fix's leftover; neither says much about the bias.
constexpr float kBiasMinGapS = 0.2F;
constexpr float kBiasMaxGapS = 3.0F;

bool finite(const Vec3f& v) {
  return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

float dot(const Vec3f& a, const Vec3f& b) {
  return a.x * b.x + a.y * b.y + a.z * b.z;
}

std::int32_t since(std::uint32_t later, std::uint32_t earlier) {
  return static_cast<std::int32_t>(later - earlier);
}

}  // namespace

void SpeedFusion::reset() { *this = SpeedFusion{}; }

float SpeedFusion::sigma_mps() const { return std::sqrt(variance_); }

void SpeedFusion::step(const Vec3f& accel_body, const Vec3f& up_body,
                       bool stationary, float dt, std::uint32_t now_ms) {
  if (!finite(accel_body) || !finite(up_body)) return;
  if (!std::isfinite(dt) || dt <= 0.0F) return;
  dt = std::clamp(dt, 0.0005F, 0.05F);

  // x_h = normalize(x - (x·û)û), with x = body forward (1, 0, 0). Pitched
  // past ~78° the horizontal forward is undefined and the step only ages.
  const float up_length = std::sqrt(dot(up_body, up_body));
  float force = 0.0F;
  bool levelled = false;
  if (up_length > 0.5F) {
    const Vec3f up{up_body.x / up_length, up_body.y / up_length,
                   up_body.z / up_length};
    const Vec3f forward{1.0F - up.x * up.x, -up.x * up.y, -up.x * up.z};
    const float forward_length = std::sqrt(dot(forward, forward));
    if (forward_length > 0.2F) {
      force = dot(accel_body, forward) / forward_length;
      levelled = true;
    }
  }

  if (stationary) {
    // ZUPT (§3.4 gate). Gravity has no horizontal component, so whatever
    // force remains along the levelled forward axis is the slow error.
    speed_mps_ = 0.0F;
    variance_ = std::min(variance_, kParkedVariance);
    if (levelled) {
      bias_mps2_ += (force - bias_mps2_) *
                    std::clamp(dt / kParkedBiasTauS, 0.0F, 1.0F);
      bias_mps2_ = std::clamp(bias_mps2_, -kMaxBiasMps2, kMaxBiasMps2);
    }
  } else if (levelled) {
    speed_mps_ += (force - bias_mps2_) * dt;
  }
  variance_ = std::min(variance_ + kProcessNoise * dt, kMaxVariance);

  now_ms_ = now_ms;
  has_time_ = true;
  record(now_ms);
}

void SpeedFusion::record(std::uint32_t now_ms) {
  if (history_count_ > 0) {
    const HistoryPoint& newest =
        history_[(history_next_ + kHistorySize - 1) % kHistorySize];
    if (since(now_ms, newest.t_ms) < static_cast<std::int32_t>(kHistoryStepMs)) {
      return;
    }
  }
  history_[history_next_] = {now_ms, speed_mps_};
  history_next_ = (history_next_ + 1) % kHistorySize;
  if (history_count_ < kHistorySize) ++history_count_;
}

bool SpeedFusion::speed_at(std::uint32_t t_ms, float* mps) const {
  // The present is always a point, even between recorded ones.
  if (since(t_ms, now_ms_) >= 0) {
    *mps = speed_mps_;
    return true;
  }
  HistoryPoint later{now_ms_, speed_mps_};
  for (std::size_t i = 0; i < history_count_; ++i) {
    const HistoryPoint& point =
        history_[(history_next_ + kHistorySize - 1 - i) % kHistorySize];
    if (since(t_ms, point.t_ms) >= 0) {
      const std::int32_t span = since(later.t_ms, point.t_ms);
      if (span <= 0) {
        *mps = point.mps;
        return true;
      }
      const float w =
          static_cast<float>(since(t_ms, point.t_ms)) /
          static_cast<float>(span);
      *mps = point.mps + (later.mps - point.mps) * w;
      return true;
    }
    later = point;
  }
  return false;
}

void SpeedFusion::shift_since(std::uint32_t t_ms, float delta_mps) {
  for (std::size_t i = 0; i < history_count_; ++i) {
    HistoryPoint& point = history_[i];
    if (since(point.t_ms, t_ms) >= 0) point.mps += delta_mps;
  }
}

bool SpeedFusion::add_fix(const SpeedFix& fix) {
  if (!std::isfinite(fix.mps) || fix.mps < 0.0F) {
    ++rejected_;
    return false;
  }
  if (has_fix_ && since(fix.fix_ms, last_fix_ms_) <= 0) {
    ++rejected_;
    return false;
  }
  std::uint32_t fix_ms = fix.fix_ms;
  // The clock mapping can be a few ms ahead; a fix is never from the future.
  if (has_time_ && since(fix_ms, now_ms_) > 0) fix_ms = now_ms_;

  const float accuracy =
      std::isfinite(fix.accuracy_mps) && fix.accuracy_mps >= 0.0F
          ? std::max(fix.accuracy_mps, kMinAccuracyMps)
          : kUnknownAccuracyMps;
  const float noise = accuracy * accuracy;

  if (!has_time_) {
    // No IMU sample yet: nothing to carry forward.
    speed_mps_ = fix.mps;
    variance_ = noise;
    has_fix_ = true;
    last_fix_ms_ = fix_ms;
    return true;
  }

  float then_mps = 0.0F;
  if (!speed_at(fix_ms, &then_mps)) {
    ++rejected_;
    return false;
  }
  const float residual = fix.mps - then_mps;
  // The first fix replaces whatever the IMU integrated from nothing.
  const float gain = has_fix_ ? variance_ / (variance_ + noise) : 1.0F;
  const float correction = gain * residual;
  // Corrected as of fix_ms: the same offset applies to everything integrated
  // since, and to the present.
  speed_mps_ += correction;
  shift_since(fix_ms, correction);
  variance_ = has_fix_ ? (1.0F - gain) * variance_ : noise;

  if (has_fix_) {
    // A residual of r over T seconds is r/T of unexplained acceleration; a
    // positive residual means the integration ran slow, i.e. bias too high.
    const float gap_s = static_cast<float>(since(fix_ms, last_fix_ms_)) /
                        1'000.0F;
    if (gap_s >= kBiasMinGapS && gap_s <= kBiasMaxGapS) {
      bias_mps2_ = std::clamp(bias_mps2_ - kBiasGain * residual / gap_s,
                              -kMaxBiasMps2, kMaxBiasMps2);
    }
  }
  has_fix_ = true;
  last_fix_ms_ = fix_ms;
  return true;
}

SpeedSample SpeedFusion::sample(std::uint32_t now_ms) const {
  SpeedSample sample;
  if (!has_fix_) return sample;
  // int32_t is `long` on xtensa, so the literal needs the type spelled out.
  const std::int32_t age_ms =
      std::max<std::int32_t>(0, since(now_ms, last_fix_ms_));
  if (age_ms > kValidForMs) return sample;
  sample.valid = true;
  sample.mps = std::max(0.0F, speed_mps_);
  sample.age_s = static_cast<float>(age_ms) / 1'000.0F;
  return sample;
}

}  // namespace moto::motion
