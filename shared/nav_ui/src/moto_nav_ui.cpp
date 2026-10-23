#include "moto_nav_ui.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <type_traits>

#include "lvgl.h"

LV_FONT_DECLARE(moto_font_nav_16);
// GB2312 level 1 (3,755 characters) plus Latin at two sizes. Track titles and
// artist names are phone data, not UI strings: they need a face that can print
// them, and a 32 px CJK glyph is the smallest that still reads at arm's length.
LV_FONT_DECLARE(moto_font_text_40);
LV_FONT_DECLARE(moto_font_text_24);
// Barlow Condensed ExtraBold at four sizes. The device previously used LVGL's
// Montserrat regular, which is too light for a round panel read in daylight;
// DESIGN.md already specifies Barlow Condensed for the phone's display role,
// so the radar now uses the same face at 800 weight.
LV_FONT_DECLARE(moto_font_display_18);
LV_FONT_DECLARE(moto_font_display_24);
LV_FONT_DECLARE(moto_font_display_40);
LV_FONT_DECLARE(moto_font_display_72);
// The hero face: 200 px of the same condensed extra bold. The rider reads one
// number at speed, so that number is printed at poster scale and the rest of
// the page gets out of its way.
LV_FONT_DECLARE(moto_font_hero_176);
// Six glyphs at 132px for the boot wordmark: large enough to be a poster
// headline without scaling a smaller bitmap up.
LV_FONT_DECLARE(moto_font_wordmark_132);
// Digits and the state words only: the run timer is the largest thing the
// firmware draws, so it gets its own face instead of a scaled bitmap.
LV_FONT_DECLARE(moto_font_timer_96);
LV_FONT_DECLARE(lv_font_montserrat_20);
// RAM copies of the display faces with a CJK fallback attached. The generated
// descriptors are const in flash, so they cannot be patched at runtime; a copy
// of the descriptor is ~100 bytes and leaves the glyph data in flash.
lv_font_t moto_font_display_40_cjk;
lv_font_t moto_font_display_24_cjk;

namespace {

constexpr lv_color_t kBlack = LV_COLOR_MAKE(0x05, 0x06, 0x07);
constexpr lv_color_t kWhite = LV_COLOR_MAKE(0xF3, 0xF4, 0xEF);
constexpr lv_color_t kIce = LV_COLOR_MAKE(0xB8, 0xED, 0xF5);
constexpr lv_color_t kGraphite = LV_COLOR_MAKE(0x30, 0x35, 0x39);
// Real surrounding roads need to remain legible on the AMOLED's true-black
// background.  LV_OPA_70 is 70/255 (not 70 percent), which made the previous
// road layer effectively disappear on the physical display.
constexpr lv_color_t kRoadGray = LV_COLOR_MAKE(0x42, 0x47, 0x4B);
constexpr lv_color_t kRoadMajor = LV_COLOR_MAKE(0x62, 0x69, 0x6D);
constexpr lv_color_t kRoadMinor = LV_COLOR_MAKE(0x2B, 0x30, 0x33);
constexpr lv_color_t kBuildingGray = LV_COLOR_MAKE(0x24, 0x29, 0x2C);
constexpr lv_color_t kBuildingLandmark = LV_COLOR_MAKE(0x38, 0x40, 0x44);
constexpr lv_color_t kSoft = LV_COLOR_MAKE(0xAE, 0xB2, 0xB0);
constexpr lv_color_t kAmber = LV_COLOR_MAKE(0xE6, 0xC8, 0x4F);
constexpr lv_color_t kRed = LV_COLOR_MAKE(0xFF, 0x4B, 0x43);

struct MapPalette {
    // Outside the disc: the device bezel. Never a data colour.
    lv_color_t frame;
    // The disc paper on the tool pages. Nav keeps its own map ground.
    lv_color_t ground;
    lv_color_t background;
    lv_color_t road;
    lv_color_t road_major;
    lv_color_t road_minor;
    lv_color_t building;
    lv_color_t building_landmark;
    lv_color_t route;
    lv_color_t route_shadow;
    // Type and marks on the paper.
    lv_color_t ink;
    lv_color_t muted;
    lv_color_t accent;
    lv_color_t turquoise;
    lv_color_t coral;
    // Type that sits on the map instead of the paper.
    lv_color_t map_ink;
    // The halo behind a display numeral: the inverse of `ink`, so the figure
    // separates from whatever it is printed over.
    lv_color_t outline;
    // Filled plates. The fill and the ink that goes on it travel together, so
    // a theme cannot invert one without the other.
    lv_color_t on_accent;
    lv_color_t slab;
    lv_color_t on_slab;
};

constexpr MapPalette kOfficialPalette = {
    kBlack, kBlack, kBlack, kRoadGray, kRoadMajor, kRoadMinor,
    kBuildingGray, kBuildingLandmark, kWhite, kGraphite,
    kWhite, kSoft, LV_COLOR_MAKE(0xF3, 0x4D, 0xAA), kIce, kAmber,
    kWhite, kBlack,
    kBlack, kGraphite, kWhite,
};

// Vice City day: the game's map block as the paper. Green ground, cream roads
// with a black casing, and a magenta rim. Every numeral is ink on cream, which
// is where daylight contrast comes from.
constexpr MapPalette kGtaDayPalette = {
    LV_COLOR_MAKE(0x07, 0x05, 0x0D),  // bezel
    LV_COLOR_MAKE(0xF6, 0xF0, 0xD7),  // ROAD CREAM paper
    LV_COLOR_MAKE(0x1E, 0x7A, 0x46),  // MAP GREEN
    LV_COLOR_MAKE(0xF6, 0xF0, 0xD7),  // ROAD CREAM
    LV_COLOR_MAKE(0xFF, 0xFD, 0xF2),
    LV_COLOR_MAKE(0xCF, 0xE0, 0xBC),  // secondary roads
    LV_COLOR_MAKE(0x1A, 0x6B, 0x3D),  // blocks under the roads
    LV_COLOR_MAKE(0x2A, 0x8B, 0x55),
    LV_COLOR_MAKE(0xF3, 0x4D, 0xAA),  // HOT PINK route
    LV_COLOR_MAKE(0x17, 0x15, 0x1B),  // OUTLINE BLACK casing
    LV_COLOR_MAKE(0x17, 0x15, 0x1B),  // ink
    LV_COLOR_MAKE(0x5C, 0x4A, 0x38),  // muted brown
    LV_COLOR_MAKE(0xF3, 0x4D, 0xAA),  // VICE MAGENTA
    LV_COLOR_MAKE(0x0E, 0x6F, 0x68),  // deep turquoise: legible on cream
    LV_COLOR_MAKE(0xD8, 0x45, 0x1A),  // sunset coral
    LV_COLOR_MAKE(0xF6, 0xF0, 0xD7),  // type on the map is cream, always
    LV_COLOR_MAKE(0xF6, 0xF0, 0xD7),  // halo = paper, the inverse of ink
    LV_COLOR_MAKE(0x17, 0x15, 0x1B),  // ink on a magenta plate
    LV_COLOR_MAKE(0x17, 0x15, 0x1B),  // counter slab
    LV_COLOR_MAKE(0xF6, 0xF0, 0xD7),
};

// Vice City night: midnight purple paper, cream type, the same roads and the
// same magenta. One structure, inverted paper.
constexpr MapPalette kGtaNightPalette = {
    LV_COLOR_MAKE(0x07, 0x05, 0x0D),
    LV_COLOR_MAKE(0x14, 0x10, 0x24),  // MIDNIGHT PURPLE paper
    LV_COLOR_MAKE(0x14, 0x10, 0x24),  // MAP GROUND
    LV_COLOR_MAKE(0xF6, 0xF0, 0xD7),
    LV_COLOR_MAKE(0xFF, 0xFD, 0xF4),
    LV_COLOR_MAKE(0xC9, 0xC6, 0xA8),
    LV_COLOR_MAKE(0x21, 0x18, 0x48),
    LV_COLOR_MAKE(0x2C, 0x21, 0x60),
    LV_COLOR_MAKE(0xFF, 0x5A, 0xCD),  // VICE PINK route
    LV_COLOR_MAKE(0x0A, 0x06, 0x14),
    LV_COLOR_MAKE(0xF6, 0xF0, 0xD7),  // SUNSET CREAM
    LV_COLOR_MAKE(0xB9, 0xB1, 0xC9),
    LV_COLOR_MAKE(0xF3, 0x4D, 0xAA),  // VICE MAGENTA
    LV_COLOR_MAKE(0x42, 0xF5, 0xD7),  // MIAMI TURQUOISE
    LV_COLOR_MAKE(0xFF, 0x8F, 0x5E),  // sunset coral
    LV_COLOR_MAKE(0xF6, 0xF0, 0xD7),  // map type
    LV_COLOR_MAKE(0x07, 0x05, 0x0D),  // halo = bezel, the inverse of ink
    LV_COLOR_MAKE(0x12, 0x0D, 0x1C),  // ink on a magenta plate
    LV_COLOR_MAKE(0x07, 0x05, 0x0D),  // counter slab
    LV_COLOR_MAKE(0xF6, 0xF0, 0xD7),
};

// Vice City frame colours, shared by both themes: the rim is the brand, not a
// theme variable.
constexpr lv_color_t kViceMagenta = LV_COLOR_MAKE(0xF3, 0x4D, 0xAA);
constexpr lv_color_t kOutlineBlack = LV_COLOR_MAKE(0x17, 0x15, 0x1B);

// One resolver for a theme name and the phone's day/night flag. AUTO has no
// palette of its own; the flag is the only thing that can resolve it.
const MapPalette &palette_for_theme(moto_ui_theme_t theme, bool ambient_night) {
    switch(theme) {
        case MOTO_UI_THEME_GTA_DAY: return kGtaDayPalette;
        case MOTO_UI_THEME_GTA_NIGHT: return kGtaNightPalette;
        case MOTO_UI_THEME_AUTO:
            return ambient_night ? kGtaNightPalette : kGtaDayPalette;
        case MOTO_UI_THEME_OFFICIAL:
        default: return kOfficialPalette;
    }
}

// The boot screen is painted before the tree and before the phone can supply
// an ambient flag, so its theme is chosen by the caller from persisted
// settings and kept outside Ui (teardown_ui() would otherwise reset it).
moto_ui_theme_t g_boot_theme = MOTO_UI_THEME_GTA_NIGHT;

constexpr double kPi = 3.14159265358979323846;
constexpr int kDesignWidth = 360;
constexpr std::uint32_t kPageDotsVisibleMs = 5'000;
// Keep LVGL, the UI interpolation timer and IMU presentation on the same
// 40 Hz cadence. The previous 40/33 ms mismatch periodically produced a
// 66 ms visual gap even when both tasks were otherwise keeping up.
constexpr std::uint32_t kRouteMotionFrameMs = 25;
static_assert(LV_DEF_REFR_PERIOD == kRouteMotionFrameMs,
              "LVGL refresh and map motion must use the same cadence");
constexpr std::uint32_t kConnectionSuccessHoldMs = 920;
// 0-100 instrument. The page is a drag strip gauge, not a stats panel: one
// hero numeral, a shift strip that fills as speed climbs to the target, and
// the state spelled out so the rider never has to infer whether the clock is
// running.
constexpr std::uint16_t kPerfTargetKph = 100;
constexpr int kPerfSegmentCount = 12;
// A stop has to look deliberate: braking to 0 for a red light is not a new
// run until the wheels have been still for a moment.
constexpr std::uint32_t kPerfRearmStillMs = 1'500;
// Home is only reachable while the bike is effectively stopped; see
// home_shortcut_event().
constexpr std::uint16_t kHomeShortcutMaxSpeedKph = 5;
// Home blip geometry, in design pixels like every other HUD measurement.
constexpr int kHomeBlipSizeDesign = 14;
// The panel is round: the ring sits just inside the glass, and a blip further
// out is clamped to it rather than dropped.
constexpr std::uint32_t kHomeHintMs = 3'000;
// North badge diameter. It rides the ring, so it is sized in design pixels
// like the rest of the HUD.
constexpr int kNorthBadgeSizeDesign = 20;

// Which palette slot a string reads from. Labels register here once, at
// creation, so a theme switch can repaint every string without walking the
// whole object tree or knowing each label by name.
enum class TextRole : std::uint8_t {
    Ink = 0,
    Muted,
    Accent,
    // Poster-scale numerals: same colour as Ink, heavier casing.
    Display,
    // Type printed on the map rather than on the paper.
    MapInk,
    OnAccent,
    OnSlab,
};

constexpr std::size_t kThemedTextCapacity = 40;
struct ThemedText {
    lv_obj_t *object;
    TextRole role;
};

enum class LifecycleVisual : std::uint8_t {
    Hidden = 0,
    PhoneOffline,
    PhoneConnecting,
    Connected,
    Ready,
    Planning,
};

constexpr int px(int value) {
    return value >= 0
               ? (value * MOTO_UI_CANVAS_WIDTH + kDesignWidth / 2) /
                     kDesignWidth
               : -((-value * MOTO_UI_CANVAS_WIDTH + kDesignWidth / 2) /
                   kDesignWidth);
}

constexpr double px(double value) {
    return value * static_cast<double>(MOTO_UI_CANVAS_WIDTH) /
           static_cast<double>(kDesignWidth);
}

struct MapPolyline {
    const lv_point_precise_t *points = nullptr;
    std::uint16_t count = 0;
};

struct Ui {
    lv_obj_t *screen = nullptr;
    lv_obj_t *pages[MOTO_UI_PAGE_COUNT]{};
    // One paper disc and one bezel per page. They are stored so a theme switch
    // repaints the paper without rebuilding the tree.
    lv_obj_t *page_discs[MOTO_UI_PAGE_COUNT]{};
    lv_obj_t *page_frames[MOTO_UI_PAGE_COUNT]{};
    lv_obj_t *page_dots[MOTO_UI_PAGE_COUNT]{};
    lv_timer_t *page_dots_timer = nullptr;
    moto_ui_page_t page = MOTO_UI_PAGE_NAVIGATION;
    bool page_dots_visible = true;

    lv_obj_t *nav_map = nullptr;
    lv_obj_t *nav_map_background = nullptr;
    // GTA is the product's own look, so it is the default rather than an
    // opt-in. MOTO_UI_THEME_OFFICIAL stays available for the vendor palette.
    moto_ui_theme_t theme = MOTO_UI_THEME_GTA_DAY;
    // Day/night signal supplied by the phone. Only MOTO_UI_THEME_AUTO reads
    // it; an explicit theme stays authoritative.
    bool ambient_night = false;
    // Building footprints are one visibility decision covering the fill layer
    // and the outline lines. Hidden by default: at 466 px the block layer
    // competes with the roads that matter while moving, and the phone owns
    // the trade through SettingsState.show_buildings.
    bool show_buildings = false;
    MapPalette palette = kGtaDayPalette;
    lv_obj_t *nav_buildings[MOTO_UI_BUILDING_FOOTPRINT_CAPACITY]{};
    lv_obj_t *nav_building_fill = nullptr;
    MapPolyline nav_building_lines[MOTO_UI_BUILDING_FOOTPRINT_CAPACITY]{};
    lv_point_precise_t nav_building_points[
        MOTO_UI_BUILDING_POINT_CAPACITY +
        MOTO_UI_BUILDING_FOOTPRINT_CAPACITY]{};
    float nav_building_x[MOTO_UI_BUILDING_POINT_CAPACITY]{};
    float nav_building_y[MOTO_UI_BUILDING_POINT_CAPACITY]{};
    float nav_building_target_x[MOTO_UI_BUILDING_POINT_CAPACITY]{};
    float nav_building_target_y[MOTO_UI_BUILDING_POINT_CAPACITY]{};
    moto_ui_building_span_t
        nav_building_spans[MOTO_UI_BUILDING_FOOTPRINT_CAPACITY]{};
    std::uint8_t nav_building_point_count = 0;
    std::uint8_t nav_building_footprint_count = 0;
    std::uint32_t nav_building_scene_revision = 0;
    lv_obj_t *nav_roads[MOTO_UI_ROAD_POLYLINE_CAPACITY]{};
    MapPolyline nav_road_lines[MOTO_UI_ROAD_POLYLINE_CAPACITY]{};
    lv_point_precise_t nav_road_points[MOTO_UI_ROAD_POINT_CAPACITY]{};
    float nav_road_x[MOTO_UI_ROAD_POINT_CAPACITY]{};
    float nav_road_y[MOTO_UI_ROAD_POINT_CAPACITY]{};
    float nav_road_target_x[MOTO_UI_ROAD_POINT_CAPACITY]{};
    float nav_road_target_y[MOTO_UI_ROAD_POINT_CAPACITY]{};
    moto_ui_polyline_span_t
        nav_road_spans[MOTO_UI_ROAD_POLYLINE_CAPACITY]{};
    std::uint8_t nav_road_point_count = 0;
    std::uint8_t nav_road_polyline_count = 0;
    std::uint32_t nav_road_scene_revision = 0;
    lv_obj_t *nav_route_shadow = nullptr;
    lv_obj_t *nav_route = nullptr;
    lv_obj_t *nav_home_blip = nullptr;
    MapPolyline nav_route_shadow_line{};
    MapPolyline nav_route_line{};
    lv_point_precise_t nav_route_points[MOTO_UI_ROUTE_POINT_CAPACITY]{};
    float nav_route_x[MOTO_UI_ROUTE_POINT_CAPACITY]{};
    float nav_route_y[MOTO_UI_ROUTE_POINT_CAPACITY]{};
    float nav_route_target_x[MOTO_UI_ROUTE_POINT_CAPACITY]{};
    float nav_route_target_y[MOTO_UI_ROUTE_POINT_CAPACITY]{};
    std::uint8_t nav_route_point_count = 0;
    std::uint8_t nav_route_target_count = 0;
    std::uint32_t nav_route_identity = 0;
    std::uint32_t nav_route_generation = 0;
    lv_timer_t *nav_route_motion_timer = nullptr;
    lv_obj_t *nav_marker = nullptr;
    lv_obj_t *nav_status = nullptr;
    lv_obj_t *nav_distance = nullptr;
    lv_obj_t *nav_unit = nullptr;
    lv_obj_t *nav_maneuver = nullptr;
    moto_maneuver_t nav_maneuver_type = MOTO_MANEUVER_STRAIGHT;
    lv_obj_t *nav_limit = nullptr;
    lv_obj_t *nav_limit_value = nullptr;
    lv_obj_t *nav_home = nullptr;
    lv_obj_t *nav_home_hint = nullptr;
    lv_timer_t *nav_home_hint_timer = nullptr;
    std::uint16_t nav_speed_kph = 0;
    // The ring replaces the old 84-degree arc: the whole rim is the gauge now,
    // and the north badge rides it.
    lv_obj_t *nav_ring = nullptr;
    lv_obj_t *nav_north = nullptr;
    std::uint8_t nav_radar_progress = 0;
    moto_traffic_t nav_radar_traffic = MOTO_TRAFFIC_UNKNOWN;

    // A full-screen empty-state surface replaces the old tiny
    // WAITING FOR PHONE / WAITING FOR ROUTE caption. It is kept inside the
    // navigation page so the speed and compass tools remain independently
    // usable while the phone is disconnected.
    lv_obj_t *nav_lifecycle = nullptr;
    lv_obj_t *nav_lifecycle_symbol = nullptr;
    lv_obj_t *nav_lifecycle_title = nullptr;
    lv_obj_t *nav_lifecycle_subtitle = nullptr;
    lv_obj_t *nav_lifecycle_kicker = nullptr;
    lv_timer_t *nav_lifecycle_timer = nullptr;
    lv_obj_t *lifecycle_rules[2]{};
    lv_obj_t *lifecycle_slash = nullptr;
    ThemedText themed_text[kThemedTextCapacity]{};
    std::uint8_t themed_text_count = 0;

    // Filled plates are themed the same way text is: the magenta brand block
    // and the ink counter slab are different roles and must not be swapped.
    enum class FillRole : std::uint8_t { Accent = 0, Slab };
    struct ThemedFill {
        lv_obj_t *object;
        FillRole role;
    };
    ThemedFill themed_fills[32]{};
    std::uint8_t themed_fill_count = 0;
    lv_timer_t *nav_success_timer = nullptr;
    moto_ui_phone_connection_t phone_connection = MOTO_UI_PHONE_OFFLINE;
    LifecycleVisual lifecycle_visual = LifecycleVisual::Hidden;
    LifecycleVisual lifecycle_target = LifecycleVisual::Hidden;
    std::uint16_t lifecycle_phase_deg = 0;
    bool lifecycle_success_active = false;
    bool navigation_has_guidance = false;
    bool navigation_route_request_in_flight = false;

    lv_obj_t *speed_gauge = nullptr;
    lv_obj_t *speed_value = nullptr;
    lv_obj_t *speed_limit = nullptr;
    lv_obj_t *speed_limit_value = nullptr;
    std::uint16_t speed_drawn = 0;

    lv_obj_t *perf_target = nullptr;
    lv_obj_t *perf_timer = nullptr;
    lv_obj_t *perf_best = nullptr;
    lv_obj_t *perf_chip = nullptr;
    lv_obj_t *perf_chip_text = nullptr;
    lv_obj_t *perf_gg_ring = nullptr;
    lv_obj_t *perf_delta = nullptr;
    lv_obj_t *perf_segments[kPerfSegmentCount]{};
    lv_obj_t *perf_strip = nullptr;
    lv_timer_t *perf_timer_task = nullptr;
    enum class PerfState : std::uint8_t { Armed, Running, Launched };
    PerfState perf_state = PerfState::Armed;
    std::uint32_t perf_run_start_ms = 0;
    std::uint32_t perf_still_since_ms = 0;
    std::uint16_t perf_best_centis = 0;
    std::uint32_t perf_last_centis = 0;
    lv_obj_t *perf_long_g = nullptr;
    lv_obj_t *perf_lat_g = nullptr;
    lv_obj_t *perf_gg_dot = nullptr;
    lv_obj_t *lean_value = nullptr;
    lv_obj_t *lean_scene = nullptr;
    lv_obj_t *lean_marks = nullptr;
    lv_obj_t *lean_blip = nullptr;
    lv_obj_t *lean_max_l_plate = nullptr;
    lv_obj_t *lean_max_r_plate = nullptr;
    lv_obj_t *lean_max_l = nullptr;
    lv_obj_t *lean_max_r = nullptr;
    lv_obj_t *lean_state = nullptr;
    lv_obj_t *lean_state_text = nullptr;
    float lean_deg = 0.0F;
    float lean_peak_left = 0.0F;
    float lean_peak_right = 0.0F;
    bool lean_has = false;
    bool lean_scene_dirty = true;

    lv_obj_t *compass_heading = nullptr;
    lv_obj_t *compass_cardinal = nullptr;
    lv_obj_t *compass_speed = nullptr;
    lv_obj_t *compass_marks = nullptr;
    std::uint16_t compass_drawn_heading = 0;
    bool compass_heading_drawn = false;
    lv_obj_t *compass_letters[4]{};

    lv_obj_t *music_source_plate = nullptr;
    lv_obj_t *music_source = nullptr;
    lv_obj_t *music_title = nullptr;
    lv_obj_t *music_artist = nullptr;
    lv_obj_t *music_disc = nullptr;
    lv_obj_t *music_buttons[4]{};
    lv_obj_t *music_button_labels[4]{};
    moto_music_state_t music{};
    char music_source_text[32]{};
    char music_title_text[64]{};
    char music_artist_text[48]{};

    moto_music_command_callback_t music_callback = nullptr;
    void *music_callback_context = nullptr;
    moto_page_change_callback_t page_callback = nullptr;
    void *page_callback_context = nullptr;
    moto_demo_change_callback_t demo_callback = nullptr;
    void *demo_callback_context = nullptr;
    moto_home_shortcut_callback_t home_callback = nullptr;
    void *home_callback_context = nullptr;
    bool demo_active = false;
    bool reduce_motion = false;
    // True only while the full widget tree from moto_nav_ui_create() exists.
    // A terminal screen clears it, so every mutator becomes inert in the
    // shutdown window instead of trusting a pointer that still looks valid.
    bool live = false;
} ui;

void reset_ui_state() {
    static_assert(std::is_trivially_copyable_v<Ui>,
                  "Ui must remain safe to reset without a stack temporary");

    // `ui = {}` materializes the complete aggregate as a temporary.  The
    // offline map buffers make Ui about 15 KiB, so that temporary can consume
    // nearly the whole ESP-IDF main-task stack before LVGL is called.  Clear
    // the retained instance in place and restore the two non-zero defaults.
    //
    // This also clears `ui.live` and the four callbacks, so a caller that
    // intends to receive taps must install them *after* create(); a callback
    // registered before it is silently discarded.
    std::memset(&ui, 0, sizeof(ui));
    ui.page = MOTO_UI_PAGE_NAVIGATION;
    ui.page_dots_visible = true;
    ui.nav_maneuver_type = MOTO_MANEUVER_STRAIGHT;
    ui.phone_connection = MOTO_UI_PHONE_OFFLINE;
    ui.theme = MOTO_UI_THEME_GTA_DAY;
    ui.palette = kGtaDayPalette;
    ui.lifecycle_visual = LifecycleVisual::Hidden;
    ui.lifecycle_target = LifecycleVisual::Hidden;
    // Force the first geometry payload to bind every mutable LVGL line even
    // when its protocol revision happens to start at zero.
    ui.nav_route_generation = ~std::uint32_t{0};
    ui.nav_route_identity = ~std::uint32_t{0};
    ui.nav_road_scene_revision = ~std::uint32_t{0};
    ui.nav_building_scene_revision = ~std::uint32_t{0};
}

// Every screen that owns the whole widget tree leaves through here. The
// timers go first: they retain raw widget pointers, and lv_obj_clean() is what
// invalidates those pointers. Clearing the pointers is what stops a late
// snapshot from reaching freed LVGL objects; clearing `ui.live` is what stops
// a late touch or a second create() from doing the same thing by another door.
void teardown_ui() {
    if(ui.page_dots_timer != nullptr) {
        lv_timer_delete(ui.page_dots_timer);
        ui.page_dots_timer = nullptr;
    }
    if(ui.nav_route_motion_timer != nullptr) {
        lv_timer_delete(ui.nav_route_motion_timer);
        ui.nav_route_motion_timer = nullptr;
    }
    if(ui.nav_lifecycle_timer != nullptr) {
        lv_timer_delete(ui.nav_lifecycle_timer);
        ui.nav_lifecycle_timer = nullptr;
    }
    if(ui.perf_timer_task != nullptr) {
        lv_timer_delete(ui.perf_timer_task);
        ui.perf_timer_task = nullptr;
    }
    if(ui.nav_success_timer != nullptr) {
        lv_timer_delete(ui.nav_success_timer);
        ui.nav_success_timer = nullptr;
    }
    if(ui.nav_home_hint_timer != nullptr) {
        lv_timer_delete(ui.nav_home_hint_timer);
        ui.nav_home_hint_timer = nullptr;
    }
    reset_ui_state();
    // The active screen itself is never deleted, only its children. Keep it so
    // the boot and power-off screens still have a surface to draw on.
    ui.screen = lv_screen_active();
    if(ui.screen != nullptr) {
        lv_obj_clean(ui.screen);
    }
}

lv_obj_t *make_layer(lv_obj_t *parent) {
    lv_obj_t *layer = lv_obj_create(parent);
    lv_obj_remove_style_all(layer);
    lv_obj_set_size(layer, MOTO_UI_CANVAS_WIDTH, MOTO_UI_CANVAS_HEIGHT);
    lv_obj_set_pos(layer, 0, 0);
    lv_obj_remove_flag(layer, LV_OBJ_FLAG_SCROLLABLE);
    return layer;
}

lv_obj_t *make_label(lv_obj_t *parent, const lv_font_t *font,
                     lv_color_t color, const char *text) {
    lv_obj_t *label = lv_label_create(parent);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, color, 0);
    lv_label_set_text(label, text);
    return label;
}

lv_color_t role_color(TextRole role) {
    switch(role) {
        case TextRole::Muted: return ui.palette.muted;
        case TextRole::Accent: return ui.palette.accent;
        case TextRole::MapInk: return ui.palette.map_ink;
        case TextRole::OnAccent: return ui.palette.on_accent;
        case TextRole::OnSlab: return ui.palette.on_slab;
        case TextRole::Display:
        case TextRole::Ink:
        default: return ui.palette.ink;
    }
}

// Use this instead of make_label() for any string that must survive a theme
// switch. The colour is resolved now and again on every theme change.
void apply_text_outline(lv_obj_t *label, TextRole role) {
    // The casing is the inverse of the type colour: black behind cream type at
    // night, paper behind ink type by day. Without that rule a display numeral
    // disappears into the horizon or the map it is printed over.
    int width = 0;
    lv_color_t color = ui.palette.outline;
    switch(role) {
        case TextRole::Display: width = px(5); break;
        case TextRole::Ink:
        case TextRole::Accent:
        case TextRole::MapInk: width = px(2); break;
        case TextRole::OnAccent:
        case TextRole::OnSlab:
        case TextRole::Muted:
        default: width = 0; break;
    }
    if(role == TextRole::MapInk) {
        // Type on the map keeps the game HUD's black casing in both themes.
        color = kOutlineBlack;
        width = px(3);
    }
    lv_obj_set_style_text_outline_stroke_color(label, color, 0);
    lv_obj_set_style_text_outline_stroke_width(label, width, 0);
    lv_obj_set_style_text_outline_stroke_opa(
        label, width > 0 ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
}

lv_obj_t *make_text(lv_obj_t *parent, const lv_font_t *font, TextRole role,
                    const char *text) {
    lv_obj_t *label = make_label(parent, font, role_color(role), text);
    apply_text_outline(label, role);
    if(ui.themed_text_count < kThemedTextCapacity) {
        ui.themed_text[ui.themed_text_count].object = label;
        ui.themed_text[ui.themed_text_count].role = role;
        ++ui.themed_text_count;
    }
    return label;
}

// ---------------------------------------------------------------------------
// Instrument chrome
//
// Every page is the same physical object: a disc of paper inside a black
// casing under one magenta band. A page may not invent its own frame, so the
// six screens read as six readouts of one instrument rather than six screens.
// ---------------------------------------------------------------------------
constexpr int kDialRadius = 207;       // paper edge, under the casing
// LVGL arcs stroke inward from the given radius, so these are outer edges.
// DESIGN.md: casing 197-212, band 212-222, glass edge outside 222. The band
// used to start at 207, which left a 5 px black moat inside the glass and made
// every page read as a smaller circle than the poster.
constexpr int kDialBandOuter = 222;    // magenta band outer edge
constexpr int kDialCasingOuter = 212;  // black casing outer edge
constexpr int kDialCasingWidth = 15;   // 197..212
constexpr int kDialBezelOuter = 233;   // glass edge
constexpr int kDialBandWidth = 10;     // 212..222

// Points are integer in this LVGL configuration; every drawing routine in the
// instrument chrome computes in double and lands here.
lv_point_precise_t pt(double x, double y) {
    return {static_cast<lv_value_precise_t>(std::lround(x)),
            static_cast<lv_value_precise_t>(std::lround(y))};
}

void stroke_ring(lv_layer_t *layer, int radius, int width, lv_color_t color) {
    lv_draw_arc_dsc_t dsc;
    lv_draw_arc_dsc_init(&dsc);
    dsc.center = {MOTO_UI_CANVAS_WIDTH / 2, MOTO_UI_CANVAS_HEIGHT / 2};
    dsc.radius = radius;
    dsc.width = width;
    dsc.color = color;
    dsc.opa = LV_OPA_COVER;
    dsc.rounded = 0;
    // Two halves: a full 360 arc is the one thing every LVGL major version
    // treats differently, and the rim is too visible to gamble.
    dsc.start_angle = 0;
    dsc.end_angle = 180;
    lv_draw_arc(layer, &dsc);
    dsc.start_angle = 180;
    dsc.end_angle = 360;
    lv_draw_arc(layer, &dsc);
}

void draw_instrument_frame(lv_event_t *event) {
    lv_layer_t *layer = lv_event_get_layer(event);
    // The bezel is painted over the map, so no sliver of map can show between
    // the rim and the glass edge.
    // Bezel 222..233, band 212..222, casing 197..212. The three rings share
    // edges, so no sliver of ground can leak between them.
    stroke_ring(layer, kDialBezelOuter, kDialBezelOuter - kDialBandOuter,
                ui.palette.frame);
    stroke_ring(layer, kDialBandOuter, kDialBandWidth, ui.palette.accent);
    stroke_ring(layer, kDialCasingOuter, kDialCasingWidth, kOutlineBlack);
}

lv_obj_t *register_fill(lv_obj_t *object, Ui::FillRole role) {
    if(ui.themed_fill_count < 32) {
        ui.themed_fills[ui.themed_fill_count].object = object;
        ui.themed_fills[ui.themed_fill_count].role = role;
        ++ui.themed_fill_count;
    }
    return object;
}

// The magenta plate: the brand block, the thing that carries a short label.
lv_obj_t *make_plate(lv_obj_t *parent, const char *text, int width,
                     int height, lv_obj_t **label_out = nullptr) {
    lv_obj_t *plate = lv_obj_create(parent);
    lv_obj_remove_style_all(plate);
    lv_obj_clear_flag(plate, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(plate, px(width), px(height));
    lv_obj_set_style_bg_color(plate, ui.palette.accent, 0);
    lv_obj_set_style_bg_opa(plate, LV_OPA_COVER, 0);
    lv_obj_t *label = make_text(plate, &moto_font_display_24,
                                TextRole::OnAccent, text);
    lv_obj_set_style_text_letter_space(label, px(2), 0);
    lv_obj_center(label);
    if(label_out != nullptr) *label_out = label;
    return register_fill(plate, Ui::FillRole::Accent);
}

// The counter slab: the game parks cash and ammo on a black block with cream
// numerals. Secondary values live here.
lv_obj_t *make_slab(lv_obj_t *parent, const char *text, int width, int height,
                    lv_obj_t **label_out = nullptr) {
    lv_obj_t *slab = lv_obj_create(parent);
    lv_obj_remove_style_all(slab);
    lv_obj_clear_flag(slab, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(slab, px(width), px(height));
    lv_obj_set_style_bg_color(slab, ui.palette.slab, 0);
    lv_obj_set_style_bg_opa(slab, LV_OPA_COVER, 0);
    lv_obj_t *label = make_text(slab, &moto_font_display_24,
                                TextRole::OnSlab, text);
    lv_obj_set_style_text_letter_space(label, px(1), 0);
    lv_obj_center(label);
    if(label_out != nullptr) *label_out = label;
    return register_fill(slab, Ui::FillRole::Slab);
}

// Traffic-sign face. It is not a themed surface: the sign has to read the same
// way in daylight and at night, so it keeps its cream face in both palettes.
constexpr lv_color_t kLimitFace = LV_COLOR_MAKE(0xF6, 0xF0, 0xD7);

// One speed-limit sign for both pages that show one. The caller owns position
// and visibility; the face, ring and digit size cannot drift apart again.
lv_obj_t *make_speed_limit_sign(lv_obj_t *parent, int design_size) {
    lv_obj_t *sign = lv_obj_create(parent);
    lv_obj_remove_style_all(sign);
    lv_obj_clear_flag(sign, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(sign, px(design_size), px(design_size));
    lv_obj_set_style_radius(sign, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(sign, kLimitFace, 0);
    lv_obj_set_style_bg_opa(sign, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(sign, kRed, 0);
    lv_obj_set_style_border_width(sign, px(6), 0);
    lv_obj_set_style_pad_all(sign, 0, 0);
    lv_obj_t *value =
        make_label(sign, &moto_font_display_24, kOutlineBlack, "70");
    lv_obj_center(value);
    lv_obj_add_flag(sign, LV_OBJ_FLAG_HIDDEN);
    return sign;
}

// Paper + bezel. Content is created after this call and therefore above it.
void make_page_surface(lv_obj_t *page, moto_ui_page_t index) {
    lv_obj_set_style_bg_color(page, ui.palette.frame, 0);
    lv_obj_set_style_bg_opa(page, LV_OPA_COVER, 0);
    lv_obj_t *disc = lv_obj_create(page);
    lv_obj_remove_style_all(disc);
    lv_obj_clear_flag(disc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(disc, kDialRadius * 2, kDialRadius * 2);
    lv_obj_center(disc);
    lv_obj_set_style_radius(disc, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(disc, ui.palette.ground, 0);
    lv_obj_set_style_bg_opa(disc, LV_OPA_COVER, 0);
    ui.page_discs[index] = disc;

    lv_obj_t *frame = lv_obj_create(page);
    lv_obj_remove_style_all(frame);
    lv_obj_clear_flag(frame, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(frame, MOTO_UI_CANVAS_WIDTH, MOTO_UI_CANVAS_HEIGHT);
    lv_obj_center(frame);
    lv_obj_add_event_cb(frame, draw_instrument_frame, LV_EVENT_DRAW_MAIN,
                        nullptr);
    ui.page_frames[index] = frame;
}

void apply_text_colors() {
    for(std::uint8_t i = 0; i < ui.themed_text_count; ++i) {
        const ThemedText slot = ui.themed_text[i];
        if(slot.object != nullptr) {
            lv_obj_set_style_text_color(slot.object, role_color(slot.role), 0);
            apply_text_outline(slot.object, slot.role);
        }
    }
    for(std::uint8_t i = 0; i < ui.themed_fill_count; ++i) {
        const Ui::ThemedFill slot = ui.themed_fills[i];
        if(slot.object == nullptr) continue;
        lv_obj_set_style_bg_color(
            slot.object,
            slot.role == Ui::FillRole::Accent ? ui.palette.accent
                                              : ui.palette.slab,
            0);
    }
}

void copy_text(char *destination, std::size_t capacity, const char *source,
               const char *fallback) {
    if(capacity == 0) return;
    const char *value = source != nullptr && source[0] != '\0' ? source : fallback;
    std::snprintf(destination, capacity, "%s", value != nullptr ? value : "");
}

const char *cardinal_name(std::uint16_t heading) {
    static const char *names[] = {"N", "NE", "E", "SE", "S", "SW", "W", "NW"};
    return names[((heading + 22U) / 45U) % 8U];
}

void draw_vehicle_marker(lv_event_t *event) {
    lv_obj_t *object = lv_event_get_target_obj(event);
    lv_area_t area;
    lv_obj_get_coords(object, &area);
    lv_layer_t *layer = lv_event_get_layer(event);

    lv_draw_triangle_dsc_t triangle;
    lv_draw_triangle_dsc_init(&triangle);
    triangle.color = kBlack;
    triangle.opa = LV_OPA_COVER;
    triangle.p[0] = {static_cast<lv_value_precise_t>(area.x1 + px(17)),
                     static_cast<lv_value_precise_t>(area.y1)};
    triangle.p[1] = {static_cast<lv_value_precise_t>(area.x1),
                     static_cast<lv_value_precise_t>(area.y1 + px(36))};
    triangle.p[2] = {static_cast<lv_value_precise_t>(area.x1 + px(34)),
                     static_cast<lv_value_precise_t>(area.y1 + px(36))};
    lv_draw_triangle(layer, &triangle);

    triangle.color = kWhite;
    triangle.p[0] = {static_cast<lv_value_precise_t>(area.x1 + px(17)),
                     static_cast<lv_value_precise_t>(area.y1 + px(5))};
    triangle.p[1] = {static_cast<lv_value_precise_t>(area.x1 + px(6)),
                     static_cast<lv_value_precise_t>(area.y1 + px(30))};
    triangle.p[2] = {static_cast<lv_value_precise_t>(area.x1 + px(28)),
                     static_cast<lv_value_precise_t>(area.y1 + px(30))};
    lv_draw_triangle(layer, &triangle);
}

void draw_home_blip(lv_event_t *event) {
    // Same ink-on-white treatment as the rider arrow: the device has no light
    // map theme, so the blip never has to invert. The black outline keeps it
    // legible over both cream roads and dark blocks.
    lv_obj_t *object = lv_event_get_target_obj(event);
    lv_area_t area;
    lv_obj_get_coords(object, &area);
    lv_layer_t *layer = lv_event_get_layer(event);
    const lv_value_precise_t left = area.x1;
    const lv_value_precise_t top = area.y1;
    const lv_value_precise_t right = area.x2;
    const lv_value_precise_t bottom = area.y2;
    const lv_value_precise_t centre = (left + right) / 2;
    const lv_value_precise_t roof = top + (bottom - top) * 0.45;

    lv_draw_rect_dsc_t body;
    lv_draw_rect_dsc_init(&body);
    body.radius = 0;
    body.bg_opa = LV_OPA_COVER;
    body.border_width = 0;
    lv_area_t body_area{static_cast<int32_t>(left),
                        static_cast<int32_t>(roof),
                        static_cast<int32_t>(right),
                        static_cast<int32_t>(bottom)};

    lv_draw_triangle_dsc_t triangle;
    lv_draw_triangle_dsc_init(&triangle);
    triangle.opa = LV_OPA_COVER;

    // Outline pass.
    body.bg_color = kBlack;
    lv_draw_rect(layer, &body, &body_area);
    triangle.color = kBlack;
    triangle.p[0] = {centre, top};
    triangle.p[1] = {left, roof + 1};
    triangle.p[2] = {right, roof + 1};
    lv_draw_triangle(layer, &triangle);

    // Inner pass, inset so a hard 2 px edge remains on every side.
    body.bg_color = kWhite;
    body_area.x1 += 2;
    body_area.x2 -= 2;
    body_area.y1 += 2;
    body_area.y2 -= 2;
    lv_draw_rect(layer, &body, &body_area);
    triangle.color = kWhite;
    triangle.p[0] = {centre, top + 4};
    triangle.p[1] = {left + 3, roof + 1};
    triangle.p[2] = {right - 3, roof + 1};
    lv_draw_triangle(layer, &triangle);
}

void update_page_dots() {
    for(int i = 0; i < MOTO_UI_PAGE_COUNT; ++i) {
        const bool active = i == static_cast<int>(ui.page);
        lv_obj_set_size(ui.page_dots[i], px(active ? 16 : 5), px(5));
        lv_obj_set_style_radius(ui.page_dots[i], px(3), 0);
        lv_obj_set_style_bg_color(
            ui.page_dots[i], active ? ui.palette.accent : ui.palette.map_ink, 0);
        lv_obj_set_style_bg_opa(ui.page_dots[i],
                                active ? LV_OPA_COVER : LV_OPA_40, 0);
        lv_obj_set_x(ui.page_dots[i], px(145 + i * 22 - (active ? 5 : 0)));
        // Every page is reachable at all times. The music page used to
        // disappear until the phone advertised media capabilities, which
        // meant the controls vanished exactly when a rider wanted them.
        if(ui.page_dots_visible) {
            lv_obj_remove_flag(ui.page_dots[i], LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(ui.page_dots[i], LV_OBJ_FLAG_HIDDEN);
        }
    }
}

void hide_page_dots(lv_timer_t *) {
    ui.page_dots_visible = false;
    update_page_dots();
    if(ui.page_dots_timer != nullptr) lv_timer_pause(ui.page_dots_timer);
}

void reveal_page_dots() {
    if(ui.screen == nullptr || ui.page_dots[0] == nullptr) return;
    ui.page_dots_visible = true;
    update_page_dots();
    if(ui.page_dots_timer != nullptr) {
        lv_timer_set_period(ui.page_dots_timer, kPageDotsVisibleMs);
        lv_timer_reset(ui.page_dots_timer);
        lv_timer_resume(ui.page_dots_timer);
    }
}

void interaction_event(lv_event_t *) {
    if(!ui.live) return;
    reveal_page_dots();
}

void install_interaction_wake(lv_obj_t *object) {
    if(object == nullptr) return;
    lv_obj_add_event_cb(object, interaction_event, LV_EVENT_PRESSED, nullptr);
    const std::uint32_t child_count = lv_obj_get_child_count(object);
    for(std::uint32_t index = 0; index < child_count; ++index) {
        install_interaction_wake(lv_obj_get_child(object,
                                                  static_cast<int32_t>(index)));
    }
}

void show_page(moto_ui_page_t page, bool reveal_on_same_page = false) {
    if(page < MOTO_UI_PAGE_NAVIGATION || page >= MOTO_UI_PAGE_COUNT) return;
    // The boot and power-off screens clean the widget tree and reset the page
    // pointers. A snapshot that arrives during that window (the BLE task can
    // still deliver one before the PMIC cuts power) must not touch LVGL: the
    // object is gone and LVGL asserts on it.
    if(ui.pages[page] == nullptr) return;
    const bool changed = page != ui.page;
    ui.page = page;
    for(int i = 0; i < MOTO_UI_PAGE_COUNT; ++i) {
        if(i == static_cast<int>(page)) {
            lv_obj_remove_flag(ui.pages[i], LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(ui.pages[i], LV_OBJ_FLAG_HIDDEN);
        }
    }
    update_page_dots();
    // Navigation state arrives continuously. Only a real page transition or
    // user action may restart the five-second affordance timer.
    if(changed || reveal_on_same_page) reveal_page_dots();
}

void gesture_event(lv_event_t *) {
    // Screen event callbacks outlive lv_obj_clean(); see teardown_ui().
    if(!ui.live) return;
#if LV_USE_GESTURE_RECOGNITION == 0
#error "Page paging needs LV_USE_GESTURE_RECOGNITION in config/lv_conf.h"
#endif
    lv_indev_t *indev = lv_indev_active();
    if(indev == nullptr) return;
    const lv_dir_t direction = lv_indev_get_gesture_dir(indev);
    int next = static_cast<int>(ui.page);
    if(direction == LV_DIR_LEFT) {
        next = (next + 1) % MOTO_UI_PAGE_COUNT;
    } else if(direction == LV_DIR_RIGHT) {
        next = (next + MOTO_UI_PAGE_COUNT - 1) % MOTO_UI_PAGE_COUNT;
    } else {
        return;
    }
    const auto requested = static_cast<moto_ui_page_t>(next);
    if(ui.page_callback != nullptr) {
        ui.page_callback(requested, ui.page_callback_context);
    } else {
        show_page(requested, true);
    }
    reveal_page_dots();
    lv_indev_wait_release(indev);
}

void draw_map_line(lv_layer_t *layer, const lv_draw_line_dsc_t &line) {
    const lv_area_t original_clip = layer->_clip_area;
    lv_area_t visible{
        std::max(original_clip.x1, static_cast<int32_t>(
            std::min(line.p1.x, line.p2.x) - line.width)),
        std::max(original_clip.y1, static_cast<int32_t>(
            std::min(line.p1.y, line.p2.y) - line.width)),
        std::min(original_clip.x2, static_cast<int32_t>(
            std::max(line.p1.x, line.p2.x) + line.width)),
        std::min(original_clip.y2, static_cast<int32_t>(
            std::max(line.p1.y, line.p2.y) + line.width)),
    };
    if(visible.x1 > visible.x2 || visible.y1 > visible.y2) return;
    const float dx = static_cast<float>(line.p2.x - line.p1.x);
    const float dy = static_cast<float>(line.p2.y - line.p1.y);
    constexpr int32_t band_height = 32;
    if(std::abs(dy) <= band_height || std::abs(dx) <= 64.0F ||
       visible.y2 - visible.y1 < band_height) {
        lv_draw_line(layer, &line);
        return;
    }

    // LVGL masks every pixel in a diagonal's bounding rectangle, including
    // the empty space beside a long thin street. Narrow that rectangle per
    // non-overlapping band. Keep the original endpoints so antialiasing and
    // round caps remain identical, including where adjacent bands meet.
    const float slope = dx / dy;
    const float padding = static_cast<float>(line.width) + 2.0F;
    const float minimum_y = static_cast<float>(std::min(line.p1.y, line.p2.y));
    const float maximum_y = static_cast<float>(std::max(line.p1.y, line.p2.y));
    for(int32_t y = visible.y1; y <= visible.y2; y += band_height) {
        lv_area_t band = visible;
        band.y1 = y;
        band.y2 = std::min(y + band_height - 1, visible.y2);
        const float low = std::clamp(static_cast<float>(band.y1) - padding,
                                     minimum_y, maximum_y);
        const float high = std::clamp(static_cast<float>(band.y2) + padding,
                                      minimum_y, maximum_y);
        const float a = line.p1.x + slope * (low - line.p1.y);
        const float b = line.p1.x + slope * (high - line.p1.y);
        band.x1 = std::max(band.x1, static_cast<int32_t>(
            std::floor(std::min(a, b) - padding)));
        band.x2 = std::min(band.x2, static_cast<int32_t>(
            std::ceil(std::max(a, b) + padding)));
        if(band.x1 > band.x2) continue;
        // lv_draw_line copies this clip into its task before dispatching.
        layer->_clip_area = band;
        lv_draw_line(layer, &line);
    }
    layer->_clip_area = original_clip;
}

void draw_building_fills(lv_event_t *event) {
    if(lv_event_get_code(event) != LV_EVENT_DRAW_MAIN) return;
    if(ui.nav_building_point_count < 3 ||
       ui.nav_building_footprint_count == 0) {
        return;
    }
    lv_obj_t *object = lv_event_get_target_obj(event);
    lv_area_t area;
    lv_obj_get_coords(object, &area);
    lv_layer_t *layer = lv_event_get_layer(event);
    const int32_t local_width = area.x2 - area.x1;
    const int32_t local_height = area.y2 - area.y1;

    lv_draw_rect_dsc_t fill;
    lv_draw_rect_dsc_init(&fill);
    fill.bg_opa = LV_OPA_COVER;
    fill.radius = 0;
    fill.border_width = 0;

    std::size_t packed_index = 0;
    for(std::uint8_t footprint = 0;
        footprint < ui.nav_building_footprint_count; ++footprint) {
        const moto_ui_building_span_t span = ui.nav_building_spans[footprint];
        const std::uint8_t count = span.point_count;
        const lv_point_precise_t *points =
            &ui.nav_building_points[packed_index];
        fill.bg_color = span.building_class == 1U
                            ? ui.palette.building_landmark
                            : ui.palette.building;

        float minimum_y = static_cast<float>(points[0].y);
        float maximum_y = static_cast<float>(points[0].y);
        for(std::uint8_t i = 1; i < count; ++i) {
            minimum_y = std::min(minimum_y, static_cast<float>(points[i].y));
            maximum_y = std::max(maximum_y, static_cast<float>(points[i].y));
        }
        const int32_t first_row = std::max<int32_t>(
            0, static_cast<int32_t>(std::ceil(minimum_y)));
        const int32_t last_row = std::min<int32_t>(
            local_height, static_cast<int32_t>(std::floor(maximum_y)));
        for(int32_t row = first_row; row <= last_row; ++row) {
            const float scan_y = static_cast<float>(row) + 0.5F;
            float crossings[MOTO_UI_BUILDING_POINT_CAPACITY];
            std::size_t crossing_count = 0;
            for(std::uint8_t i = 0; i < count; ++i) {
                const std::uint8_t next = (i + 1U) % count;
                const float y0 = static_cast<float>(points[i].y);
                const float y1 = static_cast<float>(points[next].y);
                if(!((y0 <= scan_y && y1 > scan_y) ||
                     (y1 <= scan_y && y0 > scan_y))) {
                    continue;
                }
                const float t = (scan_y - y0) / (y1 - y0);
                crossings[crossing_count++] =
                    static_cast<float>(points[i].x) +
                    t * (static_cast<float>(points[next].x) -
                         static_cast<float>(points[i].x));
            }
            std::sort(crossings, crossings + crossing_count);
            for(std::size_t i = 0; i + 1 < crossing_count; i += 2) {
                const int32_t first_column = std::max<int32_t>(
                    0, static_cast<int32_t>(
                           std::ceil(crossings[i] - 0.5F)));
                const int32_t last_column = std::min<int32_t>(
                    local_width, static_cast<int32_t>(
                                     std::floor(crossings[i + 1] - 0.5F)));
                if(first_column > last_column) continue;
                lv_area_t row_area = {
                    area.x1 + first_column,
                    area.y1 + row,
                    area.x1 + last_column,
                    area.y1 + row,
                };
                lv_draw_rect(layer, &fill, &row_area);
            }
        }
        packed_index += static_cast<std::size_t>(count) + 1U;
    }
}

void draw_map_polyline(lv_event_t *event) {
    lv_obj_t *object = lv_event_get_target_obj(event);
    const auto code = lv_event_get_code(event);
    if(code == LV_EVENT_REFR_EXT_DRAW_SIZE) {
        auto *size = static_cast<int32_t *>(lv_event_get_param(event));
        *size = std::max(*size, lv_obj_get_style_line_width(object, LV_PART_MAIN));
        return;
    }
    if(code != LV_EVENT_DRAW_MAIN) return;
    const auto *polyline = static_cast<const MapPolyline *>(
        lv_event_get_user_data(event));
    if(polyline->points == nullptr || polyline->count < 2) return;
    lv_layer_t *layer = lv_event_get_layer(event);
    lv_area_t object_area;
    lv_obj_get_coords(object, &object_area);
    lv_draw_line_dsc_t line;
    lv_draw_line_dsc_init(&line);
    line.base.layer = layer;
    lv_obj_init_draw_line_dsc(object, LV_PART_MAIN, &line);
    for(std::uint16_t index = 1; index < polyline->count; ++index) {
        line.p1 = polyline->points[index - 1];
        line.p2 = polyline->points[index];
        line.p1.x += object_area.x1;
        line.p2.x += object_area.x1;
        line.p1.y += object_area.y1;
        line.p2.y += object_area.y1;
        draw_map_line(layer, line);
        line.round_start = 0;
    }
}

lv_obj_t *create_map_polyline(lv_obj_t *parent, MapPolyline &polyline) {
    lv_obj_t *object = lv_obj_create(parent);
    lv_obj_remove_style_all(object);
    lv_obj_remove_flag(object, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(object, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_user_data(object, &polyline);
    lv_obj_add_event_cb(object, draw_map_polyline, LV_EVENT_ALL, &polyline);
    return object;
}

void set_map_polyline_points(lv_obj_t *object, const lv_point_precise_t *points,
                            std::uint16_t count) {
    auto *polyline = static_cast<MapPolyline *>(lv_obj_get_user_data(object));
    polyline->points = points;
    polyline->count = count;
    lv_obj_invalidate(object);
}

bool apply_route_geometry_frame(bool rebind_lines = true) {
    if(ui.nav_route_point_count < 2) return false;
    bool pixels_changed = false;
    for(std::uint8_t i = 0; i < ui.nav_route_point_count; ++i) {
        const auto x = static_cast<lv_value_precise_t>(
            std::lround(ui.nav_route_x[i]));
        const auto y = static_cast<lv_value_precise_t>(
            std::lround(ui.nav_route_y[i]));
        pixels_changed = pixels_changed || ui.nav_route_points[i].x != x ||
                         ui.nav_route_points[i].y != y;
        ui.nav_route_points[i] = {x, y};
    }
    if(rebind_lines) {
        set_map_polyline_points(ui.nav_route_shadow, ui.nav_route_points,
                                   ui.nav_route_point_count);
        set_map_polyline_points(ui.nav_route, ui.nav_route_points,
                                   ui.nav_route_point_count);
    }
    return pixels_changed || rebind_lines;
}

bool apply_road_geometry_frame(bool rebind_lines = true) {
    if(ui.nav_road_point_count < 2 || ui.nav_road_polyline_count == 0) {
        return false;
    }
    bool pixels_changed = false;
    for(std::uint8_t i = 0; i < ui.nav_road_point_count; ++i) {
        const auto x = static_cast<lv_value_precise_t>(
            std::lround(ui.nav_road_x[i]));
        const auto y = static_cast<lv_value_precise_t>(
            std::lround(ui.nav_road_y[i]));
        pixels_changed = pixels_changed || ui.nav_road_points[i].x != x ||
                         ui.nav_road_points[i].y != y;
        ui.nav_road_points[i] = {x, y};
    }
    if(rebind_lines) {
        for(std::uint8_t i = 0; i < ui.nav_road_polyline_count; ++i) {
            const moto_ui_polyline_span_t span = ui.nav_road_spans[i];
            set_map_polyline_points(
                ui.nav_roads[i],
                &ui.nav_road_points[span.first_point_index],
                span.point_count);
        }
    }
    return pixels_changed || rebind_lines;
}

bool apply_building_geometry_frame(bool rebind_lines = true) {
    if(ui.nav_building_point_count < 3 ||
       ui.nav_building_footprint_count == 0) return false;
    bool pixels_changed = false;
    std::size_t packed_index = 0;
    for(std::uint8_t footprint_index = 0;
        footprint_index < ui.nav_building_footprint_count;
        ++footprint_index) {
        const moto_ui_building_span_t span =
            ui.nav_building_spans[footprint_index];
        for(std::uint8_t point_index = 0; point_index < span.point_count;
            ++point_index) {
            const std::size_t source = span.first_point_index + point_index;
            const auto x = static_cast<lv_value_precise_t>(
                std::lround(ui.nav_building_x[source]));
            const auto y = static_cast<lv_value_precise_t>(
                std::lround(ui.nav_building_y[source]));
            pixels_changed = pixels_changed ||
                ui.nav_building_points[packed_index].x != x ||
                ui.nav_building_points[packed_index].y != y;
            ui.nav_building_points[packed_index++] = {x, y};
        }
        // BLE omits the duplicate closing point; LVGL needs it explicitly.
        const auto closing =
            ui.nav_building_points[packed_index - span.point_count];
        pixels_changed = pixels_changed ||
            ui.nav_building_points[packed_index].x != closing.x ||
            ui.nav_building_points[packed_index].y != closing.y;
        ui.nav_building_points[packed_index] = closing;
        if(rebind_lines) {
            set_map_polyline_points(
                ui.nav_buildings[footprint_index],
                &ui.nav_building_points[packed_index - span.point_count],
                span.point_count + 1U);
        }
        ++packed_index;
    }
    if(ui.nav_building_fill != nullptr) {
        lv_obj_invalidate(ui.nav_building_fill);
    }
    return pixels_changed || rebind_lines;
}

void route_motion_tick(lv_timer_t *) {
    if(ui.nav_route_target_count < 2 || ui.nav_route_point_count < 2) return;
    // 0.39 at 25 ms has approximately the same smoothing time constant as
    // the old 0.56 at 40 ms, but supplies smaller and more frequent steps.
    // Screen coordinates need subpixel precision, not geographic doubles.
    // Float uses the ESP32-S3 FPU for every point in this 40 Hz hot loop.
    const float blend = ui.reduce_motion ? 1.0F : 0.39F;
    bool route_moved = false;
    for(std::uint8_t i = 0; i < ui.nav_route_point_count; ++i) {
        const float dx = ui.nav_route_target_x[i] - ui.nav_route_x[i];
        const float dy = ui.nav_route_target_y[i] - ui.nav_route_y[i];
        if(std::abs(dx) < 0.08F && std::abs(dy) < 0.08F) {
            ui.nav_route_x[i] = ui.nav_route_target_x[i];
            ui.nav_route_y[i] = ui.nav_route_target_y[i];
            continue;
        }
        ui.nav_route_x[i] += dx * blend;
        ui.nav_route_y[i] += dy * blend;
        route_moved = true;
    }
    bool roads_moved = false;
    for(std::uint8_t i = 0; i < ui.nav_road_point_count; ++i) {
        const float dx = ui.nav_road_target_x[i] - ui.nav_road_x[i];
        const float dy = ui.nav_road_target_y[i] - ui.nav_road_y[i];
        if(std::abs(dx) < 0.08F && std::abs(dy) < 0.08F) {
            ui.nav_road_x[i] = ui.nav_road_target_x[i];
            ui.nav_road_y[i] = ui.nav_road_target_y[i];
            continue;
        }
        ui.nav_road_x[i] += dx * blend;
        ui.nav_road_y[i] += dy * blend;
        roads_moved = true;
    }
    bool buildings_moved = false;
    for(std::uint8_t i = 0; i < ui.nav_building_point_count; ++i) {
        const float dx = ui.nav_building_target_x[i] - ui.nav_building_x[i];
        const float dy = ui.nav_building_target_y[i] - ui.nav_building_y[i];
        if(std::abs(dx) < 0.08F && std::abs(dy) < 0.08F) {
            ui.nav_building_x[i] = ui.nav_building_target_x[i];
            ui.nav_building_y[i] = ui.nav_building_target_y[i];
            continue;
        }
        ui.nav_building_x[i] += dx * blend;
        ui.nav_building_y[i] += dy * blend;
        buildings_moved = true;
    }
    // The line objects retain pointers to the mutable point arrays. Ordinary
    // animation therefore only changes those arrays and invalidates the one
    // common map layer. Rebinding every road/building used to invalidate the
    // same 466x232 region dozens of times per frame.
    bool pixels_changed = false;
    if(buildings_moved) {
        pixels_changed = apply_building_geometry_frame(false) || pixels_changed;
    }
    if(roads_moved) {
        pixels_changed = apply_road_geometry_frame(false) || pixels_changed;
    }
    if(route_moved) {
        pixels_changed = apply_route_geometry_frame(false) || pixels_changed;
    }
    if(pixels_changed) lv_obj_invalidate(ui.nav_map);
}

void update_road_geometry(const moto_ui_state_t *state) {
    const std::uint8_t point_count = std::min<std::uint8_t>(
        state->road_point_count, MOTO_UI_ROAD_POINT_CAPACITY);
    const std::uint8_t polyline_count = std::min<std::uint8_t>(
        state->road_polyline_count, MOTO_UI_ROAD_POLYLINE_CAPACITY);
    if(point_count < 2 || polyline_count == 0) {
        ui.nav_road_point_count = 0;
        ui.nav_road_polyline_count = 0;
        ui.nav_road_scene_revision = state->map_scene_revision;
        for(lv_obj_t *road : ui.nav_roads) {
            lv_obj_add_flag(road, LV_OBJ_FLAG_HIDDEN);
        }
        return;
    }

    bool spans_changed =
        ui.nav_road_scene_revision != state->map_scene_revision ||
        ui.nav_road_point_count != point_count ||
        ui.nav_road_polyline_count != polyline_count;
    for(std::uint8_t i = 0; i < point_count; ++i) {
        ui.nav_road_target_x[i] = state->road_points[i].x;
        ui.nav_road_target_y[i] = state->road_points[i].y;
    }
    for(std::uint8_t i = 0; i < polyline_count; ++i) {
        const moto_ui_polyline_span_t next = state->road_polylines[i];
        spans_changed = spans_changed ||
                        ui.nav_road_spans[i].first_point_index !=
                            next.first_point_index ||
                        ui.nav_road_spans[i].point_count != next.point_count ||
                        ui.nav_road_spans[i].road_class != next.road_class;
        ui.nav_road_spans[i] = next;
    }

    // A wide point can move more than 96 px during a perfectly ordinary fast
    // yaw. Treating screen-space distance as a route replacement made the
    // whole map jump. Only topology/revision changes snap; motion interpolates.
    const bool snap = spans_changed || ui.reduce_motion;
    ui.nav_road_point_count = point_count;
    ui.nav_road_polyline_count = polyline_count;
    ui.nav_road_scene_revision = state->map_scene_revision;
    if(snap) {
        for(std::uint8_t i = 0; i < point_count; ++i) {
            ui.nav_road_x[i] = ui.nav_road_target_x[i];
            ui.nav_road_y[i] = ui.nav_road_target_y[i];
        }
    }
    if(spans_changed) {
        for(std::uint8_t i = 0; i < MOTO_UI_ROAD_POLYLINE_CAPACITY; ++i) {
            if(i < polyline_count) {
                const std::uint8_t road_class = ui.nav_road_spans[i].road_class;
                const bool major = road_class <= 2U;
                const bool service = road_class >= 4U;
                lv_obj_set_style_line_width(
                    ui.nav_roads[i], px(major ? 4 : (service ? 2 : 3)), 0);
                lv_obj_set_style_line_color(
                    ui.nav_roads[i],
                    major ? ui.palette.road_major
                          : (service ? ui.palette.road_minor
                                     : ui.palette.road), 0);
                lv_obj_remove_flag(ui.nav_roads[i], LV_OBJ_FLAG_HIDDEN);
            } else {
                lv_obj_add_flag(ui.nav_roads[i], LV_OBJ_FLAG_HIDDEN);
            }
        }
    }
    if(snap) apply_road_geometry_frame();
}


// The fill layer and the outline lines are one layer to the rider. Toggling
// only one of them leaves either a hollow ghost or a solid block with no edge,
// so both are driven from this single place. Lives in the anonymous namespace
// because the public setter only forwards to it.
void apply_building_visibility() {
    if(ui.nav_building_fill != nullptr) {
        if(ui.show_buildings) {
            lv_obj_remove_flag(ui.nav_building_fill, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(ui.nav_building_fill, LV_OBJ_FLAG_HIDDEN);
        }
    }
    for(std::uint8_t i = 0; i < MOTO_UI_BUILDING_FOOTPRINT_CAPACITY; ++i) {
        if(ui.nav_buildings[i] == nullptr) continue;
        // A slot past footprint_count is unused, not merely off-screen.
        if(ui.show_buildings && i < ui.nav_building_footprint_count) {
            lv_obj_remove_flag(ui.nav_buildings[i], LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(ui.nav_buildings[i], LV_OBJ_FLAG_HIDDEN);
        }
    }
}

void update_building_geometry(const moto_ui_state_t *state) {
    const std::uint8_t point_count = std::min<std::uint8_t>(
        state->building_point_count, MOTO_UI_BUILDING_POINT_CAPACITY);
    const std::uint8_t footprint_count = std::min<std::uint8_t>(
        state->building_footprint_count,
        MOTO_UI_BUILDING_FOOTPRINT_CAPACITY);
    if(point_count < 3 || footprint_count == 0) {
        // Motion snapshots arrive at 40 Hz. Invalidating the full-canvas fill
        // on every one of them, even when the previous snapshot also had no
        // buildings, pins the UI at ~3 fps and makes every swipe feel dead.
        // Only repaint when something was actually on screen to clear.
        const bool had_buildings =
            ui.nav_building_point_count != 0 ||
            ui.nav_building_footprint_count != 0;
        ui.nav_building_point_count = 0;
        ui.nav_building_footprint_count = 0;
        ui.nav_building_scene_revision = state->map_scene_revision;
        for(lv_obj_t *building : ui.nav_buildings) {
            lv_obj_add_flag(building, LV_OBJ_FLAG_HIDDEN);
        }
        if(had_buildings && ui.nav_building_fill != nullptr) {
            lv_obj_invalidate(ui.nav_building_fill);
        }
        return;
    }

    bool spans_changed =
        ui.nav_building_scene_revision != state->map_scene_revision ||
        ui.nav_building_point_count != point_count ||
        ui.nav_building_footprint_count != footprint_count;
    for(std::uint8_t i = 0; i < point_count; ++i) {
        ui.nav_building_target_x[i] = state->building_points[i].x;
        ui.nav_building_target_y[i] = state->building_points[i].y;
    }
    for(std::uint8_t i = 0; i < footprint_count; ++i) {
        const moto_ui_building_span_t next = state->building_footprints[i];
        spans_changed = spans_changed ||
                        ui.nav_building_spans[i].first_point_index !=
                            next.first_point_index ||
                        ui.nav_building_spans[i].point_count !=
                            next.point_count ||
                        ui.nav_building_spans[i].building_class !=
                            next.building_class;
        ui.nav_building_spans[i] = next;
    }

    const bool snap = spans_changed || ui.reduce_motion;
    ui.nav_building_point_count = point_count;
    ui.nav_building_footprint_count = footprint_count;
    ui.nav_building_scene_revision = state->map_scene_revision;
    if(snap) {
        for(std::uint8_t i = 0; i < point_count; ++i) {
            ui.nav_building_x[i] = ui.nav_building_target_x[i];
            ui.nav_building_y[i] = ui.nav_building_target_y[i];
        }
    }
    if(spans_changed) {
        for(std::uint8_t i = 0;
            i < MOTO_UI_BUILDING_FOOTPRINT_CAPACITY; ++i) {
            if(i < footprint_count) {
                const bool landmark =
                    ui.nav_building_spans[i].building_class == 1U;
                lv_obj_set_style_line_color(
                    ui.nav_buildings[i],
                    landmark ? ui.palette.building_landmark
                             : ui.palette.building, 0);
                lv_obj_set_style_line_width(
                    ui.nav_buildings[i], px(landmark ? 2 : 1), 0);
            }
        }
        apply_building_visibility();
    }
    if(snap) apply_building_geometry_frame();
}

void update_route_geometry(const moto_ui_state_t *state) {
    const std::uint8_t count = std::min<std::uint8_t>(
        state->route_point_count, MOTO_UI_ROUTE_POINT_CAPACITY);
    if(count < 2) {
        ui.nav_route_point_count = 0;
        ui.nav_route_target_count = 0;
        ui.nav_route_identity = state->route_identity;
        ui.nav_route_generation = state->route_generation;
        // The only objects that may legitimately be absent while the UI is
        // live: the route layer is optional, unlike the page tree, which the
        // `live` gate already guarantees.
        if(ui.nav_route_shadow != nullptr) {
            lv_obj_add_flag(ui.nav_route_shadow, LV_OBJ_FLAG_HIDDEN);
        }
        if(ui.nav_route != nullptr) {
            lv_obj_add_flag(ui.nav_route, LV_OBJ_FLAG_HIDDEN);
        }
        if(ui.nav_marker != nullptr) {
            lv_obj_add_flag(ui.nav_marker, LV_OBJ_FLAG_HIDDEN);
        }
        return;
    }

    for(std::uint8_t i = 0; i < count; ++i) {
        ui.nav_route_target_x[i] = state->route_points[i].x;
        ui.nav_route_target_y[i] = state->route_points[i].y;
    }

    // A new route or a wholesale reroute should appear immediately. Ordinary
    // GNSS/IMU updates are blended at 40 Hz so the road glides under the fixed
    // rider marker instead of jumping from one phone fix to the next.
    const bool snap = ui.nav_route_identity != state->route_identity ||
                      ui.nav_route_generation != state->route_generation ||
                      ui.nav_route_point_count != count || ui.reduce_motion;
    ui.nav_route_target_count = count;
    ui.nav_route_point_count = count;
    ui.nav_route_identity = state->route_identity;
    ui.nav_route_generation = state->route_generation;
    if(snap) {
        for(std::uint8_t i = 0; i < count; ++i) {
            ui.nav_route_x[i] = ui.nav_route_target_x[i];
            ui.nav_route_y[i] = ui.nav_route_target_y[i];
        }
    }

    lv_obj_remove_flag(ui.nav_route_shadow, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(ui.nav_route, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(ui.nav_marker, LV_OBJ_FLAG_HIDDEN);
    if(snap) apply_route_geometry_frame();
}

lv_point_precise_t arrow_base(const lv_point_precise_t& previous,
                              const lv_point_precise_t& tip,
                              double head_length = px(24.0)) {
    const double dx = static_cast<double>(tip.x - previous.x);
    const double dy = static_cast<double>(tip.y - previous.y);
    const double length = std::max(1.0, std::hypot(dx, dy));
    const double ux = dx / length;
    const double uy = dy / length;
    return {
        static_cast<lv_value_precise_t>(tip.x - ux * head_length),
        static_cast<lv_value_precise_t>(tip.y - uy * head_length),
    };
}

void draw_arrowhead(lv_layer_t *layer, const lv_area_t& area,
                    const lv_point_precise_t& previous,
                    const lv_point_precise_t& tip) {
    const double dx = static_cast<double>(tip.x - previous.x);
    const double dy = static_cast<double>(tip.y - previous.y);
    const double length = std::max(1.0, std::hypot(dx, dy));
    const double ux = dx / length;
    const double uy = dy / length;
    const auto base = arrow_base(previous, tip);
    const double wing = px(18.0);

    lv_draw_triangle_dsc_t triangle;
    lv_draw_triangle_dsc_init(&triangle);
    triangle.color = kWhite;
    triangle.opa = LV_OPA_COVER;
    triangle.p[0] = {
        static_cast<lv_value_precise_t>(area.x1 + tip.x),
        static_cast<lv_value_precise_t>(area.y1 + tip.y),
    };
    triangle.p[1] = {
        static_cast<lv_value_precise_t>(area.x1 + base.x - uy * wing),
        static_cast<lv_value_precise_t>(area.y1 + base.y + ux * wing),
    };
    triangle.p[2] = {
        static_cast<lv_value_precise_t>(area.x1 + base.x + uy * wing),
        static_cast<lv_value_precise_t>(area.y1 + base.y - ux * wing),
    };
    lv_draw_triangle(layer, &triangle);
}

void draw_maneuver_icon(lv_event_t *event) {
    lv_obj_t *object = lv_event_get_target_obj(event);
    lv_area_t area;
    lv_obj_get_coords(object, &area);
    lv_layer_t *layer = lv_event_get_layer(event);

    lv_point_precise_t points[8]{};
    int count = 0;
    switch(ui.nav_maneuver_type) {
        case MOTO_MANEUVER_RIGHT:
            points[0] = {px(34), px(102)};
            points[1] = {px(34), px(63)};
            points[2] = {px(55), px(42)};
            points[3] = {px(105), px(42)};
            count = 4;
            break;
        case MOTO_MANEUVER_LEFT:
            points[0] = {px(82), px(102)};
            points[1] = {px(82), px(63)};
            points[2] = {px(61), px(42)};
            points[3] = {px(11), px(42)};
            count = 4;
            break;
        case MOTO_MANEUVER_SLIGHT_RIGHT:
            points[0] = {px(36), px(102)};
            points[1] = {px(36), px(72)};
            points[2] = {px(96), px(12)};
            count = 3;
            break;
        case MOTO_MANEUVER_SLIGHT_LEFT:
            points[0] = {px(80), px(102)};
            points[1] = {px(80), px(72)};
            points[2] = {px(20), px(12)};
            count = 3;
            break;
        case MOTO_MANEUVER_UTURN:
            points[0] = {px(88), px(102)};
            points[1] = {px(88), px(50)};
            points[2] = {px(82), px(33)};
            points[3] = {px(69), px(23)};
            points[4] = {px(51), px(20)};
            points[5] = {px(34), px(27)};
            points[6] = {px(25), px(43)};
            points[7] = {px(25), px(84)};
            count = 8;
            break;
        case MOTO_MANEUVER_ROUNDABOUT: {
            lv_draw_line_dsc_t stem;
            lv_draw_line_dsc_init(&stem);
            stem.color = kWhite;
            stem.width = px(11);
            stem.round_start = 1;
            stem.round_end = 1;
            stem.p1 = {static_cast<lv_value_precise_t>(area.x1 + px(58)),
                       static_cast<lv_value_precise_t>(area.y1 + px(101))};
            stem.p2 = {static_cast<lv_value_precise_t>(area.x1 + px(58)),
                       static_cast<lv_value_precise_t>(area.y1 + px(79))};
            lv_draw_line(layer, &stem);

            lv_draw_arc_dsc_t circle;
            lv_draw_arc_dsc_init(&circle);
            circle.color = kWhite;
            circle.width = px(11);
            circle.rounded = 1;
            circle.center = {
                static_cast<int32_t>(area.x1 + px(58)),
                static_cast<int32_t>(area.y1 + px(53)),
            };
            circle.radius = px(27);
            circle.start_angle = 86;
            circle.end_angle = 326;
            lv_draw_arc(layer, &circle);

            const lv_point_precise_t previous = {px(69), px(24)};
            const lv_point_precise_t tip = {px(86), px(36)};
            draw_arrowhead(layer, area, previous, tip);
            return;
        }
        case MOTO_MANEUVER_ARRIVE: {
            lv_draw_line_dsc_t pole;
            lv_draw_line_dsc_init(&pole);
            pole.color = kWhite;
            pole.width = px(11);
            pole.round_start = 1;
            pole.round_end = 1;
            pole.p1 = {static_cast<lv_value_precise_t>(area.x1 + px(42)),
                       static_cast<lv_value_precise_t>(area.y1 + px(101))};
            pole.p2 = {static_cast<lv_value_precise_t>(area.x1 + px(42)),
                       static_cast<lv_value_precise_t>(area.y1 + px(17))};
            lv_draw_line(layer, &pole);

            lv_draw_triangle_dsc_t flag;
            lv_draw_triangle_dsc_init(&flag);
            flag.color = kWhite;
            flag.opa = LV_OPA_COVER;
            flag.p[0] = {
                static_cast<lv_value_precise_t>(area.x1 + px(46)),
                static_cast<lv_value_precise_t>(area.y1 + px(20)),
            };
            flag.p[1] = {
                static_cast<lv_value_precise_t>(area.x1 + px(102)),
                static_cast<lv_value_precise_t>(area.y1 + px(39)),
            };
            flag.p[2] = {
                static_cast<lv_value_precise_t>(area.x1 + px(46)),
                static_cast<lv_value_precise_t>(area.y1 + px(58)),
            };
            lv_draw_triangle(layer, &flag);
            return;
        }
        case MOTO_MANEUVER_STRAIGHT:
        default:
            points[0] = {px(58), px(103)};
            points[1] = {px(58), px(10)};
            count = 2;
            break;
    }

    lv_point_precise_t absolute[8]{};
    const lv_point_precise_t base = arrow_base(points[count - 2],
                                                points[count - 1]);
    for(int i = 0; i < count; ++i) {
        const lv_point_precise_t point = i == count - 1 ? base : points[i];
        absolute[i] = {
            static_cast<lv_value_precise_t>(area.x1 + point.x),
            static_cast<lv_value_precise_t>(area.y1 + point.y),
        };
    }
    lv_draw_line_dsc_t line;
    lv_draw_line_dsc_init(&line);
    line.color = kWhite;
    line.width = px(12);
    line.round_start = 1;
    line.round_end = 1;
    line.points = absolute;
    line.point_cnt = count;
    lv_draw_line(layer, &line);
    draw_arrowhead(layer, area, points[count - 2], points[count - 1]);
}

void update_maneuver(moto_maneuver_t maneuver) {
    if(ui.nav_maneuver_type == maneuver) return;
    ui.nav_maneuver_type = maneuver;
    lv_obj_invalidate(ui.nav_maneuver);
}

void draw_segment(lv_layer_t *layer, lv_color_t color, int width,
                  lv_opa_t opacity, const lv_point_precise_t *points,
                  int count) {
    lv_draw_line_dsc_t line;
    lv_draw_line_dsc_init(&line);
    line.color = color;
    line.width = width;
    line.opa = opacity;
    line.round_start = 1;
    line.round_end = 1;
    // LVGL's descriptor predates const-correct polyline inputs; the draw task
    // only reads the points for the duration of this call.
    line.points = const_cast<lv_point_precise_t *>(points);
    line.point_cnt = count;
    lv_draw_line(layer, &line);
}

void set_layer_opacity(void *object, int32_t opacity) {
    lv_obj_set_style_opa(static_cast<lv_obj_t *>(object), opacity, 0);
}

void apply_lifecycle_content(LifecycleVisual visual) {
    ui.lifecycle_visual = visual;
    const char *kicker = "PHONE LINK";
    const char *title = "";
    const char *subtitle = "";
    // Every state is two lines: the headline is the poster, and a layout that
    // jumps between one and two lines would move the composition on each
    // transition.
    switch(visual) {
        case LifecycleVisual::PhoneOffline:
            title = "CONNECT\nPHONE";
            subtitle = "请打开手机应用";
            break;
        case LifecycleVisual::PhoneConnecting:
            title = "LINKING\nPHONE";
            subtitle = "正在建立连接";
            break;
        case LifecycleVisual::Connected:
            title = "PHONE\nLINKED";
            subtitle = "连接成功";
            break;
        case LifecycleVisual::Ready:
            kicker = "READY";
            title = "READY TO\nRIDE";
            subtitle = "请在手机选择目的地";
            break;
        case LifecycleVisual::Planning:
            kicker = "ROUTE";
            title = "BUILDING\nROUTE";
            subtitle = "正在规划路线";
            break;
        case LifecycleVisual::Hidden:
            break;
    }
    lv_label_set_text(ui.nav_lifecycle_kicker, kicker);
    lv_label_set_text(ui.nav_lifecycle_title, title);
    lv_label_set_text(ui.nav_lifecycle_subtitle, subtitle);
    lv_obj_set_style_text_color(ui.nav_lifecycle_title, ui.palette.ink, 0);
    lv_obj_set_style_text_color(
        ui.nav_lifecycle_subtitle,
        visual == LifecycleVisual::Connected ? ui.palette.accent
                                             : ui.palette.muted,
        0);
}

void lifecycle_fade_in();

void lifecycle_fade_out_complete(lv_anim_t *) {
    if(ui.nav_lifecycle == nullptr) return;
    if(ui.lifecycle_target == LifecycleVisual::Hidden) {
        ui.lifecycle_visual = LifecycleVisual::Hidden;
        lv_obj_add_flag(ui.nav_lifecycle, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_opa(ui.nav_lifecycle, LV_OPA_COVER, 0);
        return;
    }
    apply_lifecycle_content(ui.lifecycle_target);
    lifecycle_fade_in();
}

void lifecycle_fade_in() {
    lv_obj_remove_flag(ui.nav_lifecycle, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_opa(ui.nav_lifecycle, LV_OPA_TRANSP, 0);
    lv_anim_t fade;
    lv_anim_init(&fade);
    lv_anim_set_var(&fade, ui.nav_lifecycle);
    lv_anim_set_exec_cb(&fade, set_layer_opacity);
    lv_anim_set_values(&fade, LV_OPA_TRANSP, LV_OPA_COVER);
    lv_anim_set_duration(&fade, 220);
    lv_anim_set_path_cb(&fade, lv_anim_path_ease_out);
    lv_anim_start(&fade);
}

void transition_lifecycle(LifecycleVisual next) {
    if(ui.nav_lifecycle == nullptr) return;
    if(next == ui.lifecycle_target &&
       (next == ui.lifecycle_visual ||
        lv_obj_has_flag(ui.nav_lifecycle, LV_OBJ_FLAG_HIDDEN))) {
        return;
    }
    ui.lifecycle_target = next;
    lv_anim_delete(ui.nav_lifecycle, set_layer_opacity);
    if(ui.reduce_motion) {
        if(next == LifecycleVisual::Hidden) {
            ui.lifecycle_visual = LifecycleVisual::Hidden;
            lv_obj_add_flag(ui.nav_lifecycle, LV_OBJ_FLAG_HIDDEN);
        } else {
            apply_lifecycle_content(next);
            lv_obj_remove_flag(ui.nav_lifecycle, LV_OBJ_FLAG_HIDDEN);
            lv_obj_set_style_opa(ui.nav_lifecycle, LV_OPA_COVER, 0);
        }
        return;
    }
    if(ui.lifecycle_visual == LifecycleVisual::Hidden ||
       lv_obj_has_flag(ui.nav_lifecycle, LV_OBJ_FLAG_HIDDEN)) {
        if(next == LifecycleVisual::Hidden) return;
        apply_lifecycle_content(next);
        lifecycle_fade_in();
        return;
    }

    lv_anim_t fade;
    lv_anim_init(&fade);
    lv_anim_set_var(&fade, ui.nav_lifecycle);
    lv_anim_set_exec_cb(&fade, set_layer_opacity);
    lv_anim_set_values(&fade,
                       lv_obj_get_style_opa(ui.nav_lifecycle, LV_PART_MAIN),
                       LV_OPA_TRANSP);
    lv_anim_set_duration(&fade, 140);
    lv_anim_set_path_cb(&fade, lv_anim_path_ease_in);
    lv_anim_set_completed_cb(&fade, lifecycle_fade_out_complete);
    lv_anim_start(&fade);
}

LifecycleVisual desired_lifecycle_visual() {
    if(ui.demo_active) {
        return LifecycleVisual::Hidden;
    }
    if(ui.phone_connection == MOTO_UI_PHONE_OFFLINE) {
        return LifecycleVisual::PhoneOffline;
    }
    if(ui.phone_connection == MOTO_UI_PHONE_CONNECTING) {
        return LifecycleVisual::PhoneConnecting;
    }
    if(ui.lifecycle_success_active) {
        return LifecycleVisual::Connected;
    }
    if(ui.navigation_has_guidance) {
        return LifecycleVisual::Hidden;
    }
    if(ui.navigation_route_request_in_flight) {
        return LifecycleVisual::Planning;
    }
    return LifecycleVisual::Ready;
}

void refresh_lifecycle() {
    transition_lifecycle(desired_lifecycle_visual());
}

void lifecycle_tick(lv_timer_t *) {
    if(ui.reduce_motion || ui.nav_lifecycle_symbol == nullptr ||
       ui.lifecycle_visual == LifecycleVisual::Hidden) {
        return;
    }
    const std::uint16_t step =
        ui.lifecycle_visual == LifecycleVisual::PhoneConnecting ? 9U : 5U;
    ui.lifecycle_phase_deg =
        static_cast<std::uint16_t>((ui.lifecycle_phase_deg + step) % 360U);
    lv_obj_invalidate(ui.nav_lifecycle_symbol);
}

void connection_success_timeout(lv_timer_t *timer) {
    lv_timer_pause(timer);
    ui.lifecycle_success_active = false;
    refresh_lifecycle();
}

void set_navigation_guidance_visible(bool visible) {
    lv_obj_t *objects[] = {
        ui.nav_maneuver,
        ui.nav_distance,
        ui.nav_unit,
        ui.nav_ring,
        ui.nav_north,
    };
    for(lv_obj_t *object : objects) {
        if(visible) {
            lv_obj_remove_flag(object, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(object, LV_OBJ_FLAG_HIDDEN);
        }
    }
    if(!visible) {
        lv_obj_add_flag(ui.nav_limit, LV_OBJ_FLAG_HIDDEN);
        // The hint belongs to the instrument, not to the empty-state surface.
        lv_obj_add_flag(ui.nav_home_hint, LV_OBJ_FLAG_HIDDEN);
    }
}

void update_nav_status(const moto_ui_state_t *state) {
    const char *text = "";
    // v2: status colours are palette tokens, so a theme switch actually
    // changes them instead of leaving the old literals on the poster.
    lv_color_t color = ui.palette.muted;
    if(ui.demo_active) {
        lv_label_set_text(ui.nav_status, "DEMO RIDE");
        lv_obj_set_style_text_color(ui.nav_status, ui.palette.accent, 0);
        return;
    }
    if(state->mode != MOTO_UI_ARRIVED && state->route_point_count < 2) {
        // Empty navigation states are rendered by the full lifecycle surface,
        // never by a tiny diagnostic caption floating in a black screen.
        lv_label_set_text(ui.nav_status, "");
        return;
    }
    switch(state->mode) {
        case MOTO_UI_ACQUIRING_FIX:
            text = "ACQUIRING GPS";
            color = ui.palette.accent;
            break;
        case MOTO_UI_REROUTING:
            text = "REROUTING";
            color = ui.palette.accent;
            break;
        case MOTO_UI_OFFLINE: text = "ROUTE CACHED"; color = ui.palette.muted; break;
        case MOTO_UI_ARRIVED: text = "ARRIVED"; color = ui.palette.turquoise; break;
        case MOTO_UI_NAVIGATING:
            if(state->online == 0) {
                text = "NO HOTSPOT";
                color = ui.palette.accent;
            }
            break;
    }
    lv_label_set_text(ui.nav_status, text);
    lv_obj_set_style_text_color(ui.nav_status, color, 0);
}

// Defined with the navigation page; declared here because the gate is applied
// from update_navigation().
void update_home_shortcut();
void update_home_blip(const moto_ui_state_t *state);
void update_radar_ring(const moto_ui_state_t *state);

void update_navigation(const moto_ui_state_t *state) {
    update_building_geometry(state);
    update_road_geometry(state);
    update_route_geometry(state);
    update_maneuver(state->maneuver);
    update_nav_status(state);

    // Never suggest "go straight for 0 m" while there is no route. Waiting is
    // a connection state, not actionable navigation guidance.
    const bool has_guidance = ui.demo_active ||
                              state->mode == MOTO_UI_ARRIVED ||
                              state->route_point_count >= 2;
    ui.navigation_has_guidance = has_guidance;
    ui.navigation_route_request_in_flight =
        state->route_request_in_flight != 0;
    set_navigation_guidance_visible(has_guidance);
    update_home_shortcut();
    update_home_blip(state);
    refresh_lifecycle();
    if(!has_guidance) return;

    char value[16];
    const char *unit = "m";
    if(state->distance_to_maneuver_m >= 1000) {
        std::snprintf(value, sizeof(value), "%.1f",
                      state->distance_to_maneuver_m / 1000.0);
        unit = "km";
    } else {
        std::snprintf(value, sizeof(value), "%u",
                      static_cast<unsigned>(state->distance_to_maneuver_m));
    }
    if(state->mode == MOTO_UI_ARRIVED) std::snprintf(value, sizeof(value), "0");
    lv_label_set_text(ui.nav_distance, value);
    lv_label_set_text(ui.nav_unit, unit);
    // This number is never the total trip distance. Its physical attachment to
    // the maneuver glyph makes that meaning clear without an explanatory label.
    lv_obj_align(ui.nav_distance, LV_ALIGN_TOP_LEFT, px(151), px(244));
    lv_obj_align_to(ui.nav_unit, ui.nav_distance, LV_ALIGN_OUT_RIGHT_BOTTOM,
                    px(6), px(-7));

    update_radar_ring(state);

    if(state->speed_limit_kph > 0) {
        char limit[8];
        std::snprintf(limit, sizeof(limit), "%u",
                      static_cast<unsigned>(state->speed_limit_kph));
        lv_label_set_text(ui.nav_limit_value, limit);
        lv_obj_remove_flag(ui.nav_limit, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(ui.nav_limit, LV_OBJ_FLAG_HIDDEN);
    }
}

void update_speedometer(const moto_ui_state_t *state) {
    if(ui.speed_value == nullptr) return;
    const bool moving = state->speed_kph > 0;
    char value[8];
    if(moving) {
        std::snprintf(value, sizeof(value), "%u",
                      static_cast<unsigned>(state->speed_kph));
    } else {
        std::snprintf(value, sizeof(value), "0");
    }
    lv_label_set_text(ui.speed_value, value);
    if(ui.speed_drawn != state->speed_kph) {
        ui.speed_drawn = state->speed_kph;
        lv_obj_invalidate(ui.speed_gauge);
    }
    if(state->speed_limit_kph > 0) {
        std::snprintf(value, sizeof(value), "%u",
                      static_cast<unsigned>(state->speed_limit_kph));
        lv_label_set_text(ui.speed_limit_value, value);
        lv_obj_remove_flag(ui.speed_limit, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(ui.speed_limit, LV_OBJ_FLAG_HIDDEN);
    }
}

void update_compass(const moto_ui_state_t *state) {
    if(ui.compass_cardinal == nullptr) return;
    char heading[12];
    std::snprintf(heading, sizeof(heading), "%03u°",
                  static_cast<unsigned>(state->heading_deg));
    lv_label_set_text(ui.compass_heading, heading);
    lv_label_set_text(ui.compass_cardinal, cardinal_name(state->heading_deg));
    char speed[20];
    std::snprintf(speed, sizeof(speed), "%u KM/H",
                  static_cast<unsigned>(state->speed_kph));
    lv_label_set_text(ui.compass_speed, speed);

    if(ui.compass_drawn_heading != state->heading_deg) {
        ui.compass_drawn_heading = state->heading_deg;
        lv_obj_invalidate(ui.compass_marks);
    }

}

const char *perf_state_word() {
    switch(ui.perf_state) {
        case Ui::PerfState::Running: return "LAUNCH";
        case Ui::PerfState::Launched: return "DONE";
        case Ui::PerfState::Armed:
        default: return "ARMED";
    }
}

void perf_set_elapsed(std::uint32_t centis) {
    char text[16];
    std::snprintf(text, sizeof(text), "%u.%02u",
                  static_cast<unsigned>(centis / 100U),
                  static_cast<unsigned>(centis % 100U));
    lv_label_set_text(ui.perf_timer, text);
}

void perf_apply_chip() {
    if(ui.perf_chip_text == nullptr) return;
    lv_label_set_text(ui.perf_chip_text, perf_state_word());
}

void perf_fill_strip(std::uint16_t speed_kph) {
    const int filled = static_cast<int>(
        std::min<int>(kPerfSegmentCount,
                      static_cast<int>(speed_kph) * kPerfSegmentCount /
                          kPerfTargetKph));
    for(int i = 0; i < kPerfSegmentCount; ++i) {
        // Filled segments use the accent once the run is done and ink while
        // it is live; unfilled segments stay a faint plate.
        const bool on = i < filled;
        lv_obj_set_style_bg_opa(ui.perf_segments[i],
                                on ? LV_OPA_COVER : LV_OPA_20, 0);
        lv_obj_set_style_bg_color(
            ui.perf_segments[i],
            (on && ui.perf_state == Ui::PerfState::Launched) ? ui.palette.accent
                                                             : ui.palette.ink,
            0);
    }
}

void perf_publish_best() {
    if(ui.perf_best == nullptr) return;
    char text[24];
    if(ui.perf_best_centis == 0) {
        std::snprintf(text, sizeof(text), "BEST --.-");
    } else {
        std::snprintf(text, sizeof(text), "BEST %u.%02u",
                      ui.perf_best_centis / 100U, ui.perf_best_centis % 100U);
    }
    lv_label_set_text(ui.perf_best, text);
}

// Runs on the LVGL thread, so it can read the retained state without a lock.
void perf_tick(lv_timer_t *) {
    if(ui.perf_timer == nullptr || !ui.live) return;
    if(ui.perf_state != Ui::PerfState::Running) return;
    const std::uint32_t elapsed = lv_tick_elaps(ui.perf_run_start_ms);
    perf_set_elapsed(elapsed / 10U);
}

void update_performance(const moto_ui_state_t *state) {
    if(ui.perf_timer == nullptr) return;

    const std::uint16_t speed = state->speed_kph;
    const std::uint32_t now = lv_tick_get();
    switch(ui.perf_state) {
        case Ui::PerfState::Armed:
            // Rolling away from a standstill starts the clock; the strip is
            // the only thing that moves until then.
            if(speed >= 1) {
                ui.perf_state = Ui::PerfState::Running;
                ui.perf_run_start_ms = now;
                perf_set_elapsed(0);
            }
            break;
        case Ui::PerfState::Running:
            if(speed >= kPerfTargetKph) {
                const std::uint32_t centis =
                    lv_tick_elaps(ui.perf_run_start_ms) / 10U;
                ui.perf_last_centis = centis;
                if(ui.perf_best_centis == 0 || centis < ui.perf_best_centis) {
                    ui.perf_best_centis =
                        static_cast<std::uint16_t>(std::min<std::uint32_t>(
                            centis, 0xFFFFU));
                    perf_publish_best();
                    if(ui.perf_delta != nullptr) {
                        lv_label_set_text(ui.perf_delta, "PB");
                    }
                } else {
                    char text[16];
                    const std::int32_t diff =
                        static_cast<std::int32_t>(centis) -
                        static_cast<std::int32_t>(ui.perf_best_centis);
                    std::snprintf(
                        text, sizeof(text), "%+d.%02u",
                        static_cast<int>(diff / 100),
                        static_cast<unsigned>(std::abs(diff) % 100));
                    if(ui.perf_delta != nullptr) {
                        lv_label_set_text(ui.perf_delta, text);
                    }
                }
                ui.perf_state = Ui::PerfState::Launched;
            } else {
                perf_set_elapsed(lv_tick_elaps(ui.perf_run_start_ms) / 10U);
            }
            break;
        case Ui::PerfState::Launched:
            // Rearm only after the bike has actually stopped and stayed
            // stopped, so coasting through a junction does not wipe the run.
            if(speed == 0) {
                if(ui.perf_still_since_ms == 0) {
                    ui.perf_still_since_ms = now;
                } else if(lv_tick_elaps(ui.perf_still_since_ms) >=
                          kPerfRearmStillMs) {
                    ui.perf_state = Ui::PerfState::Armed;
                    ui.perf_still_since_ms = 0;
                    if(ui.perf_delta != nullptr) {
                        lv_label_set_text(ui.perf_delta, "");
                    }
                    lv_label_set_text(ui.perf_timer, "-.-");
                }
            } else {
                ui.perf_still_since_ms = 0;
            }
            break;
    }
    perf_apply_chip();
    perf_fill_strip(speed);

}

void update_lean(const moto_ui_state_t *state) {
    if(ui.lean_value == nullptr) return;
    const bool has = state->has_attitude != 0;
    const float lean = has ? static_cast<float>(state->lean_deg) : 0.0F;

    char text[24];
    if(has) {
        std::snprintf(text, sizeof(text), "%+0.0f°",
                      static_cast<double>(lean));
        lv_label_set_text(ui.lean_value, text);
        ui.lean_peak_left = std::max(ui.lean_peak_left, -lean);
        ui.lean_peak_right = std::max(ui.lean_peak_right, lean);
        if(ui.lean_peak_left > 0.5F) {
            std::snprintf(text, sizeof(text), "L %0.0f°",
                          static_cast<double>(ui.lean_peak_left));
            lv_label_set_text(ui.lean_max_l, text);
        } else {
            lv_label_set_text(ui.lean_max_l, "L --");
        }
        if(ui.lean_peak_right > 0.5F) {
            std::snprintf(text, sizeof(text), "R %0.0f°",
                          static_cast<double>(ui.lean_peak_right));
            lv_label_set_text(ui.lean_max_r, text);
        } else {
            lv_label_set_text(ui.lean_max_r, "R --");
        }
        lv_obj_add_flag(ui.lean_state, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(ui.lean_scene, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(ui.lean_blip, LV_OBJ_FLAG_HIDDEN);
    } else {
        // A 176 px hyphen renders as two solid bricks, not a dash. The NO IMU
        // plate carries the state; leave the poster's centre empty.
        lv_label_set_text(ui.lean_value, "");
        lv_label_set_text(ui.lean_max_l, "L --");
        lv_label_set_text(ui.lean_max_r, "R --");
        lv_obj_remove_flag(ui.lean_state, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(ui.lean_blip, LV_OBJ_FLAG_HIDDEN);
    }

    if(ui.lean_has != has || ui.lean_scene_dirty ||
       std::fabs(ui.lean_deg - lean) > 0.05F) {
        ui.lean_has = has;
        ui.lean_deg = lean;
        ui.lean_scene_dirty = false;
        lv_obj_invalidate(ui.lean_scene);
    }

    if(has) {
        // Rim position: 0 at twelve o'clock, positive clockwise. Radius 196
        // sits on the casing, so the blip reads as part of the instrument
        // rather than as another label.
        const double rad = static_cast<double>(lean) * kPi / 180.0;
        const double cx = MOTO_UI_CANVAS_WIDTH / 2.0;
        const double cy = MOTO_UI_CANVAS_HEIGHT / 2.0;
        const int x = static_cast<int>(std::lround(cx + std::sin(rad) * 196.0));
        const int y = static_cast<int>(std::lround(cy - std::cos(rad) * 196.0));
        lv_obj_set_pos(ui.lean_blip, x - 7, y - 7);
    }
}

void update_music_view() {
    if(ui.music_source == nullptr) return;
    lv_label_set_text(ui.music_source,
                      ui.music.connected ? ui.music_source_text
                                         : "NO SIGNAL");
    lv_label_set_text(ui.music_title, ui.music_title_text);
    lv_label_set_text(ui.music_artist, ui.music_artist_text);
    lv_label_set_text(ui.music_button_labels[1],
                      ui.music.playing ? "II" : ">");
    lv_label_set_text(ui.music_button_labels[3],
                      ui.music.liked ? "SENT" : "LIKE");
    if(ui.music.like_available) {
        lv_obj_remove_flag(ui.music_buttons[3], LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(ui.music_buttons[3], LV_OBJ_FLAG_HIDDEN);
    }
}

void music_button_event(lv_event_t *event) {
    const auto command = static_cast<moto_music_command_t>(
        reinterpret_cast<std::intptr_t>(lv_event_get_user_data(event)));
    if(command == MOTO_MUSIC_TOGGLE_PLAYBACK) {
        ui.music.playing = ui.music.playing == 0;
    } else if(command == MOTO_MUSIC_LIKE && ui.music.like_available) {
        ui.music.liked = 1;
    }
    update_music_view();
    if(ui.music_callback != nullptr) ui.music_callback(command, ui.music_callback_context);
}

void create_page_dots() {
    for(int i = 0; i < MOTO_UI_PAGE_COUNT; ++i) {
        ui.page_dots[i] = lv_obj_create(ui.screen);
        lv_obj_remove_style_all(ui.page_dots[i]);
        lv_obj_set_size(ui.page_dots[i], px(5), px(5));
        lv_obj_set_y(ui.page_dots[i], px(337));
        lv_obj_set_style_bg_opa(ui.page_dots[i], LV_OPA_COVER, 0);
        lv_obj_set_style_radius(ui.page_dots[i], px(3), 0);
        lv_obj_clear_flag(ui.page_dots[i], LV_OBJ_FLAG_CLICKABLE);
    }
    ui.page_dots_timer = lv_timer_create(hide_page_dots,
                                         kPageDotsVisibleMs, nullptr);
    update_page_dots();
    reveal_page_dots();
}

void home_shortcut_event(lv_event_t *) {
    // The glyph is hidden above the gate; refusing here as well means a queued
    // or synthetic tap cannot start a route while the bike is moving.
    if(ui.nav_speed_kph > kHomeShortcutMaxSpeedKph) return;
    if(ui.home_callback != nullptr) ui.home_callback(ui.home_callback_context);
}

void update_home_shortcut() {
    if(ui.nav_home == nullptr) return;
    if(ui.nav_speed_kph > kHomeShortcutMaxSpeedKph) {
        lv_obj_add_flag(ui.nav_home, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_remove_flag(ui.nav_home, LV_OBJ_FLAG_HIDDEN);
    }
}

void update_home_blip(const moto_ui_state_t *state) {
    if(ui.nav_home_blip == nullptr) return;
    if(state->has_home == 0) {
        lv_obj_add_flag(ui.nav_home_blip, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    const int32_t size = px(kHomeBlipSizeDesign);
    const int32_t centre_x = MOTO_UI_CANVAS_WIDTH / 2;
    const int32_t centre_y = MOTO_UI_CANVAS_HEIGHT / 2;
    const int32_t clamp_radius =
        MOTO_UI_RADAR_RING_RADIUS_PX - size / 2 - px(2);
    int32_t dx = state->home_x - centre_x;
    int32_t dy = state->home_y - centre_y;
    const std::int64_t distance_sq =
        static_cast<std::int64_t>(dx) * dx + static_cast<std::int64_t>(dy) * dy;
    if(distance_sq >
       static_cast<std::int64_t>(clamp_radius) * clamp_radius) {
        // A home three kilometres away must still be findable: keep its bearing
        // and pin it to the ring instead of dropping the safehouse entirely.
        const double distance = std::sqrt(static_cast<double>(distance_sq));
        dx = static_cast<int32_t>(std::lround(dx / distance * clamp_radius));
        dy = static_cast<int32_t>(std::lround(dy / distance * clamp_radius));
    }
    lv_obj_set_pos(ui.nav_home_blip, centre_x + dx - size / 2,
                   centre_y + dy - size / 2);
    lv_obj_remove_flag(ui.nav_home_blip, LV_OBJ_FLAG_HIDDEN);
}

void update_radar_ring(const moto_ui_state_t *state) {
    if(ui.nav_ring != nullptr) {
        const std::uint8_t progress = static_cast<std::uint8_t>(
            std::min<int>(100, std::max<int>(0, state->route_progress_percent)));
        if(progress != ui.nav_radar_progress ||
           state->traffic != ui.nav_radar_traffic) {
            ui.nav_radar_progress = progress;
            ui.nav_radar_traffic = state->traffic;
            lv_obj_invalidate(ui.nav_ring);
        }
    }
    if(ui.nav_north == nullptr) return;
    // Heading-up map: north is wherever the rider is pointing away from. A
    // badge pinned to twelve o'clock would be a lie on a rotating map.
    const double heading =
        static_cast<double>(state->heading_deg) * kPi / 180.0;
    const int32_t centre_x = MOTO_UI_CANVAS_WIDTH / 2;
    const int32_t centre_y = MOTO_UI_CANVAS_HEIGHT / 2;
    const int32_t size = px(kNorthBadgeSizeDesign);
    const int32_t x = centre_x - static_cast<int32_t>(
                           std::lround(std::sin(heading) *
                                       MOTO_UI_RADAR_RING_RADIUS_PX));
    const int32_t y = centre_y - static_cast<int32_t>(
                           std::lround(std::cos(heading) *
                                       MOTO_UI_RADAR_RING_RADIUS_PX));
    lv_obj_set_pos(ui.nav_north, x - size / 2, y - size / 2);
}

void draw_radar_ring(lv_event_t *event) {
    draw_instrument_frame(event);
    if(ui.nav_radar_progress == 0) return;
    lv_layer_t *layer = lv_event_get_layer(event);
    lv_draw_arc_dsc_t dsc;
    lv_draw_arc_dsc_init(&dsc);
    dsc.center = {MOTO_UI_CANVAS_WIDTH / 2, MOTO_UI_CANVAS_HEIGHT / 2};
    dsc.radius = kDialBandOuter;
    dsc.width = kDialBandWidth;
    dsc.color = ui.palette.map_ink;
    dsc.opa = LV_OPA_COVER;
    dsc.rounded = 0;
    // The band is the progress bar: cream replaces magenta clockwise from
    // twelve o'clock, the same grammar the speed page uses.
    double cursor = 270.0;
    double left = ui.nav_radar_progress * 3.6;

    while(left > 0.001) {
        const double step = std::min(90.0, left);
        dsc.start_angle = static_cast<lv_value_precise_t>(cursor);
        dsc.end_angle = static_cast<lv_value_precise_t>(cursor + step);
        lv_draw_arc(layer, &dsc);
        cursor += step;
        if(cursor >= 360.0) cursor -= 360.0;
        left -= step;
    }
}

void hide_home_hint(lv_timer_t *timer) {
    lv_timer_pause(timer);
    if(ui.nav_home_hint != nullptr) {
        lv_obj_add_flag(ui.nav_home_hint, LV_OBJ_FLAG_HIDDEN);
    }
}

void create_navigation_page() {
    lv_obj_t *page = ui.pages[MOTO_UI_PAGE_NAVIGATION];
    ui.nav_map = make_layer(page);
    // The map is the topmost hit target across most of the navigation page.
    // Forward swipe gestures to the page/screen. Local demo activation is not
    // bound to a long press: a glove, mount or palm must never replace active
    // phone guidance with the built-in fixture while riding.
    lv_obj_add_flag(ui.nav_map, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_add_flag(ui.nav_map, LV_OBJ_FLAG_GESTURE_BUBBLE);
    // The Radar owns the whole round canvas; the HUD is overlaid on top.
    lv_obj_set_height(ui.nav_map, MOTO_UI_CANVAS_HEIGHT);
    // The map is the paper disc, so it is clipped to the dial and the bezel is
    // drawn over its edge. Nothing square ever shows inside the glass.
    lv_obj_set_style_radius(ui.nav_map, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_clip_corner(ui.nav_map, true, 0);
    lv_obj_set_style_bg_color(ui.nav_map, ui.palette.background, 0);
    lv_obj_set_style_bg_opa(ui.nav_map, LV_OPA_COVER, 0);

    // The map owns a solid land colour so the GTA day theme can switch the
    // Radar background without changing the speed/compass/music pages.
    ui.nav_map_background = lv_obj_create(ui.nav_map);
    lv_obj_remove_style_all(ui.nav_map_background);
    lv_obj_remove_flag(ui.nav_map_background, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(ui.nav_map_background, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(ui.nav_map_background, MOTO_UI_CANVAS_WIDTH,
                    MOTO_UI_CANVAS_HEIGHT);
    lv_obj_set_pos(ui.nav_map_background, 0, 0);
    lv_obj_set_style_bg_color(ui.nav_map_background,
                              ui.palette.background, 0);
    lv_obj_set_style_bg_opa(ui.nav_map_background, LV_OPA_COVER, 0);

    // Footprint fills sit beneath the building outlines. A triangle fan from
    // the footprint centroid gives the GTA-style flat blocks without a
    // polygon fill primitive or a bitmap asset.
    ui.nav_building_fill = lv_obj_create(ui.nav_map);
    lv_obj_remove_style_all(ui.nav_building_fill);
    // Matches the default of the settings flag; the phone's replay unhides it.
    lv_obj_add_flag(ui.nav_building_fill, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(ui.nav_building_fill, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(ui.nav_building_fill, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(ui.nav_building_fill, MOTO_UI_CANVAS_WIDTH, MOTO_UI_CANVAS_HEIGHT);
    lv_obj_set_pos(ui.nav_building_fill, 0, 0);
    lv_obj_add_event_cb(ui.nav_building_fill, draw_building_fills,
                        LV_EVENT_DRAW_MAIN, nullptr);
    // Real building footprints are quiet closed outlines beneath both roads
    // and the selected route. No inferred rectangles or decorative fill is
    // generated on-device.
    for(std::uint8_t i = 0;
        i < MOTO_UI_BUILDING_FOOTPRINT_CAPACITY; ++i) {
        ui.nav_buildings[i] = create_map_polyline(ui.nav_map,
                                                  ui.nav_building_lines[i]);
        lv_obj_set_size(ui.nav_buildings[i], MOTO_UI_CANVAS_WIDTH, MOTO_UI_CANVAS_HEIGHT);
        lv_obj_set_style_line_width(ui.nav_buildings[i], px(1), 0);
        lv_obj_set_style_line_color(ui.nav_buildings[i],
                                     ui.palette.building, 0);
        lv_obj_set_style_line_opa(ui.nav_buildings[i], LV_OPA_COVER, 0);
        lv_obj_set_style_line_rounded(ui.nav_buildings[i], false, 0);
        lv_obj_add_flag(ui.nav_buildings[i], LV_OBJ_FLAG_HIDDEN);
    }

    // Quiet, real street context sits behind the selected route. Each road is
    // its own LVGL line so disconnected streets are never joined by a fake
    // diagonal. The bundled Jinan fixture uses all eight bounded slots; a
    // future online provider must simplify its response to the same limit.
    for(std::uint8_t i = 0; i < MOTO_UI_ROAD_POLYLINE_CAPACITY; ++i) {
        ui.nav_roads[i] = create_map_polyline(ui.nav_map, ui.nav_road_lines[i]);
        lv_obj_set_size(ui.nav_roads[i], MOTO_UI_CANVAS_WIDTH, MOTO_UI_CANVAS_HEIGHT);
        lv_obj_set_style_line_width(ui.nav_roads[i], px(3), 0);
        lv_obj_set_style_line_color(ui.nav_roads[i], ui.palette.road, 0);
        lv_obj_set_style_line_opa(ui.nav_roads[i], LV_OPA_COVER, 0);
        lv_obj_set_style_line_rounded(ui.nav_roads[i], true, 0);
        lv_obj_add_flag(ui.nav_roads[i], LV_OBJ_FLAG_HIDDEN);
    }

    ui.nav_route_shadow = create_map_polyline(ui.nav_map, ui.nav_route_shadow_line);
    lv_obj_set_size(ui.nav_route_shadow, MOTO_UI_CANVAS_WIDTH, MOTO_UI_CANVAS_HEIGHT);
    lv_obj_set_style_line_width(ui.nav_route_shadow, px(13), 0);
    lv_obj_set_style_line_color(ui.nav_route_shadow, ui.palette.route_shadow, 0);
    lv_obj_set_style_line_rounded(ui.nav_route_shadow, true, 0);
    ui.nav_route = create_map_polyline(ui.nav_map, ui.nav_route_line);
    lv_obj_set_size(ui.nav_route, MOTO_UI_CANVAS_WIDTH, MOTO_UI_CANVAS_HEIGHT);
    lv_obj_set_style_line_width(ui.nav_route, px(6), 0);
    lv_obj_set_style_line_color(ui.nav_route, ui.palette.route, 0);
    lv_obj_set_style_line_rounded(ui.nav_route, true, 0);
    ui.nav_route_motion_timer = lv_timer_create(route_motion_tick,
                                                 kRouteMotionFrameMs,
                                                 nullptr);

    // Home blip. It is created before the rider arrow so the arrow wins when
    // both land on the same spot, which is exactly what arriving home looks
    // like. Hidden until the phone sends a saved home.
    ui.nav_home_blip = lv_obj_create(ui.nav_map);
    lv_obj_remove_style_all(ui.nav_home_blip);
    lv_obj_set_size(ui.nav_home_blip, px(kHomeBlipSizeDesign),
                    px(kHomeBlipSizeDesign));
    lv_obj_clear_flag(ui.nav_home_blip, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_name_static(ui.nav_home_blip, "moto-home-blip");
    lv_obj_add_event_cb(ui.nav_home_blip, draw_home_blip,
                        LV_EVENT_DRAW_MAIN_END, nullptr);
    lv_obj_add_flag(ui.nav_home_blip, LV_OBJ_FLAG_HIDDEN);

    ui.nav_marker = lv_obj_create(ui.nav_map);
    lv_obj_remove_style_all(ui.nav_marker);
    lv_obj_set_size(ui.nav_marker, px(35), px(37));
    // Keep the arrow's bounding-box centre on the presenter's (233,233)
    // rider anchor; the marker used to sit at the old map-strip anchor.
    lv_obj_set_pos(ui.nav_marker, px(163), px(162));
    lv_obj_clear_flag(ui.nav_marker, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(ui.nav_marker, draw_vehicle_marker,
                        LV_EVENT_DRAW_MAIN_END, nullptr);

    ui.nav_status = make_text(page, &moto_font_display_24, TextRole::MapInk, "");
    lv_obj_set_width(ui.nav_status, px(240));
    lv_obj_set_style_text_align(ui.nav_status, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(ui.nav_status, LV_ALIGN_TOP_MID, 0, px(44));

    // The next action is the navigation page's single visual hero. The short
    // road window above is deliberately quieter and contains no fake side
    // streets; this standard symbol must be understood in one glance.
    ui.nav_maneuver = lv_obj_create(page);
    lv_obj_remove_style_all(ui.nav_maneuver);
    lv_obj_set_size(ui.nav_maneuver, px(116), px(108));
    lv_obj_set_pos(ui.nav_maneuver, px(29), px(220));
    lv_obj_clear_flag(ui.nav_maneuver, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(ui.nav_maneuver, draw_maneuver_icon,
                        LV_EVENT_DRAW_MAIN_END, nullptr);

    // The single number a moving rider reads: the distance to the next
    // action, printed at instrument scale, in the game HUD's cream-on-black.
    ui.nav_distance = make_text(page, &moto_font_timer_96, TextRole::MapInk,
                                "300");
    ui.nav_unit = make_text(page, &moto_font_display_24, TextRole::MapInk,
                            "m");

    // Keep the optional demo/SDK-provided value inside the circular safe area.
    // Real AMap Web-Service snapshots use zero and remain hidden because that
    // API does not return a road speed limit.
    ui.nav_limit = make_speed_limit_sign(page, 61);
    lv_obj_set_pos(ui.nav_limit, px(240), px(96));
    ui.nav_limit_value = lv_obj_get_child(ui.nav_limit, 0);

    // The rim is the gauge. It is drawn on its own full-canvas layer above the
    // map so the route, the blip and the sweep can never paint over it.
    ui.nav_ring = make_layer(page);
    lv_obj_remove_flag(ui.nav_ring, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_name_static(ui.nav_ring, "moto-radar-ring");
    lv_obj_add_event_cb(ui.nav_ring, draw_radar_ring, LV_EVENT_DRAW_MAIN,
                        nullptr);

    ui.nav_north = lv_obj_create(page);
    lv_obj_remove_style_all(ui.nav_north);
    lv_obj_set_size(ui.nav_north, px(kNorthBadgeSizeDesign),
                    px(kNorthBadgeSizeDesign));
    lv_obj_set_style_radius(ui.nav_north, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(ui.nav_north, kBlack, 0);
    lv_obj_set_style_bg_opa(ui.nav_north, LV_OPA_80, 0);
    lv_obj_clear_flag(ui.nav_north, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_name_static(ui.nav_north, "moto-compass-north");
    lv_obj_t *north_glyph =
        make_text(ui.nav_north, &moto_font_display_18, TextRole::Ink, "N");
    lv_obj_center(north_glyph);

    // One line, three seconds, and only when the phone refuses the tap. The
    // device cannot know whether a home exists; guessing would be worse than
    // saying nothing.
    ui.nav_home_hint = make_text(page, &moto_font_display_18, TextRole::Accent,
                                  "SET HOME ON PHONE");
    lv_obj_set_name_static(ui.nav_home_hint, "moto-home-hint");
    lv_obj_set_style_bg_color(ui.nav_home_hint, kBlack, 0);
    lv_obj_set_style_bg_opa(ui.nav_home_hint, LV_OPA_80, 0);
    lv_obj_set_style_pad_hor(ui.nav_home_hint, px(6), 0);
    lv_obj_set_style_pad_ver(ui.nav_home_hint, px(2), 0);
    lv_obj_align(ui.nav_home_hint, LV_ALIGN_TOP_MID, 0, px(40));
    lv_obj_add_flag(ui.nav_home_hint, LV_OBJ_FLAG_HIDDEN);
    ui.nav_home_hint_timer = lv_timer_create(hide_home_hint, kHomeHintMs,
                                             nullptr);
    lv_timer_pause(ui.nav_home_hint_timer);

    // Route home. Deliberately a small stop-time control, not part of the
    // riding HUD: it disappears above the speed gate so a glove or a mount
    // cannot replace active guidance with a plan home.
    ui.nav_home = lv_obj_create(page);
    lv_obj_remove_style_all(ui.nav_home);
    lv_obj_set_size(ui.nav_home, px(46), px(46));
    lv_obj_set_pos(ui.nav_home, px(334), px(300));
    lv_obj_set_style_radius(ui.nav_home, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(ui.nav_home, kBlack, 0);
    lv_obj_set_style_bg_opa(ui.nav_home, LV_OPA_70, 0);
    lv_obj_set_style_border_width(ui.nav_home, px(1), 0);
    lv_obj_set_style_border_color(ui.nav_home, kGraphite, 0);
    lv_obj_set_style_border_opa(ui.nav_home, LV_OPA_COVER, 0);
    lv_obj_add_flag(ui.nav_home, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_set_name_static(ui.nav_home, "moto-home");
    lv_obj_add_event_cb(ui.nav_home, home_shortcut_event, LV_EVENT_CLICKED,
                        nullptr);
    lv_obj_t *home_glyph =
        // The home glyph lives in LVGL's symbol font, not the display face.
        make_label(ui.nav_home, &lv_font_montserrat_20, ui.palette.ink,
                   LV_SYMBOL_HOME);
    lv_obj_center(home_glyph);

    // Connection lifecycle overlay. The composition follows the same circular
    // safe area as the navigation instrument and intentionally uses one hero
    // symbol instead of a dashboard of technical status labels.
    ui.nav_lifecycle = make_layer(page);
    lv_obj_set_style_radius(ui.nav_lifecycle, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_clip_corner(ui.nav_lifecycle, true, 0);
    lv_obj_set_style_bg_color(ui.nav_lifecycle, ui.palette.ground, 0);
    lv_obj_set_style_bg_opa(ui.nav_lifecycle, LV_OPA_COVER, 0);
    lv_obj_add_flag(ui.nav_lifecycle, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_add_flag(ui.nav_lifecycle, LV_OBJ_FLAG_GESTURE_BUBBLE);

    // The link screen is a cover, not a dialog, and the glass is round: every
    // element has to live inside the inscribed circle. That rules out the
    // full-bleed poster furniture (footer bar, corner credits) and pushes the
    // composition the other way - one oversized headline in the widest band of
    // the circle, an accent slash cut by the bezel, and a single status chip.
    lv_obj_t *slash = lv_obj_create(ui.nav_lifecycle);
    lv_obj_remove_style_all(slash);
    lv_obj_clear_flag(slash, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(slash, px(360), px(30));
    lv_obj_set_pos(slash, px(-10), px(246));
    lv_obj_set_style_bg_color(slash, ui.palette.accent, 0);
    lv_obj_set_style_bg_opa(slash, LV_OPA_COVER, 0);
    lv_obj_set_style_transform_rotation(slash, -50, 0);
    ui.lifecycle_slash = slash;

    // Display type: 1.6x the largest firmware font, tight leading, tracking
    // near zero. Two lines is the whole composition.
    ui.nav_lifecycle_title = make_text(
        ui.nav_lifecycle, &moto_font_display_72, TextRole::Ink,
        "CONNECT\nPHONE");
    lv_obj_set_width(ui.nav_lifecycle_title, px(300));
    lv_obj_set_style_text_align(ui.nav_lifecycle_title,
                                LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_letter_space(ui.nav_lifecycle_title, 0, 0);
    lv_obj_set_style_text_line_space(ui.nav_lifecycle_title, px(-14), 0);
    lv_obj_align(ui.nav_lifecycle_title, LV_ALIGN_TOP_MID, 0, px(104));

    ui.nav_lifecycle_subtitle = make_text(
        ui.nav_lifecycle, &moto_font_text_24, TextRole::Muted, "请打开手机应用");
    lv_obj_set_width(ui.nav_lifecycle_subtitle, px(240));
    lv_obj_set_style_text_align(ui.nav_lifecycle_subtitle,
                                LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(ui.nav_lifecycle_subtitle, LV_ALIGN_TOP_MID, 0, px(268));

    // Status chip: the one place the state is spelled out, inverse on ink.
    lv_obj_t *chip = lv_obj_create(ui.nav_lifecycle);
    lv_obj_remove_style_all(chip);
    lv_obj_clear_flag(chip, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(chip, px(150), px(30));
    lv_obj_align(chip, LV_ALIGN_TOP_MID, 0, px(292));
    lv_obj_set_style_bg_color(chip, ui.palette.slab, 0);
    lv_obj_set_style_bg_opa(chip, LV_OPA_COVER, 0);
    ui.lifecycle_rules[1] = chip;

    ui.nav_lifecycle_kicker = make_text(
        ui.nav_lifecycle, &moto_font_display_18, TextRole::OnSlab, "OFFLINE");
    lv_obj_set_style_text_letter_space(ui.nav_lifecycle_kicker, px(5), 0);
    lv_obj_center(ui.nav_lifecycle_kicker);
    lv_obj_set_parent(ui.nav_lifecycle_kicker, chip);

    ui.nav_lifecycle_timer = lv_timer_create(lifecycle_tick, 40, nullptr);
    ui.nav_success_timer = lv_timer_create(connection_success_timeout,
                                            kConnectionSuccessHoldMs,
                                            nullptr);
    lv_timer_pause(ui.nav_success_timer);
    apply_lifecycle_content(LifecycleVisual::PhoneOffline);
    ui.lifecycle_target = LifecycleVisual::PhoneOffline;
}

void draw_speed_gauge(lv_event_t *event) {
    const int speed = std::min<int>(160, ui.speed_drawn);
    if(speed <= 0) return;
    lv_layer_t *layer = lv_event_get_layer(event);
    const double sweep = 360.0 * speed / 160.0;
    lv_draw_arc_dsc_t dsc;
    lv_draw_arc_dsc_init(&dsc);
    dsc.center = {MOTO_UI_CANVAS_WIDTH / 2, MOTO_UI_CANVAS_HEIGHT / 2};
    dsc.radius = kDialBandOuter;
    dsc.width = kDialBandWidth;
    dsc.rounded = 0;
    dsc.color = ui.palette.map_ink;
    dsc.opa = LV_OPA_COVER;
    // The band itself is the speedometer: magenta is the unlit part, cream is
    // the speed. One gauge, no second ring, no ticks to read.
    double cursor = 270.0;
    double left = sweep;
    while(left > 0.001) {
        const double step = std::min(90.0, left);
        dsc.start_angle = static_cast<lv_value_precise_t>(cursor);
        dsc.end_angle = static_cast<lv_value_precise_t>(cursor + step);
        lv_draw_arc(layer, &dsc);
        cursor += step;
        if(cursor >= 360.0) cursor -= 360.0;
        left -= step;
    }
}

void create_speed_page() {
    lv_obj_t *page = ui.pages[MOTO_UI_PAGE_SPEED];
    make_page_surface(page, MOTO_UI_PAGE_SPEED);

    ui.speed_gauge = lv_obj_create(page);
    lv_obj_remove_style_all(ui.speed_gauge);
    lv_obj_clear_flag(ui.speed_gauge, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(ui.speed_gauge, MOTO_UI_CANVAS_WIDTH,
                    MOTO_UI_CANVAS_HEIGHT);
    lv_obj_add_event_cb(ui.speed_gauge, draw_speed_gauge, LV_EVENT_DRAW_MAIN,
                        nullptr);

    ui.speed_value = make_text(page, &moto_font_hero_176, TextRole::Display,
                               "0");
    lv_obj_align(ui.speed_value, LV_ALIGN_CENTER, 0, -px(34));

    lv_obj_t *unit = make_slab(page, "KM/H", 108, 32);
    lv_obj_align(unit, LV_ALIGN_CENTER, 0, px(66));

    // Speed-limit sign, the one prop a rider already knows how to read.
    ui.speed_limit = make_speed_limit_sign(page, 52);
    lv_obj_align(ui.speed_limit, LV_ALIGN_CENTER, px(96), px(74));
    ui.speed_limit_value = lv_obj_get_child(ui.speed_limit, 0);
}

void draw_compass_marks(lv_event_t *event) {
    lv_layer_t *layer = lv_event_get_layer(event);
    const double cx = MOTO_UI_CANVAS_WIDTH / 2.0;
    const double cy = MOTO_UI_CANVAS_HEIGHT / 2.0;
    const double heading = static_cast<double>(ui.compass_drawn_heading) *
                           kPi / 180.0;
    // The rose turns under a fixed index. 24 marks read as a bearing scale on
    // a 466 px disc; 360 would turn into a grey ring.
    for(int i = 0; i < 24; ++i) {
        const double rad = i * 15.0 * kPi / 180.0 - heading;
        const double sx = std::sin(rad);
        const double sy = -std::cos(rad);
        const bool major = i % 3 == 0;
        lv_point_precise_t pts[2] = {
            pt(cx + sx * 212.0, cy + sy * 212.0),
            pt(cx + sx * (major ? 223.0 : 218.0),
               cy + sy * (major ? 223.0 : 218.0)),
        };
        draw_segment(layer, ui.palette.map_ink, major ? 3 : 2, LV_OPA_COVER,
                     pts, 2);
    }
    // Fixed index at twelve o'clock: a cream wedge with an ink casing.
    lv_draw_triangle_dsc_t tri;
    lv_draw_triangle_dsc_init(&tri);
    tri.opa = LV_OPA_COVER;
    tri.color = kOutlineBlack;
    tri.p[0] = pt(cx - 9.0, cy - 223.0);
    tri.p[1] = pt(cx + 9.0, cy - 223.0);
    tri.p[2] = pt(cx, cy - 198.0);
    lv_draw_triangle(layer, &tri);
    tri.color = ui.palette.ink;
    tri.p[0] = pt(cx - 5.0, cy - 220.0);
    tri.p[1] = pt(cx + 5.0, cy - 220.0);
    tri.p[2] = pt(cx, cy - 207.0);
    lv_draw_triangle(layer, &tri);
}

void create_compass_page() {
    lv_obj_t *page = ui.pages[MOTO_UI_PAGE_COMPASS];
    make_page_surface(page, MOTO_UI_PAGE_COMPASS);

    ui.compass_marks = lv_obj_create(page);
    lv_obj_remove_style_all(ui.compass_marks);
    lv_obj_clear_flag(ui.compass_marks, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(ui.compass_marks, MOTO_UI_CANVAS_WIDTH,
                    MOTO_UI_CANVAS_HEIGHT);
    lv_obj_add_event_cb(ui.compass_marks, draw_compass_marks,
                        LV_EVENT_DRAW_MAIN, nullptr);

    ui.compass_cardinal = make_text(page, &moto_font_hero_176,
                                    TextRole::Display, "N");
    lv_obj_align(ui.compass_cardinal, LV_ALIGN_CENTER, 0, -px(22));

    ui.compass_heading = make_text(page, &moto_font_display_40,
                                   TextRole::Accent, "0°");
    lv_obj_set_style_text_letter_space(ui.compass_heading, px(2), 0);
    lv_obj_align(ui.compass_heading, LV_ALIGN_CENTER, 0, px(64));

    lv_obj_t *speed = make_slab(page, "0 KM/H", 150, 30);
    lv_obj_align(speed, LV_ALIGN_CENTER, 0, px(122));
    ui.compass_speed = lv_obj_get_child(speed, 0);
}

void create_performance_page() {
    lv_obj_t *page = ui.pages[MOTO_UI_PAGE_PERFORMANCE];
    make_page_surface(page, MOTO_UI_PAGE_PERFORMANCE);

    // State first: a drag strip answers "is the clock running" before it
    // answers "how fast".
    ui.perf_chip = make_plate(page, "ARMED", 110, 28, &ui.perf_chip_text);
    lv_obj_align(ui.perf_chip, LV_ALIGN_TOP_MID, 0, px(58));

    ui.perf_target = make_text(page, &moto_font_display_24, TextRole::Muted,
                                "0-100 KM/H");
    lv_obj_set_style_text_letter_space(ui.perf_target, px(3), 0);
    lv_obj_align(ui.perf_target, LV_ALIGN_TOP_MID, 0, px(92));

    // The stopwatch is the page. Nothing else on this screen is allowed to be
    // within one type size of it.
    ui.perf_timer = make_text(page, &moto_font_hero_176, TextRole::Display,
                              "-.-");
    lv_obj_align(ui.perf_timer, LV_ALIGN_CENTER, 0, -px(14));

    ui.perf_strip = lv_obj_create(page);
    lv_obj_remove_style_all(ui.perf_strip);
    lv_obj_clear_flag(ui.perf_strip, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(ui.perf_strip, px(284), px(16));
    lv_obj_align(ui.perf_strip, LV_ALIGN_CENTER, 0, px(72));
    for(int i = 0; i < kPerfSegmentCount; ++i) {
        lv_obj_t *segment = lv_obj_create(ui.perf_strip);
        lv_obj_remove_style_all(segment);
        lv_obj_clear_flag(segment, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_size(segment, px(20), px(16));
        lv_obj_set_pos(segment, px(i * 24), 0);
        lv_obj_set_style_bg_color(segment, ui.palette.ink, 0);
        lv_obj_set_style_bg_opa(segment, LV_OPA_20, 0);
        ui.perf_segments[i] = segment;
    }

    // BEST and the delta are one row of secondary values. The delta label was
    // referenced by the state machine since the page was introduced but never
    // created, so PB/delta feedback never rendered (v2 fixes the dead code).
    lv_obj_t *best = make_slab(page, "BEST --.-", 132, 30, &ui.perf_best);
    lv_obj_align(best, LV_ALIGN_CENTER, -52, 112);
    ui.perf_delta = make_text(page, &moto_font_display_40, TextRole::Ink, "");
    lv_obj_align(ui.perf_delta, LV_ALIGN_CENTER, 92, 112);

    // The stopwatch runs on its own timer: state snapshots arrive at the BLE
    // cadence, but the clock has to move between them.
    ui.perf_timer_task = lv_timer_create(perf_tick, 50, nullptr);
}

void draw_lean_marks(lv_event_t *event) {
    lv_layer_t *layer = lv_event_get_layer(event);
    const double cx = MOTO_UI_CANVAS_WIDTH / 2.0;
    const double cy = MOTO_UI_CANVAS_HEIGHT / 2.0;
    // A protractor printed on the band: every 15 degrees, long marks at the
    // riding-relevant stops. The scale is fixed; the horizon is what moves.
    for(int t = -60; t <= 60; t += 15) {
        const double rad = t * kPi / 180.0;
        const double sx = std::sin(rad);
        const double sy = -std::cos(rad);
        const bool major = t % 45 == 0;
        lv_point_precise_t pts[2] = {
            pt(cx + sx * 212.0, cy + sy * 212.0),
            pt(cx + sx * (major ? 223.0 : 218.0),
               cy + sy * (major ? 223.0 : 218.0)),
        };
        draw_segment(layer, ui.palette.map_ink, major ? 3 : 2, LV_OPA_COVER,
                     pts, 2);
    }
    // The zero reference: the only mark that is a bar rather than a tick.
    lv_point_precise_t zero[2] = {pt(cx, cy - 223.0), pt(cx, cy - 198.0)};
    draw_segment(layer, ui.palette.map_ink, 5, LV_OPA_COVER, zero, 2);
}

// The lean page is an artificial horizon, not a bar graph. The bike is the
// frame: the disc stays still and the world tilts, which is exactly what the
// rider sees over the bars. The sun gives the horizon a second reference so a
// ten-degree lean is readable without reading the number.
void draw_lean_scene(lv_event_t *event) {
    lv_layer_t *layer = lv_event_get_layer(event);
    // No attitude yet is still a valid composition: draw the horizon level and
    // let the number read "--". Hiding the whole scene was what made the page
    // look like a different product whenever the IMU had not produced a sample
    // (or the board was on the bench).
    const double lean = ui.lean_has ? static_cast<double>(ui.lean_deg) : 0.0;
    // Right lean tilts the world left: the horizon's right end rises.
    const double a = -lean * kPi / 180.0;
    const double cx = MOTO_UI_CANVAS_WIDTH / 2.0;
    const double cy = MOTO_UI_CANVAS_HEIGHT / 2.0;
    const double ux = std::cos(a);
    const double uy = std::sin(a);
    const double nx = -std::sin(a);
    const double ny = std::cos(a);
    const double horizon = 74.0;
    const auto at = [&](double along, double down, double &x, double &y) {
        x = cx + ux * along + nx * down;
        y = cy + uy * along + ny * down;
    };

    // The sun is a half disc whose flat edge is the horizon. It orbits the
    // centre with the world instead of spinning in place.
    double sx, sy;
    at(-122.0, horizon, sx, sy);
    lv_draw_arc_dsc_t arc;
    lv_draw_arc_dsc_init(&arc);
    arc.center = {static_cast<int32_t>(std::lround(sx)),
                  static_cast<int32_t>(std::lround(sy))};
    arc.rounded = 0;
    arc.start_angle = static_cast<lv_value_precise_t>(a * 180.0 / kPi + 180.0);
    arc.end_angle = static_cast<lv_value_precise_t>(a * 180.0 / kPi + 360.0);
    arc.width = 60;
    arc.radius = 30;
    arc.color = ui.palette.outline;
    arc.opa = LV_OPA_COVER;
    lv_draw_arc(layer, &arc);
    arc.width = 54;
    arc.radius = 27;
    arc.color = ui.palette.coral;
    lv_draw_arc(layer, &arc);

    // One datum line, cased so it survives crossing the numeral.
    double hx1, hy1, hx2, hy2;
    at(-176.0, horizon, hx1, hy1);
    at(176.0, horizon, hx2, hy2);
    lv_point_precise_t line[2] = {pt(hx1, hy1), pt(hx2, hy2)};
    draw_segment(layer, ui.palette.outline, 11, LV_OPA_COVER, line, 2);
    draw_segment(layer, ui.palette.ink, 5, LV_OPA_COVER, line, 2);
}

void create_lean_page() {
    lv_obj_t *page = ui.pages[MOTO_UI_PAGE_LEAN];
    make_page_surface(page, MOTO_UI_PAGE_LEAN);

    ui.lean_marks = lv_obj_create(page);
    lv_obj_remove_style_all(ui.lean_marks);
    lv_obj_clear_flag(ui.lean_marks, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(ui.lean_marks, MOTO_UI_CANVAS_WIDTH, MOTO_UI_CANVAS_HEIGHT);
    lv_obj_add_event_cb(ui.lean_marks, draw_lean_marks, LV_EVENT_DRAW_MAIN,
                        nullptr);

    ui.lean_scene = lv_obj_create(page);
    lv_obj_remove_style_all(ui.lean_scene);
    lv_obj_clear_flag(ui.lean_scene, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(ui.lean_scene, MOTO_UI_CANVAS_WIDTH, MOTO_UI_CANVAS_HEIGHT);
    lv_obj_add_event_cb(ui.lean_scene, draw_lean_scene, LV_EVENT_DRAW_MAIN,
                        nullptr);

    // The rider point. It rides the rim at the current angle, so the value has
    // a place on the dial as well as a number.
    ui.lean_blip = lv_obj_create(page);
    lv_obj_remove_style_all(ui.lean_blip);
    lv_obj_clear_flag(ui.lean_blip, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(ui.lean_blip, 14, 14);
    lv_obj_set_style_radius(ui.lean_blip, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(ui.lean_blip, ui.palette.accent, 0);
    lv_obj_set_style_bg_opa(ui.lean_blip, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(ui.lean_blip, kOutlineBlack, 0);
    lv_obj_set_style_border_width(ui.lean_blip, 3, 0);
    lv_obj_add_flag(ui.lean_blip, LV_OBJ_FLAG_HIDDEN);

    ui.lean_value = make_text(page, &moto_font_hero_176, TextRole::Display,
                              "");
    lv_obj_align(ui.lean_value, LV_ALIGN_CENTER, 0, 0);

    ui.lean_max_l_plate =
        make_plate(page, "L --", 90, 26, &ui.lean_max_l);
    lv_obj_set_pos(ui.lean_max_l_plate, px(76), px(72));
    ui.lean_max_r_plate =
        make_plate(page, "R --", 90, 26, &ui.lean_max_r);
    lv_obj_set_pos(ui.lean_max_r_plate, px(188), px(72));

    ui.lean_state = make_plate(page, "NO IMU", 120, 30, &ui.lean_state_text);
    lv_obj_align(ui.lean_state, LV_ALIGN_CENTER, 0, px(78));
    lv_obj_add_flag(ui.lean_state, LV_OBJ_FLAG_HIDDEN);
}

void create_music_page() {
    lv_obj_t *page = ui.pages[MOTO_UI_PAGE_MUSIC];
    make_page_surface(page, MOTO_UI_PAGE_MUSIC);

    // A car radio faceplate, not a media app: source on a plate, the track
    // printed like a station ident, and three physical keys.
    ui.music_source_plate =
        make_plate(page, "APPLE MUSIC", 126, 26, &ui.music_source);
    lv_obj_set_pos(ui.music_source_plate, px(78), px(68));

    ui.music_title = make_text(page, &moto_font_display_40_cjk,
                               TextRole::Display, "NIGHT RIDE");
    lv_obj_set_width(ui.music_title, px(288));
    lv_obj_set_style_text_align(ui.music_title, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(ui.music_title, LV_LABEL_LONG_WRAP);
    lv_obj_align(ui.music_title, LV_ALIGN_TOP_MID, 0, px(110));

    ui.music_artist = make_text(page, &moto_font_display_24_cjk,
                                TextRole::Muted, "PHONE NOW PLAYING");
    lv_obj_set_width(ui.music_artist, px(288));
    lv_obj_set_style_text_align(ui.music_artist, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(ui.music_artist, LV_LABEL_LONG_DOT);
    lv_obj_align(ui.music_artist, LV_ALIGN_TOP_MID, 0, px(172));

    // The deck: one ink block that carries all three transport keys. It is the
    // only furniture on the page, so the eyes never hunt for a button.
    lv_obj_t *deck = lv_obj_create(page);
    lv_obj_remove_style_all(deck);
    lv_obj_clear_flag(deck, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(deck, px(260), px(64));
    lv_obj_align(deck, LV_ALIGN_CENTER, 0, px(52));
    lv_obj_set_style_bg_color(deck, ui.palette.slab, 0);
    lv_obj_set_style_bg_opa(deck, LV_OPA_COVER, 0);
    register_fill(deck, Ui::FillRole::Slab);

    static const int sizes[3] = {52, 62, 52};
    static const int offsets[3] = {-82, 0, 82};
    static const char *labels[3] = {"<<", ">", ">>"};
    for(int i = 0; i < 3; ++i) {
        const bool primary = i == 1;
        ui.music_buttons[i] = lv_button_create(deck);
        lv_obj_set_size(ui.music_buttons[i], px(sizes[i]), px(sizes[i]));
        lv_obj_align(ui.music_buttons[i], LV_ALIGN_CENTER, px(offsets[i]), 0);
        lv_obj_set_style_radius(ui.music_buttons[i], 0, 0);
        lv_obj_set_style_bg_color(
            ui.music_buttons[i], primary ? ui.palette.accent : ui.palette.slab,
            0);
        lv_obj_set_style_bg_opa(ui.music_buttons[i], LV_OPA_COVER, 0);
        lv_obj_set_style_shadow_width(ui.music_buttons[i], 0, 0);
        lv_obj_set_style_border_color(ui.music_buttons[i], ui.palette.map_ink,
                                      0);
        lv_obj_set_style_border_width(ui.music_buttons[i],
                                      primary ? 0 : px(2), 0);
        // Pin the pressed state: LVGL's default recolour + grow painted as a
        // hard block over the round key on the ESP32 PPA path.
        lv_obj_set_style_bg_color(
            ui.music_buttons[i], primary ? ui.palette.accent : ui.palette.slab,
            LV_STATE_PRESSED);
        lv_obj_set_style_bg_opa(ui.music_buttons[i], LV_OPA_COVER,
                                LV_STATE_PRESSED);
        lv_obj_set_style_radius(ui.music_buttons[i], 0, LV_STATE_PRESSED);
        lv_obj_set_style_shadow_width(ui.music_buttons[i], 0, LV_STATE_PRESSED);
        lv_obj_set_style_border_width(ui.music_buttons[i],
                                      primary ? 0 : px(2), LV_STATE_PRESSED);
        lv_obj_set_style_recolor_opa(ui.music_buttons[i], LV_OPA_TRANSP,
                                     LV_STATE_PRESSED);
        lv_obj_set_style_transform_width(ui.music_buttons[i], 0,
                                         LV_STATE_PRESSED);
        lv_obj_set_style_transform_height(ui.music_buttons[i], 0,
                                          LV_STATE_PRESSED);
        lv_obj_add_flag(ui.music_buttons[i], LV_OBJ_FLAG_GESTURE_BUBBLE);
        lv_obj_add_event_cb(
            ui.music_buttons[i], music_button_event, LV_EVENT_CLICKED,
            reinterpret_cast<void *>(static_cast<std::intptr_t>(i)));
        ui.music_button_labels[i] = make_label(
            ui.music_buttons[i], &moto_font_display_40,
            primary ? ui.palette.on_accent : ui.palette.on_slab, labels[i]);
        lv_obj_center(ui.music_button_labels[i]);
    }

    // Like is a mode, not a transport key: it lives on the plate row.
    lv_obj_t *like = lv_button_create(page);
    ui.music_buttons[3] = like;
    lv_obj_set_size(like, px(70), px(26));
    lv_obj_set_pos(like, px(212), px(68));
    lv_obj_set_style_radius(like, 0, 0);
    lv_obj_set_style_bg_color(like, ui.palette.accent, 0);
    lv_obj_set_style_bg_opa(like, LV_OPA_COVER, 0);
    lv_obj_set_style_shadow_width(like, 0, 0);
    lv_obj_set_style_border_width(like, 0, 0);
    lv_obj_add_flag(like, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_event_cb(
        like, music_button_event, LV_EVENT_CLICKED,
        reinterpret_cast<void *>(static_cast<std::intptr_t>(3)));
    ui.music_button_labels[3] = make_label(like, &moto_font_display_24,
                                           ui.palette.on_accent, "LIKE");
    lv_obj_center(ui.music_button_labels[3]);
}

void set_boot_content_opacity(void *object, int32_t opacity) {
    lv_obj_set_style_opa(static_cast<lv_obj_t *>(object), opacity, 0);
}

}  // namespace

extern "C" void moto_nav_ui_set_boot_theme(moto_ui_theme_t theme) {
    g_boot_theme = theme;
}

extern "C" void moto_nav_ui_show_boot_screen(void) {
    /* Also used while the full UI is live, so it must leave nothing behind
       that still points at the previous tree. */
    teardown_ui();
    if(ui.screen == nullptr) return;
    lv_obj_remove_flag(ui.screen, LV_OBJ_FLAG_SCROLLABLE);
    // The boot frame belongs to the same paper system as the instrument. It
    // used to take kGtaDayPalette.background, which is MAP GREEN; the boot was
    // the one screen still showing the old map field instead of the poster.
    const MapPalette &palette = palette_for_theme(g_boot_theme, false);
    const lv_color_t poster = palette.ground;
    const lv_color_t ink = palette.ink;
    const lv_color_t accent = palette.accent;
    lv_obj_set_style_bg_color(ui.screen, poster, 0);
    lv_obj_set_style_bg_opa(ui.screen, LV_OPA_COVER, 0);

    lv_obj_t *content = make_layer(ui.screen);
    lv_obj_set_style_opa(content, LV_OPA_TRANSP, 0);

    // The slash goes down first so the wordmark sits on top of it. Rotating a
    // plain rectangle is cheaper than a canvas and needs no asset.
    lv_obj_t *slash = lv_obj_create(content);
    lv_obj_remove_style_all(slash);
    lv_obj_set_size(slash, px(420), px(20));
    lv_obj_set_pos(slash, px(-31), px(246));
    lv_obj_set_style_bg_color(slash, accent, 0);
    lv_obj_set_style_bg_opa(slash, LV_OPA_COVER, 0);
    lv_obj_set_style_transform_rotation(slash, -120, 0);

    // Poster rules: heavy ink bars above and below the wordmark, the grammar
    // the GTA cover art and loading cards use.
    for(int i = 0; i < 2; ++i) {
        lv_obj_t *rule = lv_obj_create(content);
        lv_obj_remove_style_all(rule);
        lv_obj_set_size(rule, px(360), px(6));
        lv_obj_set_pos(rule, 0, px(i == 0 ? 96 : 352));
        lv_obj_set_style_bg_color(rule, kViceMagenta, 0);
        lv_obj_set_style_bg_opa(rule, LV_OPA_COVER, 0);
    }

    lv_obj_t *moto = make_label(content, &moto_font_wordmark_132, ink, "MOTO");
    lv_obj_set_style_text_outline_stroke_color(moto, kOutlineBlack, 0);
    lv_obj_set_style_text_outline_stroke_width(moto, px(3), 0);
    lv_obj_set_style_text_outline_stroke_opa(moto, LV_OPA_COVER, 0);
    lv_obj_set_style_text_letter_space(moto, px(2), 0);
    lv_obj_align(moto, LV_ALIGN_CENTER, 0, px(-56));

    lv_obj_t *gps = make_label(content, &moto_font_wordmark_132, ink, "GPS");
    lv_obj_set_style_text_outline_stroke_color(gps, kOutlineBlack, 0);
    lv_obj_set_style_text_outline_stroke_width(gps, px(3), 0);
    lv_obj_set_style_text_outline_stroke_opa(gps, LV_OPA_COVER, 0);
    lv_obj_set_style_text_letter_space(gps, px(6), 0);
    lv_obj_align(gps, LV_ALIGN_CENTER, 0, px(56));

    lv_obj_t *credit = make_label(content, &moto_font_display_18, ink,
                                  "NAVIGATION");
    lv_obj_set_style_text_letter_space(credit, px(6), 0);
    lv_obj_align(credit, LV_ALIGN_CENTER, 0, px(150));

    lv_anim_t fade;
    lv_anim_init(&fade);
    lv_anim_set_var(&fade, content);
    lv_anim_set_exec_cb(&fade, set_boot_content_opacity);
    lv_anim_set_values(&fade, LV_OPA_TRANSP, LV_OPA_COVER);
    lv_anim_set_delay(&fade, 30);
    lv_anim_set_duration(&fade, 230);
    lv_anim_set_reverse_delay(&fade, 650);
    lv_anim_set_reverse_duration(&fade, 260);
    lv_anim_set_path_cb(&fade, lv_anim_path_ease_in_out);
    lv_anim_start(&fade);
}

extern "C" void moto_nav_ui_show_power_off_screen(void) {
    teardown_ui();
    if(ui.screen == nullptr) return;
    lv_obj_set_style_bg_color(ui.screen, LV_COLOR_MAKE(0x00, 0x00, 0x00), 0);
    lv_obj_set_style_bg_opa(ui.screen, LV_OPA_COVER, 0);

    lv_obj_t *brand = make_label(ui.screen, &moto_font_display_40,
                                 LV_COLOR_MAKE(0xFF, 0xFF, 0xFF), "MOTO");
    lv_obj_set_style_text_letter_space(brand, px(4), 0);
    lv_obj_set_style_text_outline_stroke_color(
        brand, LV_COLOR_MAKE(0xFF, 0xFF, 0xFF), 0);
    lv_obj_set_style_text_outline_stroke_width(brand, px(2), 0);
    lv_obj_set_style_text_outline_stroke_opa(brand, LV_OPA_COVER, 0);
    lv_obj_align(brand, LV_ALIGN_CENTER, 0, px(-18));

    lv_obj_t *status = make_label(ui.screen, &moto_font_display_18,
                                  LV_COLOR_MAKE(0xFF, 0xFF, 0xFF),
                                  "POWER OFF");
    lv_obj_set_style_text_letter_space(status, px(4), 0);
    lv_obj_align(status, LV_ALIGN_CENTER, 0, px(35));
}

void install_font_fallbacks() {
    // Track titles arrive from the phone in whatever script the rider's
    // library uses. The display face is Latin-only, so a Chinese title used to
    // render as four empty boxes; the 16 px CJK face keeps it readable.
    if(moto_font_display_40_cjk.line_height == 0) {
        moto_font_display_40_cjk = moto_font_display_40;
        moto_font_display_40_cjk.fallback = &moto_font_text_40;
        moto_font_display_24_cjk = moto_font_display_24;
        moto_font_display_24_cjk.fallback = &moto_font_text_24;
    }
}

extern "C" void moto_nav_ui_create(void) {
    // Normally runs right after the boot screen, but a second create() while
    // the UI is live must retire the old timers before the tree is replaced.
    teardown_ui();
    if(ui.screen == nullptr) return;
    install_font_fallbacks();
    lv_obj_remove_flag(ui.screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(ui.screen, ui.palette.frame, 0);
    lv_obj_set_style_bg_opa(ui.screen, LV_OPA_COVER, 0);
    // The active screen survives lv_obj_clean(), so a second create() would
    // otherwise stack a duplicate gesture and wake callback on it.
    lv_obj_remove_event_cb(ui.screen, gesture_event);
    lv_obj_remove_event_cb(ui.screen, interaction_event);
    lv_obj_add_event_cb(ui.screen, gesture_event, LV_EVENT_GESTURE, nullptr);
    for(int i = 0; i < MOTO_UI_PAGE_COUNT; ++i) {
        ui.pages[i] = make_layer(ui.screen);
        lv_obj_add_flag(ui.pages[i], LV_OBJ_FLAG_GESTURE_BUBBLE);
    }
    create_navigation_page();
    create_performance_page();
    create_lean_page();
    create_speed_page();
    create_compass_page();
    create_music_page();
    create_page_dots();
    // Press events are delivered to the topmost object under the finger, not
    // necessarily to its page. Register once across the finished tree so any
    // touch reliably wakes the dots, including the map and music controls.
    install_interaction_wake(ui.screen);
    // The tree is complete. This must be set before the two public entry
    // points below, because they are gated on it like every other mutator.
    ui.live = true;

    moto_music_state_t initial_music = {
        "APPLE MUSIC", "NIGHT RIDE", "PHONE NOW PLAYING", 1, 1, 0, 0,
    };
    moto_nav_ui_set_music_state(&initial_music);

    moto_ui_state_t initial{};
    initial.mode = MOTO_UI_ACQUIRING_FIX;
    initial.page = MOTO_UI_PAGE_NAVIGATION;
    initial.maneuver = MOTO_MANEUVER_STRAIGHT;
    initial.traffic = MOTO_TRAFFIC_UNKNOWN;
    moto_nav_ui_set_state(&initial);
    show_page(MOTO_UI_PAGE_NAVIGATION, true);
    // The default phone state is offline, but the lifecycle cover starts
    // Hidden. Without this the navigation page is a bare paper disc: no map
    // (nothing has been sent yet) and no "CONNECT PHONE" cover either, which
    // reads as a blank screen that has crashed.
    refresh_lifecycle();
}

void apply_theme_colors() {
    for(int i = 0; i < MOTO_UI_PAGE_COUNT; ++i) {
        if(ui.pages[i] != nullptr) {
            lv_obj_set_style_bg_color(ui.pages[i], ui.palette.frame, 0);
        }
        if(ui.page_discs[i] != nullptr) {
            lv_obj_set_style_bg_color(ui.page_discs[i], ui.palette.ground, 0);
        }
        if(ui.page_frames[i] != nullptr) {
            lv_obj_invalidate(ui.page_frames[i]);
        }
    }
    if(ui.screen != nullptr) {
        lv_obj_set_style_bg_color(ui.screen, ui.palette.frame, 0);
    }
    if(ui.nav_map_background != nullptr) {
        lv_obj_set_style_bg_color(ui.nav_map_background,
                                  ui.palette.background, 0);
    }
    if(ui.nav_lifecycle != nullptr) {
        lv_obj_set_style_bg_color(ui.nav_lifecycle, ui.palette.ground, 0);
    }
    if(ui.lifecycle_rules[1] != nullptr) {
        lv_obj_set_style_bg_color(ui.lifecycle_rules[1], ui.palette.slab, 0);
    }
    if(ui.lifecycle_slash != nullptr) {
        lv_obj_set_style_bg_color(ui.lifecycle_slash, ui.palette.accent, 0);
    }

    for(std::uint8_t i = 0; i < ui.nav_building_footprint_count; ++i) {
        const bool landmark = ui.nav_building_spans[i].building_class == 1U;
        lv_obj_set_style_line_color(
            ui.nav_buildings[i],
            landmark ? ui.palette.building_landmark : ui.palette.building, 0);
    }
    for(std::uint8_t i = 0; i < ui.nav_road_polyline_count; ++i) {
        const std::uint8_t road_class = ui.nav_road_spans[i].road_class;
        const bool major = road_class <= 2U;
        const bool service = road_class >= 4U;
        lv_obj_set_style_line_color(
            ui.nav_roads[i],
            major ? ui.palette.road_major
                  : (service ? ui.palette.road_minor : ui.palette.road), 0);
    }
    if(ui.nav_route != nullptr) {
        lv_obj_set_style_line_color(ui.nav_route, ui.palette.route, 0);
    }
    if(ui.nav_route_shadow != nullptr) {
        lv_obj_set_style_line_color(ui.nav_route_shadow,
                                    ui.palette.route_shadow, 0);
    }
    apply_text_colors();
    if(ui.nav_map != nullptr) {
        lv_obj_invalidate(ui.nav_map);
    }
    if(ui.nav_lifecycle != nullptr) {
        lv_obj_invalidate(ui.nav_lifecycle);
    }
}

extern "C" void moto_nav_ui_set_theme(moto_ui_theme_t theme) {
    if(!ui.live) return;
    ui.theme = theme;
    // AUTO resolves through the phone's flag in one place; explicit themes
    // ignore the flag.
    ui.palette = palette_for_theme(theme, ui.ambient_night != 0);
    if(ui.nav_map_background != nullptr) apply_theme_colors();
}

extern "C" void moto_nav_ui_set_show_buildings(uint8_t visible) {
    if(!ui.live) return;
    const bool show = visible != 0;
    // The phone re-sends settings on every session and after every echo;
    // a repeat must not invalidate the map layer.
    if(ui.show_buildings == show) return;
    ui.show_buildings = show;
    apply_building_visibility();
}

extern "C" void moto_nav_ui_set_ambient_night(uint8_t is_night) {
    if(!ui.live) return;
    const bool night = is_night != 0;
    // The phone may repeat this on every session; a repeat must not repaint.
    if(ui.ambient_night == night) return;
    ui.ambient_night = night;
    if(ui.theme == MOTO_UI_THEME_AUTO) moto_nav_ui_set_theme(ui.theme);
}

void moto_nav_ui_set_state(const moto_ui_state_t *state) {
    if(state == nullptr || !ui.live) return;
    ui.nav_speed_kph = state->speed_kph;
    show_page(state->page);
    // Hidden pages do not need to be invalidated. Page changes immediately
    // apply a fresh snapshot through PhoneNavBridge, so this keeps every page
    // correct while avoiding three full page redraws per navigation update.
    switch(ui.page) {
        case MOTO_UI_PAGE_NAVIGATION: update_navigation(state); break;
        case MOTO_UI_PAGE_PERFORMANCE: update_performance(state); break;
        case MOTO_UI_PAGE_LEAN: update_lean(state); break;
        case MOTO_UI_PAGE_SPEED: update_speedometer(state); break;
        case MOTO_UI_PAGE_COMPASS: update_compass(state); break;
        case MOTO_UI_PAGE_MUSIC:
        case MOTO_UI_PAGE_COUNT: break;
    }
}

extern "C" void moto_nav_ui_show_home_error(void) {
    if(!ui.live) return;
    if(ui.nav_home_hint == nullptr || ui.nav_home_hint_timer == nullptr) {
        return;
    }
    lv_obj_remove_flag(ui.nav_home_hint, LV_OBJ_FLAG_HIDDEN);
    lv_timer_reset(ui.nav_home_hint_timer);
    lv_timer_resume(ui.nav_home_hint_timer);
}

extern "C" void moto_nav_ui_set_motion_state(const moto_ui_state_t *state) {
    if(state == nullptr || !ui.live ||
       ui.pages[MOTO_UI_PAGE_NAVIGATION] == nullptr || state->page != ui.page) {
        return;
    }
    // QMI8658 samples arrive at high frequency. Only the route polyline or
    // compass rose actually changes with yaw; labels, arcs and hidden pages
    // remain untouched so LVGL submits a small, bounded dirty region.
    if(ui.page == MOTO_UI_PAGE_NAVIGATION) {
        update_building_geometry(state);
        update_road_geometry(state);
        update_route_geometry(state);
        update_radar_ring(state);
    } else if(ui.page == MOTO_UI_PAGE_COMPASS) {
        update_compass(state);
    } else if(ui.page == MOTO_UI_PAGE_LEAN) {
        update_lean(state);
    } else if(ui.page == MOTO_UI_PAGE_PERFORMANCE) {
        update_performance(state);
    }
}

extern "C" void moto_nav_ui_set_phone_connection(
    moto_ui_phone_connection_t connection) {
    if(!ui.live || ui.nav_lifecycle == nullptr ||
       connection < MOTO_UI_PHONE_OFFLINE ||
       connection > MOTO_UI_PHONE_ONLINE) {
        return;
    }
    if(ui.phone_connection == connection) return;
    const bool became_online =
        connection == MOTO_UI_PHONE_ONLINE &&
        ui.phone_connection != MOTO_UI_PHONE_ONLINE;
    ui.phone_connection = connection;
    if(became_online) {
        ui.lifecycle_success_active = true;
        if(ui.nav_success_timer != nullptr) {
            lv_timer_set_period(ui.nav_success_timer,
                                kConnectionSuccessHoldMs);
            lv_timer_reset(ui.nav_success_timer);
            lv_timer_resume(ui.nav_success_timer);
        }
    } else if(connection != MOTO_UI_PHONE_ONLINE) {
        ui.lifecycle_success_active = false;
        if(ui.nav_success_timer != nullptr) {
            lv_timer_pause(ui.nav_success_timer);
        }
    }
    refresh_lifecycle();
}

extern "C" void moto_nav_ui_set_reduce_motion(uint8_t reduce_motion) {
    if(!ui.live) return;
    ui.reduce_motion = reduce_motion != 0;
    if(ui.nav_lifecycle_timer != nullptr) {
        if(ui.reduce_motion) {
            lv_timer_pause(ui.nav_lifecycle_timer);
        } else {
            lv_timer_resume(ui.nav_lifecycle_timer);
        }
    }
    refresh_lifecycle();
}

extern "C" void moto_nav_ui_set_page(moto_ui_page_t page) {
    if(!ui.live) return;
    show_page(page, true);
}

extern "C" moto_ui_page_t moto_nav_ui_get_page(void) {
    return ui.page;
}

extern "C" void moto_nav_ui_set_page_change_callback(
    moto_page_change_callback_t callback, void *context) {
    ui.page_callback = callback;
    ui.page_callback_context = context;
}

extern "C" void moto_nav_ui_set_music_state(const moto_music_state_t *state) {
    if(state == nullptr || !ui.live) return;
    ui.music = *state;
    copy_text(ui.music_source_text, sizeof(ui.music_source_text),
              state->source_name, "PHONE MEDIA");
    copy_text(ui.music_title_text, sizeof(ui.music_title_text),
              state->track_title, "--");
    copy_text(ui.music_artist_text, sizeof(ui.music_artist_text),
              state->artist_name, "--");
    ui.music.source_name = ui.music_source_text;
    ui.music.track_title = ui.music_title_text;
    ui.music.artist_name = ui.music_artist_text;
    update_music_view();
}

extern "C" void moto_nav_ui_set_music_command_callback(
    moto_music_command_callback_t callback, void *context) {
    ui.music_callback = callback;
    ui.music_callback_context = context;
}

extern "C" void moto_nav_ui_set_demo_active(uint8_t enabled) {
    if(!ui.live) return;
    ui.demo_active = enabled != 0;
    refresh_lifecycle();
}

extern "C" void moto_nav_ui_set_demo_change_callback(
    moto_demo_change_callback_t callback, void *context) {
    ui.demo_callback = callback;
    ui.demo_callback_context = context;
}

extern "C" void moto_nav_ui_set_home_callback(
    moto_home_shortcut_callback_t callback, void *context) {
    ui.home_callback = callback;
    ui.home_callback_context = context;
}
