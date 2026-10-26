#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

// The phone's GNSS speed as this device sees it (DESIGN_V2 §3.7, §12).
//
// Header-only: PhoneNavBridge builds a SpeedFix on the BLE task and the motion
// sensor consumes it on its own, and the host tests link neither.
namespace moto::motion {

// One GNSS speed, placed on this device's monotonic clock. `fix_ms` is when
// the phone took the fix, not when the message arrived: a fix that waited in
// the phone's send queue is that much older.
struct SpeedFix {
  float mps = 0.0F;
  // One sigma, from Core Location's speedAccuracy. Negative when unknown.
  float accuracy_mps = -1.0F;
  std::uint32_t fix_ms = 0;
  // Arrival on this device, kept for the recorder; the fusion uses fix_ms.
  std::uint32_t rx_ms = 0;
  // From GnssInput (fix time known) rather than a snapshot (arrival only).
  bool timestamped = false;
};

// Maps the phone's monotonic milliseconds onto this device's from one-way
// samples: offset = arrival on this device - the phone's send stamp. Transit
// only ever adds to that, so the smallest offset seen recently is the best
// estimate; it is the true offset plus the shortest transit, which is a
// connection interval or two and is the only delay this cannot see. A window
// instead of an all-time minimum lets the estimate follow drift between the
// two crystals, and recover in a window's time if the phone clock steps.
//
// All arithmetic is modulo 2^32: both clocks are the low 32 bits of
// millisecond counters, and the phone's has usually wrapped many times.
class PhoneClock {
 public:
  void reset() {
    count_ = 0;
    next_ = 0;
  }

  void observe(std::uint32_t phone_ms, std::uint32_t device_rx_ms) {
    offsets_[next_] = device_rx_ms - phone_ms;
    next_ = (next_ + 1) % kWindow;
    if (count_ < kWindow) ++count_;
  }

  bool ready() const { return count_ > 0; }

  // Only meaningful once ready().
  std::uint32_t to_device(std::uint32_t phone_ms) const {
    return phone_ms + offset_ms();
  }

  std::uint32_t offset_ms() const {
    // Offsets in one window lie within seconds of each other, so a signed
    // difference orders them correctly across the wrap.
    std::uint32_t best = offsets_[(next_ + kWindow - 1) % kWindow];
    for (std::size_t i = 0; i < count_; ++i) {
      const std::uint32_t candidate = offsets_[i];
      if (static_cast<std::int32_t>(candidate - best) < 0) best = candidate;
    }
    return best;
  }

 private:
  // Sixteen fixes, about sixteen seconds at Core Location's usual 1 Hz.
  static constexpr std::size_t kWindow = 16;
  std::array<std::uint32_t, kWindow> offsets_{};
  std::size_t count_ = 0;
  std::size_t next_ = 0;
};

}  // namespace moto::motion
