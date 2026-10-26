#include "speed_fusion.hpp"

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <vector>

namespace {

int failures = 0;

#define CHECK(condition)                                                   \
  do {                                                                     \
    if (!(condition)) {                                                    \
      std::cerr << __FILE__ << ':' << __LINE__                             \
                << " CHECK failed: " #condition << '\n';                  \
      ++failures;                                                          \
    }                                                                      \
  } while (false)

using moto::motion::PhoneClock;
using moto::motion::SpeedFix;
using moto::motion::SpeedFusion;
using moto::motion::SpeedSample;
using moto::motion::Vec3f;

constexpr float kGravity = 9.80665F;
constexpr float kDegToRad = 0.017453292519943295F;
// The latch chain's sample interval: two 448.4 Hz ODR periods.
constexpr std::uint32_t kStepUs = 4'464;

// A straight road with a phone that takes a fix every `fix_period_ms` and
// delivers it `fix_age_ms` later, the way a BLE snapshot used to arrive. The
// IMU sees the true longitudinal acceleration on a bike pitched `pitch_deg`
// (nose up positive), plus `accel_offset` along the sensor's forward axis.
struct Ride {
  SpeedFusion fusion;
  // An arbitrary device clock, high enough that nothing starts at zero.
  std::uint64_t t_us = 7'000'000'000ULL;
  float true_mps = 0.0F;
  bool gnss = true;
  std::uint32_t fix_period_ms = 1'000;
  std::uint32_t fix_age_ms = 300;
  float fix_accuracy_mps = 0.3F;
  float accel_offset = 0.0F;
  float pitch_deg = 0.0F;
  std::uint32_t next_fix_ms = 0;
  std::vector<SpeedFix> in_flight;
  int delivered = 0;

  std::uint32_t now_ms() const {
    return static_cast<std::uint32_t>(t_us / 1'000U);
  }

  void run(float accel_mps2, float seconds) {
    const auto steps = static_cast<int>(seconds * 1.0e6F / kStepUs);
    const float dt = static_cast<float>(kStepUs) / 1.0e6F;
    for (int i = 0; i < steps; ++i) {
      t_us += kStepUs;
      true_mps = std::max(0.0F, true_mps + accel_mps2 * dt);
      const std::uint32_t now = now_ms();
      if (gnss && static_cast<std::int32_t>(now - next_fix_ms) >= 0) {
        SpeedFix fix;
        fix.mps = true_mps;
        fix.accuracy_mps = fix_accuracy_mps;
        fix.fix_ms = now;
        fix.rx_ms = now + fix_age_ms;
        fix.timestamped = true;
        in_flight.push_back(fix);
        next_fix_ms = now + fix_period_ms;
      }
      // Like the firmware: whatever arrived is applied before this sample.
      for (auto it = in_flight.begin(); it != in_flight.end();) {
        if (static_cast<std::int32_t>(now - it->rx_ms) >= 0) {
          fusion.add_fix(*it);
          ++delivered;
          it = in_flight.erase(it);
        } else {
          ++it;
        }
      }
      const float accel = true_mps > 0.0F ? accel_mps2 : 0.0F;
      const float pitch = pitch_deg * kDegToRad;
      const Vec3f up{std::sin(pitch), 0.0F, std::cos(pitch)};
      // Specific force = g * up + a * (horizontal forward in the body frame).
      const Vec3f accel_body{
          kGravity * std::sin(pitch) + accel * std::cos(pitch) + accel_offset,
          0.0F, kGravity * std::cos(pitch) - accel * std::sin(pitch)};
      fusion.step(accel_body, up, true_mps == 0.0F && accel == 0.0F, dt, now);
    }
  }
};

bool near(float value, float expected, float tolerance) {
  return std::fabs(value - expected) <= tolerance;
}

void test_phone_clock_takes_the_smallest_transit() {
  PhoneClock clock;
  CHECK(!clock.ready());
  clock.observe(1'000, 50'000);   // transit t
  clock.observe(2'000, 51'040);   // t + 40 ms
  clock.observe(3'000, 52'010);   // t + 10 ms
  CHECK(clock.ready());
  CHECK(clock.offset_ms() == 49'000U);
  CHECK(clock.to_device(2'500) == 51'500U);

  // A message that sat 700 ms in the phone's queue does not move the
  // estimate, so its fix is placed 700 ms further in the past than its
  // arrival suggests.
  clock.observe(4'000, 53'700);
  CHECK(clock.offset_ms() == 49'000U);
  CHECK(53'700U - clock.to_device(4'000 - 300) == 1'000U);

  // The window forgets: after the phone clock steps by +100 ms, the old
  // minimum ages out within sixteen messages.
  for (std::uint32_t i = 0; i < 16; ++i) {
    clock.observe(10'000 + i * 1'000, 58'900 + i * 1'000);
  }
  CHECK(clock.offset_ms() == 48'900U);

  // Both counters are low 32 bits; the phone's has usually wrapped.
  PhoneClock wrapped;
  wrapped.observe(0xFFFFFF00U, 1'000U);
  wrapped.observe(0xFFFFFF00U + 1'000U, 2'020U);
  CHECK(wrapped.to_device(0xFFFFFF00U + 500U) == 1'500U);
  wrapped.reset();
  CHECK(!wrapped.ready());
}

void test_no_speed_before_the_first_fix() {
  Ride ride;
  ride.gnss = false;
  ride.run(0.0F, 2.0F);
  CHECK(!ride.fusion.sample(ride.now_ms()).valid);
  CHECK(!ride.fusion.has_fix());
}

// The first fix is trusted outright, but it describes the moment it was
// taken: everything the IMU integrated since then is kept.
void test_first_fix_is_carried_forward_to_now() {
  Ride ride;
  ride.gnss = false;
  ride.true_mps = 8.0F;
  ride.run(2.0F, 1.0F);  // 10 m/s
  SpeedFix fix;
  fix.mps = ride.true_mps;
  fix.accuracy_mps = 0.3F;
  fix.fix_ms = ride.now_ms();
  ride.run(2.0F, 0.5F);  // 11 m/s, and the fix arrives now
  CHECK(ride.fusion.add_fix(fix));
  const SpeedSample sample = ride.fusion.sample(ride.now_ms());
  CHECK(sample.valid);
  CHECK(near(sample.mps, 11.0F, 0.05F));
  CHECK(near(sample.age_s, 0.5F, 0.01F));
}

void test_constant_speed_holds_between_fixes() {
  Ride ride;
  ride.true_mps = 15.0F;
  ride.run(0.0F, 10.0F);
  const SpeedSample sample = ride.fusion.sample(ride.now_ms());
  CHECK(sample.valid);
  CHECK(near(sample.mps, 15.0F, 0.05F));
  // The age is the fix's, which is at least the delivery delay.
  CHECK(sample.age_s >= 0.3F);
  CHECK(sample.age_s < 1.31F);
}

// DESIGN_V2 §14.1 "速度融合延迟": braking hard, a fix taken 600 ms ago says
// a speed the bike no longer has. It must not drag the estimate back up.
void test_late_fix_in_a_hard_stop_does_not_pull_the_speed_up() {
  Ride ride;
  ride.true_mps = 20.0F;
  ride.fix_age_ms = 600;
  ride.run(0.0F, 6.0F);
  CHECK(near(ride.fusion.speed_mps(), 20.0F, 0.05F));

  ride.gnss = false;
  ride.in_flight.clear();
  ride.run(-6.0F, 0.5F);  // 17 m/s
  SpeedFix late;
  late.mps = ride.true_mps;
  late.accuracy_mps = 0.3F;
  late.fix_ms = ride.now_ms();
  ride.run(-6.0F, 0.6F);  // 13.4 m/s when it arrives
  CHECK(ride.fusion.add_fix(late));
  CHECK(near(ride.fusion.speed_mps(), ride.true_mps, 0.15F));

  // A fix that disagrees with the trajectory *at its own time* still
  // corrects: 1 m/s below what the bike did then moves the present down, not
  // up towards the stale value.
  SpeedFix disagreeing;
  disagreeing.accuracy_mps = 0.3F;
  disagreeing.fix_ms = ride.now_ms();
  ride.run(-6.0F, 0.2F);
  disagreeing.mps = ride.fusion.speed_mps() + 6.0F * 0.2F - 1.0F;
  const float before = ride.fusion.speed_mps();
  CHECK(ride.fusion.add_fix(disagreeing));
  CHECK(ride.fusion.speed_mps() < before - 0.4F);
  CHECK(ride.fusion.speed_mps() > before - 1.0F);
}

// GNSS drops out in a stop: the IMU carries the speed down, the sample stays
// valid with an honest age, and expires 5 s after the last fix was taken.
void test_braking_through_a_gnss_dropout() {
  Ride ride;
  ride.true_mps = 20.0F;
  ride.run(0.0F, 5.0F);
  ride.gnss = false;
  ride.in_flight.clear();
  const std::uint32_t last_fix = ride.fusion.last_fix_ms();
  ride.run(-5.0F, 2.0F);  // 10 m/s
  SpeedSample sample = ride.fusion.sample(ride.now_ms());
  CHECK(sample.valid);
  CHECK(near(sample.mps, 10.0F, 0.2F));
  CHECK(near(sample.age_s,
             static_cast<float>(ride.now_ms() - last_fix) / 1'000.0F, 0.01F));
  CHECK(sample.age_s > 2.0F);

  ride.run(0.0F, 5.0F - sample.age_s - 0.1F);
  CHECK(ride.fusion.sample(ride.now_ms()).valid);
  ride.run(0.0F, 0.2F);
  CHECK(!ride.fusion.sample(ride.now_ms()).valid);
}

// A pitched mount (or a hill) puts g*sin(pitch) on the forward axis. The
// horizontal projection removes it, so only the real acceleration counts.
void test_pitch_does_not_become_acceleration() {
  Ride ride;
  ride.pitch_deg = 8.0F;
  ride.true_mps = 10.0F;
  ride.run(0.0F, 3.0F);
  ride.gnss = false;
  ride.in_flight.clear();
  ride.run(2.0F, 2.0F);  // 14 m/s
  CHECK(near(ride.fusion.speed_mps(), 14.0F, 0.2F));
  ride.run(0.0F, 2.0F);
  CHECK(near(ride.fusion.speed_mps(), 14.0F, 0.2F));
}

// An offset along forward that the Lean state does not explain (a pitch
// error, the accelerometer's own offset) leaves the same residual at every
// fix. bias_lon learns it, so a later dropout drifts far less.
void test_a_constant_offset_is_learned_as_bias() {
  Ride ride;
  ride.accel_offset = 0.4F;
  ride.fix_age_ms = 200;
  ride.true_mps = 15.0F;
  ride.run(0.0F, 120.0F);
  CHECK(near(ride.fusion.bias_mps2(), 0.4F, 0.08F));

  ride.gnss = false;
  ride.in_flight.clear();
  ride.run(0.0F, 3.0F);
  // Unlearned, 0.4 m/s^2 for 3 s would be 1.2 m/s.
  CHECK(near(ride.fusion.speed_mps(), 15.0F, 0.35F));
}

// Parked, the speed is zero whatever the accelerometer says, and a fix of
// zero agrees with it.
void test_parked_speed_is_zero() {
  Ride ride;
  ride.accel_offset = 0.3F;
  ride.true_mps = 0.0F;
  ride.run(0.0F, 4.0F);
  const SpeedSample sample = ride.fusion.sample(ride.now_ms());
  CHECK(sample.valid);
  CHECK(sample.mps == 0.0F);
  CHECK(ride.fusion.speed_mps() == 0.0F);
}

void test_reordered_old_and_broken_fixes_are_refused() {
  Ride ride;
  ride.true_mps = 12.0F;
  ride.run(0.0F, 4.0F);
  ride.gnss = false;
  ride.in_flight.clear();
  const std::uint32_t newest = ride.fusion.last_fix_ms();

  SpeedFix reordered;
  reordered.mps = 30.0F;
  reordered.fix_ms = newest - 10;
  CHECK(!ride.fusion.add_fix(reordered));
  reordered.fix_ms = newest;
  CHECK(!ride.fusion.add_fix(reordered));

  // Older than the recorded trajectory: nothing to compare it with.
  ride.run(0.0F, 4.0F);
  SpeedFix ancient;
  ancient.mps = 30.0F;
  ancient.fix_ms = ride.now_ms() - 3'500;
  CHECK(!ride.fusion.add_fix(ancient));

  SpeedFix broken;
  broken.mps = std::nanf("");
  broken.fix_ms = ride.now_ms();
  CHECK(!ride.fusion.add_fix(broken));
  broken.mps = -1.0F;
  CHECK(!ride.fusion.add_fix(broken));
  CHECK(ride.fusion.rejected_fixes() == 5U);
  CHECK(near(ride.fusion.speed_mps(), 12.0F, 0.05F));

  // A clock estimate a few ms ahead can place a fix in the future; it is
  // taken as "now", not refused.
  SpeedFix early;
  early.mps = 12.0F;
  early.accuracy_mps = 0.3F;
  early.fix_ms = ride.now_ms() + 20;
  CHECK(ride.fusion.add_fix(early));
  CHECK(near(ride.fusion.sample(ride.now_ms()).age_s, 0.0F, 1.0e-6F));
}

}  // namespace

int main() {
  test_phone_clock_takes_the_smallest_transit();
  test_no_speed_before_the_first_fix();
  test_first_fix_is_carried_forward_to_now();
  test_constant_speed_holds_between_fixes();
  test_late_fix_in_a_hard_stop_does_not_pull_the_speed_up();
  test_braking_through_a_gnss_dropout();
  test_pitch_does_not_become_acceleration();
  test_a_constant_offset_is_learned_as_bias();
  test_parked_speed_is_zero();
  test_reordered_old_and_broken_fixes_are_refused();
  if (failures != 0) {
    std::cerr << failures << " speed fusion checks failed\n";
    return EXIT_FAILURE;
  }
  std::cout << "All speed fusion checks passed\n";
  return EXIT_SUCCESS;
}
