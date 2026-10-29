#pragma once

// The two round-screen implementations behind the public moto_nav_ui_* API.
//
// moto_nav_ui.cpp owns the public entry points and decides which backend is
// live: MOTO_UI_THEME_OFFICIAL is the upstream interface as it was forked
// (black ground, white route, Montserrat, four pages), every other theme is
// the GTA poster instrument. Only one tree exists at a time, so a backend is
// called only while it is the active one, and a theme switch tears one tree
// down before the other is created.
//
// Callbacks are not stored here: each create() resets its backend's slots, so
// the dispatcher keeps the registrations and re-installs them after a switch.

#include "moto_nav_ui.h"

// The lean page's `ui dbg` profiling (clock, per-layer timers, hide mask). On
// unless a build says otherwise: the host tests and the Web build keep it, and
// the firmware's release overlay (CONFIG_MOTO_BENCH_TOOLS_DISABLE) compiles
// with MOTO_UI_DEBUG_TOOLS=0, which leaves the moto_nav_ui_debug_* calls as
// no-ops.
#ifndef MOTO_UI_DEBUG_TOOLS
#define MOTO_UI_DEBUG_TOOLS 1
#endif

namespace moto_ui_gta {
void set_boot_theme(moto_ui_theme_t theme);
void show_boot_screen(void);
void show_power_off_screen(void);
void create(void);
void set_state(const moto_ui_state_t *state);
void set_theme(moto_ui_theme_t theme);
void set_ambient_night(uint8_t is_night);
void set_show_buildings(uint8_t visible);
void set_motion_state(const moto_ui_state_t *state);
void set_phone_connection(moto_ui_phone_connection_t connection);
/** Apply a known link state to a freshly created tree, without the
    "connected" flash. */
void restore_phone_connection(moto_ui_phone_connection_t connection);
void set_reduce_motion(uint8_t reduce_motion);
void set_page(moto_ui_page_t page);
moto_ui_page_t get_page(void);
void set_page_change_callback(moto_page_change_callback_t callback,
                              void *context);
void set_music_state(const moto_music_state_t *state);
void set_music_command_callback(moto_music_command_callback_t callback,
                                void *context);
void set_demo_active(uint8_t enabled);
void set_demo_change_callback(moto_demo_change_callback_t callback,
                              void *context);
void set_home_callback(moto_home_shortcut_callback_t callback, void *context);
void set_lean_calibration_callback(moto_lean_calibration_callback_t callback,
                                   void *context);
void set_performance_callback(moto_ui_performance_callback_t callback,
                              void *context);
void set_performance_target(uint16_t target_kph);
void set_performance_bests(const uint32_t *best_ms);
void set_clear_peaks_callback(moto_lean_clear_peaks_callback_t callback,
                              void *context);
void set_lean_cal_state(moto_ui_lean_cal_state_t state, uint8_t value);
void set_lean_flags(uint8_t cal_unverified, uint8_t imu_poll,
                    uint8_t log_full, uint8_t log_dirty);
struct _lv_obj_t *page_root(moto_ui_page_t page);
#if MOTO_UI_DEBUG_TOOLS
void debug_set_clock(moto_ui_debug_clock_t clock);
void debug_set_enabled(uint8_t enabled);
uint8_t debug_enabled(void);
void debug_set_hidden(uint32_t mask);
uint32_t debug_hidden(void);
void debug_reset(void);
void debug_stats(moto_ui_debug_stats_t *out);
#endif
void show_home_error(void);
/** Delete the backend's timers and forget its tree without drawing anything,
    so the other backend can take the screen. */
void teardown(void);
}  // namespace moto_ui_gta

// The battery ring. It belongs to neither backend: it sits on LVGL's top layer
// above whichever tree is live, so the dispatcher only says when the
// instrument is up (create) and when a terminal screen takes over.
namespace moto_ui_battery {
void set_live(bool live);
void post(moto_ui_power_source_t source, uint8_t percent);
}  // namespace moto_ui_battery

// The upstream interface. It knows nothing about attitude, settings, home or
// lean calibration; it shows navigation, speed, compass and music, in that
// order, and maps a request for the Performance or Lean page to navigation.
namespace moto_ui_classic {
void show_boot_screen(void);
void show_power_off_screen(void);
void create(void);
void set_state(const moto_ui_state_t *state);
void set_show_buildings(uint8_t visible);
void set_motion_state(const moto_ui_state_t *state);
void set_phone_connection(moto_ui_phone_connection_t connection);
/** Apply a known link state to a freshly created tree, without the
    "connected" flash. */
void restore_phone_connection(moto_ui_phone_connection_t connection);
void set_reduce_motion(uint8_t reduce_motion);
void set_page(moto_ui_page_t page);
moto_ui_page_t get_page(void);
void set_page_change_callback(moto_page_change_callback_t callback,
                              void *context);
void set_music_state(const moto_music_state_t *state);
void set_music_command_callback(moto_music_command_callback_t callback,
                                void *context);
void set_demo_active(uint8_t enabled);
void set_demo_change_callback(moto_demo_change_callback_t callback,
                              void *context);
struct _lv_obj_t *page_root(moto_ui_page_t page);
void teardown(void);
}  // namespace moto_ui_classic
