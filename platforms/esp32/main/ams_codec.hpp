#pragma once

// Apple Media Service (AMS) wire format and Now Playing projection.
//
// AMS is the iPhone's GATT service for an accessory to read and drive the
// system Now Playing app, whichever app that is (NetEase Cloud Music, QQ Music,
// Spotify, Apple Music, Podcasts...). The ESP32 is the Media Remote (GATT
// client); the iPhone is the Media Source. Reference: Apple, "Apple Media
// Service Reference" (Specification and Appendix A).
//
// This file is pure C++ with no NimBLE dependency so the native suite can test
// it; ams_client_nimble.cpp owns discovery, subscriptions and writes.

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <string>
#include <string_view>
#include <vector>

#include "moto/ble_protocol/ble_protocol.hpp"
#include "moto_nav_ui.h"

namespace moto::ams {

// 89D3502B-0F36-433A-8EF4-C502AD55F8DC and its three characteristics, in the
// Bluetooth little-endian byte order BLE_UUID128_INIT expects.
inline constexpr std::uint8_t kServiceUuid[16] = {
    0xDC, 0xF8, 0x55, 0xAD, 0x02, 0xC5, 0xF4, 0x8E,
    0x3A, 0x43, 0x36, 0x0F, 0x2B, 0x50, 0xD3, 0x89};
// 9B3C81D8-57B1-4A8A-B8DF-0E56F7CA51C2: write one RemoteCommandID; notifies
// the list of commands the current player accepts.
inline constexpr std::uint8_t kRemoteCommandUuid[16] = {
    0xC2, 0x51, 0xCA, 0xF7, 0x56, 0x0E, 0xDF, 0xB8,
    0x8A, 0x4A, 0xB1, 0x57, 0xD8, 0x81, 0x3C, 0x9B};
// 2F7CABCE-808D-411F-9A0C-BB92BA96C102: write EntityID + AttributeIDs to
// register; notifies EntityID, AttributeID, flags, value.
inline constexpr std::uint8_t kEntityUpdateUuid[16] = {
    0x02, 0xC1, 0x96, 0xBA, 0x92, 0xBB, 0x0C, 0x9A,
    0x1F, 0x41, 0x8D, 0x80, 0xCE, 0xAB, 0x7C, 0x2F};
// C6B2F38C-23AB-46D8-A6AB-A3A870BBD5D7: read the full value of a truncated
// attribute. Unused: the device clips every string to its label buffer anyway.
inline constexpr std::uint8_t kEntityAttributeUuid[16] = {
    0xD7, 0xD5, 0xBB, 0x70, 0xA8, 0xA3, 0xAB, 0xA6,
    0xD8, 0x46, 0xAB, 0x23, 0x8C, 0xF3, 0xB2, 0xC6};

enum class RemoteCommand : std::uint8_t {
  Play = 0,
  Pause = 1,
  TogglePlayPause = 2,
  NextTrack = 3,
  PreviousTrack = 4,
  VolumeUp = 5,
  VolumeDown = 6,
  AdvanceRepeatMode = 7,
  AdvanceShuffleMode = 8,
  SkipForward = 9,
  SkipBackward = 10,
  LikeTrack = 11,
  DislikeTrack = 12,
  BookmarkTrack = 13,
};
inline constexpr std::uint8_t kRemoteCommandCount = 14;

enum class EntityId : std::uint8_t {
  Player = 0,
  Queue = 1,
  Track = 2,
};

enum PlayerAttribute : std::uint8_t {
  PlayerName = 0,
  PlayerPlaybackInfo = 1,
  PlayerVolume = 2,
};

enum TrackAttribute : std::uint8_t {
  TrackArtist = 0,
  TrackAlbum = 1,
  TrackTitle = 2,
  TrackDuration = 3,
};

inline constexpr std::uint8_t kEntityUpdateFlagTruncated = 1U << 0U;

// ATT application error codes returned by the Media Source.
inline constexpr std::uint8_t kErrorInvalidState = 0xA0;
inline constexpr std::uint8_t kErrorInvalidCommand = 0xA1;
inline constexpr std::uint8_t kErrorAbsentAttribute = 0xA2;

enum class PlaybackState : std::uint8_t {
  Paused = 0,
  Playing = 1,
  Rewinding = 2,
  FastForwarding = 3,
};

// One bit per RemoteCommandID, from the Remote Command notification.
using CommandSet = std::uint16_t;

constexpr CommandSet command_bit(RemoteCommand command) noexcept {
  return static_cast<CommandSet>(1U << static_cast<std::uint8_t>(command));
}

// Reserved IDs (14-255) are ignored, never folded into another bit.
CommandSet parse_remote_command_notification(const std::uint8_t* data,
                                             std::size_t length) noexcept;

// Entity Update registration payloads: one write per entity.
std::vector<std::uint8_t> encode_entity_update_request(
    EntityId entity, std::initializer_list<std::uint8_t> attributes);
// What the device registers for. Album, volume and the queue have no place on
// the music page, so they are never requested.
std::vector<std::uint8_t> player_registration();
std::vector<std::uint8_t> track_registration();

struct EntityUpdate {
  EntityId entity = EntityId::Player;
  std::uint8_t attribute = 0;
  bool truncated = false;
  std::string value;
};

// False for a short notification or a reserved EntityID. A value cut by
// truncation is trimmed back to its last complete UTF-8 character.
bool parse_entity_update(const std::uint8_t* data, std::size_t length,
                         EntityUpdate& output);

struct PlaybackInfo {
  PlaybackState state = PlaybackState::Paused;
  float rate = 0.0F;
  float elapsed_s = 0.0F;
};

// "PlaybackState,PlaybackRate,ElapsedTime", e.g. "1,1.0,12.345". iOS sends
// an empty string when no app owns Now Playing; that is not a parse.
bool parse_playback_info(std::string_view value, PlaybackInfo& output);
// Non-negative decimal seconds, e.g. a track duration of "245.317".
bool parse_seconds(std::string_view value, float& output);

// Length of the longest prefix of `value` that is at most `max_bytes` long and
// does not end inside a UTF-8 sequence. The UI copies strings into fixed
// buffers with snprintf, which would otherwise cut a CJK character in half.
std::size_t utf8_prefix_length(std::string_view value,
                               std::size_t max_bytes) noexcept;

// The music page's source plate uses an ASCII display face, so the localized
// player name ("网易云音乐", "音乐") is shown as a short ASCII label instead.
std::string source_label(std::string_view player_name);

// The label buffers in both UI backends (moto_nav_ui_*.cpp), minus the NUL.
inline constexpr std::size_t kSourceLabelBytes = 31;
inline constexpr std::size_t kTitleBytes = 63;
inline constexpr std::size_t kArtistBytes = 47;

// Accumulates AMS notifications into the device's MediaState projection.
// Not thread-safe: the NimBLE host task owns it.
class NowPlaying {
 public:
  void reset() noexcept;
  // Each returns true when the projection the screen shows has changed.
  bool apply(const EntityUpdate& update);
  bool set_supported_commands(CommandSet commands) noexcept;

  // The AMS command for a music-page key, or false when the current player
  // has said it does not accept one (the key then does nothing).
  bool command_for(moto_music_command_t key, RemoteCommand& output) const;

  moto::ble::MediaState media_state() const;
  bool playing() const noexcept;
  CommandSet supported_commands() const noexcept { return supported_; }

 private:
  bool supports(RemoteCommand command) const noexcept;

  std::string player_name_;
  std::string title_;
  std::string artist_;
  PlaybackInfo playback_{};
  bool has_playback_ = false;
  float duration_s_ = 0.0F;
  CommandSet supported_ = 0;
  // Until the first Remote Command notification nothing is known; send
  // rather than swallow a key press on a link that is still settling.
  bool supported_known_ = false;
};

}  // namespace moto::ams
