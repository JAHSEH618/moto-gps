// Public moto_nav_ui_* entry points.
//
// Two interfaces share the round screen: the upstream one (OFFICIAL, black and
// white, moto_nav_ui_classic.cpp) and the GTA poster instrument (day, night
// and auto, moto_nav_ui_gta.cpp). Only one widget tree exists at a time. A
// theme change inside one family is a repaint; a change across families tears
// the live tree down, builds the other one and replays what the rider was
// looking at — page, link state, music, the last snapshot and every callback
// — so the switch looks like a theme change rather than a reboot.
//
// Everything here runs on the LVGL task except set_lean_cal_state() and
// set_lean_flags(), which the sensor task posts. Those only touch atomics.

#include "moto_nav_ui.h"

#include <atomic>
#include <cstdio>
#include <cstring>
#include <new>

#include "moto_nav_ui_backends.h"
#include "moto_nav_ui_memory.h"

namespace {

enum class Backend : std::uint8_t {
    Gta = 0,
    Classic,
};

Backend backend_for(moto_ui_theme_t theme) {
    return theme == MOTO_UI_THEME_OFFICIAL ? Backend::Classic : Backend::Gta;
}

struct Dispatch {
    Backend active = Backend::Gta;
    // True between create() and a terminal (boot / power-off) screen.
    bool live = false;
    moto_ui_theme_t boot_theme = MOTO_UI_THEME_GTA_NIGHT;
    moto_ui_theme_t theme = MOTO_UI_THEME_GTA_DAY;
    bool ambient_night = false;
    bool show_buildings = false;
    bool reduce_motion = false;
    bool demo_active = false;
    moto_ui_phone_connection_t phone_connection = MOTO_UI_PHONE_OFFLINE;
    bool has_state = false;
    bool has_music = false;

    moto_page_change_callback_t page_callback = nullptr;
    void *page_context = nullptr;
    moto_music_command_callback_t music_callback = nullptr;
    void *music_context = nullptr;
    moto_demo_change_callback_t demo_callback = nullptr;
    void *demo_context = nullptr;
    moto_home_shortcut_callback_t home_callback = nullptr;
    void *home_context = nullptr;
    moto_lean_calibration_callback_t calibration_callback = nullptr;
    void *calibration_context = nullptr;
    moto_lean_clear_peaks_callback_t clear_peaks_callback = nullptr;
    void *clear_peaks_context = nullptr;
    moto_ui_performance_callback_t performance_callback = nullptr;
    void *performance_context = nullptr;
};

Dispatch d;

// The replay copies: the last snapshot and track, with their strings. A few
// KiB, all zero-initialised so it can sit in PSRAM on the board, where
// internal RAM is the scarce pool. Only read on a theme-family switch.
struct Replay {
    moto_ui_state_t state;
    char road_name[64];
    char next_road_name[64];
    moto_music_state_t music;
    char music_source[32];
    char music_title[64];
    char music_artist[48];
};
MOTO_UI_EXT_RAM Replay r;

// Posted from the sensor task, so kept apart from the LVGL-task state.
std::atomic<std::uint8_t> g_flag_cal{0};
std::atomic<std::uint8_t> g_flag_poll{0};
std::atomic<std::uint8_t> g_flag_full{0};
std::atomic<std::uint8_t> g_flag_dirty{0};

const char *keep_text(char *buffer, std::size_t capacity, const char *text) {
    if(text == nullptr) return nullptr;
    std::snprintf(buffer, capacity, "%s", text);
    return buffer;
}

void install_callbacks() {
    if(d.active == Backend::Gta) {
        moto_ui_gta::set_page_change_callback(d.page_callback, d.page_context);
        moto_ui_gta::set_music_command_callback(d.music_callback,
                                                d.music_context);
        moto_ui_gta::set_demo_change_callback(d.demo_callback, d.demo_context);
        moto_ui_gta::set_home_callback(d.home_callback, d.home_context);
        moto_ui_gta::set_lean_calibration_callback(d.calibration_callback,
                                                   d.calibration_context);
        moto_ui_gta::set_clear_peaks_callback(d.clear_peaks_callback,
                                              d.clear_peaks_context);
        moto_ui_gta::set_performance_callback(d.performance_callback,
                                              d.performance_context);
    } else {
        moto_ui_classic::set_page_change_callback(d.page_callback,
                                                  d.page_context);
        moto_ui_classic::set_music_command_callback(d.music_callback,
                                                    d.music_context);
        moto_ui_classic::set_demo_change_callback(d.demo_callback,
                                                  d.demo_context);
    }
}

// Build the tree for d.active and bring it to everything the dispatcher has
// been told since create(). Called for a family switch only.
void rebuild_active() {
    const moto_ui_page_t page = d.has_state ? r.state.page
                                            : MOTO_UI_PAGE_NAVIGATION;
    if(d.active == Backend::Gta) {
        moto_ui_classic::teardown();
        moto_ui_gta::create();
        moto_ui_gta::set_ambient_night(d.ambient_night ? 1U : 0U);
        moto_ui_gta::set_theme(d.theme);
        moto_ui_gta::set_show_buildings(d.show_buildings ? 1U : 0U);
        moto_ui_gta::set_reduce_motion(d.reduce_motion ? 1U : 0U);
        moto_ui_gta::set_lean_flags(g_flag_cal.load(), g_flag_poll.load(),
                                    g_flag_full.load(), g_flag_dirty.load());
        moto_ui_gta::set_demo_active(d.demo_active ? 1U : 0U);
        moto_ui_gta::restore_phone_connection(d.phone_connection);
        if(d.has_music) moto_ui_gta::set_music_state(&r.music);
        if(d.has_state) moto_ui_gta::set_state(&r.state);
        moto_ui_gta::set_page(page);
    } else {
        moto_ui_gta::teardown();
        moto_ui_classic::create();
        moto_ui_classic::set_show_buildings(d.show_buildings ? 1U : 0U);
        moto_ui_classic::set_reduce_motion(d.reduce_motion ? 1U : 0U);
        moto_ui_classic::set_demo_active(d.demo_active ? 1U : 0U);
        moto_ui_classic::restore_phone_connection(d.phone_connection);
        if(d.has_music) moto_ui_classic::set_music_state(&r.music);
        if(d.has_state) moto_ui_classic::set_state(&r.state);
        moto_ui_classic::set_page(page);
    }
    install_callbacks();
}

// create() starts from a clean slate, as the header promises: callbacks and
// display flags registered before it are gone.
void reset_dispatch() {
    const moto_ui_theme_t boot_theme = d.boot_theme;
    d.~Dispatch();
    new(&d) Dispatch{};
    d.boot_theme = boot_theme;
}

}  // namespace

extern "C" void moto_nav_ui_set_boot_theme(moto_ui_theme_t theme) {
    d.boot_theme = theme;
    moto_ui_gta::set_boot_theme(theme);
}

extern "C" void moto_nav_ui_show_boot_screen(void) {
    d.live = false;
    moto_ui_battery::set_live(false);
    if(backend_for(d.boot_theme) == Backend::Classic) {
        moto_ui_gta::teardown();
        moto_ui_classic::show_boot_screen();
    } else {
        moto_ui_classic::teardown();
        moto_ui_gta::show_boot_screen();
    }
}

extern "C" void moto_nav_ui_show_power_off_screen(void) {
    d.live = false;
    moto_ui_battery::set_live(false);
    if(d.active == Backend::Classic) {
        moto_ui_classic::show_power_off_screen();
    } else {
        moto_ui_gta::show_power_off_screen();
    }
}

extern "C" void moto_nav_ui_create(void) {
    reset_dispatch();
    // Build the persisted family straight away, so a rider on the original
    // theme never sees one frame of the GTA tree while the phone reconnects.
    // Inside GTA the palette still waits for the live theme, as before.
    d.active = backend_for(d.boot_theme);
    if(d.active == Backend::Classic) {
        d.theme = MOTO_UI_THEME_OFFICIAL;
        moto_ui_gta::teardown();
        moto_ui_classic::create();
    } else {
        moto_ui_classic::teardown();
        moto_ui_gta::create();
    }
    d.live = true;
    moto_ui_battery::set_live(true);
}

extern "C" void moto_nav_ui_set_theme(moto_ui_theme_t theme) {
    if(!d.live) return;
    if(theme < MOTO_UI_THEME_OFFICIAL || theme > MOTO_UI_THEME_AUTO) return;
    d.theme = theme;
    const Backend wanted = backend_for(theme);
    if(wanted != d.active) {
        d.active = wanted;
        rebuild_active();
        return;
    }
    if(d.active == Backend::Gta) moto_ui_gta::set_theme(theme);
}

extern "C" void moto_nav_ui_set_ambient_night(uint8_t is_night) {
    if(!d.live) return;
    d.ambient_night = is_night != 0;
    if(d.active == Backend::Gta) moto_ui_gta::set_ambient_night(is_night);
}

extern "C" void moto_nav_ui_set_show_buildings(uint8_t visible) {
    if(!d.live) return;
    d.show_buildings = visible != 0;
    if(d.active == Backend::Gta) {
        moto_ui_gta::set_show_buildings(visible);
    } else {
        moto_ui_classic::set_show_buildings(visible);
    }
}

extern "C" void moto_nav_ui_set_state(const moto_ui_state_t *state) {
    if(state == nullptr || !d.live) return;
    r.state = *state;
    r.state.road_name = keep_text(r.road_name, sizeof(r.road_name),
                                  state->road_name);
    r.state.next_road_name = keep_text(
        r.next_road_name, sizeof(r.next_road_name), state->next_road_name);
    d.has_state = true;
    if(d.active == Backend::Gta) {
        moto_ui_gta::set_state(state);
    } else {
        moto_ui_classic::set_state(state);
    }
}

extern "C" void moto_nav_ui_set_motion_state(const moto_ui_state_t *state) {
    if(state == nullptr || !d.live) return;
    if(d.active == Backend::Gta) {
        moto_ui_gta::set_motion_state(state);
    } else {
        moto_ui_classic::set_motion_state(state);
    }
}

extern "C" void moto_nav_ui_set_phone_connection(
    moto_ui_phone_connection_t connection) {
    if(!d.live) return;
    if(connection < MOTO_UI_PHONE_OFFLINE || connection > MOTO_UI_PHONE_ONLINE) {
        return;
    }
    d.phone_connection = connection;
    if(d.active == Backend::Gta) {
        moto_ui_gta::set_phone_connection(connection);
    } else {
        moto_ui_classic::set_phone_connection(connection);
    }
}

extern "C" void moto_nav_ui_set_reduce_motion(uint8_t reduce_motion) {
    if(!d.live) return;
    d.reduce_motion = reduce_motion != 0;
    if(d.active == Backend::Gta) {
        moto_ui_gta::set_reduce_motion(reduce_motion);
    } else {
        moto_ui_classic::set_reduce_motion(reduce_motion);
    }
}

extern "C" void moto_nav_ui_set_page(moto_ui_page_t page) {
    if(!d.live) return;
    if(d.active == Backend::Gta) {
        moto_ui_gta::set_page(page);
    } else {
        moto_ui_classic::set_page(page);
    }
}

extern "C" moto_ui_page_t moto_nav_ui_get_page(void) {
    return d.active == Backend::Gta ? moto_ui_gta::get_page()
                                    : moto_ui_classic::get_page();
}

extern "C" void moto_nav_ui_set_page_change_callback(
    moto_page_change_callback_t callback, void *context) {
    d.page_callback = callback;
    d.page_context = context;
    install_callbacks();
}

extern "C" void moto_nav_ui_set_music_state(const moto_music_state_t *state) {
    if(state == nullptr || !d.live) return;
    r.music = *state;
    r.music.source_name = keep_text(r.music_source, sizeof(r.music_source),
                                    state->source_name);
    r.music.track_title = keep_text(r.music_title, sizeof(r.music_title),
                                    state->track_title);
    r.music.artist_name = keep_text(r.music_artist, sizeof(r.music_artist),
                                    state->artist_name);
    d.has_music = true;
    if(d.active == Backend::Gta) {
        moto_ui_gta::set_music_state(state);
    } else {
        moto_ui_classic::set_music_state(state);
    }
}

extern "C" void moto_nav_ui_set_music_command_callback(
    moto_music_command_callback_t callback, void *context) {
    d.music_callback = callback;
    d.music_context = context;
    install_callbacks();
}

extern "C" void moto_nav_ui_set_demo_active(uint8_t enabled) {
    if(!d.live) return;
    d.demo_active = enabled != 0;
    if(d.active == Backend::Gta) {
        moto_ui_gta::set_demo_active(enabled);
    } else {
        moto_ui_classic::set_demo_active(enabled);
    }
}

extern "C" void moto_nav_ui_set_demo_change_callback(
    moto_demo_change_callback_t callback, void *context) {
    d.demo_callback = callback;
    d.demo_context = context;
    install_callbacks();
}

extern "C" void moto_nav_ui_set_home_callback(
    moto_home_shortcut_callback_t callback, void *context) {
    d.home_callback = callback;
    d.home_context = context;
    install_callbacks();
}

extern "C" void moto_nav_ui_set_lean_calibration_callback(
    moto_lean_calibration_callback_t callback, void *context) {
    d.calibration_callback = callback;
    d.calibration_context = context;
    install_callbacks();
}

extern "C" void moto_nav_ui_set_clear_peaks_callback(
    moto_lean_clear_peaks_callback_t callback, void *context) {
    d.clear_peaks_callback = callback;
    d.clear_peaks_context = context;
    install_callbacks();
}

extern "C" void moto_nav_ui_set_performance_callback(
    moto_ui_performance_callback_t callback, void *context) {
    d.performance_callback = callback;
    d.performance_context = context;
    install_callbacks();
}

// Only the GTA tree has a 0-100 page, but its target and bests are kept
// outside the tree, so they are forwarded whichever tree is live.
extern "C" void moto_nav_ui_set_performance_target(uint16_t target_kph) {
    moto_ui_gta::set_performance_target(target_kph);
}

extern "C" void moto_nav_ui_set_performance_bests(const uint32_t *best_ms) {
    moto_ui_gta::set_performance_bests(best_ms);
}

extern "C" void moto_nav_ui_set_lean_cal_state(moto_ui_lean_cal_state_t state,
                                               uint8_t value) {
    // Only the GTA tree has a Lean page; its post is atomics-only, so it is
    // safe whichever tree is live.
    moto_ui_gta::set_lean_cal_state(state, value);
}

extern "C" void moto_nav_ui_set_lean_flags(uint8_t cal_unverified,
                                           uint8_t imu_poll, uint8_t log_full,
                                           uint8_t log_dirty) {
    g_flag_cal.store(cal_unverified);
    g_flag_poll.store(imu_poll);
    g_flag_full.store(log_full);
    g_flag_dirty.store(log_dirty);
    moto_ui_gta::set_lean_flags(cal_unverified, imu_poll, log_full, log_dirty);
}

extern "C" void moto_nav_ui_set_battery(moto_ui_power_source_t source,
                                        uint8_t percent) {
    moto_ui_battery::post(source, percent);
}

extern "C" void moto_nav_ui_show_home_error(void) {
    if(!d.live || d.active != Backend::Gta) return;
    moto_ui_gta::show_home_error();
}

extern "C" struct _lv_obj_t *moto_nav_ui_page_root(moto_ui_page_t page) {
    return d.active == Backend::Gta ? moto_ui_gta::page_root(page)
                                    : moto_ui_classic::page_root(page);
}

extern "C" void moto_nav_ui_debug_set_clock(moto_ui_debug_clock_t clock) {
    moto_ui_gta::debug_set_clock(clock);
}

extern "C" void moto_nav_ui_debug_set_enabled(uint8_t enabled) {
    moto_ui_gta::debug_set_enabled(enabled);
}

extern "C" uint8_t moto_nav_ui_debug_enabled(void) {
    return moto_ui_gta::debug_enabled();
}

extern "C" void moto_nav_ui_debug_set_hidden(uint32_t mask) {
    moto_ui_gta::debug_set_hidden(mask);
}

extern "C" uint32_t moto_nav_ui_debug_hidden(void) {
    return moto_ui_gta::debug_hidden();
}

extern "C" void moto_nav_ui_debug_reset(void) {
    moto_ui_gta::debug_reset();
}

extern "C" void moto_nav_ui_debug_stats(moto_ui_debug_stats_t *out) {
    moto_ui_gta::debug_stats(out);
}
