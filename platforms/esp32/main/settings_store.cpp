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
  if (!ensure_nvs()) return true;
  nvs_handle_t handle = 0;
  if (nvs_open(kNamespace, NVS_READONLY, &handle) != ESP_OK) return true;
  std::uint8_t value = 1;
  const esp_err_t result = nvs_get_u8(handle, kImuFifoKey, &value);
  nvs_close(handle);
  return result != ESP_OK ? true : value != 0;
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

}  // namespace moto::device
