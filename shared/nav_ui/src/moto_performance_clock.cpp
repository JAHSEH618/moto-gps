#include "moto_performance_clock.hpp"

#include <algorithm>
#include <cmath>

namespace moto::nav_ui {

namespace {

// Wrap-safe "a is later than b" on a 32-bit millisecond clock.
bool later(std::uint32_t a, std::uint32_t b) {
  return static_cast<std::int32_t>(a - b) > 0;
}

}  // namespace

void PerformanceClock::set_target_kph(std::uint16_t target_kph) {
  if (target_kph == target_kph_) return;
  target_kph_ = target_kph;
  // A run timed against the old target means nothing against the new one.
  if (state_ == State::Running) {
    abort(AbortReason::Cancel);
  } else if (state_ == State::Finished) {
    state_ = State::Armed;
    require_still_ = true;
  }
}

void PerformanceClock::require_standstill() {
  require_still_ = true;
  still_ = false;
}

PerformanceClock::Event PerformanceClock::cancel() {
  if (state_ == State::Running) return abort(AbortReason::Cancel);
  if (state_ == State::Finished) {
    state_ = State::Armed;
    require_still_ = true;
    still_ = false;
    return Event::Rearmed;
  }
  return Event::None;
}

void PerformanceClock::reset() {
  const std::uint32_t run_id = run_id_;
  const std::uint16_t target = target_kph_;
  *this = PerformanceClock{};
  run_id_ = run_id;
  target_kph_ = target;
  require_still_ = true;
}

std::uint32_t PerformanceClock::elapsed_at_last_sample_ms() const {
  switch (state_) {
    case State::Running:
      return later(last_ms_, start_ms_) ? last_ms_ - start_ms_ : 0U;
    case State::Finished:
      return result_ms();
    case State::Armed:
      break;
  }
  return 0U;
}

PerformanceClock::Event PerformanceClock::abort(AbortReason reason) {
  state_ = State::Armed;
  last_abort_ = reason;
  require_still_ = true;
  peak_kph_ = 0.0F;
  return Event::Aborted;
}

std::uint32_t PerformanceClock::crossing_ms(float threshold, float from_kph,
                                            std::uint32_t from_ms,
                                            float to_kph,
                                            std::uint32_t to_ms) {
  if (!(to_kph > from_kph)) return to_ms;
  const float fraction =
      std::clamp((threshold - from_kph) / (to_kph - from_kph), 0.0F, 1.0F);
  const auto span = static_cast<float>(to_ms - from_ms);
  return from_ms + static_cast<std::uint32_t>(std::lround(fraction * span));
}

PerformanceClock::Event PerformanceClock::add_sample(float kph,
                                                     std::uint32_t t_ms,
                                                     bool valid) {
  if (has_last_ && !later(t_ms, last_ms_)) return Event::None;
  valid = valid && std::isfinite(kph);
  has_last_ = true;
  last_valid_ = valid;
  last_kph_ = valid ? std::max(kph, 0.0F) : 0.0F;
  last_ms_ = t_ms;

  const bool gap = has_valid_ && t_ms - valid_ms_ > kMaxSampleGapMs;
  if (!valid) {
    still_ = false;
    if (state_ == State::Running && gap) return abort(AbortReason::SpeedLost);
    return Event::None;
  }

  kph = last_kph_;
  const bool prev_usable = has_valid_ && !gap;
  const float prev_kph = valid_kph_;
  const std::uint32_t prev_ms = valid_ms_;
  has_valid_ = true;
  valid_kph_ = kph;
  valid_ms_ = t_ms;

  Event gap_event = Event::None;
  if (gap) {
    // Speed came back after a hole: whatever happened in it was not timed,
    // so neither a live run nor an armed launch can carry on across it.
    if (state_ == State::Running) gap_event = abort(AbortReason::SpeedLost);
    require_still_ = true;
  }

  if (kph < kStillKph) {
    if (!still_) {
      still_ = true;
      still_since_ms_ = t_ms;
    }
  } else {
    still_ = false;
  }
  const bool held_still =
      still_ && t_ms - still_since_ms_ >= kRearmStillMs;

  switch (state_) {
    case State::Armed: {
      if (require_still_) {
        if (held_still) require_still_ = false;
        return gap_event;
      }
      if (kph < kStartKph) return gap_event;
      start_ms_ = prev_usable && prev_kph < kStartKph
                      ? crossing_ms(kStartKph, prev_kph, prev_ms, kph, t_ms)
                      : t_ms;
      state_ = State::Running;
      last_abort_ = AbortReason::None;
      peak_kph_ = kph;
      ++run_id_;
      if (kph < static_cast<float>(target_kph_)) return Event::Started;
      // Both crossings fell between the same two samples (a slow speed
      // source and a short target); finish in the same step.
      [[fallthrough]];
    }
    case State::Running: {
      peak_kph_ = std::max(peak_kph_, kph);
      const auto target = static_cast<float>(target_kph_);
      if (kph >= target) {
        finish_ms_ = prev_usable
                         ? crossing_ms(target, prev_kph, prev_ms, kph, t_ms)
                         : t_ms;
        if (later(start_ms_, finish_ms_)) finish_ms_ = start_ms_;
        state_ = State::Finished;
        still_ = false;
        return Event::Finished;
      }
      if (t_ms - start_ms_ >= kTimeoutMs) return abort(AbortReason::Timeout);
      if (peak_kph_ - kph > kAbortDropKph) {
        return abort(AbortReason::SpeedDrop);
      }
      if (held_still) return abort(AbortReason::Stopped);
      return Event::None;
    }
    case State::Finished:
      // Coasting through a junction keeps the time on the glass; only a
      // real stop clears it.
      if (held_still) {
        state_ = State::Armed;
        require_still_ = false;
        return Event::Rearmed;
      }
      return Event::None;
  }
  return Event::None;
}

}  // namespace moto::nav_ui
