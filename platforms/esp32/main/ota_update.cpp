#include "ota_update.hpp"

#include <algorithm>
#include <cstdlib>
#include <cstring>

#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_timer.h"
#include "flash_guard.hpp"
#include "mbedtls/sha256.h"

namespace moto::device {
namespace {

constexpr char kTag[] = "ota";
// A hotspot join takes a few seconds from a cold radio; 25 s covers one retry
// cycle of the WiFi worker without leaving the rider at 0% forever.
constexpr std::uint32_t kNetworkWaitMs = 25'000;
constexpr std::uint32_t kNetworkPollMs = 250;
// The body is read in 4 KiB pieces: one flash sector per write, and small
// enough that a 10 KiB task stack holds the TLS read path.
constexpr std::size_t kReadChunkBytes = 4U * 1024U;
// Two percent steps or half a second, whichever comes first: the phone's bar
// moves smoothly and the BLE link keeps its 5 Hz snapshot slot.
constexpr std::uint8_t kProgressStep = 2;
constexpr std::uint32_t kProgressIntervalMs = 500;
// Time between "the image is verified" and the reboot, so the last OtaStatus
// reaches the phone.
constexpr std::uint32_t kRebootDelayMs = 1'200;

std::uint32_t now_ms() {
  return static_cast<std::uint32_t>(esp_timer_get_time() / 1'000LL);
}

}  // namespace

void OtaUpdate::set_sender(SendCallback callback, void* context) noexcept {
  sender_ = callback;
  sender_context_ = context;
}

void OtaUpdate::set_secure_link_check(SecureLinkCheck check,
                                      void* context) noexcept {
  secure_link_check_ = check;
  secure_link_context_ = context;
}

void OtaUpdate::set_network(const NetworkPort& port) noexcept {
  network_ = port;
}

esp_err_t OtaUpdate::start() {
  if (request_mutex_ != nullptr) return ESP_OK;
  request_mutex_ = xSemaphoreCreateMutex();
  if (request_mutex_ == nullptr) return ESP_ERR_NO_MEM;
  queue_ = xQueueCreate(4, sizeof(std::uint8_t));
  if (queue_ == nullptr) return ESP_ERR_NO_MEM;
  const esp_partition_t* next = esp_ota_get_next_update_partition(nullptr);
  const esp_partition_t* running = esp_ota_get_running_partition();
  esp_ota_img_states_t running_state = ESP_OTA_IMG_UNDEFINED;
  if (running != nullptr) {
    esp_ota_get_state_partition(running, &running_state);
  }
  ESP_LOGI(kTag, "ready; running %s (state %d), target %s at 0x%lx, %u KiB",
           running != nullptr ? running->label : "(none)",
           static_cast<int>(running_state),
           next != nullptr ? next->label : "(none)",
           next != nullptr ? static_cast<unsigned long>(next->address) : 0UL,
           static_cast<unsigned>(moto::ble::kMaxOtaImageBytes / 1024U));
  return ESP_OK;
}

// The worker is created on the first update, not at boot: its 10 KiB stack and
// the TLS heap it later takes are worth nothing to a rider who never updates,
// and internal RAM is the pool this board is short of.
bool OtaUpdate::ensure_task() {
  if (task_ != nullptr) return true;
  // Pinned to core 0: the LVGL tree and the IMU sampler own core 1, and a TLS
  // handshake must not steal their slices.
  if (xTaskCreatePinnedToCore(task_entry, "ota", 10U * 1024U, this, 3, &task_,
                              0) != pdPASS) {
    task_ = nullptr;
    return false;
  }
  return true;
}

moto::ble::AckStatus OtaUpdate::on_command(
    const moto::ble::OtaCommand& command) {
  if (secure_link_check_ == nullptr ||
      !secure_link_check_(secure_link_context_)) {
    ESP_LOGW(kTag, "OtaCommand refused: link is not encrypted and bonded");
    return moto::ble::AckStatus::InvalidState;
  }
  if (request_mutex_ == nullptr || queue_ == nullptr) {
    return moto::ble::AckStatus::InvalidState;
  }
  if (running_.load()) {
    ESP_LOGW(kTag, "OtaCommand refused: an update is already running");
    return moto::ble::AckStatus::InvalidState;
  }
  // The slot size is a device fact, not a sender preference.
  if (command.size_bytes > moto::ble::kMaxOtaImageBytes) {
    ESP_LOGW(kTag, "OtaCommand refused: %u bytes does not fit a slot",
             static_cast<unsigned>(command.size_bytes));
    return moto::ble::AckStatus::InvalidState;
  }
  xSemaphoreTake(request_mutex_, portMAX_DELAY);
  std::memset(&pending_, 0, sizeof(pending_));
  std::memcpy(pending_.url, command.url.data(), command.url.size());
  pending_.url[command.url.size()] = '\0';
  pending_.sha256 = command.sha256;
  pending_.size_bytes = command.size_bytes;
  has_pending_ = true;
  xSemaphoreGive(request_mutex_);
  ESP_LOGI(kTag, "accepted a %u-byte image from %s",
           static_cast<unsigned>(command.size_bytes), command.url.c_str());
  const std::uint8_t event = 1;
  if (!ensure_task()) {
    ESP_LOGE(kTag, "OTA worker could not start");
    return moto::ble::AckStatus::Failed;
  }
  xQueueSend(queue_, &event, 0);
  return moto::ble::AckStatus::Ok;
}

void OtaUpdate::on_phone_status(
    const moto::ble::ConnectionStatus& status) {
  phone_wants_status_.store(
      (status.capabilities & moto::ble::CapabilityOta) != 0U);
  // A phone that reached Ready over the new image is the proof the bootloader
  // was waiting for.
  if (status.state == moto::ble::ConnectionState::Ready) {
    confirm_running_image();
  }
}

void OtaUpdate::confirm_running_image() {
  const esp_partition_t* running = esp_ota_get_running_partition();
  if (running == nullptr) return;
  esp_ota_img_states_t state;
  if (esp_ota_get_state_partition(running, &state) != ESP_OK) return;
  if (state != ESP_OTA_IMG_PENDING_VERIFY) return;
  const esp_err_t result = esp_ota_mark_app_valid_cancel_rollback();
  if (result == ESP_OK) {
    ESP_LOGI(kTag, "image confirmed: rollback cancelled after the handshake");
  } else {
    ESP_LOGE(kTag, "could not confirm the image: %s",
             esp_err_to_name(result));
  }
}

bool OtaUpdate::running() const noexcept { return running_.load(); }

void OtaUpdate::log_status() const {
  ESP_LOGI(kTag, "state=%u failure=%u progress=%u%% bytes=%u",
           static_cast<unsigned>(status_.state),
           static_cast<unsigned>(status_.failure),
           static_cast<unsigned>(status_.progress_percent),
           static_cast<unsigned>(status_.bytes_received));
}

void OtaUpdate::task_entry(void* context) {
  static_cast<OtaUpdate*>(context)->run();
}

void OtaUpdate::run() {
  while (true) {
    std::uint8_t event = 0;
    if (xQueueReceive(queue_, &event, portMAX_DELAY) != pdTRUE) continue;
    if (event != 1) continue;
    Request request;
    xSemaphoreTake(request_mutex_, portMAX_DELAY);
    if (!has_pending_) {
      xSemaphoreGive(request_mutex_);
      continue;
    }
    request = pending_;
    has_pending_ = false;
    std::memset(&pending_, 0, sizeof(pending_));
    xSemaphoreGive(request_mutex_);
    fetch(request);
  }
}

bool OtaUpdate::wait_for_network() {
  if (network_.request != nullptr) network_.request(network_.context);
  const std::uint32_t deadline = now_ms() + kNetworkWaitMs;
  while (now_ms() < deadline) {
    if (network_.connected != nullptr && network_.connected(network_.context)) {
      ESP_LOGI(kTag, "hotspot up; fetching");
      return true;
    }
    vTaskDelay(pdMS_TO_TICKS(kNetworkPollMs));
  }
  return false;
}

std::uint8_t OtaUpdate::percent_of(std::uint32_t bytes,
                                   std::uint64_t total) noexcept {
  if (total == 0) return 0;
  if (bytes >= total) return 100;
  return static_cast<std::uint8_t>((static_cast<std::uint64_t>(bytes) * 100U) /
                                   total);
}

void OtaUpdate::publish(moto::ble::OtaState state,
                        moto::ble::OtaFailure failure, std::uint8_t percent,
                        std::uint32_t bytes) {
  status_.state = state;
  status_.failure = failure;
  status_.progress_percent = percent;
  status_.bytes_received = bytes;
  last_publish_ms_ = now_ms();
  if (!phone_wants_status_.load() || sender_ == nullptr) return;
  sender_(moto::ble::Message{status_}, 0, sender_context_);
}

void OtaUpdate::log_heap(const char* label) const {
  ESP_LOGI(kTag, "%s: internal free %u B, largest %u B; PSRAM free %u B", label,
           static_cast<unsigned>(
               heap_caps_get_free_size(MALLOC_CAP_INTERNAL |
                                       MALLOC_CAP_8BIT)),
           static_cast<unsigned>(heap_caps_get_largest_free_block(
               MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
           static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)));
}

void OtaUpdate::fail(moto::ble::OtaFailure failure, std::uint32_t bytes) {
  ESP_LOGE(kTag, "update failed: %u after %u bytes",
           static_cast<unsigned>(failure), static_cast<unsigned>(bytes));
  publish(moto::ble::OtaState::Failed, failure, 0, bytes);
}

void OtaUpdate::fetch(const Request& request) {
  running_.store(true);
  log_heap("before");
  // The phone's bar starts now: Downloading covers the hotspot join too, so a
  // slow network never looks like a dead screen.
  publish(moto::ble::OtaState::Downloading, moto::ble::OtaFailure::None, 0, 0);
  if (!wait_for_network()) {
    fail(moto::ble::OtaFailure::NoWifi, 0);
    running_.store(false);
    return;
  }

  moto::ble::OtaFailure failure = moto::ble::OtaFailure::None;
  std::uint32_t written = 0;
  bool verified = false;
  std::uint8_t* buffer =
      static_cast<std::uint8_t*>(std::malloc(kReadChunkBytes));
  mbedtls_sha256_context digest;
  mbedtls_sha256_init(&digest);

  const esp_partition_t* slot = esp_ota_get_next_update_partition(nullptr);
  esp_ota_handle_t handle = 0;
  esp_http_client_handle_t client = nullptr;
  if (buffer == nullptr) {
    failure = moto::ble::OtaFailure::NoMemory;
  } else if (slot == nullptr) {
    failure = moto::ble::OtaFailure::ImageRejected;
  } else if (esp_ota_begin(slot, request.size_bytes == 0
                                     ? OTA_SIZE_UNKNOWN
                                     : request.size_bytes,
                           &handle) != ESP_OK) {
    failure = moto::ble::OtaFailure::NoMemory;
  }

  if (failure == moto::ble::OtaFailure::None) {
    esp_http_client_config_t config = {};
    config.url = request.url;
    config.timeout_ms = 15'000;
    config.buffer_size = 4U * 1024U;
    config.buffer_size_tx = 1U * 1024U;
    // A release URL is https; the phone serving the image it already holds is
    // http on the hotspot link. The bundle only matters for the former.
    config.crt_bundle_attach = esp_crt_bundle_attach;
    config.keep_alive_enable = false;
    client = esp_http_client_init(&config);
    if (client == nullptr ||
        esp_http_client_open(client, 0) != ESP_OK) {
      failure = moto::ble::OtaFailure::Network;
    }
  }

  std::uint64_t total = 0;
  if (failure == moto::ble::OtaFailure::None) {
    const std::int64_t declared = esp_http_client_fetch_headers(client);
    const int status_code = esp_http_client_get_status_code(client);
    if (status_code != 200) {
      ESP_LOGE(kTag, "HTTP %d from %s", status_code, request.url);
      failure = moto::ble::OtaFailure::Network;
    } else if (declared > static_cast<std::int64_t>(moto::ble::kMaxOtaImageBytes)) {
      failure = moto::ble::OtaFailure::TooLarge;
    } else if (request.size_bytes != 0 && declared > 0 &&
               static_cast<std::uint32_t>(declared) != request.size_bytes) {
      // The phone stated a size and the server disagrees: the digest cannot
      // match whatever arrives, so refuse before writing a flash sector.
      ESP_LOGE(kTag, "size mismatch: command %u, server %lld",
               static_cast<unsigned>(request.size_bytes),
               static_cast<long long>(declared));
      failure = moto::ble::OtaFailure::Network;
    } else {
      total = declared > 0 ? static_cast<std::uint64_t>(declared)
                           : static_cast<std::uint64_t>(request.size_bytes);
    }
  }

  if (failure == moto::ble::OtaFailure::None) {
    mbedtls_sha256_starts(&digest, 0);
    std::uint8_t last_percent = 0;
    while (true) {
      const int read = esp_http_client_read(
          client, reinterpret_cast<char*>(buffer),
          static_cast<int>(kReadChunkBytes));
      if (read < 0) {
        failure = moto::ble::OtaFailure::Network;
        break;
      }
      if (read == 0) break;
      if (written + static_cast<std::uint32_t>(read) >
          moto::ble::kMaxOtaImageBytes) {
        failure = moto::ble::OtaFailure::TooLarge;
        break;
      }
      mbedtls_sha256_update(&digest, buffer, static_cast<std::size_t>(read));
      // The same lock the tlog recorder takes: an erase stalls the cache and
      // the I2C bus the IMU sampler reads from.
      const bool guarded = flash_guard_take(2'000);
      const esp_err_t write =
          esp_ota_write(handle, buffer, static_cast<std::size_t>(read));
      if (guarded) flash_guard_give();
      if (write != ESP_OK) {
        ESP_LOGE(kTag, "esp_ota_write: %s", esp_err_to_name(write));
        failure = moto::ble::OtaFailure::NoMemory;
        break;
      }
      written += static_cast<std::uint32_t>(read);
      const std::uint8_t percent = percent_of(written, total);
      if (percent >= last_percent + kProgressStep ||
          now_ms() - last_publish_ms_ >= kProgressIntervalMs) {
        last_percent = percent;
        publish(moto::ble::OtaState::Downloading, moto::ble::OtaFailure::None,
                percent, written);
      }
    }
  }

  if (client != nullptr) {
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
  }

  if (failure == moto::ble::OtaFailure::None) {
    std::uint8_t actual[moto::ble::kOtaSha256Bytes] = {};
    mbedtls_sha256_finish(&digest, actual);
    if (written == 0) {
      failure = moto::ble::OtaFailure::Network;
    } else {
      publish(moto::ble::OtaState::Verifying, moto::ble::OtaFailure::None, 100,
              written);
      if (std::memcmp(actual, request.sha256.data(), sizeof(actual)) != 0) {
        ESP_LOGE(kTag, "SHA-256 does not match the command");
        failure = moto::ble::OtaFailure::HashMismatch;
      }
    }
  }
  mbedtls_sha256_free(&digest);
  if (buffer != nullptr) std::free(buffer);

  if (failure == moto::ble::OtaFailure::None) {
    const esp_err_t ended = esp_ota_end(handle);
    if (ended != ESP_OK) {
      ESP_LOGE(kTag, "esp_ota_end: %s", esp_err_to_name(ended));
      failure = moto::ble::OtaFailure::ImageRejected;
    } else if (esp_ota_set_boot_partition(slot) != ESP_OK) {
      failure = moto::ble::OtaFailure::ImageRejected;
    } else {
      verified = true;
    }
  }
  if (!verified) {
    // Nothing points at this slot, so aborting leaves the running image alone.
    esp_ota_abort(handle);
    fail(failure, written);
    running_.store(false);
    return;
  }

  log_heap("after");
  publish(moto::ble::OtaState::Ready, moto::ble::OtaFailure::None, 100,
          written);
  ESP_LOGI(kTag, "image in place (%u bytes); rebooting into %s",
           static_cast<unsigned>(written), slot->label);
  vTaskDelay(pdMS_TO_TICKS(kRebootDelayMs));
  esp_restart();
}

}  // namespace moto::device
