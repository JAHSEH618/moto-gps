#ifndef LV_CONF_H
#define LV_CONF_H

/* Keep this file shared by WebAssembly and ESP32 builds. Platform-specific
 * drivers belong outside LVGL and outside the shared UI. */
#define LV_COLOR_DEPTH 16
/* This definition overrides CONFIG_LV_DEF_REFR_PERIOD on ESP32 as well.
 * Match the shared map-motion timer; do not silently cap a 40 Hz UI at 30 Hz. */
#define LV_DEF_REFR_PERIOD 25
#define LV_DPI_DEF 130

#define LV_USE_LOG 1
#define LV_LOG_LEVEL LV_LOG_LEVEL_WARN

#if defined(ESP_PLATFORM)
/* Two software draw units split each frame's draw tasks across both S3 cores,
 * as the Waveshare 02/03/05 examples do on the same BSP and adapter. The
 * render order and pixels are unchanged; Web/native stays single-threaded.
 *
 * Priority ladder (FreeRTOS, higher wins): IMU 7 > LVGL adapter 6 > BLE rx 5
 * > draw threads 4 > PWR 3 > UI render/demo 2. The draw threads only run
 * while the adapter task is blocked waiting for them, and the IMU and BLE
 * receive tasks must still preempt a long frame.
 *
 * Task notifications stay off: the adapter task already waits on its own
 * notification slot (wake-up, TE, flush DMA done) and clears it before each
 * transfer, so LVGL's draw-done signal would share and lose that slot. */
#define LV_USE_OS LV_OS_FREERTOS
#define LV_USE_FREERTOS_TASK_NOTIFY 0
#define LV_DRAW_SW_DRAW_UNIT_CNT 2
#define LV_DRAW_THREAD_PRIO LV_THREAD_PRIO_HIGHEST
#define LV_DRAW_THREAD_STACK_SIZE (8 * 1024)
#else
#define LV_USE_OS LV_OS_NONE
#endif
#define LV_USE_STDLIB_MALLOC LV_STDLIB_BUILTIN
/* The gesture recognizer is the only user of LVGL's float path (it computes
 * the drag vector and direction). Without this, LV_USE_GESTURE_RECOGNITION
 * fails at compile time with a hard #error. */
#define LV_USE_FLOAT 1
/* Permit the board to attach a 2 MiB PSRAM pool without allocating that space
 * in internal BSS. LVGL's TLSF maximum block size includes this expansion
 * budget; leaving it at zero silently rejects a large lv_mem_add_pool(). */
#define LV_MEM_POOL_EXPAND_SIZE (2U * 1024U * 1024U)
#define LV_USE_STDLIB_STRING LV_STDLIB_BUILTIN
#define LV_USE_STDLIB_SPRINTF LV_STDLIB_BUILTIN

#define LV_FONT_MONTSERRAT_16 1
#define LV_FONT_MONTSERRAT_20 1
#define LV_FONT_MONTSERRAT_28 1
#define LV_FONT_MONTSERRAT_48 1
#define LV_FONT_SOURCE_HAN_SANS_SC_16_CJK 0

#define LV_USE_ARC 1
#define LV_USE_LABEL 1
#define LV_USE_LINE 1
#define LV_USE_BUTTON 1
/* Page paging is driven by LV_EVENT_GESTURE. LVGL compiles the gesture
 * recognizer out by default and this file overrides Kconfig on ESP32, so
 * without this line the touch driver logs "only single-point pointer events"
 * and swipe paging is dead on the panel. The native suite cannot catch it:
 * LV_USE_TEST's mouse indev synthesises the event directly. */
#define LV_USE_GESTURE_RECOGNITION 1
// Named objects let the render tests address a control (for example the home
// glyph) without exposing a test-only accessor in the public header.
#define LV_USE_OBJ_NAME 1
#define LV_USE_ANIMIMG 0
#define LV_USE_CANVAS 0
#define LV_USE_LOTTIE 0
#define LV_USE_GIF 0

#if defined(__EMSCRIPTEN__)
#define LV_USE_SDL 1
#define LV_SDL_INCLUDE_PATH <SDL2/SDL.h>
#else
#define LV_USE_SDL 0
#endif

#define LV_BUILD_EXAMPLES 0
#define LV_BUILD_DEMOS 0

#endif /* LV_CONF_H */
