// Apple Media Service codec and Now Playing projection. Byte layouts and IDs
// are from Apple's "Apple Media Service Reference" (Specification, Appendix A).
#include "ams_codec.hpp"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace {

using moto::ams::CommandSet;
using moto::ams::EntityId;
using moto::ams::EntityUpdate;
using moto::ams::NowPlaying;
using moto::ams::PlaybackInfo;
using moto::ams::PlaybackState;
using moto::ams::RemoteCommand;
using moto::ams::command_bit;

int failures = 0;

#define CHECK(condition)                                                   \
  do {                                                                     \
    if (!(condition)) {                                                    \
      std::cerr << __FILE__ << ':' << __LINE__                             \
                << " CHECK failed: " #condition << '\n';                   \
      ++failures;                                                          \
    }                                                                      \
  } while (false)

// Formats a little-endian BLE_UUID128_INIT byte array as the canonical
// big-endian string Apple publishes.
std::string uuid_string(const std::uint8_t (&bytes)[16]) {
  char text[37]{};
  int offset = 0;
  for (int index = 15; index >= 0; --index) {
    offset += std::snprintf(text + offset, sizeof(text) - offset, "%02X",
                            bytes[index]);
    if (index == 12 || index == 10 || index == 8 || index == 6) {
      text[offset++] = '-';
    }
  }
  return text;
}

EntityUpdate update(EntityId entity, std::uint8_t attribute,
                    const std::string& value, std::uint8_t flags = 0) {
  std::vector<std::uint8_t> bytes = {static_cast<std::uint8_t>(entity),
                                     attribute, flags};
  bytes.insert(bytes.end(), value.begin(), value.end());
  EntityUpdate parsed;
  CHECK(moto::ams::parse_entity_update(bytes.data(), bytes.size(), parsed));
  return parsed;
}

void test_uuids_match_the_specification() {
  CHECK(uuid_string(moto::ams::kServiceUuid) ==
        "89D3502B-0F36-433A-8EF4-C502AD55F8DC");
  CHECK(uuid_string(moto::ams::kRemoteCommandUuid) ==
        "9B3C81D8-57B1-4A8A-B8DF-0E56F7CA51C2");
  CHECK(uuid_string(moto::ams::kEntityUpdateUuid) ==
        "2F7CABCE-808D-411F-9A0C-BB92BA96C102");
  CHECK(uuid_string(moto::ams::kEntityAttributeUuid) ==
        "C6B2F38C-23AB-46D8-A6AB-A3A870BBD5D7");
}

void test_command_ids_and_registration_payloads() {
  CHECK(static_cast<int>(RemoteCommand::Play) == 0);
  CHECK(static_cast<int>(RemoteCommand::Pause) == 1);
  CHECK(static_cast<int>(RemoteCommand::TogglePlayPause) == 2);
  CHECK(static_cast<int>(RemoteCommand::NextTrack) == 3);
  CHECK(static_cast<int>(RemoteCommand::PreviousTrack) == 4);
  CHECK(static_cast<int>(RemoteCommand::SkipForward) == 9);
  CHECK(static_cast<int>(RemoteCommand::SkipBackward) == 10);
  CHECK(static_cast<int>(RemoteCommand::LikeTrack) == 11);

  // Player: Name(0), PlaybackInfo(1). Track: Artist(0), Title(2), Duration(3).
  CHECK((moto::ams::player_registration() ==
         std::vector<std::uint8_t>{0x00, 0x00, 0x01}));
  CHECK((moto::ams::track_registration() ==
         std::vector<std::uint8_t>{0x02, 0x00, 0x02, 0x03}));
  CHECK((moto::ams::encode_entity_update_request(EntityId::Queue, {0, 1}) ==
         std::vector<std::uint8_t>{0x01, 0x00, 0x01}));
}

void test_remote_command_notification() {
  const std::uint8_t spotify[] = {0, 1, 2, 3, 4, 5, 6, 7, 8};
  const CommandSet commands =
      moto::ams::parse_remote_command_notification(spotify, sizeof(spotify));
  CHECK((commands & command_bit(RemoteCommand::TogglePlayPause)) != 0U);
  CHECK((commands & command_bit(RemoteCommand::LikeTrack)) == 0U);
  CHECK(commands == 0x01FFU);

  // Reserved IDs must not alias onto real commands.
  const std::uint8_t reserved[] = {11, 14, 30, 255};
  CHECK(moto::ams::parse_remote_command_notification(reserved,
                                                     sizeof(reserved)) ==
        command_bit(RemoteCommand::LikeTrack));
  CHECK(moto::ams::parse_remote_command_notification(nullptr, 3) == 0U);
  CHECK(moto::ams::parse_remote_command_notification(spotify, 0) == 0U);
}

void test_entity_update_parsing() {
  const std::uint8_t title[] = {0x02, 0x02, 0x00, 'S', 'k', 'y'};
  EntityUpdate parsed;
  CHECK(moto::ams::parse_entity_update(title, sizeof(title), parsed));
  CHECK(parsed.entity == EntityId::Track);
  CHECK(parsed.attribute == moto::ams::TrackTitle);
  CHECK(!parsed.truncated);
  CHECK(parsed.value == "Sky");

  // An attribute the Now Playing app leaves empty is a 3-byte notification.
  const std::uint8_t empty[] = {0x00, 0x00, 0x00};
  CHECK(moto::ams::parse_entity_update(empty, sizeof(empty), parsed));
  CHECK(parsed.value.empty());

  const std::uint8_t short_frame[] = {0x02, 0x02};
  CHECK(!moto::ams::parse_entity_update(short_frame, sizeof(short_frame),
                                        parsed));
  const std::uint8_t reserved_entity[] = {0x03, 0x00, 0x00, 'x'};
  CHECK(!moto::ams::parse_entity_update(reserved_entity,
                                        sizeof(reserved_entity), parsed));

  // "夜曲" is E5 A4 9C E6 9B B2. Truncation at the ATT limit can leave the
  // first byte(s) of the last character; they must not reach the label.
  const std::uint8_t cut[] = {0x02, 0x02, 0x01, 0xE5, 0xA4, 0x9C, 0xE6, 0x9B};
  CHECK(moto::ams::parse_entity_update(cut, sizeof(cut), parsed));
  CHECK(parsed.truncated);
  CHECK(parsed.value == "夜");
}

void test_playback_info_and_seconds() {
  PlaybackInfo info;
  CHECK(moto::ams::parse_playback_info("1,1.0,12.345", info));
  CHECK(info.state == PlaybackState::Playing);
  CHECK(std::fabs(info.rate - 1.0F) < 1e-6F);
  CHECK(std::fabs(info.elapsed_s - 12.345F) < 1e-4F);

  CHECK(moto::ams::parse_playback_info("0,0.0,245", info));
  CHECK(info.state == PlaybackState::Paused);
  CHECK(moto::ams::parse_playback_info("2,-2.0,30.5", info));
  CHECK(info.state == PlaybackState::Rewinding);
  CHECK(info.rate < 0.0F);
  CHECK(moto::ams::parse_playback_info("3,2,0.", info));
  CHECK(info.state == PlaybackState::FastForwarding);

  CHECK(!moto::ams::parse_playback_info("", info));
  CHECK(!moto::ams::parse_playback_info("1,1.0", info));
  CHECK(!moto::ams::parse_playback_info("4,1.0,0", info));
  CHECK(!moto::ams::parse_playback_info("1.5,1.0,0", info));
  CHECK(!moto::ams::parse_playback_info("x,1.0,0", info));
  CHECK(!moto::ams::parse_playback_info("1,1.0,12s", info));

  float seconds = -1.0F;
  CHECK(moto::ams::parse_seconds("245.317", seconds));
  CHECK(std::fabs(seconds - 245.317F) < 1e-3F);
  CHECK(!moto::ams::parse_seconds("", seconds));
  CHECK(!moto::ams::parse_seconds("-3", seconds));
}

void test_utf8_prefix() {
  const std::string mixed = "AB夜曲";  // 2 + 3 + 3 bytes
  CHECK(moto::ams::utf8_prefix_length(mixed, 100) == 8);
  CHECK(moto::ams::utf8_prefix_length(mixed, 8) == 8);
  CHECK(moto::ams::utf8_prefix_length(mixed, 7) == 5);
  CHECK(moto::ams::utf8_prefix_length(mixed, 4) == 2);
  CHECK(moto::ams::utf8_prefix_length(mixed, 0) == 0);
  const std::string emoji = "\xF0\x9F\x8E\xB5!";  // U+1F3B5 then '!'
  CHECK(moto::ams::utf8_prefix_length(emoji, 3) == 0);
  CHECK(moto::ams::utf8_prefix_length(emoji, 5) == 5);
  const std::string stray = "a\x9C" "b";
  CHECK(moto::ams::utf8_prefix_length(stray, 3) == 1);
}

void test_source_labels_fit_the_ascii_plate() {
  CHECK(moto::ams::source_label("网易云音乐") == "NETEASE");
  CHECK(moto::ams::source_label("NetEase Cloud Music") == "NETEASE");
  CHECK(moto::ams::source_label("QQ音乐") == "QQ MUSIC");
  CHECK(moto::ams::source_label("酷狗音乐") == "KUGOU");
  CHECK(moto::ams::source_label("Spotify") == "SPOTIFY");
  CHECK(moto::ams::source_label("音乐") == "APPLE MUSIC");
  CHECK(moto::ams::source_label("Music") == "APPLE MUSIC");
  CHECK(moto::ams::source_label("播客") == "PODCASTS");
  CHECK(moto::ams::source_label("") == "IPHONE");
  CHECK(moto::ams::source_label("Tidal") == "TIDAL");
  CHECK(moto::ams::source_label("Pocket Casts & More") == "POCKET CASTS");
  CHECK(moto::ams::source_label("某个播放器") == "NOW PLAYING");
  for (const char* name : {"网易云音乐", "Spotify", "A very long player name"}) {
    CHECK(moto::ams::source_label(name).size() <=
          moto::ams::kSourceLabelBytes);
  }
}

void test_now_playing_projection() {
  NowPlaying now;
  moto::ble::MediaState state = now.media_state();
  CHECK((state.flags & moto::ble::MediaConnected) != 0U);
  CHECK((state.flags & moto::ble::MediaPlaying) == 0U);
  CHECK(state.source_name == "IPHONE");
  CHECK(state.track_title == "尚未播放");

  CHECK(now.apply(update(EntityId::Player, moto::ams::PlayerName,
                         "网易云音乐")));
  CHECK(now.apply(update(EntityId::Track, moto::ams::TrackTitle, "晴天")));
  CHECK(now.apply(update(EntityId::Track, moto::ams::TrackArtist, "周杰伦")));
  CHECK(now.apply(update(EntityId::Track, moto::ams::TrackDuration,
                         "269.5")));
  CHECK(now.apply(update(EntityId::Player, moto::ams::PlayerPlaybackInfo,
                         "1,1.0,42.4")));
  // A repeated value changes nothing on screen.
  CHECK(!now.apply(update(EntityId::Track, moto::ams::TrackTitle, "晴天")));
  // Album was never requested; if it arrives anyway it is ignored.
  CHECK(!now.apply(update(EntityId::Track, moto::ams::TrackAlbum, "叶惠美")));

  state = now.media_state();
  CHECK((state.flags & moto::ble::MediaPlaying) != 0U);
  CHECK((state.flags & moto::ble::MediaLikeAvailable) == 0U);
  CHECK(state.source_name == "NETEASE");
  CHECK(state.track_title == "晴天");
  CHECK(state.artist_name == "周杰伦");
  CHECK(state.position_s == 42);
  CHECK(state.duration_s == 270);
  CHECK(state.track_token != 0U);

  CHECK(now.apply(update(EntityId::Player, moto::ams::PlayerPlaybackInfo,
                         "0,0.0,43.0")));
  CHECK((now.media_state().flags & moto::ble::MediaPlaying) == 0U);

  // The Now Playing app quits: iOS sends empty values.
  now.apply(update(EntityId::Player, moto::ams::PlayerName, ""));
  now.apply(update(EntityId::Player, moto::ams::PlayerPlaybackInfo, ""));
  now.apply(update(EntityId::Track, moto::ams::TrackTitle, ""));
  now.apply(update(EntityId::Track, moto::ams::TrackArtist, ""));
  state = now.media_state();
  CHECK(state.source_name == "IPHONE");
  CHECK(state.track_title == "尚未播放");
  CHECK(state.artist_name.empty());
  CHECK((state.flags & moto::ble::MediaPlaying) == 0U);
  CHECK(state.track_token == 0U);
}

void test_long_titles_are_clipped_on_character_boundaries() {
  NowPlaying now;
  std::string title;
  for (int index = 0; index < 30; ++index) title += "夜";  // 90 bytes
  now.apply(update(EntityId::Track, moto::ams::TrackTitle, title));
  const moto::ble::MediaState state = now.media_state();
  CHECK(state.track_title.size() <= moto::ams::kTitleBytes);
  CHECK(state.track_title.size() % 3U == 0U);
  CHECK(state.track_title.size() == 63U);
}

void test_keys_map_to_supported_commands() {
  NowPlaying now;
  RemoteCommand command{};
  // Nothing known yet: send the natural command rather than drop the press.
  CHECK(now.command_for(MOTO_MUSIC_TOGGLE_PLAYBACK, command));
  CHECK(command == RemoteCommand::TogglePlayPause);
  CHECK(now.command_for(MOTO_MUSIC_PREVIOUS, command));
  CHECK(command == RemoteCommand::PreviousTrack);
  CHECK(now.command_for(MOTO_MUSIC_NEXT, command));
  CHECK(command == RemoteCommand::NextTrack);
  CHECK(!now.command_for(MOTO_MUSIC_LIKE, command));

  // Apple Music with a likeable track.
  const std::uint8_t music[] = {0, 1, 2, 3, 4, 11, 12};
  CHECK(now.set_supported_commands(
      moto::ams::parse_remote_command_notification(music, sizeof(music))));
  CHECK(now.command_for(MOTO_MUSIC_LIKE, command));
  CHECK(command == RemoteCommand::LikeTrack);
  CHECK((now.media_state().flags & moto::ble::MediaLikeAvailable) != 0U);

  // A podcast: no previous/next item, only 15 s skips, and no toggle.
  CHECK(now.set_supported_commands(
      command_bit(RemoteCommand::Play) | command_bit(RemoteCommand::Pause) |
      command_bit(RemoteCommand::SkipForward) |
      command_bit(RemoteCommand::SkipBackward)));
  CHECK(!now.set_supported_commands(
      command_bit(RemoteCommand::Play) | command_bit(RemoteCommand::Pause) |
      command_bit(RemoteCommand::SkipForward) |
      command_bit(RemoteCommand::SkipBackward)));
  CHECK(now.command_for(MOTO_MUSIC_PREVIOUS, command));
  CHECK(command == RemoteCommand::SkipBackward);
  CHECK(now.command_for(MOTO_MUSIC_NEXT, command));
  CHECK(command == RemoteCommand::SkipForward);
  CHECK(now.command_for(MOTO_MUSIC_TOGGLE_PLAYBACK, command));
  CHECK(command == RemoteCommand::Play);
  now.apply(update(EntityId::Player, moto::ams::PlayerPlaybackInfo,
                   "1,1.0,3.0"));
  CHECK(now.command_for(MOTO_MUSIC_TOGGLE_PLAYBACK, command));
  CHECK(command == RemoteCommand::Pause);
  CHECK(!now.command_for(MOTO_MUSIC_LIKE, command));

  // No Now Playing app at all: every key is a no-op, never a stray write.
  CHECK(now.set_supported_commands(0));
  CHECK(!now.command_for(MOTO_MUSIC_PREVIOUS, command));
  CHECK(!now.command_for(MOTO_MUSIC_TOGGLE_PLAYBACK, command));
  CHECK(!now.command_for(MOTO_MUSIC_NEXT, command));

  now.reset();
  CHECK(now.command_for(MOTO_MUSIC_NEXT, command));
}

}  // namespace

int main() {
  test_uuids_match_the_specification();
  test_command_ids_and_registration_payloads();
  test_remote_command_notification();
  test_entity_update_parsing();
  test_playback_info_and_seconds();
  test_utf8_prefix();
  test_source_labels_fit_the_ascii_plate();
  test_now_playing_projection();
  test_long_titles_are_clipped_on_character_boundaries();
  test_keys_map_to_supported_commands();

  if (failures != 0) {
    std::cerr << failures << " AMS codec checks failed\n";
    return EXIT_FAILURE;
  }
  std::cout << "All AMS codec checks passed\n";
  return EXIT_SUCCESS;
}
