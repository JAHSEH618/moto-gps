#pragma once

#include <cstdint>

// AMOLED burn-in and idle policy (DESIGN_V2 §2.6): every 120 s the whole frame
// moves to the next of nine ±2 px orbit positions; after 5 min still the panel
// dims by 30%; after 15 min still it goes dark. A route that is active holds
// the panel at full brightness.
//
// Pure: the firmware feeds it timestamps and reads back what the panel should
// be, so the host tests can pin the clock. Not thread-safe; the burn-in task
// is its only caller.
namespace moto::device {

enum class ScreenLevel : std::uint8_t { Full, Dim, Off };

inline const char* to_string(ScreenLevel level) {
  switch (level) {
    case ScreenLevel::Full: return "full";
    case ScreenLevel::Dim: return "dim";
    case ScreenLevel::Off: return "off";
  }
  return "?";
}

struct BurnInConfig {
  std::uint32_t orbit_period_ms = 120'000;
  std::uint32_t dim_after_ms = 5U * 60'000U;
  std::uint32_t off_after_ms = 15U * 60'000U;
  // The phone repeats NavigationSnapshot while a route runs. A route last heard
  // longer ago than this is a lost link, not a route, and no longer holds the
  // panel lit.
  std::uint32_t route_stale_ms = 30'000;
  std::uint8_t full_percent = 100;
  std::uint8_t dim_percent = 70;
};

struct BurnInOutput {
  ScreenLevel level = ScreenLevel::Full;
  // What 0x51 should carry; 0 when the panel is off.
  std::uint8_t brightness_percent = 100;
  // Frame offset in panel pixels. Always even: the CO5300 needs two-pixel
  // aligned windows, and the board rounds every dirty area to that grid.
  std::int8_t dx = 0;
  std::int8_t dy = 0;
  std::uint8_t orbit_index = 0;
  bool route_held = false;
};

class BurnInPolicy {
 public:
  // Centre first, then once around the square. Each step moves at most 2 px
  // per axis, so a shift is never a visible jump.
  static constexpr std::uint8_t kOrbitPositions = 9;
  static constexpr std::int8_t kOrbitDx[kOrbitPositions] = {0, 2, 2, 0, -2,
                                                            -2, -2, 0, 2};
  static constexpr std::int8_t kOrbitDy[kOrbitPositions] = {0, 0, 2, 2, 2,
                                                            0, -2, -2, -2};
  static constexpr std::uint8_t kOrbitAuto = 0xFF;

  explicit BurnInPolicy(const BurnInConfig& config = {},
                        std::uint64_t now_ms = 0)
      : config_(config),
        last_activity_ms_(static_cast<std::int64_t>(now_ms)) {}

  const BurnInConfig& config() const { return config_; }
  // The stillness clock restarts so a shorter bench timing does not fire at
  // once on the time already spent still.
  void set_config(const BurnInConfig& config, std::uint64_t now_ms) {
    config_ = config;
    last_activity_ms_ = static_cast<std::int64_t>(now_ms);
  }

  // Touch, vehicle motion or a navigation event at now_ms.
  void note_activity(std::uint64_t now_ms) {
    const auto now = static_cast<std::int64_t>(now_ms);
    if (now > last_activity_ms_) last_activity_ms_ = now;
  }

  // One NavigationSnapshot. A route starting or ending is a navigation event.
  void note_navigation(bool route_active, std::uint64_t now_ms) {
    const bool was_active = route_active_at(now_ms);
    last_navigation_ms_ = now_ms;
    has_navigation_ = true;
    route_active_ = route_active;
    if (route_active != was_active) note_activity(now_ms);
  }

  // Bench: move the stillness clock so the panel is at `level` now and goes
  // on from there exactly as it would on its own (a touch still wakes it).
  void force(ScreenLevel level, std::uint64_t now_ms) {
    std::uint64_t still_ms = 0;
    if (level == ScreenLevel::Dim) still_ms = config_.dim_after_ms;
    if (level == ScreenLevel::Off) still_ms = config_.off_after_ms;
    // Signed, so a forced state also works in the first minutes after boot.
    last_activity_ms_ = static_cast<std::int64_t>(now_ms) -
                        static_cast<std::int64_t>(still_ms);
  }

  // Bench: pin the orbit to one position, or kOrbitAuto to follow the clock.
  void pin_orbit(std::uint8_t index) {
    pinned_orbit_ = index < kOrbitPositions ? index : kOrbitAuto;
  }

  std::uint64_t still_ms(std::uint64_t now_ms) const {
    const auto now = static_cast<std::int64_t>(now_ms);
    return now > last_activity_ms_
               ? static_cast<std::uint64_t>(now - last_activity_ms_)
               : 0;
  }

  bool route_active_at(std::uint64_t now_ms) const {
    return has_navigation_ && route_active_ &&
           now_ms - last_navigation_ms_ <= config_.route_stale_ms;
  }

  BurnInOutput evaluate(std::uint64_t now_ms) {
    BurnInOutput out;
    out.route_held = route_active_at(now_ms);
    if (out.route_held) {
      // Navigating is activity: when the route ends the clock starts there,
      // not at the last touch before it.
      note_activity(now_ms);
    }
    const std::uint64_t still = still_ms(now_ms);
    if (still >= config_.off_after_ms) {
      out.level = ScreenLevel::Off;
      out.brightness_percent = 0;
    } else if (still >= config_.dim_after_ms) {
      out.level = ScreenLevel::Dim;
      out.brightness_percent = config_.dim_percent;
    } else {
      out.level = ScreenLevel::Full;
      out.brightness_percent = config_.full_percent;
    }
    out.orbit_index =
        pinned_orbit_ != kOrbitAuto
            ? pinned_orbit_
            : static_cast<std::uint8_t>(
                  (config_.orbit_period_ms == 0
                       ? 0
                       : now_ms / config_.orbit_period_ms) %
                  kOrbitPositions);
    out.dx = kOrbitDx[out.orbit_index];
    out.dy = kOrbitDy[out.orbit_index];
    return out;
  }

 private:
  BurnInConfig config_;
  std::int64_t last_activity_ms_ = 0;
  std::uint64_t last_navigation_ms_ = 0;
  bool has_navigation_ = false;
  bool route_active_ = false;
  std::uint8_t pinned_orbit_ = kOrbitAuto;
};

}  // namespace moto::device
