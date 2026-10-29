#include "motion_heading_sensor.h"

// qmi8658.h defines M_PI as a float macro; the estimator uses its own double
// constant, so the macro is cleared before the standard headers are parsed.
// The vendored header's extern "C" guard was fixed in place (the old #endif
// sat before the closing brace, so a second include emitted a stray brace).
#ifdef M_PI
#undef M_PI
#endif

#include <algorithm>
#include <cmath>
#include <cstring>

#include "bsp/esp-bsp.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "settings_store.h"

namespace {
constexpr char kTag[] = "motion_heading";
constexpr TickType_t kPollDelay = pdMS_TO_TICKS(8);
constexpr TickType_t kFifoPollDelay = pdMS_TO_TICKS(4);
constexpr std::uint32_t kWarmupMs = 200;
constexpr float kGravity = 9.80665F;

// The assumed mount before the rider calibrates: screen upright and facing
// the rider, so forward is into the glass and up is the screen's top (sensor
// +x, measured; it used to be guessed as +y). Usable but not calibrated, so
// CAL_OK stays false until the rider runs the v2 calibration.
constexpr moto::motion::Vec3f kSensorForward = moto::motion::kSensorIntoScreen;
constexpr moto::motion::Vec3f kSensorUp = moto::motion::kSensorScreenTop;

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

const char* MotionHeadingSensor::mode_name() const {
  switch (acquisition_mode()) {
    case AcquisitionMode::Latch:
      return "latch";
    case AcquisitionMode::Fifo:
      return "fifo";
    case AcquisitionMode::Poll:
    default:
      return "poll";
  }
}

const char* MotionHeadingSensor::fallback_reason() const {
  if (acquisition_mode() == AcquisitionMode::Poll) {
    if (latch_failed_ && latch_reason_ != nullptr) return latch_reason_;
    if (fifo_reason_ != nullptr) return fifo_reason_;
  }
  return nullptr;
}

void MotionHeadingSensor::reset_imu_stats() {
  latch_.reset_stats();
}

esp_err_t MotionHeadingSensor::start(SampleCallback callback, void* context) {
  if (callback == nullptr) return ESP_ERR_INVALID_ARG;
  if (started_) return ESP_ERR_INVALID_STATE;
  callback_ = callback;
  callback_context_ = context;
  // Above the LVGL adapter task (6): a 0.4 ms I2C read every 4.46 ms must
  // preempt a long page render, otherwise a 100 ms frame starves the latch and
  // the acquisition acceptance (lost reads) fails while the rider looks at the
  // horizon. The tlog page write inherits this priority too. Core 1 with the
  // LVGL task (DESIGN_V2 §13), away from the NimBLE host and controller.
  if (xTaskCreatePinnedToCore(task_entry, "moto_qmi_heading", 8'192, this, 7,
                              nullptr, 1) != pdPASS) {
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

void MotionHeadingSensor::announce_mode(AcquisitionMode mode) {
  mode_ = static_cast<std::uint8_t>(mode);
  latch_active_ = mode == AcquisitionMode::Latch;
  if (mode_callback_ != nullptr) {
    mode_callback_(static_cast<std::uint8_t>(mode), mode_context_);
  }
}

void MotionHeadingSensor::configure_poll_mode() {
  // Restore the 125 Hz polling shape and leave syncSmpl. CTRL1 keeps
  // auto-increment on and byte order little-endian, which is what the vendored
  // read helper decodes.
  ESP_ERROR_CHECK_WITHOUT_ABORT(
      qmi8658_set_accel_range(&device_, QMI8658_ACCEL_RANGE_4G));
  ESP_ERROR_CHECK_WITHOUT_ABORT(
      qmi8658_set_accel_odr(&device_, QMI8658_ACCEL_ODR_125HZ));
  ESP_ERROR_CHECK_WITHOUT_ABORT(
      qmi8658_set_gyro_range(&device_, QMI8658_GYRO_RANGE_1024DPS));
  ESP_ERROR_CHECK_WITHOUT_ABORT(
      qmi8658_set_gyro_odr(&device_, QMI8658_GYRO_ODR_125HZ));
  qmi8658_write_register(&device_, 0x02, 0x40);  // AI on, BE off
  qmi8658_write_register(&device_, 0x06, 0x00);  // no LPF on the fallback path
  qmi8658_write_register(&device_, 0x09, 0x00);  // INT1 handshake default
  qmi8658_write_register(&device_, 0x08, 0x03);  // accel + gyro, sync off
}

void MotionHeadingSensor::run() {
  task_handle_ = xTaskGetCurrentTaskHandle();
  const esp_err_t init = qmi8658_init(
      &device_, bsp_i2c_get_handle(), QMI8658_ADDRESS_HIGH);
  if (init != ESP_OK) {
    ESP_LOGW(kTag, "QMI8658 unavailable; using phone course only: %s",
             esp_err_to_name(init));
    return;
  }
  // The vendored driver returns mg unless told otherwise. Everything here is
  // m/s^2: in mg the static capture saw ~1000 and rejected every CAL, and the
  // poll fallback fed the estimator a 100x gravity vector. The latch and FIFO
  // readers decode the registers themselves and were never affected.
  qmi8658_set_accel_unit_mps2(&device_, true);
  configure_poll_mode();

  mount_ = has_mount_ ? mount_ : [] {
    moto::motion::MountCalibration assumed;
    assumed.set_basis(kSensorForward, kSensorUp, false);
    return assumed;
  }();
  lean_.set_mount(mount_);
  // Start from the offset learned on the last ride instead of zero, so the
  // first corner after power-on does not integrate the part's raw offset.
  moto::motion::Vec3f stored_bias{};
  if (moto::device::load_gyro_bias(stored_bias)) {
    lean_.set_bias_sensor_dps(stored_bias);
    saved_bias_dps_ = stored_bias;
    bias_saved_ = true;
    ESP_LOGI(kTag, "gyro bias restored: %.2f %.2f %.2f dps",
             static_cast<double>(stored_bias.x),
             static_cast<double>(stored_bias.y),
             static_cast<double>(stored_bias.z));
  }
  have_emit_ = false;
  temperature_countdown_ = 0;

  bool fifo_enabled = moto::device::load_imu_fifo_enabled();
  bool poll_mode = false;
  while (true) {
    if (fifo_request_ >= 0) {
      fifo_enabled = fifo_request_ == 1;
      fifo_request_ = -1;
      latch_failed_ = false;
      poll_mode = false;
      moto::device::save_imu_fifo_enabled(fifo_enabled);
    }
    bool restart = false;

    if (fifo_enabled) {
      if (fifo_.begin(&device_)) {
        fifo_reason_ = nullptr;
        announce_mode(AcquisitionMode::Fifo);
        fifo_loop(restart);
        fifo_.shutdown();
        const char* reason = fifo_.fallback_reason();
        if (reason != nullptr) {
          fifo_reason_ = reason;
          fifo_enabled = false;
          poll_mode = false;
          ESP_LOGW(kTag, "FIFO fell back: %s", reason);
        }
      } else {
        fifo_reason_ = fifo_.fallback_reason();
        fifo_enabled = false;
        poll_mode = false;
        ESP_LOGW(kTag, "FIFO unavailable: %s",
                 fifo_reason_ != nullptr ? fifo_reason_ : "init failed");
      }
      if (restart) continue;
    }

    if (!fifo_enabled && !poll_mode) {
      // Latch polling is the production chain. It is only skipped after it
      // failed once in this boot; a mode request clears the latch_failed_ flag.
      if (!latch_failed_ && latch_.begin(&device_)) {
        latch_reason_ = nullptr;
        announce_mode(AcquisitionMode::Latch);
        latch_loop(restart);
        const char* reason = latch_.fallback_reason();
        latch_.shutdown();
        if (!latch_.active()) {
          latch_failed_ = true;
          if (reason != nullptr) {
            latch_reason_ = reason;
            ESP_LOGW(kTag, "latch fell back: %s", reason);
          }
        }
      } else {
        latch_reason_ = latch_.fallback_reason() != nullptr
                            ? latch_.fallback_reason()
                            : "init failed";
        latch_failed_ = true;
        ESP_LOGW(kTag, "latch unavailable: %s", latch_reason_);
      }
      if (restart) continue;
      poll_mode = true;
      configure_poll_mode();
      announce_mode(AcquisitionMode::Poll);
    }

    if (poll_mode) {
      poll_loop(restart);
      if (restart) continue;
    }
  }
}

void MotionHeadingSensor::run_calibration() {
  calibration_requested_ = false;
  moto::motion::MountCalibration candidate = mount_;
  // Step 1 measures up only; forward is never measured on the bike (step 2 is
  // not wired), so it comes from the screen's geometry, not from the stored
  // basis. Inheriting it kept a wrong guess through every re-CAL.
  candidate.set_basis(kSensorForward, kSensorUp, false);
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
                               std::int64_t sample_us) {
  // The estimator integrates gyro rate; dt from the sample timestamps keeps
  // its scale right even when a flash page write or a skipped tick stretches
  // one interval. The 2 ms floor matches the old 125 Hz path.
  const float dt =
      have_emit_
          ? std::clamp(static_cast<float>(sample_us - last_emit_us_) / 1.0e6F,
                       0.002F, 0.05F)
          : 1.0F / 112.0F;
  const std::uint64_t sample_ms =
      static_cast<std::uint64_t>(sample_us) / 1'000U;
  last_emit_us_ = sample_us;
  have_emit_ = true;
  // DESIGN_V2 §3.7. Fixes first, so a fix that arrived since the last sample
  // corrects the trajectory before this sample extends it; then this sample's
  // longitudinal force, levelled by the Lean state of the sample before.
  apply_speed_fixes();
  const auto now_ms = static_cast<std::uint32_t>(sample_ms);
  speed_.step(lean_.mount().to_body(
                  {raw.accel_mps2[0], raw.accel_mps2[1], raw.accel_mps2[2]}),
              lean_.up_body(),
              lean_.static_for_s() >= moto::motion::SpeedFusion::kParkedForS,
              dt, now_ms);
  lean_.step({raw.gyro_dps[0], raw.gyro_dps[1], raw.gyro_dps[2]},
             {raw.accel_mps2[0], raw.accel_mps2[1], raw.accel_mps2[2]}, dt,
             speed_.sample(now_ms));
  const moto::motion::LeanOutput& out = lean_.output();
  persist_bias(sample_ms);
  last_lean_deg_ = out.lean_deg;
  last_pitch_deg_ = out.pitch_deg;
  last_quality_ = out.quality;
  last_flags_ = out.flags;
  last_raw_ = raw;
  if (gyro_int_reset_) {
    gyro_int_reset_ = false;
    for (int axis = 0; axis < 3; ++axis) {
      gyro_int_deg_[axis] = 0.0F;
      gyro_int_accel_[axis] = raw.accel_mps2[axis];
    }
  } else {
    const moto::motion::Vec3f bias = lean_.bias_sensor_dps();
    gyro_int_deg_[0] += (raw.gyro_dps[0] - bias.x) * dt;
    gyro_int_deg_[1] += (raw.gyro_dps[1] - bias.y) * dt;
    gyro_int_deg_[2] += (raw.gyro_dps[2] - bias.z) * dt;
  }

  moto::motion::AttitudeEstimate attitude;
  attitude.lean_deg = out.lean_deg;
  attitude.pitch_deg = out.pitch_deg;
  // The estimator's basis, which may hold a provisional up before the rider
  // calibrates; longitudinal G must use the same axes as Lean and pitch.
  const moto::motion::Vec3f accel_body = lean_.mount().to_body(
      {raw.accel_mps2[0], raw.accel_mps2[1], raw.accel_mps2[2]});
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
  if (acquisition_mode() == AcquisitionMode::Poll) {
    attitude.flags |= moto::motion::kLeanImuFallback;
  }
  attitude.valid = out.valid;
  callback_(-out.yaw_rate_dps, attitude, raw, sample_ms, callback_context_);
}

void MotionHeadingSensor::add_speed_fix(const moto::motion::SpeedFix& fix) {
  const std::lock_guard<std::mutex> lock(fix_mutex_);
  if (fix_count_ == fix_queue_.size()) {
    fixes_dropped_ = fixes_dropped_ + 1;
    return;
  }
  fix_queue_[fix_count_++] = fix;
}

void MotionHeadingSensor::set_fix_callback(FixCallback callback,
                                           void* context) {
  fix_callback_ = callback;
  fix_context_ = context;
}

void MotionHeadingSensor::apply_speed_fixes() {
  // Never wait on the BLE task here: a fix that misses this sample makes the
  // next one, 4.5 ms later.
  std::array<moto::motion::SpeedFix, 4> fixes{};
  std::size_t count = 0;
  {
    std::unique_lock<std::mutex> lock(fix_mutex_, std::try_to_lock);
    if (!lock.owns_lock() || fix_count_ == 0) return;
    count = fix_count_;
    for (std::size_t i = 0; i < count; ++i) fixes[i] = fix_queue_[i];
    fix_count_ = 0;
  }
  for (std::size_t i = 0; i < count; ++i) {
    const bool applied = speed_.add_fix(fixes[i]);
    if (fix_callback_ != nullptr) {
      fix_callback_(fixes[i], applied, fix_context_);
    }
  }
}

void MotionHeadingSensor::persist_bias(std::uint64_t now_ms) {
  // DESIGN_V2 §3.4 asks for the bias to be written at power-off, but the
  // bike's key can cut power without a shutdown. Write instead after 30 s of
  // continuous stillness, when it moved by more than 0.2 dps, at most once a
  // minute. Riding never writes.
  constexpr std::uint64_t kStillMs = 30'000;
  constexpr std::uint64_t kMinIntervalMs = 60'000;
  constexpr float kChangeDps = 0.2F;
  if (!lean_.bias_learning() || !lean_.bias_known()) {
    still_since_ms_ = 0;
    return;
  }
  if (still_since_ms_ == 0) still_since_ms_ = now_ms;
  if (now_ms - still_since_ms_ < kStillMs) return;
  if (bias_saved_ && now_ms - last_bias_save_ms_ < kMinIntervalMs) return;
  const moto::motion::Vec3f bias = lean_.bias_sensor_dps();
  const float dx = bias.x - saved_bias_dps_.x;
  const float dy = bias.y - saved_bias_dps_.y;
  const float dz = bias.z - saved_bias_dps_.z;
  if (bias_saved_ && std::sqrt(dx * dx + dy * dy + dz * dz) < kChangeDps) {
    return;
  }
  last_bias_save_ms_ = now_ms;
  if (moto::device::save_gyro_bias(bias)) {
    saved_bias_dps_ = bias;
    bias_saved_ = true;
    ESP_LOGI(kTag, "gyro bias saved: %.2f %.2f %.2f dps",
             static_cast<double>(bias.x), static_cast<double>(bias.y),
             static_cast<double>(bias.z));
  } else {
    ESP_LOGW(kTag, "gyro bias not persisted");
  }
}

void MotionHeadingSensor::fifo_loop(bool& restart) {
  constexpr std::uint32_t kOdrHz10 = 4484;  // 448.4 Hz in 0.1 Hz
  constexpr float kPeriodUs = 1.0e6F / 448.4F;
  struct PendingFrame {
    moto::motion::FifoFrame frame;
    std::uint64_t time_ms;
  };
  PendingFrame pending[64];
  std::size_t pending_count = 0;
  const std::uint64_t warmup_end = monotonic_ms() + kWarmupMs;
  TickType_t next = xTaskGetTickCount();
  while (true) {
    if (fifo_request_ >= 0 || !fifo_.active()) {
      restart = true;
      return;
    }
    if (calibration_requested_) run_calibration();
    moto::motion::FifoFrame frames[16];
    const std::size_t count = fifo_.poll(frames, 16);
    if (count > 0) {
      const std::uint64_t now = monotonic_ms();
      for (std::size_t i = 0; i < count && pending_count < 64; ++i) {
        const std::uint64_t age_us = static_cast<std::uint64_t>(
            (count - 1 - i) * kPeriodUs);
        pending[pending_count].frame = frames[i];
        pending[pending_count].time_ms = now - age_us / 1000U;
        ++pending_count;
      }
      // The watermark may be 2 samples (the 4-sample window was too slow on
      // this unit), so averaging groups of four spans poll iterations.
      while (pending_count >= 4) {
        RawImuSample raw;
        for (int axis = 0; axis < 3; ++axis) {
          raw.accel_mps2[axis] = 0.0F;
          raw.gyro_dps[axis] = 0.0F;
          for (int k = 0; k < 4; ++k) {
            raw.accel_mps2[axis] += pending[k].frame.accel_mps2[axis] * 0.25F;
            raw.gyro_dps[axis] += pending[k].frame.gyro_dps[axis] * 0.25F;
          }
        }
        const std::uint64_t sample_ms = pending[3].time_ms;
        if (--temperature_countdown_ <= 0) {
          temperature_countdown_ = 112;
          float measured = 0.0F;
          if (qmi8658_read_temp(&device_, &measured) == ESP_OK) {
            temperature_c_ = measured;
          }
        }
        raw.temp_c = temperature_c_;
        if (sample_ms > warmup_end) {
          emit(raw, static_cast<std::int64_t>(sample_ms) * 1'000);
        }
        for (std::size_t i = 4; i < pending_count; ++i) {
          pending[i - 4] = pending[i];
        }
        pending_count -= 4;
      }
    }
    (void)kOdrHz10;
    xTaskDelayUntil(&next, kFifoPollDelay);
  }
}

void MotionHeadingSensor::latch_timer_notify(void* context) {
  auto* sensor = static_cast<MotionHeadingSensor*>(context);
  // esp_timer callbacks run in the esp_timer task, not in an ISR.
  if (sensor->task_handle_ != nullptr) {
    xTaskNotifyGive(sensor->task_handle_);
  }
}

void MotionHeadingSensor::latch_loop(bool& restart) {
  task_handle_ = xTaskGetCurrentTaskHandle();
  esp_timer_create_args_t timer_args{};
  timer_args.callback = latch_timer_notify;
  timer_args.arg = this;
  timer_args.dispatch_method = ESP_TIMER_TASK;
  timer_args.name = "moto_imu_tick";
  esp_timer_handle_t timer = nullptr;
  if (esp_timer_create(&timer_args, &timer) != ESP_OK) {
    latch_reason_ = "esp_timer create failed";
    latch_.shutdown();
    return;
  }
  if (esp_timer_start_periodic(timer, moto::motion::Qmi8658Latch::kPeriodUs) !=
      ESP_OK) {
    esp_timer_delete(timer);
    latch_reason_ = "esp_timer start failed";
    latch_.shutdown();
    return;
  }

  moto::motion::LatchSample pair[2];
  std::size_t pair_count = 0;
  const std::int64_t warmup_end_us =
      esp_timer_get_time() + static_cast<std::int64_t>(kWarmupMs) * 1'000;
  bool logged_status = false;
  while (true) {
    // The tick is the whole clock. A 50 ms timeout only exists so a mode
    // request or a stopped timer is still noticed.
    (void)ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(50));
    if (fifo_request_ >= 0 || !latch_.active()) {
      restart = fifo_request_ >= 0;
      break;
    }
    if (calibration_requested_) {
      // Calibration needs the driver's blocking read path. Its sample gaps are
      // outside the acquisition acceptance window; the bench card says to
      // reset the counters after a successful CAL.
      run_calibration();
      continue;
    }
    if (!logged_status && esp_timer_get_time() >= warmup_end_us) {
      latch_.log_status("after warmup");
      logged_status = true;
    }
    moto::motion::LatchSample sample;
    if (!latch_.read(&sample)) continue;
    pair[pair_count++] = sample;
    if (pair_count < 2) continue;
    pair_count = 0;

    RawImuSample raw;
    for (int axis = 0; axis < 3; ++axis) {
      raw.accel_mps2[axis] =
          (pair[0].accel_mps2[axis] + pair[1].accel_mps2[axis]) * 0.5F;
      raw.gyro_dps[axis] =
          (pair[0].gyro_dps[axis] + pair[1].gyro_dps[axis]) * 0.5F;
    }
    raw.temp_c = (pair[0].temp_c + pair[1].temp_c) * 0.5F;
    // The pair describes the centre of its two read instants, not the later
    // one; this keeps the 112 Hz timestamps evenly spaced.
    const std::int64_t sample_us =
        (pair[0].timestamp_us + pair[1].timestamp_us) / 2;
    if (sample_us >= warmup_end_us) emit(raw, sample_us);
  }

  esp_timer_stop(timer);
  esp_timer_delete(timer);
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
      emit(raw, static_cast<std::int64_t>(monotonic_ms()) * 1'000);
    } else if (result != ESP_OK) {
      ESP_LOGW(kTag, "QMI8658 read error: %s", esp_err_to_name(result));
    }
    xTaskDelayUntil(&next_sample_tick, kPollDelay);
  }
}
