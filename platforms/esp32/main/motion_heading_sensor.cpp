#include "motion_heading_sensor.h"

// The vendored qmi8658.h is guarded by an #endif placed before its
// extern "C" close, so a second include emits a stray brace. It is included
// exactly once, through this header, and M_PI is cleared here.
#ifdef M_PI
#undef M_PI
#endif

#include <algorithm>
#include <cmath>

#include "bsp/esp-bsp.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "settings_store.h"

namespace {
constexpr char kTag[] = "motion_heading";
constexpr TickType_t kPollDelay = pdMS_TO_TICKS(8);
constexpr TickType_t kFifoPollDelay = pdMS_TO_TICKS(8);
constexpr std::uint32_t kWarmupMs = 200;
constexpr float kGravity = 9.80665F;

// Board axis regression is still pending. These are the sensor-frame forward
// and up axes the adapter has always assumed: sensor +y is up and sensor -z is
// forward. The basis is usable but not calibrated, so CAL_OK stays false until
// the rider runs the v2 calibration.
constexpr moto::motion::Vec3f kSensorForward{0.0F, 0.0F, -1.0F};
constexpr moto::motion::Vec3f kSensorUp{0.0F, 1.0F, 0.0F};

std::uint64_t monotonic_ms() {
  return static_cast<std::uint64_t>(esp_timer_get_time()) / 1'000U;
}

}  // namespace

void MotionHeadingSensor::set_mount(
    const moto::motion::MountCalibration& mount) {
  mount_ = mount;
  has_mount_ = true;
  lean_.set_mount(mount_);
}

moto::motion::MountCalibration MotionHeadingSensor::mount() const {
  return mount_;
}

void MotionHeadingSensor::request_static_calibration() {
  calibration_requested_ = true;
}

void MotionHeadingSensor::set_calibration_callback(
    CalibrationCallback callback, void* context) {
  calibration_callback_ = callback;
  calibration_context_ = context;
}

void MotionHeadingSensor::request_fifo_mode(bool enabled) {
  fifo_request_ = enabled ? 1 : 0;
}

void MotionHeadingSensor::set_mode_callback(ModeCallback callback,
                                            void* context) {
  mode_callback_ = callback;
  mode_context_ = context;
}

esp_err_t MotionHeadingSensor::start(SampleCallback callback, void* context) {
  if (callback == nullptr) return ESP_ERR_INVALID_ARG;
  if (started_) return ESP_ERR_INVALID_STATE;
  callback_ = callback;
  callback_context_ = context;
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
  const esp_err_t init = qmi8658_init(
      &device_, bsp_i2c_get_handle(), QMI8658_ADDRESS_HIGH);
  if (init != ESP_OK) {
    ESP_LOGW(kTag, "QMI8658 unavailable; using phone course only: %s",
             esp_err_to_name(init));
    return;
  }
  ESP_ERROR_CHECK_WITHOUT_ABORT(
      qmi8658_set_accel_range(&device_, QMI8658_ACCEL_RANGE_4G));
  ESP_ERROR_CHECK_WITHOUT_ABORT(
      qmi8658_set_accel_odr(&device_, QMI8658_ACCEL_ODR_125HZ));
  ESP_ERROR_CHECK_WITHOUT_ABORT(
      qmi8658_set_gyro_range(&device_, QMI8658_GYRO_RANGE_512DPS));
  ESP_ERROR_CHECK_WITHOUT_ABORT(
      qmi8658_set_gyro_odr(&device_, QMI8658_GYRO_ODR_125HZ));
  qmi8658_set_accel_unit_mps2(&device_, true);
  qmi8658_set_gyro_unit_dps(&device_, true);

  mount_ = has_mount_ ? mount_ : [] {
    moto::motion::MountCalibration assumed;
    assumed.set_basis(kSensorForward, kSensorUp, false);
    return assumed;
  }();
  lean_.set_mount(mount_);
  have_emit_ = false;
  temperature_countdown_ = 0;

  bool fifo_enabled = moto::device::load_imu_fifo_enabled();
  while (true) {
    if (fifo_request_ >= 0) {
      fifo_enabled = fifo_request_ == 1;
      fifo_request_ = -1;
      moto::device::save_imu_fifo_enabled(fifo_enabled);
    }
    bool restart = false;
    if (fifo_enabled) {
      moto::motion::Qmi8658Fifo fifo;
      if (fifo.begin(&device_)) {
        fifo_active_ = true;
        fifo_reason_ = nullptr;
        if (mode_callback_ != nullptr) mode_callback_(true, mode_context_);
        fifo_loop(fifo, restart);
        fifo_active_ = false;
        fifo_stats_ = fifo.stats();
        if (!restart && fifo.fallback_reason() != nullptr) {
          fifo_reason_ = fifo.fallback_reason();
          fifo_enabled = false;
          if (mode_callback_ != nullptr) mode_callback_(false, mode_context_);
          ESP_LOGW(kTag, "falling back to polling: %s", fifo_reason_);
        }
      } else {
        fifo_reason_ = fifo.fallback_reason();
        fifo_enabled = false;
        if (mode_callback_ != nullptr) mode_callback_(false, mode_context_);
        ESP_LOGW(kTag, "FIFO unavailable: %s",
                 fifo_reason_ != nullptr ? fifo_reason_ : "init failed");
      }
    }
    if (!fifo_enabled) poll_loop(restart);
    if (restart) continue;
  }
}

void MotionHeadingSensor::run_calibration() {
  calibration_requested_ = false;
  moto::motion::MountCalibration candidate = mount_;
  candidate.begin_static();
  bool ok = false;
  const std::uint64_t started = monotonic_ms();
  while (!ok && monotonic_ms() - started < 4'000U) {
    bool ready = false;
    qmi8658_data_t sample{};
    if (qmi8658_is_data_ready(&device_, &ready) == ESP_OK && ready &&
        qmi8658_read_sensor_data(&device_, &sample) == ESP_OK) {
      ok = candidate.observe_static(
          {sample.accelX, sample.accelY, sample.accelZ},
          {sample.gyroX, sample.gyroY, sample.gyroZ}, 0.008F);
    }
    vTaskDelay(pdMS_TO_TICKS(8));
  }
  if (ok) {
    mount_ = candidate;
    lean_.set_mount(mount_);
  }
  ESP_LOGI(kTag, "static calibration %s", ok ? "captured" : "rejected");
  if (calibration_callback_ != nullptr) {
    calibration_callback_(mount_, ok, candidate.reject_reason,
                          calibration_context_);
  }
}

void MotionHeadingSensor::emit(const RawImuSample& raw,
                               std::uint64_t sample_ms) {
  const float dt =
      have_emit_
          ? std::clamp(static_cast<float>(sample_ms - last_emit_ms_) / 1000.0F,
                       0.002F, 0.05F)
          : 1.0F / 125.0F;
  last_emit_ms_ = sample_ms;
  have_emit_ = true;
  lean_.step({raw.gyro_dps[0], raw.gyro_dps[1], raw.gyro_dps[2]},
             {raw.accel_mps2[0], raw.accel_mps2[1], raw.accel_mps2[2]}, dt,
             moto::motion::SpeedSample{});
  const moto::motion::LeanOutput& out = lean_.output();
  last_lean_deg_ = out.lean_deg;
  last_pitch_deg_ = out.pitch_deg;
  last_quality_ = out.quality;
  last_flags_ = out.flags;

  moto::motion::AttitudeEstimate attitude;
  attitude.lean_deg = out.lean_deg;
  attitude.pitch_deg = out.pitch_deg;
  const moto::motion::Vec3f accel_body =
      mount_.to_body({raw.accel_mps2[0], raw.accel_mps2[1], raw.accel_mps2[2]});
  const float pitch_rad = out.pitch_deg * 0.017453292519943295F;
  // Specific force along forward minus the gravity component along forward.
  attitude.longitudinal_g =
      (accel_body.x - std::sin(pitch_rad) * kGravity) / kGravity;
  attitude.lateral_g = out.lateral_g;
  attitude.lean_eff_deg =
      std::isfinite(out.lean_eff_deg) ? out.lean_eff_deg : 0.0F;
  attitude.lean_rate_dps = out.lean_rate_dps;
  attitude.has_lean_eff = std::isfinite(out.lean_eff_deg);
  attitude.quality = out.quality;
  attitude.flags = out.flags;
  if (!fifo_active_) attitude.flags |= moto::motion::kLeanImuFallback;
  attitude.valid = out.valid;
  callback_(-out.yaw_rate_dps, attitude, raw, sample_ms, callback_context_);
}

void MotionHeadingSensor::fifo_loop(moto::motion::Qmi8658Fifo& fifo,
                                    bool& restart) {
  constexpr std::uint32_t kOdrHz10 = 4484;  // 448.4 Hz in 0.1 Hz
  constexpr float kPeriodUs = 1.0e6F / 448.4F;
  moto::motion::FifoFrame frames[16];
  const std::uint64_t warmup_end = monotonic_ms() + kWarmupMs;
  TickType_t next = xTaskGetTickCount();
  while (true) {
    if (fifo_request_ >= 0 || !fifo.active()) {
      restart = true;
      return;
    }
    if (calibration_requested_) run_calibration();
    const std::size_t count = fifo.poll(frames, 16);
    if (count >= 4) {
      const std::uint64_t now = monotonic_ms();
      const std::size_t groups = count / 4;
      for (std::size_t group = 0; group < groups; ++group) {
        RawImuSample raw;
        for (int i = 0; i < 3; ++i) {
          raw.accel_mps2[i] = 0.0F;
          raw.gyro_dps[i] = 0.0F;
          for (int k = 0; k < 4; ++k) {
            raw.accel_mps2[i] += frames[group * 4 + k].accel_mps2[i] * 0.25F;
            raw.gyro_dps[i] += frames[group * 4 + k].gyro_dps[i] * 0.25F;
          }
        }
        if (--temperature_countdown_ <= 0) {
          temperature_countdown_ = 112;
          float measured = 0.0F;
          if (qmi8658_read_temp(&device_, &measured) == ESP_OK) {
            temperature_c_ = measured;
          }
        }
        raw.temp_c = temperature_c_;
        const std::size_t newest = group * 4 + 3;
        const std::uint64_t age_us =
            static_cast<std::uint64_t>((count - 1 - newest) * kPeriodUs);
        const std::uint64_t sample_ms = now - age_us / 1000U;
        if (sample_ms > warmup_end) emit(raw, sample_ms);
      }
    }
    (void)kOdrHz10;
    xTaskDelayUntil(&next, kFifoPollDelay);
  }
}

void MotionHeadingSensor::poll_loop(bool& restart) {
  TickType_t next_sample_tick = xTaskGetTickCount();
  while (true) {
    if (fifo_request_ >= 0) {
      restart = true;
      return;
    }
    if (calibration_requested_) run_calibration();
    bool ready = false;
    qmi8658_data_t data{};
    const esp_err_t result = qmi8658_is_data_ready(&device_, &ready);
    if (result == ESP_OK && ready &&
        qmi8658_read_sensor_data(&device_, &data) == ESP_OK) {
      RawImuSample raw;
      raw.accel_mps2[0] = data.accelX;
      raw.accel_mps2[1] = data.accelY;
      raw.accel_mps2[2] = data.accelZ;
      raw.gyro_dps[0] = data.gyroX;
      raw.gyro_dps[1] = data.gyroY;
      raw.gyro_dps[2] = data.gyroZ;
      if (--temperature_countdown_ <= 0) {
        temperature_countdown_ = 125;
        float measured = 0.0F;
        if (qmi8658_read_temp(&device_, &measured) == ESP_OK) {
          temperature_c_ = measured;
        }
      }
      raw.temp_c = temperature_c_;
      emit(raw, monotonic_ms());
    } else if (result != ESP_OK) {
      ESP_LOGW(kTag, "QMI8658 read error: %s", esp_err_to_name(result));
    }
    xTaskDelayUntil(&next_sample_tick, kPollDelay);
  }
}
