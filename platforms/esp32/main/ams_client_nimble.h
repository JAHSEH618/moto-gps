#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>

#include "ams_codec.hpp"
#include "moto/ble_protocol/ble_protocol.hpp"
#include "moto_nav_ui.h"

struct ble_gap_event;
struct ble_gatt_error;
struct ble_gatt_svc;
struct ble_gatt_chr;
struct ble_gatt_dsc;
struct ble_gatt_attr;
struct ble_npl_event;

// GATT client for the iPhone's Apple Media Service on the link the phone app
// already opened to BleNavTransport. The iPhone is the GATT client of the
// MOTO service and, at the same time, the GATT server of AMS; AMS needs the
// encrypted, bonded link the MOTO service already requires.
//
// Every method except handle_key() runs on the NimBLE host task:
// on_gap_event() is fed by BleNavTransport::gap_event, and the GATT callbacks
// and the retry callout are host-task callbacks too. handle_key() runs on the
// LVGL worker and only touches the atomics and the mutex-guarded projection.
class AmsClient {
 public:
  // `available` is false once AMS is gone (disconnect or never found); the
  // music page then shows the phone-relayed state again.
  using StateListener = void (*)(const moto::ble::MediaState& state,
                                 bool available,
                                 void* context);

  AmsClient();
  AmsClient(const AmsClient&) = delete;
  AmsClient& operator=(const AmsClient&) = delete;

  void set_listener(StateListener listener, void* context) noexcept;
  void on_gap_event(const ble_gap_event& event);

  // Returns true when AMS owns the music keys, whether or not the current
  // player accepts this one. False means "use the phone-relayed path".
  bool handle_key(moto_music_command_t key);

  // Adapters with the signatures BleNavTransport and PhoneNavBridge take.
  static void gap_observer(const ble_gap_event& event, void* context);
  static bool key_handler(moto_music_command_t key, void* context);

 private:
  static constexpr std::uint16_t kNoHandle = 0;
  static constexpr std::uint16_t kNoConnection = 0xFFFFU;
  static constexpr std::uint8_t kMaxDiscoveryAttempts = 6;

  enum class Stage : std::uint8_t {
    Idle,
    Service,
    Characteristics,
    EntityUpdateCccd,
    RemoteCommandCccd,
    SubscribeEntityUpdate,
    SubscribeRemoteCommand,
    RegisterPlayer,
    RegisterTrack,
    Ready,
  };

  static int on_service(std::uint16_t connection,
                        const ble_gatt_error* error,
                        const ble_gatt_svc* service, void* argument);
  static int on_characteristic(std::uint16_t connection,
                               const ble_gatt_error* error,
                               const ble_gatt_chr* characteristic,
                               void* argument);
  static int on_descriptor(std::uint16_t connection,
                           const ble_gatt_error* error,
                           std::uint16_t characteristic_value_handle,
                           const ble_gatt_dsc* descriptor, void* argument);
  static int on_write(std::uint16_t connection, const ble_gatt_error* error,
                      ble_gatt_attr* attribute, void* argument);
  static int on_command_written(std::uint16_t connection,
                                const ble_gatt_error* error,
                                ble_gatt_attr* attribute, void* argument);
  static void on_retry(struct ble_npl_event* event);

  void begin(std::uint16_t connection);
  void reset_link();
  void start_discovery();
  void schedule_retry();
  void discover_cccd(std::uint16_t value_handle, Stage stage);
  void advance();
  void fail(const char* step, int status);
  void receive_notification(std::uint16_t attribute_handle,
                            const std::uint8_t* data, std::size_t length);
  void publish(bool available);
  // End of the characteristic whose value handle is `value_handle`: the
  // handle before the next declaration, or the end of the service.
  std::uint16_t characteristic_end(std::uint16_t value_handle) const noexcept;

  StateListener listener_ = nullptr;
  void* listener_context_ = nullptr;
  bool retry_initialized_ = false;

  // Host task only.
  Stage stage_ = Stage::Idle;
  std::uint8_t attempts_ = 0;
  std::uint16_t service_start_ = kNoHandle;
  std::uint16_t service_end_ = kNoHandle;
  std::uint16_t entity_update_handle_ = kNoHandle;
  std::uint16_t entity_update_cccd_ = kNoHandle;
  std::uint16_t remote_command_cccd_ = kNoHandle;
  // Declaration handles of every characteristic in the service, to bound
  // each descriptor search. AMS defines three.
  static constexpr std::size_t kMaxCharacteristics = 8;
  std::uint16_t declarations_[kMaxCharacteristics]{};
  std::size_t declaration_count_ = 0;

  // Shared with handle_key() on the LVGL worker.
  std::atomic<std::uint16_t> connection_{kNoConnection};
  std::atomic<std::uint16_t> remote_command_handle_{kNoHandle};
  std::atomic<bool> remote_command_with_response_{true};
  std::atomic<bool> ready_{false};
  std::mutex now_playing_mutex_;
  moto::ams::NowPlaying now_playing_;
};
