#include "settings_store.h"

#include <cstddef>

#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

namespace moto::device {
namespace {

constexpr char kTag[] = "settings_store";
constexpr char kNamespace[] = "moto_device";
constexpr char kKey[] = "settings";
constexpr char kMountKey[] = "att.rmount";
constexpr char kTlogPosKey[] = "tlog.pos";
constexpr char kTlogOkKey[] = "tlog.ok";
constexpr char kImuFifoKey[] = "imu.fifo";
constexpr char kGyroBiasKey[] = "att.gbias";
constexpr char kTlogClearKey[] = "tlog.clear";
constexpr char kPerfBestKey[] = "perf.best";
constexpr std::uint8_t kPerfBestVersion = 1;
struct PerfBestBlob {
  std::uint8_t version;
  std::uint8_t reserved[3];
  std::uint32_t best_ms[3];
};
// Version byte first, so a later layout is rejected instead of misread.
constexpr std::uint8_t kGyroBiasVersion = 1;
struct GyroBiasBlob {
  std::uint8_t version;
  std::uint8_t reserved[3];
  float x;
  float y;
  float z;
};

// app_main loads settings before the BLE transport starts, and the transport
// is the other caller of nvs_flash_init(). ESP-IDF returns ESP_OK for a second
// init, so whichever runs first owns the setup; the erase branch mirrors the
// transport's because a first boot after a repartition is exactly when NVS has
// no free pages.
bool ensure_nvs() {
  esp_err_t result = nvs_flash_init();
  if(result == ESP_ERR_NVS_NO_FREE_PAGES ||
     result == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_LOGW(kTag, "NVS layout changed; rebuilding this firmware's NVS");
    result = nvs_flash_erase();
    if(result == ESP_OK) {
      result = nvs_flash_init();
    }
  }
  if(result != ESP_OK) {
    ESP_LOGE(kTag, "NVS initialization failed: %s", esp_err_to_name(result));
    return false;
  }
  return true;
}

}  // namespace

DeviceSettings load_settings() {
  if(!ensure_nvs()) {
    return DeviceSettings{};
  }
  nvs_handle_t handle = 0;
  if(nvs_open(kNamespace, NVS_READONLY, &handle) != ESP_OK) {
    // A fresh install has no namespace yet. Defaults are the correct answer.
    return DeviceSettings{};
  }
  std::uint8_t blob[kEncodedSettingsSize]{};
  std::size_t size = sizeof(blob);
  const esp_err_t result = nvs_get_blob(handle, kKey, blob, &size);
  nvs_close(handle);
  if(result != ESP_OK) {
    ESP_LOGI(kTag, "no stored settings (%s); using defaults",
             esp_err_to_name(result));
    return DeviceSettings{};
  }
  const DeviceSettings settings = DeviceSettings::decode(blob, size);
  ESP_LOGI(kTag, "loaded settings v%u theme=%u buildings=%u",
           static_cast<unsigned>(blob[0]),
           static_cast<unsigned>(settings.theme),
           settings.show_buildings ? 1U : 0U);
  return settings;
}

bool save_settings(const DeviceSettings& settings) {
  if(!ensure_nvs()) {
    return false;
  }
  nvs_handle_t handle = 0;
  esp_err_t result = nvs_open(kNamespace, NVS_READWRITE, &handle);
  if(result != ESP_OK) {
    ESP_LOGE(kTag, "nvs_open failed: %s", esp_err_to_name(result));
    return false;
  }
  const auto blob = settings.encoded();
  result = nvs_set_blob(handle, kKey, blob.data(), blob.size());
  if(result == ESP_OK) {
    result = nvs_commit(handle);
  }
  nvs_close(handle);
  if(result != ESP_OK) {
    ESP_LOGE(kTag, "settings write failed: %s", esp_err_to_name(result));
    return false;
  }
  return true;
}

bool load_imu_fifo_enabled() {
  // Latch polling is the production acquisition chain (DESIGN_V2 r2 §3.2).
  // FIFO stays in the firmware for A/B comparison only and is off by default,
  // including on a fresh NVS.
  if (!ensure_nvs()) return false;
  nvs_handle_t handle = 0;
  if (nvs_open(kNamespace, NVS_READONLY, &handle) != ESP_OK) return false;
  std::uint8_t value = 0;
  const esp_err_t result = nvs_get_u8(handle, kImuFifoKey, &value);
  nvs_close(handle);
  return result != ESP_OK ? false : value != 0;
}

bool save_imu_fifo_enabled(bool enabled) {
  if (!ensure_nvs()) return false;
  nvs_handle_t handle = 0;
  if (nvs_open(kNamespace, NVS_READWRITE, &handle) != ESP_OK) return false;
  esp_err_t result = nvs_set_u8(handle, kImuFifoKey, enabled ? 1 : 0);
  if (result == ESP_OK) result = nvs_commit(handle);
  nvs_close(handle);
  return result == ESP_OK;
}

bool load_gyro_bias(moto::motion::Vec3f& bias_dps) {
  if (!ensure_nvs()) return false;
  nvs_handle_t handle = 0;
  if (nvs_open(kNamespace, NVS_READONLY, &handle) != ESP_OK) return false;
  GyroBiasBlob blob{};
  std::size_t size = sizeof(blob);
  const esp_err_t result = nvs_get_blob(handle, kGyroBiasKey, &blob, &size);
  nvs_close(handle);
  if (result != ESP_OK || size != sizeof(blob) ||
      blob.version != kGyroBiasVersion) {
    return false;
  }
  bias_dps = {blob.x, blob.y, blob.z};
  return true;
}

bool save_gyro_bias(const moto::motion::Vec3f& bias_dps) {
  if (!ensure_nvs()) return false;
  nvs_handle_t handle = 0;
  if (nvs_open(kNamespace, NVS_READWRITE, &handle) != ESP_OK) return false;
  const GyroBiasBlob blob{kGyroBiasVersion, {}, bias_dps.x, bias_dps.y,
                          bias_dps.z};
  esp_err_t result = nvs_set_blob(handle, kGyroBiasKey, &blob, sizeof(blob));
  if (result == ESP_OK) result = nvs_commit(handle);
  nvs_close(handle);
  return result == ESP_OK;
}

bool load_performance_bests(std::array<std::uint32_t, 3>& best_ms) {
  if (!ensure_nvs()) return false;
  nvs_handle_t handle = 0;
  if (nvs_open(kNamespace, NVS_READONLY, &handle) != ESP_OK) return false;
  PerfBestBlob blob{};
  std::size_t size = sizeof(blob);
  const esp_err_t result = nvs_get_blob(handle, kPerfBestKey, &blob, &size);
  nvs_close(handle);
  if (result != ESP_OK || size != sizeof(blob) ||
      blob.version != kPerfBestVersion) {
    return false;
  }
  for (std::size_t i = 0; i < best_ms.size(); ++i) best_ms[i] = blob.best_ms[i];
  return true;
}

bool save_performance_bests(const std::array<std::uint32_t, 3>& best_ms) {
  if (!ensure_nvs()) return false;
  nvs_handle_t handle = 0;
  if (nvs_open(kNamespace, NVS_READWRITE, &handle) != ESP_OK) return false;
  PerfBestBlob blob{kPerfBestVersion, {}, {best_ms[0], best_ms[1], best_ms[2]}};
  esp_err_t result = nvs_set_blob(handle, kPerfBestKey, &blob, sizeof(blob));
  if (result == ESP_OK) result = nvs_commit(handle);
  nvs_close(handle);
  return result == ESP_OK;
}

bool tlog_selftest_done() {
  if (!ensure_nvs()) return false;
  nvs_handle_t handle = 0;
  if (nvs_open(kNamespace, NVS_READONLY, &handle) != ESP_OK) return false;
  std::uint8_t value = 0;
  const esp_err_t result = nvs_get_u8(handle, kTlogOkKey, &value);
  nvs_close(handle);
  return result == ESP_OK && value != 0;
}

void mark_tlog_selftest_done() {
  if (!ensure_nvs()) return;
  nvs_handle_t handle = 0;
  if (nvs_open(kNamespace, NVS_READWRITE, &handle) != ESP_OK) return;
  if (nvs_set_u8(handle, kTlogOkKey, 1) == ESP_OK) nvs_commit(handle);
  nvs_close(handle);
}

}  // namespace moto::device

namespace moto::device {

StoredMount load_mount_basis() {
  if (!ensure_nvs()) return StoredMount{};
  nvs_handle_t handle = 0;
  if (nvs_open(kNamespace, NVS_READONLY, &handle) != ESP_OK) {
    return StoredMount{};
  }
  std::uint8_t blob[kEncodedMountSize]{};
  std::size_t size = sizeof(blob);
  const esp_err_t result = nvs_get_blob(handle, kMountKey, blob, &size);
  nvs_close(handle);
  if (result != ESP_OK) {
    ESP_LOGI(kTag, "no stored mount basis (%s)", esp_err_to_name(result));
    return StoredMount{};
  }
  const StoredMount mount = decode_mount(blob, size);
  ESP_LOGI(kTag, "mount basis %s (trim %d cdeg)",
           mount.valid ? "loaded" : "rejected",
           static_cast<int>(mount.trim_cdeg));
  return mount;
}

bool save_mount_basis(const StoredMount& mount) {
  if (!ensure_nvs()) return false;
  nvs_handle_t handle = 0;
  esp_err_t result = nvs_open(kNamespace, NVS_READWRITE, &handle);
  if (result != ESP_OK) {
    ESP_LOGE(kTag, "nvs_open failed: %s", esp_err_to_name(result));
    return false;
  }
  const auto blob = encode_mount(mount);
  result = nvs_set_blob(handle, kMountKey, blob.data(), blob.size());
  if (result == ESP_OK) {
    result = nvs_commit(handle);
  }
  nvs_close(handle);
  if (result != ESP_OK) {
    ESP_LOGE(kTag, "mount basis write failed: %s", esp_err_to_name(result));
    return false;
  }
  return true;
}


void request_tlog_clear() {
  if (!ensure_nvs()) return;
  nvs_handle_t handle = 0;
  if (nvs_open(kNamespace, NVS_READWRITE, &handle) != ESP_OK) return;
  esp_err_t result = nvs_set_u8(handle, kTlogClearKey, 1);
  if (result == ESP_OK) result = nvs_commit(handle);
  nvs_close(handle);
  if (result != ESP_OK) {
    ESP_LOGE(kTag, "tlog clear request failed: %s", esp_err_to_name(result));
  }
}

bool tlog_clear_requested() {
  if (!ensure_nvs()) return false;
  nvs_handle_t handle = 0;
  if (nvs_open(kNamespace, NVS_READONLY, &handle) != ESP_OK) return false;
  std::uint8_t value = 0;
  const esp_err_t result = nvs_get_u8(handle, kTlogClearKey, &value);
  nvs_close(handle);
  return result == ESP_OK && value != 0;
}

void clear_tlog_clear_request() {
  if (!ensure_nvs()) return;
  nvs_handle_t handle = 0;
  if (nvs_open(kNamespace, NVS_READWRITE, &handle) != ESP_OK) return;
  esp_err_t result = nvs_erase_key(handle, kTlogClearKey);
  if (result == ESP_ERR_NVS_NOT_FOUND) result = ESP_OK;
  if (result == ESP_OK) result = nvs_commit(handle);
  nvs_close(handle);
  if (result != ESP_OK) {
    ESP_LOGW(kTag, "tlog clear flag could not be removed: %s",
             esp_err_to_name(result));
  }
}

}  // namespace moto::device
