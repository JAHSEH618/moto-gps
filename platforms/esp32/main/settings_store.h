#pragma once

#include "device_settings.hpp"
#include "mount_basis.hpp"

namespace moto::device {

// The NVS blob is the device's own copy of the settings it is currently
// running. It is written after a wire change was applied, and read once at
// boot so a reboot does not flash the vendor theme before the phone reconnects.
//
// The wire is still the source of truth: a phone whose revision is higher than
// anything this device has applied overwrites the blob. Storage exists to
// survive a power cycle, not to arbitrate between endpoints.

// Loads the blob. A missing, short, or unknown-version blob yields defaults
// (decode() is the single owner of that rule), so this never fails.
DeviceSettings load_settings();

// Writes the clamped blob. Returns false when NVS refuses, which the caller
// logs rather than hides: a setting that silently does not survive a reboot is
// worse than a visible error.
bool save_settings(const DeviceSettings& settings);

// v2 `att.rmount`. Stored separately from the settings blob so a phone that
// rewrites SettingsState never rotates the calibration, and so the blob format
// does not need another version for a value only the device can produce.
StoredMount load_mount_basis();
bool save_mount_basis(const StoredMount& mount);

// One-time above-16-MB self-test marker. The tlog append point is found from
// the partition itself (self-describing blocks), not from NVS.
bool tlog_selftest_done();
void mark_tlog_selftest_done();

// `tlog clear` is a deferred erase: the serial command writes this flag and
// reboots, and app_main erases the partition before the display, BLE and IMU
// tasks start. Erasing 14 MB with the cache off while they run would stall
// every one of them.
void request_tlog_clear();
bool tlog_clear_requested();
void clear_tlog_clear_request();

// Runtime FIFO toggle (NVS `imu.fifo`), default off: latch polling is the
// production chain. The bench A/B needs to switch acquisition without
// reflashing.
bool load_imu_fifo_enabled();
bool save_imu_fifo_enabled(bool enabled);

// Gyro bias in the sensor's own axes, deg/s (DESIGN_V2 §3.4: learned at
// rest, kept across power cycles). Returns false when nothing valid is stored.
bool load_gyro_bias(moto::motion::Vec3f& bias_dps);
bool save_gyro_bias(const moto::motion::Vec3f& bias_dps);

}  // namespace moto::device
