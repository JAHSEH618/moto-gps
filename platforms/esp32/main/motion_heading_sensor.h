#pragma once

#include <cstdint>

#include "attitude_estimator.hpp"
#include "lean_estimator.hpp"
#include "esp_err.h"

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

  MotionHeadingSensor() = default;
  MotionHeadingSensor(const MotionHeadingSensor&) = delete;
  MotionHeadingSensor& operator=(const MotionHeadingSensor&) = delete;

  // Starts one low-priority 125 Hz sensor task. Failure is non-fatal: phone
  // course navigation continues to work without the local latency filler.
  // Restores the persisted R_mount before the task starts. Without one the
  // adapter falls back to its assumed bracket axes and reports CAL_OK = false.
  void set_mount(const moto::motion::MountCalibration& mount);

  // v2 static calibration. The capture runs in the sensor task (it needs the
  // QMI8658 samples) and reports through the completion callback, which the
  // caller uses to persist att.rmount.
  using CalibrationCallback = void (*)(
      const moto::motion::MountCalibration& mount, bool ok, void* context);
  void request_static_calibration();
  void set_calibration_callback(CalibrationCallback callback, void* context);

  esp_err_t start(SampleCallback callback, void* context);

 private:
  static void task_entry(void* context);
  void run();

  SampleCallback callback_ = nullptr;
  void* callback_context_ = nullptr;
  bool started_ = false;
  bool has_mount_ = false;
  moto::motion::MountCalibration mount_{};
  volatile bool calibration_requested_ = false;
  CalibrationCallback calibration_callback_ = nullptr;
  void* calibration_context_ = nullptr;
};
