#pragma once

#include <cstdint>

#include "attitude_estimator.hpp"
#include "esp_err.h"
#include "lean_estimator.hpp"
#include "qmi8658_fifo.hpp"

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
  using ModeCallback = void (*)(bool fifo_active, void* context);

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

  // Runtime FIFO toggle (NVS imu.fifo). Applied by restarting the sample loop.
  void request_fifo_mode(bool enabled);
  bool fifo_active() const { return fifo_active_; }
  const moto::motion::FifoStats& fifo_stats() const { return fifo_stats_; }
  const char* fifo_fallback_reason() const { return fifo_reason_; }
  // Last estimates, for the `imu lean` bench command. Written by the sensor
  // task, read by the command task; single floats, no locking.
  float last_lean_deg() const { return last_lean_deg_; }
  float last_pitch_deg() const { return last_pitch_deg_; }
  std::uint8_t last_quality() const { return last_quality_; }
  std::uint16_t last_flags() const { return last_flags_; }
  void set_mode_callback(ModeCallback callback, void* context);

  esp_err_t start(SampleCallback callback, void* context);

 private:
  static void task_entry(void* context);
  void run();
  void poll_loop(bool& restart);
  void fifo_loop(moto::motion::Qmi8658Fifo& fifo, bool& restart);
  void run_calibration();
  void emit(const RawImuSample& raw, std::uint64_t sample_ms);

  qmi8658_dev_t device_{};
  moto::motion::LeanEstimator lean_{};
  std::uint64_t last_emit_ms_ = 0;
  bool have_emit_ = false;
  float temperature_c_ = 0.0F;
  int temperature_countdown_ = 0;

  SampleCallback callback_ = nullptr;
  void* callback_context_ = nullptr;
  bool started_ = false;
  bool has_mount_ = false;
  moto::motion::MountCalibration mount_{};
  volatile bool calibration_requested_ = false;
  CalibrationCallback calibration_callback_ = nullptr;
  void* calibration_context_ = nullptr;
  volatile int fifo_request_ = -1;  // -1 none, 0 off, 1 on
  volatile bool fifo_active_ = false;
  volatile float last_lean_deg_ = 0.0F;
  volatile float last_pitch_deg_ = 0.0F;
  volatile uint8_t last_quality_ = 0;
  volatile uint16_t last_flags_ = 0;
  const char* fifo_reason_ = nullptr;
  moto::motion::FifoStats fifo_stats_{};
  ModeCallback mode_callback_ = nullptr;
  void* mode_context_ = nullptr;
};
