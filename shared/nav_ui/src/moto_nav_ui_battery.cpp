// The battery ring (DESIGN_V2 §2.9).
//
// Apple's battery ring, and the instrument's whole bezel: outside the band a
// green stroke fills clockwise from twelve o'clock over a dim track of the
// same hue, red at 20% and below, and on USB the Apple Watch charging bolt
// sits at twelve in a gap of the ring. Full charge is a full ring.
//
// It is one object on LVGL's top layer, so every page of both trees carries it
// and a theme switch never rebuilds it. The board's power task posts readings
// from any task; a timer on the LVGL task applies them and repaints only when
// what is drawn changes.

#include <algorithm>
#include <atomic>
#include <cmath>

#include "lvgl.h"
#include "moto_nav_ui_backends.h"

namespace moto_ui_battery {
namespace {

// Bezel 222..233 (moto_nav_ui_gta.cpp). The ring takes all of it but a 2 px
// black seam against the band: r 224..232, out to the glass edge.
constexpr int kBandOuter = 222;
constexpr int kGlassEdge = 233;
constexpr int kRingOuter = kGlassEdge;
constexpr int kRingWidth = 9;
constexpr int kCenterX = MOTO_UI_CANVAS_WIDTH / 2;
constexpr int kCenterY = MOTO_UI_CANVAS_HEIGHT / 2;
// LVGL angles run clockwise from three o'clock.
constexpr double kTwelve = 270.0;
// Half of the gap that holds the bolt: 16 px a side at r=228, which leaves
// black between the 10 px bolt and the ring's round caps. Arc angles are
// whole degrees in this LVGL build, so the gap is too.
constexpr double kBoltGapDeg = 4.0;
// The bolt's centre: on the ring, held 6 px inside the canvas edge.
constexpr double kBoltCenterY = 6.0;
constexpr std::uint8_t kLowPercent = 20;
constexpr std::uint32_t kApplyPeriodMs = 200;

// Apple's dark-appearance system colours: the bezel is black in every theme.
constexpr lv_color_t kGreen = LV_COLOR_MAKE(0x30, 0xD1, 0x58);
constexpr lv_color_t kRed = LV_COLOR_MAKE(0xFF, 0x45, 0x3A);
constexpr lv_color_t kBezel = LV_COLOR_MAKE(0x00, 0x00, 0x00);
// The track is the stroke's own hue at 30% over the bezel, mixed ahead of
// time: opaque, so the pieces of a split arc cannot double-blend where they
// meet.
constexpr lv_opa_t kTrackMix = 77;

struct Shown {
    moto_ui_power_source_t source = MOTO_UI_POWER_UNKNOWN;
    std::uint8_t percent = 0;
};

// Posted by the power task.
std::atomic<std::uint8_t> g_source{MOTO_UI_POWER_UNKNOWN};
std::atomic<std::uint8_t> g_percent{0};

lv_obj_t *g_ring = nullptr;
lv_timer_t *g_timer = nullptr;
bool g_live = false;
Shown g_shown;

bool visible(const Shown &shown) {
    return g_live && shown.source != MOTO_UI_POWER_UNKNOWN;
}

// Same culls as the instrument frame: most dirty areas sit inside the disc,
// and an arc is the costliest primitive on the rim.
bool clip_misses_ring(const lv_layer_t *layer) {
    const lv_area_t &clip = layer->_clip_area;
    const double x1 = clip.x1 - kCenterX;
    const double x2 = clip.x2 + 1 - kCenterX;
    const double y1 = clip.y1 - kCenterY;
    const double y2 = clip.y2 + 1 - kCenterY;
    const double nx = x1 > 0.0 ? x1 : (x2 < 0.0 ? x2 : 0.0);
    const double ny = y1 > 0.0 ? y1 : (y2 < 0.0 ? y2 : 0.0);
    const double fx = std::max(std::fabs(x1), std::fabs(x2));
    const double fy = std::max(std::fabs(y1), std::fabs(y2));
    const double nearest = std::sqrt(nx * nx + ny * ny);
    const double farthest = std::sqrt(fx * fx + fy * fy);
    return farthest < kBandOuter - 1 || nearest > kGlassEdge;
}

// A stroke from `start` clockwise through `sweep` degrees. LVGL disagrees
// between versions about arcs that cross 0° or span 360°, so the stroke is
// split into pieces that do neither. Caps are drawn only at the two ends.
void stroke(lv_layer_t *layer, int radius, int width, double start,
            double sweep, lv_color_t color, bool caps) {
    if(sweep <= 0.0) return;
    lv_draw_arc_dsc_t dsc;
    lv_draw_arc_dsc_init(&dsc);
    dsc.center = {kCenterX, kCenterY};
    dsc.radius = radius;
    dsc.width = width;
    dsc.color = color;
    dsc.opa = LV_OPA_COVER;
    double from = std::fmod(start, 360.0);
    if(from < 0.0) from += 360.0;
    double left = std::min(sweep, 360.0);
    bool first = true;
    while(left > 0.0) {
        const double piece = std::min({left, 360.0 - from, 180.0});
        const bool last = piece >= left;
        // Whole degrees here: round rather than let the cast truncate.
        dsc.start_angle = static_cast<lv_value_precise_t>(std::lround(from));
        dsc.end_angle =
            static_cast<lv_value_precise_t>(std::lround(from + piece));
        // A round cap on an inner joint would overlap its neighbour in the
        // same colour, so only the outer ends get one.
        dsc.rounded = caps && (first || last) ? 1 : 0;
        lv_draw_arc(layer, &dsc);
        first = false;
        left -= piece;
        from += piece;
        if(from >= 360.0) from -= 360.0;
    }
}

// Apple Watch's charging bolt, upright at twelve o'clock in the ring's gap:
// two wedges meeting across the middle, the upper one pointing down-left, the
// lower one up-right. Ten pixels tall, inside r 222..232.
void draw_bolt(lv_layer_t *layer) {
    const double cx = kCenterX;
    const double cy = kBoltCenterY;
    lv_draw_triangle_dsc_t triangle;
    lv_draw_triangle_dsc_init(&triangle);
    triangle.color = kGreen;
    triangle.opa = LV_OPA_COVER;
    const auto at = [cx, cy](double x, double y) {
        return lv_point_precise_t{static_cast<lv_value_precise_t>(cx + x),
                                  static_cast<lv_value_precise_t>(cy + y)};
    };
    triangle.p[0] = at(2.0, -5.0);
    triangle.p[1] = at(-5.0, 1.0);
    triangle.p[2] = at(1.0, 1.0);
    lv_draw_triangle(layer, &triangle);
    triangle.p[0] = at(-1.0, -1.0);
    triangle.p[1] = at(5.0, -1.0);
    triangle.p[2] = at(-2.0, 5.0);
    lv_draw_triangle(layer, &triangle);
}

void draw_event(lv_event_t *event) {
    if(!visible(g_shown)) return;
    lv_layer_t *layer = lv_event_get_layer(event);
    if(clip_misses_ring(layer)) return;

    const bool external = g_shown.source == MOTO_UI_POWER_EXTERNAL;
    // Charging is always green, as on the phone; on the battery the stroke
    // turns red at 20% and below.
    const lv_color_t color =
        !external && g_shown.percent <= kLowPercent ? kRed : kGreen;
    const lv_color_t track = lv_color_mix(color, kBezel, kTrackMix);

    // The ring owns the bezel: clear it first, so the tips of the pages'
    // indices that reach past the band (the Lean zero bar at twelve) cannot
    // show through the track or crowd the bolt.
    stroke(layer, kGlassEdge, kGlassEdge - kBandOuter, 0.0, 360.0, kBezel,
           false);

    // On USB the ring opens round the bolt; the stroke starts after the gap.
    const double gap = external ? kBoltGapDeg : 0.0;
    const double start = kTwelve + gap;
    const double span = 360.0 - 2.0 * gap;
    const double fill = span * g_shown.percent / 100.0;
    const bool full = g_shown.percent >= 100;

    if(!full) {
        stroke(layer, kRingOuter, kRingWidth, start + fill, span - fill,
               track, external);
    }
    if(g_shown.percent > 0) {
        // A full ring has no ends, so no caps; a ring with a gap keeps them.
        stroke(layer, kRingOuter, kRingWidth, start, fill, color,
               !full || external);
    } else {
        // Apple's empty battery still shows a dot at twelve.
        stroke(layer, kRingOuter, kRingWidth, start, 1.0, color, true);
    }
    if(external) draw_bolt(layer);
}

void apply(lv_timer_t *) {
    Shown next;
    const std::uint8_t source = g_source.load();
    next.source = source <= MOTO_UI_POWER_EXTERNAL
                      ? static_cast<moto_ui_power_source_t>(source)
                      : MOTO_UI_POWER_UNKNOWN;
    next.percent = std::min<std::uint8_t>(g_percent.load(), 100);
    if(next.source == g_shown.source && next.percent == g_shown.percent) {
        return;
    }
    const bool was_visible = visible(g_shown);
    g_shown = next;
    if(g_ring == nullptr) return;
    // The ring spans the canvas, so any change repaints it whole. Readings
    // move by a percent every few minutes; that is one frame each.
    if(was_visible || visible(g_shown)) lv_obj_invalidate(g_ring);
}

void ensure_ring() {
    if(g_ring != nullptr) return;
    g_ring = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(g_ring);
    lv_obj_set_pos(g_ring, 0, 0);
    lv_obj_set_size(g_ring, MOTO_UI_CANVAS_WIDTH, MOTO_UI_CANVAS_HEIGHT);
    // Touches belong to the page underneath.
    lv_obj_remove_flag(g_ring, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(g_ring, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(g_ring, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_set_name(g_ring, "moto-battery-ring");
    lv_obj_add_event_cb(g_ring, draw_event, LV_EVENT_DRAW_MAIN, nullptr);
    g_timer = lv_timer_create(apply, kApplyPeriodMs, nullptr);
}

}  // namespace

void set_live(bool live) {
    if(live) ensure_ring();
    if(g_ring == nullptr) return;
    const bool was_visible = visible(g_shown);
    g_live = live;
    // A terminal screen must not wait for the timer: the next frame it draws
    // is the last one before the PMIC cuts power.
    if(was_visible != visible(g_shown)) lv_obj_invalidate(g_ring);
}

void post(moto_ui_power_source_t source, uint8_t percent) {
    g_percent.store(percent);
    g_source.store(static_cast<std::uint8_t>(source));
}

}  // namespace moto_ui_battery
