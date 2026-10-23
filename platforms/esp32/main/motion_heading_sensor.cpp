#include "motion_heading_sensor.h"
#include "lean_estimator.hpp"

#include <algorithm>
#include <cmath>

#include "bsp/esp-bsp.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// qmi8658.h defines M_PI globally. This adapter does not use that macro and
// removes it after the official component header to keep C++ <cmath> clean.
#include "qmi8658.h"
#ifdef M_PI
#undef M_PI
#endif

namespace {
constexpr char kTag[] = "motion_heading";
constexpr TickType_t kSampleDelay = pdMS_TO_TICKS(8);  // 125 Hz target.

// Board axis regression is still pending. These are the sensor-frame forward
// and up axes the adapter has always assumed: sensor +y is up and sensor -z is
// forward. This basis is usable but not calibrated, so the estimator reports
// CAL_OK = false until the rider runs the v2 calibration (DESIGN_V2 §3.3).
constexpr moto::motion::Vec3f kSensorForward{0.0F, 0.0F, -1.0F};
constexpr moto::motion::Vec3f kSensorUp{0.0F, 1.0F, 0.0F};

std::uint64_t monotonic_ms() {
  return static_cast<std::uint64_t>(esp_timer_get_time()) / 1'000U;
}
}  // namespace

esp_err_t MotionHeadingSensor::start(SampleCallback callback, void* context) {
  if (callback == nullptr) return ESP_ERR_INVALID_ARG;
  if (started_) return ESP_ERR_INVALID_STATE;
  callback_ = callback;
  callback_context_ = context;
  // Sensor callbacks reach the presenter and LVGL adapter. Keep enough margin
  // for those nested frames even though the large map snapshot now remains in
  // PhoneNavBridge member storage.
  if (xTaskCreate(task_entry, "moto_qmi_heading", 8'192, this, 3, nullptr) !=
      pdPASS) {
    callback_ = nullptr;
    callback_context_ = nullptr;
    return ESP_ERR_NO_MEM;
  }
  started_ = true;
  return ESP_OK;
}

void MotionHeadingSensor::task_entry(void* context) {
  static_cast<MotionHeadingSensor*>(context)->run();
  vTaskDelete(nullptr);
}

void MotionHeadingSensor::run() {
  qmi8658_dev_t device{};
  const esp_err_t init = qmi8658_init(
      &device, bsp_i2c_get_handle(), QMI8658_ADDRESS_HIGH);
  if (init != ESP_OK) {
    ESP_LOGW(kTag, "QMI8658 unavailable; using phone course only: %s",
             esp_err_to_name(init));
    return;
  }
  // The v2 sample-rate change (ODR + LPF + FIFO) is a separate hardware slice;
  // this loop is still the 125 Hz polling path, so the integrator runs at the
  // rate it actually receives samples.
  ESP_ERROR_CHECK_WITHOUT_ABORT(
      qmi8658_set_accel_range(&device, QMI8658_ACCEL_RANGE_4G));
  ESP_ERROR_CHECK_WITHOUT_ABORT(
      qmi8658_set_accel_odr(&device, QMI8658_ACCEL_ODR_125HZ));
  ESP_ERROR_CHECK_WITHOUT_ABORT(
      qmi8658_set_gyro_range(&device, QMI8658_GYRO_RANGE_512DPS));
  ESP_ERROR_CHECK_WITHOUT_ABORT(
      qmi8658_set_gyro_odr(&device, QMI8658_GYRO_ODR_125HZ));
  qmi8658_set_accel_unit_mps2(&device, true);
  qmi8658_set_gyro_unit_dps(&device, true);

  moto::motion::MountCalibration mount;
  mount.set_basis(kSensorForward, kSensorUp, false);
  moto::motion::LeanEstimator lean;
  lean.set_mount(mount);

  qmi8658_data_t data{};
  int consecutive_failures = 0;
  std::uint64_t last_sample_ms = monotonic_ms();
  TickType_t next_sample_tick = xTaskGetTickCount();
  while (true) {
    bool ready = false;
    esp_err_t result = qmi8658_is_data_ready(&device, &ready);
    if (result == ESP_OK && ready) {
      result = qmi8658_read_sensor_data(&device, &data);
      if (result == ESP_OK) {
        consecutive_failures = 0;
        const std::uint64_t now = monotonic_ms();
        const float dt = std::clamp(
            static_cast<float>(now - last_sample_ms) / 1'000.0F, 0.002F,
            0.05F);
        last_sample_ms = now;

        // Speed is not plumbed into this sensor task yet: v2 SpeedFusion is a
        // separate slice, so the estimator runs on gyro plus static/zero
        // observations. It no longer derives Lean from the accelerometer angle
        // alone, which is the v1 error (DESIGN_V2 §3.5.1).
        lean.step({data.gyroX, data.gyroY, data.gyroZ},
                  {data.accelX, data.accelY, data.accelZ}, dt,
                  moto::motion::SpeedSample{});
        const moto::motion::LeanOutput& out = lean.output();

        moto::motion::AttitudeEstimate attitude;
        attitude.lean_deg = out.lean_deg;
        attitude.pitch_deg = out.pitch_deg;
        const moto::motion::Vec3f accel_body = mount.to_body(
            {data.accelX, data.accelY, data.accelZ});
        const float pitch_rad = out.pitch_deg * 0.017453292519943295F;
        attitude.longitudinal_g =
            (accel_body.x +
             std::sin(pitch_rad) * moto::motion::kStandardGravity) /
            moto::motion::kStandardGravity;
        attitude.lateral_g = out.lateral_g;
        attitude.valid = out.valid;
        // The bridge's heading fusion is clockwise-positive.
        callback_(-out.yaw_rate_dps, attitude, now, callback_context_);
      }
    }
    if (result != ESP_OK && ++consecutive_failures == 25) {
      ESP_LOGW(kTag, "QMI8658 read errors continue: %s",
               esp_err_to_name(result));
    }
    xTaskDelayUntil(&next_sample_tick, kSampleDelay);
  }
}
