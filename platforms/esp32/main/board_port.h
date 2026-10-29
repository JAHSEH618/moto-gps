#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "lvgl.h"
#include "moto_nav_ui.h"

#ifdef __cplusplus
extern "C" {
#endif

enum {
  MOTO_DISPLAY_WIDTH = MOTO_UI_CANVAS_WIDTH,
  MOTO_DISPLAY_HEIGHT = MOTO_UI_CANVAS_HEIGHT,
  MOTO_DISPLAY_BITS_PER_PIXEL = 16,
};

/**
 * Initialize only board/platform facilities required by the shared UI.
 *
 * A real implementation must:
 * - initialize LVGL exactly once;
 * - create one display matching MOTO_UI_CANVAS_WIDTH x
 *   MOTO_UI_CANVAS_HEIGHT using LV_COLOR_FORMAT_RGB565;
 * - attach the physical panel flush callback and compatible draw buffers;
 * - provide LVGL's millisecond tick;
 * - run lv_timer_handler from one board-owned FreeRTOS task; and
 * - serialize all other LVGL access through board_port_lock/unlock.
 *
 * It must not contain route parsing, navigation, rerouting, or UI layout logic.
 */
esp_err_t board_port_init(void);

/** Return the initialized LVGL display, or NULL before successful init. */
lv_display_t *board_port_get_display(void);

/**
 * Reveal the AMOLED after the caller has drawn the first intentional frame.
 * board_port_init keeps the panel dark so the LVGL default white screen can
 * never appear between panel reset and the boot animation.
 */
esp_err_t board_port_reveal_display(void);

/**
 * Lock LVGL for calls made outside the board-owned LVGL task.
 * timeout_ms == UINT32_MAX means wait indefinitely.
 */
bool board_port_lock(uint32_t timeout_ms);

/** Release a lock previously acquired with board_port_lock. */
void board_port_unlock(void);

/* Bench frame-time window. Not built into a release image
   (CONFIG_MOTO_BENCH_TOOLS_DISABLE): there the timing callbacks are never
   registered and these three do not exist. */
/** Reset the display frame-time window (bench page measurements). */
void board_port_reset_display_timing(void);
/** Print the current window on demand, tagged `manual`. */
void board_port_log_display_timing(void);
/** Cumulative render time and frame count since boot (lean-page profiling). */
void board_port_display_totals(uint64_t *render_us, uint32_t *frames);

/*
 * Burn-in and idle control (DESIGN_V2 §2.6; driven by burn_in.cpp).
 *
 * The orbit offset moves every flushed area by (dx, dy) panel pixels, so the
 * paper disc, the bezel and the top layer move as one and LVGL's own dirty
 * areas are untouched. Both must be even (the CO5300 two-pixel grid) and
 * within +/-2. Call with board_port_lock held; a change redraws the frame.
 */
void board_port_set_orbit_offset(int8_t dx, int8_t dy);
/** CO5300 0x51, 0-100. Call with board_port_lock held, while the panel is lit. */
esp_err_t board_port_set_brightness(uint8_t percent);
/**
 * Dark: brightness 0, DISPOFF, LVGL worker paused. Call without the lock.
 * While dark, board_port_touch_pressed() is the only touch input.
 */
esp_err_t board_port_screen_off(void);
/**
 * Lit again: resume LVGL, DISPON at zero brightness, render one whole frame,
 * then raise the brightness. The finger that woke the panel is not passed to
 * the UI as a tap. Call without the lock.
 */
esp_err_t board_port_screen_on(uint8_t brightness_percent);
/** Reads the CST9217 directly. Only while dark: LVGL owns it otherwise. */
bool board_port_touch_pressed(void);
/** Milliseconds since LVGL last saw a touch. Call with board_port_lock held. */
uint32_t board_port_touch_idle_ms(void);

/** GPIO3/SYS_OUT is high while the case PWR key is physically held. */
bool board_port_power_button_pressed(void);

/** Supply and battery as the AXP2101 reports them. */
typedef struct {
    bool vbus;            /* USB power is present */
    bool battery_present; /* a cell is connected */
    bool charging;        /* current flows into the cell */
    uint8_t percent;      /* fuel gauge; meaningful only with battery_present */
} board_power_t;

/**
 * Read the PMIC's supply status and fuel gauge. Three register reads on the
 * shared I2C bus; call it every few seconds, not per frame.
 */
esp_err_t board_port_read_power(board_power_t *out);

/**
 * Request AXP2101 software power-off. Keep a deep-sleep fallback in the caller
 * for the USB-powered case until all three supply combinations are bench-tested.
 */
esp_err_t board_port_power_off(void);

#ifdef __cplusplus
}
#endif
