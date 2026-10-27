#include "wifi_link.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>

#include "esp_event.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "nvs.h"

namespace {

using moto::ble::WifiFailure;
using moto::ble::WifiLinkState;
using moto::ble::WifiPolicy;

constexpr char kTag[] = "wifi_link";
constexpr char kNamespace[] = "moto_wifi";
constexpr char kPolicyKey[] = "policy";
constexpr char kSsidKey[] = "ssid";
constexpr char kPskKey[] = "psk";

// One attempt is association, the 4-way handshake and DHCP. The iPhone
// hotspot answers DHCP within a second or two; 20 s also covers a hotspot
// that is still waking its 2.4 GHz radio.
constexpr std::uint32_t kAttemptTimeoutMs = 20'000;
constexpr std::uint8_t kMaxAttempts = 3;
constexpr std::uint32_t kRetryDelayMs = 1'500;
constexpr std::uint32_t kRssiPeriodMs = 5'000;
constexpr int kRssiReportStepDb = 3;
constexpr std::uint32_t kRefreshMs = 30'000;
// No task uses the link yet, so a forgotten `wifi up` must not keep the radio
// (and its heap) for the rest of the ride.
constexpr std::uint32_t kMaxSessionMs = 10U * 60U * 1'000U;
// The phone drops business messages until it has the device's final Ready,
// which the transport sends after the phone's Ready has been handled.
constexpr std::uint32_t kHandshakePublishDelayMs = 500;
constexpr std::uint32_t kTickMs = 250;
constexpr std::size_t kQueueDepth = 12;
// esp_wifi_init and the event-loop calls run on this stack.
constexpr std::uint32_t kTaskStackBytes = 4'096;

std::uint32_t now_ms() {
  return static_cast<std::uint32_t>(esp_timer_get_time() / 1'000);
}

bool reached(std::uint32_t now, std::uint32_t deadline) {
  return static_cast<std::int32_t>(now - deadline) >= 0;
}

// The PSK passes through stack buffers on its way to the driver; do not leave
// it there for the next frame to find.
void wipe(void* data, std::size_t size) {
  volatile auto* bytes = static_cast<volatile std::uint8_t*>(data);
  for (std::size_t i = 0; i < size; ++i) bytes[i] = 0;
}

WifiFailure classify(std::uint8_t reason, bool was_connected) {
  switch (reason) {
    case WIFI_REASON_NO_AP_FOUND:
    case WIFI_REASON_NO_AP_FOUND_IN_RSSI_THRESHOLD:
    case WIFI_REASON_BEACON_TIMEOUT:
      return was_connected ? WifiFailure::Disconnected
                           : WifiFailure::NetworkNotFound;
    // A wrong password shows up as a handshake that never completes.
    case WIFI_REASON_AUTH_FAIL:
    case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
    case WIFI_REASON_HANDSHAKE_TIMEOUT:
    case WIFI_REASON_NO_AP_FOUND_W_COMPATIBLE_SECURITY:
    case WIFI_REASON_NO_AP_FOUND_IN_AUTHMODE_THRESHOLD:
      return WifiFailure::AuthFailed;
    default:
      return was_connected ? WifiFailure::Disconnected : WifiFailure::Timeout;
  }
}

const char* state_name(WifiLinkState state) {
  switch (state) {
    case WifiLinkState::Off: return "off";
    case WifiLinkState::Connecting: return "connecting";
    case WifiLinkState::Connected: return "connected";
    case WifiLinkState::Failed: return "failed";
  }
  return "?";
}

const char* failure_name(WifiFailure failure) {
  switch (failure) {
    case WifiFailure::None: return "none";
    case WifiFailure::NoCredentials: return "no_credentials";
    case WifiFailure::NetworkNotFound: return "network_not_found";
    case WifiFailure::AuthFailed: return "auth_failed";
    case WifiFailure::Timeout: return "timeout";
    case WifiFailure::NoMemory: return "no_memory";
    case WifiFailure::Disconnected: return "disconnected";
  }
  return "?";
}

}  // namespace

void WifiLink::set_sender(SendCallback callback, void* context) noexcept {
  sender_ = callback;
  sender_context_ = context;
}

void WifiLink::set_secure_link_check(SecureLinkCheck check,
                                     void* context) noexcept {
  secure_link_check_ = check;
  secure_link_context_ = context;
}

esp_err_t WifiLink::start() {
  config_mutex_ = xSemaphoreCreateMutex();
  queue_ = xQueueCreate(kQueueDepth, sizeof(QueuedEvent));
  if (config_mutex_ == nullptr || queue_ == nullptr) return ESP_ERR_NO_MEM;
  load_config();
  status_.flags = has_credentials() ? moto::ble::WifiStatusHasCredentials : 0;
  if (xTaskCreate(task_entry, "moto_wifi", kTaskStackBytes, this, 2,
                  &task_) != pdPASS) {
    return ESP_ERR_NO_MEM;
  }
  return ESP_OK;
}

bool WifiLink::load_config() {
  nvs_handle_t handle = 0;
  if (nvs_open(kNamespace, NVS_READONLY, &handle) != ESP_OK) {
    return false;  // Never configured.
  }
  std::uint8_t policy = 0;
  std::size_t ssid_size = sizeof(ssid_);
  std::size_t psk_size = sizeof(psk_);
  const bool ok = nvs_get_u8(handle, kPolicyKey, &policy) == ESP_OK &&
                  nvs_get_str(handle, kSsidKey, ssid_, &ssid_size) == ESP_OK &&
                  nvs_get_str(handle, kPskKey, psk_, &psk_size) == ESP_OK &&
                  policy == static_cast<std::uint8_t>(WifiPolicy::OnDemand);
  nvs_close(handle);
  if (!ok) {
    wipe(ssid_, sizeof(ssid_));
    wipe(psk_, sizeof(psk_));
    policy_ = WifiPolicy::Disabled;
    return false;
  }
  policy_ = WifiPolicy::OnDemand;
  ESP_LOGI(kTag, "stored hotspot \"%s\"", ssid_);
  return true;
}

bool WifiLink::has_credentials() const {
  return policy_ == WifiPolicy::OnDemand && ssid_[0] != '\0';
}

moto::ble::AckStatus WifiLink::on_config(
    const moto::ble::WifiConfig& config) {
  // The PSK must not arrive over a link anyone in radio range could have
  // opened. The characteristic already requires encryption; bonding is the
  // phone the rider paired, not merely an encrypted stranger.
  if (secure_link_check_ == nullptr ||
      !secure_link_check_(secure_link_context_)) {
    ESP_LOGW(kTag, "WifiConfig refused: link is not encrypted and bonded");
    return moto::ble::AckStatus::InvalidState;
  }
  if (config_mutex_ == nullptr) return moto::ble::AckStatus::InvalidState;

  xSemaphoreTake(config_mutex_, portMAX_DELAY);
  const bool unchanged =
      config.policy == policy_ &&
      config.ssid == ssid_ && config.psk == psk_;
  bool stored = true;
  if (!unchanged) {
    nvs_handle_t handle = 0;
    stored = nvs_open(kNamespace, NVS_READWRITE, &handle) == ESP_OK;
    if (stored) {
      if (config.policy == WifiPolicy::Disabled) {
        stored = nvs_erase_all(handle) == ESP_OK;
      } else {
        stored = nvs_set_u8(handle, kPolicyKey,
                            static_cast<std::uint8_t>(config.policy)) ==
                     ESP_OK &&
                 nvs_set_str(handle, kSsidKey, config.ssid.c_str()) ==
                     ESP_OK &&
                 nvs_set_str(handle, kPskKey, config.psk.c_str()) == ESP_OK;
      }
      stored = stored && nvs_commit(handle) == ESP_OK;
      nvs_close(handle);
    }
    if (stored) {
      wipe(ssid_, sizeof(ssid_));
      wipe(psk_, sizeof(psk_));
      policy_ = config.policy;
      // The shared validator bounds both lengths to the buffers.
      std::memcpy(ssid_, config.ssid.data(), config.ssid.size());
      std::memcpy(psk_, config.psk.data(), config.psk.size());
    }
  }
  xSemaphoreGive(config_mutex_);

  if (!stored) {
    ESP_LOGE(kTag, "hotspot could not be stored in NVS");
    return moto::ble::AckStatus::Failed;
  }
  if (unchanged) {
    ESP_LOGI(kTag, "hotspot unchanged; nothing written");
  } else if (config.policy == WifiPolicy::Disabled) {
    ESP_LOGI(kTag, "hotspot forgotten");
  } else {
    // The SSID is shown on the phone anyway; the password never is.
    ESP_LOGI(kTag, "hotspot stored: \"%s\" (password %u bytes)",
             config.ssid.c_str(), static_cast<unsigned>(config.psk.size()));
  }
  post(Event::ConfigChanged);
  return moto::ble::AckStatus::Ok;
}

void WifiLink::on_phone_status(const moto::ble::ConnectionStatus& status) {
  const bool wants =
      (status.capabilities & moto::ble::CapabilityWifiHotspot) != 0U;
  phone_wants_status_.store(wants);
  if (wants && status.state == moto::ble::ConnectionState::Ready) {
    post(Event::Publish);
  }
}

void WifiLink::on_link_state(bool active) {
  // The WiFi link is independent of BLE: losing the phone's BLE session does
  // not stop a transfer that is already running. Only the reports stop.
  if (!active) phone_wants_status_.store(false);
}

void WifiLink::request_up() { post(Event::Up); }
void WifiLink::request_down() { post(Event::Down); }
void WifiLink::request_forget() { post(Event::Forget); }

void WifiLink::log_status() {
  // The worker owns status_; printing there avoids a torn read.
  post(Event::Publish, 1);
}

void WifiLink::post(Event event, std::uint8_t reason, std::uint32_t ipv4) {
  if (queue_ == nullptr) return;
  const QueuedEvent queued{event, reason, ipv4};
  if (xQueueSend(queue_, &queued, 0) != pdTRUE) {
    ESP_LOGW(kTag, "event queue full; dropped event %u",
             static_cast<unsigned>(event));
  }
}

void WifiLink::task_entry(void* context) {
  static_cast<WifiLink*>(context)->run();
}

void WifiLink::run() {
  QueuedEvent event;
  while (true) {
    if (xQueueReceive(queue_, &event, pdMS_TO_TICKS(kTickMs)) == pdTRUE) {
      handle(event, now_ms());
    }
    service_timers(now_ms());
  }
}

void WifiLink::wifi_event(void* argument, esp_event_base_t,
                          std::int32_t id, void* data) {
  auto* self = static_cast<WifiLink*>(argument);
  if (id == WIFI_EVENT_STA_START) {
    self->post(Event::StaStarted);
  } else if (id == WIFI_EVENT_STA_DISCONNECTED && data != nullptr) {
    const auto* disconnected =
        static_cast<const wifi_event_sta_disconnected_t*>(data);
    self->post(Event::StaDisconnected, disconnected->reason);
  }
}

void WifiLink::ip_event(void* argument, esp_event_base_t, std::int32_t id,
                        void* data) {
  auto* self = static_cast<WifiLink*>(argument);
  if (id == IP_EVENT_STA_GOT_IP && data != nullptr) {
    const auto* got = static_cast<const ip_event_got_ip_t*>(data);
    self->post(Event::GotIp, 0, got->ip_info.ip.addr);
  } else if (id == IP_EVENT_STA_LOST_IP) {
    self->post(Event::LostIp);
  }
}

void WifiLink::handle(const QueuedEvent& event, std::uint32_t now) {
  switch (event.event) {
    case Event::Up:
      if (driver_up_) {
        publish();
      } else {
        bring_up(now);
      }
      return;

    case Event::Down:
      tear_down();
      set_state(WifiLinkState::Off, WifiFailure::None);
      publish();
      return;

    case Event::Forget: {
      tear_down();
      nvs_handle_t handle = 0;
      if (nvs_open(kNamespace, NVS_READWRITE, &handle) == ESP_OK) {
        nvs_erase_all(handle);
        nvs_commit(handle);
        nvs_close(handle);
      }
      xSemaphoreTake(config_mutex_, portMAX_DELAY);
      policy_ = WifiPolicy::Disabled;
      wipe(ssid_, sizeof(ssid_));
      wipe(psk_, sizeof(psk_));
      xSemaphoreGive(config_mutex_);
      ESP_LOGI(kTag, "hotspot forgotten");
      set_state(WifiLinkState::Off, WifiFailure::None);
      publish();
      return;
    }

    case Event::ConfigChanged:
      // A running link keeps the network it joined; the new one applies at
      // the next `up`. Forgetting the network stops it now.
      if (driver_up_) {
        tear_down();
        set_state(WifiLinkState::Off, WifiFailure::None);
      }
      publish();
      return;

    case Event::Publish:
      if (event.reason == 1) {
        ESP_LOGI(kTag,
                 "wifi state=%s failure=%s stored=%d ip=%u.%u.%u.%u rssi=%d "
                 "phone=%d attempts=%u stack_free=%u",
                 state_name(status_.state), failure_name(status_.failure),
                 has_credentials() ? 1 : 0, status_.ipv4[0], status_.ipv4[1],
                 status_.ipv4[2], status_.ipv4[3], status_.rssi_dbm,
                 phone_wants_status_.load() ? 1 : 0,
                 static_cast<unsigned>(attempts_),
                 static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr) *
                                       sizeof(StackType_t)));
        log_heap("wifi status");
        return;
      }
      publish_at_ms_ = now + kHandshakePublishDelayMs;
      if (publish_at_ms_ == 0) publish_at_ms_ = 1;
      return;

    case Event::StaStarted:
      if (driver_up_) connect_attempt(now);
      return;

    case Event::StaDisconnected: {
      if (!driver_up_) return;
      last_reason_ = event.reason;
      const bool was_connected = status_.state == WifiLinkState::Connected;
      ESP_LOGW(kTag, "station disconnected: reason=%u",
               static_cast<unsigned>(event.reason));
      if (was_connected) {
        // One fresh set of attempts to get the same hotspot back.
        attempts_ = 0;
        set_state(WifiLinkState::Connecting, WifiFailure::None);
        publish();
      }
      attempt_deadline_ms_ = 0;
      if (attempts_ >= kMaxAttempts) {
        fail(classify(last_reason_, was_connected));
      } else if (retry_at_ms_ == 0) {
        // A timed-out attempt already scheduled its retry before it
        // disconnected.
        retry_at_ms_ = now + kRetryDelayMs;
        if (retry_at_ms_ == 0) retry_at_ms_ = 1;
      }
      return;
    }

    case Event::GotIp: {
      if (!driver_up_) return;
      status_.state = WifiLinkState::Connected;
      status_.failure = WifiFailure::None;
      for (std::size_t i = 0; i < status_.ipv4.size(); ++i) {
        status_.ipv4[i] =
            static_cast<std::uint8_t>((event.ipv4 >> (8U * i)) & 0xFFU);
      }
      wifi_ap_record_t ap{};
      status_.rssi_dbm = esp_wifi_sta_get_ap_info(&ap) == ESP_OK
                             ? static_cast<std::int8_t>(
                                   std::min<int>(ap.rssi, -1))
                             : -1;
      attempts_ = 0;
      attempt_deadline_ms_ = 0;
      if (connected_at_ms_ == 0) connected_at_ms_ = now;
      next_rssi_ms_ = now + kRssiPeriodMs;
      ESP_LOGI(kTag, "joined; ip=%u.%u.%u.%u rssi=%d", status_.ipv4[0],
               status_.ipv4[1], status_.ipv4[2], status_.ipv4[3],
               status_.rssi_dbm);
      log_heap("wifi connected");
      publish();
      return;
    }

    case Event::LostIp:
      if (driver_up_ && status_.state == WifiLinkState::Connected) {
        set_state(WifiLinkState::Connecting, WifiFailure::None);
        attempt_deadline_ms_ = now + kAttemptTimeoutMs;
        publish();
      }
      return;
  }
}

void WifiLink::service_timers(std::uint32_t now) {
  if (driver_up_) {
    if (retry_at_ms_ != 0 && reached(now, retry_at_ms_)) {
      retry_at_ms_ = 0;
      connect_attempt(now);
    }
    if (status_.state == WifiLinkState::Connecting &&
        attempt_deadline_ms_ != 0 && reached(now, attempt_deadline_ms_)) {
      attempt_deadline_ms_ = 0;
      if (attempts_ >= kMaxAttempts) {
        fail(last_reason_ != 0 ? classify(last_reason_, false)
                               : WifiFailure::Timeout);
        return;
      }
      // Schedule before disconnecting: a link that never associated may not
      // produce a disconnect event to do it.
      retry_at_ms_ = now + kRetryDelayMs;
      if (retry_at_ms_ == 0) retry_at_ms_ = 1;
      esp_wifi_disconnect();
    }
    if (status_.state == WifiLinkState::Connected) {
      if (connected_at_ms_ != 0 &&
          reached(now, connected_at_ms_ + kMaxSessionMs)) {
        ESP_LOGI(kTag, "idle for %u min; switching WiFi off",
                 static_cast<unsigned>(kMaxSessionMs / 60'000U));
        tear_down();
        set_state(WifiLinkState::Off, WifiFailure::None);
        publish();
        return;
      }
      if (reached(now, next_rssi_ms_)) {
        next_rssi_ms_ = now + kRssiPeriodMs;
        wifi_ap_record_t ap{};
        if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
          const auto rssi =
              static_cast<std::int8_t>(std::min<int>(ap.rssi, -1));
          const bool moved =
              std::abs(rssi - status_.rssi_dbm) >= kRssiReportStepDb;
          status_.rssi_dbm = rssi;
          if (moved || reached(now, last_publish_ms_ + kRefreshMs)) {
            publish();
          }
        }
      }
    }
  }
  if (publish_at_ms_ != 0 && reached(now, publish_at_ms_)) {
    publish();
  }
}

void WifiLink::bring_up(std::uint32_t now) {
  if (!has_credentials()) {
    ESP_LOGW(kTag, "wifi up: no hotspot stored; set it in the app first");
    set_state(WifiLinkState::Failed, WifiFailure::NoCredentials);
    publish();
    return;
  }
  log_heap("wifi before up");

  // esp_netif_init() starts lwIP's tcpip task, which cannot be stopped again;
  // it is deferred to the first `up` so a device that never uses WiFi never
  // pays for it.
  if (!netif_ready_) {
    const esp_err_t result = esp_netif_init();
    if (result != ESP_OK) {
      ESP_LOGE(kTag, "esp_netif_init: %s", esp_err_to_name(result));
      fail(WifiFailure::NoMemory);
      return;
    }
    netif_ready_ = true;
  }
  esp_err_t result = esp_event_loop_create_default();
  if (result == ESP_OK) {
    own_event_loop_ = true;
  } else if (result != ESP_ERR_INVALID_STATE) {
    ESP_LOGE(kTag, "event loop: %s", esp_err_to_name(result));
    fail(WifiFailure::NoMemory);
    return;
  }
  netif_ = esp_netif_create_default_wifi_sta();
  if (netif_ == nullptr) {
    fail(WifiFailure::NoMemory);
    return;
  }
  esp_netif_set_hostname(netif_, "moto-gps");

  wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
  result = esp_wifi_init(&init);
  if (result != ESP_OK) {
    ESP_LOGE(kTag, "esp_wifi_init: %s", esp_err_to_name(result));
    fail(WifiFailure::NoMemory);
    return;
  }
  driver_up_ = true;
  // The network lives in this firmware's own NVS namespace; the driver must
  // not keep a second copy of the password in its own.
  esp_wifi_set_storage(WIFI_STORAGE_RAM);
  esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                      &WifiLink::wifi_event, this,
                                      &wifi_handler_);
  esp_event_handler_instance_register(IP_EVENT, ESP_EVENT_ANY_ID,
                                      &WifiLink::ip_event, this, &ip_handler_);

  wifi_config_t config{};
  xSemaphoreTake(config_mutex_, portMAX_DELAY);
  std::memcpy(config.sta.ssid, ssid_,
              std::min(sizeof(config.sta.ssid), std::strlen(ssid_)));
  std::memcpy(config.sta.password, psk_,
              std::min(sizeof(config.sta.password), std::strlen(psk_)));
  xSemaphoreGive(config_mutex_);
  // iPhone Personal Hotspot is WPA2-PSK, and only on 2.4 GHz with "Maximize
  // Compatibility" on. Anything weaker is refused rather than joined.
  config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
  config.sta.pmf_cfg.capable = true;
  config.sta.pmf_cfg.required = false;
  config.sta.sae_pwe_h2e = WPA3_SAE_PWE_BOTH;
  result = esp_wifi_set_mode(WIFI_MODE_STA);
  if (result == ESP_OK) result = esp_wifi_set_config(WIFI_IF_STA, &config);
  wipe(&config, sizeof(config));
  if (result == ESP_OK) result = esp_wifi_start();
  if (result != ESP_OK) {
    ESP_LOGE(kTag, "wifi start: %s", esp_err_to_name(result));
    fail(result == ESP_ERR_NO_MEM ? WifiFailure::NoMemory
                                  : WifiFailure::Timeout);
    return;
  }
  attempts_ = 0;
  last_reason_ = 0;
  connected_at_ms_ = 0;
  // Covers a STA_START that never arrives as well as the first attempt.
  attempt_deadline_ms_ = now + kAttemptTimeoutMs;
  set_state(WifiLinkState::Connecting, WifiFailure::None);
  log_heap("wifi started");
  publish();
}

void WifiLink::connect_attempt(std::uint32_t now) {
  ++attempts_;
  attempt_deadline_ms_ = now + kAttemptTimeoutMs;
  if (attempt_deadline_ms_ == 0) attempt_deadline_ms_ = 1;
  ESP_LOGI(kTag, "joining hotspot (attempt %u/%u)",
           static_cast<unsigned>(attempts_),
           static_cast<unsigned>(kMaxAttempts));
  const esp_err_t result = esp_wifi_connect();
  if (result != ESP_OK) {
    ESP_LOGW(kTag, "esp_wifi_connect: %s", esp_err_to_name(result));
  }
}

void WifiLink::tear_down() {
  if (wifi_handler_ != nullptr) {
    esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                          wifi_handler_);
    wifi_handler_ = nullptr;
  }
  if (ip_handler_ != nullptr) {
    esp_event_handler_instance_unregister(IP_EVENT, ESP_EVENT_ANY_ID,
                                          ip_handler_);
    ip_handler_ = nullptr;
  }
  const bool was_up = driver_up_ || netif_ != nullptr;
  if (driver_up_) {
    esp_wifi_disconnect();
    esp_wifi_stop();
    esp_wifi_deinit();
    driver_up_ = false;
  }
  if (netif_ != nullptr) {
    esp_netif_destroy_default_wifi(netif_);
    netif_ = nullptr;
  }
  if (own_event_loop_) {
    esp_event_loop_delete_default();
    own_event_loop_ = false;
  }
  attempts_ = 0;
  retry_at_ms_ = 0;
  attempt_deadline_ms_ = 0;
  connected_at_ms_ = 0;
  // Events already queued by the driver belong to the link just closed.
  QueuedEvent stale;
  QueuedEvent keep[kQueueDepth];
  std::size_t kept = 0;
  while (xQueueReceive(queue_, &stale, 0) == pdTRUE) {
    const bool driver_event = stale.event == Event::StaStarted ||
                              stale.event == Event::StaDisconnected ||
                              stale.event == Event::GotIp ||
                              stale.event == Event::LostIp;
    if (!driver_event && kept < kQueueDepth) keep[kept++] = stale;
  }
  for (std::size_t i = 0; i < kept; ++i) xQueueSend(queue_, &keep[i], 0);
  if (was_up) log_heap("wifi after down");
}

void WifiLink::fail(WifiFailure failure) {
  ESP_LOGW(kTag, "wifi failed: %s", failure_name(failure));
  tear_down();
  set_state(WifiLinkState::Failed, failure);
  publish();
}

void WifiLink::set_state(WifiLinkState state, WifiFailure failure) {
  status_.state = state;
  status_.failure = state == WifiLinkState::Failed ? failure
                                                   : WifiFailure::None;
  if (state != WifiLinkState::Connected) {
    status_.ipv4 = {};
    status_.rssi_dbm = 0;
  }
  // The OTA worker polls this: it must see the address the moment the link
  // has one, and lose it the moment the radio goes back down.
  connected_.store(state == WifiLinkState::Connected);
}

void WifiLink::publish() {
  publish_at_ms_ = 0;
  last_publish_ms_ = now_ms();
  status_.flags = has_credentials() ? moto::ble::WifiStatusHasCredentials : 0;
  if (!phone_wants_status_.load() || sender_ == nullptr) return;
  sender_(moto::ble::Message{status_}, 0, sender_context_);
}

void WifiLink::log_heap(const char* label) const {
  // Same pool and fields as app_main's HEAPPROBE lines, so W0 numbers line up.
  ESP_LOGI(kTag, "%-16s internal free %u min %u largest %u", label,
           static_cast<unsigned>(
               heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
           static_cast<unsigned>(heap_caps_get_minimum_free_size(
               MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
           static_cast<unsigned>(heap_caps_get_largest_free_block(
               MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)));
}
