#include "ams_client_nimble.h"

#include <cstring>

#include "esp_log.h"
#include "host/ble_gap.h"
#include "host/ble_gatt.h"
#include "host/ble_hs.h"
#include "host/ble_hs_mbuf.h"
#include "host/ble_uuid.h"
#include "nimble/nimble_npl.h"
#include "nimble/nimble_port.h"

namespace {
constexpr char kTag[] = "moto_ams";
constexpr std::uint16_t kCccdUuid = 0x2902;
// ATT MTU 185 bounds a notification to 182 bytes; leave room for a larger
// MTU an iPhone might negotiate in the future.
constexpr std::size_t kNotificationCapacity = 256;

// One client per firmware; the callout lives beside it, not in the header,
// so the header stays free of the NimBLE porting layer.
ble_npl_callout g_retry_callout;

ble_uuid128_t make_uuid(const std::uint8_t (&bytes)[16]) {
  ble_uuid128_t uuid{};
  uuid.u.type = BLE_UUID_TYPE_128;
  std::memcpy(uuid.value, bytes, sizeof(uuid.value));
  return uuid;
}

const ble_uuid128_t kServiceUuid = make_uuid(moto::ams::kServiceUuid);
const ble_uuid128_t kRemoteCommandUuid =
    make_uuid(moto::ams::kRemoteCommandUuid);
const ble_uuid128_t kEntityUpdateUuid =
    make_uuid(moto::ams::kEntityUpdateUuid);

bool is_cccd(const ble_gatt_dsc& descriptor) {
  return descriptor.uuid.u.type == BLE_UUID_TYPE_16 &&
         descriptor.uuid.u16.value == kCccdUuid;
}

const char* command_name(moto::ams::RemoteCommand command) {
  using moto::ams::RemoteCommand;
  switch (command) {
    case RemoteCommand::Play: return "play";
    case RemoteCommand::Pause: return "pause";
    case RemoteCommand::TogglePlayPause: return "toggle";
    case RemoteCommand::NextTrack: return "next";
    case RemoteCommand::PreviousTrack: return "previous";
    case RemoteCommand::SkipForward: return "skip+";
    case RemoteCommand::SkipBackward: return "skip-";
    case RemoteCommand::LikeTrack: return "like";
    default: return "other";
  }
}

}  // namespace

AmsClient::AmsClient() = default;

void AmsClient::set_listener(StateListener listener, void* context) noexcept {
  listener_ = listener;
  listener_context_ = context;
}

void AmsClient::gap_observer(const ble_gap_event& event, void* context) {
  static_cast<AmsClient*>(context)->on_gap_event(event);
}

bool AmsClient::key_handler(moto_music_command_t key, void* context) {
  return static_cast<AmsClient*>(context)->handle_key(key);
}

void AmsClient::on_gap_event(const ble_gap_event& event) {
  switch (event.type) {
    case BLE_GAP_EVENT_ENC_CHANGE: {
      // AMS is only exposed on an encrypted link; with a restored bond this
      // is the first moment the iPhone will answer AMS discovery.
      ble_gap_conn_desc description{};
      if (event.enc_change.status == 0 &&
          ble_gap_conn_find(event.enc_change.conn_handle, &description) == 0 &&
          description.sec_state.encrypted != 0) {
        begin(event.enc_change.conn_handle);
      }
      return;
    }
    case BLE_GAP_EVENT_DISCONNECT:
      reset_link();
      return;
    case BLE_GAP_EVENT_NOTIFY_RX: {
      if (event.notify_rx.conn_handle != connection_.load() ||
          event.notify_rx.om == nullptr) {
        return;
      }
      std::uint8_t bytes[kNotificationCapacity];
      std::uint16_t length = 0;
      if (ble_hs_mbuf_to_flat(event.notify_rx.om, bytes, sizeof(bytes),
                              &length) != 0) {
        return;
      }
      receive_notification(event.notify_rx.attr_handle, bytes, length);
      return;
    }
    default:
      return;
  }
}

void AmsClient::begin(std::uint16_t connection) {
  if (connection_.load() == connection && stage_ != Stage::Idle) {
    return;  // Re-encryption of a link that is already set up or in progress.
  }
  reset_link();
  connection_.store(connection);
  attempts_ = 0;
  start_discovery();
}

void AmsClient::reset_link() {
  const bool was_ready = ready_.exchange(false);
  if (retry_initialized_) {
    ble_npl_callout_stop(&g_retry_callout);
  }
  stage_ = Stage::Idle;
  connection_.store(kNoConnection);
  remote_command_handle_.store(kNoHandle);
  service_start_ = kNoHandle;
  service_end_ = kNoHandle;
  entity_update_handle_ = kNoHandle;
  entity_update_cccd_ = kNoHandle;
  remote_command_cccd_ = kNoHandle;
  remote_command_with_response_.store(true);
  declaration_count_ = 0;
  {
    const std::lock_guard<std::mutex> lock(now_playing_mutex_);
    now_playing_.reset();
  }
  if (was_ready) {
    publish(false);
  }
}

void AmsClient::start_discovery() {
  stage_ = Stage::Service;
  service_start_ = kNoHandle;
  service_end_ = kNoHandle;
  entity_update_handle_ = kNoHandle;
  entity_update_cccd_ = kNoHandle;
  remote_command_cccd_ = kNoHandle;
  remote_command_handle_.store(kNoHandle);
  declaration_count_ = 0;
  const int result = ble_gattc_disc_svc_by_uuid(
      connection_.load(), &kServiceUuid.u, on_service, this);
  if (result != 0) {
    fail("service discovery", result);
  }
}

void AmsClient::schedule_retry() {
  stage_ = Stage::Idle;
  if (connection_.load() == kNoConnection) {
    return;
  }
  ++attempts_;
  if (attempts_ >= kMaxDiscoveryAttempts) {
    ESP_LOGW(kTag,
             "AMS unavailable after %u attempts; music keys use the phone",
             static_cast<unsigned>(attempts_));
    return;
  }
  if (!retry_initialized_) {
    ble_npl_callout_init(&g_retry_callout, nimble_port_get_dflt_eventq(),
                         on_retry, this);
    retry_initialized_ = true;
  }
  // iOS can publish AMS a moment after the first pairing completes.
  ble_npl_callout_reset(&g_retry_callout,
                        ble_npl_time_ms_to_ticks32(2'000U * attempts_));
}

void AmsClient::on_retry(ble_npl_event* event) {
  auto* self = static_cast<AmsClient*>(ble_npl_event_get_arg(event));
  if (self != nullptr && self->connection_.load() != kNoConnection &&
      self->stage_ == Stage::Idle) {
    self->start_discovery();
  }
}

void AmsClient::fail(const char* step, int status) {
  ESP_LOGW(kTag, "%s failed: status=%d (attempt %u)", step, status,
           static_cast<unsigned>(attempts_ + 1U));
  schedule_retry();
}

int AmsClient::on_service(std::uint16_t connection,
                          const ble_gatt_error* error,
                          const ble_gatt_svc* service, void* argument) {
  auto* self = static_cast<AmsClient*>(argument);
  if (connection != self->connection_.load() ||
      self->stage_ != Stage::Service) {
    return BLE_HS_EDONE;
  }
  if (error->status == 0 && service != nullptr) {
    self->service_start_ = service->start_handle;
    self->service_end_ = service->end_handle;
    return 0;
  }
  if (error->status != BLE_HS_EDONE) {
    self->fail("service discovery", error->status);
    return 0;
  }
  if (self->service_start_ == kNoHandle) {
    ESP_LOGI(kTag, "iPhone has not published AMS yet");
    self->schedule_retry();
    return 0;
  }
  self->stage_ = Stage::Characteristics;
  const int result = ble_gattc_disc_all_chrs(
      connection, self->service_start_, self->service_end_,
      on_characteristic, self);
  if (result != 0) {
    self->fail("characteristic discovery", result);
  }
  return 0;
}

int AmsClient::on_characteristic(std::uint16_t connection,
                                 const ble_gatt_error* error,
                                 const ble_gatt_chr* characteristic,
                                 void* argument) {
  auto* self = static_cast<AmsClient*>(argument);
  if (connection != self->connection_.load() ||
      self->stage_ != Stage::Characteristics) {
    return BLE_HS_EDONE;
  }
  if (error->status == 0 && characteristic != nullptr) {
    if (self->declaration_count_ < kMaxCharacteristics) {
      self->declarations_[self->declaration_count_++] =
          characteristic->def_handle;
    }
    if (ble_uuid_cmp(&characteristic->uuid.u, &kEntityUpdateUuid.u) == 0) {
      self->entity_update_handle_ = characteristic->val_handle;
    } else if (ble_uuid_cmp(&characteristic->uuid.u,
                            &kRemoteCommandUuid.u) == 0) {
      self->remote_command_handle_.store(characteristic->val_handle);
      self->remote_command_with_response_.store(
          (characteristic->properties & BLE_GATT_CHR_PROP_WRITE) != 0U);
    }
    return 0;
  }
  if (error->status != BLE_HS_EDONE) {
    self->fail("characteristic discovery", error->status);
    return 0;
  }
  if (self->entity_update_handle_ == kNoHandle) {
    self->fail("Entity Update lookup", BLE_HS_ENOENT);
    return 0;
  }
  self->discover_cccd(self->entity_update_handle_, Stage::EntityUpdateCccd);
  return 0;
}

std::uint16_t AmsClient::characteristic_end(
    std::uint16_t value_handle) const noexcept {
  std::uint16_t end = service_end_;
  for (std::size_t index = 0; index < declaration_count_; ++index) {
    const std::uint16_t declaration = declarations_[index];
    if (declaration > value_handle && declaration - 1U < end) {
      end = static_cast<std::uint16_t>(declaration - 1U);
    }
  }
  return end;
}

void AmsClient::discover_cccd(std::uint16_t value_handle, Stage stage) {
  stage_ = stage;
  const std::uint16_t end = characteristic_end(value_handle);
  if (end <= value_handle) {
    advance();  // No descriptors; advance() reports the missing CCCD.
    return;
  }
  const int result = ble_gattc_disc_all_dscs(connection_.load(), value_handle,
                                             end, on_descriptor, this);
  if (result != 0) {
    fail("descriptor discovery", result);
  }
}

int AmsClient::on_descriptor(std::uint16_t connection,
                             const ble_gatt_error* error, std::uint16_t,
                             const ble_gatt_dsc* descriptor, void* argument) {
  auto* self = static_cast<AmsClient*>(argument);
  if (connection != self->connection_.load() ||
      (self->stage_ != Stage::EntityUpdateCccd &&
       self->stage_ != Stage::RemoteCommandCccd)) {
    return BLE_HS_EDONE;
  }
  if (error->status == 0 && descriptor != nullptr) {
    if (is_cccd(*descriptor)) {
      if (self->stage_ == Stage::EntityUpdateCccd) {
        self->entity_update_cccd_ = descriptor->handle;
      } else {
        self->remote_command_cccd_ = descriptor->handle;
      }
    }
    return 0;
  }
  if (error->status != BLE_HS_EDONE) {
    self->fail("descriptor discovery", error->status);
    return 0;
  }
  self->advance();
  return 0;
}

int AmsClient::on_write(std::uint16_t connection, const ble_gatt_error* error,
                        ble_gatt_attr*, void* argument) {
  auto* self = static_cast<AmsClient*>(argument);
  if (connection != self->connection_.load()) {
    return 0;
  }
  if (error->status != 0) {
    // 0x1A0 is AMS InvalidState: Entity Update written before subscribing.
    self->fail("AMS setup write", error->status);
    return 0;
  }
  self->advance();
  return 0;
}

void AmsClient::advance() {
  static constexpr std::uint8_t kEnableNotifications[] = {0x01, 0x00};
  const std::uint16_t connection = connection_.load();
  int result = 0;
  switch (stage_) {
    case Stage::EntityUpdateCccd:
      if (entity_update_cccd_ == kNoHandle) {
        fail("Entity Update CCCD lookup", BLE_HS_ENOENT);
        return;
      }
      if (remote_command_handle_.load() != kNoHandle) {
        discover_cccd(remote_command_handle_.load(), Stage::RemoteCommandCccd);
        return;
      }
      [[fallthrough]];
    case Stage::RemoteCommandCccd:
      // The spec requires the Entity Update subscription before any write
      // to Entity Update; the Remote Command one is optional.
      stage_ = Stage::SubscribeEntityUpdate;
      result = ble_gattc_write_flat(connection, entity_update_cccd_,
                                    kEnableNotifications,
                                    sizeof(kEnableNotifications), on_write,
                                    this);
      break;
    case Stage::SubscribeEntityUpdate:
      if (remote_command_cccd_ != kNoHandle) {
        stage_ = Stage::SubscribeRemoteCommand;
        result = ble_gattc_write_flat(connection, remote_command_cccd_,
                                      kEnableNotifications,
                                      sizeof(kEnableNotifications), on_write,
                                      this);
        break;
      }
      [[fallthrough]];
    case Stage::SubscribeRemoteCommand: {
      stage_ = Stage::RegisterPlayer;
      const std::vector<std::uint8_t> payload =
          moto::ams::player_registration();
      result = ble_gattc_write_flat(connection, entity_update_handle_,
                                    payload.data(),
                                    static_cast<std::uint16_t>(payload.size()),
                                    on_write, this);
      break;
    }
    case Stage::RegisterPlayer: {
      stage_ = Stage::RegisterTrack;
      const std::vector<std::uint8_t> payload =
          moto::ams::track_registration();
      result = ble_gattc_write_flat(connection, entity_update_handle_,
                                    payload.data(),
                                    static_cast<std::uint16_t>(payload.size()),
                                    on_write, this);
      break;
    }
    case Stage::RegisterTrack:
      stage_ = Stage::Ready;
      attempts_ = 0;
      ready_.store(true);
      ESP_LOGI(kTag, "AMS ready; music page follows the iPhone Now Playing app");
      publish(true);
      return;
    default:
      return;
  }
  if (result != 0) {
    fail("AMS setup write", result);
  }
}

void AmsClient::receive_notification(std::uint16_t attribute_handle,
                                     const std::uint8_t* data,
                                     std::size_t length) {
  bool changed = false;
  if (attribute_handle == entity_update_handle_ &&
      entity_update_handle_ != kNoHandle) {
    moto::ams::EntityUpdate update;
    if (!moto::ams::parse_entity_update(data, length, update)) {
      ESP_LOGW(kTag, "malformed Entity Update (%u bytes)",
               static_cast<unsigned>(length));
      return;
    }
    const std::lock_guard<std::mutex> lock(now_playing_mutex_);
    changed = now_playing_.apply(update);
  } else if (attribute_handle == remote_command_handle_.load() &&
             attribute_handle != kNoHandle) {
    const moto::ams::CommandSet commands =
        moto::ams::parse_remote_command_notification(data, length);
    const std::lock_guard<std::mutex> lock(now_playing_mutex_);
    changed = now_playing_.set_supported_commands(commands);
  } else {
    return;
  }
  // Values that arrive while the registrations are still being written are
  // kept and shown by the Ready publish.
  if (changed && ready_.load()) {
    publish(true);
  }
}

void AmsClient::publish(bool available) {
  if (listener_ == nullptr) {
    return;
  }
  moto::ble::MediaState state;
  {
    const std::lock_guard<std::mutex> lock(now_playing_mutex_);
    state = now_playing_.media_state();
  }
  // Never hold the projection lock across the listener: it takes the
  // bridge's state lock, and the bridge calls handle_key() without it.
  listener_(state, available, listener_context_);
}

bool AmsClient::handle_key(moto_music_command_t key) {
  const std::uint16_t connection = connection_.load();
  const std::uint16_t handle = remote_command_handle_.load();
  // AMS may publish Entity Update without Remote Command; the keys then stay
  // on the phone-relayed path while the screen still shows AMS state.
  if (!ready_.load() || connection == kNoConnection || handle == kNoHandle) {
    return false;
  }
  moto::ams::RemoteCommand command{};
  bool accepted = false;
  {
    const std::lock_guard<std::mutex> lock(now_playing_mutex_);
    accepted = now_playing_.command_for(key, command);
  }
  if (!accepted) {
    ESP_LOGI(kTag, "Now Playing app does not accept key %d",
             static_cast<int>(key));
    return true;
  }
  const std::uint8_t byte = static_cast<std::uint8_t>(command);
  const int result =
      remote_command_with_response_.load()
          ? ble_gattc_write_flat(connection, handle, &byte, sizeof(byte),
                                 on_command_written, nullptr)
          : ble_gattc_write_no_rsp_flat(connection, handle, &byte,
                                        sizeof(byte));
  if (result != 0) {
    ESP_LOGW(kTag, "remote command %s not sent: rc=%d", command_name(command),
             result);
  } else {
    ESP_LOGI(kTag, "remote command %s", command_name(command));
  }
  return true;
}

int AmsClient::on_command_written(std::uint16_t, const ble_gatt_error* error,
                                  ble_gatt_attr*, void*) {
  if (error->status != 0) {
    ESP_LOGW(kTag, "remote command rejected: status=%d", error->status);
  }
  return 0;
}
