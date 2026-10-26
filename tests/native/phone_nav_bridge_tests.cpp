#include "phone_nav_bridge.h"
#include "phone_nav_bridge_test_probe.hpp"
#include "demo_fixture/jinan_big_data_demo.hpp"

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

namespace {

int failures = 0;

void pump(PhoneNavBridge& bridge) {
  bridge.render_pending_for_test();
}

#define CHECK(condition)                                                   \
  do {                                                                     \
    if (!(condition)) {                                                    \
      std::cerr << __FILE__ << ':' << __LINE__                             \
                << " CHECK failed: " #condition << '\n';                  \
      ++failures;                                                          \
    }                                                                      \
  } while (false)

moto::ble::ReassembledMessage make_message(
    const moto::ble::Message& message,
    std::uint16_t sequence) {
  const moto::ble::BytesResult encoded = moto::ble::encode_message(message);
  if (!encoded.ok()) {
    std::cerr << "Could not encode bridge test message\n";
    ++failures;
    return {};
  }
  moto::ble::ReassembledMessage result;
  result.type = moto::ble::message_type(message);
  result.sequence = sequence;
  result.payload = encoded.value;
  return result;
}

struct ReentrantSenderContext {
  PhoneNavBridge* bridge = nullptr;
  int calls = 0;
  moto::ble::DisplayPage last_page = moto::ble::DisplayPage::Navigation;
};

bool reentrant_sender(const moto::ble::Message& message,
                      std::uint8_t,
                      void* context) {
  auto* sender = static_cast<ReentrantSenderContext*>(context);
  const auto* command = std::get_if<moto::ble::DeviceCommand>(&message);
  if (command == nullptr) {
    return false;
  }
  ++sender->calls;
  sender->last_page = command->page;

  // A real transport can synchronously reject a send or cause a link callback.
  // This re-entry deadlocks if PhoneNavBridge keeps state_mutex_ while sending.
  sender->bridge->on_link_state(false);
  return true;
}

struct MusicSenderContext {
  std::vector<moto::ble::DeviceCommand> commands;
  std::vector<std::uint8_t> flags;
};

bool record_music_sender(const moto::ble::Message& message,
                         std::uint8_t flags,
                         void* context) {
  auto* sender = static_cast<MusicSenderContext*>(context);
  const auto* command = std::get_if<moto::ble::DeviceCommand>(&message);
  if (command == nullptr) return false;
  sender->commands.push_back(*command);
  sender->flags.push_back(flags);
  return true;
}

void test_sender_is_called_without_bridge_state_lock() {  moto::nav::NavPresenter presenter;
  PhoneNavBridge bridge(presenter);
  ReentrantSenderContext sender{&bridge};
  bridge.set_sender(reentrant_sender, &sender);
  bridge.install_ui_callbacks();
  bridge.on_link_state(true);

  moto::test::emit_page_change(MOTO_UI_PAGE_SPEED);
  CHECK(sender.calls == 1);
  CHECK(sender.last_page == moto::ble::DisplayPage::Speed);
}

struct SettingsSenderContext {
  std::vector<moto::ble::SettingsState> states;
  std::vector<std::uint8_t> flags;
};

bool record_settings_sender(const moto::ble::Message& message,
                            std::uint8_t flags,
                            void* context) {
  auto* sender = static_cast<SettingsSenderContext*>(context);
  const auto* state = std::get_if<moto::ble::SettingsState>(&message);
  if (state == nullptr) return false;
  sender->states.push_back(*state);
  sender->flags.push_back(flags);
  return true;
}

moto::device::DeviceSettings stored_settings{};
int stored_settings_count = 0;

bool record_settings_store(const moto::device::DeviceSettings& settings) {
  stored_settings = settings;
  ++stored_settings_count;
  return true;
}

moto::ble::SettingsState settings_request(moto::ble::SettingsTheme theme,
                                          std::uint32_t revision) {
  moto::ble::SettingsState request;
  request.theme = theme;
  request.brightness_percent = 60;
  request.units = moto::ble::SettingsUnits::Mph;
  request.lean_full_scale_deg = 45;
  request.performance_target_kph = 100;
  request.visible_pages = moto::ble::kSettingsAllPages;
  request.lean_zero_offset_cdeg = 0;
  request.ambient_night = false;
  request.show_buildings = true;
  request.revision = revision;
  return request;
}

void test_settings_apply_echo_and_persist() {
  moto::test::reset_phone_nav_bridge_probe();
  stored_settings = {};
  stored_settings_count = 0;
  moto::nav::NavPresenter presenter;
  PhoneNavBridge bridge(presenter);
  SettingsSenderContext sender;
  bridge.set_sender(record_settings_sender, &sender);
  bridge.set_settings_store(record_settings_store);
  bridge.on_link_state(true);

  const moto::ble::SettingsState request =
      settings_request(moto::ble::SettingsTheme::Night, 1);
  CHECK(bridge.on_message(make_message(request, 1)) ==
        moto::ble::AckStatus::Ok);
  // Applied, persisted, and echoed with the same revision so the phone can
  // stop retrying.
  CHECK(stored_settings_count == 1);
  CHECK(stored_settings.show_buildings);
  CHECK(sender.states.size() == 1);
  CHECK(sender.states[0].revision == 1);
  CHECK(sender.states[0].theme == moto::ble::SettingsTheme::Night);
  CHECK(sender.states[0].show_buildings);
  // Settings are a display change, not an urgent one.
  CHECK(sender.flags.size() == 1 && sender.flags[0] == 0);

  // LVGL writes belong to the render task, never to the BLE callback.
  CHECK(moto::test::phone_nav_bridge_theme_apply_count() == 0);
  pump(bridge);
  CHECK(moto::test::phone_nav_bridge_last_theme() ==
        MOTO_UI_THEME_GTA_NIGHT);
  CHECK(moto::test::phone_nav_bridge_last_show_buildings() == 1);

  // An equal revision is a replay: no re-apply, no re-store, no echo.
  sender.states.clear();
  CHECK(bridge.on_message(make_message(request, 2)) ==
        moto::ble::AckStatus::Ok);
  CHECK(stored_settings_count == 1);
  CHECK(sender.states.empty());

  // AUTO is resolved on the device from the phone's ambient flag, and the
  // phone does not get to pick a palette for it.
  const moto::ble::SettingsState auto_night =
      settings_request(moto::ble::SettingsTheme::Auto, 2);
  auto auto_night_with_flag = auto_night;
  auto_night_with_flag.ambient_night = true;
  CHECK(bridge.on_message(make_message(auto_night_with_flag, 3)) ==
        moto::ble::AckStatus::Ok);
  pump(bridge);
  CHECK(moto::test::phone_nav_bridge_last_ambient_night() == 1);
  CHECK(moto::test::phone_nav_bridge_last_theme() == MOTO_UI_THEME_AUTO);

  // A higher revision carrying the same values still echoes (the phone may be
  // confirming after a reconnect) but must not rewrite NVS.
  auto same_values = auto_night_with_flag;
  same_values.revision = 3;
  CHECK(bridge.on_message(make_message(same_values, 4)) ==
        moto::ble::AckStatus::Ok);
  CHECK(stored_settings_count == 2);
  pump(bridge);
  CHECK(moto::test::phone_nav_bridge_theme_apply_count() >= 2);
  CHECK(moto::test::phone_nav_bridge_last_theme() == MOTO_UI_THEME_AUTO);
}

// Boot replay: the stored settings must reach the UI before the phone says
// anything, because the tree reset clears the theme and the building layer.
void test_boot_settings_replay_reaches_the_ui() {
  moto::test::reset_phone_nav_bridge_probe();
  moto::nav::NavPresenter presenter;
  PhoneNavBridge bridge(presenter);

  moto::device::DeviceSettings stored;
  stored.theme = moto::device::DeviceThemeMode::Day;
  stored.show_buildings = true;
  bridge.apply_settings(stored, false);

  CHECK(moto::test::phone_nav_bridge_last_theme() == MOTO_UI_THEME_GTA_DAY);
  CHECK(moto::test::phone_nav_bridge_last_ambient_night() == 0);
  CHECK(moto::test::phone_nav_bridge_last_show_buildings() == 1);

  // Nothing to echo: no link has ever supplied a revision.
  SettingsSenderContext sender;
  bridge.set_sender(record_settings_sender, &sender);
  bridge.on_link_state(true);
  bridge.send_settings_state();
  CHECK(sender.states.empty());
}

void test_home_shortcut_sends_the_intent() {
  moto::nav::NavPresenter presenter;
  PhoneNavBridge bridge(presenter);
  MusicSenderContext sender;
  bridge.set_sender(record_music_sender, &sender);
  bridge.install_ui_callbacks();
  bridge.on_link_state(true);

  moto::test::emit_home_shortcut();
  CHECK(sender.commands.size() == 1);
  if (sender.commands.size() == 1) {
    CHECK(sender.commands[0].kind ==
          moto::ble::DeviceCommandKind::HomeShortcut);
    CHECK(sender.commands[0].page == moto::ble::DisplayPage::Navigation);
    CHECK(sender.commands[0].x == moto::ble::kUnknownTouchCoordinate);
    CHECK(sender.commands[0].y == moto::ble::kUnknownTouchCoordinate);
    CHECK(sender.flags[0] ==
          (moto::ble::AckRequested | moto::ble::Urgent));
  }

  // With no link the tap must not leave a queued "go home" to fire later.
  bridge.on_link_state(false);
  moto::test::emit_home_shortcut();
  CHECK(sender.commands.size() == 1);
}

void test_refused_home_tap_reaches_the_screen() {
  moto::test::reset_phone_nav_bridge_probe();
  moto::nav::NavPresenter presenter;
  PhoneNavBridge bridge(presenter);
  MusicSenderContext sender;
  bridge.set_sender(record_music_sender, &sender);
  bridge.install_ui_callbacks();
  bridge.on_link_state(true);

  moto::test::emit_home_shortcut();
  CHECK(sender.commands.size() == 1);
  if (sender.commands.empty()) return;
  const std::uint16_t home_command = sender.commands[0].command_id;
  CHECK(home_command != 0);

  // An Ack for someone else's command must not put a hint on screen.
  moto::ble::Ack unrelated;
  unrelated.acknowledged_sequence = 12;
  unrelated.status = moto::ble::AckStatus::InvalidState;
  unrelated.command_id = static_cast<std::uint16_t>(home_command + 1);
  CHECK(bridge.on_message(make_message(unrelated, 12)) ==
        moto::ble::AckStatus::Ok);
  bridge.render_pending_for_test();
  CHECK(moto::test::phone_nav_bridge_home_error_count() == 0);

  // The phone has no home. The device cannot know, so the refusal has to be
  // visible: a tap that silently does nothing is indistinguishable from a bug.
  moto::ble::Ack refused;
  refused.acknowledged_sequence = 13;
  refused.status = moto::ble::AckStatus::InvalidState;
  refused.command_id = home_command;
  CHECK(bridge.on_message(make_message(refused, 13)) ==
        moto::ble::AckStatus::Ok);
  bridge.render_pending_for_test();
  CHECK(moto::test::phone_nav_bridge_home_error_count() == 1);

  // A repeated Ack for the same command is not a new failure.
  CHECK(bridge.on_message(make_message(refused, 14)) ==
        moto::ble::AckStatus::Ok);
  bridge.render_pending_for_test();
  CHECK(moto::test::phone_nav_bridge_home_error_count() == 1);

  // A successful tap produces no hint at all.
  moto::test::emit_home_shortcut();
  CHECK(sender.commands.size() == 2);
  moto::ble::Ack accepted;
  accepted.acknowledged_sequence = 15;
  accepted.status = moto::ble::AckStatus::Ok;
  accepted.command_id = sender.commands[1].command_id;
  CHECK(bridge.on_message(make_message(accepted, 15)) ==
        moto::ble::AckStatus::Ok);
  bridge.render_pending_for_test();
  CHECK(moto::test::phone_nav_bridge_home_error_count() == 1);
}

void test_navigation_and_touch_updates_are_serialized() {
  moto::test::reset_phone_nav_bridge_probe();
  moto::nav::NavPresenter presenter;
  PhoneNavBridge bridge(presenter);
  bridge.install_ui_callbacks();
  bridge.on_link_state(true);

  constexpr int kIterations = 2'000;
  std::atomic<bool> begin{false};
  std::thread phone([&]() {
    while (!begin.load()) {
      std::this_thread::yield();
    }
    for (int index = 0; index < kIterations; ++index) {
      moto::ble::NavigationSnapshot snapshot;
      snapshot.state = moto::ble::NavigationState::Navigating;
      snapshot.network = moto::ble::NetworkState::Online;
      snapshot.flags = moto::ble::NavigationHasNextManeuver;
      snapshot.maneuver = (index % 2 == 0)
                              ? moto::ble::Maneuver::Left
                              : moto::ble::Maneuver::Right;
      snapshot.route_token = 17;
      snapshot.route_generation = 3;
      snapshot.road_name = "并发道路-" + std::to_string(index) +
                           std::string(32, static_cast<char>('a' + index % 26));
      snapshot.distance_to_maneuver_m = static_cast<std::uint32_t>(index);
      const auto message = make_message(
          moto::ble::Message{snapshot},
          static_cast<std::uint16_t>(index % 65'535 + 1));
      CHECK(bridge.on_message(message) == moto::ble::AckStatus::Ok);
    }
  });

  std::thread touch([&]() {
    while (!begin.load()) {
      std::this_thread::yield();
    }
    for (int index = 0; index < kIterations; ++index) {
      moto::test::emit_page_change(
          index % 2 == 0 ? MOTO_UI_PAGE_SPEED : MOTO_UI_PAGE_COMPASS);
    }
  });

  begin.store(true);
  phone.join();
  touch.join();

  // Thousands of producer updates collapse into the latest complete state;
  // none of the producer threads enters LVGL.
  CHECK(moto::test::phone_nav_bridge_apply_count() == 0);
  CHECK(moto::test::phone_nav_bridge_board_lock_count() == 0);
  pump(bridge);
  CHECK(moto::test::phone_nav_bridge_apply_count() == 1);
  const std::string final_name = moto::test::phone_nav_bridge_last_road_name();
  CHECK(final_name.rfind("并发道路-", 0) == 0);
}

void test_ble_submission_never_waits_for_lvgl_and_retries_latest_state() {
  moto::test::reset_phone_nav_bridge_probe();
  moto::nav::NavPresenter presenter;
  PhoneNavBridge bridge(presenter);

  moto::ble::NavigationSnapshot first;
  first.state = moto::ble::NavigationState::Navigating;
  first.network = moto::ble::NetworkState::Online;
  first.flags = moto::ble::NavigationHasNextManeuver;
  first.route_token = 29;
  first.route_generation = 1;
  first.maneuver = moto::ble::Maneuver::Left;
  first.road_name = "旧状态";

  moto::test::phone_nav_bridge_set_board_lock_available(false);
  CHECK(bridge.on_message(make_message(first, 1)) ==
        moto::ble::AckStatus::Ok);
  CHECK(moto::test::phone_nav_bridge_board_lock_count() == 0);
  CHECK(moto::test::phone_nav_bridge_apply_count() == 0);

  // Only the render consumer attempts LVGL. Failure keeps the dirty bit and
  // the producer remains completely decoupled from that unavailable lock.
  pump(bridge);
  CHECK(moto::test::phone_nav_bridge_board_lock_count() == 1);
  CHECK(moto::test::phone_nav_bridge_apply_count() == 0);

  auto latest = first;
  latest.maneuver = moto::ble::Maneuver::Right;
  latest.road_name = "保留的最新状态";
  CHECK(bridge.on_message(make_message(latest, 2)) ==
        moto::ble::AckStatus::Ok);
  CHECK(moto::test::phone_nav_bridge_board_lock_count() == 1);

  moto::test::phone_nav_bridge_set_board_lock_available(true);
  pump(bridge);
  CHECK(moto::test::phone_nav_bridge_apply_count() == 1);
  CHECK(moto::test::phone_nav_bridge_last_road_name() == "保留的最新状态");
  CHECK(moto::test::phone_nav_bridge_last_maneuver() ==
        MOTO_MANEUVER_RIGHT);
}

void test_phone_connection_lifecycle_reaches_the_ui() {
  moto::test::reset_phone_nav_bridge_probe();
  moto::nav::NavPresenter presenter;
  PhoneNavBridge bridge(presenter);

  bridge.on_link_state(true);
  pump(bridge);
  CHECK(moto::test::phone_nav_bridge_last_phone_connection() ==
        MOTO_UI_PHONE_CONNECTING);

  moto::ble::ConnectionStatus starting;
  starting.role = moto::ble::EndpointRole::Phone;
  starting.state = moto::ble::ConnectionState::Starting;
  starting.minimum_version = moto::ble::kProtocolVersion;
  starting.maximum_version = moto::ble::kProtocolVersion;
  starting.session_id = 1234;
  CHECK(bridge.on_message(make_message(starting, 10)) ==
        moto::ble::AckStatus::Ok);
  pump(bridge);
  CHECK(moto::test::phone_nav_bridge_last_phone_connection() ==
        MOTO_UI_PHONE_CONNECTING);

  starting.state = moto::ble::ConnectionState::Ready;
  CHECK(bridge.on_message(make_message(starting, 11)) ==
        moto::ble::AckStatus::Ok);
  pump(bridge);
  CHECK(moto::test::phone_nav_bridge_last_phone_connection() ==
        MOTO_UI_PHONE_ONLINE);

  bridge.on_link_state(false);
  pump(bridge);
  CHECK(moto::test::phone_nav_bridge_last_phone_connection() ==
        MOTO_UI_PHONE_OFFLINE);
}

void test_music_capability_state_and_touch_commands() {
  moto::test::reset_phone_nav_bridge_probe();
  moto::nav::NavPresenter presenter;
  PhoneNavBridge bridge(presenter);
  MusicSenderContext sender;
  bridge.set_sender(record_music_sender, &sender);
  bridge.install_ui_callbacks();
  bridge.on_link_state(true);

  moto::ble::ConnectionStatus ready;
  ready.role = moto::ble::EndpointRole::Phone;
  ready.state = moto::ble::ConnectionState::Starting;
  ready.minimum_version = moto::ble::kProtocolVersion;
  ready.maximum_version = moto::ble::kProtocolVersion;
  ready.capabilities = moto::ble::CapabilityMediaState |
                       moto::ble::CapabilityMusicCommands |
                       moto::ble::CapabilityCommandAck;
  ready.session_id = 4321;
  CHECK(bridge.on_message(make_message(ready, 20)) ==
        moto::ble::AckStatus::Ok);
  pump(bridge);
  ready.state = moto::ble::ConnectionState::Ready;
  CHECK(bridge.on_message(make_message(ready, 21)) ==
        moto::ble::AckStatus::Ok);
  pump(bridge);

  moto::ble::MediaState media;
  media.flags = moto::ble::MediaConnected |
                moto::ble::MediaPlaying |
                moto::ble::MediaLikeAvailable;
  media.track_token = 88;
  media.position_s = 12;
  media.duration_s = 240;
  media.source_name = "APPLE MUSIC";
  media.track_title = "夜间骑行";
  media.artist_name = "MOTO GPS";
  CHECK(bridge.on_message(make_message(media, 22)) ==
        moto::ble::AckStatus::Ok);
  pump(bridge);
  CHECK(moto::test::phone_nav_bridge_media_connected());
  CHECK(moto::test::phone_nav_bridge_media_playing());
  CHECK(moto::test::phone_nav_bridge_media_like_available());
  CHECK(moto::test::phone_nav_bridge_media_source() == "APPLE MUSIC");
  CHECK(moto::test::phone_nav_bridge_media_title() == "夜间骑行");
  CHECK(moto::test::phone_nav_bridge_media_artist() == "MOTO GPS");

  moto::test::emit_music_command(MOTO_MUSIC_PREVIOUS);
  moto::test::emit_music_command(MOTO_MUSIC_TOGGLE_PLAYBACK);
  moto::test::emit_music_command(MOTO_MUSIC_NEXT);
  moto::test::emit_music_command(MOTO_MUSIC_LIKE);
  CHECK(sender.commands.size() == 4);
  const moto::ble::DeviceCommandKind expected[] = {
      moto::ble::DeviceCommandKind::MusicPrevious,
      moto::ble::DeviceCommandKind::MusicTogglePlayback,
      moto::ble::DeviceCommandKind::MusicNext,
      moto::ble::DeviceCommandKind::MusicLike,
  };
  for (std::size_t index = 0; index < sender.commands.size(); ++index) {
    CHECK(sender.commands[index].kind == expected[index]);
    CHECK(sender.commands[index].page == moto::ble::DisplayPage::Music);
    CHECK(sender.commands[index].command_id == index + 1);
    CHECK((sender.flags[index] & moto::ble::AckRequested) != 0U);
    CHECK((sender.flags[index] & moto::ble::Urgent) != 0U);
  }
}

struct MusicKeyRecorder {
  bool owns_keys = true;
  std::vector<moto_music_command_t> keys;
};

bool record_music_key(moto_music_command_t key, void* context) {
  auto* recorder = static_cast<MusicKeyRecorder*>(context);
  recorder->keys.push_back(key);
  return recorder->owns_keys;
}

// AMS (the iPhone's Now Playing, any app) replaces the phone app's Apple Music
// relay while it is available, and the relay comes back when it is not.
void test_system_media_takes_over_the_music_page_and_falls_back() {
  moto::test::reset_phone_nav_bridge_probe();
  moto::nav::NavPresenter presenter;
  PhoneNavBridge bridge(presenter);
  MusicSenderContext sender;
  MusicKeyRecorder keys;
  bridge.set_sender(record_music_sender, &sender);
  bridge.set_music_key_handler(record_music_key, &keys);
  bridge.install_ui_callbacks();
  bridge.on_link_state(true);

  moto::ble::MediaState relay;
  relay.flags = moto::ble::MediaConnected;
  relay.source_name = "APPLE MUSIC";
  relay.track_title = "尚未播放";
  CHECK(bridge.on_message(make_message(relay, 30)) ==
        moto::ble::AckStatus::Ok);
  pump(bridge);
  CHECK(moto::test::phone_nav_bridge_media_source() == "APPLE MUSIC");

  moto::ble::MediaState system;
  system.flags = moto::ble::MediaConnected | moto::ble::MediaPlaying;
  system.source_name = "NETEASE";
  system.track_title = "晴天";
  system.artist_name = "周杰伦";
  bridge.on_system_media(system, true);
  pump(bridge);
  CHECK(moto::test::phone_nav_bridge_media_source() == "NETEASE");
  CHECK(moto::test::phone_nav_bridge_media_title() == "晴天");
  CHECK(moto::test::phone_nav_bridge_media_artist() == "周杰伦");
  CHECK(moto::test::phone_nav_bridge_media_playing());

  // A relay update must not repaint the page over the Now Playing app.
  CHECK(bridge.on_message(make_message(relay, 31)) ==
        moto::ble::AckStatus::Ok);
  pump(bridge);
  CHECK(moto::test::phone_nav_bridge_media_source() == "NETEASE");

  moto::test::emit_music_command(MOTO_MUSIC_NEXT);
  moto::test::emit_music_command(MOTO_MUSIC_TOGGLE_PLAYBACK);
  CHECK(keys.keys.size() == 2);
  CHECK(keys.keys[0] == MOTO_MUSIC_NEXT);
  CHECK(keys.keys[1] == MOTO_MUSIC_TOGGLE_PLAYBACK);
  CHECK(sender.commands.empty());

  // The protocol link goes stale while BLE (and so AMS) stays up: the page
  // keeps following the phone's Now Playing.
  bridge.on_link_state(false);
  pump(bridge);
  CHECK(moto::test::phone_nav_bridge_media_source() == "NETEASE");
  bridge.on_link_state(true);

  // A key AMS cannot carry goes over the relay.
  keys.owns_keys = false;
  moto::test::emit_music_command(MOTO_MUSIC_PREVIOUS);
  CHECK(keys.keys.size() == 3);
  CHECK(sender.commands.size() == 1);
  if (!sender.commands.empty()) {
    CHECK(sender.commands[0].kind ==
          moto::ble::DeviceCommandKind::MusicPrevious);
  }

  // AMS gone: the relay's newest state is shown again.
  CHECK(bridge.on_message(make_message(relay, 32)) ==
        moto::ble::AckStatus::Ok);
  bridge.on_system_media(system, false);
  pump(bridge);
  CHECK(moto::test::phone_nav_bridge_media_source() == "APPLE MUSIC");
  CHECK(moto::test::phone_nav_bridge_media_title() == "尚未播放");
}

void test_demo_uses_the_production_presenter_path() {
  moto::test::reset_phone_nav_bridge_probe();
  moto::nav::NavPresenter presenter;
  PhoneNavBridge bridge(presenter);
  bridge.install_ui_callbacks();

  moto::test::emit_demo_change(true);
  pump(bridge);
  const auto now_ms = static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::steady_clock::now().time_since_epoch()).count());

  // Approach and cross the first junction in the real OSM route. The symbol
  // and distance switch together: right into 新泺大街, then left into 崇华路.
  // Distances come from cumulative route geometry, not UI timer thresholds.
  bridge.update_demo(now_ms + 6'200);
  pump(bridge);
  CHECK(moto::test::phone_nav_bridge_last_maneuver() ==
        MOTO_MANEUVER_RIGHT);
  CHECK(moto::test::phone_nav_bridge_last_distance_to_maneuver_m() > 3);
  CHECK(moto::test::phone_nav_bridge_last_distance_to_maneuver_m() < 8);
  bridge.update_demo(now_ms + 7'000);
  pump(bridge);
  CHECK(moto::test::phone_nav_bridge_last_maneuver() ==
        MOTO_MANEUVER_LEFT);
  CHECK(moto::test::phone_nav_bridge_last_distance_to_maneuver_m() > 135);
  CHECK(moto::test::phone_nav_bridge_last_distance_to_maneuver_m() < 141);

  bridge.update_demo(now_ms + 12'000);
  pump(bridge);

  CHECK(moto::test::phone_nav_bridge_last_demo_active());
  CHECK(moto::test::phone_nav_bridge_last_route_point_count() >= 2);
  CHECK(moto::test::phone_nav_bridge_last_road_point_count() ==
        moto::demo::jinan_big_data::kRoadPoints.size());
  CHECK(moto::test::phone_nav_bridge_last_road_polyline_count() ==
        moto::demo::jinan_big_data::kRoads.size());
  CHECK(moto::test::phone_nav_bridge_apply_count() >= 2);

  bridge.update_demo(now_ms + 6'200);
  pump(bridge);
  const uint16_t heading_before =
      moto::test::phone_nav_bridge_last_heading_deg();
  bridge.update_demo(now_ms + 8'500);
  pump(bridge);
  const uint16_t heading_after =
      moto::test::phone_nav_bridge_last_heading_deg();
  CHECK(heading_before != heading_after);

  moto::test::emit_demo_change(false);
  pump(bridge);
  CHECK(!moto::test::phone_nav_bridge_last_demo_active());
}

void test_ios_demo_token_attaches_context_and_live_route_clears_it() {
  moto::test::reset_phone_nav_bridge_probe();
  moto::nav::NavPresenter presenter;
  PhoneNavBridge bridge(presenter);

  const std::uint32_t demo_token =
      moto::ble::route_token(moto::demo::jinan_big_data::kRouteId);
  moto::ble::NavigationSnapshot snapshot;
  snapshot.state = moto::ble::NavigationState::Navigating;
  snapshot.network = moto::ble::NetworkState::Online;
  snapshot.flags = moto::ble::NavigationHasRouteView;
  snapshot.route_token = demo_token;
  snapshot.route_generation = 1;
  moto::ble::RouteGeometry geometry;
  geometry.route_token = demo_token;
  geometry.route_generation = 1;
  geometry.total_point_count = 2;
  geometry.view_origin = {36'674'773, 117'128'056};
  geometry.points = {
      {36'674'773, 117'128'056},
      {36'674'145, 117'128'489},
  };
  // The phone queues changed geometry before the snapshot that references it.
  // Geometry updates state only; the following snapshot is the single LVGL
  // presentation boundary for the pair.
  const int before_pair = moto::test::phone_nav_bridge_apply_count();
  CHECK(bridge.on_message(make_message(geometry, 40)) ==
        moto::ble::AckStatus::Ok);
  CHECK(moto::test::phone_nav_bridge_apply_count() == before_pair);
  CHECK(bridge.on_message(make_message(snapshot, 41)) ==
        moto::ble::AckStatus::Ok);
  CHECK(moto::test::phone_nav_bridge_apply_count() == before_pair);
  pump(bridge);
  CHECK(moto::test::phone_nav_bridge_apply_count() == before_pair + 1);
  CHECK(moto::test::phone_nav_bridge_last_road_point_count() ==
        moto::demo::jinan_big_data::kRoadPoints.size());
  CHECK(moto::test::phone_nav_bridge_last_road_polyline_count() ==
        moto::demo::jinan_big_data::kRoads.size());

  snapshot.flags = 0;
  snapshot.route_token = moto::ble::route_token("amap-live-route");
  snapshot.route_generation = 2;
  CHECK(bridge.on_message(make_message(snapshot, 42)) ==
        moto::ble::AckStatus::Ok);
  pump(bridge);
  CHECK(moto::test::phone_nav_bridge_last_road_point_count() == 0);
  CHECK(moto::test::phone_nav_bridge_last_road_polyline_count() == 0);
}

void test_map_scene_atomically_replaces_roads_and_buildings_at_capacity() {
  moto::test::reset_phone_nav_bridge_probe();
  moto::nav::NavPresenter presenter;
  PhoneNavBridge bridge(presenter);

  moto::ble::NavigationSnapshot navigation;
  navigation.state = moto::ble::NavigationState::Navigating;
  navigation.network = moto::ble::NetworkState::Online;
  navigation.flags = moto::ble::NavigationHasRouteView;
  navigation.route_token = 77;
  navigation.route_generation = 1;
  navigation.heading_cdeg = 9'000;
  CHECK(bridge.on_message(make_message(navigation, 50)) ==
        moto::ble::AckStatus::Ok);

  moto::ble::RouteGeometry geometry;
  geometry.route_token = 77;
  geometry.route_generation = 1;
  geometry.total_point_count = 2;
  geometry.view_origin = {36'670'000, 117'130'000};
  geometry.points = {
      geometry.view_origin,
      {36'670'000, 117'131'000},
  };
  CHECK(bridge.on_message(make_message(geometry, 51)) ==
        moto::ble::AckStatus::Ok);

  moto::ble::MapScene first;
  first.scene_revision = 1;
  first.view_origin = geometry.view_origin;
  first.radius_m = 500;
  first.roads = {
      {moto::ble::MapRoadClass::Primary,
       {{36'670'000, 117'130'000}, {36'670'000, 117'131'000}}},
  };
  first.buildings = {
      {moto::ble::MapBuildingClass::Landmark,
       {{36'670'100, 117'130'100},
        {36'670'100, 117'130'200},
        {36'670'200, 117'130'200}}},
  };
  const int before_first = moto::test::phone_nav_bridge_apply_count();
  CHECK(bridge.on_message(make_message(first, 52)) ==
        moto::ble::AckStatus::Ok);
  CHECK(moto::test::phone_nav_bridge_apply_count() == before_first);
  pump(bridge);
  CHECK(moto::test::phone_nav_bridge_apply_count() == before_first + 1);
  CHECK(moto::test::phone_nav_bridge_last_road_point_count() == 2);
  CHECK(moto::test::phone_nav_bridge_last_road_polyline_count() == 1);
  CHECK(moto::test::phone_nav_bridge_last_road_class() ==
        static_cast<std::uint8_t>(moto::ble::MapRoadClass::Primary));
  CHECK(moto::test::phone_nav_bridge_last_building_point_count() == 3);
  CHECK(moto::test::phone_nav_bridge_last_building_footprint_count() == 1);
  CHECK(moto::test::phone_nav_bridge_last_building_class() ==
        static_cast<std::uint8_t>(moto::ble::MapBuildingClass::Landmark));

  moto::ble::MapScene maximum;
  maximum.scene_revision = 2;
  maximum.view_origin = geometry.view_origin;
  maximum.radius_m = 700;
  for (std::size_t road_index = 0;
       road_index < moto::ble::kMaxMapSceneRoads; ++road_index) {
    moto::ble::MapRoadPolyline road;
    road.road_class = road_index == 0
                          ? moto::ble::MapRoadClass::Service
                          : moto::ble::MapRoadClass::Residential;
    for (std::size_t point_index = 0; point_index < 8; ++point_index) {
      road.points.push_back({
          geometry.view_origin.latitude_e6 +
              static_cast<std::int32_t>(road_index * 20 + point_index),
          geometry.view_origin.longitude_e6 +
              static_cast<std::int32_t>(road_index * 25 + point_index * 3),
      });
    }
    maximum.roads.push_back(std::move(road));
  }
  for (std::size_t building_index = 0;
       building_index < moto::ble::kMaxMapSceneBuildings;
       ++building_index) {
    moto::ble::MapBuildingFootprint building;
    building.building_class = moto::ble::MapBuildingClass::Parking;
    for (std::size_t point_index = 0; point_index < 8; ++point_index) {
      building.points.push_back({
          geometry.view_origin.latitude_e6 +
              static_cast<std::int32_t>(building_index * 30 + point_index),
          geometry.view_origin.longitude_e6 +
              static_cast<std::int32_t>(building_index * 35 + point_index * 2),
      });
    }
    maximum.buildings.push_back(std::move(building));
  }

  const int before_maximum = moto::test::phone_nav_bridge_apply_count();
  CHECK(bridge.on_message(make_message(maximum, 53)) ==
        moto::ble::AckStatus::Ok);
  CHECK(moto::test::phone_nav_bridge_apply_count() == before_maximum);
  pump(bridge);
  CHECK(moto::test::phone_nav_bridge_apply_count() == before_maximum + 1);
  CHECK(moto::test::phone_nav_bridge_last_road_point_count() ==
        moto::ble::kMaxMapSceneRoadPoints);
  CHECK(moto::test::phone_nav_bridge_last_road_polyline_count() ==
        moto::ble::kMaxMapSceneRoads);
  CHECK(moto::test::phone_nav_bridge_last_road_class() ==
        static_cast<std::uint8_t>(moto::ble::MapRoadClass::Service));
  CHECK(moto::test::phone_nav_bridge_last_building_point_count() ==
        moto::ble::kMaxMapSceneBuildingPoints);
  CHECK(moto::test::phone_nav_bridge_last_building_footprint_count() ==
        moto::ble::kMaxMapSceneBuildings);
  CHECK(moto::test::phone_nav_bridge_last_building_class() ==
        static_cast<std::uint8_t>(moto::ble::MapBuildingClass::Parking));

  const int before_stale = moto::test::phone_nav_bridge_apply_count();
  CHECK(bridge.on_message(make_message(first, 54)) ==
        moto::ble::AckStatus::Duplicate);
  CHECK(moto::test::phone_nav_bridge_apply_count() == before_stale);
  CHECK(moto::test::phone_nav_bridge_last_road_point_count() ==
        moto::ble::kMaxMapSceneRoadPoints);
  CHECK(moto::test::phone_nav_bridge_last_building_point_count() ==
        moto::ble::kMaxMapSceneBuildingPoints);

  moto::ble::MapScene empty;
  empty.scene_revision = 3;
  empty.view_origin = geometry.view_origin;
  empty.radius_m = 500;
  CHECK(bridge.on_message(make_message(empty, 55)) ==
        moto::ble::AckStatus::Ok);
  pump(bridge);
  CHECK(moto::test::phone_nav_bridge_last_road_point_count() == 0);
  CHECK(moto::test::phone_nav_bridge_last_road_polyline_count() == 0);
  CHECK(moto::test::phone_nav_bridge_last_building_point_count() == 0);
  CHECK(moto::test::phone_nav_bridge_last_building_footprint_count() == 0);
}

void test_imu_presentation_preserves_40hz_phase_and_stale_course_is_ignored() {
  moto::test::reset_phone_nav_bridge_probe();
  moto::nav::NavPresenter presenter;
  PhoneNavBridge bridge(presenter);
  // The IMU is usually ready before BLE. Its relative zero cannot become the
  // absolute heading when the phone finally connects.
  bridge.on_imu_sample(0.0F, moto::motion::AttitudeEstimate{}, 1);
  moto::ble::NavigationSnapshot navigation;
  navigation.state = moto::ble::NavigationState::Navigating;
  navigation.display_page = moto::ble::DisplayPage::Compass;
  navigation.flags = moto::ble::NavigationHasFix;
  navigation.heading_cdeg = 9'000;
  navigation.speed_deci_kph = 360;
  CHECK(bridge.on_message(make_message(navigation, 1)) == moto::ble::AckStatus::Ok);
  pump(bridge);
  CHECK(moto::test::phone_nav_bridge_last_heading_deg() == 90);

  const int before = moto::test::phone_nav_bridge_apply_count();
  for (std::uint64_t time = 8; time <= 1'000; time += 8) {
    bridge.on_imu_sample(45.0F, moto::motion::AttitudeEstimate{}, time);
    pump(bridge);
  }
  const int motion_frames = moto::test::phone_nav_bridge_apply_count() - before;
  CHECK(motion_frames == 40);  // resetting the deadline yields only 31
  const auto before_stale = moto::test::phone_nav_bridge_last_heading_deg();
  navigation.heading_cdeg = 27'000;
  navigation.flags |= moto::ble::NavigationGnssStale;
  CHECK(bridge.on_message(make_message(navigation, 2)) == moto::ble::AckStatus::Ok);
  pump(bridge);
  CHECK(moto::test::phone_nav_bridge_last_heading_deg() == before_stale);
}


// The rider's report: phone due east, handlebar 60 degrees. The last moving
// course was 60; parked, the phone now sends its compass, flagged, and the
// compass page must follow it to east.
void test_parked_compass_heading_reaches_the_compass_page() {
  moto::test::reset_phone_nav_bridge_probe();
  moto::nav::NavPresenter presenter;
  PhoneNavBridge bridge(presenter);
  moto::ble::NavigationSnapshot navigation;
  navigation.state = moto::ble::NavigationState::Navigating;
  navigation.display_page = moto::ble::DisplayPage::Compass;
  navigation.flags = moto::ble::NavigationHasFix;
  navigation.heading_cdeg = 6'000;
  navigation.speed_deci_kph = 300;
  CHECK(bridge.on_message(make_message(navigation, 1)) == moto::ble::AckStatus::Ok);
  pump(bridge);
  CHECK(moto::test::phone_nav_bridge_last_heading_deg() == 60);

  // Parked without the compass: the stale course is ignored, as before.
  navigation.speed_deci_kph = 0;
  CHECK(bridge.on_message(make_message(navigation, 2)) == moto::ble::AckStatus::Ok);
  pump(bridge);
  CHECK(moto::test::phone_nav_bridge_last_heading_deg() == 60);

  navigation.flags |= moto::ble::NavigationHeadingCompass;
  navigation.heading_cdeg = 9'000;
  for (std::uint16_t sequence = 3; sequence < 8; ++sequence) {
    CHECK(bridge.on_message(make_message(navigation, sequence)) ==
          moto::ble::AckStatus::Ok);
    pump(bridge);
  }
  const auto heading = moto::test::phone_nav_bridge_last_heading_deg();
  CHECK(heading >= 89 && heading <= 90);
}

struct AttitudeSenderContext {
  std::vector<moto::ble::AttitudeSample> samples;
  std::vector<std::uint8_t> flags;
};

bool record_attitude_sender(const moto::ble::Message& message,
                            std::uint8_t flags, void* context) {
  auto* sender = static_cast<AttitudeSenderContext*>(context);
  if (const auto* sample = std::get_if<moto::ble::AttitudeSample>(&message)) {
    sender->samples.push_back(*sample);
    sender->flags.push_back(flags);
  }
  return true;
}

void phone_ready(PhoneNavBridge& bridge, std::uint32_t capabilities,
                 std::uint16_t sequence) {
  moto::ble::ConnectionStatus ready;
  ready.role = moto::ble::EndpointRole::Phone;
  ready.state = moto::ble::ConnectionState::Ready;
  ready.minimum_version = moto::ble::kProtocolVersion;
  ready.maximum_version = moto::ble::kProtocolVersion;
  ready.capabilities = capabilities;
  ready.session_id = 777;
  CHECK(bridge.on_message(make_message(ready, sequence)) ==
        moto::ble::AckStatus::Ok);
  pump(bridge);
}

// ADR 0001: the device sends AttitudeSample at 10 Hz to a phone that
// advertised bit 8, valid estimates only, no Ack, in protocol units.
void test_attitude_uplink_is_gated_and_paced() {
  moto::test::reset_phone_nav_bridge_probe();
  moto::nav::NavPresenter presenter;
  PhoneNavBridge bridge(presenter);
  AttitudeSenderContext sender;
  bridge.set_sender(record_attitude_sender, &sender);

  moto::motion::AttitudeEstimate attitude;
  attitude.valid = true;
  attitude.lean_deg = -31.27F;
  attitude.pitch_deg = 2.5F;
  attitude.longitudinal_g = -0.412F;
  attitude.lateral_g = 0.6F;

  // A dead link sends nothing.
  bridge.on_imu_sample(0.0F, attitude, 1'000);
  pump(bridge);
  CHECK(sender.samples.empty());

  // A phone without the capability is never sent the type.
  bridge.on_link_state(true);
  phone_ready(bridge, moto::ble::CapabilityNavigation, 30);
  bridge.on_imu_sample(0.0F, attitude, 2'000);
  pump(bridge);
  CHECK(sender.samples.empty());

  // A phone that asks receives it, paced to 10 Hz across a 125 Hz stream.
  phone_ready(bridge, moto::ble::CapabilityAttitudeTelemetry, 31);
  for (std::uint64_t t = 3'000; t < 4'000; t += 8) {
    bridge.on_imu_sample(0.0F, attitude, t);
    pump(bridge);
  }
  CHECK(sender.samples.size() == 10);
  if (!sender.samples.empty()) {
    const moto::ble::AttitudeSample& first = sender.samples.front();
    CHECK(first.monotonic_ms == 3'000);
    CHECK(first.lean_cdeg == -3'127);
    CHECK(first.pitch_cdeg == 250);
    CHECK(first.longitudinal_mg == -412);
    CHECK(first.lateral_mg == 600);
    CHECK(sender.flags.front() == 0);
  }

  // An invalid estimate is not telemetry.
  sender.samples.clear();
  attitude.valid = false;
  bridge.on_imu_sample(0.0F, attitude, 5'000);
  pump(bridge);
  CHECK(sender.samples.empty());

  // Losing the link forgets the phone's capabilities.
  attitude.valid = true;
  bridge.on_link_state(false);
  bridge.on_link_state(true);
  bridge.on_imu_sample(0.0F, attitude, 6'000);
  pump(bridge);
  CHECK(sender.samples.empty());
}

void test_imu_attitude_reaches_the_lean_page() {
  moto::test::reset_phone_nav_bridge_probe();
  moto::nav::NavPresenter presenter;
  PhoneNavBridge bridge(presenter);

  moto::ble::NavigationSnapshot navigation;
  navigation.state = moto::ble::NavigationState::Navigating;
  navigation.network = moto::ble::NetworkState::Online;
  navigation.display_page = moto::ble::DisplayPage::Lean;
  navigation.flags = moto::ble::NavigationHasFix;
  CHECK(bridge.on_message(make_message(navigation, 1)) ==
        moto::ble::AckStatus::Ok);
  pump(bridge);

  moto::motion::AttitudeEstimate attitude;
  attitude.valid = true;
  attitude.lean_deg = 30.0F;
  attitude.pitch_deg = 2.0F;
  attitude.longitudinal_g = 0.3F;
  attitude.lateral_g = 0.58F;
  bridge.on_imu_sample(0.0F, attitude, 1'000);
  pump(bridge);

  CHECK(moto::test::phone_nav_bridge_last_has_attitude() == 1);
  CHECK(std::abs(moto::test::phone_nav_bridge_last_lean_deg() - 30.0F) <
        0.01F);
  CHECK(std::abs(moto::test::phone_nav_bridge_last_pitch_deg() - 2.0F) <
        0.01F);
  CHECK(std::abs(
            moto::test::phone_nav_bridge_last_longitudinal_g() - 0.3F) <
        0.01F);
  CHECK(std::abs(moto::test::phone_nav_bridge_last_lateral_g() - 0.58F) <
        0.01F);
}

void test_home_location_projects_onto_the_radar() {
  moto::test::reset_phone_nav_bridge_probe();
  moto::nav::NavPresenter presenter;
  PhoneNavBridge bridge(presenter);
  bridge.on_link_state(true);

  moto::ble::NavigationSnapshot navigation;
  navigation.state = moto::ble::NavigationState::Navigating;
  navigation.network = moto::ble::NetworkState::Online;
  navigation.flags = moto::ble::NavigationHasRouteView;
  navigation.route_token = 77;
  navigation.route_generation = 1;
  navigation.heading_cdeg = 0;
  CHECK(bridge.on_message(make_message(navigation, 60)) ==
        moto::ble::AckStatus::Ok);

  moto::ble::RouteGeometry geometry;
  geometry.route_token = 77;
  geometry.route_generation = 1;
  geometry.total_point_count = 2;
  geometry.view_origin = {36'670'000, 117'130'000};
  geometry.points = {
      geometry.view_origin,
      {36'670'000, 117'131'000},
  };
  CHECK(bridge.on_message(make_message(geometry, 61)) ==
        moto::ble::AckStatus::Ok);
  pump(bridge);

  // Home is ~300 m east of the rider; the transform is rider-centred and
  // heading-up, so it must land right of centre on the same row.
  moto::ble::HomeLocation home;
  home.revision = 4;
  home.has_home = true;
  home.position = {36'670'000, 117'133'500};
  CHECK(bridge.on_message(make_message(home, 62)) ==
        moto::ble::AckStatus::Ok);
  pump(bridge);
  CHECK(moto::test::phone_nav_bridge_last_has_home() == 1);
  CHECK(moto::test::phone_nav_bridge_last_home_x() >
        MOTO_UI_CANVAS_WIDTH / 2);
  CHECK(std::abs(moto::test::phone_nav_bridge_last_home_y() -
                 MOTO_UI_CANVAS_HEIGHT / 2) <= 1);

  // A replayed older revision must not resurrect the previous address.
  moto::ble::HomeLocation cleared = home;
  cleared.revision = 5;
  cleared.has_home = false;
  CHECK(bridge.on_message(make_message(cleared, 63)) ==
        moto::ble::AckStatus::Ok);
  pump(bridge);
  CHECK(moto::test::phone_nav_bridge_last_has_home() == 0);

  CHECK(bridge.on_message(make_message(home, 64)) ==
        moto::ble::AckStatus::Ok);
  pump(bridge);
  CHECK(moto::test::phone_nav_bridge_last_has_home() == 0);

  // A WGS84 home would land hundreds of metres away, so the decoder refuses
  // it instead of drawing a wrong blip.
  moto::ble::HomeLocation wgs84 = home;
  wgs84.revision = 6;
  wgs84.coordinate_system = moto::ble::CoordinateSystem::Wgs84;
  const auto encoded = moto::ble::encode_message(moto::ble::Message{home});
  CHECK(encoded.ok());
  if (encoded.ok()) {
    // The encoder refuses WGS84, so flip the frame byte to prove the decoder
    // rejects a peer that sends one.
    moto::ble::Bytes payload = encoded.value;
    payload[1] = static_cast<std::uint8_t>(
        moto::ble::CoordinateSystem::Wgs84);
    moto::ble::ReassembledMessage tampered;
    tampered.type = moto::ble::MessageType::HomeLocation;
    tampered.sequence = 65;
    tampered.payload = payload;
    CHECK(bridge.on_message(tampered) == moto::ble::AckStatus::Failed);
  }
  pump(bridge);
  CHECK(moto::test::phone_nav_bridge_last_has_home() == 0);
}

}  // namespace

struct SpeedRecord {
  int calls = 0;
  float mps = -1.0F;
  bool valid = false;
};

void record_speed(float mps, bool valid, void* context) {
  auto* record = static_cast<SpeedRecord*>(context);
  ++record->calls;
  record->mps = mps;
  record->valid = valid;
}

// The Lean estimator's kinematic correction runs on the phone's GPS speed.
// It must arrive with every snapshot, and stop counting as valid when the
// fix goes stale or the phone goes away.
void test_phone_speed_reaches_the_lean_estimator() {
  moto::test::reset_phone_nav_bridge_probe();
  moto::nav::NavPresenter presenter;
  PhoneNavBridge bridge(presenter);
  SpeedRecord speed;
  bridge.set_speed_listener(record_speed, &speed);
  bridge.on_link_state(true);

  moto::ble::NavigationSnapshot navigation;
  navigation.state = moto::ble::NavigationState::Navigating;
  navigation.flags = moto::ble::NavigationHasFix;
  navigation.speed_deci_kph = 540;  // 54 km/h
  CHECK(bridge.on_message(make_message(navigation, 1)) ==
        moto::ble::AckStatus::Ok);
  CHECK(speed.calls == 1);
  CHECK(speed.valid);
  CHECK(std::fabs(speed.mps - 15.0F) < 1.0e-4F);

  navigation.flags |= moto::ble::NavigationGnssStale;
  CHECK(bridge.on_message(make_message(navigation, 2)) ==
        moto::ble::AckStatus::Ok);
  CHECK(speed.calls == 2);
  CHECK(!speed.valid);

  navigation.flags = 0;  // no fix at all
  CHECK(bridge.on_message(make_message(navigation, 3)) ==
        moto::ble::AckStatus::Ok);
  CHECK(!speed.valid);

  bridge.on_link_state(false);
  CHECK(speed.calls == 4);
  CHECK(!speed.valid);
  pump(bridge);
}

int main() {
  test_sender_is_called_without_bridge_state_lock();  test_settings_apply_echo_and_persist();
  test_boot_settings_replay_reaches_the_ui();
  test_home_shortcut_sends_the_intent();
  test_refused_home_tap_reaches_the_screen();
  test_navigation_and_touch_updates_are_serialized();
  test_ble_submission_never_waits_for_lvgl_and_retries_latest_state();
  test_phone_connection_lifecycle_reaches_the_ui();
  test_music_capability_state_and_touch_commands();
  test_system_media_takes_over_the_music_page_and_falls_back();
  test_demo_uses_the_production_presenter_path();
  test_ios_demo_token_attaches_context_and_live_route_clears_it();
  test_map_scene_atomically_replaces_roads_and_buildings_at_capacity();
  test_imu_presentation_preserves_40hz_phase_and_stale_course_is_ignored();
  test_parked_compass_heading_reaches_the_compass_page();
  test_imu_attitude_reaches_the_lean_page();
  test_attitude_uplink_is_gated_and_paced();
  test_home_location_projects_onto_the_radar();
  test_phone_speed_reaches_the_lean_estimator();

  if (failures != 0) {
    std::cerr << failures << " phone navigation bridge checks failed\n";
    return EXIT_FAILURE;
  }
  std::cout << "All phone navigation bridge checks passed\n";
  return EXIT_SUCCESS;
}
