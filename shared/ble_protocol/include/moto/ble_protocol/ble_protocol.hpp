#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace moto::ble {

using Bytes = std::vector<std::uint8_t>;
using TimestampMs = std::uint64_t;

constexpr std::uint8_t kFrameMagic = 0xB7;
constexpr std::uint8_t kProtocolVersion = 1;
constexpr std::uint8_t kPayloadRevision = 1;
// ESP32 is the GATT server/peripheral. The phone writes PhoneToDevice and
// subscribes to DeviceToPhone notifications.
inline constexpr char kServiceUuid[] =
    "7e57a000-b50c-4b6a-9c57-40a54e8e1000";
inline constexpr char kPhoneToDeviceUuid[] =
    "7e57a001-b50c-4b6a-9c57-40a54e8e1000";
inline constexpr char kDeviceToPhoneUuid[] =
    "7e57a002-b50c-4b6a-9c57-40a54e8e1000";
inline constexpr char kCccdUuid[] =
    "00002902-0000-1000-8000-00805f9b34fb";

enum GattRequirement : std::uint8_t {
  GattWrite = 1U << 0U,
  GattWriteWithoutResponse = 1U << 1U,
  GattNotify = 1U << 2U,
  GattCccd = 1U << 3U,
  GattEncryptedPermission = 1U << 4U,
};

constexpr std::uint8_t kPhoneToDeviceGattRequirements =
    GattWrite | GattWriteWithoutResponse | GattEncryptedPermission;
constexpr std::uint8_t kDeviceToPhoneGattRequirements =
    GattNotify | GattCccd | GattEncryptedPermission;
constexpr std::size_t kFrameHeaderSize = 10;
constexpr std::size_t kFrameCrcSize = 2;
constexpr std::size_t kFrameOverhead =
    kFrameHeaderSize + kFrameCrcSize;
constexpr std::size_t kMaxBleAttributeValueSize = 512;
constexpr std::size_t kDefaultMaxMessageSize = 4'096;
// Matches nav::kRouteViewPointCapacity. RouteGeometry is the bounded local
// display window, not the phone's complete provider route.
constexpr std::size_t kMaxRoutePointsPerChunk = 24;
constexpr std::size_t kMaxTrafficSegments = 64;
// One replaceable, rider-centred minimap window.  The complete Jinan database
// remains on the phone; these limits bound ESP32 RAM and LVGL object counts.
constexpr std::size_t kMaxMapSceneRoads = 40;
constexpr std::size_t kMaxMapSceneRoadPoints = 255;
constexpr std::size_t kMaxMapSceneBuildings = 24;
constexpr std::size_t kMaxMapSceneBuildingPoints = 192;

struct ByteView {
  const std::uint8_t* data = nullptr;
  std::size_t size = 0;

  constexpr ByteView() = default;
  constexpr ByteView(const std::uint8_t* bytes, std::size_t byte_count)
      : data(bytes), size(byte_count) {}
  explicit ByteView(const Bytes& bytes) : data(bytes.data()), size(bytes.size()) {}
};

enum class Error : std::uint8_t {
  None = 0,
  InvalidArgument,
  FrameTooShort,
  FrameTooLarge,
  BadMagic,
  UnsupportedVersion,
  ReservedFlags,
  SequenceZero,
  InvalidFragment,
  LengthMismatch,
  CrcMismatch,
  MessageTooLarge,
  MissingStart,
  UnexpectedFragment,
  FragmentConflict,
  StaleSequence,
  ReassemblyTimeout,
  UnknownMessageType,
  UnsupportedPayloadRevision,
  TruncatedPayload,
  TrailingPayload,
  InvalidEnum,
  OutOfRange,
  StringTooLong,
  TooManyItems,
  InvalidVarint,
};

const char* to_string(Error error) noexcept;

template <typename T>
struct Result {
  T value{};
  Error error = Error::None;
  std::size_t offset = 0;

  [[nodiscard]] constexpr bool ok() const noexcept {
    return error == Error::None;
  }
};

enum class MessageType : std::uint8_t {
  ConnectionStatus = 0x01,
  Heartbeat = 0x02,
  Ack = 0x03,
  NavigationSnapshot = 0x10,
  RouteGeometry = 0x11,
  TrafficDeviation = 0x12,
  MediaState = 0x13,
  MapScene = 0x14,
  AttitudeSample = 0x15,
  HomeLocation = 0x16,
  SettingsState = 0x17,
  GnssInput = 0x18,
  PerformanceRun = 0x19,
  WifiConfig = 0x1A,
  WifiStatus = 0x1B,
  OtaCommand = 0x1C,
  OtaStatus = 0x1D,
  FirmwareInfo = 0x1E,
  DeviceCommand = 0x20,
};

enum FrameFlag : std::uint8_t {
  FrameStart = 1U << 0U,
  FrameEnd = 1U << 1U,
  AckRequested = 1U << 2U,
  Urgent = 1U << 3U,
};

constexpr std::uint8_t kKnownFrameFlags =
    FrameStart | FrameEnd | AckRequested | Urgent;
constexpr std::uint8_t kApplicationFrameFlags = AckRequested | Urgent;

struct Frame {
  std::uint8_t version = kProtocolVersion;
  MessageType type = MessageType::Heartbeat;
  std::uint8_t flags = 0;
  std::uint16_t sequence = 0;
  std::uint16_t fragment_offset = 0;
  std::uint16_t message_length = 0;
  Bytes payload;
};

using BytesResult = Result<Bytes>;
using FrameResult = Result<Frame>;
using FramesResult = Result<std::vector<Bytes>>;

[[nodiscard]] std::uint16_t crc16_ccitt_false(ByteView bytes) noexcept;
[[nodiscard]] BytesResult encode_frame(const Frame& frame);
[[nodiscard]] FrameResult decode_frame(ByteView bytes);

// max_frame_size is the complete GATT characteristic value size, not the ATT
// MTU. For an ATT MTU of 23, pass 20. Start/End are assigned by this function;
// application_flags may contain only AckRequested and Urgent.
[[nodiscard]] FramesResult fragment_message(
    MessageType type,
    std::uint16_t sequence,
    ByteView payload,
    std::size_t max_frame_size,
    std::uint8_t application_flags = 0,
    std::uint8_t version = kProtocolVersion);

[[nodiscard]] bool sequence_is_newer(std::uint16_t candidate,
                                     std::uint16_t reference) noexcept;

class SequenceGenerator {
 public:
  explicit SequenceGenerator(std::uint16_t first = 1) noexcept;
  [[nodiscard]] std::uint16_t next() noexcept;
  void reset(std::uint16_t first = 1) noexcept;

 private:
  std::uint16_t next_ = 1;
};

enum class ReassemblyState : std::uint8_t {
  InProgress,
  Complete,
  DuplicateFragment,
  DuplicateMessage,
  Error,
};

struct ReassembledMessage {
  MessageType type = MessageType::Heartbeat;
  std::uint8_t version = kProtocolVersion;
  std::uint8_t flags = 0;
  std::uint16_t sequence = 0;
  Bytes payload;
};

struct ReassemblyResult {
  ReassemblyState state = ReassemblyState::Error;
  Error error = Error::None;
  std::size_t error_offset = 0;
  bool dropped_incomplete = false;
  ReassembledMessage message;

  [[nodiscard]] bool complete() const noexcept {
    return state == ReassemblyState::Complete;
  }
};

struct ReassemblerConfig {
  std::size_t max_message_size = kDefaultMaxMessageSize;
  TimestampMs fragment_timeout_ms = 1'000;
  bool enforce_monotonic_sequence = true;
};

// BLE preserves notification/write order. Reassembler therefore accepts one
// in-flight logical message per direction and rejects gaps. A newer START may
// supersede an incomplete message so a lost fragment cannot wedge the link.
class Reassembler {
 public:
  explicit Reassembler(ReassemblerConfig config = {});

  [[nodiscard]] ReassemblyResult push(ByteView encoded_frame,
                                      TimestampMs now_ms);
  [[nodiscard]] bool expire(TimestampMs now_ms) noexcept;
  void reset() noexcept;

 private:
  void clear_active() noexcept;

  ReassemblerConfig config_;
  bool active_ = false;
  bool has_last_sequence_ = false;
  std::uint8_t active_version_ = kProtocolVersion;
  MessageType active_type_ = MessageType::Heartbeat;
  std::uint8_t active_application_flags_ = 0;
  std::uint16_t active_sequence_ = 0;
  std::uint16_t active_message_length_ = 0;
  std::size_t expected_offset_ = 0;
  TimestampMs last_fragment_at_ms_ = 0;
  std::uint16_t last_sequence_ = 0;
  Bytes buffer_;
};

class LinkWatchdog {
 public:
  explicit LinkWatchdog(TimestampMs timeout_ms = 3'000) noexcept;

  void set_timeout(TimestampMs timeout_ms) noexcept;
  void note_valid_frame(TimestampMs now_ms) noexcept;
  void reset() noexcept;
  [[nodiscard]] bool armed() const noexcept;
  [[nodiscard]] bool expired(TimestampMs now_ms) const noexcept;
  [[nodiscard]] TimestampMs deadline_ms() const noexcept;

 private:
  TimestampMs timeout_ms_ = 3'000;
  TimestampMs last_rx_ms_ = 0;
  bool armed_ = false;
};

enum class EndpointRole : std::uint8_t {
  Phone = 1,
  Device = 2,
};

enum class ConnectionState : std::uint8_t {
  Starting = 0,
  Ready = 1,
  Degraded = 2,
  Closing = 3,
};

enum Capability : std::uint32_t {
  CapabilityNavigation = 1U << 0U,
  CapabilityRouteGeometry = 1U << 1U,
  CapabilityTraffic = 1U << 2U,
  CapabilityMediaState = 1U << 3U,
  CapabilityTouchCommands = 1U << 4U,
  CapabilityMusicCommands = 1U << 5U,
  CapabilityCommandAck = 1U << 6U,
  CapabilityMapScene = 1U << 7U,
  CapabilityAttitudeTelemetry = 1U << 8U,
  // The device can draw a saved home on its minimap. The phone only sends
  // HomeLocation to a peer that advertises this, so an older firmware is not
  // sent a message type it has never seen.
  CapabilityHomeLocation = 1U << 9U,
  // The device stores its own presentation settings (theme, units, scale) and
  // echoes them back. Without this bit the phone keeps its settings to itself
  // rather than sending a type the firmware would reject.
  CapabilityDeviceSettings = 1U << 10U,
  // The device understands NavigationHeadingCompass. Without this bit the
  // phone never sets that flag, which an older firmware would reject.
  CapabilityCompassHeading = 1U << 11U,
  // The device takes GnssInput, the phone's speed stamped with when the fix
  // was taken. Without this bit the phone sends none and the device ages the
  // snapshot speed from its arrival.
  CapabilityGnssInput = 1U << 12U,
  // Phone->device: the phone shows the device's 0-100 runs. Only a phone that
  // advertises this is sent PerformanceRun, so an older app never sees 0x19.
  CapabilityPerformanceRun = 1U << 13U,
  // Both directions. The device advertises it when it takes WifiConfig and
  // reports WifiStatus; the phone advertises it when it shows WifiStatus. The
  // phone sends WifiConfig only to a device with this bit, and the device
  // sends WifiStatus only to a phone with it.
  CapabilityWifiHotspot = 1U << 14U,
  // Both directions. The device advertises it when it takes OtaCommand and
  // reports OtaStatus; the phone advertises it when it offers a firmware
  // update. Gated like the wifi bit, so a device without an OTA path is
  // never sent 0x1C.
  CapabilityOta = 1U << 15U,
  // Both directions. The device advertises it when it reports FirmwareInfo;
  // the phone advertises it when it reads it, and only such a phone is sent
  // 0x1E.
  CapabilityFirmwareInfo = 1U << 16U,
};

struct ConnectionStatus {
  EndpointRole role = EndpointRole::Phone;
  ConnectionState state = ConnectionState::Starting;
  std::uint8_t minimum_version = kProtocolVersion;
  std::uint8_t maximum_version = kProtocolVersion;
  std::uint32_t capabilities = 0;
  std::uint32_t session_id = 0;
  std::uint16_t max_frame_size = 20;
  std::uint16_t heartbeat_interval_ms = 1'000;

  bool operator==(const ConnectionStatus& rhs) const noexcept;
};

struct Heartbeat {
  std::uint32_t session_id = 0;
  std::uint32_t monotonic_ms = 0;
  std::uint16_t status_flags = 0;

  bool operator==(const Heartbeat& rhs) const noexcept;
};

enum class AckStatus : std::uint8_t {
  Ok = 0,
  Unsupported = 1,
  InvalidState = 2,
  Failed = 3,
  Duplicate = 4,
};

struct Ack {
  std::uint16_t acknowledged_sequence = 0;
  AckStatus status = AckStatus::Ok;
  std::uint16_t command_id = 0;

  bool operator==(const Ack& rhs) const noexcept;
};

enum class NavigationState : std::uint8_t {
  Idle = 0,
  Acquiring = 1,
  Planning = 2,
  Navigating = 3,
  Rerouting = 4,
  Arrived = 5,
};

enum class Maneuver : std::uint8_t {
  Unknown = 0,
  Continue = 1,
  SlightLeft = 2,
  Left = 3,
  SharpLeft = 4,
  UTurnLeft = 5,
  SlightRight = 6,
  Right = 7,
  SharpRight = 8,
  UTurnRight = 9,
  Roundabout = 10,
  Exit = 11,
  Arrive = 12,
};

enum class TrafficLevel : std::uint8_t {
  Unknown = 0,
  FreeFlow = 1,
  Slow = 2,
  Congested = 3,
  Severe = 4,
};

enum class NetworkState : std::uint8_t {
  Offline = 0,
  Connecting = 1,
  Online = 2,
};

enum class DisplayPage : std::uint8_t {
  Navigation = 0,
  Speed = 1,
  Compass = 2,
  Music = 3,
  Performance = 4,
  Lean = 5,
};

enum NavigationFlag : std::uint16_t {
  NavigationHasDestination = 1U << 0U,
  NavigationHasFix = 1U << 1U,
  NavigationGnssStale = 1U << 2U,
  NavigationOffRoute = 1U << 3U,
  NavigationHasNextManeuver = 1U << 4U,
  NavigationRouteRequestInFlight = 1U << 5U,
  NavigationTrafficRequestInFlight = 1U << 6U,
  NavigationHasRouteView = 1U << 7U,
  // heading_cdeg is the phone's compass (magnetometer) heading, not a GNSS
  // course. A course exists only while moving; parked, the phone sends its
  // compass instead, and only to a peer that advertises
  // CapabilityCompassHeading.
  NavigationHeadingCompass = 1U << 8U,
};

constexpr std::uint16_t kKnownNavigationFlags =
    NavigationHasDestination | NavigationHasFix | NavigationGnssStale |
    NavigationOffRoute | NavigationHasNextManeuver |
    NavigationRouteRequestInFlight | NavigationTrafficRequestInFlight |
    NavigationHasRouteView | NavigationHeadingCompass;

struct NavigationSnapshot {
  NavigationState state = NavigationState::Idle;
  NetworkState network = NetworkState::Offline;
  DisplayPage display_page = DisplayPage::Navigation;
  Maneuver maneuver = Maneuver::Unknown;
  TrafficLevel traffic = TrafficLevel::Unknown;
  std::uint16_t flags = 0;
  std::uint32_t route_token = 0;
  std::uint32_t route_generation = 0;
  std::uint32_t maneuver_id = 0;
  std::uint32_t distance_to_maneuver_m = 0;
  std::uint32_t remaining_distance_m = 0;
  std::uint32_t remaining_duration_s = 0;
  std::uint32_t route_progress_m = 0;
  std::uint32_t total_distance_m = 0;
  std::uint16_t speed_deci_kph = 0;
  std::uint16_t speed_limit_kph = 0;
  std::uint16_t heading_cdeg = 0;
  std::uint16_t accuracy_dm = 0;
  std::uint16_t cross_track_dm = 0;
  std::uint8_t roundabout_exit = 0;
  std::string road_name;
  std::string instruction;

  bool operator==(const NavigationSnapshot& rhs) const noexcept;
};

enum class CoordinateSystem : std::uint8_t {
  Wgs84 = 1,
  Gcj02 = 2,
};

struct GeoPointE6 {
  std::int32_t latitude_e6 = 0;
  std::int32_t longitude_e6 = 0;

  bool operator==(const GeoPointE6& rhs) const noexcept;
};

struct RouteGeometry {
  CoordinateSystem coordinate_system = CoordinateSystem::Gcj02;
  std::uint32_t route_token = 0;
  std::uint32_t route_generation = 0;
  std::uint16_t chunk_index = 0;
  std::uint16_t chunk_count = 1;
  std::uint16_t first_point_index = 0;
  std::uint16_t total_point_count = 0;
  GeoPointE6 view_origin;
  std::vector<GeoPointE6> points;

  bool operator==(const RouteGeometry& rhs) const noexcept;
};

enum TrafficDeviationFlag : std::uint16_t {
  TrafficChanged = 1U << 0U,
  OffRoute = 1U << 1U,
  Rerouting = 1U << 2U,
  RouteInvalidated = 1U << 3U,
};

constexpr std::uint16_t kKnownTrafficDeviationFlags =
    TrafficChanged | OffRoute | Rerouting | RouteInvalidated;

struct TrafficSegment {
  std::uint32_t start_offset_m = 0;
  std::uint32_t length_m = 0;
  TrafficLevel level = TrafficLevel::Unknown;

  bool operator==(const TrafficSegment& rhs) const noexcept;
};

struct TrafficDeviation {
  std::uint32_t route_token = 0;
  std::uint32_t route_generation = 0;
  std::uint16_t flags = 0;
  std::uint32_t observed_at_ms = 0;
  std::uint32_t remaining_duration_s = 0;
  std::uint16_t cross_track_dm = 0;
  std::vector<TrafficSegment> segments;

  bool operator==(const TrafficDeviation& rhs) const noexcept;
};

enum MediaFlag : std::uint8_t {
  MediaConnected = 1U << 0U,
  MediaPlaying = 1U << 1U,
  MediaLikeAvailable = 1U << 2U,
  MediaLiked = 1U << 3U,
};

constexpr std::uint8_t kKnownMediaFlags =
    MediaConnected | MediaPlaying | MediaLikeAvailable | MediaLiked;

struct MediaState {
  std::uint8_t flags = 0;
  std::uint32_t track_token = 0;
  std::uint16_t position_s = 0;
  std::uint16_t duration_s = 0;
  std::string source_name;
  std::string track_title;
  std::string artist_name;

  bool operator==(const MediaState& rhs) const noexcept;
};

// A low-frequency, atomically replaceable GTA-style local map window.  Points
// use GCJ-02 so they align with the AMap route already rendered by the device.
// The phone clips and simplifies its offline database before encoding this
// message; the ESP32 never performs a city-wide spatial query.
enum class MapRoadClass : std::uint8_t {
  Motorway = 0,
  Primary = 1,
  Secondary = 2,
  Residential = 3,
  Service = 4,
  Other = 5,
};

enum class MapBuildingClass : std::uint8_t {
  Generic = 0,
  Landmark = 1,
  Parking = 2,
};

struct MapRoadPolyline {
  MapRoadClass road_class = MapRoadClass::Other;
  std::vector<GeoPointE6> points;

  bool operator==(const MapRoadPolyline& rhs) const noexcept;
};

struct MapBuildingFootprint {
  MapBuildingClass building_class = MapBuildingClass::Generic;
  // The closing point is implicit and must not repeat points.front().
  std::vector<GeoPointE6> points;

  bool operator==(const MapBuildingFootprint& rhs) const noexcept;
};

struct MapScene {
  CoordinateSystem coordinate_system = CoordinateSystem::Gcj02;
  std::uint32_t scene_revision = 0;
  GeoPointE6 view_origin;
  std::uint16_t radius_m = 0;
  std::vector<MapRoadPolyline> roads;
  std::vector<MapBuildingFootprint> buildings;

  bool operator==(const MapScene& rhs) const noexcept;
};

// The saved home address, sent by the phone when it changes and after every
// handshake. It is deliberately its own message rather than a MapScene field:
// the blip must survive a scene that has no map window (no offline tiles) and
// the value is not tied to a route or a scene revision. Devices that do not
// advertise CapabilityHomeLocation reject the type and keep working.
struct HomeLocation {
  CoordinateSystem coordinate_system = CoordinateSystem::Gcj02;
  // Monotonic; a reordered or replayed message must not resurrect an old
  // address. Zero is only used by "never received".
  std::uint32_t revision = 0;
  bool has_home = false;
  GeoPointE6 position;

  bool operator==(const HomeLocation& rhs) const noexcept;
};

// Presentation settings, written by the phone and owned by the device. The
// device is the authority for what it displays: a phone write is a request,
// the device applies it and echoes the applied state, and a receiver ignores
// any revision it has already applied. That makes the echo terminate and stops
// a reconnect from resurrecting an old theme. Both endpoints may originate a
// revision; the sender must use a value strictly above the highest it has
// seen. Sending this to a peer without CapabilityDeviceSettings is a phone
// bug, not a protocol fallback: an older firmware has never seen 0x17.
//
// The numbering matches moto::device::DeviceSettings, which owns the NVS blob
// and the UI. device_settings_tests pins the two together.
enum class SettingsTheme : std::uint8_t {
  Official = 0,
  Auto = 1,
  Day = 2,
  Night = 3,
};

enum class SettingsUnits : std::uint8_t {
  Kph = 0,
  Mph = 1,
};

// Mirrors moto::device::PageVisibility. Navigation cannot be hidden: a state
// without it is refused rather than silently corrected.
enum SettingsPage : std::uint8_t {
  SettingsPageNavigation = 1U << 0U,
  SettingsPagePerformance = 1U << 1U,
  SettingsPageLean = 1U << 2U,
  SettingsPageSpeed = 1U << 3U,
  SettingsPageCompass = 1U << 4U,
  SettingsPageMusic = 1U << 5U,
};

inline constexpr std::uint8_t kSettingsAllPages =
    SettingsPageNavigation | SettingsPagePerformance | SettingsPageLean |
    SettingsPageSpeed | SettingsPageCompass | SettingsPageMusic;

// Calibration offset applied to the measured Lean, in centidegrees.
inline constexpr std::int16_t kMaxSettingsLeanOffsetCdeg = 30 * 100;

// payload_revision + theme + brightness + units + lean scale + performance
// target + page mask + lean offset (u16) + ambient night + show buildings +
// settings revision (u32).
//
// The two display bytes took this from 14 to 16 bytes without a payload
// revision bump. That is a deliberate, coordinated break: both endpoints ship
// together in this project, and a stale decoder rejects the trailing bytes
// loudly (Error::TrailingBytes) instead of applying a settings set with the
// wrong meaning. A revision bump would have rejected every other message too.
inline constexpr std::size_t kSettingsPayloadSize = 16;

struct SettingsState {
  SettingsTheme theme = SettingsTheme::Auto;
  std::uint8_t brightness_percent = 80;
  SettingsUnits units = SettingsUnits::Kph;
  std::uint8_t lean_full_scale_deg = 45;
  std::uint16_t performance_target_kph = 100;
  std::uint8_t visible_pages = kSettingsAllPages;
  std::int16_t lean_zero_offset_cdeg = 0;
  // The phone's day/night fact, not a preference: the device has no clock and
  // no light sensor, so SettingsTheme::Auto can only resolve against this.
  // The device echoes it unchanged, so a phone sees its own value back.
  bool ambient_night = false;
  // Building footprints are a readability decision, not scenery. Off by
  // default: at 466 px they crowd out the roads that matter while moving.
  bool show_buildings = false;
  // Monotonic; zero is reserved for "never seen" and is not encodable.
  std::uint32_t revision = 0;

  bool operator==(const SettingsState& rhs) const noexcept;
};

enum class DeviceCommandKind : std::uint8_t {
  PageSelected = 0,
  Tap = 1,
  LongPress = 2,
  SwipeLeft = 3,
  SwipeRight = 4,
  SwipeUp = 5,
  SwipeDown = 6,
  MusicPrevious = 16,
  MusicTogglePlayback = 17,
  MusicNext = 18,
  MusicLike = 19,
  // Tap on the device's home glyph: the phone plans a route to the saved home
  // address. The device never stores the address, only the intent.
  HomeShortcut = 20,
};

constexpr std::uint16_t kUnknownTouchCoordinate = 0xFFFFU;

struct DeviceCommand {
  DeviceCommandKind kind = DeviceCommandKind::PageSelected;
  std::uint16_t command_id = 0;
  DisplayPage page = DisplayPage::Navigation;
  std::uint16_t x = kUnknownTouchCoordinate;
  std::uint16_t y = kUnknownTouchCoordinate;
  std::uint32_t event_time_ms = 0;

  bool operator==(const DeviceCommand& rhs) const noexcept;
};

// Device->phone, 10 Hz fire-and-forget telemetry. Only valid estimates are
// encoded; the payload is intentionally fixed-size and Ack-free.
struct AttitudeSample {
  std::uint32_t monotonic_ms = 0;
  std::int16_t lean_cdeg = 0;
  std::int16_t pitch_cdeg = 0;
  std::int16_t longitudinal_mg = 0;
  std::int16_t lateral_mg = 0;

  bool operator==(const AttitudeSample& rhs) const noexcept;
};

enum GnssInputFlag : std::uint8_t {
  GnssInputHasSpeed = 1U << 0U,
  GnssInputHasSpeedAccuracy = 1U << 1U,
};

constexpr std::uint8_t kKnownGnssInputFlags =
    GnssInputHasSpeed | GnssInputHasSpeedAccuracy;
inline constexpr std::size_t kGnssInputPayloadSize = 12;
// 360 km/h. A larger value is a broken fix, not a motorcycle.
inline constexpr std::uint16_t kMaxGnssSpeedCmps = 10'000;

// Phone->device, one per Core Location fix, fire-and-forget. The snapshot's
// speed carries no time, so a fix that waited in the phone's send queue looked
// fresh when it arrived. This one says when the fix was taken, in the phone's
// own monotonic clock: `phone_monotonic_ms` is stamped when the message is
// encoded and `fix_age_ms` is how long before that the fix was taken. The
// device maps the phone clock onto its own from the smallest
// (arrival - phone_monotonic_ms) it has seen recently, so a delay in the queue
// shows up as age instead of disappearing. Wall clocks are never compared
// across devices (DESIGN_V2 §12). Only sent to a peer that advertises
// CapabilityGnssInput. Fields without their flag must be zero.
struct GnssInput {
  std::uint8_t flags = 0;
  std::uint32_t phone_monotonic_ms = 0;
  std::uint16_t fix_age_ms = 0;
  std::uint16_t speed_cmps = 0;
  std::uint16_t speed_accuracy_cmps = 0;

  bool operator==(const GnssInput& rhs) const noexcept;
};

enum class PerformanceRunState : std::uint8_t {
  // Waiting at a standstill; the next roll-away starts the clock.
  Armed = 0,
  Running = 1,
  // The target was reached; elapsed_ms is the time.
  Finished = 2,
};

inline constexpr std::size_t kPerformanceRunPayloadSize = 16;

// Device->phone. The round screen owns the 0-100 clock and the phone shows
// what it measured, so the two numbers can never disagree. Sent on every state
// change and after each handshake, to a phone that advertises
// CapabilityPerformanceRun; no Ack, the next state replaces a lost one. While
// Running the phone ticks its own display from elapsed_ms at arrival.
struct PerformanceRun {
  PerformanceRunState state = PerformanceRunState::Armed;
  std::uint16_t target_kph = 100;
  // Bumps when a run starts; zero before the first one since power-on.
  std::uint32_t run_id = 0;
  // Running: time so far when sent. Finished: the run's time. Armed: zero.
  std::uint32_t elapsed_ms = 0;
  // Best time since power-on; zero when there is none.
  std::uint32_t best_ms = 0;

  bool operator==(const PerformanceRun& rhs) const noexcept;
};

// The device joins the iPhone's Personal Hotspot as a station, on demand
// (WAVESHARE_OPTIMIZATION_PLAN §4.3). BLE stays the control plane: WiFi is a
// data plane that is off unless a task needs it, and nothing depends on it.
enum class WifiPolicy : std::uint8_t {
  // Forget the stored network and never bring WiFi up. SSID and PSK empty.
  Disabled = 0,
  // Keep the network; join it only while a task needs it.
  OnDemand = 1,
};

// 802.11 SSID limit and the WPA2 passphrase range. A 64-byte PSK is the raw
// key in hex; anything shorter than 8 bytes is not a WPA2 passphrase.
inline constexpr std::size_t kMaxWifiSsidBytes = 32;
inline constexpr std::size_t kMinWifiPskBytes = 8;
inline constexpr std::size_t kMaxWifiPskBytes = 64;

// Phone->device, sent with AckRequested when the rider saves the hotspot on
// the phone (iOS cannot read its own hotspot name or password, so the rider
// types them once). The device accepts it only on an encrypted, bonded link;
// anything else is answered InvalidState and nothing is stored. The PSK is a
// secret: the device keeps it in NVS and never logs it, and nothing echoes it.
// Only sent to a peer that advertises CapabilityWifiHotspot.
struct WifiConfig {
  WifiPolicy policy = WifiPolicy::OnDemand;
  std::string ssid;
  std::string psk;

  bool operator==(const WifiConfig& rhs) const noexcept;
};

enum class WifiLinkState : std::uint8_t {
  // Radio off and its memory returned. The normal state.
  Off = 0,
  // Started, joining the network or waiting for DHCP.
  Connecting = 1,
  // Associated with an address; ipv4 and rssi are valid.
  Connected = 2,
  // The last attempt ended without an address; reason says why. The radio is
  // already off again.
  Failed = 3,
};

enum class WifiFailure : std::uint8_t {
  None = 0,
  // No network stored, or the policy is Disabled.
  NoCredentials = 1,
  // The SSID was not seen: hotspot off, or 5 GHz only ("Maximize
  // Compatibility" off on iPhone 12 and later).
  NetworkNotFound = 2,
  AuthFailed = 3,
  // Associated but no DHCP address in time, or the whole attempt timed out.
  Timeout = 4,
  // esp_wifi_init/start refused, usually for want of internal RAM.
  NoMemory = 5,
  // Joined, then lost the network.
  Disconnected = 6,
};

enum WifiStatusFlag : std::uint8_t {
  // The device has a network stored (the SSID only; the PSK never leaves it).
  WifiStatusHasCredentials = 1U << 0U,
};

constexpr std::uint8_t kKnownWifiStatusFlags = WifiStatusHasCredentials;
inline constexpr std::size_t kWifiStatusPayloadSize = 9;

// Device->phone, on every state change, every few seconds while connected
// (RSSI), and after each handshake, to a phone that advertises
// CapabilityWifiHotspot. No Ack: the next status replaces a lost one.
// `ipv4` is in dotted order, ipv4[0] being the first octet; it and `rssi_dbm`
// are zero unless Connected, and `failure` is None unless Failed.
struct WifiStatus {
  WifiLinkState state = WifiLinkState::Off;
  WifiFailure failure = WifiFailure::None;
  std::uint8_t flags = 0;
  std::array<std::uint8_t, 4> ipv4{};
  std::int8_t rssi_dbm = 0;

  bool operator==(const WifiStatus& rhs) const noexcept;
};

// Firmware update over the hotspot (WAVESHARE_OPTIMIZATION_PLAN §4.4, W3).
//
// The image never crosses BLE: the phone either serves it from the hotspot it
// already shares with the device, or hands over a release URL, and the device
// fetches it over that link. The device writes the idle OTA slot, verifies
// SHA-256 against the command, and only then points the bootloader at it. A
// build that comes up but never completes a handshake is rolled back by the
// bootloader (development builds carry tlog; release overlay builds do not).
inline constexpr std::size_t kOtaSha256Bytes = 32;
// "https://" plus a host and a path; shorter is not a URL the device takes.
inline constexpr std::size_t kMinOtaUrlBytes = 10;
inline constexpr std::size_t kMaxOtaUrlBytes = 200;
// One OTA slot is 7 MiB. Below a megabyte is not an image, and a size past
// the slot is refused before a byte is written. Zero means "not stated".
inline constexpr std::uint32_t kMinOtaImageBytes = 1U * 1024U * 1024U;
inline constexpr std::uint32_t kMaxOtaImageBytes = 7U * 1024U * 1024U;

// Phone->device, sent with AckRequested. Accepted only on an encrypted,
// bonded link; anything else is answered InvalidState and nothing is written.
// Only sent to a peer that advertises CapabilityOta. The device needs the
// hotspot up (W1) for the fetch; the phone starts it from the same screen.
struct OtaCommand {
  // Absolute http:// or https:// URL the device fetches over the hotspot.
  std::string url;
  // SHA-256 of the whole image, as sent. A hash the payload cannot influence
  // is the point: the device compares after the last byte and before the
  // bootloader switch.
  std::array<std::uint8_t, kOtaSha256Bytes> sha256{};
  // Image size when the phone knows it; zero means the device only learns it
  // from the response. Used, when set, to refuse an image that cannot fit and
  // to drive the progress bar from the first byte.
  std::uint32_t size_bytes = 0;

  bool operator==(const OtaCommand& rhs) const noexcept;
};

enum class OtaState : std::uint8_t {
  // No update has run since power-on.
  Idle = 0,
  // Fetching and writing the idle slot.
  Downloading = 1,
  // All bytes are in; SHA-256 and image header are being checked.
  Verifying = 2,
  // Verified and written. The device reboots into it shortly after this.
  Ready = 3,
  // The update stopped. failure says why; the slot was discarded.
  Failed = 4,
};

enum class OtaFailure : std::uint8_t {
  None = 0,
  // The radio is off, or the hotspot never gave an address.
  NoWifi = 1,
  // No hotspot credentials stored: WifiConfig comes first.
  NoCredentials = 2,
  // The URL is not absolute http(s), or the phone asked for something the
  // device cannot honour (scheme, port, length).
  BadUrl = 3,
  // DNS, connect, TLS, HTTP status or a broken body.
  Network = 4,
  // SHA-256 does not match the command. The image was not activated.
  HashMismatch = 5,
  // Right hash, but `esp_ota_end` refused it as an image for this chip.
  ImageRejected = 6,
  // No room for the TLS/download buffers, or the slot would not open.
  NoMemory = 7,
  // Another update is already running.
  Busy = 8,
  // The image is larger than the slot, or larger than the advertised size.
  TooLarge = 9,
};

// Device->phone, on every state change and at most twice a second while
// downloading, to a phone that advertises CapabilityOta. No Ack: the next
// status replaces a lost one. bytes_received is what the device wrote, which
// is what the progress bar shows; progress_percent is 0..100 and is zero
// while the total is unknown.
struct OtaStatus {
  OtaState state = OtaState::Idle;
  OtaFailure failure = OtaFailure::None;
  std::uint8_t progress_percent = 0;
  std::uint32_t bytes_received = 0;

  bool operator==(const OtaStatus& rhs) const noexcept;
};

inline constexpr std::size_t kMaxFirmwareVersionBytes = 31;
inline constexpr std::size_t kMaxFirmwareBuildBytes = 47;

// Device->phone, after each handshake, to a phone that advertises
// CapabilityFirmwareInfo. No Ack: the next handshake sends it again. Both
// strings are printable ASCII without spaces. `version` is the firmware's
// own version (ESP-IDF PROJECT_VER). `build` is the commit the image was
// built from, as `git rev-parse --short HEAD`, with "-dirty" for a tree that
// had uncommitted changes; it is what the phone compares with a release
// manifest's git_commit to tell whether that release is already installed.
struct FirmwareInfo {
  std::string version;
  std::string build;

  bool operator==(const FirmwareInfo& rhs) const noexcept;
};

using Message = std::variant<ConnectionStatus,                             Heartbeat,
                             Ack,
                             NavigationSnapshot,
                             RouteGeometry,
                             TrafficDeviation,
                             MediaState,
                             MapScene,
                             AttitudeSample,
                             HomeLocation,
                             SettingsState,
                             GnssInput,
                             PerformanceRun,
                             WifiConfig,
                             WifiStatus,
                             OtaCommand,
                             OtaStatus,
                             DeviceCommand,
                             FirmwareInfo>;

using MessageResult = Result<Message>;

[[nodiscard]] MessageType message_type(const Message& message) noexcept;
[[nodiscard]] BytesResult encode_message(const Message& message);
[[nodiscard]] MessageResult decode_message(MessageType type,
                                           ByteView payload);

// Stable non-cryptographic association token for the existing string
// RouteBundle::route_id. Both endpoints hash the exact UTF-8 bytes.
[[nodiscard]] std::uint32_t route_token(std::string_view route_id) noexcept;

}  // namespace moto::ble
