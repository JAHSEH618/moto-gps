// Deterministic host-side rendering tests for the LVGL navigation UI.
// A headless LVGL display renders the real moto_nav_ui into an RGB565
// framebuffer; frames are hashed to verify bit-exact determinism, clean page
// transitions and label/geometry residue cleanup.

#include "moto_nav_ui.h"
#include "moto_nav_ui_text.h"

#include <lvgl.h>

// Pointer indev shipped with LVGL and compiled in when LV_USE_TEST is set.
// It gives the suite a real gesture: LV_EVENT_GESTURE cannot be synthesised
// with lv_obj_send_event, because gesture_event() reads the direction from
// the active indev.
#include "src/debugging/test/lv_test_indev.h"
#include "src/core/lv_obj_class_private.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

// The music page's faces, declared here so the text-coverage test reads the
// same glyph subsets the firmware ships.
LV_FONT_DECLARE(moto_font_display_40);
LV_FONT_DECLARE(moto_font_text_40);

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
// When set, every flushed area is appended here: the dirty-area tests read
// what LVGL actually repainted, not what the UI meant to invalidate.
std::vector<lv_area_t>* flush_log = nullptr;

void flush_cb(lv_display_t* display, const lv_area_t* area,
              std::uint8_t* px_map) {
  if (flush_log != nullptr) flush_log->push_back(*area);
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

// GTA night paper: the block fill and the map ground the building tests sample.
const std::uint16_t kNightBlock = rgb565(0x24, 0x24, 0x24);
const std::uint16_t kNightGround = rgb565(0x16, 0x16, 0x16);

std::uint16_t pixel_at(int x, int y) {
  const std::size_t offset =
      (static_cast<std::size_t>(y) * kWidth + static_cast<std::size_t>(x)) *
      (LV_COLOR_DEPTH / 8);
  std::uint16_t pixel = 0;
  std::memcpy(&pixel, shadow_frame.data() + offset, sizeof(pixel));
  return pixel;
}

// Pixels of exactly `color` inside [x1, x2] x [y1, y2].
int count_pixels_in(int x1, int y1, int x2, int y2, std::uint16_t color) {
  int count = 0;
  for (int y = y1; y <= y2; ++y) {
    for (int x = x1; x <= x2; ++x) {
      if (pixel_at(x, y) == color) ++count;
    }
  }
  return count;
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

// A believable block for the review frames: a right turn 120 m ahead (68 px
// at 0.57 px/m), a street grid of mixed classes and a few footprints. The
// zigzag in fill_route() is a hash fixture; it read as a wavy route in review.
void fill_review_city(moto_ui_state_t& state) {
  const moto_ui_point_t route[] = {
      {233, 340}, {233, 233}, {233, 175}, {241, 165}, {330, 165}, {440, 165}};
  state.route_point_count = 6;
  for (int i = 0; i < 6; ++i) state.route_points[i] = route[i];

  struct Street {
    moto_ui_point_t a;
    moto_ui_point_t b;
    std::uint8_t road_class;
  };
  const Street streets[] = {
      {{233, -20}, {233, 486}, 0},  {{-20, 165}, {486, 165}, 1},
      {{113, -20}, {113, 486}, 3},  {{353, -20}, {353, 486}, 3},
      {{-20, 55}, {486, 55}, 3},    {{-20, 285}, {486, 285}, 2},
      {{-20, 395}, {486, 395}, 3},  {{173, 165}, {173, 285}, 4},
      {{293, 55}, {293, 165}, 4},   {{293, 285}, {293, 395}, 5},
      {{40, 470}, {470, 20}, 4},
  };
  int point = 0;
  int line = 0;
  for (const Street& street : streets) {
    state.road_points[point] = street.a;
    state.road_points[point + 1] = street.b;
    state.road_polylines[line] = {static_cast<std::uint8_t>(point), 2,
                                  street.road_class};
    point += 2;
    ++line;
  }
  state.road_point_count = static_cast<std::uint8_t>(point);
  state.road_polyline_count = static_cast<std::uint8_t>(line);

  const moto_ui_point_t blocks[][2] = {
      {{125, 67}, {221, 153}}, {{245, 67}, {281, 153}},
      {{125, 177}, {161, 273}}, {{305, 297}, {341, 383}},
      {{245, 297}, {281, 383}}};
  int corner = 0;
  int footprint = 0;
  for (const auto& block : blocks) {
    const moto_ui_point_t lo = block[0];
    const moto_ui_point_t hi = block[1];
    state.building_points[corner] = lo;
    state.building_points[corner + 1] = {hi.x, lo.y};
    state.building_points[corner + 2] = hi;
    state.building_points[corner + 3] = {lo.x, hi.y};
    state.building_footprints[footprint] = {static_cast<std::uint8_t>(corner),
                                            4, 0};
    corner += 4;
    ++footprint;
  }
  state.building_point_count = static_cast<std::uint8_t>(corner);
  state.building_footprint_count = static_cast<std::uint8_t>(footprint);
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
  // The block layer is opt-in; see test_buildings_are_opt_in.
  moto_nav_ui_set_show_buildings(1);
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
  CHECK(pixel_at(150, 150) == kNightBlock);
  // The old fan-spoke seams must be covered as well, not just the centre.
  CHECK(pixel_at(125, 125) == kNightBlock);
  CHECK(pixel_at(175, 175) == kNightBlock);
  // The adjacent-row spans are merged into one rectangle; the first and last
  // rows must still be painted, not just the row that opened the run.
  CHECK(pixel_at(150, 101) == kNightBlock);
  CHECK(pixel_at(150, 199) == kNightBlock);
  // The rest of the map must remain the page background.
  CHECK(pixel_at(300, 150) == kNightGround);
}

void test_the_map_covers_the_bottom_of_the_round_canvas() {
  moto_nav_ui_set_reduce_motion(1);
  moto_nav_ui_set_phone_connection(MOTO_UI_PHONE_ONLINE);
  moto_nav_ui_set_show_buildings(1);
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
  CHECK(pixel_at(120, 380) == kNightBlock);
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

  // The band is the gauge: progress is painted on the magenta rim itself, so
  // every change must repaint the ring layer rather than the map.
  const Frame idle = capture();
  state.route_progress_percent = 62;
  moto_nav_ui_set_state(&state);
  const Frame quarter = capture();
  CHECK(quarter.hash != idle.hash);

  state.route_progress_percent = 50;
  moto_nav_ui_set_state(&state);
  const Frame half = capture();
  CHECK(half.hash != quarter.hash);

  state.route_progress_percent = 100;
  moto_nav_ui_set_state(&state);
  const Frame full = capture();
  CHECK(full.hash != half.hash);

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

// Buildings are a readability trade, not scenery: hidden after create() until
// the phone asks. This is the only test that asserts the default, because the
// rest of the suite enables the layer it samples.
void test_buildings_are_opt_in() {
  moto_nav_ui_create();
  moto_nav_ui_set_reduce_motion(1);
  moto_nav_ui_set_phone_connection(MOTO_UI_PHONE_ONLINE);
  // Night paper: the building tests below sample its block and ground colours.
  // OFFICIAL is the classic interface now, not a palette of this tree.
  moto_nav_ui_set_theme(MOTO_UI_THEME_GTA_NIGHT);
  moto_ui_state_t state = base_state();
  // Keep a route so the lifecycle overlay stays hidden; the route is drawn
  // below the building and never crosses its interior.
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
  // create() resets the link to offline, so going online again raises the
  // 920 ms CONNECTED confirmation over the map. Let it expire first: sampling
  // through it would read the overlay's colour, not the building layer.
  pump(240);
  capture();

  // Default after create(): the footprint interior is bare land and the
  // outline is gone too, so a hidden layer cannot leave a hollow ghost.
  CHECK(pixel_at(150, 150) == kNightGround);
  CHECK(pixel_at(100, 100) == kNightGround);

  moto_nav_ui_set_show_buildings(1);
  capture();
  CHECK(pixel_at(150, 150) == kNightBlock);

  // The phone repeats its settings after every handshake and every echo; a
  // repeat must not invalidate the map layer.
  const Frame shown = capture();
  moto_nav_ui_set_show_buildings(1);
  CHECK(shown.hash == capture().hash);

  moto_nav_ui_set_show_buildings(0);
  capture();
  CHECK(pixel_at(150, 150) == kNightGround);
}

void test_day_and_night_themes_render_different_map_colors() {
  moto_nav_ui_set_reduce_motion(1);
  moto_nav_ui_set_phone_connection(MOTO_UI_PHONE_ONLINE);
  moto_nav_ui_set_show_buildings(1);
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
  // Liberty City day: a grey radar ground with blocks a shade darker, the
  // relationship GTA III's radar uses. Roads and numerals are paper on a
  // black casing, which is where the daylight contrast comes from.
  CHECK(pixel_at(150, 150) == rgb565(0x40, 0x40, 0x3C));
  CHECK(pixel_at(300, 150) == rgb565(0x4E, 0x4E, 0x4A));

  moto_nav_ui_set_theme(MOTO_UI_THEME_GTA_NIGHT);
  capture();
  // Liberty City night: near-black ground with a raised grey block.
  CHECK(pixel_at(150, 150) == kNightBlock);
  CHECK(pixel_at(300, 150) == kNightGround);

  moto_nav_ui_set_theme(MOTO_UI_THEME_GTA_NIGHT);
}

void test_auto_theme_follows_the_ambient_night_flag() {
  moto_nav_ui_set_reduce_motion(1);
  moto_nav_ui_set_phone_connection(MOTO_UI_PHONE_ONLINE);
  moto_nav_ui_set_show_buildings(1);
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
  CHECK(pixel_at(150, 150) == kNightBlock);
  CHECK(pixel_at(300, 150) == kNightGround);
  const Frame night = capture();

  // The flag is expected to repeat; a repeat must not restyle or repaint.
  moto_nav_ui_set_ambient_night(1);
  CHECK(night.hash == capture().hash);

  moto_nav_ui_set_ambient_night(0);
  capture();
  CHECK(pixel_at(150, 150) == rgb565(0x40, 0x40, 0x3C));
  CHECK(pixel_at(300, 150) == rgb565(0x4E, 0x4E, 0x4A));

  // An explicit theme is authoritative: the flag is stored but not applied.
  moto_nav_ui_set_theme(MOTO_UI_THEME_GTA_NIGHT);
  const Frame explicit_night = capture();
  moto_nav_ui_set_ambient_night(1);
  CHECK(explicit_night.hash == capture().hash);

  moto_nav_ui_set_theme(MOTO_UI_THEME_GTA_NIGHT);
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


// The phone shows the round screen's 0-100 runs (PerformanceRun, 0x19), so
// every transition of the device clock is reported: armed, started, finished,
// and armed again after a deliberate stop.
std::vector<moto_ui_performance_run_t> performance_reports;
void record_performance(const moto_ui_performance_run_t *run, void *) {
  performance_reports.push_back(*run);
}

void test_the_launch_clock_reports_each_run() {
  moto_nav_ui_set_reduce_motion(1);
  moto_nav_ui_set_phone_connection(MOTO_UI_PHONE_ONLINE);
  performance_reports.clear();
  moto_nav_ui_set_performance_callback(record_performance, nullptr);
  moto_ui_state_t state = base_state();
  state.page = MOTO_UI_PAGE_PERFORMANCE;
  state.speed_kph = 0;
  moto_nav_ui_set_state(&state);
  pump(10);
  // A page that is merely shown reports nothing new.
  CHECK(performance_reports.empty());

  state.speed_kph = 8;
  moto_nav_ui_set_state(&state);
  CHECK(performance_reports.size() == 1U);
  if (performance_reports.size() != 1U) return;
  CHECK(performance_reports[0].state == MOTO_UI_RUN_RUNNING);
  CHECK(performance_reports[0].target_kph == 100);
  const std::uint32_t first_run = performance_reports[0].run_id;
  CHECK(first_run > 0U);
  CHECK(performance_reports[0].elapsed_ms == 0U);

  pump(100);  // 500 ms of LVGL ticks
  state.speed_kph = 60;
  moto_nav_ui_set_state(&state);
  // Speeding up mid-run is not a transition.
  CHECK(performance_reports.size() == 1U);
  pump(100);
  state.speed_kph = 101;
  moto_nav_ui_set_state(&state);
  CHECK(performance_reports.size() == 2U);
  if (performance_reports.size() != 2U) return;
  const moto_ui_performance_run_t done = performance_reports[1];
  CHECK(done.state == MOTO_UI_RUN_FINISHED);
  CHECK(done.run_id == first_run);
  CHECK(done.elapsed_ms >= 1'000U && done.elapsed_ms < 1'200U);
  CHECK(done.best_ms == done.elapsed_ms || done.best_ms < done.elapsed_ms);

  // A stop rearms only once it has lasted 1.5 s.
  state.speed_kph = 0;
  moto_nav_ui_set_state(&state);
  pump(100);
  moto_nav_ui_set_state(&state);
  CHECK(performance_reports.size() == 2U);
  pump(240);
  moto_nav_ui_set_state(&state);
  CHECK(performance_reports.size() == 3U);
  if (performance_reports.size() == 3U) {
    CHECK(performance_reports[2].state == MOTO_UI_RUN_ARMED);
    CHECK(performance_reports[2].elapsed_ms == 0U);
    CHECK(performance_reports[2].best_ms == done.best_ms);
  }

  // The next roll-away is a new run.
  state.speed_kph = 3;
  moto_nav_ui_set_state(&state);
  CHECK(performance_reports.size() == 4U);
  if (performance_reports.size() == 4U) {
    CHECK(performance_reports[3].run_id == first_run + 1U);
  }

  moto_nav_ui_set_performance_callback(nullptr, nullptr);
  state.speed_kph = 0;
  moto_nav_ui_set_state(&state);
  moto_nav_ui_set_page(MOTO_UI_PAGE_NAVIGATION);
  pump(1100);
}

int lean_calibration_requests = 0;
int lean_peak_clears = 0;

void lean_calibration_requested(void *) { ++lean_calibration_requests; }
void lean_peaks_cleared(void *) { ++lean_peak_clears; }

void test_lean_press_durations_dispatch_actions() {
  moto_nav_ui_set_reduce_motion(1);
  moto_nav_ui_set_phone_connection(MOTO_UI_PHONE_ONLINE);
  moto_ui_state_t state = base_state();
  state.page = MOTO_UI_PAGE_LEAN;
  moto_nav_ui_set_state(&state);
  capture();

  lv_obj_t *layer =
      lv_obj_find_by_name(lv_screen_active(), "moto-lean-page");
  CHECK(layer != nullptr);
  if (layer == nullptr) return;
  lean_calibration_requests = 0;
  lean_peak_clears = 0;
  moto_nav_ui_set_lean_calibration_callback(lean_calibration_requested,
                                            nullptr);
  moto_nav_ui_set_clear_peaks_callback(lean_peaks_cleared, nullptr);

  // 2.5 s: release clears peaks, never starts calibration.
  lv_obj_send_event(layer, LV_EVENT_PRESSED, nullptr);
  pump(500);
  lv_obj_send_event(layer, LV_EVENT_RELEASED, nullptr);
  CHECK(lean_peak_clears == 1);
  CHECK(lean_calibration_requests == 0);

  // 3.2 s: release starts the 3 s countdown, then requests calibration.
  lv_obj_send_event(layer, LV_EVENT_PRESSED, nullptr);
  pump(640);
  lv_obj_send_event(layer, LV_EVENT_RELEASED, nullptr);
  CHECK(lean_calibration_requests == 0);
  pump(640);
  CHECK(lean_calibration_requests == 1);
  CHECK(lean_peak_clears == 1);

  moto_nav_ui_set_lean_calibration_callback(nullptr, nullptr);
  moto_nav_ui_set_clear_peaks_callback(nullptr, nullptr);
}

void test_lean_status_slot_reports_cal_and_fallback() {
  moto_nav_ui_set_reduce_motion(1);
  moto_ui_state_t state = base_state();
  state.page = MOTO_UI_PAGE_LEAN;
  moto_nav_ui_set_state(&state);
  capture();
  lv_obj_t *plate =
      lv_obj_find_by_name(lv_screen_active(), "moto-lean-status");
  CHECK(plate != nullptr);
  if (plate == nullptr) return;

  // The previous press test leaves a capture in flight; start from a clean
  // status slot so the flags below are what the plate shows.
  moto_nav_ui_set_lean_cal_state(MOTO_UI_LEAN_CAL_NONE, 0);
  moto_nav_ui_set_lean_flags(0, 1, 0, 0);  // IMU POLL
  pump(30);
  CHECK(!lv_obj_has_flag(plate, LV_OBJ_FLAG_HIDDEN));
  moto_nav_ui_set_lean_flags(0, 0, 1, 0);  // LOG FULL outranks POLL
  pump(30);
  CHECK(!lv_obj_has_flag(plate, LV_OBJ_FLAG_HIDDEN));
  moto_nav_ui_set_lean_flags(0, 0, 0, 1);  // LOG DIRTY outranks FULL
  pump(30);
  CHECK(!lv_obj_has_flag(plate, LV_OBJ_FLAG_HIDDEN));
  CHECK(std::strcmp(lv_label_get_text(lv_obj_get_child(plate, 0)),
                    "LOG DIRTY") == 0);
  moto_nav_ui_set_lean_flags(0, 0, 1, 0);  // back to FULL while dirty clears
  pump(30);
  CHECK(!lv_obj_has_flag(plate, LV_OBJ_FLAG_HIDDEN));
  moto_nav_ui_set_lean_cal_state(MOTO_UI_LEAN_CAL_FAIL_MOVING, 0);
  pump(30);
  CHECK(!lv_obj_has_flag(plate, LV_OBJ_FLAG_HIDDEN));
  // A result is transient: it clears itself after about a second, while
  // LOG FULL still holds the slot.
  pump(300);
  CHECK(!lv_obj_has_flag(plate, LV_OBJ_FLAG_HIDDEN));
  moto_nav_ui_set_lean_cal_state(MOTO_UI_LEAN_CAL_NONE, 0);
  moto_nav_ui_set_lean_flags(0, 0, 0, 0);
  pump(30);
  CHECK(lv_obj_has_flag(plate, LV_OBJ_FLAG_HIDDEN));
}

void test_lean_page_without_attitude_keeps_the_poster() {
  moto_nav_ui_set_reduce_motion(1);
  moto_nav_ui_set_theme(MOTO_UI_THEME_GTA_NIGHT);
  moto_ui_state_t state = base_state();
  state.page = MOTO_UI_PAGE_LEAN;
  state.has_attitude = 0;
  moto_nav_ui_set_state(&state);
  capture();
  // The instrument is still there: an absent attitude is a dark arc and a
  // NO IMU plate where the numeral goes, not an empty disc.
  CHECK(pixel_at(240, 72) == rgb565(0x2A, 0x2A, 0x2A));
  CHECK(count_pixels_in(180, 210, 290, 240, rgb565(0xF2, 0xEE, 0xE4)) > 40);
  CHECK(pixel_at(kWidth / 2, kHeight / 2) !=
        rgb565(0x00, 0x00, 0x00));
  moto_nav_ui_set_theme(MOTO_UI_THEME_GTA_DAY);
  moto_nav_ui_set_page(MOTO_UI_PAGE_NAVIGATION);
}

// The Lean page's frame time on the board is set by how many areas it dirties
// and whether any of them reaches the rim: each area costs a tree walk and a
// QSPI flush, and one that touches the rim re-strokes band, casing and
// protractor. A sweep from upright to full lean and over to the other side
// must therefore repaint only inside the casing, in a bounded number of areas.
void test_lean_sweep_repaints_inside_the_casing() {
  moto_nav_ui_set_reduce_motion(1);
  moto_nav_ui_set_theme(MOTO_UI_THEME_GTA_NIGHT);
  moto_nav_ui_set_phone_connection(MOTO_UI_PHONE_ONLINE);
  moto_ui_state_t state = base_state();
  state.page = MOTO_UI_PAGE_LEAN;
  state.has_attitude = 1;
  state.lean_deg = 0.0F;
  moto_nav_ui_set_state(&state);
  // Let the page dots fade first; they sit at the rim and are not the lean.
  pump(1'200);

  std::vector<lv_area_t> areas;
  std::size_t most_per_frame = 0;
  bool reached_rim = false;
  const double cx = kWidth / 2.0;
  const double cy = kHeight / 2.0;
  constexpr double kCasingInner = 197.0;
  float lean = 0.0F;
  float step = 1.5F;
  for (int frame = 0; frame < 140; ++frame) {
    lean += step;
    if (lean >= 52.0F || lean <= -52.0F) step = -step;
    state.lean_deg = lean;
    areas.clear();
    flush_log = &areas;
    moto_nav_ui_set_state(&state);
    pump(5);  // one 25 ms refresh
    flush_log = nullptr;
    most_per_frame = std::max(most_per_frame, areas.size());
    for (const lv_area_t& area : areas) {
      const double xs[2] = {area.x1 - cx, area.x2 + 1 - cx};
      const double ys[2] = {area.y1 - cy, area.y2 + 1 - cy};
      for (double x : xs) {
        for (double y : ys) {
          if (std::sqrt(x * x + y * y) >= kCasingInner) {
            if (!reached_rim) {
              std::cerr << "lean " << lean << " repainted the rim: "
                        << area.x1 << ',' << area.y1 << '-' << area.x2 << ','
                        << area.y2 << '\n';
            }
            reached_rim = true;
          }
        }
      }
    }
  }
  CHECK(!reached_rim);
  // A changed arc segment or two a side, and the numeral's old and new box
  // on the frames where the number changes.
  CHECK(most_per_frame <= 6);
  moto_nav_ui_set_page(MOTO_UI_PAGE_NAVIGATION);
}

// The sensor runs faster than the panel, so the page dirties the motion once
// per refresh, from what it last drew to the newest sample. Several samples
// between two refreshes must leave exactly the frame a full repaint gives:
// no horizon, sun or blip left behind at a position that was never drawn.
void test_lean_samples_between_refreshes_leave_no_residue() {
  moto_nav_ui_set_reduce_motion(1);
  moto_nav_ui_set_theme(MOTO_UI_THEME_GTA_DAY);
  moto_nav_ui_set_phone_connection(MOTO_UI_PHONE_ONLINE);
  moto_ui_state_t state = base_state();
  state.page = MOTO_UI_PAGE_LEAN;
  state.has_attitude = 1;
  state.lean_deg = -8.0F;
  moto_nav_ui_set_state(&state);
  capture();
  const float burst[] = {4.0F, 19.0F, 33.0F, 27.5F};
  for (float lean : burst) {
    state.lean_deg = lean;
    moto_nav_ui_set_state(&state);
  }
  const Frame incremental = capture();
  lv_obj_invalidate(lv_screen_active());
  const Frame repainted = capture();
  CHECK(incremental.hash == repainted.hash);
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

  // The music page is never hidden. It used to disappear until the phone
  // advertised media capabilities, which removed the controls exactly when a
  // rider reached for them; an absent session now renders an empty state and
  // keeps the transport buttons on screen.
  pump();
  CHECK(moto_nav_ui_get_page() == MOTO_UI_PAGE_MUSIC);
  moto_music_state_t no_session{};
  moto_nav_ui_set_music_state(&no_session);
  const Frame no_session_frame = capture();
  CHECK(no_session_frame.hash != titled.hash);
  CHECK(moto_nav_ui_get_page() == MOTO_UI_PAGE_MUSIC);
}

// Phone-supplied strings end up on faces with a fixed glyph set. A codepoint
// no face in the chain can draw used to paint as an empty box, and so did a
// copy cut in the middle of a codepoint.
bool every_codepoint_is_drawable(const char *text, const lv_font_t *font) {
  std::size_t index = 0;
  const std::size_t size = std::strlen(text);
  while (index < size) {
    const std::uint32_t codepoint =
        moto::nav_ui::detail::next_codepoint(text, size, index);
    if (codepoint != '\n' &&
        !moto::nav_ui::detail::drawable(font, codepoint)) {
      return false;
    }
  }
  return true;
}

void test_music_text_never_paints_a_box() {
  // A copy of the chain the GTA backend installs, so the test reads the same
  // glyph coverage the device has.
  lv_font_t title_face = moto_font_display_40;
  title_face.fallback = &moto_font_text_40;

  char title[64];
  moto::nav_ui::copy_display_text(title, sizeof(title), &title_face, "公路之歌",
                                  "--");
  CHECK(std::strcmp(title, "公路之歌") == 0);

  // GB2312 level 2 is in the subset too: a level-1-only face dropped 嚯 from a
  // real title and rendered it as if the singer had not sung it.
  moto::nav_ui::copy_display_text(title, sizeof(title), &title_face,
                                  "哎哟嚯radio", "--");
  CHECK(std::strcmp(title, "哎哟嚯radio") == 0);

  // Scripts the subset does not carry are dropped rather than drawn as boxes.
  moto::nav_ui::copy_display_text(title, sizeof(title), &title_face,
                                  "夜に駆ける", "--");
  CHECK(std::strcmp(title, "夜に駆ける") != 0);
  CHECK(every_codepoint_is_drawable(title, &title_face));
  CHECK(title[0] != '\0');

  // An ideographic space is not a glyph either: dropping it must not leave the
  // words welded together or a stray gap at the end.
  moto::nav_ui::copy_display_text(title, sizeof(title), &title_face,
                                  "NIGHT\xE3\x80\x80RIDE", "--");
  CHECK(std::strcmp(title, "NIGHTRIDE") == 0);
  moto::nav_ui::copy_display_text(title, sizeof(title), &title_face,
                                  "RIDE\xE3\x80\x80", "--");
  CHECK(std::strcmp(title, "RIDE") == 0);

  // Truncation stops at a character boundary, never inside one: the leftovers
  // of a split codepoint are the box the rider reported.
  char narrow[10];
  moto::nav_ui::copy_display_text(narrow, sizeof(narrow), &title_face,
                                  "公路之歌公路之歌", "--");
  CHECK(std::strlen(narrow) == 9);
  CHECK(every_codepoint_is_drawable(narrow, &title_face));

  // Nothing drawable at all leaves the placeholder, not a blank line. Kana are
  // the clean case: GB2312 level 1 carries no kana at all, while a kanji like
  // 夜 is shared and survives.
  CHECK(!moto::nav_ui::detail::drawable(&title_face, 0x306B));
  moto::nav_ui::copy_display_text(title, sizeof(title), &title_face,
                                  "\xE3\x81\xAB\xE3\x81\x91", "--");
  CHECK(std::strcmp(title, "--") == 0);

  // A missing face must not blackhole the text.
  moto::nav_ui::copy_display_text(title, sizeof(title), nullptr, "KANA", "--");
  CHECK(std::strcmp(title, "KANA") == 0);
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
  // The glyph's ink is the HUD orange route colour on a black casing, the
  // same safehouse as the phone radar; assert "orange", not an exact value.
  bool blip_is_light = false;
  for (int yy = blip_y - 5; yy <= blip_y + 5; ++yy) {
    for (int xx = blip_x - 5; xx <= blip_x + 5; ++xx) {
      const std::uint16_t pixel = pixel_at(xx, yy);
      const int red = ((pixel >> 11) & 0x1F) * 255 / 31;
      const int green = ((pixel >> 5) & 0x3F) * 255 / 63;
      const int blue = (pixel & 0x1F) * 255 / 31;
      blip_is_light = blip_is_light ||
                      (red > 200 && green > 100 && green < 180 && blue < 110);
    }
  }
  CHECK(blip_is_light);  // orange
  moto_nav_ui_set_theme(MOTO_UI_THEME_GTA_NIGHT);

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

// A short route moving 12 px must dirty its own span, not the whole 466 px
// map. Before the dirty-area pass every route motion frame invalidated the
// full Radar, which is what made swipes and taps feel late while riding.
void test_map_motion_invalidates_only_the_moved_span() {
  moto_nav_ui_set_reduce_motion(1);
  moto_nav_ui_set_theme(MOTO_UI_THEME_GTA_NIGHT);
  moto_nav_ui_set_phone_connection(MOTO_UI_PHONE_ONLINE);
  moto_ui_state_t state = base_state();
  state.route_point_count = 3;
  state.route_points[0] = {233, 180};
  state.route_points[1] = {233, 233};
  state.route_points[2] = {233, 300};
  state.road_point_count = 0;
  state.road_polyline_count = 0;
  state.building_point_count = 0;
  state.building_footprint_count = 0;
  moto_nav_ui_set_state(&state);
  // Settle the page dots and the 920 ms connection confirmation first.
  pump(1'200);
  const std::uint64_t before = frame_hash();

  for (int i = 0; i < state.route_point_count; ++i) {
    state.route_points[i].x = static_cast<std::int16_t>(
        state.route_points[i].x + 12);
  }
  moto_nav_ui_set_reduce_motion(0);
  moto_nav_ui_set_state(&state);
  // The state setter also updates the road-name and maneuver labels. Those
  // are settled before the measured frame so this test sees map motion only.
  lv_refr_now(lv_display_get_default());

  std::vector<lv_area_t> areas;
  flush_log = &areas;
  pump(5);  // one 25 ms interpolation tick
  flush_log = nullptr;

  CHECK(!areas.empty());
  CHECK(frame_hash() != before);
  bool saw_route_span = false;
  for (const lv_area_t& area : areas) {
    const int32_t width = area.x2 - area.x1 + 1;
    const int32_t height = area.y2 - area.y1 + 1;
    CHECK(width <= 140);
    CHECK(height <= 220);
    if (width <= 140 && height <= 220) saw_route_span = true;
  }
  CHECK(saw_route_span);
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
  // Partition 255 points over 40 polylines; the last span keeps the
  // remainder and the first-point index stays inside the uint8 span field.
  const int span_points = MOTO_UI_ROAD_POINT_CAPACITY /
                          MOTO_UI_ROAD_POLYLINE_CAPACITY;
  for (int i = 0; i < state.road_polyline_count; ++i) {
    const int first = i * span_points;
    const int count = i + 1 == state.road_polyline_count
                          ? MOTO_UI_ROAD_POINT_CAPACITY - first
                          : span_points;
    state.road_polylines[i] = {
        static_cast<std::uint8_t>(first),
        static_cast<std::uint8_t>(count),
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

// Dev-only review sheet: one PPM per theme and page, so a design change can be
// inspected without flashing the board. Enabled with MOTO_UI_DUMP_DIR; never
// runs in CI.
void write_ppm_to(const char *path) {
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

void dump_review_frames(const char *dir) {
  struct ThemeCase {
    const char *name;
    moto_ui_theme_t theme;
  };
  const ThemeCase themes[] = {
      {"official", MOTO_UI_THEME_OFFICIAL},
      {"vice-day", MOTO_UI_THEME_GTA_DAY},
      {"vice-night", MOTO_UI_THEME_GTA_NIGHT},
  };
  const char *page_names[] = {"nav", "perf", "lean", "speedg", "compass", "music"};
  moto_nav_ui_set_reduce_motion(1);
  moto_nav_ui_set_phone_connection(MOTO_UI_PHONE_ONLINE);
  // The battery ring is reviewed on every page: 62% on the battery.
  moto_nav_ui_set_battery(MOTO_UI_POWER_BATTERY, 62);
  pump(300);

  moto_ui_state_t state = base_state();
  state.has_attitude = 1;
  state.lean_deg = 42.0F;
  state.longitudinal_g = 0.4F;
  state.lateral_g = 0.6F;
  state.route_progress_percent = 25;
  state.distance_to_maneuver_m = 120;
  state.remaining_distance_m = 2'400;
  state.remaining_time_s = 305;
  state.speed_kph = 78;
  state.heading_deg = 128;
  fill_review_city(state);
  // A saved home inside the radar, so the safehouse blip is reviewed too.
  state.has_home = 1;
  state.home_x = 150;
  state.home_y = 150;

  moto_music_state_t music{};
  music.connected = 1;
  music.playing = 1;
  music.like_available = 1;
  music.source_name = "NetEase";
  music.track_title = "公路之歌";
  music.artist_name = "新裤子";
  moto_nav_ui_set_music_state(&music);

  for (const ThemeCase &theme : themes) {
    moto_nav_ui_set_theme(theme.theme);
    for (int page = 0; page < MOTO_UI_PAGE_COUNT; ++page) {
      state.page = static_cast<moto_ui_page_t>(page);
      moto_nav_ui_set_page(static_cast<moto_ui_page_t>(page));
      if (page == MOTO_UI_PAGE_SPEED && theme.theme != MOTO_UI_THEME_OFFICIAL) {
        // Speed·G: a short synthetic ride so the envelope has a shape —
        // braking, a left and a right corner, then the review sample.
        moto_ui_state_t ride = state;
        const float samples[][2] = {{0.0F, -0.9F}, {-0.8F, -0.3F},
                                    {-1.0F, 0.0F}, {-0.6F, 0.3F},
                                    {0.0F, 0.45F}, {0.7F, 0.25F},
                                    {1.05F, 0.0F}, {0.8F, -0.4F}};
        for (const auto &sample : samples) {
          ride.lateral_g = sample[0];
          ride.longitudinal_g = sample[1];
          for (int i = 0; i < 40; ++i) {
            moto_nav_ui_set_state(&ride);
            pump(2);
          }
        }
      }
      moto_nav_ui_set_state(&state);
      pump(80);
      char path[512];
      std::snprintf(path, sizeof(path), "%s/%s-%s.ppm", dir, theme.name,
                    page_names[page]);
      write_ppm_to(path);
    }
  }

  // The lean page through a corner: upright, mid-corner, past the hot mark,
  // and back the other way, so the held peaks show on both sides.
  {
    moto_nav_ui_set_page(MOTO_UI_PAGE_LEAN);
    moto_nav_ui_set_theme(MOTO_UI_THEME_GTA_NIGHT);
    moto_ui_state_t corner = state;
    corner.page = MOTO_UI_PAGE_LEAN;
    const float leans[] = {0.0F, 24.0F, 52.0F, -31.0F};
    for (float lean : leans) {
      corner.lean_deg = lean;
      moto_nav_ui_set_state(&corner);
      pump(80);
      char path[512];
      std::snprintf(path, sizeof(path), "%s/lean-at-%+03d.ppm", dir,
                    static_cast<int>(lean));
      write_ppm_to(path);
    }
  }
  // The no-IMU lean page is a distinct design state: the poster must still be
  // there, with the NO IMU plate instead of a number.
  state.page = MOTO_UI_PAGE_LEAN;
  state.has_attitude = 0;
  moto_nav_ui_set_page(MOTO_UI_PAGE_LEAN);
  for (const ThemeCase &theme : themes) {
    moto_nav_ui_set_theme(theme.theme);
    moto_nav_ui_set_state(&state);
    pump(80);
    char path[512];
    std::snprintf(path, sizeof(path), "%s/%s-lean-no-imu.ppm", dir,
                  theme.name);
    write_ppm_to(path);
  }
  // Empty / link states are what the rider sees most of the time before a
  // destination exists; dump them with the same theme so they are reviewed
  // rather than assumed.
  moto_nav_ui_set_page(MOTO_UI_PAGE_NAVIGATION);
  moto_ui_state_t empty = base_state();
  empty.mode = MOTO_UI_ACQUIRING_FIX;
  empty.route_point_count = 0;
  empty.road_point_count = 0;
  empty.road_polyline_count = 0;
  empty.building_point_count = 0;
  empty.building_footprint_count = 0;
  // Home far beyond the radar: the blip must ride the rim at its bearing.
  empty.has_home = 1;
  empty.home_x = 233 - 900;
  empty.home_y = 233 + 500;
  moto_nav_ui_set_state(&empty);
  // Every link state, in both papers: offline, linking, the linked flash
  // (inside its 920 ms hold), ready, and building a route.
  const struct {
    const char *name;
    moto_ui_phone_connection_t connection;
    std::uint8_t route_in_flight;
    int cycles;
  } lifecycle[] = {
      {"offline", MOTO_UI_PHONE_OFFLINE, 0, 120},
      {"connecting", MOTO_UI_PHONE_CONNECTING, 0, 120},
      {"connected", MOTO_UI_PHONE_ONLINE, 0, 60},
      {"ready", MOTO_UI_PHONE_ONLINE, 0, 400},
      {"planning", MOTO_UI_PHONE_ONLINE, 1, 120},
  };
  const ThemeCase link_themes[] = {{"day", MOTO_UI_THEME_GTA_DAY},
                                   {"night", MOTO_UI_THEME_GTA_NIGHT}};
  for (const ThemeCase &theme : link_themes) {
    moto_nav_ui_set_theme(theme.theme);
    for (const auto &step : lifecycle) {
      moto_nav_ui_set_phone_connection(step.connection);
      empty.route_request_in_flight = step.route_in_flight;
      moto_nav_ui_set_state(&empty);
      pump(step.cycles);
      char path[512];
      std::snprintf(path, sizeof(path), "%s/life-%s-%s.ppm", dir, theme.name,
                    step.name);
      write_ppm_to(path);
    }
    // Drop the link so the next paper starts from offline again.
    empty.route_request_in_flight = 0;
    moto_nav_ui_set_phone_connection(MOTO_UI_PHONE_OFFLINE);
    moto_nav_ui_set_state(&empty);
    pump(120);
  }

  // The boot sequence is a timed series of cuts: dump each stage.
  const struct {
    const char *name;
    moto_ui_theme_t theme;
  } boot_themes[] = {{"night", MOTO_UI_THEME_GTA_NIGHT},
                     {"day", MOTO_UI_THEME_GTA_DAY}};
  const int boot_marks_ms[] = {40, 200, 450, 700, 1200};
  for (const auto &boot : boot_themes) {
    moto_nav_ui_set_boot_theme(boot.theme);
    moto_nav_ui_show_boot_screen();
    int elapsed = 0;
    for (int mark : boot_marks_ms) {
      pump((mark - elapsed) / 5);
      elapsed = mark;
      char path[512];
      std::snprintf(path, sizeof(path), "%s/boot-%s-%04d.ppm", dir,
                    boot.name, mark);
      write_ppm_to(path);
    }
  }
  moto_nav_ui_set_boot_theme(MOTO_UI_THEME_GTA_NIGHT);
  moto_nav_ui_create();
  moto_nav_ui_set_reduce_motion(1);
  moto_nav_ui_set_phone_connection(MOTO_UI_PHONE_ONLINE);

  moto_nav_ui_set_theme(MOTO_UI_THEME_GTA_NIGHT);
  moto_nav_ui_set_page(MOTO_UI_PAGE_NAVIGATION);

  // The two other ring states, on the Lean page of each family's look.
  moto_nav_ui_set_state(&state);
  const struct {
    const char *name;
    moto_ui_power_source_t source;
    std::uint8_t percent;
  } batteries[] = {
      {"usb", MOTO_UI_POWER_EXTERNAL, 100},
      {"low", MOTO_UI_POWER_BATTERY, 15},
  };
  for (const auto &battery : batteries) {
    moto_nav_ui_set_battery(battery.source, battery.percent);
    for (const ThemeCase &theme : themes) {
      moto_nav_ui_set_theme(theme.theme);
      moto_nav_ui_set_page(MOTO_UI_PAGE_LEAN);
      pump(1100);
      char path[512];
      std::snprintf(path, sizeof(path), "%s/battery-%s-%s.ppm", dir,
                    battery.name, theme.name);
      write_ppm_to(path);
    }
  }
  moto_nav_ui_set_battery(MOTO_UI_POWER_UNKNOWN, 0);
  moto_nav_ui_set_theme(MOTO_UI_THEME_GTA_NIGHT);
  moto_nav_ui_set_page(MOTO_UI_PAGE_NAVIGATION);
  pump(60);
}

void render_demo_frame() {
  // Do not inherit the previous test's theme or page: the dump is a review
  // artefact, and a silent dependency on test order would make it lie.
  moto_nav_ui_set_theme(MOTO_UI_THEME_GTA_NIGHT);
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

// The panel once booted to a black screen: pages were transparent layers over
// the screen's own black background, so a page that failed to show its content
// was indistinguishable from a dead instrument. Every page now paints its own
// surface, and this test is the guard for that class of bug.
void test_every_page_paints_its_own_surface() {
  moto_nav_ui_set_reduce_motion(1);
  moto_nav_ui_set_phone_connection(MOTO_UI_PHONE_ONLINE);
  moto_ui_state_t state = base_state();
  state.has_attitude = 1;
  state.lean_deg = 12.0F;
  state.route_progress_percent = 40;
  fill_route(state, 12);
  fill_streets(state);
  pump(300);

  const std::uint16_t bezel = rgb565(0x00, 0x00, 0x00);
  const moto_ui_theme_t themes[] = {MOTO_UI_THEME_GTA_DAY,
                                    MOTO_UI_THEME_GTA_NIGHT};
  for(const moto_ui_theme_t theme : themes) {
    moto_nav_ui_set_theme(theme);
    for(int page = 0; page < MOTO_UI_PAGE_COUNT; ++page) {
      state.page = static_cast<moto_ui_page_t>(page);
      moto_nav_ui_set_state(&state);
      capture();
      // The disc is 62% of the square canvas; anything under a third means the
      // page failed to paint and the screen is showing through.
      int painted = 0;
      for(int y = 0; y < kHeight; y += 4) {
        for(int x = 0; x < kWidth; x += 4) {
          if(pixel_at(x, y) != bezel) ++painted;
        }
      }
      const int sampled = (kWidth / 4) * (kHeight / 4);
      CHECK(painted * 100 / sampled > 30);
      CHECK(pixel_at(kWidth / 2, kHeight / 2) != bezel);
    }
  }
  moto_nav_ui_set_theme(MOTO_UI_THEME_GTA_DAY);
  moto_nav_ui_set_page(MOTO_UI_PAGE_NAVIGATION);
}


// Geometry gate: every visible object in every page is registered in
// tests/native/ui_geometry_manifest.txt. The manifest is the registry; a new
// object, a moved box, a revived scroll flag or a changed style all produce a
// diff and fail the suite until the manifest is deliberately updated with
// MOTO_UI_GEOMETRY_WRITE=1. Object paths are child indices, not LVGL's global
// object names, so they are stable across rebuilds.
void collect_visible(lv_obj_t *obj, const std::string &path,
                     std::string &out) {
  if (lv_obj_has_flag(obj, LV_OBJ_FLAG_HIDDEN)) return;
  const lv_obj_class_t *cls = lv_obj_get_class(obj);
  lv_area_t area;
  lv_obj_get_coords(obj, &area);
  const char *name = lv_obj_get_name(obj);
  char line[320];
  std::snprintf(
      line, sizeof(line),
      "%s|%s|%s|%d,%d-%d,%d|bg=%06x/opa%d|bd=%06x/%d|opa=%d|r=%d|%s%s%s%s\n",
      path.c_str(),
      cls != nullptr && cls->name != nullptr ? cls->name : "?",
      name != nullptr ? name : "-", area.x1, area.y1, area.x2, area.y2,
      static_cast<unsigned>(lv_color_to_u32(
          lv_obj_get_style_bg_color(obj, LV_PART_MAIN))),
      static_cast<int>(lv_obj_get_style_bg_opa(obj, LV_PART_MAIN)),
      static_cast<unsigned>(lv_color_to_u32(
          lv_obj_get_style_border_color(obj, LV_PART_MAIN))),
      static_cast<int>(lv_obj_get_style_border_width(obj, LV_PART_MAIN)),
      static_cast<int>(lv_obj_get_style_opa(obj, LV_PART_MAIN)),
      static_cast<int>(lv_obj_get_style_radius(obj, LV_PART_MAIN)),
      lv_obj_has_flag(obj, LV_OBJ_FLAG_SCROLLABLE) ? "S" : "-",
      lv_obj_has_flag(obj, LV_OBJ_FLAG_CLICKABLE) ? "C" : "-",
      lv_obj_has_flag(obj, LV_OBJ_FLAG_GESTURE_BUBBLE) ? "G" : "-",
      lv_obj_has_flag(obj, LV_OBJ_FLAG_EVENT_BUBBLE) ? "E" : "-");
  out += line;
  for (uint32_t i = 0; i < lv_obj_get_child_count(obj); ++i) {
    collect_visible(lv_obj_get_child(obj, i),
                    path + "." + std::to_string(i), out);
  }
}

std::string build_geometry_manifest() {
  moto_nav_ui_set_reduce_motion(1);
  moto_nav_ui_set_theme(MOTO_UI_THEME_GTA_DAY);
  moto_nav_ui_set_phone_connection(MOTO_UI_PHONE_ONLINE);
  moto_nav_ui_set_lean_flags(0, 0, 0, 0);
  moto_music_state_t music{};
  music.connected = 1;
  music.playing = 1;
  music.like_available = 1;
  music.source_name = "APPLE MUSIC";
  music.track_title = "NIGHT RIDE";
  music.artist_name = "PHONE NOW PLAYING";
  moto_nav_ui_set_music_state(&music);

  moto_ui_state_t state = base_state();
  state.page = MOTO_UI_PAGE_NAVIGATION;
  state.has_attitude = 1;
  state.lean_deg = 12.0F;
  state.longitudinal_g = 0.2F;
  state.lateral_g = 0.1F;
  fill_route(state, 12);
  fill_streets(state);

  std::string manifest;
  for (int page = 0; page < MOTO_UI_PAGE_COUNT; ++page) {
    state.page = static_cast<moto_ui_page_t>(page);
    moto_nav_ui_set_state(&state);
    (void)capture();
    lv_obj_t *root = moto_nav_ui_page_root(static_cast<moto_ui_page_t>(page));
    CHECK(root != nullptr);
    if (root == nullptr) continue;
    manifest += "page " + std::to_string(page) + "\n";
    collect_visible(root, "p", manifest);
  }
  state.page = MOTO_UI_PAGE_NAVIGATION;
  moto_nav_ui_set_state(&state);
  moto_nav_ui_set_theme(MOTO_UI_THEME_GTA_NIGHT);
  (void)capture();
  return manifest;
}

void test_visible_objects_are_registered() {
  const std::string actual = build_geometry_manifest();
  if (std::getenv("MOTO_UI_GEOMETRY_WRITE") != nullptr) {
    std::ofstream out(MOTO_UI_GEOMETRY_MANIFEST);
    out << actual;
    std::cout << "wrote " << MOTO_UI_GEOMETRY_MANIFEST << '\n';
    return;
  }
  std::ifstream in(MOTO_UI_GEOMETRY_MANIFEST);
  CHECK(in.good());
  if (!in.good()) return;
  std::stringstream buffer;
  buffer << in.rdbuf();
  const std::string expected = buffer.str();
  if (expected == actual) return;
  std::cerr << "geometry manifest changed; review the diff and regenerate by\n"
            << "  running the same binary with MOTO_UI_GEOMETRY_WRITE=1\n";
  std::istringstream expected_stream(expected);
  std::istringstream actual_stream(actual);
  std::string e;
  std::string a;
  int line = 0;
  while (std::getline(expected_stream, e) &&
         std::getline(actual_stream, a)) {
    ++line;
    if (e != a) {
      std::cerr << "first diff line " << line << "\nexpected: " << e
                << "\nactual:   " << a << '\n';
      break;
    }
  }
  CHECK(false);
}

// The Lean page used to trap the pager: its transparent full-screen
// calibration overlay kept LV_OBJ_FLAG_SCROLLABLE, so a horizontal drag
// scrolled the overlay instead of producing a page gesture. Entering and
// leaving the page twenty times is the regression the bench card repeats by
// hand. The overlay is gone; the page container owns the long press.
void test_lean_page_survives_twenty_swipes() {
  moto_nav_ui_set_reduce_motion(1);
  lv_test_indev_create_all();
  moto_nav_ui_set_page(MOTO_UI_PAGE_PERFORMANCE);
  capture();
  for(int i = 0; i < 20; ++i) {
    swipe(320, 180);  // performance -> lean
    CHECK(moto_nav_ui_get_page() == MOTO_UI_PAGE_LEAN);
    swipe(180, 320);  // lean -> performance
    CHECK(moto_nav_ui_get_page() == MOTO_UI_PAGE_PERFORMANCE);
  }
  lv_test_indev_delete_all();
  pump(1100);
}

void test_one_swipe_moves_one_page_even_after_a_rebuild() {
  moto_nav_ui_set_reduce_motion(1);
  lv_test_indev_create_all();

  moto_nav_ui_set_page(MOTO_UI_PAGE_NAVIGATION);
  capture();
  swipe(320, 180);
  // Speed·G follows navigation (DESIGN_V2 §2.2).
  CHECK(moto_nav_ui_get_page() == MOTO_UI_PAGE_SPEED);

  // create() re-adds the screen callbacks on every call. A stacked handler
  // would move two pages here while every object-level assertion stayed green.
  moto_nav_ui_create();
  CHECK(moto_nav_ui_get_page() == MOTO_UI_PAGE_NAVIGATION);
  capture();
  swipe(320, 180);
  CHECK(moto_nav_ui_get_page() == MOTO_UI_PAGE_SPEED);

  // And the reverse direction still moves exactly one page back.
  swipe(180, 320);
  CHECK(moto_nav_ui_get_page() == MOTO_UI_PAGE_NAVIGATION);

  lv_test_indev_delete_all();
  // The swipe reveals the page dots for five seconds; leave the shared UI
  // settled, because the remaining tests compare exact frames.
  pump(1100);
}


// The battery ring sits on the bezel outside the band, on every page of both
// interfaces (DESIGN_V2 §2.9): green from twelve o'clock clockwise over a dim
// track, red at 20% and below, and on USB the charging bolt in a gap at
// twelve. Samples sit on the ring's centre line, r = 227.
std::uint16_t to565(lv_color_t color) { return lv_color_to_u16(color); }

void test_battery_ring_follows_the_power_reading() {
  moto_nav_ui_set_reduce_motion(1);
  moto_nav_ui_set_phone_connection(MOTO_UI_PHONE_ONLINE);
  moto_nav_ui_set_theme(MOTO_UI_THEME_GTA_NIGHT);
  moto_nav_ui_set_page(MOTO_UI_PAGE_NAVIGATION);
  pump(1100);
  const Frame before = capture();
  const std::uint16_t bezel_right = pixel_at(460, 233);
  const std::uint16_t before_band = pixel_at(450, 233);
  CHECK(bezel_right == rgb565(0x00, 0x00, 0x00));
  lv_obj_t *ring = lv_obj_find_by_name(lv_layer_top(), "moto-battery-ring");
  CHECK(ring != nullptr);

  const lv_color_t green = lv_color_hex(0x30D158);
  const lv_color_t red = lv_color_hex(0xFF453A);
  const lv_color_t black = lv_color_hex(0x000000);
  const std::uint16_t green_track = to565(lv_color_mix(green, black, 77));
  const std::uint16_t red_track = to565(lv_color_mix(red, black, 77));

  // 60% on the battery: three o'clock (25%) and six (50%) are filled, nine
  // (75%) is track.
  moto_nav_ui_set_battery(MOTO_UI_POWER_BATTERY, 60);
  pump(60);
  CHECK(pixel_at(460, 233) == to565(green));
  CHECK(pixel_at(233, 460) == to565(green));
  CHECK(pixel_at(6, 233) == green_track);
  // The ring is the bezel, r 224..232 out to the glass edge, with a 2 px
  // black seam (r 222, 223) against the band. The band itself is untouched.
  CHECK(pixel_at(457, 233) == to565(green));
  CHECK(pixel_at(465, 233) == to565(green));
  CHECK(pixel_at(456, 233) == rgb565(0x00, 0x00, 0x00));
  CHECK(pixel_at(455, 233) == rgb565(0x00, 0x00, 0x00));
  CHECK(pixel_at(450, 233) == before_band);

  // A swipe still reaches the page underneath.
  lv_test_indev_create_all();
  swipe(320, 180);
  CHECK(moto_nav_ui_get_page() == MOTO_UI_PAGE_SPEED);
  swipe(180, 320);
  CHECK(moto_nav_ui_get_page() == MOTO_UI_PAGE_NAVIGATION);
  lv_test_indev_delete_all();
  pump(1100);
  // The ring is on every page: the Speed·G page carries it too.
  moto_nav_ui_set_page(MOTO_UI_PAGE_LEAN);
  pump(60);
  CHECK(pixel_at(460, 233) == to565(green));
  moto_nav_ui_set_page(MOTO_UI_PAGE_NAVIGATION);
  pump(1100);

  // 15%: red, and three o'clock is now track; one o'clock (8%) is filled.
  moto_nav_ui_set_battery(MOTO_UI_POWER_BATTERY, 15);
  pump(60);
  CHECK(pixel_at(460, 233) == red_track);
  CHECK(has_color_within(347, 36, to565(red), 1));

  // USB with no battery fitted: a full green ring, open at twelve round the
  // bolt. Low charge does not turn a charging ring red.
  moto_nav_ui_set_battery(MOTO_UI_POWER_EXTERNAL, 100);
  pump(60);
  CHECK(pixel_at(460, 233) == to565(green));
  CHECK(pixel_at(6, 233) == to565(green));
  CHECK(has_color_within(233, 6, to565(green), 1));
  CHECK(pixel_at(240, 6) == rgb565(0x00, 0x00, 0x00));
  CHECK(pixel_at(226, 6) == rgb565(0x00, 0x00, 0x00));
  moto_nav_ui_set_battery(MOTO_UI_POWER_EXTERNAL, 10);
  pump(60);
  CHECK(has_color_within(347, 36, to565(green), 1));
  CHECK(pixel_at(460, 233) == green_track);

  // Both families: the Original tree carries the same ring.
  moto_nav_ui_set_theme(MOTO_UI_THEME_OFFICIAL);
  pump(1100);
  CHECK(has_color_within(347, 36, to565(green), 1));
  CHECK(has_color_within(233, 6, to565(green), 1));
  moto_nav_ui_set_theme(MOTO_UI_THEME_GTA_NIGHT);
  pump(1100);

  // No reading, no ring, and nothing left behind.
  moto_nav_ui_set_battery(MOTO_UI_POWER_UNKNOWN, 0);
  pump(60);
  CHECK(pixel_at(460, 233) == bezel_right);
  CHECK(capture().hash == before.hash);

  // A second create() keeps the ring and its reading.
  moto_nav_ui_set_battery(MOTO_UI_POWER_EXTERNAL, 10);
  moto_nav_ui_create();
  moto_nav_ui_set_reduce_motion(1);
  moto_nav_ui_set_phone_connection(MOTO_UI_PHONE_ONLINE);
  moto_nav_ui_set_theme(MOTO_UI_THEME_GTA_NIGHT);
  pump(1100);
  CHECK(lv_obj_find_by_name(lv_layer_top(), "moto-battery-ring") == ring);
  CHECK(has_color_within(347, 36, to565(green), 1));
  moto_nav_ui_set_battery(MOTO_UI_POWER_UNKNOWN, 0);
  pump(60);
  CHECK(pixel_at(460, 233) == bezel_right);
}

// Six MapRoadClass values cross the wire; each must keep its own stroke. One
// horizontal road per class, sampled on its centre line, in both GTA papers.
void test_every_road_class_has_its_own_stroke() {
  moto_nav_ui_set_reduce_motion(1);
  moto_nav_ui_set_phone_connection(MOTO_UI_PHONE_ONLINE);
  moto_nav_ui_set_page(MOTO_UI_PAGE_NAVIGATION);
  moto_ui_state_t state = base_state();
  state.route_point_count = 2;
  state.route_points[0] = {40, 440};
  state.route_points[1] = {60, 440};
  state.building_point_count = 0;
  state.building_footprint_count = 0;
  state.road_point_count = 12;
  state.road_polyline_count = 6;
  for (int i = 0; i < 6; ++i) {
    // Rows stay in the map above the rider: the turn glyph and distance own
    // the band below it.
    const std::int16_t y = static_cast<std::int16_t>(104 + i * 24);
    state.road_points[i * 2] = {150, y};
    state.road_points[i * 2 + 1] = {320, y};
    state.road_polylines[i] = {static_cast<std::uint8_t>(i * 2), 2,
                               static_cast<std::uint8_t>(i)};
  }
  state.map_scene_revision = 91;

  struct Paper {
    moto_ui_theme_t theme;
    std::uint16_t colors[6];
  };
  const Paper papers[] = {
      {MOTO_UI_THEME_GTA_DAY,
       {rgb565(0xF3, 0x4D, 0xAA), rgb565(0xF7, 0xF5, 0xEF),
        rgb565(0xEF, 0xEC, 0xE4), rgb565(0xD6, 0xD2, 0xC8),
        rgb565(0xB4, 0xB0, 0xA6), rgb565(0x8E, 0x8A, 0x82)}},
      {MOTO_UI_THEME_GTA_NIGHT,
       {rgb565(0xF3, 0x4D, 0xAA), rgb565(0xF2, 0xEE, 0xE4),
        rgb565(0xD6, 0xD2, 0xC8), rgb565(0xB4, 0xB0, 0xA6),
        rgb565(0x8E, 0x8A, 0x82), rgb565(0x5E, 0x5B, 0x55)}},
  };
  for (const Paper &paper : papers) {
    moto_nav_ui_set_theme(paper.theme);
    moto_nav_ui_set_state(&state);
    capture();
    for (int i = 0; i < 6; ++i) {
      // x=170 stays clear of the rider marker and the HUD numerals.
      CHECK(pixel_at(170, 104 + i * 24) == paper.colors[i]);
    }
  }
  moto_nav_ui_set_theme(MOTO_UI_THEME_GTA_NIGHT);
}

int count_pixels(std::uint16_t color) {
  int count = 0;
  for (int y = 0; y < kHeight; ++y) {
    for (int x = 0; x < kWidth; ++x) {
      if (pixel_at(x, y) == color) ++count;
    }
  }
  return count;
}

// Speed·G: the live dot follows the horizontal G, the envelope keeps the most
// the ride reached after the dot has gone back to the middle, a two-second
// hold clears it, and without attitude the dot is not drawn at all.
void ride_gg(moto_ui_state_t &state, float lateral, float longitudinal,
             int samples) {
  state.lateral_g = lateral;
  state.longitudinal_g = longitudinal;
  for (int i = 0; i < samples; ++i) {
    moto_nav_ui_set_state(&state);
    pump(2);
  }
}

// Pink inside the friction circle only: the live "x.xx G" label above it is
// pink too, and its pixel count changes with the digits.
int count_pink_in_plot() {
  const std::uint16_t pink = rgb565(0xF3, 0x4D, 0xAA);
  const int cx = kWidth / 2;
  const int cy = kHeight / 2 + 3;
  int count = 0;
  for (int y = cy - 132; y <= cy + 132; ++y) {
    for (int x = cx - 132; x <= cx + 132; ++x) {
      if ((x - cx) * (x - cx) + (y - cy) * (y - cy) > 132 * 132) continue;
      if (pixel_at(x, y) == pink) ++count;
    }
  }
  return count;
}

void test_speed_g_page_plots_g_and_keeps_the_envelope() {
  moto_nav_ui_create();
  moto_nav_ui_set_reduce_motion(1);
  moto_nav_ui_set_phone_connection(MOTO_UI_PHONE_ONLINE);
  moto_nav_ui_set_theme(MOTO_UI_THEME_GTA_NIGHT);
  moto_ui_state_t state = base_state();
  state.page = MOTO_UI_PAGE_SPEED;
  state.has_attitude = 1;
  moto_nav_ui_set_page(MOTO_UI_PAGE_SPEED);
  ride_gg(state, 0.0F, 0.0F, 20);
  lv_obj_t *dot = lv_obj_find_by_name(lv_screen_active(), "moto-gg-dot");
  CHECK(dot != nullptr);
  if (dot == nullptr) return;
  capture();
  const int pink_at_rest = count_pink_in_plot();
  CHECK(pink_at_rest == 0);

  // A right-hand sweeper: lateral G to the right, a little drive and brake.
  ride_gg(state, 1.0F, 0.45F, 60);
  ride_gg(state, 1.0F, 0.0F, 60);
  ride_gg(state, 1.0F, -0.45F, 60);
  capture();
  CHECK(!lv_obj_has_flag(dot, LV_OBJ_FLAG_HIDDEN));
  const int32_t dot_x = lv_obj_get_x(dot) + lv_obj_get_width(dot) / 2;
  CHECK(dot_x > kWidth / 2 + 60);  // 1 g is 85 px right of centre
  CHECK(count_pink_in_plot() > 40);

  // Back upright: the dot returns, the envelope stays.
  ride_gg(state, 0.0F, 0.0F, 120);
  capture();
  const int32_t settled_x = lv_obj_get_x(dot) + lv_obj_get_width(dot) / 2;
  CHECK(std::abs(settled_x - kWidth / 2) < 6);
  CHECK(count_pink_in_plot() > 40);

  // A two-second hold clears the envelope.
  lv_obj_t *page = lv_obj_find_by_name(lv_screen_active(), "moto-gg-page");
  CHECK(page != nullptr);
  if (page != nullptr) {
    lv_obj_send_event(page, LV_EVENT_PRESSED, nullptr);
    pump(420);
    lv_obj_send_event(page, LV_EVENT_RELEASED, nullptr);
    ride_gg(state, 0.0F, 0.0F, 10);
    capture();
    CHECK(count_pink_in_plot() == 0);
  }

  // No IMU: no dot, the NO IMU plate instead.
  state.has_attitude = 0;
  ride_gg(state, 0.0F, 0.0F, 10);
  capture();
  CHECK(lv_obj_has_flag(dot, LV_OBJ_FLAG_HIDDEN));
  // Leave a fresh default tree: the tests after this one compare frames
  // across a rebuild, and create() resets the theme this test chose.
  moto_nav_ui_create();
}

int g_page_requests = 0;
void count_page_request(moto_ui_page_t page, void *) {
  ++g_page_requests;
  moto_nav_ui_set_page(page);
}

// OFFICIAL is the interface the fork started from, not a palette: a different
// tree, four pages, white route on true black. Switching to it and back must
// keep the rider where they were and keep every callback, because the phone
// can change the theme mid-ride.
void test_official_theme_swaps_in_the_classic_interface() {
  moto_nav_ui_set_reduce_motion(1);
  moto_nav_ui_set_phone_connection(MOTO_UI_PHONE_ONLINE);
  moto_nav_ui_set_theme(MOTO_UI_THEME_GTA_NIGHT);
  moto_nav_ui_set_page(MOTO_UI_PAGE_NAVIGATION);
  moto_ui_state_t state = base_state();
  fill_route(state, 12);
  fill_streets(state);
  moto_nav_ui_set_state(&state);
  pump(1100);
  const Frame gta = capture();
  CHECK(lv_obj_find_by_name(lv_screen_active(), "moto-compass-north") !=
        nullptr);

  moto_nav_ui_set_theme(MOTO_UI_THEME_OFFICIAL);
  pump(1100);
  const Frame classic = capture();
  CHECK(classic.hash != gta.hash);
  // The GTA furniture left with its tree; the corner is the upstream black,
  // not the GTA bezel.
  CHECK(lv_obj_find_by_name(lv_screen_active(), "moto-compass-north") ==
        nullptr);
  CHECK(pixel_at(4, 4) == rgb565(0x05, 0x06, 0x07));
  // The snapshot was replayed into the new tree: the white route is on screen
  // and the "connected" flash did not fire for a link that never dropped.
  CHECK(count_pixels(rgb565(0xF3, 0xF4, 0xEF)) > 200);
  moto_nav_ui_set_state(&state);
  CHECK(capture().hash == classic.hash);

  // Four pages. The two GTA-only pages fall back to navigation.
  moto_nav_ui_set_page(MOTO_UI_PAGE_PERFORMANCE);
  CHECK(moto_nav_ui_get_page() == MOTO_UI_PAGE_NAVIGATION);
  moto_nav_ui_set_page(MOTO_UI_PAGE_LEAN);
  CHECK(moto_nav_ui_get_page() == MOTO_UI_PAGE_NAVIGATION);

  // A swipe walks the classic order, and the page callback registered before
  // the switch still receives it.
  g_page_requests = 0;
  moto_nav_ui_set_page_change_callback(count_page_request, nullptr);
  moto_nav_ui_set_theme(MOTO_UI_THEME_GTA_NIGHT);
  moto_nav_ui_set_theme(MOTO_UI_THEME_OFFICIAL);
  lv_test_indev_create_all();
  capture();
  swipe(320, 180);
  CHECK(g_page_requests == 1);
  CHECK(moto_nav_ui_get_page() == MOTO_UI_PAGE_SPEED);
  swipe(180, 320);
  CHECK(moto_nav_ui_get_page() == MOTO_UI_PAGE_NAVIGATION);
  swipe(180, 320);
  CHECK(moto_nav_ui_get_page() == MOTO_UI_PAGE_MUSIC);
  swipe(320, 180);
  CHECK(moto_nav_ui_get_page() == MOTO_UI_PAGE_NAVIGATION);
  lv_test_indev_delete_all();
  moto_nav_ui_set_page_change_callback(nullptr, nullptr);

  // Back to GTA: the same poster frame, rebuilt from the replayed state.
  moto_nav_ui_set_theme(MOTO_UI_THEME_GTA_NIGHT);
  pump(1100);
  CHECK(capture().hash == gta.hash);
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
  // created, so settle it before moto_nav_ui_create(). The boot palette is the
  // persisted theme's paper, not the navigation map's green field.
  moto_nav_ui_set_boot_theme(MOTO_UI_THEME_GTA_DAY);
  moto_nav_ui_show_boot_screen();
  // First frame: nothing but the bezel, committed before the panel lights.
  pump(4);
  CHECK(pixel_at(233, 233) == rgb565(0x00, 0x00, 0x00));
  pump(256);
  const Frame settled_boot = capture();
  // The GTA III loading screen, fully cut in: panel grid, logo, full bar.
  lv_obj_t *grid = lv_obj_find_by_name(lv_screen_active(), "moto-boot-grid");
  lv_obj_t *bar = lv_obj_find_by_name(lv_screen_active(), "moto-boot-bar");
  CHECK(grid != nullptr && !lv_obj_has_flag(grid, LV_OBJ_FLAG_HIDDEN));
  CHECK(bar != nullptr && !lv_obj_has_flag(bar, LV_OBJ_FLAG_HIDDEN));
  if (bar != nullptr) {
    CHECK(lv_obj_get_width(lv_obj_get_child(bar, 0)) ==
          lv_obj_get_width(bar) - 6);
  }
  // Outside the glass is bezel; by day the box-art gutter is black ink.
  CHECK(pixel_at(2, 2) == rgb565(0x00, 0x00, 0x00));
  CHECK(pixel_at(60, 254) == rgb565(0x11, 0x11, 0x11));
  // By night the gutter is the cover's white. A green field here was the
  // regression that put the old map under the wordmark.
  moto_nav_ui_set_boot_theme(MOTO_UI_THEME_GTA_NIGHT);
  moto_nav_ui_show_boot_screen();
  pump(260);
  CHECK(pixel_at(60, 254) == rgb565(0xF2, 0xEE, 0xE4));

  test_frames_are_deterministic();
  test_speed_g_page_plots_g_and_keeps_the_envelope();
  test_rebuilding_the_ui_is_idempotent();
  test_buildings_are_opt_in();
  test_a_building_footprint_interior_is_filled();
  test_the_map_covers_the_bottom_of_the_round_canvas();
  test_the_ring_carries_progress_and_a_north_badge();
  test_a_refused_home_tap_shows_a_hint_and_fades();
  test_day_and_night_themes_render_different_map_colors();
  test_auto_theme_follows_the_ambient_night_flag();
  test_performance_and_lean_pages_render_attitude();
  test_lean_page_without_attitude_keeps_the_poster();
  test_lean_sweep_repaints_inside_the_casing();
  test_lean_samples_between_refreshes_leave_no_residue();
  test_lean_press_durations_dispatch_actions();
  test_lean_status_slot_reports_cal_and_fallback();
  test_clearing_geometry_leaves_no_residue();
  test_music_page_state_and_fallback();
  test_music_text_never_paints_a_box();
  test_home_glyph_is_gated_by_speed();
  test_home_blip_is_clamped_to_the_ring();
  test_phone_lifecycle_renders_deterministically();
  test_demo_mode_visual_contract();
  test_capacity_payloads_render_stably();
  test_motion_frames_interpolate_then_settle_without_residue();
  test_map_motion_invalidates_only_the_moved_span();
  test_diagonal_map_pixels_are_independent_of_partial_buffer_height();
  test_one_swipe_moves_one_page_even_after_a_rebuild();
  test_visible_objects_are_registered();
  test_lean_page_survives_twenty_swipes();
  test_every_page_paints_its_own_surface();
  test_official_theme_swaps_in_the_classic_interface();
  test_every_road_class_has_its_own_stroke();
  test_battery_ring_follows_the_power_reading();
  test_the_launch_clock_reports_each_run();

  // Visual check runs while the instrument is still alive; the terminal
  // screens below delete the whole widget tree.
  if (const char *dump = std::getenv("MOTO_UI_DUMP")) {
    render_demo_frame();
    write_ppm(dump);
  }
  if (const char *dump_dir = std::getenv("MOTO_UI_DUMP_DIR")) {
    dump_review_frames(dump_dir);
  }

  // Terminal screens run last: they tear the full UI down. Leave a hint on
  // screen first: its timer is the one widget-retaining timer a terminal
  // screen used to forget, and a visible hint is the worst case.
  moto_nav_ui_show_home_error();
  pump(10);
  // The boot theme is sticky; restore the one the first capture used.
  moto_nav_ui_set_boot_theme(MOTO_UI_THEME_GTA_DAY);
  moto_nav_ui_show_boot_screen();
  pump(260);
  const Frame boot_again = capture();
  // A second boot screen must repaint the whole poster, not leave the previous
  // tree's pixels behind, so it has to match the first one exactly.
  CHECK(boot_again.hash == settled_boot.hash);

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
