#include "ams_codec.hpp"

#include <array>
#include <initializer_list>

namespace moto::ams {
namespace {

bool is_digit(char value) noexcept { return value >= '0' && value <= '9'; }

// Locale-free decimal parser: [-]digits[.digits]. std::stof would abort on a
// malformed value in this -fno-exceptions firmware, and strtof needs a NUL.
bool parse_decimal(std::string_view text, float& output) {
  std::size_t index = 0;
  while (index < text.size() && text[index] == ' ') ++index;
  bool negative = false;
  if (index < text.size() && (text[index] == '-' || text[index] == '+')) {
    negative = text[index] == '-';
    ++index;
  }
  double value = 0.0;
  bool any_digit = false;
  while (index < text.size() && is_digit(text[index])) {
    value = value * 10.0 + (text[index] - '0');
    any_digit = true;
    ++index;
  }
  if (index < text.size() && text[index] == '.') {
    ++index;
    double scale = 0.1;
    while (index < text.size() && is_digit(text[index])) {
      value += (text[index] - '0') * scale;
      scale *= 0.1;
      any_digit = true;
      ++index;
    }
  }
  while (index < text.size() && text[index] == ' ') ++index;
  if (!any_digit || index != text.size()) return false;
  output = static_cast<float>(negative ? -value : value);
  return true;
}

bool contains(std::string_view haystack, std::string_view needle) {
  return haystack.find(needle) != std::string_view::npos;
}

char ascii_upper(char value) noexcept {
  return value >= 'a' && value <= 'z' ? static_cast<char>(value - 'a' + 'A')
                                      : value;
}

std::string ascii_upper(std::string_view value) {
  std::string output;
  output.reserve(value.size());
  for (const char character : value) output.push_back(ascii_upper(character));
  return output;
}

// Glyphs moto_font_display_24 carries besides letters and digits.
bool plate_glyph(char value) noexcept {
  if ((value >= 'A' && value <= 'Z') || is_digit(value)) return true;
  switch (value) {
    case ' ': case '.': case ',': case ':': case '/': case '+': case '-':
    case '%': case '(': case ')': case '!': case '?':
      return true;
    default:
      return false;
  }
}

std::string clip_utf8(const std::string& value, std::size_t max_bytes) {
  return value.substr(0, utf8_prefix_length(value, max_bytes));
}

std::uint32_t fnv1a(std::initializer_list<std::string_view> parts) {
  std::uint32_t hash = 2'166'136'261U;
  for (const std::string_view part : parts) {
    for (const char byte : part) {
      hash ^= static_cast<std::uint8_t>(byte);
      hash *= 16'777'619U;
    }
    hash ^= 0x1FU;
    hash *= 16'777'619U;
  }
  return hash == 0U ? 1U : hash;
}

}  // namespace

CommandSet parse_remote_command_notification(const std::uint8_t* data,
                                             std::size_t length) noexcept {
  CommandSet commands = 0;
  for (std::size_t index = 0; data != nullptr && index < length; ++index) {
    if (data[index] < kRemoteCommandCount) {
      commands |= static_cast<CommandSet>(1U << data[index]);
    }
  }
  return commands;
}

std::vector<std::uint8_t> encode_entity_update_request(
    EntityId entity, std::initializer_list<std::uint8_t> attributes) {
  std::vector<std::uint8_t> output;
  output.reserve(1U + attributes.size());
  output.push_back(static_cast<std::uint8_t>(entity));
  output.insert(output.end(), attributes.begin(), attributes.end());
  return output;
}

std::vector<std::uint8_t> player_registration() {
  return encode_entity_update_request(EntityId::Player,
                                      {PlayerName, PlayerPlaybackInfo});
}

std::vector<std::uint8_t> track_registration() {
  return encode_entity_update_request(
      EntityId::Track, {TrackArtist, TrackTitle, TrackDuration});
}

bool parse_entity_update(const std::uint8_t* data, std::size_t length,
                         EntityUpdate& output) {
  if (data == nullptr || length < 3U ||
      data[0] > static_cast<std::uint8_t>(EntityId::Track)) {
    return false;
  }
  output.entity = static_cast<EntityId>(data[0]);
  output.attribute = data[1];
  output.truncated = (data[2] & kEntityUpdateFlagTruncated) != 0U;
  output.value.assign(reinterpret_cast<const char*>(data + 3), length - 3U);
  // Truncation cuts at the ATT payload limit, which is a byte count. Only the
  // tail can be damaged, so trim back to the last complete character.
  output.value.resize(utf8_prefix_length(output.value, output.value.size()));
  return true;
}

bool parse_playback_info(std::string_view value, PlaybackInfo& output) {
  const std::size_t first = value.find(',');
  if (first == std::string_view::npos) return false;
  const std::size_t second = value.find(',', first + 1U);
  if (second == std::string_view::npos) return false;

  float state = 0.0F;
  float rate = 0.0F;
  float elapsed = 0.0F;
  if (!parse_decimal(value.substr(0, first), state) ||
      !parse_decimal(value.substr(first + 1U, second - first - 1U), rate) ||
      !parse_decimal(value.substr(second + 1U), elapsed)) {
    return false;
  }
  const int state_id = static_cast<int>(state);
  if (static_cast<float>(state_id) != state || state_id < 0 ||
      state_id > static_cast<int>(PlaybackState::FastForwarding)) {
    return false;
  }
  output.state = static_cast<PlaybackState>(state_id);
  output.rate = rate;
  output.elapsed_s = elapsed < 0.0F ? 0.0F : elapsed;
  return true;
}

bool parse_seconds(std::string_view value, float& output) {
  float parsed = 0.0F;
  if (!parse_decimal(value, parsed) || parsed < 0.0F) return false;
  output = parsed;
  return true;
}

std::size_t utf8_prefix_length(std::string_view value,
                               std::size_t max_bytes) noexcept {
  std::size_t index = 0;
  const std::size_t limit = value.size() < max_bytes ? value.size() : max_bytes;
  while (index < limit) {
    const auto lead = static_cast<std::uint8_t>(value[index]);
    std::size_t width = 1;
    if (lead >= 0xF0U && lead <= 0xF4U) {
      width = 4;
    } else if (lead >= 0xE0U) {
      width = lead <= 0xEFU ? 3U : 0U;
    } else if (lead >= 0xC2U) {
      width = 2;
    } else if (lead >= 0x80U) {
      width = 0;  // Stray continuation or invalid lead byte.
    }
    if (width == 0U || index + width > limit) break;
    for (std::size_t next = 1; next < width; ++next) {
      if ((static_cast<std::uint8_t>(value[index + next]) & 0xC0U) != 0x80U) {
        return index;
      }
    }
    index += width;
  }
  return index;
}

std::string source_label(std::string_view player_name) {
  if (player_name.empty()) return "IPHONE";
  const std::string upper = ascii_upper(player_name);
  struct Known {
    std::string_view needle;
    const char* label;
  };
  // Checked in order; localized Chinese names first, then the English ones
  // iOS shows when the phone language is not Chinese.
  static constexpr std::array<Known, 15> kKnown{{
      {"网易云", "NETEASE"},
      {"NETEASE", "NETEASE"},
      {"QQ音乐", "QQ MUSIC"},
      {"QQ MUSIC", "QQ MUSIC"},
      {"酷狗", "KUGOU"},
      {"酷我", "KUWO"},
      {"汽水音乐", "SODA MUSIC"},
      {"喜马拉雅", "XIMALAYA"},
      {"哔哩哔哩", "BILIBILI"},
      {"YOUTUBE MUSIC", "YT MUSIC"},
      {"SPOTIFY", "SPOTIFY"},
      {"APPLE MUSIC", "APPLE MUSIC"},
      {"播客", "PODCASTS"},
      {"PODCASTS", "PODCASTS"},
      {"BILIBILI", "BILIBILI"},
  }};
  for (const Known& known : kKnown) {
    if (contains(upper, known.needle)) return known.label;
  }
  // The built-in player is just "音乐" / "Music" on the phone.
  if (player_name == "音乐" || upper == "MUSIC") return "APPLE MUSIC";

  std::string label;
  for (const char character : upper) {
    if (static_cast<std::uint8_t>(character) >= 0x80U) {
      // A name the plate cannot draw. Say what it is instead of drawing tofu.
      return "NOW PLAYING";
    }
    if (plate_glyph(character)) label.push_back(character);
  }
  // The plate is 126 px wide; twelve condensed capitals fill it.
  constexpr std::size_t kPlateCharacters = 12;
  if (label.size() > kPlateCharacters) label.resize(kPlateCharacters);
  while (!label.empty() && label.back() == ' ') label.pop_back();
  return label.empty() ? "NOW PLAYING" : label;
}

void NowPlaying::reset() noexcept {
  *this = NowPlaying{};
}

bool NowPlaying::apply(const EntityUpdate& update) {
  const moto::ble::MediaState before = media_state();
  switch (update.entity) {
    case EntityId::Player:
      if (update.attribute == PlayerName) {
        player_name_ = update.value;
      } else if (update.attribute == PlayerPlaybackInfo) {
        has_playback_ = parse_playback_info(update.value, playback_);
        if (!has_playback_) playback_ = {};
      }
      break;
    case EntityId::Track:
      if (update.attribute == TrackTitle) {
        title_ = update.value;
      } else if (update.attribute == TrackArtist) {
        artist_ = update.value;
      } else if (update.attribute == TrackDuration) {
        if (!parse_seconds(update.value, duration_s_)) duration_s_ = 0.0F;
      }
      break;
    case EntityId::Queue:
      break;
  }
  return !(media_state() == before);
}

bool NowPlaying::set_supported_commands(CommandSet commands) noexcept {
  const bool changed = !supported_known_ || supported_ != commands;
  supported_ = commands;
  supported_known_ = true;
  return changed;
}

bool NowPlaying::supports(RemoteCommand command) const noexcept {
  return !supported_known_ || (supported_ & command_bit(command)) != 0U;
}

bool NowPlaying::playing() const noexcept {
  return has_playback_ && playback_.state != PlaybackState::Paused;
}

bool NowPlaying::command_for(moto_music_command_t key,
                             RemoteCommand& output) const {
  const auto pick = [this, &output](RemoteCommand first,
                                    RemoteCommand second) {
    if (supports(first)) {
      output = first;
      return true;
    }
    if (supports(second)) {
      output = second;
      return true;
    }
    return false;
  };
  switch (key) {
    case MOTO_MUSIC_PREVIOUS:
      // Podcasts and audiobooks offer a 15 s skip instead of a previous item.
      return pick(RemoteCommand::PreviousTrack, RemoteCommand::SkipBackward);
    case MOTO_MUSIC_NEXT:
      return pick(RemoteCommand::NextTrack, RemoteCommand::SkipForward);
    case MOTO_MUSIC_TOGGLE_PLAYBACK:
      if (supports(RemoteCommand::TogglePlayPause)) {
        output = RemoteCommand::TogglePlayPause;
        return true;
      }
      output = playing() ? RemoteCommand::Pause : RemoteCommand::Play;
      return supports(output);
    case MOTO_MUSIC_LIKE:
      output = RemoteCommand::LikeTrack;
      return supported_known_ && supports(output);
  }
  return false;
}

moto::ble::MediaState NowPlaying::media_state() const {
  moto::ble::MediaState state;
  // AMS answered, so the phone's media system is reachable even when no app
  // currently owns Now Playing.
  state.flags = moto::ble::MediaConnected;
  if (playing()) state.flags |= moto::ble::MediaPlaying;
  if (supported_known_ && supports(RemoteCommand::LikeTrack)) {
    state.flags |= moto::ble::MediaLikeAvailable;
  }
  state.source_name = source_label(player_name_);
  state.track_title = title_.empty() ? std::string("尚未播放")
                                     : clip_utf8(title_, kTitleBytes);
  state.artist_name = clip_utf8(artist_, kArtistBytes);
  const auto seconds = [](float value) -> std::uint16_t {
    if (!(value > 0.0F)) return 0;
    return value >= 65'535.0F ? 65'535U
                              : static_cast<std::uint16_t>(value + 0.5F);
  };
  state.position_s = has_playback_ ? seconds(playback_.elapsed_s) : 0U;
  state.duration_s = seconds(duration_s_);
  state.track_token = title_.empty() && artist_.empty()
      ? 0U
      : fnv1a({player_name_, title_, artist_});
  return state;
}

}  // namespace moto::ams
