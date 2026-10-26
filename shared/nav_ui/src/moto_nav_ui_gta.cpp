#include "moto_nav_ui.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <type_traits>

#include "lvgl.h"
#include "moto_nav_ui_backends.h"
#include "moto_nav_ui_memory.h"
#include "moto_nav_ui_text.h"

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
constexpr lv_color_t kGraphite = LV_COLOR_MAKE(0x30, 0x35, 0x39);
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
    // One stroke colour per MapRoadClass (motorway, primary, secondary,
    // residential, service, other). The wire carries all six; collapsing them
    // to three bands threw the hierarchy away.
    lv_color_t road_class[6];
    // Poster-scale numerals. GTA prints its HUD readouts in colour, never
    // plain white: here the neon pink the phone wears too. By day the paper
    // needs black ink.
    lv_color_t hud;
    // The Lean arc: an unlit segment, and a lit one past 45 degrees.
    lv_color_t gauge_off;
    lv_color_t gauge_hot;
};

// GTA III, Liberty City by day: the cover's white field as the paper and the
// game's grey radar as the map. Every numeral is black ink on newsprint, which
// is where daylight contrast comes from. The band is black: the rim reads as
// one heavy radar ring with the casing.
constexpr MapPalette kGtaDayPalette = {
    LV_COLOR_MAKE(0x00, 0x00, 0x00),  // bezel
    LV_COLOR_MAKE(0xEF, 0xEC, 0xE4),  // NEWSPRINT paper
    LV_COLOR_MAKE(0x4E, 0x4E, 0x4A),  // RADAR GREY map ground
    LV_COLOR_MAKE(0xEF, 0xEC, 0xE4),  // roads
    LV_COLOR_MAKE(0xF7, 0xF5, 0xEF),
    LV_COLOR_MAKE(0xB4, 0xB0, 0xA6),  // secondary roads
    LV_COLOR_MAKE(0x40, 0x40, 0x3C),  // blocks under the roads
    LV_COLOR_MAKE(0x5C, 0x5C, 0x57),
    LV_COLOR_MAKE(0xF0, 0x8A, 0x3C),  // HUD ORANGE route (health)
    LV_COLOR_MAKE(0x0A, 0x0A, 0x0A),  // casing
    LV_COLOR_MAKE(0x11, 0x11, 0x11),  // ink
    LV_COLOR_MAKE(0x4B, 0x46, 0x3E),  // muted
    LV_COLOR_MAKE(0x11, 0x11, 0x11),  // band: black gutter
    LV_COLOR_MAKE(0x35, 0x56, 0x7F),  // HUD STEEL BLUE, darkened for paper
    LV_COLOR_MAKE(0xB0, 0x47, 0x16),  // HUD orange, darkened: the sun
    LV_COLOR_MAKE(0xEF, 0xEC, 0xE4),  // type on the map is paper, always
    LV_COLOR_MAKE(0xEF, 0xEC, 0xE4),  // halo = paper, the inverse of ink
    LV_COLOR_MAKE(0xEF, 0xEC, 0xE4),  // paper on a black plate
    LV_COLOR_MAKE(0x11, 0x11, 0x11),  // counter slab
    LV_COLOR_MAKE(0xEF, 0xEC, 0xE4),
    {
        LV_COLOR_MAKE(0xF3, 0x4D, 0xAA),  // motorway: neon pink
        LV_COLOR_MAKE(0xF7, 0xF5, 0xEF),  // primary
        LV_COLOR_MAKE(0xEF, 0xEC, 0xE4),  // secondary
        LV_COLOR_MAKE(0xD6, 0xD2, 0xC8),  // residential
        LV_COLOR_MAKE(0xB4, 0xB0, 0xA6),  // service
        LV_COLOR_MAKE(0x8E, 0x8A, 0x82),  // other: almost the block
    },
    LV_COLOR_MAKE(0x11, 0x11, 0x11),  // hud numerals: ink
    LV_COLOR_MAKE(0xD6, 0xD2, 0xC8),  // gauge off: a pale print grey
    LV_COLOR_MAKE(0xC8, 0x32, 0x1E),  // gauge hot: HUD red, darkened for paper
};

// GTA III, Liberty City at night: true black, box-art white type, and the
// HUD's colours: neon pink (the phone's accent; it replaced the clock gold),
// money steel-blue, health orange. The band is white, like the cover's panel
// gutters.
constexpr MapPalette kGtaNightPalette = {
    LV_COLOR_MAKE(0x00, 0x00, 0x00),
    LV_COLOR_MAKE(0x0A, 0x0A, 0x0A),  // BLACK paper
    LV_COLOR_MAKE(0x16, 0x16, 0x16),  // RADAR map ground
    LV_COLOR_MAKE(0xD6, 0xD2, 0xC8),
    LV_COLOR_MAKE(0xF2, 0xEE, 0xE4),
    LV_COLOR_MAKE(0x8E, 0x8A, 0x82),
    LV_COLOR_MAKE(0x24, 0x24, 0x24),
    LV_COLOR_MAKE(0x2E, 0x2E, 0x2E),
    LV_COLOR_MAKE(0xF0, 0x8A, 0x3C),  // HUD ORANGE route
    LV_COLOR_MAKE(0x00, 0x00, 0x00),
    LV_COLOR_MAKE(0xF2, 0xEE, 0xE4),  // BOX-ART WHITE
    LV_COLOR_MAKE(0xA8, 0xA2, 0x94),
    LV_COLOR_MAKE(0xF2, 0xEE, 0xE4),  // band: white gutter
    LV_COLOR_MAKE(0x86, 0xA6, 0xCF),  // HUD STEEL BLUE (money)
    LV_COLOR_MAKE(0xF3, 0x4D, 0xAA),  // NEON PINK: the sun
    LV_COLOR_MAKE(0xF2, 0xEE, 0xE4),  // map type
    LV_COLOR_MAKE(0x00, 0x00, 0x00),  // halo = bezel, the inverse of ink
    LV_COLOR_MAKE(0x0A, 0x0A, 0x0A),  // black on a white plate
    LV_COLOR_MAKE(0x00, 0x00, 0x00),  // counter slab
    LV_COLOR_MAKE(0xF2, 0xEE, 0xE4),
    {
        LV_COLOR_MAKE(0xF3, 0x4D, 0xAA),  // motorway: neon pink
        LV_COLOR_MAKE(0xF2, 0xEE, 0xE4),  // primary
        LV_COLOR_MAKE(0xD6, 0xD2, 0xC8),  // secondary
        LV_COLOR_MAKE(0xB4, 0xB0, 0xA6),  // residential
        LV_COLOR_MAKE(0x8E, 0x8A, 0x82),  // service
        LV_COLOR_MAKE(0x5E, 0x5B, 0x55),  // other: into the dark
    },
    LV_COLOR_MAKE(0xF3, 0x4D, 0xAA),  // hud numerals: neon pink
    LV_COLOR_MAKE(0x2A, 0x2A, 0x2A),  // gauge off: unlit glass
    LV_COLOR_MAKE(0xFF, 0x4B, 0x3A),  // gauge hot: HUD red
};

// Frame colours shared by both themes. Neon pink is the HUD's own colour, the
// one the phone wears: the loading bars, never a data colour. The casing is
// the HUD's hard black.
constexpr lv_color_t kHudPink = LV_COLOR_MAKE(0xF3, 0x4D, 0xAA);
constexpr lv_color_t kOutlineBlack = LV_COLOR_MAKE(0x0A, 0x0A, 0x0A);

// One resolver for a theme name and the phone's day/night flag. AUTO has no
// palette of its own; the flag is the only thing that can resolve it.
const MapPalette &palette_for_theme(moto_ui_theme_t theme, bool ambient_night) {
    switch(theme) {
        case MOTO_UI_THEME_GTA_DAY: return kGtaDayPalette;
        case MOTO_UI_THEME_GTA_NIGHT: return kGtaNightPalette;
        case MOTO_UI_THEME_AUTO:
            return ambient_night ? kGtaNightPalette : kGtaDayPalette;
        // OFFICIAL is the classic backend's theme and never reaches this
        // one; day is the safe reading of anything unexpected.
        case MOTO_UI_THEME_OFFICIAL:
        default: return kGtaDayPalette;
    }
}

// Stroke for one MapRoadClass. Width carries the hierarchy as much as colour:
// a glance at 466 px reads thickness before hue.
struct RoadStroke {
    lv_color_t color;
    int width_design;
};

RoadStroke road_stroke(const MapPalette &palette, std::uint8_t road_class) {
    static constexpr int kWidths[6] = {5, 4, 4, 3, 2, 2};
    const std::uint8_t index = road_class < 6U ? road_class : 5U;
    return {palette.road_class[index], kWidths[index]};
}

// The boot screen is painted before the tree and before the phone can supply
// an ambient flag, so its theme is chosen by the caller from persisted
// settings and kept outside Ui (teardown_ui() would otherwise reset it).
moto_ui_theme_t g_boot_theme = MOTO_UI_THEME_GTA_NIGHT;

// Lean-page profiling. Off by default; the platform supplies the clock only in
// the firmware, so the Web/shared build carries the branches but no esp_timer
// dependency.
moto_ui_debug_clock_t g_debug_clock = nullptr;
bool g_debug_enabled = false;
std::uint32_t g_debug_hidden = 0;
moto_ui_debug_stats_t g_debug_stats{};
void apply_debug_hidden();

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
constexpr int kHomeBlipSizeDesign = 28;
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

// What the Lean arc shows: segments lit per side, and each side's held peak.
struct LeanArcState {
    int lit[2];   // fill in quarter degrees; [0] left, [1] right
    int peak[2];  // segment holding the side's peak, 0 for none
};

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
    // The page, theme and palette defaults are restored by reset_ui_state().
    // They are deliberately not member initialisers: a non-zero initialiser
    // moves this ~11 KiB object from .bss into .data, and .data cannot be
    // placed in PSRAM by EXT_RAM_BSS_ATTR.
    moto_ui_page_t page;
    bool page_dots_visible;

    lv_obj_t *nav_map = nullptr;
    lv_obj_t *nav_map_background = nullptr;
    // GTA is the product's own look, so it is the default rather than an
    // opt-in. MOTO_UI_THEME_OFFICIAL is the classic backend, not a palette.
    moto_ui_theme_t theme;
    // Day/night signal supplied by the phone. Only MOTO_UI_THEME_AUTO reads
    // it; an explicit theme stays authoritative.
    bool ambient_night = false;
    // Building footprints are one visibility decision covering the fill layer
    // and the outline lines. Hidden by default: at 466 px the block layer
    // competes with the roads that matter while moving, and the phone owns
    // the trade through SettingsState.show_buildings.
    bool show_buildings = false;
    MapPalette palette;
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
    // 0xFF means "this slot has never been styled"; a map scene revision
    // changes on every window, but a slot's class usually does not, and
    // restyling a full-canvas line invalidates it for no visual change.
    std::uint8_t nav_building_applied_class
        [MOTO_UI_BUILDING_FOOTPRINT_CAPACITY]{};
    std::uint8_t nav_building_point_count = 0;
    std::uint8_t nav_building_footprint_count = 0;
    std::uint32_t nav_building_scene_revision = 0;
    lv_obj_t *nav_roads[MOTO_UI_ROAD_POLYLINE_CAPACITY]{};
    // One layer under every road: the radar's dark casing. Drawn as a single
    // pass so a later road's casing never cuts across an earlier road.
    lv_obj_t *nav_road_casing = nullptr;
    MapPolyline nav_road_lines[MOTO_UI_ROAD_POLYLINE_CAPACITY]{};
    lv_point_precise_t nav_road_points[MOTO_UI_ROAD_POINT_CAPACITY]{};
    float nav_road_x[MOTO_UI_ROAD_POINT_CAPACITY]{};
    float nav_road_y[MOTO_UI_ROAD_POINT_CAPACITY]{};
    float nav_road_target_x[MOTO_UI_ROAD_POINT_CAPACITY]{};
    float nav_road_target_y[MOTO_UI_ROAD_POINT_CAPACITY]{};
    moto_ui_polyline_span_t
        nav_road_spans[MOTO_UI_ROAD_POLYLINE_CAPACITY]{};
    std::uint8_t nav_road_applied_class
        [MOTO_UI_ROAD_POLYLINE_CAPACITY]{};
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
    moto_maneuver_t nav_maneuver_type;
    lv_obj_t *nav_limit = nullptr;
    lv_obj_t *nav_limit_value = nullptr;
    lv_obj_t *nav_home = nullptr;
    lv_obj_t *nav_home_hint = nullptr;
    lv_timer_t *nav_home_hint_timer = nullptr;
    // Boot sequence: hard cuts on a timer, cleaned up by teardown_ui().
    lv_timer_t *boot_timer = nullptr;
    std::uint32_t boot_started_ms = 0;
    lv_obj_t *boot_grid = nullptr;
    lv_obj_t *boot_logo = nullptr;
    lv_obj_t *boot_bar = nullptr;
    lv_obj_t *boot_bar_fill = nullptr;
    std::uint16_t nav_speed_kph = 0;
    // The ring replaces the old 84-degree arc: the whole rim is the gauge now,
    // and the north badge rides it.
    lv_obj_t *nav_ring = nullptr;
    lv_obj_t *nav_north = nullptr;
    std::uint8_t nav_radar_progress = 0;
    moto_traffic_t nav_radar_traffic;

    // A full-screen empty-state surface replaces the old tiny
    // WAITING FOR PHONE / WAITING FOR ROUTE caption. It is kept inside the
    // navigation page so the speed and compass tools remain independently
    // usable while the phone is disconnected.
    lv_obj_t *nav_lifecycle = nullptr;
    lv_obj_t *nav_lifecycle_title = nullptr;
    // The pager's LCD line: the instruction, in Chinese.
    lv_obj_t *nav_lifecycle_subtitle = nullptr;
    lv_obj_t *nav_lifecycle_kicker = nullptr;
    lv_timer_t *nav_lifecycle_timer = nullptr;
    // The cover's gutter disc and its three panels (pager, title, city),
    // repainted on a theme switch.
    lv_obj_t *lifecycle_cover[2]{};
    lv_obj_t *lifecycle_panels[3]{};
    // Boot-style loading bar in the city panel, for states that wait on the
    // system; a blinking LCD cursor for states that wait on the rider.
    lv_obj_t *lifecycle_bar = nullptr;
    lv_obj_t *lifecycle_bar_fill = nullptr;
    lv_obj_t *lifecycle_cursor = nullptr;
    const char *lifecycle_message;
    std::uint32_t lifecycle_started_ms = 0;
    int lifecycle_bar_steps;
    ThemedText themed_text[kThemedTextCapacity]{};
    std::uint8_t themed_text_count = 0;

    // Filled plates are themed the same way text is: the band plate
    // and the ink counter slab are different roles and must not be swapped.
    enum class FillRole : std::uint8_t { Accent = 0, Slab };
    struct ThemedFill {
        lv_obj_t *object;
        FillRole role;
    };
    ThemedFill themed_fills[32]{};
    std::uint8_t themed_fill_count = 0;
    lv_timer_t *nav_success_timer = nullptr;
    moto_ui_phone_connection_t phone_connection;
    LifecycleVisual lifecycle_visual;
    LifecycleVisual lifecycle_target;
    bool lifecycle_success_active = false;
    bool navigation_has_guidance = false;
    bool navigation_route_request_in_flight = false;

    // Speed·G: friction circle. Display values are the 4 Hz low-pass, the
    // envelope is built from 0.2 s means (DESIGN_V2 §3.6).
    lv_obj_t *gg_plot = nullptr;
    lv_obj_t *gg_dot = nullptr;
    lv_obj_t *gg_trail[6]{};
    lv_obj_t *gg_live = nullptr;
    lv_obj_t *gg_speed = nullptr;
    lv_obj_t *gg_state = nullptr;
    float gg_lat = 0.0F;
    float gg_lon = 0.0F;
    float gg_mean_lat = 0.0F;
    float gg_mean_lon = 0.0F;
    float gg_envelope[16]{};
    float gg_trail_lat[6]{};
    float gg_trail_lon[6]{};
    std::uint32_t gg_trail_ms[6]{};
    int gg_trail_head = 0;
    std::uint32_t gg_trail_last_ms = 0;
    std::uint32_t gg_last_ms = 0;
    bool gg_seeded = false;
    bool gg_has = false;
    bool gg_envelope_dirty = false;
    bool gg_press_active = false;
    std::uint32_t gg_press_start_ms = 0;

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
    PerfState perf_state;
    std::uint32_t perf_run_start_ms = 0;
    std::uint32_t perf_still_since_ms = 0;
    std::uint16_t perf_best_centis = 0;
    std::uint32_t perf_last_centis = 0;
    lv_obj_t *perf_long_g = nullptr;
    lv_obj_t *perf_lat_g = nullptr;
    lv_obj_t *perf_gg_dot = nullptr;
    char lean_text[24]{};
    lv_obj_t *lean_scene = nullptr;
    lv_obj_t *lean_marks = nullptr;
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
    bool lean_scene_dirty;
    // The numerals' own clock and value (DESIGN_V2 §2.7: 10 Hz, 0.6 deg
    // hysteresis). The horizon, sun and blip follow lean_deg every frame.
    std::uint32_t lean_text_tick_ms = 0;
    float lean_text_deg = 0.0F;
    // The lean the horizon was last dirtied for, and whether a newer sample
    // is waiting for the next refresh (lean_refresh_start()).
    float lean_drawn_deg = 0.0F;
    LeanArcState lean_drawn_arc{};
    bool lean_motion_pending = false;
    lv_display_t *lean_display = nullptr;

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
    moto_lean_calibration_callback_t lean_calibration_callback = nullptr;
    void *lean_calibration_context = nullptr;
    moto_lean_clear_peaks_callback_t lean_clear_callback = nullptr;
    void *lean_clear_context = nullptr;
    moto_ui_performance_callback_t perf_callback = nullptr;
    void *perf_context = nullptr;
    std::uint32_t perf_run_id = 0;
    std::uint32_t perf_best_ms = 0;
    lv_obj_t *lean_status_plate = nullptr;
    lv_obj_t *lean_status_text = nullptr;
    lv_obj_t *lean_progress = nullptr;
    lv_timer_t *lean_press_timer = nullptr;
    lv_timer_t *lean_status_timer = nullptr;
    uint32_t lean_press_start_ms = 0;
    uint32_t lean_press_elapsed_ms = 0;
    bool lean_press_active = false;
    bool lean_progress_visible = false;
    volatile uint8_t lean_cal_state = 0;
    volatile uint8_t lean_cal_value = 0;
    volatile uint8_t lean_flag_cal = 0;
    volatile uint8_t lean_flag_poll = 0;
    volatile uint8_t lean_flag_full = 0;
    volatile uint8_t lean_flag_dirty = 0;
    uint8_t lean_cal_applied;
    uint8_t lean_cal_value_applied;
    uint8_t lean_flags_applied;
    bool lean_countdown_pending = false;
    uint32_t lean_countdown_ms = 0;
    uint32_t lean_result_ms = 0;
    bool demo_active = false;
    bool reduce_motion = false;
    // True only while the full widget tree from moto_ui_gta::create() exists.
    // A terminal screen clears it, so every mutator becomes inert in the
    // shutdown window instead of trusting a pointer that still looks valid.
    bool live = false;
};

// Every other tree state is a few hundred bytes; this one is ~11 KiB because
// it owns the map, GG and lean buffers. Internal DRAM is the scarce pool (the
// BLE stack and all task stacks come from it), so keep it in PSRAM. The
// object must stay zero-initialised for EXT_RAM_BSS_ATTR to place it there;
// reset_ui_state() restores the handful of non-zero defaults.
MOTO_UI_EXT_RAM Ui ui;

void reset_ui_state() {
    static_assert(std::is_trivially_copyable_v<Ui>,
                  "Ui must remain safe to reset without a stack temporary");

    // `ui = {}` materializes the complete aggregate as a temporary. The
    // offline map buffers make Ui about 14 KiB, so that temporary can consume
    // nearly the whole ESP-IDF main-task stack before LVGL is called. Clear
    // the retained instance in place and restore the non-zero defaults.
    //
    // This also clears `ui.live` and the four callbacks, so a caller that
    // intends to receive taps must install them *after* create(); a callback
    // registered before it is silently discarded.
    std::memset(&ui, 0, sizeof(ui));
    ui.page = MOTO_UI_PAGE_NAVIGATION;
    ui.page_dots_visible = true;
    ui.nav_maneuver_type = MOTO_MANEUVER_STRAIGHT;
    ui.nav_radar_traffic = MOTO_TRAFFIC_UNKNOWN;
    ui.phone_connection = MOTO_UI_PHONE_OFFLINE;
    ui.theme = MOTO_UI_THEME_GTA_DAY;
    ui.palette = kGtaDayPalette;
    // The cover's first paint reads these before any lifecycle refresh.
    ui.lifecycle_message = "";
    ui.lifecycle_bar_steps = -1;
    // 255 is "never applied", so the first Lean sample always reaches the
    // status plate even when its state is zero.
    ui.lean_cal_applied = 255;
    ui.lean_cal_value_applied = 255;
    ui.lean_flags_applied = 255;
    std::memset(ui.nav_road_applied_class, 0xFF,
                sizeof(ui.nav_road_applied_class));
    std::memset(ui.nav_building_applied_class, 0xFF,
                sizeof(ui.nav_building_applied_class));
    ui.lifecycle_visual = LifecycleVisual::Hidden;
    ui.lifecycle_target = LifecycleVisual::Hidden;
    // The Lean page's first state must apply its text even when it has no
    // attitude; zeroed, a tree built before the IMU's first sample never
    // showed NO IMU.
    ui.lean_scene_dirty = true;
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
void lean_refresh_start(lv_event_t *);

void teardown_ui() {
    // The display outlives every tree; its hook must not call into a reset
    // Ui or into the classic backend's screen.
    if(ui.lean_display != nullptr) {
        lv_display_remove_event_cb_with_user_data(ui.lean_display,
                                                  lean_refresh_start, nullptr);
        ui.lean_display = nullptr;
    }
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
    if(ui.boot_timer != nullptr) {
        lv_timer_delete(ui.boot_timer);
        ui.boot_timer = nullptr;
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
        case TextRole::Display: return ui.palette.hud;
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
// casing under one band (the GTA III cover's panel gutter). A page may not invent its own frame, so the
// six screens read as six readouts of one instrument rather than six screens.
// ---------------------------------------------------------------------------
constexpr int kDialRadius = 207;       // paper edge, under the casing
// LVGL arcs stroke inward from the given radius, so these are outer edges.
// DESIGN.md: casing 197-212, band 212-222, glass edge outside 222. The band
// used to start at 207, which left a 5 px black moat inside the glass and made
// every page read as a smaller circle than the poster.
constexpr int kDialBandOuter = 222;    // band outer edge
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

// Nearest and farthest distance from the canvas centre to the area being
// drawn.
void clip_radius_range(const lv_layer_t *layer, double &nearest,
                       double &farthest) {
    const lv_area_t &clip = layer->_clip_area;
    const double cx = MOTO_UI_CANVAS_WIDTH / 2.0;
    const double cy = MOTO_UI_CANVAS_HEIGHT / 2.0;
    const double x1 = clip.x1 - cx;
    const double x2 = clip.x2 + 1 - cx;
    const double y1 = clip.y1 - cy;
    const double y2 = clip.y2 + 1 - cy;
    const double nx = x1 > 0.0 ? x1 : (x2 < 0.0 ? x2 : 0.0);
    const double ny = y1 > 0.0 ? y1 : (y2 < 0.0 ? y2 : 0.0);
    nearest = std::sqrt(nx * nx + ny * ny);
    const double fx = std::max(std::fabs(x1), std::fabs(x2));
    const double fy = std::max(std::fabs(y1), std::fabs(y2));
    farthest = std::sqrt(fx * fx + fy * fy);
}

void stroke_ring(lv_layer_t *layer, int radius, int width, lv_color_t color) {
    // An arc is the costliest primitive on the rim: two radius masks and an
    // angle mask per half. A dirty area that only grazes the rim used to pay
    // for six of them, so a ring, and each half of it, is stroked only where
    // the area actually reaches.
    double nearest, farthest;
    clip_radius_range(layer, nearest, farthest);
    if(farthest < radius - width - 1 || nearest > radius + 1) return;
    const lv_area_t &clip = layer->_clip_area;
    const int32_t cy = MOTO_UI_CANVAS_HEIGHT / 2;
    lv_draw_arc_dsc_t dsc;
    lv_draw_arc_dsc_init(&dsc);
    dsc.center = {MOTO_UI_CANVAS_WIDTH / 2, cy};
    dsc.radius = radius;
    dsc.width = width;
    dsc.color = color;
    dsc.opa = LV_OPA_COVER;
    dsc.rounded = 0;
    // Two halves: a full 360 arc is the one thing every LVGL major version
    // treats differently, and the rim is too visible to gamble.
    if(clip.y2 >= cy - 1) {
        dsc.start_angle = 0;
        dsc.end_angle = 180;
        lv_draw_arc(layer, &dsc);
    }
    if(clip.y1 <= cy + 1) {
        dsc.start_angle = 180;
        dsc.end_angle = 360;
        lv_draw_arc(layer, &dsc);
    }
}

// True when the segment's box, grown by `pad`, overlaps the area being drawn.
bool clip_touches_segment(const lv_layer_t *layer, const lv_point_precise_t &a,
                          const lv_point_precise_t &b, int pad) {
    const lv_area_t &clip = layer->_clip_area;
    return std::min(a.x, b.x) - pad <= clip.x2 &&
           std::max(a.x, b.x) + pad >= clip.x1 &&
           std::min(a.y, b.y) - pad <= clip.y2 &&
           std::max(a.y, b.y) + pad >= clip.y1;
}

// True when the area being drawn lies wholly inside a circle of `radius`
// around the canvas centre, so nothing on or outside that circle can touch it.
// A moving horizon dirties small strips inside the disc; without this cull each
// strip re-stroked all six rim arcs, ~21 ms of a 45 ms Lean frame on the board.
bool clip_inside_radius(const lv_layer_t *layer, int radius) {
    const lv_area_t &clip = layer->_clip_area;
    const double cx = MOTO_UI_CANVAS_WIDTH / 2.0;
    const double cy = MOTO_UI_CANVAS_HEIGHT / 2.0;
    const double r2 = static_cast<double>(radius) * radius;
    const double xs[2] = {clip.x1 - cx, clip.x2 + 1 - cx};
    const double ys[2] = {clip.y1 - cy, clip.y2 + 1 - cy};
    for(double x : xs) {
        for(double y : ys) {
            if(x * x + y * y >= r2) return false;
        }
    }
    return true;
}

void draw_instrument_frame_rings(lv_layer_t *layer) {
    // The bezel is painted over the map, so no sliver of map can show between
    // the rim and the glass edge.
    // Bezel 222..233, band 212..222, casing 197..212. The three rings share
    // edges, so no sliver of ground can leak between them.
    stroke_ring(layer, kDialBezelOuter, kDialBezelOuter - kDialBandOuter,
                ui.palette.frame);
    stroke_ring(layer, kDialBandOuter, kDialBandWidth, ui.palette.accent);
    stroke_ring(layer, kDialCasingOuter, kDialCasingWidth, kOutlineBlack);
}

void draw_instrument_frame_layer(lv_layer_t *layer) {
    // Casing inner edge: kDialCasingOuter - kDialCasingWidth.
    if(clip_inside_radius(layer, kDialCasingOuter - kDialCasingWidth)) return;
    draw_instrument_frame_rings(layer);
}

lv_obj_t *register_fill(lv_obj_t *object, Ui::FillRole role) {
    if(ui.themed_fill_count < 32) {
        ui.themed_fills[ui.themed_fill_count].object = object;
        ui.themed_fills[ui.themed_fill_count].role = role;
        ++ui.themed_fill_count;
    }
    return object;
}

// The band plate: the brand block, the thing that carries a short label.
lv_obj_t *make_plate(lv_obj_t *parent, const char *text, int width,
                     int height, lv_obj_t **label_out = nullptr) {
    lv_obj_t *plate = lv_obj_create(parent);
    lv_obj_remove_style_all(plate);
    lv_obj_clear_flag(plate, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(plate, px(width), px(height));
    // HUD type, not a tag: GTA III's HUD never boxes a label, it cases the
    // letters. The cream boxes made every page read as a settings screen.
    // The object keeps its size so layouts and hit areas do not move.
    lv_obj_set_style_bg_color(plate, ui.palette.accent, 0);
    lv_obj_set_style_bg_opa(plate, LV_OPA_TRANSP, 0);
    lv_obj_t *label = make_text(plate, &moto_font_display_24,
                                TextRole::Ink, text);
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
constexpr lv_color_t kLimitFace = LV_COLOR_MAKE(0xF2, 0xEE, 0xE4);

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
void draw_instrument_frame(lv_event_t *event) {
    draw_instrument_frame_layer(lv_event_get_layer(event));
}

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
    apply_debug_hidden();
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
    // HUD orange on a black casing, the phone radar's safehouse: it reads on
    // the grey map, on the waiting screen's paper and on the rim. It is the
    // rider arrow's size and has eaves and a door, so it reads as a house at a
    // glance instead of an orange speck.
    lv_obj_t *object = lv_event_get_target_obj(event);
    lv_area_t area;
    lv_obj_get_coords(object, &area);
    lv_layer_t *layer = lv_event_get_layer(event);
    const lv_value_precise_t left = area.x1;
    const lv_value_precise_t top = area.y1;
    const lv_value_precise_t right = area.x2;
    const lv_value_precise_t bottom = area.y2;
    const lv_value_precise_t size = right - left;
    const lv_value_precise_t centre = (left + right) / 2;
    const lv_value_precise_t eaves = top + size * 0.48;
    const lv_value_precise_t wall = size * 0.16;
    const int32_t casing = px(3);

    lv_draw_rect_dsc_t body;
    lv_draw_rect_dsc_init(&body);
    body.radius = 0;
    body.bg_opa = LV_OPA_COVER;
    body.border_width = 0;
    lv_draw_triangle_dsc_t triangle;
    lv_draw_triangle_dsc_init(&triangle);
    triangle.opa = LV_OPA_COVER;

    // Casing pass: roof wider than the walls, so the eaves overhang.
    body.bg_color = kBlack;
    triangle.color = kBlack;
    lv_area_t walls{static_cast<int32_t>(left + wall),
                    static_cast<int32_t>(eaves),
                    static_cast<int32_t>(right - wall),
                    static_cast<int32_t>(bottom)};
    lv_draw_rect(layer, &body, &walls);
    triangle.p[0] = {centre, top};
    triangle.p[1] = {left, eaves + casing};
    triangle.p[2] = {right, eaves + casing};
    lv_draw_triangle(layer, &triangle);

    // Fill pass, inset so the hard casing stays on every side.
    body.bg_color = ui.palette.route;
    triangle.color = ui.palette.route;
    walls.x1 += casing;
    walls.x2 -= casing;
    walls.y1 += 1;
    walls.y2 -= casing;
    lv_draw_rect(layer, &body, &walls);
    triangle.p[0] = {centre, top + casing * 2};
    triangle.p[1] = {left + casing * 2, eaves};
    triangle.p[2] = {right - casing * 2, eaves};
    lv_draw_triangle(layer, &triangle);

    // Door, cut back to the casing.
    body.bg_color = kBlack;
    const lv_value_precise_t door = size * 0.12;
    lv_area_t doorway{static_cast<int32_t>(centre - door),
                      static_cast<int32_t>(bottom - size * 0.34),
                      static_cast<int32_t>(centre + door),
                      walls.y2};
    lv_draw_rect(layer, &body, &doorway);
}

// ---------------------------------------------------------------------------
// Liberty City at dusk. The tool pages used to be a black disc with one gold
// number, which read as a sports watch; PRODUCT.md's grammar is radar, HUD
// band, condensed numerals and dusk silhouettes, and the silhouettes were
// missing. The skyline is frame, not data: it sits under the casing at the
// bottom of the disc, below every reading, and is static.

struct Tower {
    std::int16_t x;      // left edge, px from the centre
    std::int16_t width;
    std::int16_t top;    // px below the centre
    std::int8_t spire;   // spire height above the roof, 0 for none
};

// Far row: taller, lighter (haze). Near row: lower, darker, in front.
constexpr Tower kFarTowers[] = {
    {-150, 22, 140, 0}, {-128, 18, 122, 0}, {-110, 26, 104, 10},
    {-84, 16, 126, 0},  {-68, 30, 96, 0},   {-38, 16, 118, 0},
    {-22, 26, 80, 26},  {4, 18, 106, 0},    {22, 30, 94, 12},
    {52, 16, 120, 0},   {68, 26, 100, 0},   {94, 18, 128, 0},
    {112, 28, 110, 8},  {140, 20, 136, 0},
};
constexpr Tower kNearTowers[] = {
    {-170, 40, 156, 0}, {-130, 30, 146, 0}, {-100, 44, 160, 0},
    {-56, 34, 150, 0},  {-22, 48, 162, 0},  {26, 36, 148, 0},
    {62, 42, 158, 0},   {104, 34, 150, 0},  {138, 40, 160, 0},
};

void draw_towers(lv_layer_t *layer, const Tower *towers, std::size_t count,
                 int cx, int cy, lv_color_t body_color, lv_color_t window,
                 bool lit, int foot_reach) {
    // On a page the feet stop at r = 210, under the casing (197..212), which
    // hides the stair steps; past it a tower would paint the bezel. Screens
    // that clip to the circle themselves pass a reach beyond the glass. Lamps
    // stop where a casing would start, so none is lit under it.
    const int kFootReach = foot_reach;
    constexpr int kLampReach = 194;
    lv_draw_rect_dsc_t body;
    lv_draw_rect_dsc_init(&body);
    body.radius = 0;
    body.bg_opa = LV_OPA_COVER;
    body.bg_color = body_color;
    body.border_width = 0;
    lv_draw_rect_dsc_t lamp;
    lv_draw_rect_dsc_init(&lamp);
    lamp.radius = 0;
    lamp.bg_color = window;
    lamp.bg_opa = LV_OPA_70;
    lamp.border_width = 0;
    for(std::size_t i = 0; i < count; ++i) {
        const Tower &tower = towers[i];
        const int far_x = std::max(std::abs(static_cast<int>(tower.x)),
                                   std::abs(tower.x + tower.width));
        if(far_x >= kFootReach) continue;
        const int foot = static_cast<int>(std::sqrt(static_cast<double>(
            kFootReach * kFootReach - far_x * far_x)));
        if(tower.top >= foot) continue;
        const int bottom =
            far_x >= kLampReach
                ? tower.top
                : static_cast<int>(std::sqrt(static_cast<double>(
                      kLampReach * kLampReach - far_x * far_x)));
        lv_area_t area{cx + tower.x, cy + tower.top,
                       cx + tower.x + tower.width - 1, cy + foot};
        lv_draw_rect(layer, &body, &area);
        if(tower.spire > 0) {
            lv_draw_triangle_dsc_t spire;
            lv_draw_triangle_dsc_init(&spire);
            spire.color = body_color;
            spire.opa = LV_OPA_COVER;
            const lv_value_precise_t mid =
                cx + tower.x + tower.width / 2.0F;
            spire.p[0] = {mid, static_cast<lv_value_precise_t>(
                                   cy + tower.top - tower.spire)};
            spire.p[1] = {mid - 4, static_cast<lv_value_precise_t>(
                                       cy + tower.top)};
            spire.p[2] = {mid + 4, static_cast<lv_value_precise_t>(
                                       cy + tower.top)};
            lv_draw_triangle(layer, &spire);
        }
        if(!lit) continue;
        // A few lit windows, placed by a fixed hash so the city never
        // flickers between frames.
        for(int row = cy + tower.top + 6; row + 3 < cy + bottom - 4;
            row += 9) {
            for(int col = cx + tower.x + 4; col + 2 < cx + tower.x +
                                                   tower.width - 3;
                col += 6) {
                const unsigned hash =
                    static_cast<unsigned>(row * 73856093) ^
                    static_cast<unsigned>(col * 19349663) ^
                    static_cast<unsigned>(i * 83492791);
                if(hash % 7U != 0U) continue;
                lv_area_t pane{col, row, col + 1, row + 2};
                lv_draw_rect(layer, &lamp, &pane);
            }
        }
    }
}

void draw_skyline(lv_event_t *event) {
    lv_obj_t *object = lv_event_get_target_obj(event);
    const auto *given =
        static_cast<const MapPalette *>(lv_event_get_user_data(event));
    const MapPalette &palette = given != nullptr ? *given : ui.palette;
    lv_area_t area;
    lv_obj_get_coords(object, &area);
    // A skyline sized to a cover panel (USER_2) is cropped by its own box,
    // like the box art's illustrations, and keeps the canvas's rows.
    const bool cropped = lv_obj_has_flag(object, LV_OBJ_FLAG_USER_2);
    const int cx = (cropped ? 0 : area.x1) + MOTO_UI_CANVAS_WIDTH / 2;
    const int cy = (cropped ? 0 : area.y1) + MOTO_UI_CANVAS_HEIGHT / 2;
    lv_layer_t *layer = lv_event_get_layer(event);
    // Nothing of the city stands above its tallest spire (the far row's
    // {-22, 26, 80, 26} tower): an area above that, such as the Lean arc or
    // numeral, costs no tower loop.
    constexpr int kSkylineTop = 80 - 26;
    if(layer->_clip_area.y2 < cy + kSkylineTop) return;
    // A screen that clips to the circle itself (waiting screen, boot) lets
    // the feet run past the glass; a page relies on its casing.
    const int reach = lv_obj_has_flag(object, LV_OBJ_FLAG_USER_1) ? 240 : 210;
    // A caller's own palette (the boot's dusk) is drawn as given: towers in
    // its building tones, lamps in its sky. Otherwise, by night the city is
    // lit in neon pink on near-black towers; by day it is a print, two pale
    // greys on the newsprint with paper windows, so it never outweighs the
    // ink numerals above it.
    const bool night = lv_color_luminance(palette.ground) < 128;
    const bool own = given != nullptr;
    const lv_color_t window = (night && !own) ? palette.hud : palette.ground;
    const lv_color_t far = (night || own) ? palette.building_landmark
                                          : palette.road_class[3];
    const lv_color_t near = (night || own) ? palette.building
                                           : palette.road_class[4];
    draw_towers(layer, kFarTowers, sizeof(kFarTowers) / sizeof(kFarTowers[0]),
                cx, cy, far, window, night && !own, reach);
    draw_towers(layer, kNearTowers,
                sizeof(kNearTowers) / sizeof(kNearTowers[0]), cx, cy, near,
                window, true, reach);
}

// Full-canvas skyline layer. `palette` is for screens drawn before the tree
// has a theme (the boot sequence); pages pass nullptr and follow ui.palette.
constexpr int kPageSkylineDrop = 22;

lv_obj_t *make_skyline(lv_obj_t *parent, const MapPalette *palette) {
    lv_obj_t *sky = lv_obj_create(parent);
    lv_obj_remove_style_all(sky);
    lv_obj_clear_flag(sky, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(sky, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(sky, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_set_size(sky, MOTO_UI_CANVAS_WIDTH, MOTO_UI_CANVAS_HEIGHT);
    lv_obj_set_pos(sky, 0, 0);
    lv_obj_set_name_static(sky, "moto-skyline");
    lv_obj_add_event_cb(sky, draw_skyline, LV_EVENT_DRAW_MAIN,
                        const_cast<MapPalette *>(palette));
    return sky;
}

// Dusk over the city, for the cover panels (boot and link screens): a
// poster may be loud. HUD orange sky, black towers lit in the same orange.
// Every field the skyline reads is the same in both themes.
const MapPalette *dusk_palette() {
    static MapPalette dusk = [] {
        MapPalette palette = kGtaNightPalette;
        palette.ground = kGtaNightPalette.route;
        palette.building = kOutlineBlack;
        palette.building_landmark = LV_COLOR_MAKE(0x6A, 0x30, 0x10);
        return palette;
    }();
    return &dusk;
}

// A tool page's skyline goes between its paper disc and its casing, so the
// casing trims the towers' feet to the glass.
void add_page_skyline(lv_obj_t *page) {
    lv_obj_t *sky = make_skyline(page, nullptr);
    // Lowered on the tool pages so the city stays under the secondary row.
    lv_obj_set_y(sky, kPageSkylineDrop);
    lv_obj_move_to_index(sky, 1);
}

// MOTO_UI_PAGE_SPEED is the Speed·G page in this tree (DESIGN_V2 §7): a
// friction circle with speed small. The old speed-only page duplicated the
// compass and is gone. Swipe order and dots follow this list; the second slot
// is where §2.2 puts Speed·G.
constexpr moto_ui_page_t kPagerOrder[] = {
    MOTO_UI_PAGE_NAVIGATION, MOTO_UI_PAGE_SPEED, MOTO_UI_PAGE_PERFORMANCE,
    MOTO_UI_PAGE_LEAN, MOTO_UI_PAGE_COMPASS, MOTO_UI_PAGE_MUSIC};
constexpr int kPagerCount =
    static_cast<int>(sizeof(kPagerOrder) / sizeof(kPagerOrder[0]));

moto_ui_page_t resolve_page(moto_ui_page_t page) { return page; }

int pager_slot(moto_ui_page_t page) {
    for(int slot = 0; slot < kPagerCount; ++slot) {
        if(kPagerOrder[slot] == page) return slot;
    }
    return 0;
}

void update_page_dots() {
    for(int i = 0; i < kPagerCount; ++i) {
        const bool active = kPagerOrder[i] == ui.page;
        const int width = px(active ? 16 : 5);
        lv_obj_set_size(ui.page_dots[i], width, px(5));
        lv_obj_set_style_radius(ui.page_dots[i], px(3), 0);
        lv_obj_set_style_bg_color(
            ui.page_dots[i], active ? ui.palette.accent : ui.palette.map_ink, 0);
        lv_obj_set_style_bg_opa(ui.page_dots[i],
                                active ? LV_OPA_COVER : LV_OPA_40, 0);
        // The dots ride the casing, not a straight row: at y=+203 the outer
        // ends of a row sit outside the glass (v2 review §4).
        const double radius = 203.0;
        const double dx = (i - (kPagerCount - 1) / 2.0) * px(22);
        const double dy = std::sqrt(radius * radius - dx * dx);
        lv_obj_set_x(ui.page_dots[i],
                     static_cast<int>(std::lround(
                         MOTO_UI_CANVAS_WIDTH / 2.0 + dx - width / 2.0)));
        lv_obj_set_y(ui.page_dots[i],
                     static_cast<int>(std::lround(
                         MOTO_UI_CANVAS_HEIGHT / 2.0 + dy - px(5) / 2.0)));
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
    page = resolve_page(page);
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
    int slot = pager_slot(ui.page);
    if(direction == LV_DIR_LEFT) {
        slot = (slot + 1) % kPagerCount;
    } else if(direction == LV_DIR_RIGHT) {
        slot = (slot + kPagerCount - 1) % kPagerCount;
    } else {
        return;
    }
    const moto_ui_page_t requested = kPagerOrder[slot];
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
    // A motion frame now invalidates only the streets/buildings that moved.
    // Building fills are drawn one scanline at a time, so reject the rows and
    // columns outside that dirty area instead of submitting thousands of
    // fully clipped rectangles to LVGL.
    const lv_area_t clip = layer->_clip_area;
    const int32_t local_width = area.x2 - area.x1;
    const int32_t local_height = area.y2 - area.y1;
    const int32_t clip_left = std::max<int32_t>(0, clip.x1 - area.x1);
    const int32_t clip_right = std::min<int32_t>(local_width,
                                                  clip.x2 - area.x1);

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

        float minimum_x = static_cast<float>(points[0].x);
        float maximum_x = static_cast<float>(points[0].x);
        float minimum_y = static_cast<float>(points[0].y);
        float maximum_y = static_cast<float>(points[0].y);
        for(std::uint8_t i = 1; i < count; ++i) {
            minimum_x = std::min(minimum_x, static_cast<float>(points[i].x));
            maximum_x = std::max(maximum_x, static_cast<float>(points[i].x));
            minimum_y = std::min(minimum_y, static_cast<float>(points[i].y));
            maximum_y = std::max(maximum_y, static_cast<float>(points[i].y));
        }
        if(area.x1 + maximum_x < clip.x1 ||
           area.x1 + minimum_x > clip.x2) {
            packed_index += static_cast<std::size_t>(count) + 1U;
            continue;
        }
        // Convex footprints are the common case, and a triangle fan is O(n)
        // draw tasks instead of one per scanline. The fan triangles are
        // expanded about their own centroid by a fraction of the inradius so
        // neighbouring triangles overlap along the shared spokes; without that
        // overlap LVGL's antialiased triangle edges leave a visible seam.
        bool convex = count >= 3;
        int turn_sign = 0;
        for(std::uint8_t i = 0; i < count && convex; ++i) {
            const lv_point_precise_t &a = points[i];
            const lv_point_precise_t &b = points[(i + 1U) % count];
            const lv_point_precise_t &c = points[(i + 2U) % count];
            const float cross =
                (b.x - a.x) * (c.y - b.y) - (b.y - a.y) * (c.x - b.x);
            if(std::abs(cross) < 0.01F) continue;
            const int sign = cross > 0 ? 1 : -1;
            if(turn_sign == 0) {
                turn_sign = sign;
            } else if(turn_sign != sign) {
                convex = false;
            }
        }
        if(convex) {
            float centre_x = 0.0F;
            float centre_y = 0.0F;
            for(std::uint8_t i = 0; i < count; ++i) {
                centre_x += static_cast<float>(points[i].x);
                centre_y += static_cast<float>(points[i].y);
            }
            centre_x /= static_cast<float>(count);
            centre_y /= static_cast<float>(count);
            lv_draw_triangle_dsc_t triangle;
            lv_draw_triangle_dsc_init(&triangle);
            triangle.color = fill.bg_color;
            triangle.opa = LV_OPA_COVER;
            for(std::uint8_t i = 0; i < count; ++i) {
                const lv_point_precise_t &a = points[i];
                const lv_point_precise_t &b = points[(i + 1U) % count];
                const float tri_cx =
                    (centre_x + static_cast<float>(a.x) +
                     static_cast<float>(b.x)) / 3.0F;
                const float tri_cy =
                    (centre_y + static_cast<float>(a.y) +
                     static_cast<float>(b.y)) / 3.0F;
                const float ab = std::hypot(
                    static_cast<float>(b.x - a.x),
                    static_cast<float>(b.y - a.y));
                const float ca = std::hypot(
                    static_cast<float>(a.x - centre_x),
                    static_cast<float>(a.y - centre_y));
                const float cb = std::hypot(
                    static_cast<float>(b.x - centre_x),
                    static_cast<float>(b.y - centre_y));
                const float twice_area = std::abs(
                    (static_cast<float>(a.x - centre_x) *
                         static_cast<float>(b.y - centre_y)) -
                    (static_cast<float>(a.y - centre_y) *
                         static_cast<float>(b.x - centre_x)));
                const float perimeter = ab + ca + cb;
                const float inradius =
                    perimeter > 0.0F ? twice_area / perimeter : 1.0F;
                const float scale =
                    1.0F + 0.75F / std::max(1.0F, inradius);
                const auto expand = [&](float x, float y, int slot) {
                    triangle.p[slot] = {
                        static_cast<lv_value_precise_t>(
                            area.x1 + tri_cx + (x - tri_cx) * scale),
                        static_cast<lv_value_precise_t>(
                            area.y1 + tri_cy + (y - tri_cy) * scale),
                    };
                };
                expand(centre_x, centre_y, 0);
                expand(static_cast<float>(a.x), static_cast<float>(a.y), 1);
                expand(static_cast<float>(b.x), static_cast<float>(b.y), 2);
                lv_draw_triangle(layer, &triangle);
            }
            packed_index += static_cast<std::size_t>(count) + 1U;
            continue;
        }

        const int32_t first_row = std::max(
            std::max<int32_t>(0, clip.y1 - area.y1),
            static_cast<int32_t>(std::ceil(minimum_y)));
        const int32_t last_row = std::min(
            std::min<int32_t>(local_height, clip.y2 - area.y1),
            static_cast<int32_t>(std::floor(maximum_y)));
        // Most OSM footprints are rectangles: the scanline span repeats for
        // every row. Merge identical spans into one rectangle instead of
        // submitting one draw task per row. The old row-per-rect path was the
        // dominant cost of a moving map frame once building fills were on.
        bool run_active = false;
        int32_t run_x1 = 0;
        int32_t run_x2 = 0;
        int32_t run_y1 = 0;
        int32_t run_y2 = 0;
        const auto flush_run = [&]() {
            if(!run_active) return;
            lv_area_t run_area = {
                area.x1 + run_x1,
                area.y1 + run_y1,
                area.x1 + run_x2,
                area.y1 + run_y2,
            };
            lv_draw_rect(layer, &fill, &run_area);
            run_active = false;
        };
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
            if(crossing_count == 2) {
                const int32_t first_column = std::max(
                    clip_left, static_cast<int32_t>(
                                   std::ceil(crossings[0] - 0.5F)));
                const int32_t last_column = std::min(
                    clip_right, static_cast<int32_t>(
                                    std::floor(crossings[1] - 0.5F)));
                if(first_column <= last_column) {
                    if(run_active && run_x1 == first_column &&
                       run_x2 == last_column && run_y2 + 1 == row) {
                        run_y2 = row;
                    } else {
                        flush_run();
                        run_active = true;
                        run_x1 = first_column;
                        run_x2 = last_column;
                        run_y1 = row;
                        run_y2 = row;
                    }
                    continue;
                }
            }
            flush_run();
            for(std::size_t i = 0; i + 1 < crossing_count; i += 2) {
                const int32_t first_column = std::max(
                    clip_left, static_cast<int32_t>(
                                   std::ceil(crossings[i] - 0.5F)));
                const int32_t last_column = std::min(
                    clip_right, static_cast<int32_t>(
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
        flush_run();
        packed_index += static_cast<std::size_t>(count) + 1U;
    }
}

// Total extra width of a road's casing (one pixel each side at 466 px).
constexpr int32_t kRoadCasingPx = 3;

void draw_map_polyline(lv_event_t *event) {
    lv_obj_t *object = lv_event_get_target_obj(event);
    const auto code = lv_event_get_code(event);
    if(code == LV_EVENT_REFR_EXT_DRAW_SIZE) {
        // Plus the casing margin: a road's dirty area must also cover the
        // casing layer's stroke under it, so that layer never needs its own
        // full-canvas invalidation.
        auto *size = static_cast<int32_t *>(lv_event_get_param(event));
        *size = std::max(*size, lv_obj_get_style_line_width(object, LV_PART_MAIN) +
                                    kRoadCasingPx);
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

// GTA III radar grammar: each road is a light stroke on a dark casing. The
// casing pass draws every visible road wider, in the casing colour, before
// any road is drawn.
void draw_road_casings(lv_event_t *event) {
    lv_layer_t *layer = lv_event_get_layer(event);
    lv_draw_line_dsc_t line;
    lv_draw_line_dsc_init(&line);
    line.base.layer = layer;
    line.color = ui.palette.route_shadow;
    line.opa = LV_OPA_COVER;
    line.round_start = 1;
    line.round_end = 1;
    for(std::uint8_t i = 0; i < ui.nav_road_polyline_count; ++i) {
        lv_obj_t *road = ui.nav_roads[i];
        if(road == nullptr || lv_obj_has_flag(road, LV_OBJ_FLAG_HIDDEN)) continue;
        const MapPolyline &polyline = ui.nav_road_lines[i];
        if(polyline.points == nullptr || polyline.count < 2) continue;
        lv_area_t area;
        lv_obj_get_coords(road, &area);
        line.width = lv_obj_get_style_line_width(road, LV_PART_MAIN) + kRoadCasingPx;
        for(std::uint16_t index = 1; index < polyline.count; ++index) {
            line.p1 = polyline.points[index - 1];
            line.p2 = polyline.points[index];
            line.p1.x += area.x1;
            line.p2.x += area.x1;
            line.p1.y += area.y1;
            line.p2.y += area.y1;
            draw_map_line(layer, line);
        }
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

// Rebind the line's shared point array. This deliberately does not invalidate
// the object: it is full-canvas, so a scene with 40 roads used to submit 40
// full-screen invalidations before the map itself was even marked dirty. The
// caller batches the frame into one map invalidation (or a dirty-area union
// during motion).
void set_map_polyline_points(lv_obj_t *object, const lv_point_precise_t *points,
                            std::uint16_t count) {
    auto *polyline = static_cast<MapPolyline *>(lv_obj_get_user_data(object));
    polyline->points = points;
    polyline->count = count;
}

void invalidate_map() {
    if(ui.nav_map != nullptr) lv_obj_invalidate(ui.nav_map);
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
    // The fill is a child of nav_map; the caller's map invalidation already
    // repaints the spans it covers. Invalidating the full-canvas fill object
    // here doubled the work of every building frame.
    return pixels_changed || rebind_lines;
}

void route_motion_tick(lv_timer_t *) {
    if(ui.nav_route_target_count < 2 || ui.nav_route_point_count < 2) return;
    // 0.39 at 25 ms has approximately the same smoothing time constant as
    // the old 0.56 at 40 ms, but supplies smaller and more frequent steps.
    // Screen coordinates need subpixel precision, not geographic doubles.
    // Float uses the ESP32-S3 FPU for every point in this 40 Hz hot loop.
    const float blend = ui.reduce_motion ? 1.0F : 0.39F;

    // A moving line dirties its whole span, not just the moved vertices: when
    // one end of a segment shifts, the line between them changes along its
    // length. Accumulate a union of old/new spine and its span bounds, then
    // invalidate only that area. A pure map pan covers the circle and behaves
    // like the old full-map invalidation; a route or a single street changing
    // no longer repaints the whole panel.
    bool dirty = false;
    int32_t dirty_x1 = 0;
    int32_t dirty_y1 = 0;
    int32_t dirty_x2 = 0;
    int32_t dirty_y2 = 0;
    const auto include = [&](float x, float y) {
        const int32_t px_x = static_cast<int32_t>(std::lround(x));
        const int32_t px_y = static_cast<int32_t>(std::lround(y));
        if(!dirty) {
            dirty = true;
            dirty_x1 = dirty_x2 = px_x;
            dirty_y1 = dirty_y2 = px_y;
            return;
        }
        dirty_x1 = std::min(dirty_x1, px_x);
        dirty_y1 = std::min(dirty_y1, px_y);
        dirty_x2 = std::max(dirty_x2, px_x);
        dirty_y2 = std::max(dirty_y2, px_y);
    };
    const auto include_span = [&](const float *xs, const float *ys,
                                  std::uint8_t first, std::uint8_t count) {
        for(std::uint8_t i = 0; i < count; ++i) {
            include(xs[first + i], ys[first + i]);
        }
    };

    bool route_moved = false;
    for(std::uint8_t i = 0; i < ui.nav_route_point_count; ++i) {
        const float dx = ui.nav_route_target_x[i] - ui.nav_route_x[i];
        const float dy = ui.nav_route_target_y[i] - ui.nav_route_y[i];
        if(std::abs(dx) < 0.08F && std::abs(dy) < 0.08F) {
            ui.nav_route_x[i] = ui.nav_route_target_x[i];
            ui.nav_route_y[i] = ui.nav_route_target_y[i];
            continue;
        }
        include(ui.nav_route_x[i], ui.nav_route_y[i]);
        ui.nav_route_x[i] += dx * blend;
        ui.nav_route_y[i] += dy * blend;
        include(ui.nav_route_x[i], ui.nav_route_y[i]);
        route_moved = true;
    }
    if(route_moved) {
        include_span(ui.nav_route_x, ui.nav_route_y, 0,
                     ui.nav_route_point_count);
    }

    bool roads_moved = false;
    for(std::uint8_t line = 0; line < ui.nav_road_polyline_count; ++line) {
        const moto_ui_polyline_span_t span = ui.nav_road_spans[line];
        bool line_moved = false;
        for(std::uint8_t j = 0; j < span.point_count; ++j) {
            const std::uint8_t i = span.first_point_index + j;
            const float dx = ui.nav_road_target_x[i] - ui.nav_road_x[i];
            const float dy = ui.nav_road_target_y[i] - ui.nav_road_y[i];
            if(std::abs(dx) < 0.08F && std::abs(dy) < 0.08F) {
                ui.nav_road_x[i] = ui.nav_road_target_x[i];
                ui.nav_road_y[i] = ui.nav_road_target_y[i];
                continue;
            }
            include(ui.nav_road_x[i], ui.nav_road_y[i]);
            ui.nav_road_x[i] += dx * blend;
            ui.nav_road_y[i] += dy * blend;
            include(ui.nav_road_x[i], ui.nav_road_y[i]);
            line_moved = true;
        }
        if(line_moved) {
            include_span(ui.nav_road_x, ui.nav_road_y,
                         span.first_point_index, span.point_count);
            roads_moved = true;
        }
    }

    bool buildings_moved = false;
    for(std::uint8_t footprint = 0;
        footprint < ui.nav_building_footprint_count; ++footprint) {
        const moto_ui_building_span_t span =
            ui.nav_building_spans[footprint];
        bool footprint_moved = false;
        for(std::uint8_t j = 0; j < span.point_count; ++j) {
            const std::uint8_t i = span.first_point_index + j;
            const float dx = ui.nav_building_target_x[i] - ui.nav_building_x[i];
            const float dy = ui.nav_building_target_y[i] - ui.nav_building_y[i];
            if(std::abs(dx) < 0.08F && std::abs(dy) < 0.08F) {
                ui.nav_building_x[i] = ui.nav_building_target_x[i];
                ui.nav_building_y[i] = ui.nav_building_target_y[i];
                continue;
            }
            include(ui.nav_building_x[i], ui.nav_building_y[i]);
            ui.nav_building_x[i] += dx * blend;
            ui.nav_building_y[i] += dy * blend;
            include(ui.nav_building_x[i], ui.nav_building_y[i]);
            footprint_moved = true;
        }
        if(footprint_moved) {
            include_span(ui.nav_building_x, ui.nav_building_y,
                         span.first_point_index, span.point_count);
            buildings_moved = true;
        }
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
    if(!pixels_changed) return;
    if(!dirty) {
        invalidate_map();
        return;
    }
    constexpr int32_t kPadding = 24;
    lv_area_t map_area;
    lv_obj_get_coords(ui.nav_map, &map_area);
    lv_area_t area = {
        map_area.x1 + dirty_x1 - kPadding,
        map_area.y1 + dirty_y1 - kPadding,
        map_area.x1 + dirty_x2 + kPadding,
        map_area.y1 + dirty_y2 + kPadding,
    };
    lv_obj_invalidate_area(ui.nav_map, &area);
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
                const std::uint8_t road_class =
                    ui.nav_road_spans[i].road_class;
                // The class, not the revision, chooses the stroke. A new
                // window usually keeps the same classes, and restyling a
                // full-canvas line object would invalidate the whole map.
                if(ui.nav_road_applied_class[i] != road_class) {
                    const RoadStroke stroke =
                        road_stroke(ui.palette, road_class);
                    lv_obj_set_style_line_width(ui.nav_roads[i],
                                                px(stroke.width_design), 0);
                    lv_obj_set_style_line_color(ui.nav_roads[i], stroke.color,
                                                0);
                    ui.nav_road_applied_class[i] = road_class;
                }
                if(lv_obj_has_flag(ui.nav_roads[i], LV_OBJ_FLAG_HIDDEN)) {
                    lv_obj_remove_flag(ui.nav_roads[i], LV_OBJ_FLAG_HIDDEN);
                }
            } else if(!lv_obj_has_flag(ui.nav_roads[i],
                                       LV_OBJ_FLAG_HIDDEN)) {
                lv_obj_add_flag(ui.nav_roads[i], LV_OBJ_FLAG_HIDDEN);
            }
        }
    }
    if(snap) {
        apply_road_geometry_frame();
        invalidate_map();
    }
}


// The fill layer and the outline lines are one layer to the rider. Toggling
// only one of them leaves either a hollow ghost or a solid block with no edge,
// so both are driven from this single place. Lives in the anonymous namespace
// because the public setter only forwards to it.
void apply_building_visibility() {
    // Repeat calls arrive on every scene revision. Only touch a flag that is
    // actually different: a redundant remove_flag/add_flag invalidates the
    // full-canvas line and turns a no-op into a map repaint.
    if(ui.nav_building_fill != nullptr) {
        const bool hidden =
            lv_obj_has_flag(ui.nav_building_fill, LV_OBJ_FLAG_HIDDEN);
        if(ui.show_buildings && hidden) {
            lv_obj_remove_flag(ui.nav_building_fill, LV_OBJ_FLAG_HIDDEN);
        } else if(!ui.show_buildings && !hidden) {
            lv_obj_add_flag(ui.nav_building_fill, LV_OBJ_FLAG_HIDDEN);
        }
    }
    for(std::uint8_t i = 0; i < MOTO_UI_BUILDING_FOOTPRINT_CAPACITY; ++i) {
        if(ui.nav_buildings[i] == nullptr) continue;
        // A slot past footprint_count is unused, not merely off-screen.
        const bool want_hidden =
            !ui.show_buildings || i >= ui.nav_building_footprint_count;
        const bool hidden =
            lv_obj_has_flag(ui.nav_buildings[i], LV_OBJ_FLAG_HIDDEN);
        if(want_hidden == hidden) continue;
        if(want_hidden) {
            lv_obj_add_flag(ui.nav_buildings[i], LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_remove_flag(ui.nav_buildings[i], LV_OBJ_FLAG_HIDDEN);
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
        if(had_buildings) {
            invalidate_map();
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
                const std::uint8_t building_class =
                    ui.nav_building_spans[i].building_class;
                if(ui.nav_building_applied_class[i] != building_class) {
                    const bool landmark = building_class == 1U;
                    lv_obj_set_style_line_color(
                        ui.nav_buildings[i],
                        landmark ? ui.palette.building_landmark
                                 : ui.palette.building, 0);
                    lv_obj_set_style_line_width(
                        ui.nav_buildings[i], px(landmark ? 2 : 1), 0);
                    ui.nav_building_applied_class[i] = building_class;
                }
            }
        }
        apply_building_visibility();
    }
    if(snap) {
        apply_building_geometry_frame();
        invalidate_map();
    }
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
    if(snap) {
        apply_route_geometry_frame();
        invalidate_map();
    }
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

// The link cover's loading bar keeps the boot bar's cadence: eight hard
// steps, held full for a beat, then it starts over. It loops because these
// states have no measurable progress; it claims activity, not a percentage.
constexpr int kLinkBarSteps = 8;
constexpr std::uint32_t kLinkBarStepMs = 75;
constexpr int kLinkBarHoldSteps = 4;
constexpr std::uint32_t kLinkCursorBlinkMs = 500;
// Bar track width (the boot bar's) and the LCD message's left inset; layout
// constants, because the first content is applied before LVGL lays out.
constexpr int kLinkBarWidth = px(140);
constexpr int kLinkLcdTextX = 10;
// The LCD's left edge in its panel (the pager is centred in a 402 px band).
constexpr int kLinkLcdX = MOTO_UI_CANVAS_WIDTH / 2 - 284 / 2 + 8;

void lifecycle_tick(lv_timer_t *);

void apply_lifecycle_content(LifecycleVisual visual) {
    ui.lifecycle_visual = visual;
    const char *kicker = "PHONE LINK";
    const char *title = "";
    const char *message = "";
    // Every state is two lines: the headline is the poster, and a layout that
    // jumps between one and two lines would move the composition on each
    // transition.
    switch(visual) {
        case LifecycleVisual::PhoneOffline:
            title = "CONNECT\nPHONE";
            message = "请打开手机应用";
            break;
        case LifecycleVisual::PhoneConnecting:
            title = "LINKING\nPHONE";
            message = "正在建立连接";
            break;
        case LifecycleVisual::Connected:
            title = "PHONE\nLINKED";
            message = "连接成功";
            break;
        case LifecycleVisual::Ready:
            kicker = "READY";
            title = "READY TO\nRIDE";
            message = "请在手机选择目的地";
            break;
        case LifecycleVisual::Planning:
            kicker = "ROUTE";
            title = "BUILDING\nROUTE";
            message = "正在规划路线";
            break;
        case LifecycleVisual::Hidden:
            break;
    }
    lv_label_set_text(ui.nav_lifecycle_kicker, kicker);
    lv_label_set_text(ui.nav_lifecycle_title, title);
    lv_label_set_text(ui.nav_lifecycle_subtitle, message);
    ui.lifecycle_message = message;
    // The one moment the cover celebrates: the headline turns neon pink by
    // night (ink by day, where neon would not read on newsprint).
    lv_obj_set_style_text_color(ui.nav_lifecycle_title,
                                visual == LifecycleVisual::Connected
                                    ? ui.palette.hud
                                    : ui.palette.ink,
                                0);
    // The cursor sits after the message, so it moves with each state's text.
    lv_point_t size;
    lv_text_get_size(&size, message, &moto_font_text_24, 0, 0, LV_COORD_MAX,
                     LV_TEXT_FLAG_NONE);
    lv_obj_set_x(ui.lifecycle_cursor, kLinkLcdX + kLinkLcdTextX + size.x + 3);
    ui.lifecycle_started_ms = lv_tick_get();
    // Force the bar to repaint for the new state.
    ui.lifecycle_bar_steps = -2;
    lifecycle_tick(nullptr);
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

// The cover's only motion, both cheap: the pager cursor blinks while the
// cover waits on the rider (open the app, pick a destination), and the
// loading bar steps while it waits on the system (linking, routing). Each
// repaints a few hundred pixels, and only when its state actually changes.
void lifecycle_tick(lv_timer_t *) {
    if(ui.nav_lifecycle == nullptr || ui.lifecycle_cursor == nullptr ||
       ui.lifecycle_bar == nullptr) {
        return;
    }
    const LifecycleVisual visual = ui.lifecycle_visual;
    const std::uint32_t elapsed = lv_tick_elaps(ui.lifecycle_started_ms);
    const bool waits_on_rider = visual == LifecycleVisual::PhoneOffline ||
                                visual == LifecycleVisual::Ready;
    const bool waits_on_system = visual == LifecycleVisual::PhoneConnecting ||
                                 visual == LifecycleVisual::Planning;

    const bool cursor =
        waits_on_rider &&
        (ui.reduce_motion || (elapsed / kLinkCursorBlinkMs) % 2U == 0U);
    if(cursor == lv_obj_has_flag(ui.lifecycle_cursor, LV_OBJ_FLAG_HIDDEN)) {
        if(cursor) {
            lv_obj_remove_flag(ui.lifecycle_cursor, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(ui.lifecycle_cursor, LV_OBJ_FLAG_HIDDEN);
        }
    }

    int steps = -1;
    if(waits_on_system) {
        steps = ui.reduce_motion
                    ? kLinkBarSteps
                    : std::min<int>(
                          kLinkBarSteps,
                          static_cast<int>((elapsed / kLinkBarStepMs) %
                                           (kLinkBarSteps + kLinkBarHoldSteps)));
    }
    if(steps == ui.lifecycle_bar_steps) return;
    ui.lifecycle_bar_steps = steps;
    if(steps < 0) {
        lv_obj_add_flag(ui.lifecycle_bar, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(ui.lifecycle_bar_fill, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    lv_obj_remove_flag(ui.lifecycle_bar, LV_OBJ_FLAG_HIDDEN);
    // A zero-width object still draws nothing, but keep it hidden at zero
    // so the track reads empty, not as a one-pixel stub.
    if(steps == 0) {
        lv_obj_add_flag(ui.lifecycle_bar_fill, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_remove_flag(ui.lifecycle_bar_fill, LV_OBJ_FLAG_HIDDEN);
    }
    lv_obj_set_width(ui.lifecycle_bar_fill,
                     (kLinkBarWidth - 6) * steps / kLinkBarSteps);
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
    lv_obj_align(ui.nav_distance, LV_ALIGN_TOP_LEFT, px(175), px(212));
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
// The phone shows these runs (PerformanceRun, 0x19). Only transitions are
// reported; the phone ticks its own display while a run is in progress.
void perf_report(std::uint32_t elapsed_ms) {
    if(ui.perf_callback == nullptr) return;
    moto_ui_performance_run_t run{};
    switch(ui.perf_state) {
        case Ui::PerfState::Armed: run.state = MOTO_UI_RUN_ARMED; break;
        case Ui::PerfState::Running: run.state = MOTO_UI_RUN_RUNNING; break;
        case Ui::PerfState::Launched: run.state = MOTO_UI_RUN_FINISHED; break;
    }
    run.target_kph = kPerfTargetKph;
    run.run_id = ui.perf_run_id;
    run.elapsed_ms = run.state == MOTO_UI_RUN_ARMED ? 0 : elapsed_ms;
    run.best_ms = ui.perf_best_ms;
    ui.perf_callback(&run, ui.perf_context);
}

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
                ++ui.perf_run_id;
                perf_set_elapsed(0);
                perf_report(0);
            }
            break;
        case Ui::PerfState::Running:
            if(speed >= kPerfTargetKph) {
                const std::uint32_t elapsed_ms =
                    lv_tick_elaps(ui.perf_run_start_ms);
                const std::uint32_t centis = elapsed_ms / 10U;
                if(ui.perf_best_ms == 0 || elapsed_ms < ui.perf_best_ms) {
                    ui.perf_best_ms = elapsed_ms;
                }
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
                perf_report(elapsed_ms);
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
                    perf_report(0);
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

// The Lean page is a race graphic, not a flight instrument: the bike leans
// and the world holds still. A segmented arc across the top is the scale
// (the 0-100 page's shift strip bent round the rim) and fills as the bike
// leans, the unsigned numeral is the poster, and Liberty City sits under it.
//
// Every per-frame dirty area stays inside kLeanDirtyRadius, where the rim
// draw is culled (clip_inside_radius). On the board each dirty area costs a
// tree walk and a QSPI flush whatever its size, and one that reached the rim
// re-stroked band, casing and protractor: that was 16 ms of a 53 ms frame.
constexpr int kLeanDirtyRadius = 192;
// The numeral sits in the middle band, clear of the skyline's tallest spire.
constexpr int kLeanHeroLiftPx = -10;

// Arc: twelve 5-degree segments a side, 0 to 60 degrees, filled to the
// lean in quarter degrees, so the scale moves every frame and not in 5-degree
// steps. Lit is HUD orange, past 45 degrees HUD red; the segment holding each
// side's peak keeps its unlit part in neon pink.
// Radii keep the box of two neighbouring segments inside kLeanDirtyRadius
// (LVGL joins neighbours into one area): on a diagonal that box's corner
// sits about 8 % outside the arc.
constexpr int kLeanArcInner = 156;
constexpr int kLeanArcOuter = 177;
constexpr int kLeanArcSegments = 12;
constexpr double kLeanArcStepDeg = 5.0;
constexpr double kLeanArcGapDeg = 0.7;
constexpr int kLeanArcHotFrom = 10;  // segments 10..12 are past 45 degrees
constexpr int kLeanArcQuarters = 20;  // quarter degrees per segment

// Fill for a lean of `degrees` to one side, in quarter degrees.
int lean_arc_fill(float degrees) {
    if(!(degrees > 0.0F)) return 0;
    const int quarters = static_cast<int>(std::lround(degrees * 4.0F));
    return std::min(quarters, kLeanArcSegments * kLeanArcQuarters);
}

// The segment (1-based) that holds a peak of `degrees`, 0 for none.
int lean_arc_peak(float degrees) {
    const int quarters = lean_arc_fill(degrees);
    return (quarters + kLeanArcQuarters - 1) / kLeanArcQuarters;
}

// Angles from twelve o'clock, positive clockwise, of segment `index`
// (1-based) on `side` (+1 right, -1 left).
void lean_arc_angles(int side, int index, double &from, double &to) {
    const double a = (index - 1) * kLeanArcStepDeg + kLeanArcGapDeg / 2.0;
    const double b = index * kLeanArcStepDeg - kLeanArcGapDeg / 2.0;
    from = side > 0 ? a : -b;
    to = side > 0 ? b : -a;
}

lv_area_t lean_arc_segment_area(int side, int index) {
    double from, to;
    lean_arc_angles(side, index, from, to);
    const double cx = MOTO_UI_CANVAS_WIDTH / 2.0;
    const double cy = MOTO_UI_CANVAS_HEIGHT / 2.0;
    double x1 = 1e9, y1 = 1e9, x2 = -1e9, y2 = -1e9;
    for(double deg : {from, to, (from + to) / 2.0}) {
        const double rad = deg * kPi / 180.0;
        for(int r : {kLeanArcInner, kLeanArcOuter}) {
            const double x = cx + std::sin(rad) * r;
            const double y = cy - std::cos(rad) * r;
            x1 = std::min(x1, x);
            y1 = std::min(y1, y);
            x2 = std::max(x2, x);
            y2 = std::max(y2, y);
        }
    }
    return {static_cast<int32_t>(std::floor(x1)) - 1,
            static_cast<int32_t>(std::floor(y1)) - 1,
            static_cast<int32_t>(std::ceil(x2)) + 1,
            static_cast<int32_t>(std::ceil(y2)) + 1};
}

void invalidate_box(double x1, double y1, double x2, double y2, double pad) {
    const int32_t cx = MOTO_UI_CANVAS_WIDTH / 2;
    const int32_t cy = MOTO_UI_CANVAS_HEIGHT / 2;
    lv_area_t area{
        std::max(static_cast<int32_t>(std::floor(x1 - pad)),
                 cx - kLeanDirtyRadius),
        std::max(static_cast<int32_t>(std::floor(y1 - pad)),
                 cy - kLeanDirtyRadius),
        std::min(static_cast<int32_t>(std::ceil(x2 + pad)),
                 cx + kLeanDirtyRadius),
        std::min(static_cast<int32_t>(std::ceil(y2 + pad)),
                 cy + kLeanDirtyRadius)};
    if(area.x1 <= area.x2 && area.y1 <= area.y2) {
        lv_obj_invalidate_area(ui.lean_scene, &area);
    }
}

LeanArcState lean_arc_state(float lean, float peak_left, float peak_right) {
    return {{lean_arc_fill(-lean), lean_arc_fill(lean)},
            {lean_arc_peak(peak_left), lean_arc_peak(peak_right)}};
}

// One area per changed segment: a box around a run of segments reaches far
// past the arc on a diagonal, and LVGL joins neighbours only when that is
// cheaper than drawing them apart. A steady lean changes one segment a frame.
void invalidate_arc_change(const LeanArcState &from, const LeanArcState &to) {
    for(int s = 0; s < 2; ++s) {
        const int side = s == 0 ? -1 : 1;
        const auto dirty = [&](int index) {
            if(index < 1 || index > kLeanArcSegments) return;
            const lv_area_t a = lean_arc_segment_area(side, index);
            invalidate_box(a.x1, a.y1, a.x2, a.y2, 0.0);
        };
        if(from.lit[s] != to.lit[s]) {
            const int lo = std::min(from.lit[s], to.lit[s]);
            const int hi = std::max(from.lit[s], to.lit[s]);
            for(int i = lo / kLeanArcQuarters + 1;
                i <= (hi + kLeanArcQuarters - 1) / kLeanArcQuarters; ++i) {
                dirty(i);
            }
        }
        if(from.peak[s] != to.peak[s]) {
            dirty(from.peak[s]);
            dirty(to.peak[s]);
        }
    }
}

// The hero's box in canvas coordinates: the label's line box, centred and
// lifted. The scene draws it there and dirties exactly this box when it
// changes.
lv_area_t lean_hero_area(const char *text) {
    lv_point_t size;
    lv_text_get_size(&size, text, &moto_font_hero_176, 0, 0, LV_COORD_MAX,
                     LV_TEXT_FLAG_NONE);
    lv_area_t area;
    area.x1 = static_cast<int32_t>(
        std::lround(MOTO_UI_CANVAS_WIDTH / 2.0 - size.x / 2.0));
    area.y1 = static_cast<int32_t>(std::lround(
        MOTO_UI_CANVAS_HEIGHT / 2.0 + kLeanHeroLiftPx - size.y / 2.0));
    area.x2 = area.x1 + size.x - 1;
    area.y2 = area.y1 + size.y - 1;
    return area;
}

// The hero is painted by the scene, so a new string has to dirty its own box:
// the horizon strips only cross the bottom of it.
void invalidate_lean_hero(const char *text) {
    if(ui.lean_scene == nullptr || text == nullptr || text[0] == '\0') return;
    // LVGL strokes glyph outlines only for vector fonts; on these bitmap
    // faces the halo is never drawn, so antialiasing is all the box needs.
    lv_area_t area = lean_hero_area(text);
    lv_area_increase(&area, 2, 2);
    lv_obj_invalidate_area(ui.lean_scene, &area);
}

void update_lean_text(bool has, float lean) {
    char text[24];
    invalidate_lean_hero(ui.lean_text);
    if(has) {
        // Unsigned (DESIGN_V2 §6): the side is the bike's tilt, the lit side
        // of the arc and the L/R plates.
        const double magnitude = std::fabs(static_cast<double>(lean));
        std::snprintf(ui.lean_text, sizeof(ui.lean_text), "%0.0f°",
                      magnitude < 1.5 ? 0.0 : magnitude);
        invalidate_lean_hero(ui.lean_text);
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
        if((g_debug_hidden & MOTO_UI_DBG_HIDE_SCENE) == 0) {
            lv_obj_remove_flag(ui.lean_scene, LV_OBJ_FLAG_HIDDEN);
        }
    } else {
        // A 176 px hyphen renders as two solid bricks, not a dash. The NO IMU
        // plate carries the state; leave the poster's centre empty.
        ui.lean_text[0] = '\0';
        lv_label_set_text(ui.lean_max_l, "L --");
        lv_label_set_text(ui.lean_max_r, "R --");
        lv_obj_remove_flag(ui.lean_state, LV_OBJ_FLAG_HIDDEN);
    }
}

void schedule_lean_motion();

void update_lean(const moto_ui_state_t *state) {
    if(ui.lean_scene == nullptr) return;
    const bool has = state->has_attitude != 0;
    const float lean = has ? static_cast<float>(state->lean_deg) : 0.0F;

    // Peaks are data, not pixels: keep them at the sample rate so a short
    // spike cannot fall between two display frames.
    if(has) {
        const float left = std::max(ui.lean_peak_left, -lean);
        const float right = std::max(ui.lean_peak_right, lean);
        // A new peak can move the arc's held segment without moving the
        // bike far enough to count as motion.
        if(left != ui.lean_peak_left || right != ui.lean_peak_right) {
            ui.lean_peak_left = left;
            ui.lean_peak_right = right;
            schedule_lean_motion();
        }
    }

    // DESIGN_V2 §2.7: "graphics at frame rate, numbers at the table rate".
    // The bike and the arc follow every sample; only the numerals are a
    // 10 Hz / 1 deg / 0.6 deg-hysteresis display.
    const std::uint32_t now = lv_tick_get();
    const bool has_changed = ui.lean_has != has;
    // A quarter degree is under half a pixel at the helmet, so sensor noise
    // below it never costs a frame.
    const bool scene_moved = std::fabs(ui.lean_deg - lean) >= 0.25F;
    const bool text_due =
        static_cast<std::uint32_t>(now - ui.lean_text_tick_ms) >= 100U &&
        std::fabs(ui.lean_text_deg - lean) > 0.6F;
    const bool text_update = has_changed || ui.lean_scene_dirty || text_due;
    if(!(text_update || (has && scene_moved))) return;

    ui.lean_has = has;
    ui.lean_deg = lean;
    ui.lean_scene_dirty = false;

    if(text_update) {
        ui.lean_text_tick_ms = now;
        ui.lean_text_deg = lean;
        update_lean_text(has, lean);
    }

    schedule_lean_motion();
}

// The sensor delivers faster than the panel refreshes, so the motion is
// dirtied once per refresh, from what was last drawn to the newest sample.
// Dirtying every sample also repainted positions that were never shown, and
// each extra area costs a tree walk and a flush.
void schedule_lean_motion() {
    if(ui.lean_motion_pending) return;
    ui.lean_motion_pending = true;
    // LVGL pauses its refresh timer while nothing is dirty; nothing is dirty
    // yet, so wake it for the hook below.
    lv_timer_t *refresh = ui.lean_display != nullptr
                              ? lv_display_get_refr_timer(ui.lean_display)
                              : nullptr;
    if(refresh != nullptr) lv_timer_resume(refresh);
}

// Display LV_EVENT_REFR_START: runs before LVGL joins and renders the dirty
// areas, so the motion invalidated here is drawn in this same refresh.
void lean_refresh_start(lv_event_t *) {
    if(!ui.lean_motion_pending || ui.lean_scene == nullptr) return;
    ui.lean_motion_pending = false;
    const float lean = ui.lean_has ? ui.lean_deg : 0.0F;
    const LeanArcState arc =
        lean_arc_state(lean, ui.lean_peak_left, ui.lean_peak_right);
    invalidate_arc_change(ui.lean_drawn_arc, arc);
    ui.lean_drawn_deg = lean;
    ui.lean_drawn_arc = arc;
}

// The marquee's speed, in design pixels per second. A 40 px face at 64 px/s
// crosses the 288 px plate in about four seconds: fast enough to read a long
// title, slow enough that the glyphs are not a blur on a handlebar.
constexpr int kMusicTitleScrollPxPerSecond = 64;

void update_music_title_scroll() {
    if(ui.music_title == nullptr) return;
    const lv_font_t *font =
        lv_obj_get_style_text_font(ui.music_title, LV_PART_MAIN);
    lv_point_t text_size;
    lv_text_get_size(&text_size, ui.music_title_text, font, 0, 0, LV_COORD_MAX,
                     LV_TEXT_FLAG_NONE);
    const int32_t window = lv_obj_get_width(ui.music_title);
    if(window <= 0 || text_size.x <= window) {
        // Fits: LVGL deletes the scroll animation by itself, and a zero
        // duration means "never start one".
        lv_obj_set_style_anim_duration(ui.music_title, 0, 0);
        return;
    }
    // LVGL adds a gap of a few spaces and scrolls the text past the window, so
    // the duration covers both to keep the speed constant.
    const int32_t distance = text_size.x + window;
    const auto duration_ms = static_cast<std::uint32_t>(
        (static_cast<std::int64_t>(distance) * 1000) /
        kMusicTitleScrollPxPerSecond);
    lv_obj_set_style_anim_duration(ui.music_title, duration_ms, 0);
}

void update_music_view() {
    if(ui.music_source == nullptr) return;
    lv_label_set_text(ui.music_source,
                      ui.music.connected ? ui.music_source_text
                                         : "NO SIGNAL");
    lv_label_set_text(ui.music_title, ui.music_title_text);
    lv_label_set_text(ui.music_artist, ui.music_artist_text);
    update_music_title_scroll();
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
    for(int i = 0; i < kPagerCount; ++i) {
        ui.page_dots[i] = lv_obj_create(ui.screen);
        lv_obj_remove_style_all(ui.page_dots[i]);
        lv_obj_set_size(ui.page_dots[i], px(5), px(5));
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
    dsc.color = ui.palette.on_accent;
    dsc.opa = LV_OPA_COVER;
    dsc.rounded = 0;
    // The band is the progress bar: the band's own ink fills it clockwise
    // from twelve o'clock, the same grammar the speed page uses.
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
    ui.nav_road_casing = lv_obj_create(ui.nav_map);
    lv_obj_remove_style_all(ui.nav_road_casing);
    lv_obj_remove_flag(ui.nav_road_casing, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(ui.nav_road_casing, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(ui.nav_road_casing, MOTO_UI_CANVAS_WIDTH, MOTO_UI_CANVAS_HEIGHT);
    lv_obj_set_pos(ui.nav_road_casing, 0, 0);
    lv_obj_add_event_cb(ui.nav_road_casing, draw_road_casings,
                        LV_EVENT_DRAW_MAIN, nullptr);
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
    // Every glyph stays within 143 design px of the centre: inside the ring
    // the north badge rides, so the two can never meet. At (29,220) a turn's
    // stem ran out past the glass and under the badge.
    lv_obj_set_pos(ui.nav_maneuver, px(50), px(172));
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
    // Not themed: the badge rides the black casing in both themes, so a
    // themed ink letter vanished by day (black on black). Cream face, black
    // letter and edge, like the speed-limit sign.
    lv_obj_set_style_bg_color(ui.nav_north, kLimitFace, 0);
    lv_obj_set_style_bg_opa(ui.nav_north, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(ui.nav_north, kOutlineBlack, 0);
    lv_obj_set_style_border_width(ui.nav_north, px(2), 0);
    lv_obj_set_style_border_opa(ui.nav_north, LV_OPA_COVER, 0);
    lv_obj_clear_flag(ui.nav_north, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_name_static(ui.nav_north, "moto-compass-north");
    lv_obj_t *north_glyph =
        make_label(ui.nav_north, &moto_font_display_18, kOutlineBlack, "N");
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
    lv_obj_set_pos(ui.nav_home, px(206), px(262));
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

    // Connection lifecycle overlay: the waiting screens are a GTA III cover,
    // the same box art the boot sequence cuts through, so boot, link and ride
    // read as one sequence. Framed panels inside the cover's gutter (white by
    // night, black by day): a pager that carries the instruction, the
    // headline, and Liberty City at dusk. The glass is round, so every panel
    // is clipped to the inscribed circle and the type stays in its widest
    // band.
    ui.nav_lifecycle = make_layer(page);
    lv_obj_set_style_radius(ui.nav_lifecycle, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_clip_corner(ui.nav_lifecycle, true, 0);
    lv_obj_set_style_bg_color(ui.nav_lifecycle, ui.palette.frame, 0);
    lv_obj_set_style_bg_opa(ui.nav_lifecycle, LV_OPA_COVER, 0);
    lv_obj_add_flag(ui.nav_lifecycle, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_add_flag(ui.nav_lifecycle, LV_OBJ_FLAG_GESTURE_BUBBLE);

    const auto box = [](lv_obj_t *parent, int x, int y, int w, int h,
                        lv_color_t color) {
        lv_obj_t *object = lv_obj_create(parent);
        lv_obj_remove_style_all(object);
        lv_obj_clear_flag(object, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_clear_flag(object, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_pos(object, x, y);
        lv_obj_set_size(object, w, h);
        lv_obj_set_style_bg_color(object, color, 0);
        lv_obj_set_style_bg_opa(object, LV_OPA_COVER, 0);
        return object;
    };

    // No nested clip_corner: LVGL renders a clipped object's children into
    // ARGB layers, and a second one inside the layer's own clip overflowed
    // the 8 KiB render task. The panels are plain rectangles on a gutter
    // disc; two rings drawn over them cut them to the circle instead: the
    // 6 px gutter that frames the art, then the bezel out to the glass.
    constexpr int kGap = 6;
    constexpr int kInner = kDialRadius - kGap;
    constexpr int kInnerX = MOTO_UI_CANVAS_WIDTH / 2 - kInner;
    constexpr int kInnerY = MOTO_UI_CANVAS_HEIGHT / 2 - kInner;
    lv_obj_t *gutter = box(ui.nav_lifecycle,
                           MOTO_UI_CANVAS_WIDTH / 2 - kDialRadius,
                           MOTO_UI_CANVAS_HEIGHT / 2 - kDialRadius,
                           kDialRadius * 2, kDialRadius * 2, ui.palette.accent);
    lv_obj_set_style_radius(gutter, LV_RADIUS_CIRCLE, 0);
    ui.lifecycle_cover[0] = gutter;

    // Three bands, in canvas rows: the pager's, the headline's and the
    // city's. The headline band is centred on the glass, where it is widest.
    constexpr int kPagerBottom = 162;
    constexpr int kTitleBottom = 300;
    constexpr int kFull = kInner * 2;
    lv_obj_t *pager_panel = box(ui.nav_lifecycle, kInnerX, kInnerY, kFull,
                                kPagerBottom - kInnerY, ui.palette.background);
    lv_obj_t *title_panel =
        box(ui.nav_lifecycle, kInnerX, kPagerBottom + kGap, kFull,
            kTitleBottom - kPagerBottom - kGap, ui.palette.ground);
    constexpr int kCityY = kTitleBottom + kGap;
    lv_obj_t *city_panel = box(ui.nav_lifecycle, kInnerX, kCityY, kFull,
                               kInnerY + kFull - kCityY,
                               dusk_palette()->ground);
    ui.lifecycle_panels[0] = pager_panel;
    ui.lifecycle_panels[1] = title_panel;
    ui.lifecycle_panels[2] = city_panel;

    // GTA III's pager: the HUD's message device, a black handset with a lit
    // green LCD. Here it carries the one instruction the rider needs. It is
    // an object, not paper, so it keeps its colours in both themes.
    //
    // Everything on the cover is a direct child of the overlay, in canvas
    // coordinates. The overlay already renders through clip_corner layers
    // inside an 8 KiB render task; each level of nesting below it costs
    // that task stack, and a deeper first draft left 1.4 KiB.
    lv_obj_t *const cover_layer = ui.nav_lifecycle;
    constexpr lv_color_t kPagerBody = LV_COLOR_MAKE(0x06, 0x06, 0x06);
    constexpr lv_color_t kPagerBevel = LV_COLOR_MAKE(0x5A, 0x5A, 0x56);
    constexpr lv_color_t kPagerLcd = LV_COLOR_MAKE(0xB4, 0xC4, 0x7A);
    constexpr lv_color_t kPagerLcdEdge = LV_COLOR_MAKE(0x6E, 0x7E, 0x44);
    constexpr lv_color_t kPagerInk = LV_COLOR_MAKE(0x1B, 0x24, 0x0E);
    constexpr int kPagerW = 284;
    constexpr int kPagerH = 60;
    constexpr int kPagerX = MOTO_UI_CANVAS_WIDTH / 2 - kPagerW / 2;
    constexpr int kPagerY = 92;  // the first row where the body fits the glass
    lv_obj_t *pager = box(cover_layer, kPagerX, kPagerY, kPagerW, kPagerH,
                          kPagerBody);
    lv_obj_set_style_radius(pager, 10, 0);
    lv_obj_set_style_border_color(pager, kPagerBevel, 0);
    lv_obj_set_style_border_width(pager, 2, 0);
    // Nine CJK glyphs, the inset and the cursor fit; the keys take the rest.
    constexpr int kLcdW = 250;
    constexpr int kLcdH = 44;
    constexpr int kLcdX = kPagerX + 8;
    static_assert(kLcdX == kLinkLcdX, "the cursor is placed from kLinkLcdX");
    constexpr int kLcdY = kPagerY + (kPagerH - kLcdH) / 2;
    lv_obj_t *lcd = box(cover_layer, kLcdX, kLcdY, kLcdW, kLcdH, kPagerLcd);
    lv_obj_set_style_radius(lcd, 3, 0);
    lv_obj_set_style_border_color(lcd, kPagerLcdEdge, 0);
    lv_obj_set_style_border_width(lcd, 2, 0);
    for(int i = 0; i < 2; ++i) {
        lv_obj_t *key = box(cover_layer, kLcdX + kLcdW + 7,
                            kPagerY + 13 + i * 20, 12, 12,
                            LV_COLOR_MAKE(0x2A, 0x2A, 0x28));
        lv_obj_set_style_radius(key, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_border_color(key, kPagerBevel, 0);
        lv_obj_set_style_border_width(key, 1, 0);
    }
    // The LCD is its own light source: fixed ink, not a theme role.
    ui.nav_lifecycle_subtitle = make_label(cover_layer, &moto_font_text_24,
                                           kPagerInk, "请打开手机应用");
    lv_label_set_long_mode(ui.nav_lifecycle_subtitle, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_set_width(ui.nav_lifecycle_subtitle, kLcdW - 2 * kLinkLcdTextX);
    lv_obj_align_to(ui.nav_lifecycle_subtitle, lcd, LV_ALIGN_LEFT_MID,
                    kLinkLcdTextX, 0);
    ui.lifecycle_cursor =
        box(cover_layer, 0, kLcdY + (kLcdH - 22) / 2, 11, 22, kPagerInk);
    lv_obj_add_flag(ui.lifecycle_cursor, LV_OBJ_FLAG_HIDDEN);

    // The pager's label: which conversation this is (PHONE LINK, READY,
    // ROUTE), letter-spaced like the box art's credits.
    ui.nav_lifecycle_kicker = make_text(cover_layer, &moto_font_display_18,
                                        TextRole::OnSlab, "PHONE LINK");
    lv_obj_set_style_text_letter_space(ui.nav_lifecycle_kicker, px(5), 0);
    lv_obj_align(ui.nav_lifecycle_kicker, LV_ALIGN_TOP_MID, 0, 62);

    // The headline: 1.6x the largest firmware font, tight leading, tracking
    // near zero. Two lines in every state.
    ui.nav_lifecycle_title = make_text(cover_layer, &moto_font_display_72,
                                       TextRole::Ink, "CONNECT\nPHONE");
    lv_obj_set_width(ui.nav_lifecycle_title, px(300));
    lv_obj_set_style_text_align(ui.nav_lifecycle_title,
                                LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_letter_space(ui.nav_lifecycle_title, 0, 0);
    lv_obj_set_style_text_line_space(ui.nav_lifecycle_title, px(-14), 0);
    lv_obj_align_to(ui.nav_lifecycle_title, title_panel, LV_ALIGN_CENTER, 0,
                    0);

    // Liberty City at dusk, cropped to its panel.
    lv_obj_t *city = make_skyline(cover_layer, dusk_palette());
    lv_obj_add_flag(city, LV_OBJ_FLAG_USER_1);
    lv_obj_add_flag(city, LV_OBJ_FLAG_USER_2);
    lv_obj_set_pos(city, kInnerX, kCityY);
    lv_obj_set_size(city, kFull, kInnerY + kFull - kCityY);

    // The boot's loading bar, at the foot of the city; the fill is drawn over
    // the track as its sibling.
    constexpr int kBarX = MOTO_UI_CANVAS_WIDTH / 2 - kLinkBarWidth / 2;
    constexpr int kBarY = 386;
    ui.lifecycle_bar = box(cover_layer, kBarX, kBarY, kLinkBarWidth, px(12),
                           kOutlineBlack);
    lv_obj_set_style_border_color(ui.lifecycle_bar, kLimitFace, 0);
    lv_obj_set_style_border_width(ui.lifecycle_bar, 2, 0);
    ui.lifecycle_bar_fill = box(cover_layer, kBarX + 3, kBarY + 3, 0,
                                px(12) - 6, kHudPink);
    lv_obj_add_flag(ui.lifecycle_bar, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(ui.lifecycle_bar_fill, LV_OBJ_FLAG_HIDDEN);

    // The rings that cut the panels to the circle, over everything.
    const auto ring = [&](int radius, int width, lv_color_t color) {
        lv_obj_t *object = box(ui.nav_lifecycle,
                               MOTO_UI_CANVAS_WIDTH / 2 - radius,
                               MOTO_UI_CANVAS_HEIGHT / 2 - radius, radius * 2,
                               radius * 2, color);
        lv_obj_set_style_bg_opa(object, LV_OPA_TRANSP, 0);
        lv_obj_set_style_radius(object, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_border_color(object, color, 0);
        lv_obj_set_style_border_width(object, width, 0);
        return object;
    };
    ui.lifecycle_cover[1] = ring(kDialRadius, kGap, ui.palette.accent);
    ring(MOTO_UI_CANVAS_WIDTH / 2, MOTO_UI_CANVAS_WIDTH / 2 - kDialRadius,
         ui.palette.frame);

    ui.nav_lifecycle_timer = lv_timer_create(lifecycle_tick, 40, nullptr);
    ui.nav_success_timer = lv_timer_create(connection_success_timeout,
                                            kConnectionSuccessHoldMs,
                                            nullptr);
    lv_timer_pause(ui.nav_success_timer);
    apply_lifecycle_content(LifecycleVisual::PhoneOffline);
    ui.lifecycle_target = LifecycleVisual::PhoneOffline;
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
        draw_segment(layer, ui.palette.on_accent, major ? 3 : 2, LV_OPA_COVER,
                     pts, 2);
    }
    // Fixed index at twelve o'clock: a HUD-orange wedge, visible on the band
    // and on the casing in both themes.
    lv_draw_triangle_dsc_t tri;
    lv_draw_triangle_dsc_init(&tri);
    tri.opa = LV_OPA_COVER;
    tri.color = ui.palette.route;
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
    add_page_skyline(page);

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
    add_page_skyline(page);

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
    lv_obj_align(ui.perf_strip, LV_ALIGN_CENTER, 0, px(62));
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
    // Offsets are design units like every other HUD measurement: raw pixels
    // here put the slab on top of the strip. The row sits one gap under the
    // strip and stays inside the casing at its outer corners.
    lv_obj_t *best = make_slab(page, "BEST --.-", 132, 30, &ui.perf_best);
    lv_obj_align(best, LV_ALIGN_CENTER, -px(30), px(98));
    ui.perf_delta = make_text(page, &moto_font_display_24, TextRole::Ink, "");
    lv_obj_align_to(ui.perf_delta, best, LV_ALIGN_OUT_RIGHT_MID, px(8), 0);

    // The stopwatch runs on its own timer: state snapshots arrive at the BLE
    // cadence, but the clock has to move between them.
    ui.perf_timer_task = lv_timer_create(perf_tick, 50, nullptr);
}

void clear_lean_peaks() {
    ui.lean_peak_left = 0.0F;
    ui.lean_peak_right = 0.0F;
    if(ui.lean_max_l != nullptr) lv_label_set_text(ui.lean_max_l, "L --");
    if(ui.lean_max_r != nullptr) lv_label_set_text(ui.lean_max_r, "R --");
    // The held peak segments go dark with the numbers.
    schedule_lean_motion();
}

void draw_lean_progress(lv_event_t *event) {
    if(!ui.lean_progress_visible) return;
    lv_layer_t *layer = lv_event_get_layer(event);
    const float progress =
        std::min(1.0F, static_cast<float>(ui.lean_press_elapsed_ms) / 3000.0F);
    lv_draw_arc_dsc_t dsc;
    lv_draw_arc_dsc_init(&dsc);
    dsc.center = {MOTO_UI_CANVAS_WIDTH / 2, MOTO_UI_CANVAS_HEIGHT / 2};
    dsc.radius = 190;
    dsc.width = 6;
    dsc.rounded = 1;
    dsc.start_angle = 270.0F;
    dsc.end_angle = 270.0F + 360.0F * progress;
    dsc.color = progress >= 1.0F ? ui.palette.ink : ui.palette.accent;
    dsc.opa = LV_OPA_COVER;
    lv_draw_arc(layer, &dsc);
}

void lean_press_tick(lv_timer_t *) {
    if(!ui.lean_press_active) return;
    ui.lean_press_elapsed_ms = lv_tick_elaps(ui.lean_press_start_ms);
    const bool show = ui.lean_press_elapsed_ms >= 300;
    if(show != ui.lean_progress_visible || show) {
        if(show && !ui.lean_progress_visible) {
            ui.lean_progress_visible = true;
            lv_obj_remove_flag(ui.lean_progress, LV_OBJ_FLAG_HIDDEN);
        }
        lv_obj_invalidate(ui.lean_progress);
    }
}

void lean_press_event(lv_event_t *) {
    ui.lean_press_active = true;
    ui.lean_press_start_ms = lv_tick_get();
    ui.lean_press_elapsed_ms = 0;
    ui.lean_progress_visible = false;
    lv_timer_resume(ui.lean_press_timer);
}

void lean_release_event(lv_event_t *event) {
    if(!ui.lean_press_active) return;
    const uint32_t elapsed = lv_tick_elaps(ui.lean_press_start_ms);
    ui.lean_press_active = false;
    ui.lean_press_elapsed_ms = 0;
    ui.lean_progress_visible = false;
    lv_timer_pause(ui.lean_press_timer);
    if(ui.lean_progress != nullptr) {
        lv_obj_add_flag(ui.lean_progress, LV_OBJ_FLAG_HIDDEN);
    }
    if(lv_event_get_code(event) == LV_EVENT_PRESS_LOST) return;
    if(elapsed >= 3000) {
        // Countdown starts on release; the sensor capture request fires when
        // it reaches zero, not while the rider is still holding.
        ui.lean_cal_state = MOTO_UI_LEAN_CAL_COUNTDOWN;
        ui.lean_cal_value = 3;
        ui.lean_countdown_ms = 0;
        ui.lean_countdown_pending = true;
    } else if(elapsed >= 2000) {
        if(ui.lean_clear_callback != nullptr) {
            ui.lean_clear_callback(ui.lean_clear_context);
        } else {
            clear_lean_peaks();
        }
    }
}

void lean_status_tick(lv_timer_t *) {
    if(ui.lean_cal_state == MOTO_UI_LEAN_CAL_OK ||
       ui.lean_cal_state == MOTO_UI_LEAN_CAL_FAIL_MOVING ||
       ui.lean_cal_state == MOTO_UI_LEAN_CAL_FAIL_RANGE) {
        ui.lean_result_ms += 100;
        if(ui.lean_result_ms >= 1000) {
            ui.lean_cal_state = MOTO_UI_LEAN_CAL_NONE;
            ui.lean_result_ms = 0;
        }
    }
    if(ui.lean_cal_state == MOTO_UI_LEAN_CAL_COUNTDOWN &&
       ui.lean_countdown_pending) {
        ui.lean_countdown_ms += 100;
        if(ui.lean_countdown_ms >= 1000) {
            ui.lean_countdown_ms = 0;
            if(ui.lean_cal_value > 0) --ui.lean_cal_value;
            if(ui.lean_cal_value == 0) {
                ui.lean_cal_state = MOTO_UI_LEAN_CAL_CAPTURING;
                ui.lean_countdown_pending = false;
                if(ui.lean_calibration_callback != nullptr) {
                    ui.lean_calibration_callback(ui.lean_calibration_context);
                }
            }
        }
    }
    const uint8_t flags = static_cast<uint8_t>(
        (ui.lean_flag_cal != 0 ? 1U : 0U) | (ui.lean_flag_poll != 0 ? 2U : 0U) |
        (ui.lean_flag_full != 0 ? 4U : 0U) |
        (ui.lean_flag_dirty != 0 ? 8U : 0U));
    if(ui.lean_status_text == nullptr) return;
    if(ui.lean_cal_state == ui.lean_cal_applied &&
       ui.lean_cal_value == ui.lean_cal_value_applied &&
       flags == ui.lean_flags_applied) {
        return;
    }
    if(ui.lean_cal_state == MOTO_UI_LEAN_CAL_OK ||
       ui.lean_cal_state == MOTO_UI_LEAN_CAL_FAIL_MOVING ||
       ui.lean_cal_state == MOTO_UI_LEAN_CAL_FAIL_RANGE) {
        ui.lean_result_ms = 0;
    }
    ui.lean_cal_applied = ui.lean_cal_state;
    ui.lean_cal_value_applied = ui.lean_cal_value;
    ui.lean_flags_applied = flags;
    char text[32];
    bool visible = true;
    switch(ui.lean_cal_state) {
        case MOTO_UI_LEAN_CAL_COUNTDOWN:
            std::snprintf(text, sizeof(text), "CAL %u",
                          static_cast<unsigned>(ui.lean_cal_value));
            break;
        case MOTO_UI_LEAN_CAL_CAPTURING:
            std::snprintf(text, sizeof(text), "CAL...");
            break;
        case MOTO_UI_LEAN_CAL_OK:
            std::snprintf(text, sizeof(text), "CAL OK");
            break;
        case MOTO_UI_LEAN_CAL_FAIL_MOVING:
            std::snprintf(text, sizeof(text), "CAL FAIL\nMOVING");
            break;
        case MOTO_UI_LEAN_CAL_FAIL_RANGE:
            std::snprintf(text, sizeof(text), "CAL FAIL\nRANGE");
            break;
        default:
            // CAL first: until the mount is calibrated the number above is
            // not the bike's Lean, and a full ride log used to hide that
            // prompt for good.
            if((flags & 1U) != 0U) {
                std::snprintf(text, sizeof(text), "CAL");
            } else if((flags & 8U) != 0U) {
                std::snprintf(text, sizeof(text), "LOG DIRTY");
            } else if((flags & 4U) != 0U) {
                std::snprintf(text, sizeof(text), "LOG FULL");
            } else if((flags & 2U) != 0U) {
                std::snprintf(text, sizeof(text), "IMU POLL");
            } else {
                visible = false;
                text[0] = '\0';
            }
            break;
    }
    lv_label_set_text(ui.lean_status_text, text);
    if(visible) {
        lv_obj_remove_flag(ui.lean_status_plate, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(ui.lean_status_plate, LV_OBJ_FLAG_HIDDEN);
    }
}

void draw_lean_marks_layer(lv_layer_t *layer) {
    // Every mark starts at r >= 198 (the zero bar's inner end, minus its cap).
    if(clip_inside_radius(layer, 194)) return;
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
        // A fast flick can stretch a horizon strip into the casing; it
        // repaints the ticks it reaches, not the whole protractor.
        if(!clip_touches_segment(layer, pts[0], pts[1], 3)) continue;
        draw_segment(layer, ui.palette.on_accent, major ? 3 : 2, LV_OPA_COVER,
                     pts, 2);
    }
    // The zero reference: the only mark that is a bar rather than a tick.
    lv_point_precise_t zero[2] = {pt(cx, cy - 223.0), pt(cx, cy - 198.0)};
    if(clip_touches_segment(layer, zero[0], zero[1], 4)) {
        draw_segment(layer, ui.palette.route, 5, LV_OPA_COVER, zero, 2);
    }
}

// The lean page is an artificial horizon, not a bar graph. The bike is the
// frame: the disc stays still and the world tilts, which is exactly what the
// rider sees over the bars. The sun gives the horizon a second reference so a
// ten-degree lean is readable without reading the number.
void apply_debug_hidden() {
    if(ui.lean_scene != nullptr) {
        if((g_debug_hidden & MOTO_UI_DBG_HIDE_SCENE) != 0) {
            lv_obj_add_flag(ui.lean_scene, LV_OBJ_FLAG_HIDDEN);
        } else if(ui.lean_has) {
            lv_obj_remove_flag(ui.lean_scene, LV_OBJ_FLAG_HIDDEN);
        }
        lv_obj_invalidate(ui.lean_scene);
    }
    if(ui.lean_marks != nullptr) {
        if((g_debug_hidden & MOTO_UI_DBG_HIDE_CHROME) != 0) {
            lv_obj_add_flag(ui.lean_marks, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_remove_flag(ui.lean_marks, LV_OBJ_FLAG_HIDDEN);
        }
    }
    // The Lean page's disc stays hidden whatever the mask: the scene owns that
    // paper (create_lean_page). This runs on every theme change, and showing
    // the disc here put it back under the scene for good.
    if(ui.page_discs[MOTO_UI_PAGE_LEAN] != nullptr) {
        lv_obj_add_flag(ui.page_discs[MOTO_UI_PAGE_LEAN], LV_OBJ_FLAG_HIDDEN);
    }
    if(ui.page_frames[MOTO_UI_PAGE_LEAN] != nullptr) {
        if((g_debug_hidden & MOTO_UI_DBG_HIDE_CHROME) != 0) {
            lv_obj_add_flag(ui.page_frames[MOTO_UI_PAGE_LEAN],
                            LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_remove_flag(ui.page_frames[MOTO_UI_PAGE_LEAN],
                               LV_OBJ_FLAG_HIDDEN);
        }
    }
}

void draw_lean_marks(lv_event_t *event) {
    draw_lean_marks_layer(lv_event_get_layer(event));
}

// The scene's paper disc ends at the casing's inner edge.
constexpr int kLeanSceneRadius = kDialCasingOuter - kDialCasingWidth;

// LVGL fills a circle-radius background through a radius mask on every row,
// even for a strip deep inside the circle, and the scene's paper sits under
// every dirty area of this page. Where the strip is inside the disc the
// corner cannot show, so the fill is a plain rectangle.
void lean_scene_draw_task(lv_event_t *event) {
    lv_draw_task_t *task = lv_event_get_draw_task(event);
    if(lv_draw_task_get_type(task) != LV_DRAW_TASK_TYPE_FILL) return;
    lv_draw_fill_dsc_t *fill = lv_draw_task_get_fill_dsc(task);
    if(fill == nullptr || fill->base.part != LV_PART_MAIN ||
       fill->base.layer == nullptr) {
        return;
    }
    if(clip_inside_radius(fill->base.layer, kLeanSceneRadius - 1)) {
        fill->radius = 0;
    }
}

void draw_lean_scene(lv_event_t *event) {
    lv_layer_t *layer = lv_event_get_layer(event);
    // No attitude yet is still a valid composition: the arc is dark and the
    // NO IMU plate takes the numeral's place. Hiding the whole scene was what
    // made the page look like a different product whenever the IMU had not
    // produced a sample (or the board was on the bench).
    const float lean = ui.lean_has ? ui.lean_deg : 0.0F;
    // Each dirty area calls this once. LVGL clips every draw to the area, but
    // a draw that cannot touch it still costs its setup, so elements outside
    // the area are skipped here.
    const lv_area_t &clip = layer->_clip_area;
    const auto touches = [&](const lv_area_t &box) {
        return box.x1 <= clip.x2 && box.x2 >= clip.x1 && box.y1 <= clip.y2 &&
               box.y2 >= clip.y1;
    };
    // Profiling buckets (`ui dbg`): sun = arc, hero = numeral.
    const auto clock_now = [] {
        return (g_debug_enabled && g_debug_clock != nullptr) ? g_debug_clock()
                                                              : 0;
    };

    if((g_debug_hidden & MOTO_UI_DBG_HIDE_SUN) == 0) {
        const std::uint64_t started = clock_now();
        const LeanArcState arc =
            lean_arc_state(lean, ui.lean_peak_left, ui.lean_peak_right);
        lv_draw_arc_dsc_t dsc;
        lv_draw_arc_dsc_init(&dsc);
        dsc.center = {MOTO_UI_CANVAS_WIDTH / 2, MOTO_UI_CANVAS_HEIGHT / 2};
        dsc.radius = kLeanArcOuter;
        dsc.width = kLeanArcOuter - kLeanArcInner;
        dsc.rounded = 0;
        dsc.opa = LV_OPA_COVER;
        // LVGL counts from three o'clock; the arc from twelve.
        const auto stroke = [&](double from, double to, lv_color_t color) {
            if(to - from < 0.05) return;
            dsc.color = color;
            dsc.start_angle = static_cast<lv_value_precise_t>(from + 270.0);
            dsc.end_angle = static_cast<lv_value_precise_t>(to + 270.0);
            lv_draw_arc(layer, &dsc);
        };
        bool drew = false;
        for(int s = 0; s < 2; ++s) {
            const int side = s == 0 ? -1 : 1;
            for(int i = 1; i <= kLeanArcSegments; ++i) {
                if(!touches(lean_arc_segment_area(side, i))) continue;
                drew = true;
                const int lit = std::clamp(
                    arc.lit[s] - (i - 1) * kLeanArcQuarters, 0,
                    kLeanArcQuarters);
                const lv_color_t on = i >= kLeanArcHotFrom
                                          ? ui.palette.gauge_hot
                                          : ui.palette.route;
                const lv_color_t off = i == arc.peak[s] ? ui.palette.hud
                                                        : ui.palette.gauge_off;
                double from, to;
                lean_arc_angles(side, i, from, to);
                // The lit part starts at the segment's inner end: next to
                // zero on either side.
                const double split = (to - from) * lit / kLeanArcQuarters;
                if(side > 0) {
                    stroke(from, from + split, on);
                    stroke(from + split, to, off);
                } else {
                    stroke(to - split, to, on);
                    stroke(from, to - split, off);
                }
            }
        }
        if(started != 0 && drew) {
            g_debug_stats.sun_us +=
                static_cast<std::uint32_t>(clock_now() - started);
            ++g_debug_stats.sun_draws;
        }
    }

    const lv_area_t hero_area = ui.lean_text[0] != '\0'
                                    ? lean_hero_area(ui.lean_text)
                                    : lv_area_t{0, 0, -1, -1};
    if(ui.lean_has && ui.lean_text[0] != '\0' &&
       (g_debug_hidden & MOTO_UI_DBG_HIDE_HERO) == 0 && touches(hero_area)) {
        const std::uint64_t started = clock_now();
        lv_draw_label_dsc_t label;
        lv_draw_label_dsc_init(&label);
        label.text = ui.lean_text;
        label.font = &moto_font_hero_176;
        label.color = ui.palette.hud;
        label.align = LV_TEXT_ALIGN_LEFT;
        label.opa = LV_OPA_COVER;
        lv_draw_label(layer, &label, &hero_area);
        if(started != 0) {
            g_debug_stats.hero_us +=
                static_cast<std::uint32_t>(clock_now() - started);
            ++g_debug_stats.hero_draws;
        }
    }
}

// A full-screen drawing layer must never be scrollable. lv_obj_create() sets
// LV_OBJ_FLAG_SCROLLABLE, and remove_style_all() does not clear flags; a
// scrollable overlay swallows the horizontal drag that pages with, which is
// exactly how the Lean page used to trap the rider. Every canvas layer on this
// page is born through this helper.
lv_obj_t *make_draw_canvas(lv_obj_t *parent, lv_event_cb_t draw) {
    lv_obj_t *canvas = lv_obj_create(parent);
    lv_obj_remove_style_all(canvas);
    lv_obj_clear_flag(canvas, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(canvas, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(canvas, MOTO_UI_CANVAS_WIDTH, MOTO_UI_CANVAS_HEIGHT);
    lv_obj_set_pos(canvas, 0, 0);
    lv_obj_add_event_cb(canvas, draw, LV_EVENT_DRAW_MAIN, nullptr);
    return canvas;
}

void create_lean_page() {
    lv_obj_t *page = ui.pages[MOTO_UI_PAGE_LEAN];
    make_page_surface(page, MOTO_UI_PAGE_LEAN);

    // v2 calibration entry (DESIGN_V2 §3.3 step 1): a long press anywhere on
    // the Lean page. The handler lives on the page container instead of a
    // transparent full-screen child: the page already owns the touch surface,
    // is not scrollable (make_layer), and bubbles gestures to the pager. The
    // old overlay was scrollable, so a horizontal drag scrolled it instead of
    // reaching the pager and the page appeared to freeze. The sensor rejects
    // the request unless the bike is stationary.
    lv_obj_set_name_static(page, "moto-lean-page");
    lv_obj_add_event_cb(page, lean_press_event, LV_EVENT_PRESSED, nullptr);
    lv_obj_add_event_cb(page, lean_release_event, LV_EVENT_RELEASED, nullptr);
    lv_obj_add_event_cb(page, lean_release_event, LV_EVENT_PRESS_LOST, nullptr);

    ui.lean_marks = make_draw_canvas(page, draw_lean_marks);
    ui.lean_scene = make_draw_canvas(page, draw_lean_scene);
    // The scene owns its paper. An opaque disc lets LVGL skip everything
    // below it when only the horizon moves; a transparent full-screen layer
    // used to force a whole-page repaint on every lift update. It ends at the
    // casing's inner edge, so the frame and protractor can sit below it and
    // a dirty area inside the paper never visits them: on the board each
    // object an area has to walk is part of the fixed per-area cost that
    // dominates this page.
    lv_obj_set_size(ui.lean_scene, kLeanSceneRadius * 2, kLeanSceneRadius * 2);
    lv_obj_center(ui.lean_scene);
    lv_obj_set_style_radius(ui.lean_scene, LV_RADIUS_CIRCLE, 0);
    // clip_corner is unnecessary: the scene's content (horizon, sun, hero) is
    // inside the circle. Turning it off avoids a rounded clip mask on every
    // dirty strip.
    lv_obj_set_style_clip_corner(ui.lean_scene, false, 0);
    lv_obj_set_style_bg_color(ui.lean_scene, ui.palette.ground, 0);
    lv_obj_set_style_bg_opa(ui.lean_scene, LV_OPA_COVER, 0);
    lv_obj_add_flag(ui.lean_scene, LV_OBJ_FLAG_SEND_DRAW_TASK_EVENTS);
    lv_obj_add_event_cb(ui.lean_scene, lean_scene_draw_task,
                        LV_EVENT_DRAW_TASK_ADDED, nullptr);
    // The scene owns the Lean page's paper: hide the page disc underneath so
    // LVGL does not repaint it (and the frame rings) on every horizon update.
    if(ui.page_discs[MOTO_UI_PAGE_LEAN] != nullptr) {
        lv_obj_add_flag(ui.page_discs[MOTO_UI_PAGE_LEAN],
                        LV_OBJ_FLAG_HIDDEN);
    }

    // The hero is drawn inside draw_lean_scene, not as a label: a separate
    // transparent label forced LVGL to build and composite an extra layer for
    // every dirty strip. The number is part of the same scene layer now.

    // Liberty City at dusk along the bottom, as on the other tool pages. It
    // sits above the scene's paper (the scene is this page's disc). The
    // casing here is drawn below the scene, so a strip of casing over the
    // bottom band trims the towers' feet; the arc and the numeral never
    // dirty that band, so LVGL never visits it while the bike leans.
    lv_obj_t *city = make_skyline(page, nullptr);
    lv_obj_set_y(city, kPageSkylineDrop);
    lv_obj_t *casing = lv_obj_create(page);
    lv_obj_remove_style_all(casing);
    lv_obj_clear_flag(casing, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(casing, LV_OBJ_FLAG_SCROLLABLE);
    constexpr int kCasingBandTop = 296;
    lv_obj_set_pos(casing, 0, kCasingBandTop);
    lv_obj_set_size(casing, MOTO_UI_CANVAS_WIDTH,
                    MOTO_UI_CANVAS_HEIGHT - kCasingBandTop);
    lv_obj_add_event_cb(casing, draw_instrument_frame, LV_EVENT_DRAW_MAIN,
                        nullptr);

    // The side peaks flank the numeral, each beside its half of the arc.
    // Centred at x 82 and 384: a new value dirties only the label, which
    // stays inside the casing.
    ui.lean_max_l_plate =
        make_plate(page, "L --", 90, 26, &ui.lean_max_l);
    lv_obj_align(ui.lean_max_l_plate, LV_ALIGN_CENTER, 82 - 233,
                 kLeanHeroLiftPx);
    ui.lean_max_r_plate =
        make_plate(page, "R --", 90, 26, &ui.lean_max_r);
    lv_obj_align(ui.lean_max_r_plate, LV_ALIGN_CENTER, 384 - 233,
                 kLeanHeroLiftPx);

    // Without attitude the plate takes the numeral's place.
    ui.lean_state = make_plate(page, "NO IMU", 120, 30, &ui.lean_state_text);
    lv_obj_align(ui.lean_state, LV_ALIGN_CENTER, 0, kLeanHeroLiftPx);
    lv_obj_add_flag(ui.lean_state, LV_OBJ_FLAG_HIDDEN);

    // Full-screen, so it stays hidden until a press shows it: a visible
    // canvas is visited by every dirty area even when it draws nothing.
    ui.lean_progress = make_draw_canvas(page, draw_lean_progress);
    lv_obj_add_flag(ui.lean_progress, LV_OBJ_FLAG_HIDDEN);

    // One status slot: CAL / countdown / result / IMU POLL / LOG FULL / LOG
    // DIRTY. It sits under the top of the arc and above the numeral, so it
    // never competes with the bike (v2 r1 §6.2).
    ui.lean_status_plate = make_plate(page, "", 150, 56, &ui.lean_status_text);
    lv_obj_align(ui.lean_status_plate, LV_ALIGN_CENTER, 0, -122);
    lv_obj_set_name_static(ui.lean_status_plate, "moto-lean-status");
    lv_obj_set_style_text_align(ui.lean_status_text, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_line_space(ui.lean_status_text, -4, 0);
    lv_obj_add_flag(ui.lean_status_plate, LV_OBJ_FLAG_HIDDEN);

    // Frame and protractor stay below the scene, outside its disc.

    ui.lean_press_timer = lv_timer_create(lean_press_tick, 30, nullptr);
    lv_timer_pause(ui.lean_press_timer);
    ui.lean_status_timer = lv_timer_create(lean_status_tick, 100, nullptr);

    ui.lean_display = lv_obj_get_display(page);
    if(ui.lean_display != nullptr) {
        lv_display_add_event_cb(ui.lean_display, lean_refresh_start,
                                LV_EVENT_REFR_START, nullptr);
    }
}

// ---------------------------------------------------------------------------
// Speed·G. The friction circle is the page: lateral G across (right lean to
// the right), longitudinal G up the middle (accelerating up, braking down),
// rings at 0.5 / 1.0 / 1.5 g. The live dot is HUD orange like every "rider
// now" mark; the envelope, the most the ride has asked of the tyres in each of
// 16 directions, is drawn in neon pink. Speed is a counter slab, small.

constexpr int kGgCentreDy = 3;             // circle centre below the canvas centre
constexpr int kGgRadius = 128;             // 1.5 g
constexpr float kGgFullScale = 1.5F;
constexpr float kGgPxPerG = kGgRadius / kGgFullScale;
constexpr int kGgSectors = 16;
constexpr int kGgTrail = 6;
constexpr std::uint32_t kGgTrailStepMs = 250;   // 6 x 250 ms = 1.5 s
constexpr float kGgDisplayTauS = 0.04F;   // 4 Hz display low-pass
constexpr float kGgPeakTauS = 0.2F;       // peaks from 0.2 s means
constexpr std::uint32_t kGgResetHoldMs = 2'000;
constexpr int kGgDotSize = 18;

void gg_point(float lat, float lon, int32_t &x, int32_t &y) {
    // Clamp to the outer ring so a spike never leaves the instrument.
    const float magnitude = std::sqrt(lat * lat + lon * lon);
    if(magnitude > kGgFullScale) {
        lat *= kGgFullScale / magnitude;
        lon *= kGgFullScale / magnitude;
    }
    x = MOTO_UI_CANVAS_WIDTH / 2 +
        static_cast<int32_t>(std::lround(lat * kGgPxPerG));
    y = MOTO_UI_CANVAS_HEIGHT / 2 + kGgCentreDy -
        static_cast<int32_t>(std::lround(lon * kGgPxPerG));
}

void clear_gg_envelope() {
    for(float &sector : ui.gg_envelope) sector = 0.0F;
    for(std::uint32_t &stamp : ui.gg_trail_ms) stamp = 0;
    ui.gg_envelope_dirty = true;
    if(ui.gg_plot != nullptr) lv_obj_invalidate(ui.gg_plot);
}

void accumulate_gg(const moto_ui_state_t *state) {
    const bool has = state->has_attitude != 0 &&
                     std::isfinite(state->lateral_g) &&
                     std::isfinite(state->longitudinal_g);
    ui.gg_has = has;
    if(!has) {
        ui.gg_seeded = false;
        return;
    }
    const std::uint32_t now = lv_tick_get();
    const float lat = std::clamp(state->lateral_g, -4.0F, 4.0F);
    const float lon = std::clamp(state->longitudinal_g, -4.0F, 4.0F);
    if(!ui.gg_seeded) {
        ui.gg_lat = ui.gg_mean_lat = lat;
        ui.gg_lon = ui.gg_mean_lon = lon;
        ui.gg_last_ms = now;
        ui.gg_seeded = true;
    }
    const float dt = std::clamp(
        static_cast<float>(lv_tick_elaps(ui.gg_last_ms)) / 1000.0F, 0.0F,
        0.25F);
    ui.gg_last_ms = now;
    const float k_display = 1.0F - std::exp(-dt / kGgDisplayTauS);
    const float k_peak = 1.0F - std::exp(-dt / kGgPeakTauS);
    ui.gg_lat += (lat - ui.gg_lat) * k_display;
    ui.gg_lon += (lon - ui.gg_lon) * k_display;
    ui.gg_mean_lat += (lat - ui.gg_mean_lat) * k_peak;
    ui.gg_mean_lon += (lon - ui.gg_mean_lon) * k_peak;

    const float peak = std::sqrt(ui.gg_mean_lat * ui.gg_mean_lat +
                                 ui.gg_mean_lon * ui.gg_mean_lon);
    if(peak > 0.05F) {
        const float angle = std::atan2(ui.gg_mean_lon, ui.gg_mean_lat);
        const float turn = angle < 0.0F ? angle + 2.0F * static_cast<float>(kPi)
                                        : angle;
        const int sector =
            static_cast<int>(std::lround(turn / (2.0F * static_cast<float>(kPi)) *
                                         kGgSectors)) % kGgSectors;
        const float capped = std::min(peak, kGgFullScale);
        if(capped > ui.gg_envelope[sector] + 0.01F) {
            ui.gg_envelope[sector] = capped;
            ui.gg_envelope_dirty = true;
        }
    }
    if(lv_tick_elaps(ui.gg_trail_last_ms) >= kGgTrailStepMs) {
        ui.gg_trail_last_ms = now;
        ui.gg_trail_head = (ui.gg_trail_head + 1) % kGgTrail;
        ui.gg_trail_lat[ui.gg_trail_head] = ui.gg_lat;
        ui.gg_trail_lon[ui.gg_trail_head] = ui.gg_lon;
        ui.gg_trail_ms[ui.gg_trail_head] = now | 1U;
    }
}

void draw_gg_plot(lv_event_t *event) {
    lv_layer_t *layer = lv_event_get_layer(event);
    const int32_t cx = MOTO_UI_CANVAS_WIDTH / 2;
    const int32_t cy = MOTO_UI_CANVAS_HEIGHT / 2 + kGgCentreDy;

    lv_draw_arc_dsc_t ring;
    lv_draw_arc_dsc_init(&ring);
    ring.center = {cx, cy};
    ring.start_angle = 0;
    ring.end_angle = 360;
    ring.rounded = 0;
    // Inner rings are scale, not content: thin and muted. The outer ring is
    // the instrument's edge.
    ring.color = ui.palette.muted;
    ring.opa = LV_OPA_60;
    ring.width = 2;
    for(const float g : {0.5F, 1.0F}) {
        ring.radius = static_cast<uint16_t>(std::lround(g * kGgPxPerG));
        lv_draw_arc(layer, &ring);
    }
    ring.color = ui.palette.ink;
    ring.opa = LV_OPA_COVER;
    ring.width = 3;
    ring.radius = kGgRadius;
    lv_draw_arc(layer, &ring);

    const lv_point_precise_t across[2] = {pt(cx - kGgRadius, cy),
                                          pt(cx + kGgRadius, cy)};
    const lv_point_precise_t up[2] = {pt(cx, cy - kGgRadius),
                                      pt(cx, cy + kGgRadius)};
    draw_segment(layer, ui.palette.muted, 1, LV_OPA_60, across, 2);
    draw_segment(layer, ui.palette.muted, 1, LV_OPA_60, up, 2);

    // Envelope: join neighbouring sectors the ride has reached; an unvisited
    // direction is left open rather than drawn as a spike to the centre.
    lv_point_precise_t corner[kGgSectors];
    for(int i = 0; i < kGgSectors; ++i) {
        const double angle = 2.0 * kPi * i / kGgSectors;
        const double r = ui.gg_envelope[i] * kGgPxPerG;
        corner[i] = pt(cx + std::cos(angle) * r, cy - std::sin(angle) * r);
    }
    // Casing first, all of it, then the pink: interleaving the two let each
    // edge's casing cut the end of the one before, and the line read dashed.
    for(int pass = 0; pass < 2; ++pass) {
        for(int i = 0; i < kGgSectors; ++i) {
            const int next = (i + 1) % kGgSectors;
            if(ui.gg_envelope[i] <= 0.0F || ui.gg_envelope[next] <= 0.0F) {
                continue;
            }
            const lv_point_precise_t edge[2] = {corner[i], corner[next]};
            if(pass == 0) {
                draw_segment(layer, ui.palette.outline, 7, LV_OPA_COVER, edge,
                             2);
            } else {
                draw_segment(layer, ui.palette.hud, 3, LV_OPA_COVER, edge, 2);
            }
        }
    }
    ui.gg_envelope_dirty = false;
}

void gg_press_event(lv_event_t *) {
    ui.gg_press_active = true;
    ui.gg_press_start_ms = lv_tick_get();
}

void gg_release_event(lv_event_t *event) {
    if(!ui.gg_press_active) return;
    ui.gg_press_active = false;
    if(lv_event_get_code(event) == LV_EVENT_PRESS_LOST) return;
    // Same gesture as the Lean page's peak reset: hold two seconds.
    if(lv_tick_elaps(ui.gg_press_start_ms) >= kGgResetHoldMs) {
        clear_gg_envelope();
    }
}

lv_obj_t *make_gg_mark(lv_obj_t *parent, int size, lv_opa_t opa) {
    lv_obj_t *mark = lv_obj_create(parent);
    lv_obj_remove_style_all(mark);
    lv_obj_clear_flag(mark, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(mark, size, size);
    lv_obj_set_style_radius(mark, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(mark, ui.palette.route, 0);
    lv_obj_set_style_bg_opa(mark, opa, 0);
    lv_obj_set_style_border_color(mark, kOutlineBlack, 0);
    lv_obj_set_style_border_width(mark, size > 10 ? 3 : 0, 0);
    lv_obj_set_style_border_opa(mark, LV_OPA_COVER, 0);
    lv_obj_add_flag(mark, LV_OBJ_FLAG_HIDDEN);
    return mark;
}

void create_gg_page() {
    lv_obj_t *page = ui.pages[MOTO_UI_PAGE_SPEED];
    make_page_surface(page, MOTO_UI_PAGE_SPEED);
    lv_obj_set_name_static(page, "moto-gg-page");
    lv_obj_add_event_cb(page, gg_press_event, LV_EVENT_PRESSED, nullptr);
    lv_obj_add_event_cb(page, gg_release_event, LV_EVENT_RELEASED, nullptr);
    lv_obj_add_event_cb(page, gg_release_event, LV_EVENT_PRESS_LOST, nullptr);

    ui.gg_plot = make_draw_canvas(page, draw_gg_plot);
    lv_obj_set_name_static(ui.gg_plot, "moto-gg-plot");

    // Axis words, tiny and muted, at the ends of the cross.
    const struct {
        const char *text;
        int dx;
        int dy;
    } axes[] = {{"ACC", 0, -kGgRadius + 16},
                {"BRK", 0, kGgRadius - 16},
                {"L", -kGgRadius + 14, -12},
                {"R", kGgRadius - 14, -12}};
    for(const auto &axis : axes) {
        lv_obj_t *label = make_text(page, &moto_font_display_18,
                                    TextRole::Muted, axis.text);
        lv_obj_set_style_text_letter_space(label, px(2), 0);
        lv_obj_align(label, LV_ALIGN_CENTER, axis.dx, axis.dy + kGgCentreDy);
    }

    for(int i = 0; i < kGgTrail; ++i) {
        ui.gg_trail[i] = make_gg_mark(page, 8, LV_OPA_50);
    }
    ui.gg_dot = make_gg_mark(page, kGgDotSize, LV_OPA_COVER);
    lv_obj_set_name_static(ui.gg_dot, "moto-gg-dot");

    // Combined G, live: the one number on the page, but not a poster one.
    ui.gg_live = make_text(page, &moto_font_display_40, TextRole::Display,
                           "0.00 G");
    lv_obj_set_name_static(ui.gg_live, "moto-gg-live");
    lv_obj_align(ui.gg_live, LV_ALIGN_TOP_MID, 0, px(34));

    lv_obj_t *speed = make_slab(page, "0 KM/H", 130, 30);
    lv_obj_align(speed, LV_ALIGN_CENTER, 0, px(122));
    ui.gg_speed = lv_obj_get_child(speed, 0);

    ui.gg_state = make_plate(page, "NO IMU", 120, 30);
    lv_obj_align(ui.gg_state, LV_ALIGN_CENTER, 0, kGgCentreDy);
}

void update_gg_view(const moto_ui_state_t *state) {
    if(ui.gg_plot == nullptr) return;
    char text[20];
    std::snprintf(text, sizeof(text), "%u KM/H",
                  static_cast<unsigned>(state->speed_kph));
    lv_label_set_text(ui.gg_speed, text);
    if(!ui.gg_has) {
        lv_obj_add_flag(ui.gg_dot, LV_OBJ_FLAG_HIDDEN);
        for(lv_obj_t *mark : ui.gg_trail) lv_obj_add_flag(mark, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(ui.gg_state, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(ui.gg_live, "-.-- G");
        return;
    }
    lv_obj_add_flag(ui.gg_state, LV_OBJ_FLAG_HIDDEN);
    int32_t x = 0;
    int32_t y = 0;
    gg_point(ui.gg_lat, ui.gg_lon, x, y);
    lv_obj_set_pos(ui.gg_dot, x - kGgDotSize / 2, y - kGgDotSize / 2);
    lv_obj_remove_flag(ui.gg_dot, LV_OBJ_FLAG_HIDDEN);
    const std::uint32_t now = lv_tick_get();
    for(int i = 0; i < kGgTrail; ++i) {
        lv_obj_t *mark = ui.gg_trail[i];
        const std::uint32_t stamp = ui.gg_trail_ms[i];
        if(stamp == 0 || now - stamp > kGgTrailStepMs * kGgTrail) {
            lv_obj_add_flag(mark, LV_OBJ_FLAG_HIDDEN);
            continue;
        }
        gg_point(ui.gg_trail_lat[i], ui.gg_trail_lon[i], x, y);
        lv_obj_set_pos(mark, x - 4, y - 4);
        lv_obj_remove_flag(mark, LV_OBJ_FLAG_HIDDEN);
    }
    const float combined =
        std::sqrt(ui.gg_lat * ui.gg_lat + ui.gg_lon * ui.gg_lon);
    std::snprintf(text, sizeof(text), "%.2f G", static_cast<double>(combined));
    lv_label_set_text(ui.gg_live, text);
    if(ui.gg_envelope_dirty) lv_obj_invalidate(ui.gg_plot);
}

void create_music_page() {
    lv_obj_t *page = ui.pages[MOTO_UI_PAGE_MUSIC];
    make_page_surface(page, MOTO_UI_PAGE_MUSIC);
    add_page_skyline(page);

    // A car radio faceplate, not a media app: source on a plate, the track
    // printed like a station ident, and three physical keys.
    ui.music_source_plate =
        make_plate(page, "APPLE MUSIC", 126, 26, &ui.music_source);
    lv_obj_set_pos(ui.music_source_plate, px(78), px(68));

    ui.music_title = make_text(page, &moto_font_display_40_cjk,
                               TextRole::Display, "NIGHT RIDE");
    lv_obj_set_width(ui.music_title, px(288));
    // A long title runs across the plate like a station ident instead of
    // stacking into the artist line. One line, always: the label grows to the
    // rendered face's line height (the CJK fallback is the taller one), never
    // to a second line.
    lv_label_set_long_mode(ui.music_title, LV_LABEL_LONG_SCROLL_CIRCULAR);
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

// Hard cuts at fixed times; the bar steps eight times. Ends before app_main's
// 1.25 s boot hold, so the last frame is complete when the UI replaces it.
constexpr std::uint32_t kBootGridMs = 80;
constexpr std::uint32_t kBootLogoMs = 380;
constexpr std::uint32_t kBootBarMs = 520;
constexpr std::uint32_t kBootBarStepMs = 75;
constexpr int kBootBarSteps = 8;

void boot_tick(lv_timer_t *) {
    const std::uint32_t elapsed = lv_tick_elaps(ui.boot_started_ms);
    if(elapsed >= kBootGridMs && ui.boot_grid != nullptr) {
        lv_obj_remove_flag(ui.boot_grid, LV_OBJ_FLAG_HIDDEN);
    }
    if(elapsed >= kBootLogoMs && ui.boot_logo != nullptr) {
        lv_obj_remove_flag(ui.boot_logo, LV_OBJ_FLAG_HIDDEN);
    }
    if(elapsed >= kBootBarMs && ui.boot_bar != nullptr) {
        lv_obj_remove_flag(ui.boot_bar, LV_OBJ_FLAG_HIDDEN);
        const int steps = std::min<int>(
            kBootBarSteps,
            1 + static_cast<int>((elapsed - kBootBarMs) / kBootBarStepMs));
        const int inner = lv_obj_get_width(ui.boot_bar) - 6;
        lv_obj_set_width(ui.boot_bar_fill, inner * steps / kBootBarSteps);
        if(steps >= kBootBarSteps && ui.boot_timer != nullptr) {
            lv_timer_pause(ui.boot_timer);
        }
    }
}

}  // namespace

void moto_ui_gta::teardown(void) {
    teardown_ui();
    // The screen outlives every tree, so the classic backend would otherwise
    // inherit this one's pager and wake handlers.
    if(ui.screen != nullptr) {
        lv_obj_remove_event_cb(ui.screen, gesture_event);
        lv_obj_remove_event_cb(ui.screen, interaction_event);
    }
}

void moto_ui_gta::set_boot_theme(moto_ui_theme_t theme) {
    g_boot_theme = theme;
}

void moto_ui_gta::show_boot_screen(void) {
    /* Also used while the full UI is live, so it must leave nothing behind
       that still points at the previous tree. */
    teardown_ui();
    if(ui.screen == nullptr) return;
    lv_obj_remove_flag(ui.screen, LV_OBJ_FLAG_SCROLLABLE);
    // GTA III's loading screens cut, they never fade: black, then the box
    // art's framed panels, then the logo stamped over them, then the loading
    // bar stepping along the bottom. The old 1.1 s opacity fade of one card
    // was the least GTA transition there is, and a full-screen blend is also
    // the most expensive thing this panel can draw.
    //
    // The palette is the boot theme's, chosen from persisted settings before
    // the tree (and ui.palette) exists.
    static MapPalette palette;
    palette = palette_for_theme(g_boot_theme, false);
    // The shared glyph painters (rider arrow, safehouse) read ui.palette.
    ui.palette = palette;
    const lv_color_t gutter = palette.accent;  // white by night, black by day
    lv_obj_set_style_bg_color(ui.screen, palette.frame, 0);
    lv_obj_set_style_bg_opa(ui.screen, LV_OPA_COVER, 0);

    // Panels inside the glass, separated by box-art gutters.
    lv_obj_t *grid = lv_obj_create(ui.screen);
    lv_obj_remove_style_all(grid);
    lv_obj_clear_flag(grid, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(grid, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(grid, kDialRadius * 2, kDialRadius * 2);
    lv_obj_center(grid);
    lv_obj_set_style_radius(grid, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_clip_corner(grid, true, 0);
    lv_obj_set_style_bg_color(grid, gutter, 0);
    lv_obj_set_style_bg_opa(grid, LV_OPA_COVER, 0);
    lv_obj_set_name_static(grid, "moto-boot-grid");
    struct Panel {
        int x, y, w, h;
        lv_color_t color;
    };
    const int side = kDialRadius * 2;
    const int gap = 6;
    const int split_x = side / 2 - 40;
    const int split_y = side / 2 + 18;
    const MapPalette *dusk = dusk_palette();
    const Panel panels[] = {
        {0, 0, split_x, split_y, palette.background},
        {split_x + gap, 0, side - split_x - gap, split_y, palette.slab},
        {0, split_y + gap, side, side - split_y - gap, dusk->ground},
    };
    for(const Panel &panel : panels) {
        lv_obj_t *box = lv_obj_create(grid);
        lv_obj_remove_style_all(box);
        lv_obj_clear_flag(box, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_pos(box, panel.x, panel.y);
        lv_obj_set_size(box, panel.w, panel.h);
        lv_obj_set_style_bg_color(box, panel.color, 0);
        lv_obj_set_style_bg_opa(box, LV_OPA_COVER, 0);
    }
    // The radar panel: the HUD's ring with a route and the rider in it.
    lv_obj_t *radar = lv_arc_create(grid);
    lv_obj_remove_style_all(radar);
    lv_obj_clear_flag(radar, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(radar, 116, 116);
    lv_obj_set_pos(radar, split_x - 132, split_y - 140);
    lv_arc_set_bg_angles(radar, 0, 360);
    lv_arc_set_value(radar, 0);
    lv_obj_set_style_arc_color(radar, palette.road_major, LV_PART_MAIN);
    lv_obj_set_style_arc_width(radar, 5, LV_PART_MAIN);
    lv_obj_set_style_arc_opa(radar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_arc_opa(radar, LV_OPA_TRANSP, LV_PART_INDICATOR);
    // A route that turns, and the rider on it.
    static const lv_point_precise_t route_points[] = {
        {58, 104}, {58, 58}, {62, 50}, {104, 50}};
    lv_obj_t *route = lv_line_create(grid);
    lv_line_set_points(route, route_points, 4);
    lv_obj_set_pos(route, split_x - 132, split_y - 140);
    lv_obj_set_style_line_color(route, palette.route, 0);
    lv_obj_set_style_line_width(route, 7, 0);
    lv_obj_set_style_line_rounded(route, true, 0);
    lv_obj_t *rider = lv_obj_create(grid);
    lv_obj_remove_style_all(rider);
    lv_obj_clear_flag(rider, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(rider, px(34), px(36));
    lv_obj_set_pos(rider, split_x - 132 + 58 - px(17), split_y - 140 + 74);
    lv_obj_add_event_cb(rider, draw_vehicle_marker, LV_EVENT_DRAW_MAIN_END,
                        nullptr);
    // The safehouse panel.
    lv_obj_t *house = lv_obj_create(grid);
    lv_obj_remove_style_all(house);
    lv_obj_clear_flag(house, LV_OBJ_FLAG_CLICKABLE);
    // Right of the logo's last letter, so the stamp never half-covers it.
    lv_obj_set_size(house, 44, 44);
    lv_obj_set_pos(house, 348, 116);
    lv_obj_add_event_cb(house, draw_home_blip, LV_EVENT_DRAW_MAIN_END,
                        nullptr);
    // The city panel along the bottom: the same skyline the pages carry,
    // lifted so it stands on the panel's floor.
    lv_obj_t *city = make_skyline(grid, dusk);
    lv_obj_add_flag(city, LV_OBJ_FLAG_USER_1);
    lv_obj_set_pos(city, -(MOTO_UI_CANVAS_WIDTH / 2 - kDialRadius),
                   -(MOTO_UI_CANVAS_HEIGHT / 2 - kDialRadius) - 6);
    lv_obj_add_flag(grid, LV_OBJ_FLAG_HIDDEN);
    ui.boot_grid = grid;

    // The logo, stamped over the panels: cased letters with a hard drop.
    lv_obj_t *logo = make_layer(ui.screen);
    // A poster in both themes: GTA III's cream logo cased in black. Ink
    // letters by day disappeared into the dark panels.
    const lv_color_t letter = kLimitFace;
    const lv_color_t casing = kOutlineBlack;
    const struct {
        const char *text;
        int y;
        int spacing;
    } words[] = {{"MOTO", -px(50), px(2)}, {"GPS", px(58), px(6)}};
    for(const auto &word : words) {
        lv_obj_t *drop = make_label(logo, &moto_font_wordmark_132,
                                    kOutlineBlack, word.text);
        lv_obj_set_style_text_letter_space(drop, word.spacing, 0);
        lv_obj_align(drop, LV_ALIGN_CENTER, px(5), word.y + px(5));
        lv_obj_t *face = make_label(logo, &moto_font_wordmark_132, letter,
                                    word.text);
        lv_obj_set_style_text_outline_stroke_color(face, casing, 0);
        lv_obj_set_style_text_outline_stroke_width(face, px(3), 0);
        lv_obj_set_style_text_outline_stroke_opa(face, LV_OPA_COVER, 0);
        lv_obj_set_style_text_letter_space(face, word.spacing, 0);
        lv_obj_align(face, LV_ALIGN_CENTER, 0, word.y);
    }
    lv_obj_add_flag(logo, LV_OBJ_FLAG_HIDDEN);
    ui.boot_logo = logo;

    // The loading bar: a cased track that fills in hard steps. It measures
    // the boot hold itself (app_main waits 1.25 s before building the UI);
    // nothing else is loading, so it claims nothing else.
    lv_obj_t *bar = lv_obj_create(ui.screen);
    lv_obj_remove_style_all(bar);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(bar, px(140), px(12));
    lv_obj_align(bar, LV_ALIGN_CENTER, 0, px(136));
    lv_obj_set_style_bg_color(bar, kOutlineBlack, 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(bar, kLimitFace, 0);
    lv_obj_set_style_border_width(bar, 2, 0);
    lv_obj_set_style_border_opa(bar, LV_OPA_COVER, 0);
    lv_obj_set_name_static(bar, "moto-boot-bar");
    lv_obj_t *fill = lv_obj_create(bar);
    lv_obj_remove_style_all(fill);
    lv_obj_set_pos(fill, 3, 3);
    lv_obj_set_size(fill, 0, px(12) - 6);
    lv_obj_set_style_bg_color(fill, kHudPink, 0);
    lv_obj_set_style_bg_opa(fill, LV_OPA_COVER, 0);
    lv_obj_add_flag(bar, LV_OBJ_FLAG_HIDDEN);
    ui.boot_bar = bar;
    ui.boot_bar_fill = fill;

    ui.boot_started_ms = lv_tick_get();
    ui.boot_timer = lv_timer_create(boot_tick, 40, nullptr);
}

void moto_ui_gta::show_power_off_screen(void) {
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

void moto_ui_gta::create(void) {
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
    // The safehouse rides above the waiting screen too: with no route the
    // rider still sees which way home is, pinned to the rim. Both layers are
    // full-canvas at (0,0), so the blip keeps its coordinates.
    if(ui.nav_home_blip != nullptr) {
        lv_obj_set_parent(ui.nav_home_blip, ui.pages[MOTO_UI_PAGE_NAVIGATION]);
        lv_obj_move_foreground(ui.nav_home_blip);
    }
    create_performance_page();
    create_lean_page();
    create_gg_page();
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
    moto_ui_gta::set_music_state(&initial_music);

    moto_ui_state_t initial{};
    initial.mode = MOTO_UI_ACQUIRING_FIX;
    initial.page = MOTO_UI_PAGE_NAVIGATION;
    initial.maneuver = MOTO_MANEUVER_STRAIGHT;
    initial.traffic = MOTO_TRAFFIC_UNKNOWN;
    moto_ui_gta::set_state(&initial);
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
    if(ui.lean_scene != nullptr) {
        lv_obj_set_style_bg_color(ui.lean_scene, ui.palette.ground, 0);
        lv_obj_invalidate(ui.lean_scene);
    }
    if(ui.nav_lifecycle != nullptr) {
        lv_obj_set_style_bg_color(ui.nav_lifecycle, ui.palette.frame, 0);
    }
    // The cover's gutter and its paper panels; the dusk panel and the pager
    // are the same in both themes.
    if(ui.lifecycle_cover[0] != nullptr) {
        lv_obj_set_style_bg_color(ui.lifecycle_cover[0], ui.palette.accent, 0);
    }
    if(ui.lifecycle_cover[1] != nullptr) {
        lv_obj_set_style_border_color(ui.lifecycle_cover[1],
                                      ui.palette.accent, 0);
    }
    if(ui.lifecycle_panels[0] != nullptr) {
        lv_obj_set_style_bg_color(ui.lifecycle_panels[0],
                                  ui.palette.background, 0);
    }
    if(ui.lifecycle_panels[1] != nullptr) {
        lv_obj_set_style_bg_color(ui.lifecycle_panels[1], ui.palette.ground,
                                  0);
    }
    // The Connected headline's pink follows the paper too.
    if(ui.nav_lifecycle_title != nullptr &&
       ui.lifecycle_visual == LifecycleVisual::Connected) {
        lv_obj_set_style_text_color(ui.nav_lifecycle_title, ui.palette.hud, 0);
    }

    for(std::uint8_t i = 0; i < ui.nav_building_footprint_count; ++i) {
        const bool landmark = ui.nav_building_spans[i].building_class == 1U;
        lv_obj_set_style_line_color(
            ui.nav_buildings[i],
            landmark ? ui.palette.building_landmark : ui.palette.building, 0);
    }
    for(std::uint8_t i = 0; i < ui.nav_road_polyline_count; ++i) {
        lv_obj_set_style_line_color(
            ui.nav_roads[i],
            road_stroke(ui.palette, ui.nav_road_spans[i].road_class).color, 0);
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

void moto_ui_gta::set_theme(moto_ui_theme_t theme) {
    if(!ui.live) return;
    ui.theme = theme;
    // AUTO resolves through the phone's flag in one place; explicit themes
    // ignore the flag.
    ui.palette = palette_for_theme(theme, ui.ambient_night != 0);
    if(ui.nav_map_background != nullptr) apply_theme_colors();
}

void moto_ui_gta::set_show_buildings(uint8_t visible) {
    if(!ui.live) return;
    const bool show = visible != 0;
    // The phone re-sends settings on every session and after every echo;
    // a repeat must not invalidate the map layer.
    if(ui.show_buildings == show) return;
    ui.show_buildings = show;
    apply_building_visibility();
}

void moto_ui_gta::set_ambient_night(uint8_t is_night) {
    if(!ui.live) return;
    const bool night = is_night != 0;
    // The phone may repeat this on every session; a repeat must not repaint.
    if(ui.ambient_night == night) return;
    ui.ambient_night = night;
    if(ui.theme == MOTO_UI_THEME_AUTO) moto_ui_gta::set_theme(ui.theme);
}

void moto_ui_gta::set_state(const moto_ui_state_t *state) {
    if(state == nullptr || !ui.live) return;
    ui.nav_speed_kph = state->speed_kph;
    // The envelope is a record of the ride, not of the time spent looking.
    accumulate_gg(state);
    show_page(state->page);
    // Hidden pages do not need to be invalidated. Page changes immediately
    // apply a fresh snapshot through PhoneNavBridge, so this keeps every page
    // correct while avoiding three full page redraws per navigation update.
    switch(ui.page) {
        case MOTO_UI_PAGE_NAVIGATION: update_navigation(state); break;
        case MOTO_UI_PAGE_PERFORMANCE: update_performance(state); break;
        case MOTO_UI_PAGE_LEAN: update_lean(state); break;
        case MOTO_UI_PAGE_COMPASS: update_compass(state); break;
        case MOTO_UI_PAGE_SPEED: update_gg_view(state); break;
        case MOTO_UI_PAGE_MUSIC:
        case MOTO_UI_PAGE_COUNT: break;
    }
}

void moto_ui_gta::show_home_error(void) {
    if(!ui.live) return;
    if(ui.nav_home_hint == nullptr || ui.nav_home_hint_timer == nullptr) {
        return;
    }
    lv_obj_remove_flag(ui.nav_home_hint, LV_OBJ_FLAG_HIDDEN);
    lv_timer_reset(ui.nav_home_hint_timer);
    lv_timer_resume(ui.nav_home_hint_timer);
}

void moto_ui_gta::set_motion_state(const moto_ui_state_t *state) {
    if(state == nullptr || !ui.live ||
       ui.pages[MOTO_UI_PAGE_NAVIGATION] == nullptr) {
        return;
    }
    accumulate_gg(state);
    if(state->page != ui.page) return;
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
    } else if(ui.page == MOTO_UI_PAGE_SPEED) {
        update_gg_view(state);
    }
}

void moto_ui_gta::set_phone_connection(
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

void moto_ui_gta::restore_phone_connection(
    moto_ui_phone_connection_t connection) {
    // A theme switch rebuilds the tree while the link is already up. Take the
    // state without the success flash, which is for a link that just arrived.
    if(!ui.live || ui.nav_lifecycle == nullptr) return;
    if(connection < MOTO_UI_PHONE_OFFLINE || connection > MOTO_UI_PHONE_ONLINE) {
        return;
    }
    ui.phone_connection = connection;
    ui.lifecycle_success_active = false;
    if(ui.nav_success_timer != nullptr) lv_timer_pause(ui.nav_success_timer);
    refresh_lifecycle();
}

void moto_ui_gta::set_reduce_motion(uint8_t reduce_motion) {
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

void moto_ui_gta::set_page(moto_ui_page_t page) {
    if(!ui.live) return;
    show_page(page, true);
}

moto_ui_page_t moto_ui_gta::get_page(void) {
    return ui.page;
}

void moto_ui_gta::set_page_change_callback(
    moto_page_change_callback_t callback, void *context) {
    ui.page_callback = callback;
    ui.page_callback_context = context;
}

void moto_ui_gta::set_music_state(const moto_music_state_t *state) {
    if(state == nullptr || !ui.live) return;
    ui.music = *state;
    moto::nav_ui::copy_display_text(ui.music_source_text,
                                    sizeof(ui.music_source_text),
                                    &moto_font_display_24, state->source_name,
                                    "PHONE MEDIA");
    moto::nav_ui::copy_display_text(ui.music_title_text,
                                    sizeof(ui.music_title_text),
                                    &moto_font_display_40_cjk,
                                    state->track_title, "--");
    moto::nav_ui::copy_display_text(ui.music_artist_text,
                                    sizeof(ui.music_artist_text),
                                    &moto_font_display_24_cjk,
                                    state->artist_name, "--");
    ui.music.source_name = ui.music_source_text;
    ui.music.track_title = ui.music_title_text;
    ui.music.artist_name = ui.music_artist_text;
    update_music_view();
}

void moto_ui_gta::set_music_command_callback(
    moto_music_command_callback_t callback, void *context) {
    ui.music_callback = callback;
    ui.music_callback_context = context;
}

void moto_ui_gta::set_demo_active(uint8_t enabled) {
    if(!ui.live) return;
    ui.demo_active = enabled != 0;
    refresh_lifecycle();
}

void moto_ui_gta::set_demo_change_callback(
    moto_demo_change_callback_t callback, void *context) {
    ui.demo_callback = callback;
    ui.demo_callback_context = context;
}

void moto_ui_gta::set_home_callback(
    moto_home_shortcut_callback_t callback, void *context) {
    ui.home_callback = callback;
    ui.home_callback_context = context;
}

void moto_ui_gta::set_lean_calibration_callback(
    moto_lean_calibration_callback_t callback, void *context) {
    ui.lean_calibration_callback = callback;
    ui.lean_calibration_context = context;
}

void moto_ui_gta::set_performance_callback(
    moto_ui_performance_callback_t callback, void *context) {
    ui.perf_callback = callback;
    ui.perf_context = context;
}

void moto_ui_gta::set_clear_peaks_callback(
    moto_lean_clear_peaks_callback_t callback, void *context) {
    ui.lean_clear_callback = callback;
    ui.lean_clear_context = context;
}

void moto_ui_gta::set_lean_cal_state(
    moto_ui_lean_cal_state_t state, uint8_t value) {
    // Called from the sensor task; only atomics are touched here.
    ui.lean_cal_value = value;
    ui.lean_cal_state = static_cast<uint8_t>(state);
    ui.lean_countdown_pending = false;
    ui.lean_countdown_ms = 0;
}

void moto_ui_gta::debug_set_clock(moto_ui_debug_clock_t clock) {
    g_debug_clock = clock;
}

void moto_ui_gta::debug_set_enabled(uint8_t enabled) {
    g_debug_enabled = enabled != 0;
}

uint8_t moto_ui_gta::debug_enabled(void) {
    return g_debug_enabled ? 1U : 0U;
}

void moto_ui_gta::debug_set_hidden(uint32_t mask) {
    g_debug_hidden = mask;
    apply_debug_hidden();
}

uint32_t moto_ui_gta::debug_hidden(void) {
    return g_debug_hidden;
}

void moto_ui_gta::debug_reset(void) {
    g_debug_stats = {};
}

void moto_ui_gta::debug_stats(moto_ui_debug_stats_t *out) {
    if(out != nullptr) *out = g_debug_stats;
}

struct _lv_obj_t *moto_ui_gta::page_root(moto_ui_page_t page) {
    if(page < MOTO_UI_PAGE_NAVIGATION || page >= MOTO_UI_PAGE_COUNT) {
        return nullptr;
    }
    return ui.pages[page];
}

void moto_ui_gta::set_lean_flags(uint8_t cal_unverified,
                                           uint8_t imu_poll, uint8_t log_full,
                                           uint8_t log_dirty) {
    ui.lean_flag_cal = cal_unverified;
    ui.lean_flag_poll = imu_poll;
    ui.lean_flag_full = log_full;
    ui.lean_flag_dirty = log_dirty;
}
