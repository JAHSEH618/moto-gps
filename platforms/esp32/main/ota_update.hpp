#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <string>

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "moto/ble_protocol/ble_protocol.hpp"

namespace moto::device {

// Firmware update over the phone's hotspot (WAVESHARE_OPTIMIZATION_PLAN §4.4,
// W3). BLE carries the command and the status only: a URL plus the image's
// SHA-256 in, progress out. The image itself is fetched over the WiFi link
// (W1), written to the idle OTA slot, checked against the command's digest,
// and only then does the bootloader get pointed at it. A build that comes up
// but never completes a BLE handshake is rolled back by the bootloader
// (CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE), which is what makes an unattended
// update safe on a device the rider cannot plug into a laptop.
//
// Development builds carry tlog, the release overlay turns it off; both take
// this same path.
class OtaUpdate {
 public:
  using SendCallback = bool (*)(const moto::ble::Message& message,
                                std::uint8_t frame_flags, void* context);
  // True when the phone's link is encrypted and bonded. An image URL is an
  // instruction to overwrite the firmware, so it is refused on any other link.
  using SecureLinkCheck = bool (*)(void* context);
  // The WiFi link, as an update needs it: bring the radio up on demand, and
  // report whether it has an address yet.
  struct NetworkPort {
    void (*request)(void* context) = nullptr;
    bool (*connected)(void* context) = nullptr;
    void* context = nullptr;
  };

  OtaUpdate() = default;
  OtaUpdate(const OtaUpdate&) = delete;
  OtaUpdate& operator=(const OtaUpdate&) = delete;

  void set_sender(SendCallback callback, void* context) noexcept;
  void set_secure_link_check(SecureLinkCheck check, void* context) noexcept;
  void set_network(const NetworkPort& port) noexcept;
  // Creates the worker task. Does not touch the radio or the flash.
  esp_err_t start();

  // BLE RX task. Validates the link and the request, answers the Ack, and
  // hands the work to the worker.
  moto::ble::AckStatus on_command(const moto::ble::OtaCommand& command);
  // BLE RX task, for every phone ConnectionStatus. A phone that advertises
  // CapabilityOta gets OtaStatus; and a handshake is what confirms a freshly
  // updated image, so this also cancels the pending rollback.
  void on_phone_status(const moto::ble::ConnectionStatus& status);
  // Marks the running image valid if the bootloader is holding it pending.
  // Idempotent, and a no-op for a USB-flashed or already-confirmed image.
  void confirm_running_image();
  [[nodiscard]] bool running() const noexcept;
  void log_status() const;

 private:
  struct Request {
    char url[moto::ble::kMaxOtaUrlBytes + 1]{};
    std::array<std::uint8_t, moto::ble::kOtaSha256Bytes> sha256{};
    std::uint32_t size_bytes = 0;
  };

  bool ensure_task();
  static void task_entry(void* context);
  void run();
  // Waits for the hotspot and runs the fetch. Returns only when the update
  // has finished: Ready reboots into the new image, every other outcome
  // reports Failed and leaves the running image untouched.
  void fetch(const Request& request);
  bool wait_for_network();
  void publish(moto::ble::OtaState state, moto::ble::OtaFailure failure,
               std::uint8_t percent, std::uint32_t bytes);
  void fail(moto::ble::OtaFailure failure, std::uint32_t bytes);
  static std::uint8_t percent_of(std::uint32_t bytes,
                                 std::uint64_t total) noexcept;
  void log_heap(const char* label) const;

  SendCallback sender_ = nullptr;
  void* sender_context_ = nullptr;
  SecureLinkCheck secure_link_check_ = nullptr;
  void* secure_link_context_ = nullptr;
  NetworkPort network_{};
  QueueHandle_t queue_ = nullptr;
  TaskHandle_t task_ = nullptr;
  // Guards the pending request: written on the BLE RX task, copied by the
  // worker when it picks the work up.
  SemaphoreHandle_t request_mutex_ = nullptr;
  Request pending_{};
  bool has_pending_ = false;
  std::atomic<bool> phone_wants_status_{false};
  std::atomic<bool> running_{false};
  // Worker task only from here down.
  moto::ble::OtaStatus status_{};
  std::uint32_t last_publish_ms_ = 0;
};

}  // namespace moto::device
