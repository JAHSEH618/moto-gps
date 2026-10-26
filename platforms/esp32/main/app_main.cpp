#include <algorithm>
#include <cmath>
#include <cstdint>

#include "ams_client_nimble.h"
#include "ble_nav_transport_nimble.h"
#include "bsp/esp-bsp.h"
#include "board_port.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "esp_system.h"
#include "driver/usb_serial_jtag.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "moto_nav_presenter.hpp"
#include "moto_nav_ui.h"
#include "motion_heading_sensor.h"
#include "phone_nav_bridge.h"
#include "settings_store.h"
#include "esp_app_desc.h"
#include "esp_attr.h"
#include "esp_rtc_time.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "tlog_logger.hpp"
#include "battery_ring.hpp"

#include "driver/i2c_master.h"
#include "esp_flash.h"

namespace {
constexpr char kTag[] = "moto_gps";

// Development recorder. Absent when the tlog partition is missing or its
// above-16-MB self-test failed; every append is then a no-op.
static moto::device::TlogLogger tlog;
static bool g_calibration_unverified = true;
static MotionHeadingSensor* g_sensor = nullptr;
static PhoneNavBridge* g_bridge = nullptr;
static TaskHandle_t g_task_cmd = nullptr;
static TaskHandle_t g_task_ui_flags = nullptr;
static TaskHandle_t g_task_demo = nullptr;
static TaskHandle_t g_task_power = nullptr;

// "Screen unlit for 6 hours clears the tlog" retention. RTC memory survives
// deep sleep, and the RTC counter keeps counting through it, so this measures
// the screen-off interval without an RTC chip. A full power cut loses the RTC
// domain; there is no wall clock on this board to recover that interval, so a
// cold boot starts with no last-lit record rather than guessing.
constexpr std::uint32_t kScreenLitMagic = 0x4D4F544FU;  // "MOTO"
constexpr std::uint64_t kUnlitClearAfterUs =
    6ULL * 60ULL * 60ULL * 1'000'000ULL;
RTC_DATA_ATTR static std::uint64_t g_last_lit_rtc_us = 0;
RTC_DATA_ATTR static std::uint32_t g_last_lit_magic = 0;
// Set while the panel is lit. A software reset while the screen was on must
// not be mistaken for six hours of darkness.
RTC_DATA_ATTR static std::uint8_t g_screen_lit = 0;

void note_screen_lit() {
  g_last_lit_rtc_us = esp_rtc_get_time_us();
  g_last_lit_magic = kScreenLitMagic;
  g_screen_lit = 1;
}

void note_screen_unlit() {
  g_last_lit_rtc_us = esp_rtc_get_time_us();
  g_last_lit_magic = kScreenLitMagic;
  g_screen_lit = 0;
}

bool screen_unlit_for_clear(std::uint64_t* elapsed_us = nullptr) {
  // A lit screen is not unlit, even if the periodic stamp is a minute old.
  if (g_screen_lit != 0) {
    if (elapsed_us != nullptr) *elapsed_us = 0;
    return false;
  }
  if (g_last_lit_magic != kScreenLitMagic || g_last_lit_rtc_us == 0) {
    return false;
  }
  const std::uint64_t now_us = esp_rtc_get_time_us();
  if (now_us <= g_last_lit_rtc_us) return false;
  const std::uint64_t elapsed = now_us - g_last_lit_rtc_us;
  if (elapsed_us != nullptr) *elapsed_us = elapsed;
  return elapsed >= kUnlitClearAfterUs;
}
// Bench-only lean sweep: a repeatable 40 Hz UI load for the page frame-rate
// measurement. Zero means off.
static volatile std::uint32_t g_ui_bench_lean_until_ms = 0;
// `ui battery`: while set, the PWR task stops posting PMIC readings so the
// bench value stays on the ring.
static volatile bool g_ui_bench_battery = false;
static std::uint64_t g_dbg_render_us0 = 0;
static std::uint32_t g_dbg_frames0 = 0;

std::uint64_t ui_debug_clock_us() {
  return static_cast<std::uint64_t>(esp_timer_get_time());
}

moto::device::MountRecord mount_record_from(
    const moto::motion::MountCalibration& mount) {
  moto::device::MountRecord record;
  const moto::motion::Vec3f forward = mount.forward_axis();
  const moto::motion::Vec3f up = mount.up_axis();
  record.forward[0] = static_cast<std::int16_t>(std::lround(forward.x * 10000.0F));
  record.forward[1] = static_cast<std::int16_t>(std::lround(forward.y * 10000.0F));
  record.forward[2] = static_cast<std::int16_t>(std::lround(forward.z * 10000.0F));
  record.up[0] = static_cast<std::int16_t>(std::lround(up.x * 10000.0F));
  record.up[1] = static_cast<std::int16_t>(std::lround(up.y * 10000.0F));
  record.up[2] = static_cast<std::int16_t>(std::lround(up.z * 10000.0F));
  record.trim_cdeg = static_cast<std::int16_t>(std::lround(mount.trim_deg * 100.0F));
  return record;
}

void lean_mode_changed(std::uint8_t mode, void*) {
  // The session record is the only honest place for a mid-session fallback:
  // the first block was written before this happened. Modes: 0 poll, 1 FIFO,
  // 2 latch.
  const bool fast = mode == 1 || mode == 2;
  tlog.note_mode(mode, fast ? 448 : 125, fast ? 112 : 125);
}

const char* task_state_name(eTaskState state) {
  switch (state) {
    case eRunning: return "running";
    case eReady: return "ready";
    case eBlocked: return "blocked";
    case eSuspended: return "suspended";
    case eDeleted: return "deleted";
    case eInvalid:
    default: return "invalid";
  }
}

void log_task_entry(const char* name, TaskHandle_t handle) {
  if (handle == nullptr) {
    ESP_LOGI(kTag, "task %-16s not started", name);
    return;
  }
  const UBaseType_t words = uxTaskGetStackHighWaterMark(handle);
  ESP_LOGI(kTag, "task %-16s state=%-9s stack_free=%u B", name,
           task_state_name(eTaskGetState(handle)),
           static_cast<unsigned>(words * sizeof(StackType_t)));
}

// Bench deadlock probe (`sys tasks`). A hang in the LVGL task, the sensor
// task, a calibration callback or a flash/I2C lock shows up as a task whose
// state is blocked forever and whose remaining stack is still measurable.
// NimBLE's own tasks are not in this registry; they are not part of the
// acquisition or render path.
void log_tasks() {
  ESP_LOGI(kTag, "task dump (app tasks):");
  log_task_entry("moto_qmi_heading", g_sensor != nullptr ? g_sensor->task_handle()
                                                         : nullptr);
  log_task_entry("moto_ui_render",
                 g_bridge != nullptr ? g_bridge->render_task_handle() : nullptr);
  log_task_entry("moto_cmd", g_task_cmd);
  log_task_entry("moto_ui_flags", g_task_ui_flags);
  log_task_entry("moto_demo", g_task_demo);
  log_task_entry("moto_power", g_task_power);
  log_task_entry("moto_current", xTaskGetCurrentTaskHandle());
  ESP_LOGI(kTag, "free heap: internal %u B, PSRAM %u B",
           static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),
           static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)));
}

void ui_flags_task(void*) {
  while (true) {
    const bool imu_poll =
        g_sensor == nullptr ||
        g_sensor->acquisition_mode() ==
            MotionHeadingSensor::AcquisitionMode::Poll;
    moto_nav_ui_set_lean_flags(g_calibration_unverified ? 1 : 0,
                               imu_poll ? 1 : 0, tlog.full() ? 1 : 0,
                               tlog.dirty() ? 1 : 0);
    vTaskDelay(pdMS_TO_TICKS(500));
  }
}

// One byte from whichever console has input. UART0 is the primary console
// (stdin), but the Waveshare board's only USB-C port is the S3's USB-Serial/
// JTAG, which is a secondary, output-only console: without reading it here a
// bench command typed into that port never arrives.
int read_console_byte() {
  std::uint8_t byte = 0;
  if (usb_serial_jtag_is_driver_installed() &&
      usb_serial_jtag_read_bytes(&byte, 1, pdMS_TO_TICKS(20)) == 1) {
    return byte;
  }
  return std::fgetc(stdin);
}

void serial_command_task(void* context) {
  auto* sensor = static_cast<MotionHeadingSensor*>(context);
  if (!usb_serial_jtag_is_driver_installed()) {
    usb_serial_jtag_driver_config_t config =
        USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT();
    if (usb_serial_jtag_driver_install(&config) != ESP_OK) {
      ESP_LOGW(kTag, "USB-Serial/JTAG input unavailable; UART0 only");
    }
  }
  char line[64];
  std::size_t used = 0;
  while (true) {
    const int c = read_console_byte();
    if (c == EOF) {
      vTaskDelay(pdMS_TO_TICKS(30));
      continue;
    }
    if (c == '\r') continue;
    if (c != '\n') {
      if (used + 1 < sizeof(line)) line[used++] = static_cast<char>(c);
      continue;
    }
    line[used] = '\0';
    used = 0;
    if (std::strcmp(line, "imu stats") == 0) {
      const moto::motion::LatchStats& latch = sensor->latch_stats();
      const moto::motion::FifoStats& fifo = sensor->fifo_stats();
      const char* reason = sensor->fallback_reason();
      if (sensor->latch_active() || !sensor->fifo_active()) {
        const std::uint32_t cycles = latch.cycles == 0 ? 1 : latch.cycles;
        ESP_LOGI(kTag,
                 "imu mode=%s reason=%s samples=%u cycles=%u avail0=%u "
                 "avail0_pct=%u.%02u lockwait=%u lockto=%u i2c=%u missed=%u "
                 "flash=%u flashgaps=%u dt_max_us=%u dt_avg_us=%u",
                 sensor->mode_name(), reason != nullptr ? reason : "-",
                 static_cast<unsigned>(latch.samples),
                 static_cast<unsigned>(latch.cycles),
                 static_cast<unsigned>(latch.avail_zero),
                 static_cast<unsigned>(latch.avail_zero * 100U / cycles),
                 static_cast<unsigned>((latch.avail_zero * 10000U / cycles) %
                                       100U),
                 static_cast<unsigned>(latch.lock_waits),
                 static_cast<unsigned>(latch.lock_timeouts),
                 static_cast<unsigned>(latch.i2c_errors),
                 static_cast<unsigned>(latch.missed_reads),
                 static_cast<unsigned>(latch.flash_skips),
                 static_cast<unsigned>(latch.flash_gaps),
                 static_cast<unsigned>(latch.interval_us_max),
                 static_cast<unsigned>(latch.intervals == 0
                                           ? 0
                                           : latch.interval_us_sum /
                                                 latch.intervals));
      }
      if (sensor->fifo_active()) {
        ESP_LOGI(kTag,
                 "imu mode=fifo reason=%s batches=%u received=%u produced=%u "
                 "lost=%u overflow=%u window_max_us=%u window_avg_us=%u",
                 reason != nullptr ? reason : "-",
                 static_cast<unsigned>(fifo.batches),
                 static_cast<unsigned>(fifo.received),
                 static_cast<unsigned>(fifo.produced),
                 static_cast<unsigned>(fifo.lost),
                 static_cast<unsigned>(fifo.overflow),
                 static_cast<unsigned>(fifo.window_us_max),
                 static_cast<unsigned>(fifo.window_samples == 0
                                           ? 0
                                           : fifo.window_us_sum /
                                                 fifo.window_samples));
      }
    } else if (std::strcmp(line, "imu stats reset") == 0) {
      sensor->reset_imu_stats();
      ESP_LOGI(kTag, "latch counters reset");
    } else if (std::strcmp(line, "imu lean") == 0) {
      ESP_LOGI(kTag, "lean=%.2f pitch=%.2f quality=%u flags=0x%04x mode=%s",
               static_cast<double>(sensor->last_lean_deg()),
               static_cast<double>(sensor->last_pitch_deg()),
               static_cast<unsigned>(sensor->last_quality()),
               static_cast<unsigned>(sensor->last_flags()),
               sensor->mode_name());
    } else if (std::strcmp(line, "imu raw") == 0) {
      const RawImuSample raw = sensor->last_raw();
      const moto::motion::LeanEstimator& lean = sensor->lean();
      const moto::motion::Vec3f up = lean.up_body();
      const moto::motion::Vec3f bias = lean.bias_sensor_dps();
      const moto::motion::Vec3f fwd = lean.mount().forward_axis();
      ESP_LOGI(kTag,
               "imu raw a=%.3f %.3f %.3f g=%.2f %.2f %.2f bias=%.2f %.2f %.2f "
               "fwd=%.2f %.2f %.2f "
               "up=%.3f %.3f %.3f still=%.2f jit=%.3f da=%.4f aref=%.3f "
               "lean=%.2f pitch=%.2f flags=0x%04x",
               static_cast<double>(raw.accel_mps2[0]),
               static_cast<double>(raw.accel_mps2[1]),
               static_cast<double>(raw.accel_mps2[2]),
               static_cast<double>(raw.gyro_dps[0]),
               static_cast<double>(raw.gyro_dps[1]),
               static_cast<double>(raw.gyro_dps[2]),
               static_cast<double>(bias.x), static_cast<double>(bias.y),
               static_cast<double>(bias.z), static_cast<double>(fwd.x),
               static_cast<double>(fwd.y), static_cast<double>(fwd.z),
               static_cast<double>(up.x),
               static_cast<double>(up.y), static_cast<double>(up.z),
               static_cast<double>(lean.static_for_s()),
               static_cast<double>(lean.gyro_jitter_dps()),
               static_cast<double>(lean.accel_delta_mps2()),
               static_cast<double>(lean.accel_magnitude_ref_mps2()),
               static_cast<double>(sensor->last_lean_deg()),
               static_cast<double>(sensor->last_pitch_deg()),
               static_cast<unsigned>(sensor->last_flags()));
    } else if (std::strcmp(line, "imu speed") == 0) {
      // DESIGN_V2 §3.7 state. age is the newest fix's, from when the phone
      // took it; a snapshot-only phone shows its arrival instead.
      const moto::motion::SpeedFusion& speed = sensor->speed();
      const moto::motion::SpeedSample sample = speed.sample(
          static_cast<std::uint32_t>(esp_timer_get_time() / 1'000));
      ESP_LOGI(kTag,
               "imu speed v=%.2f valid=%d age=%.2f sigma=%.2f bias=%.3f "
               "refused=%u dropped=%u",
               static_cast<double>(speed.speed_mps()), sample.valid ? 1 : 0,
               static_cast<double>(sample.age_s),
               static_cast<double>(speed.sigma_mps()),
               static_cast<double>(speed.bias_mps2()),
               static_cast<unsigned>(speed.rejected_fixes()),
               static_cast<unsigned>(sensor->dropped_speed_fixes()));
    } else if (std::strcmp(line, "imu int reset") == 0) {
      sensor->request_gyro_integral_reset();
      ESP_LOGI(kTag, "gyro integral reset; rotate the board, then hold still");
    } else if (std::strcmp(line, "imu int") == 0) {
      // A still board's specific force is world up in sensor axes, and it
      // turns by minus the body rotation. Its change between the reset and
      // now therefore predicts the gyro integral's component across gravity.
      const float* gyro = sensor->gyro_integral_deg();
      const float* start = sensor->gyro_integral_start_accel();
      const RawImuSample now = sensor->last_raw();
      const float ax = start[1] * now.accel_mps2[2] - start[2] * now.accel_mps2[1];
      const float ay = start[2] * now.accel_mps2[0] - start[0] * now.accel_mps2[2];
      const float az = start[0] * now.accel_mps2[1] - start[1] * now.accel_mps2[0];
      const float cross = std::sqrt(ax * ax + ay * ay + az * az);
      const float dot = start[0] * now.accel_mps2[0] +
                        start[1] * now.accel_mps2[1] +
                        start[2] * now.accel_mps2[2];
      const float angle_deg = std::atan2(cross, dot) * 57.29578F;
      const float k = cross > 1.0e-6F ? -angle_deg / cross : 0.0F;
      ESP_LOGI(kTag,
               "imu int gyro=%.1f %.1f %.1f accel=%.1f %.1f %.1f turn=%.1f",
               static_cast<double>(gyro[0]), static_cast<double>(gyro[1]),
               static_cast<double>(gyro[2]), static_cast<double>(ax * k),
               static_cast<double>(ay * k), static_cast<double>(az * k),
               static_cast<double>(angle_deg));
    } else if (std::strcmp(line, "imu cal") == 0) {
      // Same path as the long-press on the Lean page.
      sensor->request_static_calibration();
      ESP_LOGI(kTag, "lean calibration requested; keep the board still 2 s");
    } else if (std::strcmp(line, "imu fifo off") == 0) {
      sensor->request_fifo_mode(false);
      ESP_LOGI(kTag, "FIFO disabled; restarting acquisition on latch");
    } else if (std::strcmp(line, "imu fifo on") == 0) {
      sensor->request_fifo_mode(true);
      ESP_LOGI(kTag, "FIFO enabled for A/B; restarting acquisition");
    } else if (std::strcmp(line, "sys tasks") == 0) {
      log_tasks();
    } else if (std::strcmp(line, "tlog clear") == 0) {
      // Deferred erase: write the flag and reboot. app_main erases the 16 MB
      // partition before the display/BLE/IMU tasks exist, because the erase
      // stalls the flash cache for seconds.
      moto::device::request_tlog_clear();
      ESP_LOGW(kTag, "tlog clear requested; rebooting to erase the partition");
      vTaskDelay(pdMS_TO_TICKS(100));
      esp_restart();
    } else if (std::strcmp(line, "tlog status") == 0) {
      std::uint64_t unlit_us = 0;
      const bool unlit = screen_unlit_for_clear(&unlit_us);
      ESP_LOGI(kTag,
               "tlog status: ready=%d full=%d dirty=%d records=%lu blocks=%lu "
               "unlit=%llu s unlit_clear=%d",
               tlog.ready() ? 1 : 0, tlog.full() ? 1 : 0,
               tlog.dirty() ? 1 : 0,
               static_cast<unsigned long>(tlog.records()),
               static_cast<unsigned long>(tlog.blocks()),
               static_cast<unsigned long long>(unlit_us / 1'000'000ULL),
               unlit ? 1 : 0);
    } else if (std::strncmp(line, "ui page ", 8) == 0) {
      // Bench-only page switch: lets the frame-time window be attributed to a
      // page without touching the screen. The touch path stays authoritative
      // for the rider.
      const char* name = line + 8;
      int page = -1;
      if (std::strcmp(name, "nav") == 0) page = MOTO_UI_PAGE_NAVIGATION;
      else if (std::strcmp(name, "performance") == 0) page = MOTO_UI_PAGE_PERFORMANCE;
      else if (std::strcmp(name, "lean") == 0) page = MOTO_UI_PAGE_LEAN;
      else if (std::strcmp(name, "speed") == 0) page = MOTO_UI_PAGE_SPEED;
      else if (std::strcmp(name, "compass") == 0) page = MOTO_UI_PAGE_COMPASS;
      else if (std::strcmp(name, "music") == 0) page = MOTO_UI_PAGE_MUSIC;
      else page = std::atoi(name);
      if (page < 0 || page >= MOTO_UI_PAGE_COUNT) {
        ESP_LOGW(kTag, "ui page: unknown page '%s'", name);
      } else if (board_port_lock(1'000)) {
        moto_nav_ui_set_page(static_cast<moto_ui_page_t>(page));
        board_port_unlock();
        ESP_LOGI(kTag, "ui page=%d", page);
      } else {
        ESP_LOGW(kTag, "ui page: LVGL lock timeout");
      }
    } else if (std::strncmp(line, "ui theme ", 9) == 0) {
      // Bench-only theme switch, for checking the classic <-> GTA tree swap on
      // the panel without a phone. The phone's SettingsState stays the
      // rider's path and overrides this on the next settings change.
      const char* name = line + 9;
      int theme = -1;
      if (std::strcmp(name, "official") == 0) theme = MOTO_UI_THEME_OFFICIAL;
      else if (std::strcmp(name, "day") == 0) theme = MOTO_UI_THEME_GTA_DAY;
      else if (std::strcmp(name, "night") == 0) theme = MOTO_UI_THEME_GTA_NIGHT;
      else if (std::strcmp(name, "auto") == 0) theme = MOTO_UI_THEME_AUTO;
      if (theme < 0) {
        ESP_LOGW(kTag, "ui theme: unknown theme '%s'", name);
      } else if (board_port_lock(1'000)) {
        const std::int64_t started = esp_timer_get_time();
        moto_nav_ui_set_theme(static_cast<moto_ui_theme_t>(theme));
        const std::int64_t elapsed_us = esp_timer_get_time() - started;
        board_port_unlock();
        ESP_LOGI(kTag, "ui theme=%d applied in %lld us; internal heap %u B free",
                 theme, static_cast<long long>(elapsed_us),
                 static_cast<unsigned>(
                     heap_caps_get_free_size(MALLOC_CAP_INTERNAL)));
      } else {
        ESP_LOGW(kTag, "ui theme: LVGL lock timeout");
      }
    } else if (std::strncmp(line, "ui battery ", 11) == 0) {
      // Bench-only: preview the bezel ring without a cell. `ui battery 37`
      // (on the battery), `ui battery usb 37` (charging), `ui battery auto`
      // (back to the PMIC).
      const char* arg = line + 11;
      if (std::strcmp(arg, "auto") == 0) {
        g_ui_bench_battery = false;
        ESP_LOGI(kTag, "ui battery: PMIC readings resume");
      } else {
        const bool usb = std::strncmp(arg, "usb", 3) == 0;
        const int percent = std::atoi(usb ? arg + 3 : arg);
        g_ui_bench_battery = true;
        moto_nav_ui_set_battery(
            usb ? MOTO_UI_POWER_EXTERNAL : MOTO_UI_POWER_BATTERY,
            static_cast<std::uint8_t>(std::clamp(percent, 0, 100)));
        ESP_LOGI(kTag, "ui battery %s%d%%", usb ? "usb " : "",
                 std::clamp(percent, 0, 100));
      }
    } else if (std::strncmp(line, "ui bench lean", 13) == 0) {
      const int seconds = std::atoi(line + 13);
      if (seconds > 0) {
        const std::uint64_t now_ms =
            static_cast<std::uint64_t>(esp_timer_get_time()) / 1'000U;
        g_ui_bench_lean_until_ms =
            static_cast<std::uint32_t>(now_ms + seconds * 1'000U);
        if (board_port_lock(1'000)) {
          moto_nav_ui_set_page(MOTO_UI_PAGE_LEAN);
          board_port_unlock();
        }
        ESP_LOGI(kTag,
                 "lean bench: %d s synthetic +/-30 deg at 40 Hz on the lean "
                 "page",
                 seconds);
      } else {
        g_ui_bench_lean_until_ms = 0;
        ESP_LOGI(kTag, "lean bench cancelled");
      }
    } else if (std::strcmp(line, "ui dbg on") == 0) {
      moto_nav_ui_debug_set_enabled(1);
      ESP_LOGI(kTag, "lean profiling on");
    } else if (std::strcmp(line, "ui dbg off") == 0) {
      moto_nav_ui_debug_set_enabled(0);
      ESP_LOGI(kTag, "lean profiling off");
    } else if (std::strncmp(line, "ui dbg hide ", 12) == 0) {
      const char* arg = line + 12;
      std::uint32_t bit = 0;
      if (std::strncmp(arg, "sun", 3) == 0) {
        bit = MOTO_UI_DBG_HIDE_SUN;
      } else if (std::strncmp(arg, "horizon", 7) == 0) {
        bit = MOTO_UI_DBG_HIDE_HORIZON;
      } else if (std::strncmp(arg, "hero", 4) == 0) {
        bit = MOTO_UI_DBG_HIDE_HERO;
      } else if (std::strncmp(arg, "halo", 4) == 0) {
        bit = MOTO_UI_DBG_HIDE_HALO;
      } else if (std::strncmp(arg, "chrome", 6) == 0) {
        bit = MOTO_UI_DBG_HIDE_CHROME;
      } else if (std::strncmp(arg, "scene", 5) == 0) {
        bit = MOTO_UI_DBG_HIDE_SCENE;
      }
      if (bit == 0) {
        ESP_LOGW(kTag,
                 "ui dbg hide: use sun|horizon|hero|halo|chrome|scene [0|1]");
      } else {
        const char* value = std::strchr(arg, ' ');
        std::uint32_t mask = moto_nav_ui_debug_hidden();
        const bool show = value != nullptr && std::atoi(value + 1) == 0;
        if (show) {
          mask &= ~bit;
        } else {
          mask |= bit;
        }
        if (board_port_lock(1'000)) {
          moto_nav_ui_debug_set_hidden(mask);
          board_port_unlock();
          ESP_LOGI(kTag, "ui dbg hide mask=0x%02x%s",
                   static_cast<unsigned>(mask), show ? " (shown)" : "");
        } else {
          ESP_LOGW(kTag, "ui dbg hide: LVGL lock timeout");
        }
      }
    } else if (std::strcmp(line, "ui dbg reset") == 0) {
      moto_nav_ui_debug_reset();
      board_port_display_totals(&g_dbg_render_us0, &g_dbg_frames0);
      board_port_reset_display_timing();
      ESP_LOGI(kTag, "lean profiling window reset");
    } else if (std::strcmp(line, "ui dbg") == 0) {
      std::uint64_t render_us = 0;
      std::uint32_t frames = 0;
      board_port_display_totals(&render_us, &frames);
      const std::uint32_t frame_count = frames - g_dbg_frames0;
      const std::uint64_t render_delta = render_us - g_dbg_render_us0;
      moto_ui_debug_stats_t stats{};
      moto_nav_ui_debug_stats(&stats);
      if (frame_count == 0) {
        ESP_LOGW(kTag, "lean dbg: no frames; run ui bench lean and reset first");
      } else {
        const double per_frame = 1.0 / static_cast<double>(frame_count);
        const double total_ms =
            static_cast<double>(render_delta) / 1000.0 * per_frame;
        const double sun_ms =
            static_cast<double>(stats.sun_us) / 1000.0 * per_frame;
        const double horizon_ms =
            static_cast<double>(stats.horizon_us) / 1000.0 * per_frame;
        const double hero_ms =
            static_cast<double>(stats.hero_us) / 1000.0 * per_frame;
        ESP_LOGI(kTag,
                 "lean dbg: frames=%u total=%.2fms sun=%.2f horizon=%.2f "
                 "hero(incl halo)=%.2f other=%.2f draws=%u/%u/%u hide=0x%02x",
                 static_cast<unsigned>(frame_count), total_ms, sun_ms,
                 horizon_ms, hero_ms,
                 total_ms - sun_ms - horizon_ms - hero_ms,
                 static_cast<unsigned>(stats.sun_draws),
                 static_cast<unsigned>(stats.horizon_draws),
                 static_cast<unsigned>(stats.hero_draws),
                 static_cast<unsigned>(moto_nav_ui_debug_hidden()));
      }
    } else if (std::strcmp(line, "ui perf reset") == 0) {
      board_port_reset_display_timing();
      ESP_LOGI(kTag, "display timing window reset");
    } else if (std::strcmp(line, "ui perf") == 0) {
      board_port_log_display_timing();
    }
  }
}

// One-shot boot report: the 1.75C SKU (33691) must be confirmed on the bench,
// not inferred from a sibling board's BSP. Logs the real flash size and every
// I2C device that answers, with the expected addresses named explicitly.
void log_hardware_report() {
  std::uint32_t flash_bytes = 0;
  if (esp_flash_get_size(nullptr, &flash_bytes) == ESP_OK) {
    ESP_LOGI(kTag, "flash: %u bytes (%u MB)", static_cast<unsigned>(flash_bytes),
             static_cast<unsigned>(flash_bytes / (1024U * 1024U)));
  } else {
    ESP_LOGW(kTag, "flash size probe failed");
  }
  i2c_master_bus_handle_t bus = bsp_i2c_get_handle();
  if (bus == nullptr) {
    ESP_LOGW(kTag, "i2c bus unavailable; device scan skipped");
    return;
  }
  for (std::uint8_t address = 0x08; address <= 0x77; ++address) {
    if (i2c_master_probe(bus, address, 20) == ESP_OK) {
      ESP_LOGI(kTag, "i2c device at 0x%02x", static_cast<unsigned>(address));
    }
  }
  struct Expectation {
    const char* name;
    std::uint8_t address;
  };
  const Expectation expected[] = {
      {"PCF85063 RTC (1.75C may omit)", 0x51},
      {"QMI8658 IMU", 0x6B},
      {"CST9217 touch", 0x5A},
      {"AXP2101 PMIC", 0x34},
      {"TCA9554 expander", 0x20},
      {"ES8311 audio", 0x18},
      {"ES7210 mic", 0x40},
  };
  for (const auto& device : expected) {
    const bool present =
        i2c_master_probe(bus, device.address, 20) == ESP_OK;
    ESP_LOGI(kTag, "%s 0x%02x: %s", device.name,
             static_cast<unsigned>(device.address),
             present ? "present" : "absent");
  }
}

void save_lean_calibration(const moto::motion::MountCalibration& mount,
                           bool ok, std::uint8_t reason, void*) {
  if (!ok) {
    ESP_LOGW(kTag, "lean calibration rejected (reason %u); keeping basis",
             static_cast<unsigned>(reason));
    moto_nav_ui_set_lean_cal_state(
        reason == 1 ? MOTO_UI_LEAN_CAL_FAIL_MOVING
                    : MOTO_UI_LEAN_CAL_FAIL_RANGE,
        0);
    return;
  }
  g_calibration_unverified = false;
  moto_nav_ui_set_lean_cal_state(MOTO_UI_LEAN_CAL_OK, 0);
  moto::device::StoredMount stored;
  stored.forward = mount.forward_axis();
  stored.up = mount.up_axis();
  stored.trim_cdeg = static_cast<std::int16_t>(
      std::lround(mount.trim_deg * 100.0F));
  stored.static_magnitude_mps2 = mount.static_magnitude_mps2;
  stored.valid = true;
  if (!moto::device::save_mount_basis(stored)) {
    ESP_LOGW(kTag, "lean calibration captured but not persisted");
  }
}

void request_lean_calibration(void* context) {
  static_cast<MotionHeadingSensor*>(context)->request_static_calibration();
}
constexpr std::uint64_t kPowerHoldMs = 3'000;

// DeviceThemeMode and moto_ui_theme_t are different enums with different
// orders (Auto is 1 on the wire and 3 in the UI), so never cast.
moto_ui_theme_t boot_theme_for(moto::device::DeviceThemeMode mode) {
  switch (mode) {
    case moto::device::DeviceThemeMode::Day:
      return MOTO_UI_THEME_GTA_DAY;
    case moto::device::DeviceThemeMode::Night:
      return MOTO_UI_THEME_GTA_NIGHT;
    case moto::device::DeviceThemeMode::Auto:
      return MOTO_UI_THEME_AUTO;
    case moto::device::DeviceThemeMode::Official:
    default:
      return MOTO_UI_THEME_OFFICIAL;
  }
}

moto::ble::AckStatus receive_phone_message(
    const moto::ble::ReassembledMessage& message, void* context) {
  return static_cast<PhoneNavBridge*>(context)->on_message(message);
}

void update_phone_link(bool active, void* context) {
  static_cast<PhoneNavBridge*>(context)->on_link_state(active);
}

void update_motion_heading(
    float heading_rate_dps,
    const moto::motion::AttitudeEstimate& attitude,
    const RawImuSample& raw, std::uint64_t sample_ms, void* context) {
  if (g_ui_bench_lean_until_ms == 0) {
    static_cast<PhoneNavBridge*>(context)->on_imu_sample(
        heading_rate_dps, attitude, sample_ms);
  }
  const std::int64_t tlog_started_us = esp_timer_get_time();
  tlog.append(sample_ms, raw, attitude);
  // A 4 KiB page write is the only synchronous flash work left on the sensor
  // task. If it took long enough to matter, tell the latch reader so the next
  // over-budget interval is filed under "flash", not "missed".
  if (esp_timer_get_time() - tlog_started_us > 2'000 && g_sensor != nullptr) {
    g_sensor->note_flash_blocking();
  }

  // The on-disk half of the acceptance set. Ten seconds is short enough to
  // catch a flash-induced gap inside one bench run and long enough that the
  // record does not compete with the 112 Hz stream.
  static std::uint64_t last_stats_ms = 0;
  if (g_sensor != nullptr && sample_ms - last_stats_ms >= 10'000U) {
    last_stats_ms = sample_ms;
    moto::device::ImuStatsRecord record;
    record.t_ms = static_cast<std::uint32_t>(sample_ms);
    record.mode = static_cast<std::uint8_t>(g_sensor->acquisition_mode());
    if (record.mode ==
        static_cast<std::uint8_t>(
            MotionHeadingSensor::AcquisitionMode::Latch)) {
      const moto::motion::LatchStats& s = g_sensor->latch_stats();
      const auto clamp16 = [](std::uint32_t value) {
        return static_cast<std::uint16_t>(std::min<std::uint32_t>(value, 65535));
      };
      record.samples = s.samples;
      record.avail_zero = s.avail_zero;
      record.lock_waits = s.lock_waits;
      record.i2c_errors = clamp16(s.i2c_errors);
      record.missed_reads = clamp16(s.missed_reads);
      record.flash_skips = clamp16(s.flash_skips);
      record.flash_gaps = clamp16(s.flash_gaps);
      record.interval_max_us = clamp16(s.interval_us_max);
      record.interval_avg_us =
          clamp16(s.intervals == 0
                      ? 0
                      : static_cast<std::uint32_t>(s.interval_us_sum /
                                                   s.intervals));
    } else if (record.mode ==
               static_cast<std::uint8_t>(
                   MotionHeadingSensor::AcquisitionMode::Fifo)) {
      const moto::motion::FifoStats& s = g_sensor->fifo_stats();
      const auto clamp16 = [](std::uint32_t value) {
        return static_cast<std::uint16_t>(std::min<std::uint32_t>(value, 65535));
      };
      record.samples = s.received;
      record.missed_reads = clamp16(s.lost);
      record.interval_max_us = clamp16(s.window_us_max);
      record.interval_avg_us =
          clamp16(s.window_samples == 0
                      ? 0
                      : static_cast<std::uint32_t>(s.window_us_sum /
                                                   s.window_samples));
    }
    record.flags = static_cast<std::uint8_t>((tlog.dirty() ? 1U : 0U) |
                                             (tlog.full() ? 2U : 0U));
    tlog.append_stats(record);
  }
}

// Sensor task, before the sample the fix was applied to, so the recorder's
// GNSS and dyn records replay in the order the fusion saw them.
void record_speed_fix(const moto::motion::SpeedFix& fix, bool applied,
                      void* context) {
  (void)context;
  moto::device::GnssRecord record;
  record.t_rx_ms = fix.rx_ms;
  record.fix_ms = fix.fix_ms;
  record.speed_cms = static_cast<std::uint16_t>(
      std::clamp(std::lround(fix.mps * 100.0F), 0L, 65'535L));
  record.course_cdeg = 0xFFFFU;
  record.speed_acc_cms =
      fix.accuracy_mps >= 0.0F
          ? static_cast<std::uint16_t>(std::clamp(
                std::lround(fix.accuracy_mps * 100.0F), 0L, 65'534L))
          : 0xFFFFU;
  record.flags = static_cast<std::uint8_t>(
      moto::device::kGnssHasSpeed |
      (fix.timestamped ? moto::device::kGnssTimestamped : 0U) |
      (applied ? 0U : moto::device::kGnssRefused));
  tlog.append_gnss(record);
}

void demo_tick_task(void* context) {
  auto* bridge = static_cast<PhoneNavBridge*>(context);
  while (true) {
    const auto now_ms = static_cast<std::uint64_t>(esp_timer_get_time()) /
                        1'000U;
    if (g_ui_bench_lean_until_ms != 0) {
      static std::uint32_t bench_attempts = 0;
      static std::uint32_t bench_applied = 0;
      if (now_ms >= g_ui_bench_lean_until_ms) {
        g_ui_bench_lean_until_ms = 0;
        ESP_LOGI(kTag, "lean bench finished: attempts=%u applied=%u",
                 static_cast<unsigned>(bench_attempts),
                 static_cast<unsigned>(bench_applied));
      } else {
        ++bench_attempts;
        if (board_port_lock(1'000)) {
          ++bench_applied;
          const double phase = static_cast<double>(now_ms % 2'000U) / 2'000.0;
          moto_ui_state_t state{};
          state.page = MOTO_UI_PAGE_LEAN;
          state.mode = MOTO_UI_NAVIGATING;
          state.has_attitude = 1;
          state.lean_deg = static_cast<float>(
              30.0 * std::sin(phase * 2.0 * 3.141592653589793));
          state.speed_kph = 0;
          moto_nav_ui_set_state(&state);
          board_port_unlock();
        }
      }
      vTaskDelay(pdMS_TO_TICKS(25));
      continue;
    }
    bridge->update_demo(now_ms);
    // Match the display and route interpolation at 40 Hz. Keeping all three
    // clocks phase-compatible avoids periodic long frame gaps.
    vTaskDelay(pdMS_TO_TICKS(25));
  }
}

void power_button_task(void*) {
  std::uint64_t pressed_since_ms = 0;
  // The AXP2101 needs roughly a one-second press to power the board on, so
  // the task usually starts while the user is still holding PWR. Arm the
  // hold-to-shutdown detector only after the line has been seen low once;
  // otherwise the tail of the power-on press is counted as a new 3-second
  // hold and the freshly booted unit switches itself off.
  bool released_once = false;
  // The bezel ring (DESIGN_V2 §2.9). The gauge moves by a percent every few
  // minutes, so five seconds is plenty and keeps the shared I2C bus quiet.
  constexpr std::uint64_t kBatteryPollMs = 5'000;
  constexpr std::uint64_t kScreenLitNoteMs = 60'000;
  std::uint64_t battery_polled_ms = 0;
  std::uint64_t screen_lit_noted_ms = 0;
  bool battery_logged = false;
  while (true) {
    const std::uint64_t now_ms =
        static_cast<std::uint64_t>(esp_timer_get_time()) / 1'000U;
    if (screen_lit_noted_ms == 0 ||
        now_ms - screen_lit_noted_ms >= kScreenLitNoteMs) {
      screen_lit_noted_ms = now_ms;
      note_screen_lit();
    }
    if (battery_polled_ms == 0 || now_ms - battery_polled_ms >= kBatteryPollMs) {
      battery_polled_ms = now_ms;
      board_power_t power{};
      const bool ok = board_port_read_power(&power) == ESP_OK;
      const moto::device::BatteryRing ring = moto::device::battery_ring(
          ok, power.vbus, power.battery_present, power.percent);
      if (!g_ui_bench_battery) moto_nav_ui_set_battery(ring.source, ring.percent);
      if (!battery_logged) {
        // Once, so the first boot with a cell says what the gauge saw.
        battery_logged = true;
        ESP_LOGI(kTag, "power: read=%d vbus=%d battery=%d charging=%d gauge=%u%%",
                 ok ? 1 : 0, power.vbus ? 1 : 0, power.battery_present ? 1 : 0,
                 power.charging ? 1 : 0, static_cast<unsigned>(power.percent));
      }
    }
    if (!board_port_power_button_pressed()) {
      if (!released_once) {
        released_once = true;
        ESP_LOGI(kTag, "PWR ready: button released, hold detection armed");
      }
      pressed_since_ms = 0;
      vTaskDelay(pdMS_TO_TICKS(20));
      continue;
    }
    if (!released_once) {
      // Startup press still in progress; do not start the hold timer.
      vTaskDelay(pdMS_TO_TICKS(20));
      continue;
    }
    if (pressed_since_ms == 0) {
      pressed_since_ms = now_ms;
      ESP_LOGI(kTag, "PWR press started");
    } else if (now_ms - pressed_since_ms >= kPowerHoldMs) {
      ESP_LOGI(kTag, "PWR held for 3 seconds; requesting shutdown");
      if (board_port_lock(UINT32_MAX)) {
        moto_nav_ui_show_power_off_screen();
        board_port_unlock();
      }
      vTaskDelay(pdMS_TO_TICKS(300));
      const esp_err_t result = board_port_power_off();
      if (result != ESP_OK) {
        ESP_LOGE(kTag, "AXP2101 software power-off failed: %s",
                 esp_err_to_name(result));
      }

      // AXP2101 should remove the switched rails here. If this particular
      // board remains alive while USB is attached, blank the panel and enter
      // deep sleep after key release. GPIO3 high wakes it on the next press.
      vTaskDelay(pdMS_TO_TICKS(500));
      while (board_port_power_button_pressed()) {
        vTaskDelay(pdMS_TO_TICKS(20));
      }
      // The screen is about to go dark; mark it unlit and stamp that moment
      // for the retention clock, then flush the partial 4 KB sector before
      // the rail drops (up to 2 s of records live in RAM between flushes).
      note_screen_unlit();
      tlog.flush();
      ESP_ERROR_CHECK(esp_sleep_enable_ext1_wakeup_io(
          1ULL << 3, ESP_EXT1_WAKEUP_ANY_HIGH));
      esp_deep_sleep_start();
    }
    vTaskDelay(pdMS_TO_TICKS(20));
  }
}
} // namespace

extern "C" void app_main(void) {
  static_assert(MOTO_DISPLAY_WIDTH == MOTO_UI_CANVAS_WIDTH &&
                    MOTO_DISPLAY_HEIGHT == MOTO_UI_CANVAS_HEIGHT,
                "The board and shared UI canvas must have one resolution");
  static_assert(LV_COLOR_DEPTH == MOTO_DISPLAY_BITS_PER_PIXEL,
                "Firmware and Web must both build the shared UI as RGB565");

  ESP_LOGI(kTag, "starting %dx%d RGB565 firmware target",
           MOTO_DISPLAY_WIDTH, MOTO_DISPLAY_HEIGHT);
  moto_nav_ui_debug_set_clock(ui_debug_clock_us);

  // Read the stored settings before the first frame: the boot screen belongs
  // to the same paper system as the instrument, and booting it green (or in
  // the vendor palette) would flash a different product on every power cycle.
  const moto::device::DeviceSettings boot_settings =
      moto::device::load_settings();

  // `tlog clear` and the 6-hour unlit retention both erase here: no display,
  // BLE or IMU task exists yet, so the multi-second cache-off erase cannot
  // drop samples or stall a frame.
  std::uint64_t unlit_us = 0;
  const bool unlit_clear = screen_unlit_for_clear(&unlit_us);
  const bool requested_clear = moto::device::tlog_clear_requested();
  if (unlit_clear) {
    ESP_LOGW(kTag,
             "screen unlit for %llu min (limit 360); clearing tlog",
             static_cast<unsigned long long>(unlit_us / 60'000'000ULL));
  }
  if (requested_clear || unlit_clear) {
    ESP_LOGW(kTag, "tlog clear requested; erasing the 16 MB partition");
    if (tlog.erase_all()) {
      if (requested_clear) moto::device::clear_tlog_clear_request();
      ESP_LOGI(kTag, "tlog partition erased; recording will start at 0x0");
    } else {
      ESP_LOGE(kTag,
               "tlog erase failed; a requested clear stays for the next boot");
    }
  }

  const esp_err_t init_result = board_port_init();
  if (init_result != ESP_OK) {
    ESP_LOGE(kTag, "board initialization stopped before shared UI startup: %s",
             esp_err_to_name(init_result));
    vTaskDelete(nullptr);
    return;
  }

  log_hardware_report();

  lv_display_t *const display = board_port_get_display();
  if (display == nullptr) {
    ESP_LOGE(kTag, "board port returned no LVGL display");
    vTaskDelete(nullptr);
    return;
  }

  if (lv_display_get_horizontal_resolution(display) != MOTO_DISPLAY_WIDTH ||
      lv_display_get_vertical_resolution(display) != MOTO_DISPLAY_HEIGHT ||
      lv_display_get_color_format(display) != LV_COLOR_FORMAT_RGB565) {
    ESP_LOGE(kTag, "board display violates the shared RGB565 portability "
                   "contract");
    vTaskDelete(nullptr);
    return;
  }

  if (!board_port_lock(UINT32_MAX)) {
    ESP_LOGE(kTag, "could not acquire LVGL lock");
    vTaskDelete(nullptr);
    return;
  }

  moto_nav_ui_set_boot_theme(boot_theme_for(boot_settings.theme));
  moto_nav_ui_show_boot_screen();
  // Commit the deliberately black first animation frame while the physical
  // panel is still hidden, then reveal it.  This removes the white frame that
  // used to leak from LVGL's default startup screen.
  lv_refr_now(display);
  const esp_err_t reveal_result = board_port_reveal_display();
  if (reveal_result != ESP_OK) {
    ESP_LOGE(kTag, "could not reveal boot animation: %s",
             esp_err_to_name(reveal_result));
  } else {
    // This is the first moment the panel is actually lit. The 6-hour
    // retention clock starts here, not at chip reset.
    note_screen_lit();
  }
  board_port_unlock();

  // Give the display worker enough time to commit the monochrome power-on
  // frame before constructing the full production UI.
  vTaskDelay(pdMS_TO_TICKS(1'250));

  if (!board_port_lock(UINT32_MAX)) {
    ESP_LOGE(kTag, "could not reacquire LVGL lock after boot screen");
    vTaskDelete(nullptr);
    return;
  }

  // The tree reset clears the theme and the building visibility, so the boot
  // settings are applied again after create(); the boot screen used a copy of
  // the same settings before the tree existed.
  moto_nav_ui_create();
  static moto::nav::NavPresenter presenter;
  static PhoneNavBridge phone_bridge(presenter);
  static BleNavTransport transport;
  g_bridge = &phone_bridge;
  // NavPresenter's constructor already applies an empty snapshot, so no
  // second full NavSnapshot is kept in static storage just to repaint the
  // same zeros. The map window is several kilobytes and internal DRAM is the
  // scarce pool; the UI state lives in PSRAM, this one does not need to.
  presenter.apply_to_lvgl();
  // Replay after create(): the tree reset clears the theme and the building
  // visibility, so applying before it would leave defaults on screen.
  phone_bridge.apply_settings(boot_settings, false);
  phone_bridge.set_settings_store(moto::device::save_settings);
  phone_bridge.install_ui_callbacks();
  // Commit the instrument's first frame before the BLE controller starts. The
  // first UI flush used to race NimBLE initialization and the SPI DMA
  // underflowed, leaving the panel on the boot screen with no retry: a blank
  // paper field and a render task waiting for an invalidation that never came.
  lv_refr_now(display);
  board_port_unlock();

  if (!phone_bridge.start_renderer()) {
    ESP_LOGE(kTag, "UI renderer startup failed; BLE was not started");
    vTaskDelete(nullptr);
    return;
  }

  phone_bridge.set_sender(BleNavTransport::send_from_bridge, &transport);
  transport.set_callbacks(receive_phone_message, update_phone_link,
                          &phone_bridge);
  // The music page follows whichever app owns the iPhone's Now Playing,
  // through Apple Media Service on the same bonded link. The phone app's
  // Apple Music relay remains the fallback while AMS is not available.
  static AmsClient ams_client;
  ams_client.set_listener(
      [](const moto::ble::MediaState& state, bool available, void* context) {
        static_cast<PhoneNavBridge*>(context)->on_system_media(state,
                                                               available);
      },
      &phone_bridge);
  phone_bridge.set_music_key_handler(AmsClient::key_handler, &ams_client);
  transport.set_gap_observer(AmsClient::gap_observer, &ams_client);
  const esp_err_t ble_result = transport.start();
  if (ble_result != ESP_OK) {
    ESP_LOGE(kTag, "BLE startup failed; display remains in offline mode: %s",
             esp_err_to_name(ble_result));
  }

  static MotionHeadingSensor motion_sensor;
  // Restore the captured bracket before the sensor task starts: every sample
  // is mapped through this basis, so applying it later would mix frames.
  const moto::device::StoredMount stored_mount =
      moto::device::load_mount_basis();
  g_sensor = &motion_sensor;
  if (stored_mount.valid) {
    motion_sensor.set_mount(stored_mount.calibration());
    g_calibration_unverified = false;
  }
  moto_nav_ui_set_lean_flags(g_calibration_unverified ? 1 : 0, 0, 0, 0);
  moto::device::TlogSessionInfo session_info;
  const bool fifo_wanted = moto::device::load_imu_fifo_enabled();
  // Latch polling is the production chain (mode 2); FIFO remains an A/B
  // option (mode 1); plain polling is only the fallback (mode 0).
  session_info.mode = fifo_wanted ? 1 : 2;
  session_info.odr_hz = 448;
  session_info.sample_hz = 112;
  session_info.mount = mount_record_from(motion_sensor.mount());
  if (const esp_app_desc_t* desc = esp_app_get_description()) {
    std::memcpy(&session_info.firmware_hash, desc->app_elf_sha256, 4);
  }
  tlog.begin(session_info);
  phone_bridge.set_speed_listener(
      [](const moto::motion::SpeedFix& fix, void* context) {
        static_cast<MotionHeadingSensor*>(context)->add_speed_fix(fix);
      },
      &motion_sensor);
  motion_sensor.set_fix_callback(record_speed_fix, nullptr);
  motion_sensor.set_mode_callback(lean_mode_changed, nullptr);
  motion_sensor.set_calibration_callback(save_lean_calibration, nullptr);
  moto_nav_ui_set_lean_calibration_callback(request_lean_calibration,
                                            &motion_sensor);
  const esp_err_t motion_result = motion_sensor.start(update_motion_heading,
                                                      &phone_bridge);
  if (motion_result != ESP_OK) {
    ESP_LOGW(kTag, "QMI8658 heading assist could not start: %s",
             esp_err_to_name(motion_result));
  } else {
    if (xTaskCreate(serial_command_task, "moto_cmd", 4'096, &motion_sensor, 1,
                    &g_task_cmd) != pdPASS) {
      ESP_LOGW(kTag, "serial command task could not start");
    }
    if (xTaskCreate(ui_flags_task, "moto_ui_flags", 3'072, nullptr, 1,
                    &g_task_ui_flags) != pdPASS) {
      ESP_LOGW(kTag, "UI flag task could not start");
    }
  }

  // Demo generation fills the retained snapshot in place, but geometry and
  // LVGL projection still use deeper C++ call frames than a trivial task.
  if (xTaskCreate(demo_tick_task, "moto_demo", 6'144, &phone_bridge, 2,
                  &g_task_demo) != pdPASS) {
    ESP_LOGW(kTag, "navigation demo task could not start");
  }
  if (xTaskCreate(power_button_task, "moto_power", 3'072, nullptr, 3,
                  &g_task_power) != pdPASS) {
    ESP_LOGW(kTag, "PWR long-hold task could not start");
  }

  ESP_LOGI(kTag,
           "iPhone BLE + QMI heading -> NavPresenter -> shared LVGL running");
  vTaskDelete(nullptr);
}
