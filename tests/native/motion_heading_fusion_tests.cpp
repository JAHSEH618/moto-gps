#include "motion_heading_fusion.hpp"
#include "motion_heading_axis.hpp"

#include <cmath>
#include <cstdlib>
#include <iostream>

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

void feed_rate(MotionHeadingFusion& fusion, float rate_dps,
               std::uint64_t start_ms, std::uint64_t end_ms) {
  for (std::uint64_t time = start_ms; time <= end_ms; time += 8) {
    fusion.integrate(rate_dps, time);
  }
}

void test_fast_gyro_turn_and_wrap() {
  MotionHeadingFusion fusion;
  fusion.anchor(350.0F, 8.0F, true);
  feed_rate(fusion, 90.0F, 8, 248);
  CHECK(fusion.heading_deg() > 5.0F);
  CHECK(fusion.heading_deg() < 20.0F);
}

void test_stationary_phone_course_does_not_drag_handlebar_heading() {
  MotionHeadingFusion fusion;
  fusion.anchor(12.0F, 0.0F, true);
  fusion.anchor(210.0F, 0.2F, true);
  CHECK(std::abs(fusion.heading_deg() - 12.0F) < 0.01F);
}

void test_moving_phone_course_corrects_gyro_drift_progressively() {
  MotionHeadingFusion fusion;
  fusion.anchor(10.0F, 8.0F, true);
  fusion.anchor(110.0F, 10.0F, true);
  CHECK(fusion.heading_deg() > 70.0F);
  CHECK(fusion.heading_deg() < 80.0F);
}

void test_first_phone_course_replaces_gyro_local_zero() {
  MotionHeadingFusion fusion;
  feed_rate(fusion, 90.0F, 8, 248);
  fusion.anchor(235.0F, 0.0F, true);
  CHECK(std::abs(fusion.heading_deg() - 235.0F) < 0.01F);
  // The first moving fix must also replace an old stationary heading in one
  // update instead of leaving the selected road behind the fixed rider arrow.
  fusion.anchor(90.0F, 8.0F, true);
  CHECK(std::abs(fusion.heading_deg() - 90.0F) < 0.01F);
  fusion.reset();
  feed_rate(fusion, -90.0F, 8, 248);
  fusion.anchor(180.0F, 12.0F, true);
  CHECK(std::abs(fusion.heading_deg() - 180.0F) < 0.01F);
}

void test_unusable_course_never_anchors() {
  MotionHeadingFusion fusion;
  fusion.anchor(90.0F, 12.0F, false);
  CHECK(!fusion.initialized());
  fusion.anchor(12.0F, 12.0F, true);
  fusion.anchor(180.0F, 12.0F, false);
  CHECK(std::abs(fusion.heading_deg() - 12.0F) < 0.01F);
}

void test_forward_facing_mount_yaw_does_not_reverse_at_zero_z() {
  const float front_tilt = clockwise_gravity_heading_rate(
      0.0F, 9.81F, 0.01F, 0.0F, -90.0F, 0.0F);
  const float back_tilt = clockwise_gravity_heading_rate(
      0.0F, 9.81F, -0.01F, 0.0F, -90.0F, 0.0F);
  CHECK(std::abs(front_tilt - 90.0F) < 0.01F);
  CHECK(std::abs(front_tilt - back_tilt) < 0.01F);
  // Rotating the mounting orientation leaves compass clockwise positive.
  CHECK(std::abs(clockwise_gravity_heading_rate(
      0.0F, 0.0F, 9.81F, 0.0F, 0.0F, -90.0F) - 90.0F) < 0.01F);
  CHECK(std::abs(clockwise_gravity_heading_rate(
      0.0F, 0.0F, -9.81F, 0.0F, 0.0F, 90.0F) - 90.0F) < 0.01F);
}

void test_large_sensor_gap_is_not_integrated() {
  MotionHeadingFusion fusion;
  fusion.anchor(42.0F, 5.0F, true);
  fusion.integrate(180.0F, 10);
  CHECK(!fusion.integrate(180.0F, 1'000));
  CHECK(std::abs(fusion.heading_deg() - 42.0F) < 0.01F);
}

float heading_with_optional_stale_sample(bool include_stale_sample) {
  MotionHeadingFusion fusion;
  fusion.anchor(90.0F, 8.0F, true);
  for (std::uint64_t time = 8; time <= 248; time += 8) {
    fusion.integrate(90.0F, time);
    if (include_stale_sample && time == 120) {
      fusion.integrate(90.0F, 116);
    }
  }
  return fusion.heading_deg();
}

void test_stale_timestamp_does_not_rewind_integration_clock() {
  const float clean = heading_with_optional_stale_sample(false);
  const float with_stale_sample = heading_with_optional_stale_sample(true);
  CHECK(std::abs(clean - with_stale_sample) < 0.01F);
}

void test_stationary_freeze_holds_heading_until_riding_resumes() {
  MotionHeadingFusion fusion;
  fusion.anchor(30.0F, 8.0F, true);
  // Parking: low-speed fixes engage the stationary freeze with hysteresis.
  fusion.anchor(30.0F, 0.4F, true);
  feed_rate(fusion, 90.0F, 8, 2'008);
  CHECK(std::abs(fusion.heading_deg() - 30.0F) < 0.01F);
  // Between 0.6 and 1.5 m/s the freeze stays engaged (hysteresis band).
  fusion.anchor(30.0F, 1.0F, true);
  feed_rate(fusion, 90.0F, 2'016, 4'016);
  CHECK(std::abs(fusion.heading_deg() - 30.0F) < 0.01F);
  // Pulling away releases the freeze and integration works again.
  fusion.anchor(30.0F, 6.0F, true);
  feed_rate(fusion, 90.0F, 4'024, 4'424);
  CHECK(fusion.heading_deg() > 45.0F);
  CHECK(fusion.heading_deg() < 80.0F);
  // reset() must also clear the freeze.
  fusion.reset();
  feed_rate(fusion, 90.0F, 8, 248);
  CHECK(fusion.heading_deg() > 5.0F);
}

// The rider's report: the phone said due east, the handlebar said 60 degrees
// north-east. Parked, the only heading the phone had was the last GNSS
// course, which the fusion rightly ignores at standstill, so the handlebar
// kept a stale reference. The phone's compass is the parked reference.
void test_parked_compass_replaces_a_stale_course() {
  MotionHeadingFusion fusion;
  fusion.anchor(60.0F, 8.0F, true);   // last course while moving
  fusion.anchor(60.0F, 0.0F, true);   // parked: the course is now stale
  for (int i = 0; i < 4; ++i) fusion.anchor_compass(90.0F, 0.0F);
  CHECK(std::abs(fusion.heading_deg() - 90.0F) < 2.0F);
}

void test_first_compass_reading_sets_the_reference() {
  MotionHeadingFusion fusion;
  feed_rate(fusion, 90.0F, 8, 248);  // gyro-only local zero, not absolute
  fusion.anchor_compass(271.0F, 0.0F);
  CHECK(std::abs(fusion.heading_deg() - 271.0F) < 0.01F);
}

void test_compass_turns_the_short_way_through_north() {
  MotionHeadingFusion fusion;
  fusion.anchor_compass(350.0F, 0.0F);
  fusion.anchor_compass(10.0F, 0.0F);
  const float heading = fusion.heading_deg();
  CHECK(heading > 350.0F || heading < 10.0F);
}

void test_moving_course_takes_over_from_the_compass() {
  MotionHeadingFusion fusion;
  fusion.anchor_compass(90.0F, 0.0F);
  // Pulling away: the course replaces the parked compass in one update, as
  // it replaces any stationary placeholder.
  fusion.anchor(200.0F, 8.0F, true);
  CHECK(std::abs(fusion.heading_deg() - 200.0F) < 0.01F);
}

}  // namespace

int main() {
  test_fast_gyro_turn_and_wrap();
  test_stationary_phone_course_does_not_drag_handlebar_heading();
  test_moving_phone_course_corrects_gyro_drift_progressively();
  test_first_phone_course_replaces_gyro_local_zero();
  test_unusable_course_never_anchors();
  test_forward_facing_mount_yaw_does_not_reverse_at_zero_z();
  test_large_sensor_gap_is_not_integrated();
  test_stale_timestamp_does_not_rewind_integration_clock();
  test_stationary_freeze_holds_heading_until_riding_resumes();
  test_parked_compass_replaces_a_stale_course();
  test_first_compass_reading_sets_the_reference();
  test_compass_turns_the_short_way_through_north();
  test_moving_course_takes_over_from_the_compass();

  if (failures != 0) {
    std::cerr << failures << " motion heading checks failed\n";
    return EXIT_FAILURE;
  }
  std::cout << "All motion heading checks passed\n";
  return EXIT_SUCCESS;
}
