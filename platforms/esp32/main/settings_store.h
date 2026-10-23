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

}  // namespace moto::device
