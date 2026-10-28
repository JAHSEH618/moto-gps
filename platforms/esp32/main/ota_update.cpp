#include "ota_update.hpp"

#include <algorithm>
#include <cstdio>
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
// WifiLink makes three 20 s join attempts 1.5 s apart before it gives up, and
// an iPhone hotspot nobody has open in Settings often misses the first one.
// Waiting for less than all three reported NoWifi while the next attempt was
// still joining.
constexpr std::uint32_t kNetworkWaitMs = 65'000;
constexpr std::uint32_t kNetworkPollMs = 250;
// While nothing else moves (the join, a resume) the last status goes out
// again this often, so the phone can tell a slow step from a dead screen.
constexpr std::uint32_t kHeartbeatMs = 2'000;
// The body is read in 4 KiB pieces: small enough that a 10 KiB task stack
// holds the TLS read path.
constexpr std::size_t kReadChunkBytes = 4U * 1024U;
// The pieces collect in a 64 KiB PSRAM stage that is written one block at a
// time. The slot is opened with OTA_WITH_SEQUENTIAL_WRITES, so each write
// erases the block it lands on, under the flash guard with the write, instead
// of esp_ota_begin erasing 6 MB in one unguarded call before the first byte
// was even requested. Block-aligned writes keep the driver on its 64 KiB
// block erase rather than sixteen sector erases.
constexpr std::size_t kStageBytes = 64U * 1024U;
// A body that stops short (the hotspot dropped, the server closed early, a
// read timed out) is picked up where it stopped with a Range request, this
// many times, before the update fails as Network.
constexpr int kMaxResumes = 3;
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

// What one attempt to open the body came to.
struct Opened {
  moto::ble::OtaFailure failure = moto::ble::OtaFailure::None;
  // A connection that dropped is worth another try. An answer the server
  // will give again (an HTTP error, a size that disagrees, a Range it
  // ignored) is not.
  bool retry = false;
};

// Opens the image at `offset`: the whole body from 0, the rest of it with a
// Range request after a short read. `total` is the image size: the command's
// when it stated one, otherwise the first response's.
Opened open_at(esp_http_client_handle_t client, const char* url,
               std::uint32_t offset, std::uint32_t stated_size,
               std::uint64_t* total) {
  char range[32] = {};
  if (offset == 0) {
    esp_http_client_delete_header(client, "Range");
  } else {
    std::snprintf(range, sizeof(range), "bytes=%u-",
                  static_cast<unsigned>(offset));
    esp_http_client_set_header(client, "Range", range);
  }
  if (esp_http_client_open(client, 0) != ESP_OK) {
    ESP_LOGE(kTag, "open failed: internal free %u largest %u",
             static_cast<unsigned>(
                 heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),
             static_cast<unsigned>(
                 heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL)));
    return {moto::ble::OtaFailure::Network, true};
  }
  const std::int64_t declared = esp_http_client_fetch_headers(client);
  if (declared < 0) {
    ESP_LOGE(kTag,
             "headers failed at offset %u; internal free %u largest %u",
             static_cast<unsigned>(offset),
             static_cast<unsigned>(
                 heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),
             static_cast<unsigned>(
                 heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL)));
    return {moto::ble::OtaFailure::Network, true};
  }
  const int status_code = esp_http_client_get_status_code(client);
  const int wanted = offset == 0 ? 200 : 206;
  if (status_code != wanted) {
    ESP_LOGE(kTag, "HTTP %d (wanted %d) from %s", status_code, wanted, url);
    // A 5xx is the server's bad moment; anything else it will say again.
    return {moto::ble::OtaFailure::Network, status_code >= 500};
  }
  if (offset == 0) {
    if (declared > static_cast<std::int64_t>(moto::ble::kMaxOtaImageBytes)) {
      return {moto::ble::OtaFailure::TooLarge, false};
    }
    if (stated_size != 0 && declared > 0 &&
        static_cast<std::uint32_t>(declared) != stated_size) {
      // The phone stated a size and the server disagrees: the digest cannot
      // match whatever arrives, so refuse before writing a flash sector.
      ESP_LOGE(kTag, "size mismatch: command %u, server %lld",
               static_cast<unsigned>(stated_size),
               static_cast<long long>(declared));
      return {moto::ble::OtaFailure::Network, false};
    }
    *total = declared > 0 ? static_cast<std::uint64_t>(declared)
                          : static_cast<std::uint64_t>(stated_size);
  } else if (*total != 0 && declared > 0 &&
             offset + static_cast<std::uint64_t>(declared) != *total) {
    ESP_LOGE(kTag, "resume at %u answered %lld bytes of %llu",
             static_cast<unsigned>(offset), static_cast<long long>(declared),
             static_cast<unsigned long long>(*total));
    return {moto::ble::OtaFailure::Network, false};
  }
  return {};
}

moto::ble::OtaFailure write_failure(esp_err_t result) {
  return result == ESP_ERR_OTA_VALIDATE_FAILED
             ? moto::ble::OtaFailure::ImageRejected
             : moto::ble::OtaFailure::NoMemory;
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
    if (now_ms() - last_publish_ms_ >= kHeartbeatMs) {
      publish(status_.state, status_.failure, status_.progress_percent,
              status_.bytes_received);
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
  // Bytes read from the body (and hashed), and bytes already in the slot;
  // the difference is what sits in the stage.
  std::uint32_t received = 0;
  std::uint32_t written = 0;
  std::size_t staged = 0;
  bool verified = false;
  auto* stage = static_cast<std::uint8_t*>(
      heap_caps_malloc(kStageBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  mbedtls_sha256_context digest;
  mbedtls_sha256_init(&digest);

  const esp_partition_t* slot = esp_ota_get_next_update_partition(nullptr);
  esp_ota_handle_t handle = 0;
  esp_http_client_handle_t client = nullptr;
  if (stage == nullptr) {
    failure = moto::ble::OtaFailure::NoMemory;
  } else if (slot == nullptr) {
    failure = moto::ble::OtaFailure::ImageRejected;
  } else if (esp_ota_begin(slot, OTA_WITH_SEQUENTIAL_WRITES, &handle) !=
             ESP_OK) {
    failure = moto::ble::OtaFailure::NoMemory;
  }

  if (failure == moto::ble::OtaFailure::None) {
    esp_http_client_config_t config = {};
    config.url = request.url;
    config.timeout_ms = 15'000;
    // Both live in internal RAM. The image arrives at ~1 MB/s at worst, so a
    // 2 KiB receive window costs nothing measurable and leaves the AES engine
    // the DMA memory it needs: 4 KiB + 1 KiB here left `esp-aes: Failed to
    // allocate memory` on the first body read.
    config.buffer_size = 2U * 1024U;
    config.buffer_size_tx = 512U;
    // A release URL is https; the phone serving the image it already holds is
    // http on the hotspot link. The bundle only matters for the former.
    config.crt_bundle_attach = esp_crt_bundle_attach;
    config.keep_alive_enable = false;
    client = esp_http_client_init(&config);
    if (client == nullptr) failure = moto::ble::OtaFailure::NoMemory;
  }

  // Writes the stage into the slot. The same lock the tlog recorder takes:
  // an erase stalls the cache and the I2C bus the IMU sampler reads from.
  const auto flush = [&]() -> esp_err_t {
    if (staged == 0) return ESP_OK;
    const bool guarded = flash_guard_take(2'000);
    const esp_err_t result = esp_ota_write(handle, stage, staged);
    if (guarded) flash_guard_give();
    if (result != ESP_OK) {
      ESP_LOGE(kTag, "esp_ota_write: %s", esp_err_to_name(result));
      return result;
    }
    written += static_cast<std::uint32_t>(staged);
    staged = 0;
    return ESP_OK;
  };

  std::uint64_t total = 0;
  int resumes = 0;
  bool complete = false;
  std::uint8_t last_percent = 0;
  if (failure == moto::ble::OtaFailure::None) {
    mbedtls_sha256_starts(&digest, 0);
  }
  while (failure == moto::ble::OtaFailure::None && !complete) {
    const Opened opened =
        open_at(client, request.url, received, request.size_bytes, &total);
    if (opened.failure == moto::ble::OtaFailure::None) {
      while (true) {
        const int want = static_cast<int>(
            std::min(kReadChunkBytes, kStageBytes - staged));
        const int read = esp_http_client_read(
            client, reinterpret_cast<char*>(stage + staged), want);
        if (read <= 0) {
          // A stall that read nothing at all is the interesting one: it says
          // whether the body never arrived (0 / EAGAIN) or the transport
          // broke (a negative esp_err_t), which the retry path cannot tell
          // apart from the log alone.
          ESP_LOGW(kTag,
                   "read=%d at %u of %llu (status %d chunked %d complete "
                   "%d); internal free %u largest %u",
                   read, static_cast<unsigned>(received),
                   static_cast<unsigned long long>(total),
                   esp_http_client_get_status_code(client),
                   esp_http_client_is_chunked_response(client) ? 1 : 0,
                   esp_http_client_is_complete_data_received(client) ? 1 : 0,
                   static_cast<unsigned>(
                       heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),
                   static_cast<unsigned>(
                       heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL)));
          break;
        }
        if (received + static_cast<std::uint32_t>(read) >
            moto::ble::kMaxOtaImageBytes) {
          failure = moto::ble::OtaFailure::TooLarge;
          break;
        }
        mbedtls_sha256_update(&digest, stage + staged,
                              static_cast<std::size_t>(read));
        staged += static_cast<std::size_t>(read);
        received += static_cast<std::uint32_t>(read);
        if (staged == kStageBytes) {
          const esp_err_t result = flush();
          if (result != ESP_OK) {
            failure = write_failure(result);
            break;
          }
        }
        const std::uint8_t percent = percent_of(received, total);
        if (percent >= last_percent + kProgressStep ||
            now_ms() - last_publish_ms_ >= kProgressIntervalMs) {
          last_percent = percent;
          publish(moto::ble::OtaState::Downloading,
                  moto::ble::OtaFailure::None, percent, received);
        }
      }
      if (failure != moto::ble::OtaFailure::None) break;
      // A clean end and a connection that closed early both read 0, and a
      // timeout reads -ESP_ERR_HTTP_EAGAIN: the count is what says the body
      // is all here. Taking every 0 for the end hashed a truncated image and
      // blamed the file with HashMismatch.
      complete = total != 0
                     ? received >= total
                     : esp_http_client_is_complete_data_received(client);
      if (complete) break;
    } else if (!opened.retry) {
      failure = opened.failure;
      break;
    }
    esp_http_client_close(client);
    if (resumes == kMaxResumes) {
      failure = moto::ble::OtaFailure::Network;
      break;
    }
    ++resumes;
    ESP_LOGW(kTag, "body stopped at %u of %llu bytes; resuming (%d/%d)",
             static_cast<unsigned>(received),
             static_cast<unsigned long long>(total), resumes, kMaxResumes);
    log_heap("resume");
    // The stall that got us here was silent; say the update is still alive
    // before the reopen adds its own wait.
    publish(moto::ble::OtaState::Downloading, moto::ble::OtaFailure::None,
            percent_of(received, total), received);
    // The radio may be what dropped: give WifiLink its attempts back.
    if (!wait_for_network()) failure = moto::ble::OtaFailure::NoWifi;
  }
  if (failure == moto::ble::OtaFailure::None) {
    const esp_err_t result = flush();
    if (result != ESP_OK) failure = write_failure(result);
  }

  if (client != nullptr) {
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
  }

  if (failure == moto::ble::OtaFailure::None) {
    std::uint8_t actual[moto::ble::kOtaSha256Bytes] = {};
    mbedtls_sha256_finish(&digest, actual);
    if (received == 0) {
      failure = moto::ble::OtaFailure::Network;
    } else {
      publish(moto::ble::OtaState::Verifying, moto::ble::OtaFailure::None, 100,
              received);
      if (std::memcmp(actual, request.sha256.data(), sizeof(actual)) != 0) {
        ESP_LOGE(kTag, "SHA-256 does not match the command");
        failure = moto::ble::OtaFailure::HashMismatch;
      }
    }
  }
  mbedtls_sha256_free(&digest);
  if (stage != nullptr) heap_caps_free(stage);

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
    fail(failure, received);
    running_.store(false);
    return;
  }

  log_heap("after");
  publish(moto::ble::OtaState::Ready, moto::ble::OtaFailure::None, 100,
          received);
  ESP_LOGI(kTag, "image in place (%u bytes); rebooting into %s",
           static_cast<unsigned>(written), slot->label);
  vTaskDelay(pdMS_TO_TICKS(kRebootDelayMs));
  esp_restart();
}

}  // namespace moto::device
