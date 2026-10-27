#pragma once

#include <atomic>
#include <cstdint>

#include "esp_err.h"
#include "esp_netif.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "moto/ble_protocol/ble_protocol.hpp"

// On-demand station link to the iPhone's Personal Hotspot
// (WAVESHARE_OPTIMIZATION_PLAN §4.3, W1). BLE stays the control plane: the
// phone hands over the hotspot's SSID and password once (WifiConfig), and the
// device reports WifiStatus back. The radio is off unless something asks for
// it; `up()` initialises the driver and joins, `down()` stops and deinitialises
// it so its buffers go back to the heap. Nothing else in the firmware depends
// on WiFi being available.
//
// The PSK is a secret. It lives in NVS (namespace `moto_wifi`) and in the
// driver's config while the radio is up, and is never logged or sent back.
class WifiLink {
 public:
  using SendCallback = bool (*)(const moto::ble::Message& message,
                                std::uint8_t frame_flags,
                                void* context);
  // True when the phone's link is encrypted and bonded. WifiConfig is refused
  // on any other link.
  using SecureLinkCheck = bool (*)(void* context);

  WifiLink() = default;
  WifiLink(const WifiLink&) = delete;
  WifiLink& operator=(const WifiLink&) = delete;

  void set_sender(SendCallback callback, void* context) noexcept;
  void set_secure_link_check(SecureLinkCheck check, void* context) noexcept;
  // Loads the stored network and starts the small worker task. Does not touch
  // the radio.
  esp_err_t start();

  // BLE RX task. Validates the link and stores the network; answers the Ack.
  moto::ble::AckStatus on_config(const moto::ble::WifiConfig& config);
  // BLE RX task, for every phone ConnectionStatus. A phone that advertises
  // CapabilityWifiHotspot gets the current status once the handshake is done.
  void on_phone_status(const moto::ble::ConnectionStatus& status);
  void on_link_state(bool active);

  // Bench commands for now: no task uses WiFi yet.
  void request_up();
  void request_down();
  void request_forget();
  void log_status();

  // True while the station has an address. Read by the OTA worker, which
  // waits for this before it fetches, and by nobody on the UI path.
  [[nodiscard]] bool connected() const noexcept { return connected_.load(); }

 private:
  enum class Event : std::uint8_t {
    Up,
    Down,
    Forget,
    ConfigChanged,
    Publish,
    StaStarted,
    StaDisconnected,
    GotIp,
    LostIp,
  };
  struct QueuedEvent {
    Event event = Event::Publish;
    std::uint8_t reason = 0;
    std::uint32_t ipv4 = 0;
  };

  static void task_entry(void* context);
  static void wifi_event(void* argument, esp_event_base_t base,
                         std::int32_t id, void* data);
  static void ip_event(void* argument, esp_event_base_t base,
                       std::int32_t id, void* data);
  void run();
  void post(Event event, std::uint8_t reason = 0, std::uint32_t ipv4 = 0);
  void handle(const QueuedEvent& event, std::uint32_t now_ms);
  void service_timers(std::uint32_t now_ms);
  void bring_up(std::uint32_t now_ms);
  void tear_down();
  void connect_attempt(std::uint32_t now_ms);
  void fail(moto::ble::WifiFailure failure);
  void set_state(moto::ble::WifiLinkState state,
                 moto::ble::WifiFailure failure);
  void publish();
  bool load_config();
  bool has_credentials() const;
  void log_heap(const char* label) const;

  SendCallback sender_ = nullptr;
  void* sender_context_ = nullptr;
  SecureLinkCheck secure_link_check_ = nullptr;
  void* secure_link_context_ = nullptr;
  QueueHandle_t queue_ = nullptr;
  TaskHandle_t task_ = nullptr;
  // Guards the stored network: written on the BLE RX task, read by the
  // worker when it brings the radio up.
  SemaphoreHandle_t config_mutex_ = nullptr;
  moto::ble::WifiPolicy policy_ = moto::ble::WifiPolicy::Disabled;
  char ssid_[moto::ble::kMaxWifiSsidBytes + 1]{};
  char psk_[moto::ble::kMaxWifiPskBytes + 1]{};
  std::atomic<bool> phone_wants_status_{false};
  std::atomic<bool> connected_{false};

  // Worker task only from here down.
  moto::ble::WifiStatus status_{};
  esp_netif_t* netif_ = nullptr;
  esp_event_handler_instance_t wifi_handler_ = nullptr;
  esp_event_handler_instance_t ip_handler_ = nullptr;
  bool netif_ready_ = false;
  bool own_event_loop_ = false;
  bool driver_up_ = false;
  std::uint8_t attempts_ = 0;
  std::uint8_t last_reason_ = 0;
  std::uint32_t attempt_deadline_ms_ = 0;
  std::uint32_t retry_at_ms_ = 0;
  std::uint32_t connected_at_ms_ = 0;
  std::uint32_t next_rssi_ms_ = 0;
  std::uint32_t publish_at_ms_ = 0;
  std::uint32_t last_publish_ms_ = 0;
};
