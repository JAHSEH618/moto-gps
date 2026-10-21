// Deterministic host-side rendering tests for the LVGL navigation UI.
// A headless LVGL display renders the real moto_nav_ui into an RGB565
// framebuffer; frames are hashed to verify bit-exact determinism, clean page
// transitions and label/geometry residue cleanup.

#include "moto_nav_ui.h"

#include <lvgl.h>

// Pointer indev shipped with LVGL and compiled in when LV_USE_TEST is set.
// It gives the suite a real gesture: LV_EVENT_GESTURE cannot be synthesised
// with lv_obj_send_event, because gesture_event() reads the direction from
// the active indev.
#include "src/debugging/test/lv_test_indev.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

namespace {

int failures = 0;

#define CHECK(condition)                                                   \
  do {                                                                     \
    if (!(condition)) {                                                    \
      std::cerr << __FILE__ << ':' << __LINE__                             \
                << " CHECK failed: " #condition << '\n';                   \
      ++failures;                                                          \
    }                                                                      \
  } while (false)

constexpr int kWidth = MOTO_UI_CANVAS_WIDTH;
constexpr int kHeight = MOTO_UI_CANVAS_HEIGHT;
constexpr std::size_t kFrameBytes = static_cast<std::size_t>(kWidth) *
                                    static_cast<std::size_t>(kHeight) *
                                (LV_COLOR_DEPTH / 8);

std::vector<std::uint8_t> shadow_frame;
std::uint64_t flush_count = 0;

void flush_cb(lv_display_t* display, const lv_area_t* area,
              std::uint8_t* px_map) {
  // Partial rendering: stitch each flushed area into the shadow frame so a
  // complete deterministic frame is always available for hashing.
  const int bytes_per_pixel = LV_COLOR_DEPTH / 8;
  for (int y = area->y1; y <= area->y2; ++y) {
    const std::size_t row_bytes =
        static_cast<std::size_t>(area->x2 - area->x1 + 1) * bytes_per_pixel;
    const std::size_t src =
        static_cast<std::size_t>(y - area->y1) *
        static_cast<std::size_t>(area->x2 - area->x1 + 1) * bytes_per_pixel;
    const std::size_t dst =
        (static_cast<std::size_t>(y) * kWidth +
         static_cast<std::size_t>(area->x1)) * bytes_per_pixel;
    std::memcpy(shadow_frame.data() + dst, px_map + src, row_bytes);
  }
  ++flush_count;
  lv_display_flush_ready(display);
}

void init_display(int buffer_rows, int extra_pool_bytes) {
  lv_init();
  static std::vector<std::uint32_t> extra_pool;
  if(extra_pool_bytes > 0) {
    extra_pool.resize((static_cast<std::size_t>(extra_pool_bytes) + 3U) / 4U);
    const bool added = lv_mem_add_pool(extra_pool.data(), extra_pool.size() * 4U) != nullptr;
    CHECK(added);
    if(!added) std::exit(EXIT_FAILURE);
  }
  shadow_frame.assign(kFrameBytes, 0);
  // Exercise both small partial strips and the firmware's map-sized buffer.
  static std::vector<std::uint8_t> draw_buffer(
      static_cast<std::size_t>(kWidth) * buffer_rows * (LV_COLOR_DEPTH / 8));
  lv_display_t* display = lv_display_create(kWidth, kHeight);
  lv_display_set_flush_cb(display, flush_cb);
  lv_display_set_buffers(
      display, draw_buffer.data(), nullptr, draw_buffer.size(),
      LV_DISPLAY_RENDER_MODE_PARTIAL);
}

void pump(int cycles = 40) {
  for (int i = 0; i < cycles; ++i) {
    lv_tick_inc(5);
    lv_timer_handler();
  }
}

std::uint64_t frame_hash() {
  std::uint64_t hash = 1'469'598'103'934'665'603ULL;
  for (const std::uint8_t byte : shadow_frame) {
    hash ^= byte;
    hash *= 1'099'511'628'211ULL;
  }
  return hash;
}

struct Frame {
  std::uint64_t hash;
  std::vector<std::uint8_t> bytes;
};

Frame capture() {
  pump();
  return Frame{frame_hash(), shadow_frame};
}

std::uint16_t rgb565(std::uint8_t r, std::uint8_t g, std::uint8_t b) {
  return static_cast<std::uint16_t>(((r & 0xF8U) << 8U) |
                                    ((g & 0xFCU) << 3U) |
                                    (b >> 3U));
}

std::uint16_t pixel_at(int x, int y) {
  const std::size_t offset =
      (static_cast<std::size_t>(y) * kWidth + static_cast<std::size_t>(x)) *
      (LV_COLOR_DEPTH / 8);
  std::uint16_t pixel = 0;
  std::memcpy(&pixel, shadow_frame.data() + offset, sizeof(pixel));
  return pixel;
}

// A single pixel on a diagonal arc can land on an antialiased edge, so the
// diagonal samples look at the pixel's immediate neighbourhood. The axial
// samples stay exact: those pixels sit on the stroke centre line.
bool has_color_within(int x, int y, std::uint16_t color, int radius = 2) {
  for (int dy = -radius; dy <= radius; ++dy) {
    for (int dx = -radius; dx <= radius; ++dx) {
      if (pixel_at(x + dx, y + dy) == color) return true;
    }
  }
  return false;
}

moto_ui_state_t base_state() {
  moto_ui_state_t state{};
  state.page = MOTO_UI_PAGE_NAVIGATION;
  state.mode = MOTO_UI_NAVIGATING;
  state.maneuver = MOTO_MANEUVER_RIGHT;
  state.traffic = MOTO_TRAFFIC_SLOW;
  state.distance_to_maneuver_m = 120;
  state.remaining_distance_m = 1'500;
  state.remaining_time_s = 200;
  state.route_progress_percent = 40;
  state.gps_accuracy_m = 5;
  state.online = 1;
  state.has_destination = 1;
  state.route_identity = 77;
  state.route_generation = 1;
  state.map_scene_revision = 4;
  state.speed_kph = 48;
  state.speed_limit_kph = 50;
  state.heading_deg = 90;
  state.road_name = "经十路";
  state.next_road_name = "青年大街";
  return state;
}

void fill_route(moto_ui_state_t& state, int count) {
  count = count < 0 ? MOTO_UI_ROUTE_POINT_CAPACITY : count;
  state.route_point_count = static_cast<std::uint8_t>(count);
  for (int i = 0; i < count; ++i) {
    state.route_points[i] = {static_cast<std::int16_t>(40 + i * 16),
                             static_cast<std::int16_t>(233 + (i % 2) * 40)};
  }
}

void fill_streets(moto_ui_state_t& state) {
  state.road_point_count = 8;
  for (int i = 0; i < 8; ++i) {
    state.road_points[i] = {static_cast<std::int16_t>(10 + i * 55),
                            static_cast<std::int16_t>(30 + i * 60)};
  }
  state.road_polyline_count = 1;
  state.road_polylines[0] = {0, 8, 1};
  state.building_point_count = 6;
  for (int i = 0; i < 6; ++i) {
    state.building_points[i] = {
        static_cast<std::int16_t>(300 + (i % 3) * 40),
        static_cast<std::int16_t>(300 + (i / 3) * 40)};
  }
  state.building_footprint_count = 1;
  state.building_footprints[0] = {0, 6, 1};
}

void test_frames_are_deterministic() {
  // Identical UI state must produce a bit-identical frame every time.
  moto_nav_ui_create();
  moto_nav_ui_set_reduce_motion(1);
  moto_nav_ui_set_phone_connection(MOTO_UI_PHONE_ONLINE);

  moto_ui_state_t state = base_state();
  fill_route(state, 12);
  fill_streets(state);
  moto_nav_ui_set_state(&state);
  const Frame first = capture();

  moto_nav_ui_set_state(&state);
  const Frame second = capture();
  CHECK(first.hash == second.hash);
  CHECK(first.bytes == second.bytes);

  // Perturb the UI with an unrelated page, then come back: the restored
  // navigation frame must be identical to the original (no residue).
  moto_nav_ui_set_page(MOTO_UI_PAGE_SPEED);
  const Frame speed_page = capture();
  moto_nav_ui_set_page(MOTO_UI_PAGE_NAVIGATION);
  const Frame restored = capture();
  CHECK(restored.hash == first.hash);

  // The speed page must actually render different pixels.
  CHECK(speed_page.hash != first.hash);

  // Determinism holds across repeated renders of the same page as well.
  moto_nav_ui_set_page(MOTO_UI_PAGE_SPEED);
  const Frame speed_again = capture();
  CHECK(speed_again.hash == speed_page.hash);
}

void test_rebuilding_the_ui_is_idempotent() {
  moto_nav_ui_set_reduce_motion(1);
  moto_nav_ui_set_phone_connection(MOTO_UI_PHONE_ONLINE);
  moto_ui_state_t state = base_state();
  fill_route(state, 12);
  fill_streets(state);
  moto_nav_ui_set_state(&state);
  // The rebuild resets the link to offline, so going online again raises the
  // 920 ms CONNECTED confirmation. Let it expire before sampling: it is a
  // correct overlay, not residue, and framing a test around it would hide the
  // next regression behind a full-screen panel.
  pump(240);
  const Frame before = capture();

  // A second create() must retire the previous timers and rebuild the same
  // frame from the same state. A tree with duplicate screen callbacks or a
  // timer still holding deleted widgets shows up here.
  moto_nav_ui_create();
  moto_nav_ui_set_reduce_motion(1);
  moto_nav_ui_set_phone_connection(MOTO_UI_PHONE_ONLINE);
  moto_nav_ui_set_state(&state);
  pump(240);
  const Frame after = capture();
  CHECK(before.hash == after.hash);
}

void test_a_building_footprint_interior_is_filled() {
  moto_nav_ui_set_reduce_motion(1);
  moto_nav_ui_set_phone_connection(MOTO_UI_PHONE_ONLINE);
  moto_ui_state_t state = base_state();
  // Keep a route so the connection lifecycle overlay stays hidden; the
  // route is drawn below the building and never crosses its interior.
  state.route_point_count = 2;
  state.route_points[0] = {40, 280};
  state.route_points[1] = {60, 280};
  state.road_point_count = 0;
  state.road_polyline_count = 0;
  state.building_point_count = 4;
  state.building_points[0] = {100, 100};
  state.building_points[1] = {200, 100};
  state.building_points[2] = {200, 200};
  state.building_points[3] = {100, 200};
  state.building_footprint_count = 1;
  state.building_footprints[0] = {0, 4, 0};
  moto_nav_ui_set_state(&state);
  capture();

  // The footprint interior must be a filled surface, not just an outline.
  CHECK(pixel_at(150, 150) == rgb565(0x24, 0x29, 0x2C));
  // The old fan-spoke seams must be covered as well, not just the centre.
  CHECK(pixel_at(125, 125) == rgb565(0x24, 0x29, 0x2C));
  CHECK(pixel_at(175, 175) == rgb565(0x24, 0x29, 0x2C));
  // The rest of the map must remain the page background.
  CHECK(pixel_at(300, 150) == rgb565(0x05, 0x06, 0x07));
}

void test_the_map_covers_the_bottom_of_the_round_canvas() {
  moto_nav_ui_set_reduce_motion(1);
  moto_nav_ui_set_phone_connection(MOTO_UI_PHONE_ONLINE);
  moto_ui_state_t state = base_state();
  // A route keeps the lifecycle overlay hidden; the route itself sits at the
  // bottom edge, away from the sampled building.
  state.route_point_count = 2;
  state.route_points[0] = {40, 440};
  state.route_points[1] = {60, 440};
  state.road_point_count = 0;
  state.road_polyline_count = 0;
  state.building_point_count = 4;
  state.building_points[0] = {80, 340};
  state.building_points[1] = {180, 340};
  state.building_points[2] = {180, 420};
  state.building_points[3] = {80, 420};
  state.building_footprint_count = 1;
  state.building_footprints[0] = {0, 4, 0};
  moto_nav_ui_set_state(&state);
  capture();

  // The old 232-design-pixel map strip ended around y=300; this row must be
  // inside the full-screen Radar, not the page background.
  CHECK(pixel_at(120, 380) == rgb565(0x24, 0x29, 0x2C));
}

void test_the_ring_carries_progress_and_a_north_badge() {
  moto_nav_ui_set_reduce_motion(1);
  moto_nav_ui_set_phone_connection(MOTO_UI_PHONE_ONLINE);
  moto_ui_state_t state = base_state();
  fill_route(state, 8);
  // Heading east: north is to the left, so the badge must leave the top of the
  // ring. A badge pinned to twelve o'clock would be wrong half the time.
  state.heading_deg = 90;
  state.route_progress_percent = 0;
  state.traffic = MOTO_TRAFFIC_SLOW;
  moto_nav_ui_set_state(&state);
  pump();

  lv_obj_t *north = lv_obj_find_by_name(lv_screen_active(), "moto-compass-north");
  CHECK(north != nullptr);
  if (north == nullptr) return;
  CHECK(!lv_obj_has_flag(north, LV_OBJ_FLAG_HIDDEN));
  const int32_t centre_x = kWidth / 2;
  const int32_t centre_y = kHeight / 2;
  const int32_t north_x = lv_obj_get_x(north) + lv_obj_get_width(north) / 2;
  const int32_t north_y = lv_obj_get_y(north) + lv_obj_get_height(north) / 2;
  CHECK(north_x < centre_x - 120);
  CHECK(std::abs(north_y - centre_y) <= 2);

  // With no progress the rim is the quiet track colour at twelve o'clock.
  const Frame idle = capture();
  CHECK(pixel_at(centre_x, centre_y - MOTO_UI_RADAR_RING_RADIUS_PX) ==
        rgb565(0x24, 0x29, 0x2C));

  // 25% needs its own sample: at twelve o'clock a clockwise and a
  // counter-clockwise arc paint the same pixel, so the top of the ring cannot
  // tell the two directions apart. The clockwise quadrant is the upper right.
  state.route_progress_percent = 25;
  moto_nav_ui_set_state(&state);
  pump();
  const int32_t diagonal = static_cast<int32_t>(
      std::lround(MOTO_UI_RADAR_RING_RADIUS_PX / std::sqrt(2.0)));
  CHECK(has_color_within(centre_x + diagonal, centre_y - diagonal,
                         rgb565(0xE6, 0xC8, 0x4F)));
  CHECK(!has_color_within(centre_x - diagonal, centre_y - diagonal,
                          rgb565(0xE6, 0xC8, 0x4F)));

  // Progress starts at twelve o'clock and is drawn in the traffic colour, the
  // only place this screen reports traffic.
  state.route_progress_percent = 50;
  moto_nav_ui_set_state(&state);
  pump();
  const Frame half = capture();
  CHECK(half.hash != idle.hash);
  CHECK(pixel_at(centre_x, centre_y - MOTO_UI_RADAR_RING_RADIUS_PX) ==
        rgb565(0xE6, 0xC8, 0x4F));

  // A full ring leaves no track colour at the top.
  state.route_progress_percent = 100;
  moto_nav_ui_set_state(&state);
  pump();
  CHECK(pixel_at(centre_x, centre_y - MOTO_UI_RADAR_RING_RADIUS_PX) ==
        rgb565(0xE6, 0xC8, 0x4F));

  // The badge turns with the rider: heading north puts it on top.
  state.heading_deg = 0;
  moto_nav_ui_set_state(&state);
  pump();
  CHECK(lv_obj_get_y(north) + lv_obj_get_height(north) / 2 < centre_y - 120);
  CHECK(std::abs(lv_obj_get_x(north) + lv_obj_get_width(north) / 2 -
                 centre_x) <= 2);

  // South and west catch a sign error that the two axis-aligned checks above
  // cannot: heading 180 must put the badge at the bottom, 270 at the right.
  state.heading_deg = 180;
  moto_nav_ui_set_state(&state);
  pump();
  CHECK(lv_obj_get_y(north) + lv_obj_get_height(north) / 2 > centre_y + 120);
  CHECK(std::abs(lv_obj_get_x(north) + lv_obj_get_width(north) / 2 -
                 centre_x) <= 2);

  state.heading_deg = 270;
  moto_nav_ui_set_state(&state);
  pump();
  CHECK(lv_obj_get_x(north) + lv_obj_get_width(north) / 2 > centre_x + 120);
  CHECK(std::abs(lv_obj_get_y(north) + lv_obj_get_height(north) / 2 -
                 centre_y) <= 2);
}

void test_a_refused_home_tap_shows_a_hint_and_fades() {
  moto_nav_ui_set_reduce_motion(1);
  moto_nav_ui_set_phone_connection(MOTO_UI_PHONE_ONLINE);
  moto_ui_state_t state = base_state();
  fill_route(state, 8);
  moto_nav_ui_set_state(&state);
  pump();

  lv_obj_t *hint = lv_obj_find_by_name(lv_screen_active(), "moto-home-hint");
  CHECK(hint != nullptr);
  if (hint == nullptr) return;
  CHECK(lv_obj_has_flag(hint, LV_OBJ_FLAG_HIDDEN));

  moto_nav_ui_show_home_error();
  const Frame shown = capture();
  CHECK(!lv_obj_has_flag(hint, LV_OBJ_FLAG_HIDDEN));

  // Three seconds, then it goes away on its own. A hint that needs a tap to
  // dismiss would be another control on a screen that must stay readable.
  pump(650);
  CHECK(lv_obj_has_flag(hint, LV_OBJ_FLAG_HIDDEN));
  CHECK(shown.hash != capture().hash);

  // The same hint can be shown again after it faded.
  moto_nav_ui_show_home_error();
  CHECK(!lv_obj_has_flag(hint, LV_OBJ_FLAG_HIDDEN));

  // Leave the shared UI in a settled state: the remaining tests compare exact
  // frames, and a hint that expires mid-capture would look like residue.
  pump(650);
  CHECK(lv_obj_has_flag(hint, LV_OBJ_FLAG_HIDDEN));
}

void test_day_and_night_themes_render_different_map_colors() {
  moto_nav_ui_set_reduce_motion(1);
  moto_nav_ui_set_phone_connection(MOTO_UI_PHONE_ONLINE);
  moto_ui_state_t state = base_state();
  state.route_point_count = 2;
  state.route_points[0] = {40, 440};
  state.route_points[1] = {60, 440};
  state.road_point_count = 0;
  state.road_polyline_count = 0;
  state.building_point_count = 4;
  state.building_points[0] = {100, 100};
  state.building_points[1] = {200, 100};
  state.building_points[2] = {200, 200};
  state.building_points[3] = {100, 200};
  state.building_footprint_count = 1;
  state.building_footprints[0] = {0, 4, 0};

  moto_nav_ui_set_theme(MOTO_UI_THEME_GTA_DAY);
  moto_nav_ui_set_state(&state);
  capture();
  // GTA day: building #4A2A32 on land #1A1F1A.
  CHECK(pixel_at(150, 150) == rgb565(0x4A, 0x2A, 0x32));
  CHECK(pixel_at(300, 150) == rgb565(0x1A, 0x1F, 0x1A));

  moto_nav_ui_set_theme(MOTO_UI_THEME_GTA_NIGHT);
  capture();
  // GTA night: building #1A2230 on the true-black night land.
  CHECK(pixel_at(150, 150) == rgb565(0x1A, 0x22, 0x30));
  CHECK(pixel_at(300, 150) == rgb565(0x05, 0x06, 0x07));

  moto_nav_ui_set_theme(MOTO_UI_THEME_OFFICIAL);
}

void test_auto_theme_follows_the_ambient_night_flag() {
  moto_nav_ui_set_reduce_motion(1);
  moto_nav_ui_set_phone_connection(MOTO_UI_PHONE_ONLINE);
  moto_ui_state_t state = base_state();
  state.route_point_count = 2;
  state.route_points[0] = {40, 440};
  state.route_points[1] = {60, 440};
  state.road_point_count = 0;
  state.road_polyline_count = 0;
  state.building_point_count = 4;
  state.building_points[0] = {100, 100};
  state.building_points[1] = {200, 100};
  state.building_points[2] = {200, 200};
  state.building_points[3] = {100, 200};
  state.building_footprint_count = 1;
  state.building_footprints[0] = {0, 4, 0};
  moto_nav_ui_set_state(&state);
  capture();

  // AUTO is resolved on the device from the phone's day/night flag, so the
  // palette has to follow that flag without another theme message.
  moto_nav_ui_set_theme(MOTO_UI_THEME_AUTO);
  moto_nav_ui_set_ambient_night(1);
  capture();
  CHECK(pixel_at(150, 150) == rgb565(0x1A, 0x22, 0x30));
  CHECK(pixel_at(300, 150) == rgb565(0x05, 0x06, 0x07));
  const Frame night = capture();

  // The flag is expected to repeat; a repeat must not restyle or repaint.
  moto_nav_ui_set_ambient_night(1);
  CHECK(night.hash == capture().hash);

  moto_nav_ui_set_ambient_night(0);
  capture();
  CHECK(pixel_at(150, 150) == rgb565(0x4A, 0x2A, 0x32));
  CHECK(pixel_at(300, 150) == rgb565(0x1A, 0x1F, 0x1A));

  // An explicit theme is authoritative: the flag is stored but not applied.
  moto_nav_ui_set_theme(MOTO_UI_THEME_GTA_NIGHT);
  const Frame explicit_night = capture();
  moto_nav_ui_set_ambient_night(1);
  CHECK(explicit_night.hash == capture().hash);

  moto_nav_ui_set_theme(MOTO_UI_THEME_OFFICIAL);
  moto_nav_ui_set_ambient_night(0);
}

void test_performance_and_lean_pages_render_attitude() {
  moto_nav_ui_set_reduce_motion(1);
  moto_nav_ui_set_phone_connection(MOTO_UI_PHONE_ONLINE);
  moto_ui_state_t state = base_state();
  state.has_attitude = 1;
  state.lean_deg = 30.0F;
  state.longitudinal_g = 0.4F;
  state.lateral_g = 0.6F;
  state.page = MOTO_UI_PAGE_LEAN;
  moto_nav_ui_set_state(&state);
  const Frame lean_right = capture();
  CHECK(moto_nav_ui_get_page() == MOTO_UI_PAGE_LEAN);

  state.lean_deg = -20.0F;
  moto_nav_ui_set_state(&state);
  const Frame lean_left = capture();
  CHECK(lean_right.hash != lean_left.hash);

  state.page = MOTO_UI_PAGE_PERFORMANCE;
  moto_nav_ui_set_state(&state);
  const Frame perf = capture();
  CHECK(perf.hash != lean_left.hash);

  state.lateral_g = -1.2F;
  moto_nav_ui_set_state(&state);
  const Frame perf_changed = capture();
  CHECK(perf.hash != perf_changed.hash);

  moto_nav_ui_set_page(MOTO_UI_PAGE_NAVIGATION);
}

void test_clearing_geometry_leaves_no_residue() {
  moto_ui_state_t with_route = base_state();
  fill_route(with_route, 18);
  fill_streets(with_route);
  moto_nav_ui_set_state(&with_route);
  const Frame route_frame = capture();

  moto_ui_state_t without_route = with_route;
  without_route.route_point_count = 0;
  without_route.road_point_count = 0;
  without_route.road_polyline_count = 0;
  without_route.building_point_count = 0;
  without_route.building_footprint_count = 0;
  moto_nav_ui_set_state(&without_route);
  const Frame cleared_frame = capture();
  CHECK(route_frame.hash != cleared_frame.hash);

  // Re-drawing the geometry must restore the exact original frame: nothing
  // was left behind by the clear, and nothing accumulated by the redraw.
  moto_nav_ui_set_state(&with_route);
  const Frame redrawn = capture();
  CHECK(redrawn.hash == route_frame.hash);
}

void test_music_page_state_and_fallback() {
  moto_nav_ui_set_music_page_enabled(1);
  moto_nav_ui_set_page(MOTO_UI_PAGE_MUSIC);
  CHECK(moto_nav_ui_get_page() == MOTO_UI_PAGE_MUSIC);

  moto_music_state_t music{};
  music.connected = 1;
  music.playing = 1;
  music.like_available = 1;
  music.source_name = "NetEase";
  music.track_title = "公路之歌";
  music.artist_name = "新裤子";
  moto_nav_ui_set_music_state(&music);
  const Frame titled = capture();

  // Emptying the strings must visibly clear the labels (no stale text).
  moto_music_state_t empty_state{};
  empty_state.connected = 1;
  moto_nav_ui_set_music_state(&empty_state);
  const Frame emptied = capture();
  CHECK(titled.hash != emptied.hash);

  // Disabling the music page while it is on screen must fall back to the
  // navigation page; re-enabling must not restore it by itself.
  moto_nav_ui_set_music_page_enabled(0);
  pump();
  CHECK(moto_nav_ui_get_page() != MOTO_UI_PAGE_MUSIC);
}

int home_tap_count = 0;

void count_home_tap(void *) {
  ++home_tap_count;
}

void test_home_glyph_is_gated_by_speed() {
  home_tap_count = 0;
  moto_nav_ui_set_home_callback(count_home_tap, nullptr);
  moto_ui_state_t state = base_state();
  state.speed_kph = 0;
  moto_nav_ui_set_state(&state);

  lv_obj_t *home = lv_obj_find_by_name(lv_screen_active(), "moto-home");
  CHECK(home != nullptr);
  if (home == nullptr) return;
  CHECK(!lv_obj_has_flag(home, LV_OBJ_FLAG_HIDDEN));
  (void)lv_obj_send_event(home, LV_EVENT_CLICKED, nullptr);
  CHECK(home_tap_count == 1);

  // Above the gate the glyph is gone, and a tap that was already queued on the
  // old frame must not start a route either.
  state.speed_kph = 30;
  moto_nav_ui_set_state(&state);
  CHECK(lv_obj_has_flag(home, LV_OBJ_FLAG_HIDDEN));
  (void)lv_obj_send_event(home, LV_EVENT_CLICKED, nullptr);
  CHECK(home_tap_count == 1);

  // Back under the gate the glyph returns and works again.
  state.speed_kph = 5;
  moto_nav_ui_set_state(&state);
  CHECK(!lv_obj_has_flag(home, LV_OBJ_FLAG_HIDDEN));
  (void)lv_obj_send_event(home, LV_EVENT_CLICKED, nullptr);
  CHECK(home_tap_count == 2);

  moto_nav_ui_set_home_callback(nullptr, nullptr);
}

void test_home_blip_is_clamped_to_the_ring() {
  moto_nav_ui_set_reduce_motion(1);
  moto_nav_ui_set_phone_connection(MOTO_UI_PHONE_ONLINE);
  moto_ui_state_t state = base_state();
  // A route keeps the connection lifecycle overlay off the map, so the blip
  // is actually visible in the captured frame.
  fill_route(state, 8);
  state.speed_kph = 0;
  state.has_home = 1;
  state.home_x = 300;
  state.home_y = 200;
  moto_nav_ui_set_state(&state);
  pump();

  lv_obj_t *blip = lv_obj_find_by_name(lv_screen_active(), "moto-home-blip");
  CHECK(blip != nullptr);
  if (blip == nullptr) return;
  CHECK(!lv_obj_has_flag(blip, LV_OBJ_FLAG_HIDDEN));
  const Frame with_home = capture();

  // A home outside the map window keeps its bearing and is pinned to the ring
  // instead of vanishing: a safehouse three kilometres away is still the point
  // of the feature.
  const int32_t centre_x = kWidth / 2;
  const int32_t centre_y = kHeight / 2;
  state.home_x = 440;
  state.home_y = 30;
  moto_nav_ui_set_state(&state);
  CHECK(!lv_obj_has_flag(blip, LV_OBJ_FLAG_HIDDEN));
  const int32_t dx = lv_obj_get_x(blip) + lv_obj_get_width(blip) / 2 - centre_x;
  const int32_t dy = lv_obj_get_y(blip) + lv_obj_get_height(blip) / 2 - centre_y;
  CHECK(dx > 0);
  CHECK(dy < 0);
  // Same bearing as the raw point (207, -203), and inside the ring.
  CHECK(std::abs(dx * 203 + dy * 207) < 207);
  CHECK(static_cast<int64_t>(dx) * dx + static_cast<int64_t>(dy) * dy <=
        static_cast<int64_t>(MOTO_UI_RADAR_RING_RADIUS_PX) *
            MOTO_UI_RADAR_RING_RADIUS_PX);
  CHECK(with_home.hash != capture().hash);

  // The blip is ink-on-paper in every theme, so it must survive the switch.
  // Without this, a themed blip could disappear into the cream day map and no
  // other test would notice.
  moto_nav_ui_set_theme(MOTO_UI_THEME_GTA_DAY);
  moto_nav_ui_set_state(&state);
  pump();
  CHECK(!lv_obj_has_flag(blip, LV_OBJ_FLAG_HIDDEN));
  const int32_t blip_x = lv_obj_get_x(blip) + lv_obj_get_width(blip) / 2;
  const int32_t blip_y = lv_obj_get_y(blip) + lv_obj_get_height(blip) / 2;
  // The glyph's ink is kWhite (a warm near-white), and the day map is cream,
  // so the property worth asserting is "still light", not an exact constant.
  bool blip_is_light = false;
  for (int yy = blip_y - 5; yy <= blip_y + 5; ++yy) {
    for (int xx = blip_x - 5; xx <= blip_x + 5; ++xx) {
      const std::uint16_t pixel = pixel_at(xx, yy);
      const int red = ((pixel >> 11) & 0x1F) * 255 / 31;
      const int green = ((pixel >> 5) & 0x3F) * 255 / 63;
      const int blue = (pixel & 0x1F) * 255 / 31;
      blip_is_light = blip_is_light ||
                      (red > 200 && green > 200 && blue > 200);
    }
  }
  CHECK(blip_is_light);
  moto_nav_ui_set_theme(MOTO_UI_THEME_OFFICIAL);

  state.has_home = 0;
  state.home_x = 300;
  state.home_y = 200;
  moto_nav_ui_set_state(&state);
  CHECK(lv_obj_has_flag(blip, LV_OBJ_FLAG_HIDDEN));
  CHECK(with_home.hash != capture().hash);
}

void test_phone_lifecycle_renders_deterministically() {
  // The connection lifecycle is a full-screen overlay for the empty
  // navigation page; cycle it there and require visually distinct terminal
  // states plus deterministic repetition (no accumulated residue).
  moto_ui_state_t empty = base_state();
  empty.mode = MOTO_UI_ACQUIRING_FIX;
  empty.has_destination = 0;
  empty.route_point_count = 0;
  empty.road_name = nullptr;
  empty.next_road_name = nullptr;
  moto_nav_ui_set_state(&empty);
  moto_nav_ui_set_phone_connection(MOTO_UI_PHONE_OFFLINE);
  const Frame offline = capture();
  moto_nav_ui_set_phone_connection(MOTO_UI_PHONE_ONLINE);
  const Frame online = capture();
  CHECK(offline.hash != online.hash);

  moto_nav_ui_set_phone_connection(MOTO_UI_PHONE_OFFLINE);
  const Frame offline_again = capture();
  CHECK(offline_again.hash == offline.hash);

  moto_nav_ui_set_phone_connection(MOTO_UI_PHONE_CONNECTING);
  const Frame connecting = capture();
  moto_nav_ui_set_phone_connection(MOTO_UI_PHONE_OFFLINE); CHECK(capture().hash == offline.hash);
}

void test_demo_mode_visual_contract() {
  // Demo mode suppresses the full-screen connection lifecycle and labels the
  // status line "DEMO RIDE"; toggling it must be perfectly reversible.
  moto_ui_state_t empty = base_state();
  empty.mode = MOTO_UI_ACQUIRING_FIX;
  empty.has_destination = 0;
  empty.route_point_count = 0;
  empty.road_name = nullptr;
  empty.next_road_name = nullptr;
  moto_nav_ui_set_state(&empty);
  moto_nav_ui_set_phone_connection(MOTO_UI_PHONE_OFFLINE);
  moto_nav_ui_set_demo_active(0);
  const Frame without_demo = capture();

  moto_nav_ui_set_demo_active(1);
  const Frame with_demo = capture();
  CHECK(with_demo.hash != without_demo.hash);

  moto_nav_ui_set_demo_active(0);
  CHECK(capture().hash == without_demo.hash);

  // Contract: demo mode is connection-independent. The lifecycle surface is
  // hidden while demo is active, so connecting the phone must not change a
  // single pixel of the demo frame.
  moto_nav_ui_set_demo_active(1);
  moto_nav_ui_set_phone_connection(MOTO_UI_PHONE_ONLINE);
  CHECK(capture().hash == with_demo.hash);
  moto_nav_ui_set_demo_active(0);
  moto_nav_ui_set_phone_connection(MOTO_UI_PHONE_OFFLINE);
}

void test_capacity_payloads_render_stably() {
  // Maximum-capacity geometry must render identically across repeats.
  moto_ui_state_t max_state = base_state();
  fill_route(max_state, -1);
  fill_streets(max_state);
  max_state.road_point_count = MOTO_UI_ROAD_POINT_CAPACITY;
  for (int i = 0; i < MOTO_UI_ROAD_POINT_CAPACITY; ++i) {
    max_state.road_points[i] = {
        static_cast<std::int16_t>((i * 7) % kWidth),
        static_cast<std::int16_t>((i * 13) % kHeight)};
  }
  max_state.road_polyline_count = 4;
  for (int i = 0; i < 4; ++i) {
    max_state.road_polylines[i] = {
        static_cast<std::uint8_t>(i * 48),
        48,
        static_cast<std::uint8_t>(i % 6)};
  }
  moto_nav_ui_set_state(&max_state);
  const Frame max_first = capture();
  moto_nav_ui_set_state(&max_state);
  const Frame max_second = capture();
  CHECK(max_first.hash == max_second.hash);
}

void test_motion_frames_interpolate_then_settle_without_residue() {
  moto_nav_ui_set_reduce_motion(1);
  moto_nav_ui_set_phone_connection(MOTO_UI_PHONE_ONLINE);
  moto_ui_state_t state = base_state();
  state.route_point_count = 3;
  state.route_points[0] = {233, 254};
  state.route_points[1] = {233, 150};
  state.route_points[2] = {233, 30};
  state.road_point_count = 2;
  state.road_points[0] = {110, 280};
  state.road_points[1] = {110, 30};
  state.road_polyline_count = 1;
  state.road_polylines[0] = {0, 2, 1};
  state.building_point_count = 4;
  state.building_points[0] = {300, 80};
  state.building_points[1] = {350, 80};
  state.building_points[2] = {350, 130};
  state.building_points[3] = {300, 130};
  state.building_footprint_count = 1;
  state.building_footprints[0] = {0, 4, 1};
  moto_nav_ui_set_state(&state);
  pump(260);  // finish the transient connected indication before comparing
  const auto before = capture().hash;

  // One heading frame moves all retained arrays through the ordinary motion
  // path, preserving the anchor and topology while the map turns under it.
  for (int index = 1; index < 3; ++index) state.route_points[index].x -= 130;
  for (int index = 0; index < 2; ++index) state.road_points[index].x += 110;
  for (int index = 0; index < 4; ++index) state.building_points[index].x -= 90;
  moto_nav_ui_set_reduce_motion(0);
  moto_nav_ui_set_motion_state(&state);
  pump(10);  // 50 ms contains two 25 ms interpolation opportunities.
  const auto intermediate = frame_hash();
  CHECK(intermediate != before);
  pump(200);
  const auto settled = frame_hash();
  const auto settled_bytes = shadow_frame;
  CHECK(settled != intermediate);

  // The eventual animated frame must match a direct render exactly, including
  // clearing the old road/building pixels in the shared invalidated region.
  moto_nav_ui_set_reduce_motion(1);
  moto_nav_ui_set_state(&state);
  const auto direct = capture();
  // Compare the map viewport. The page dots below it expire independently at
  // five seconds, which may fall between the two captured frames.
  constexpr int map_height = (232 * kWidth + 180) / 360;
  constexpr std::size_t map_bytes = kWidth * map_height * (LV_COLOR_DEPTH / 8);
  CHECK(std::equal(direct.bytes.begin(), direct.bytes.begin() + map_bytes,
                   settled_bytes.begin()));
}

void test_diagonal_map_pixels_are_independent_of_partial_buffer_height() {
  moto_nav_ui_set_reduce_motion(1);
  moto_nav_ui_set_phone_connection(MOTO_UI_PHONE_ONLINE);
  auto state = base_state();
  fill_route(state, -1);
  fill_streets(state);
  state.road_point_count = MOTO_UI_ROAD_POINT_CAPACITY;
  state.road_polyline_count = MOTO_UI_ROAD_POLYLINE_CAPACITY;
  for (int i = 0; i < state.road_point_count; ++i) {
    // Long crossing diagonals, partially outside the viewport, exercise
    // band borders, both slope signs and the different road stroke widths.
    state.road_points[i] = {
        static_cast<std::int16_t>((i * 83) % 780 - 160),
        static_cast<std::int16_t>((i * 127) % 590 - 110)};
  }
  for (int i = 0; i < state.road_polyline_count; ++i) {
    state.road_polylines[i] = {
        static_cast<std::uint8_t>(i * 8), 8,
        static_cast<std::uint8_t>(i % 6)};
  }
  moto_nav_ui_set_state(&state);
  pump(260);
  auto* display = lv_display_get_default();
  static std::vector<std::uint8_t> short_buffer(kWidth * 40 * 2);
  static std::vector<std::uint8_t> tall_buffer(kWidth * 320 * 2);
  const auto draw_with_buffer = [display](std::vector<std::uint8_t>& buffer) {
    lv_display_set_buffers(display, buffer.data(), nullptr, buffer.size(),
                            LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_obj_invalidate(lv_screen_active());
    lv_refr_now(display);
    return shadow_frame;
  };
  const auto short_frame = draw_with_buffer(short_buffer);
  const auto tall_frame = draw_with_buffer(tall_buffer);
  CHECK(tall_frame == short_frame);
  CHECK(draw_with_buffer(short_buffer) == tall_frame);
}

// Dev-only visual check for the round screen. Set MOTO_UI_DUMP=/tmp/frame.ppm
// to render one representative navigation frame after the assertions; the
// agent-side loop converts it to PNG and looks at it. It never runs otherwise,
// so the test stays a test. MOTO_UI_HEAP prints the LVGL pool high-water mark,
// which is the number the 64 KiB board budget is measured against.
void write_ppm(const char *path) {
  FILE *file = std::fopen(path, "wb");
  if (file == nullptr) return;
  std::fprintf(file, "P6\n%d %d\n255\n", kWidth, kHeight);
  for (int y = 0; y < kHeight; ++y) {
    for (int x = 0; x < kWidth; ++x) {
      const uint16_t pixel = pixel_at(x, y);
      const uint8_t rgb[3] = {
          static_cast<uint8_t>(((pixel >> 11) & 0x1F) * 255 / 31),
          static_cast<uint8_t>(((pixel >> 5) & 0x3F) * 255 / 63),
          static_cast<uint8_t>((pixel & 0x1F) * 255 / 31),
      };
      std::fwrite(rgb, 1, 3, file);
    }
  }
  std::fclose(file);
}

void render_demo_frame() {
  // Do not inherit the previous test's theme or page: the dump is a review
  // artefact, and a silent dependency on test order would make it lie.
  moto_nav_ui_set_theme(MOTO_UI_THEME_OFFICIAL);
  moto_nav_ui_set_page(MOTO_UI_PAGE_NAVIGATION);
  moto_nav_ui_set_reduce_motion(1);
  moto_nav_ui_set_phone_connection(MOTO_UI_PHONE_ONLINE);
  moto_ui_state_t state = base_state();
  state.heading_deg = 35;
  state.route_progress_percent = 62;
  state.traffic = MOTO_TRAFFIC_SLOW;
  state.has_home = 1;
  state.home_x = 320;
  state.home_y = 140;
  fill_route(state, 12);
  fill_streets(state);
  moto_nav_ui_set_state(&state);
  (void)capture();
}

// LVGL sends LV_EVENT_GESTURE while the drag is still pressed, once the summed
// pointer movement passes the gesture limit. Waiting between moves gives the
// indev timer a chance to read each step.
void swipe(int32_t from_x, int32_t to_x) {
  const int32_t y = MOTO_UI_CANVAS_HEIGHT / 2;
  lv_test_mouse_move_to(from_x, y);
  lv_test_mouse_press();
  lv_test_wait(50);
  lv_test_mouse_move_to((from_x + to_x) / 2, y);
  lv_test_wait(50);
  lv_test_mouse_move_to(to_x, y);
  lv_test_wait(50);
  lv_test_mouse_release();
  lv_test_wait(50);
}

void test_one_swipe_moves_one_page_even_after_a_rebuild() {
  moto_nav_ui_set_reduce_motion(1);
  lv_test_indev_create_all();

  moto_nav_ui_set_page(MOTO_UI_PAGE_NAVIGATION);
  capture();
  swipe(320, 180);
  CHECK(moto_nav_ui_get_page() == MOTO_UI_PAGE_PERFORMANCE);

  // create() re-adds the screen callbacks on every call. A stacked handler
  // would move two pages here while every object-level assertion stayed green.
  moto_nav_ui_create();
  CHECK(moto_nav_ui_get_page() == MOTO_UI_PAGE_NAVIGATION);
  capture();
  swipe(320, 180);
  CHECK(moto_nav_ui_get_page() == MOTO_UI_PAGE_PERFORMANCE);

  // And the reverse direction still moves exactly one page back.
  swipe(180, 320);
  CHECK(moto_nav_ui_get_page() == MOTO_UI_PAGE_NAVIGATION);

  lv_test_indev_delete_all();
  // The swipe reveals the page dots for five seconds; leave the shared UI
  // settled, because the remaining tests compare exact frames.
  pump(1100);
}

}  // namespace

int main(int argc, char **argv) {
  const int buffer_rows = argc > 1 ? std::atoi(argv[1]) : 40;
  // Default to the board's second pool so a hand-run binary is shaped like the
  // device too; pass 0 explicitly to measure the 64 KiB pool alone.
  const int extra_pool_bytes =
      argc > 2 ? std::atoi(argv[2]) : 2 * 1024 * 1024;
  if(buffer_rows < 1 || buffer_rows > kHeight) return EXIT_FAILURE;
  if(extra_pool_bytes < 0) return EXIT_FAILURE;
  init_display(buffer_rows, extra_pool_bytes);

  // Production cadence: the boot animation completes before the full UI is
  // created, so settle it before moto_nav_ui_create().
  moto_nav_ui_show_boot_screen();
  pump(260);
  const Frame settled_boot = capture();
  const bool all_black = std::all_of(
      settled_boot.bytes.begin(), settled_boot.bytes.end(),
      [](std::uint8_t byte) { return byte == 0x00; });
  CHECK(all_black);

  test_frames_are_deterministic();
  test_rebuilding_the_ui_is_idempotent();
  test_a_building_footprint_interior_is_filled();
  test_the_map_covers_the_bottom_of_the_round_canvas();
  test_the_ring_carries_progress_and_a_north_badge();
  test_a_refused_home_tap_shows_a_hint_and_fades();
  test_day_and_night_themes_render_different_map_colors();
  test_auto_theme_follows_the_ambient_night_flag();
  test_performance_and_lean_pages_render_attitude();
  test_clearing_geometry_leaves_no_residue();
  test_music_page_state_and_fallback();
  test_home_glyph_is_gated_by_speed();
  test_home_blip_is_clamped_to_the_ring();
  test_phone_lifecycle_renders_deterministically();
  test_demo_mode_visual_contract();
  test_capacity_payloads_render_stably();
  test_motion_frames_interpolate_then_settle_without_residue();
  test_diagonal_map_pixels_are_independent_of_partial_buffer_height();
  test_one_swipe_moves_one_page_even_after_a_rebuild();

  // Visual check runs while the instrument is still alive; the terminal
  // screens below delete the whole widget tree.
  if (const char *dump = std::getenv("MOTO_UI_DUMP")) {
    render_demo_frame();
    write_ppm(dump);
  }

  // Terminal screens run last: they tear the full UI down. Leave a hint on
  // screen first: its timer is the one widget-retaining timer a terminal
  // screen used to forget, and a visible hint is the worst case.
  moto_nav_ui_show_home_error();
  pump(10);
  moto_nav_ui_show_boot_screen();
  pump(260);
  const Frame boot_again = capture();
  CHECK(std::all_of(boot_again.bytes.begin(), boot_again.bytes.end(),
                    [](std::uint8_t byte) { return byte == 0x00; }));

  moto_nav_ui_show_power_off_screen();
  pump(260);
  const Frame power_off = capture();
  moto_nav_ui_show_power_off_screen();
  pump(260);
  CHECK(power_off.hash == capture().hash);
  CHECK(power_off.hash != settled_boot.hash);

  // The BLE task can still deliver a snapshot between the terminal screen and
  // the PMIC cutting power. The widget tree is already gone, and LVGL asserts
  // on a null object. The native build turns that assert into a trap, so the
  // failure arrives as a stack rather than as a process that spins at 100% CPU.
  // Every entry point must be inert.
  const Frame before_late = capture();
  moto_ui_state_t late = base_state();
  late.has_home = 1;
  late.home_x = 300;
  late.home_y = 200;
  moto_nav_ui_set_state(&late);
  moto_nav_ui_set_motion_state(&late);
  moto_nav_ui_set_phone_connection(MOTO_UI_PHONE_ONLINE);
  moto_nav_ui_show_home_error();
  moto_nav_ui_set_page(MOTO_UI_PAGE_SPEED);
  moto_nav_ui_set_theme(MOTO_UI_THEME_GTA_DAY);
  CHECK(moto_nav_ui_get_page() == MOTO_UI_PAGE_NAVIGATION);
  CHECK(before_late.hash == capture().hash);

  if (std::getenv("MOTO_UI_HEAP")) {
    // Board parity: the ESP32 LVGL pool defaults to 64 KiB. Peak usage here
    // is the number that decides whether this tree fits on the device.
    lv_mem_monitor_t mon{};
    lv_mem_monitor(&mon);
    std::cerr << "LVGL heap total=" << mon.total_size << " free=" << mon.free_size
              << " max_used=" << mon.max_used << " used_pct=" << (int)mon.used_pct
              << " frag_pct=" << (int)mon.frag_pct << "\n";
  }
  if (failures != 0) {
    std::cerr << failures << " nav_ui render checks failed\n";
    return EXIT_FAILURE;
  }
  std::cout << "All nav_ui render checks passed\n";
  return EXIT_SUCCESS;
}
