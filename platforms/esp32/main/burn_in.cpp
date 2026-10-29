#include "burn_in.h"

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <mutex>

#include "board_port.h"
#include "burn_in_policy.hpp"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

namespace {
constexpr char kTag[] = "burn_in";
// Lit, touch and motion only move a minute-scale clock, but a touch on a dim
// panel should brighten it at once. Dark, this is also the touch poll, so it
// bounds the wake latency.
constexpr TickType_t kLitPeriod = pdMS_TO_TICKS(100);
constexpr TickType_t kDarkPeriod = pdMS_TO_TICKS(40);
// The worker holds the LVGL lock for a whole frame; waiting longer than one
// frame for a touch timestamp is not worth it.
constexpr std::uint32_t kTouchLockMs = 30;

std::mutex g_mutex;
moto::device::BurnInPolicy g_policy;
TaskHandle_t g_task = nullptr;

std::uint64_t now_ms() {
  return static_cast<std::uint64_t>(esp_timer_get_time()) / 1'000U;
}

#ifndef CONFIG_MOTO_BENCH_TOOLS_DISABLE
moto::device::BurnInConfig bench_fast_config() {
  moto::device::BurnInConfig config;
  config.orbit_period_ms = 2'000;
  config.dim_after_ms = 5'000;
  config.off_after_ms = 15'000;
  return config;
}
#endif

void log_status(const char* reason) {
  const std::uint64_t now = now_ms();
  moto::device::BurnInOutput out;
  moto::device::BurnInConfig config;
  std::uint64_t still = 0;
  {
    const std::lock_guard<std::mutex> lock(g_mutex);
    out = g_policy.evaluate(now);
    config = g_policy.config();
    still = g_policy.still_ms(now);
  }
  ESP_LOGI(kTag,
           "%s: %s %u%% still=%llu s orbit=%u (%d,%d) route=%d "
           "dim@%lu s off@%lu s orbit/%lu s",
           reason, moto::device::to_string(out.level),
           static_cast<unsigned>(out.brightness_percent),
           static_cast<unsigned long long>(still / 1'000U),
           static_cast<unsigned>(out.orbit_index), out.dx, out.dy,
           out.route_held ? 1 : 0,
           static_cast<unsigned long>(config.dim_after_ms / 1'000U),
           static_cast<unsigned long>(config.off_after_ms / 1'000U),
           static_cast<unsigned long>(config.orbit_period_ms / 1'000U));
}

void burn_in_task(void*) {
  // The panel was revealed at full brightness and centred.
  moto::device::BurnInOutput applied;
  while (true) {
    const bool dark = applied.level == moto::device::ScreenLevel::Off;
    std::uint64_t touch_ms = 0;
    if (dark) {
      if (board_port_touch_pressed()) touch_ms = now_ms();
    } else if (board_port_lock(kTouchLockMs)) {
      const std::uint32_t idle = board_port_touch_idle_ms();
      board_port_unlock();
      const std::uint64_t now = now_ms();
      if (idle < now) touch_ms = now - idle;
    }

    const std::uint64_t now = now_ms();
    moto::device::BurnInOutput out;
    {
      const std::lock_guard<std::mutex> lock(g_mutex);
      if (touch_ms != 0) g_policy.note_activity(touch_ms);
      out = g_policy.evaluate(now);
    }

    if (out.level == moto::device::ScreenLevel::Off) {
      if (!dark) {
        const esp_err_t result = board_port_screen_off();
        if (result == ESP_OK) {
          applied = out;
          ESP_LOGI(kTag, "panel off");
        } else {
          ESP_LOGW(kTag, "panel off failed: %s", esp_err_to_name(result));
        }
      }
    } else if (dark) {
      const std::int64_t wake_started_us = esp_timer_get_time();
      if (board_port_lock(UINT32_MAX)) {
        board_port_set_orbit_offset(out.dx, out.dy);
        board_port_unlock();
      }
      const esp_err_t result = board_port_screen_on(out.brightness_percent);
      if (result == ESP_OK) {
        applied = out;
        // The bench number for "wake latency": from the poll that saw the
        // touch (or the event) to a lit, fresh frame.
        ESP_LOGI(kTag, "panel on (%s) in %lld ms",
                 moto::device::to_string(out.level),
                 static_cast<long long>(
                     (esp_timer_get_time() - wake_started_us) / 1'000));
      } else {
        ESP_LOGW(kTag, "panel on failed: %s", esp_err_to_name(result));
      }
    } else if (out.brightness_percent != applied.brightness_percent ||
               out.dx != applied.dx || out.dy != applied.dy) {
      if (board_port_lock(UINT32_MAX)) {
        board_port_set_orbit_offset(out.dx, out.dy);
        esp_err_t result = ESP_OK;
        if (out.brightness_percent != applied.brightness_percent) {
          result = board_port_set_brightness(out.brightness_percent);
        }
        board_port_unlock();
        if (result == ESP_OK) {
          if (out.level != applied.level) {
            ESP_LOGI(kTag, "panel %s (%u%%)",
                     moto::device::to_string(out.level),
                     static_cast<unsigned>(out.brightness_percent));
          }
          applied = out;
        } else {
          ESP_LOGW(kTag, "brightness %u%% failed: %s",
                   static_cast<unsigned>(out.brightness_percent),
                   esp_err_to_name(result));
          // Keep the new orbit; retry the brightness on the next tick.
          applied.dx = out.dx;
          applied.dy = out.dy;
        }
      }
    }
    vTaskDelay(applied.level == moto::device::ScreenLevel::Off ? kDarkPeriod
                                                               : kLitPeriod);
  }
}
}  // namespace

bool burn_in_start() {
  if (g_task != nullptr) return true;
  {
    const std::lock_guard<std::mutex> lock(g_mutex);
    g_policy = moto::device::BurnInPolicy({}, now_ms());
  }
  // Above the demo/UI feeders (2) so a wake is not queued behind them, below
  // the PWR task (3).
  if (xTaskCreate(burn_in_task, "moto_burnin", 3'072, nullptr, 2, &g_task) !=
      pdPASS) {
    ESP_LOGW(kTag, "burn-in task could not start; the panel stays lit");
    g_task = nullptr;
    return false;
  }
  log_status("started");
  return true;
}

void burn_in_note_motion() {
  const std::uint64_t now = now_ms();
  const std::lock_guard<std::mutex> lock(g_mutex);
  g_policy.note_motion(now);
}

bool burn_in_toggle_panel() {
  const std::uint64_t now = now_ms();
  const std::lock_guard<std::mutex> lock(g_mutex);
  // Toggle what the panel is actually doing, not the manual flag: the panel
  // may be dark because the 15 min idle rule fired, and the first PWR press
  // after that must wake it rather than re-blank it.
  const moto::device::BurnInOutput current = g_policy.evaluate(now);
  const bool wake = current.level == moto::device::ScreenLevel::Off;
  g_policy.set_manual_off(!wake, now);
  if (wake) g_policy.note_activity(now);
  return wake;
}

void burn_in_note_navigation(bool route_active) {
  const std::uint64_t now = now_ms();
  const std::lock_guard<std::mutex> lock(g_mutex);
  g_policy.note_navigation(route_active, now);
}

#ifndef CONFIG_MOTO_BENCH_TOOLS_DISABLE
void burn_in_command(const char* args) {
  while (*args == ' ') ++args;
  const std::uint64_t now = now_ms();
  if (*args == '\0' || std::strcmp(args, "status") == 0) {
    log_status("status");
    return;
  }
  {
    const std::lock_guard<std::mutex> lock(g_mutex);
    if (std::strcmp(args, "full") == 0) {
      g_policy.force(moto::device::ScreenLevel::Full, now);
    } else if (std::strcmp(args, "dim") == 0) {
      g_policy.force(moto::device::ScreenLevel::Dim, now);
    } else if (std::strcmp(args, "off") == 0) {
      g_policy.force(moto::device::ScreenLevel::Off, now);
    } else if (std::strcmp(args, "toggle") == 0) {
      const moto::device::BurnInOutput current = g_policy.evaluate(now);
      const bool wake = current.level == moto::device::ScreenLevel::Off;
      g_policy.set_manual_off(!wake, now);
      if (wake) g_policy.note_activity(now);
    } else if (std::strcmp(args, "fast") == 0) {
      g_policy.set_config(bench_fast_config(), now);
    } else if (std::strcmp(args, "normal") == 0) {
      g_policy.set_config({}, now);
    } else if (std::strcmp(args, "orbit auto") == 0) {
      g_policy.pin_orbit(moto::device::BurnInPolicy::kOrbitAuto);
    } else if (std::strncmp(args, "orbit ", 6) == 0) {
      g_policy.pin_orbit(
          static_cast<std::uint8_t>(std::strtoul(args + 6, nullptr, 10)));
    } else {
      ESP_LOGW(kTag,
               "usage: burnin [status|full|dim|off|fast|normal|"
               "orbit <0-8>|orbit auto]");
      return;
    }
  }
  log_status(args);
}
#endif  // CONFIG_MOTO_BENCH_TOOLS_DISABLE
