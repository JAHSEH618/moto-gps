#pragma once

#include <cstdint>

namespace moto::nav_ui {

// The 0-100 clock, without the page that shows it.
//
// It times from speed samples stamped on one monotonic clock: on the board the
// IMU-fused speed (SpeedFusion, DESIGN_V2 §3.7) at the 40 Hz motion cadence,
// elsewhere the phone's speed at its fix time. Both crossings are
// interpolated between the samples either side of them, so a run timed from
// 1 Hz fixes and one timed from 40 Hz fused speed land on the same time to
// within the speed's own error, and the phone's PerformanceRunDetector
// (MotoRideCore) agrees with it on the same samples
// (shared/protocol/fixtures/performance-runs.v1.txt).
//
// The page decides when the clock listens: a run only starts on the 0-100
// page, and leaving it cancels a live run (see moto_nav_ui_gta.cpp).
class PerformanceClock {
 public:
  enum class State : std::uint8_t { Armed, Running, Finished };

  enum class Event : std::uint8_t {
    None,
    Started,
    Finished,
    Aborted,
    // A finished run left the glass after a standstill; Armed again.
    Rearmed,
  };

  enum class AbortReason : std::uint8_t {
    None,
    Timeout,    // 30 s without reaching the target
    SpeedDrop,  // more than 5 km/h below the run's peak
    Stopped,    // back at a standstill for 1.5 s
    SpeedLost,  // no usable speed for 2 s
    Cancel,     // the rider's tap, or the page went away
  };

  // A standstill is below this; a run starts at kStartKph.
  static constexpr float kStillKph = 0.5F;
  static constexpr float kStartKph = 1.0F;
  // Braking to 0 for a red light is not a new run until the wheels have been
  // still this long.
  static constexpr std::uint32_t kRearmStillMs = 1'500;
  // DESIGN_V2 §5's abort window.
  static constexpr std::uint32_t kTimeoutMs = 30'000;
  static constexpr float kAbortDropKph = 5.0F;
  // A live run with no speed for this long is not being timed any more.
  static constexpr std::uint32_t kMaxSampleGapMs = 2'000;

  void set_target_kph(std::uint16_t target_kph);
  std::uint16_t target_kph() const { return target_kph_; }

  // One speed sample. t_ms only has to be monotonic; a sample that does not
  // move time forward is ignored. valid = false is "no speed right now".
  Event add_sample(float kph, std::uint32_t t_ms, bool valid);

  // The next run has to start from a standstill held kRearmStillMs: after an
  // abort, and whenever the page is opened (a page opened at 60 km/h is not a
  // launch).
  void require_standstill();
  // A live run ends without a result; a finished run is dismissed.
  Event cancel();
  // Back to Armed with no history, keeping the run counter.
  void reset();

  State state() const { return state_; }
  AbortReason last_abort() const { return last_abort_; }
  // Bumps when a run starts; 0 before the first one.
  std::uint32_t run_id() const { return run_id_; }
  // Running: when the run started. Finished: when it crossed the target.
  // Both on the samples' clock, interpolated.
  std::uint32_t start_ms() const { return start_ms_; }
  std::uint32_t finish_ms() const { return finish_ms_; }
  // Finished: the run's time.
  std::uint32_t result_ms() const { return finish_ms_ - start_ms_; }
  // Running: the time from the start to the newest sample.
  std::uint32_t elapsed_at_last_sample_ms() const;
  std::uint32_t last_sample_ms() const { return last_ms_; }
  bool has_speed() const { return has_last_ && last_valid_; }
  float last_kph() const { return last_valid_ ? last_kph_ : 0.0F; }
  float peak_kph() const { return peak_kph_; }
  bool waiting_for_standstill() const { return require_still_; }

 private:
  Event abort(AbortReason reason);
  static std::uint32_t crossing_ms(float threshold, float from_kph,
                                   std::uint32_t from_ms, float to_kph,
                                   std::uint32_t to_ms);

  State state_ = State::Armed;
  AbortReason last_abort_ = AbortReason::None;
  std::uint16_t target_kph_ = 100;
  std::uint32_t run_id_ = 0;
  std::uint32_t start_ms_ = 0;
  std::uint32_t finish_ms_ = 0;
  float peak_kph_ = 0.0F;
  // A clock that has never seen the bike stopped cannot tell a launch from
  // a speed that simply became known at 40 km/h.
  bool require_still_ = true;
  bool still_ = false;
  std::uint32_t still_since_ms_ = 0;
  bool has_last_ = false;
  bool last_valid_ = false;
  float last_kph_ = 0.0F;
  std::uint32_t last_ms_ = 0;
  // The newest valid sample, for the gap rule and the interpolation.
  bool has_valid_ = false;
  float valid_kph_ = 0.0F;
  std::uint32_t valid_ms_ = 0;
};

}  // namespace moto::nav_ui
