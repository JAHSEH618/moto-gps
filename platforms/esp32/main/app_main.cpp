#include <cmath>
#include <cstdint>

#include "ble_nav_transport_nimble.h"
#include "bsp/esp-bsp.h"
#include "board_port.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "moto_nav_presenter.hpp"
#include "moto_nav_ui.h"
#include "motion_heading_sensor.h"
#include "phone_nav_bridge.h"
#include "settings_store.h"
#include "esp_app_desc.h"

#include <cstdio>
#include <cstring>
#include "tlog_logger.hpp"

#include "driver/i2c_master.h"
#include "esp_flash.h"

namespace {
constexpr char kTag[] = "moto_gps";

// Development recorder. Absent when the tlog partition is missing or its
// above-16-MB self-test failed; every append is then a no-op.
static moto::device::TlogLogger tlog;
static bool g_calibration_unverified = true;
static MotionHeadingSensor* g_sensor = nullptr;

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

void lean_mode_changed(bool fifo_active, void*) {
  // The session record is the only honest place for a mid-session fallback:
  // the first block was written before this happened.
  tlog.note_mode(fifo_active ? 1 : 0, fifo_active ? 448 : 125,
                 fifo_active ? 112 : 125);
}

void ui_flags_task(void*) {
  while (true) {
    moto_nav_ui_set_lean_flags(
        g_calibration_unverified ? 1 : 0,
        (g_sensor != nullptr && !g_sensor->fifo_active()) ? 1 : 0,
        tlog.full() ? 1 : 0);
    vTaskDelay(pdMS_TO_TICKS(500));
  }
}

void serial_command_task(void* context) {
  auto* sensor = static_cast<MotionHeadingSensor*>(context);
  char line[64];
  std::size_t used = 0;
  while (true) {
    const int c = std::fgetc(stdin);
    if (c == EOF) {
      vTaskDelay(pdMS_TO_TICKS(50));
      continue;
    }
    if (c == '\r') continue;
    if (c != '\n') {
      if (used + 1 < sizeof(line)) line[used++] = static_cast<char>(c);
      continue;
    }
    line[used] = '\0';
    used = 0;
    const moto::motion::FifoStats& stats = sensor->fifo_stats();
    if (std::strcmp(line, "imu stats") == 0) {
      ESP_LOGI(kTag,
               "imu mode=%s reason=%s batches=%u received=%u produced=%u "
               "lost=%u overflow=%u window_max_us=%u window_avg_us=%u",
               sensor->fifo_active() ? "fifo" : "poll",
               sensor->fifo_fallback_reason() != nullptr
                   ? sensor->fifo_fallback_reason()
                   : "-",
               static_cast<unsigned>(stats.batches),
               static_cast<unsigned>(stats.received),
               static_cast<unsigned>(stats.produced),
               static_cast<unsigned>(stats.lost),
               static_cast<unsigned>(stats.overflow),
               static_cast<unsigned>(stats.window_us_max),
               static_cast<unsigned>(stats.window_samples == 0
                                         ? 0
                                         : stats.window_us_sum /
                                               stats.window_samples));
    } else if (std::strcmp(line, "imu lean") == 0) {
      ESP_LOGI(kTag, "lean=%.2f pitch=%.2f quality=%u flags=0x%04x mode=%s",
               static_cast<double>(sensor->last_lean_deg()),
               static_cast<double>(sensor->last_pitch_deg()),
               static_cast<unsigned>(sensor->last_quality()),
               static_cast<unsigned>(sensor->last_flags()),
               sensor->fifo_active() ? "fifo" : "poll");
    } else if (std::strcmp(line, "imu fifo off") == 0) {
      sensor->request_fifo_mode(false);
      ESP_LOGI(kTag, "FIFO disabled; restarting acquisition");
    } else if (std::strcmp(line, "imu fifo on") == 0) {
      sensor->request_fifo_mode(true);
      ESP_LOGI(kTag, "FIFO enabled; restarting acquisition");
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
  static_cast<PhoneNavBridge*>(context)->on_imu_sample(
      heading_rate_dps, attitude, sample_ms);
  tlog.append(sample_ms, raw, attitude);
}

void demo_tick_task(void* context) {
  auto* bridge = static_cast<PhoneNavBridge*>(context);
  while (true) {
    const auto now_ms = static_cast<std::uint64_t>(esp_timer_get_time()) /
                        1'000U;
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
  while (true) {
    const std::uint64_t now_ms =
        static_cast<std::uint64_t>(esp_timer_get_time()) / 1'000U;
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
      // Flush the partial 4 KB sector before the rail drops: up to 2 s of
      // records live in RAM between flushes.
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

  // Read the stored settings before the first frame: the boot screen belongs
  // to the same paper system as the instrument, and booting it green (or in
  // the vendor palette) would flash a different product on every power cycle.
  const moto::device::DeviceSettings boot_settings =
      moto::device::load_settings();

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
  // NavSnapshot contains the complete bounded roads/buildings window and is
  // several kilobytes.  A temporary here inflates app_main's stack frame for
  // the entire display/BSP startup call chain, which can trip the FreeRTOS
  // main-task canary before this line is even reached.  Keep the immutable
  // initial state in static storage just like the bridge's retained snapshots.
  static const moto::nav::NavSnapshot initial_snapshot{};
  presenter.update(initial_snapshot);
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
  moto_nav_ui_set_lean_flags(g_calibration_unverified ? 1 : 0, 0, 0);
  moto::device::TlogSessionInfo session_info;
  const bool fifo_wanted = moto::device::load_imu_fifo_enabled();
  session_info.mode = fifo_wanted ? 1 : 0;
  session_info.odr_hz = fifo_wanted ? 448 : 125;
  session_info.sample_hz = fifo_wanted ? 112 : 125;
  session_info.mount = mount_record_from(motion_sensor.mount());
  if (const esp_app_desc_t* desc = esp_app_get_description()) {
    std::memcpy(&session_info.firmware_hash, desc->app_elf_sha256, 4);
  }
  tlog.begin(session_info);
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
                    nullptr) != pdPASS) {
      ESP_LOGW(kTag, "serial command task could not start");
    }
    if (xTaskCreate(ui_flags_task, "moto_ui_flags", 3'072, nullptr, 1,
                    nullptr) != pdPASS) {
      ESP_LOGW(kTag, "UI flag task could not start");
    }
  }

  // Demo generation fills the retained snapshot in place, but geometry and
  // LVGL projection still use deeper C++ call frames than a trivial task.
  if (xTaskCreate(demo_tick_task, "moto_demo", 6'144, &phone_bridge, 2,
                  nullptr) != pdPASS) {
    ESP_LOGW(kTag, "navigation demo task could not start");
  }
  if (xTaskCreate(power_button_task, "moto_power", 3'072, nullptr, 3,
                  nullptr) != pdPASS) {
    ESP_LOGW(kTag, "PWR long-hold task could not start");
  }

  ESP_LOGI(kTag,
           "iPhone BLE + QMI heading -> NavPresenter -> shared LVGL running");
  vTaskDelete(nullptr);
}
