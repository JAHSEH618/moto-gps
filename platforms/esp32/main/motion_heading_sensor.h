#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <mutex>

#include "attitude_estimator.hpp"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "lean_estimator.hpp"
#include "qmi8658_fifo.hpp"
#include "qmi8658_latch.hpp"
#include "speed_fusion.hpp"

// The raw sample (sensor frame) travels with the estimate so the development
// recorder can store what the estimator actually saw, not just its output.
struct RawImuSample {
  float accel_mps2[3] = {0.0F, 0.0F, 0.0F};
  float gyro_dps[3] = {0.0F, 0.0F, 0.0F};
  float temp_c = 0.0F;
};

class MotionHeadingSensor {
 public:
  using SampleCallback = void (*)(
      float heading_rate_dps,
      const moto::motion::AttitudeEstimate& attitude,
      const RawImuSample& raw,
      std::uint64_t sample_ms,
      void* context);
  // 0 poll, 1 FIFO, 2 latch. The tlog session record needs the same number.
  enum class AcquisitionMode : std::uint8_t {
    Poll = 0,
    Fifo = 1,
    Latch = 2,
  };
  using ModeCallback = void (*)(std::uint8_t mode, void* context);

  MotionHeadingSensor() = default;
  MotionHeadingSensor(const MotionHeadingSensor&) = delete;
  MotionHeadingSensor& operator=(const MotionHeadingSensor&) = delete;

  // Restores the persisted R_mount before the task starts. Without one the
  // adapter falls back to its assumed bracket axes and reports CAL_OK = false.
  void set_mount(const moto::motion::MountCalibration& mount);
  moto::motion::MountCalibration mount() const;

  // v2 static calibration. The capture runs in the sensor task (it needs the
  // QMI8658 samples) and reports through the completion callback, which the
  // caller uses to persist att.rmount.
  // reason: 0 not applicable, 1 motion/gyro, 2 magnitude range.
  using CalibrationCallback = void (*)(
      const moto::motion::MountCalibration& mount, bool ok, std::uint8_t reason,
      void* context);
  void request_static_calibration();
  void set_calibration_callback(CalibrationCallback callback, void* context);

  // Runtime acquisition switch (NVS imu.fifo). Off selects the default latch
  // chain; on selects the FIFO A/B chain. Applied by restarting the loop.
  void request_fifo_mode(bool enabled);
  bool fifo_active() const {
    return mode_ == static_cast<std::uint8_t>(AcquisitionMode::Fifo);
  }
  bool latch_active() const {
    return mode_ == static_cast<std::uint8_t>(AcquisitionMode::Latch);
  }
  AcquisitionMode acquisition_mode() const {
    return static_cast<AcquisitionMode>(mode_);
  }
  const char* mode_name() const;
  // Set for the life of the sensor task; used by the `sys tasks` bench command.
  TaskHandle_t task_handle() const { return task_handle_; }
  const moto::motion::FifoStats& fifo_stats() const { return fifo_.stats(); }
  const moto::motion::LatchStats& latch_stats() const { return latch_.stats(); }
  // Reason the active chain fell back, or nullptr when it did not.
  const char* fallback_reason() const;
  void reset_imu_stats();
  // Tells the latch reader that the interval just after this moment contains a
  // synchronous flash page write, so an over-budget gap is attributed to flash.
  void note_flash_blocking() { latch_.note_flash_blocking(); }
  // Last estimates, for the `imu lean` bench command. Written by the sensor
  // task, read by the command task; single floats, no locking.
  float last_lean_deg() const { return last_lean_deg_; }
  float last_pitch_deg() const { return last_pitch_deg_; }
  std::uint8_t last_quality() const { return last_quality_; }
  std::uint16_t last_flags() const { return last_flags_; }
  // `imu raw`: the last sensor-frame sample and the estimator's gate state.
  // A torn read only garbles one bench line.
  const RawImuSample& last_raw() const { return last_raw_; }
  const moto::motion::LeanEstimator& lean() const { return lean_; }
  // `imu int`: bias-corrected gyro integrated per sensor axis since the last
  // reset, and the accelerometer at that reset. Rotating the board between two
  // still poses gives each axis's gain against the gravity change.
  void request_gyro_integral_reset() { gyro_int_reset_ = true; }
  const float* gyro_integral_deg() const { return gyro_int_deg_; }
  const float* gyro_integral_start_accel() const { return gyro_int_accel_; }
  void set_mode_callback(ModeCallback callback, void* context);

  esp_err_t start(SampleCallback callback, void* context);
  // One phone GNSS speed (any task), placed on this device's clock. It is
  // queued and applied to SpeedFusion before the next IMU sample; the fused
  // speed feeds the Lean estimator's kinematic correction with the fix's age.
  void add_speed_fix(const moto::motion::SpeedFix& fix);
  // Called on the sensor task for every queued fix, applied or not, before the
  // sample it was applied to. The recorder writes it from here, so its GNSS
  // records sit in the same order as the samples the fusion saw.
  using FixCallback = void (*)(const moto::motion::SpeedFix& fix, bool applied,
                               void* context);
  void set_fix_callback(FixCallback callback, void* context);
  // `imu speed`: written by the sensor task, read by the command task.
  const moto::motion::SpeedFusion& speed() const { return speed_; }
  std::uint32_t dropped_speed_fixes() const { return fixes_dropped_; }

 private:
  static void task_entry(void* context);
  static void latch_timer_notify(void* context);
  void run();
  void poll_loop(bool& restart);
  void fifo_loop(bool& restart);
  void latch_loop(bool& restart);
  void configure_poll_mode();
  void announce_mode(AcquisitionMode mode);
  void run_calibration();
  void emit(const RawImuSample& raw, std::int64_t sample_us);
  void persist_bias(std::uint64_t now_ms);
  void apply_speed_fixes();

  qmi8658_dev_t device_{};
  moto::motion::LeanEstimator lean_{};
  std::int64_t last_emit_us_ = 0;
  bool have_emit_ = false;
  float temperature_c_ = 0.0F;
  int temperature_countdown_ = 0;
  // Phone GNSS fixes, written by the BLE task and drained by this one. About
  // one a second arrives against a sample every 4.5 ms, so four slots only
  // fill when the sensor task is not running.
  std::mutex fix_mutex_;
  std::array<moto::motion::SpeedFix, 4> fix_queue_{};
  std::size_t fix_count_ = 0;
  volatile std::uint32_t fixes_dropped_ = 0;
  moto::motion::SpeedFusion speed_{};
  FixCallback fix_callback_ = nullptr;
  void* fix_context_ = nullptr;
  // Bias persistence: written only from this task, only while parked.
  moto::motion::Vec3f saved_bias_dps_{};
  bool bias_saved_ = false;
  std::uint64_t still_since_ms_ = 0;
  std::uint64_t last_bias_save_ms_ = 0;

  SampleCallback callback_ = nullptr;
  void* callback_context_ = nullptr;
  bool started_ = false;
  bool has_mount_ = false;
  moto::motion::MountCalibration mount_{};
  volatile bool calibration_requested_ = false;
  CalibrationCallback calibration_callback_ = nullptr;
  void* calibration_context_ = nullptr;
  volatile int fifo_request_ = -1;  // -1 none, 0 off, 1 on
  volatile std::uint8_t mode_ = 0;  // AcquisitionMode
  volatile bool latch_failed_ = false;
  volatile bool latch_active_ = false;
  volatile float last_lean_deg_ = 0.0F;
  volatile float last_pitch_deg_ = 0.0F;
  volatile uint8_t last_quality_ = 0;
  volatile uint16_t last_flags_ = 0;
  RawImuSample last_raw_{};
  volatile bool gyro_int_reset_ = false;
  float gyro_int_deg_[3] = {0.0F, 0.0F, 0.0F};
  float gyro_int_accel_[3] = {0.0F, 0.0F, 0.0F};
  const char* fifo_reason_ = nullptr;
  const char* latch_reason_ = nullptr;
  moto::motion::Qmi8658Fifo fifo_{};
  moto::motion::Qmi8658Latch latch_{};
  TaskHandle_t task_handle_ = nullptr;
  ModeCallback mode_callback_ = nullptr;
  void* mode_context_ = nullptr;
};
