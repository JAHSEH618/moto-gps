#import "MotoBLEProtocolBridge.h"
#import "../Navigation/MotoNavCoreBridge.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <string>

#include "moto/ble_protocol/ble_protocol.hpp"

namespace {

NSString *const kMotoBLEErrorDomain = @"org.example.motogps.ble-protocol";

NSError *ProtocolError(moto::ble::Error error, std::size_t offset = 0) {
  NSString *message = [NSString stringWithFormat:@"BLE v1: %s (offset %zu)",
                                                moto::ble::to_string(error),
                                                offset];
  return [NSError errorWithDomain:kMotoBLEErrorDomain
                             code:static_cast<NSInteger>(error)
                         userInfo:@{NSLocalizedDescriptionKey : message}];
}

std::int32_t CoordinateE6(double value) {
  constexpr double minimum = static_cast<double>(INT32_MIN);
  constexpr double maximum = static_cast<double>(INT32_MAX);
  return static_cast<std::int32_t>(
      std::clamp(std::round(value * 1'000'000.0), minimum, maximum));
}

struct CodecStorage {
  explicit CodecStorage(std::size_t frame_size)
      : maximum_frame_size(std::clamp<std::size_t>(
            frame_size, moto::ble::kFrameOverhead + 1,
            moto::ble::kMaxBleAttributeValueSize)),
        reassembler(moto::ble::ReassemblerConfig{}) {}

  std::size_t maximum_frame_size;
  moto::ble::SequenceGenerator sequence;
  std::uint16_t last_encoded_sequence = 0;
  moto::ble::Reassembler reassembler;
};

moto::ble::NavigationState NavigationStateFromName(NSString *name) {
  if ([name isEqualToString:@"acquiring"]) return moto::ble::NavigationState::Acquiring;
  if ([name isEqualToString:@"planning"]) return moto::ble::NavigationState::Planning;
  if ([name isEqualToString:@"navigating"]) return moto::ble::NavigationState::Navigating;
  if ([name isEqualToString:@"rerouting"]) return moto::ble::NavigationState::Rerouting;
  if ([name isEqualToString:@"arrived"]) return moto::ble::NavigationState::Arrived;
  return moto::ble::NavigationState::Idle;
}

moto::ble::NetworkState NetworkStateFromName(NSString *name) {
  if ([name isEqualToString:@"connecting"]) return moto::ble::NetworkState::Connecting;
  if ([name isEqualToString:@"online"]) return moto::ble::NetworkState::Online;
  return moto::ble::NetworkState::Offline;
}

moto::ble::DisplayPage DisplayPageFromName(NSString *name) {
  if ([name isEqualToString:@"performance"]) return moto::ble::DisplayPage::Performance;
  if ([name isEqualToString:@"lean"]) return moto::ble::DisplayPage::Lean;
  if ([name isEqualToString:@"speed"]) return moto::ble::DisplayPage::Speed;
  if ([name isEqualToString:@"compass"]) return moto::ble::DisplayPage::Compass;
  if ([name isEqualToString:@"music"]) return moto::ble::DisplayPage::Music;
  return moto::ble::DisplayPage::Navigation;
}

moto::ble::Maneuver ManeuverFromName(NSString *name) {
  if ([name isEqualToString:@"continue"]) return moto::ble::Maneuver::Continue;
  if ([name isEqualToString:@"slight_left"]) return moto::ble::Maneuver::SlightLeft;
  if ([name isEqualToString:@"left"]) return moto::ble::Maneuver::Left;
  if ([name isEqualToString:@"sharp_left"]) return moto::ble::Maneuver::SharpLeft;
  if ([name isEqualToString:@"u_turn_left"]) return moto::ble::Maneuver::UTurnLeft;
  if ([name isEqualToString:@"slight_right"]) return moto::ble::Maneuver::SlightRight;
  if ([name isEqualToString:@"right"]) return moto::ble::Maneuver::Right;
  if ([name isEqualToString:@"sharp_right"]) return moto::ble::Maneuver::SharpRight;
  if ([name isEqualToString:@"u_turn_right"]) return moto::ble::Maneuver::UTurnRight;
  if ([name isEqualToString:@"roundabout"]) return moto::ble::Maneuver::Roundabout;
  if ([name isEqualToString:@"exit"]) return moto::ble::Maneuver::Exit;
  if ([name isEqualToString:@"arrive"]) return moto::ble::Maneuver::Arrive;
  return moto::ble::Maneuver::Unknown;
}

moto::ble::TrafficLevel TrafficFromName(NSString *name) {
  if ([name isEqualToString:@"free_flow"]) return moto::ble::TrafficLevel::FreeFlow;
  if ([name isEqualToString:@"slow"]) return moto::ble::TrafficLevel::Slow;
  if ([name isEqualToString:@"congested"]) return moto::ble::TrafficLevel::Congested;
  if ([name isEqualToString:@"severe"]) return moto::ble::TrafficLevel::Severe;
  return moto::ble::TrafficLevel::Unknown;
}

moto::ble::NavigationSnapshot SnapshotFromInput(
    MotoBLENavigationSnapshotInput *input) {
  moto::ble::NavigationSnapshot snapshot;
  snapshot.state = NavigationStateFromName(input.stateName);
  snapshot.network = NetworkStateFromName(input.networkName);
  snapshot.display_page = DisplayPageFromName(input.displayPageName);
  snapshot.maneuver = ManeuverFromName(input.maneuverName);
  snapshot.traffic = TrafficFromName(input.trafficName);
  if (input.hasDestination) {
    snapshot.flags |= moto::ble::NavigationHasDestination;
  }
  if (input.hasFix) snapshot.flags |= moto::ble::NavigationHasFix;
  if (input.gnssStale) snapshot.flags |= moto::ble::NavigationGnssStale;
  if (input.offRoute) snapshot.flags |= moto::ble::NavigationOffRoute;
  if (input.routeRequestInFlight) {
    snapshot.flags |= moto::ble::NavigationRouteRequestInFlight;
  }
  if (input.trafficRequestInFlight) {
    snapshot.flags |= moto::ble::NavigationTrafficRequestInFlight;
  }
  if (input.hasRouteView) snapshot.flags |= moto::ble::NavigationHasRouteView;
  if (input.headingIsCompass) {
    snapshot.flags |= moto::ble::NavigationHeadingCompass;
  }
  if (input.maneuverName.length > 0 &&
      ![input.maneuverName isEqualToString:@"unknown"]) {
    snapshot.flags |= moto::ble::NavigationHasNextManeuver;
  }
  snapshot.route_token = input.routeToken;
  snapshot.route_generation = input.routeGeneration;
  snapshot.maneuver_id = input.maneuverID;
  snapshot.distance_to_maneuver_m = input.distanceToManeuverM;
  snapshot.remaining_distance_m = input.remainingDistanceM;
  snapshot.remaining_duration_s = input.remainingDurationS;
  snapshot.route_progress_m = input.routeProgressM;
  snapshot.total_distance_m = input.totalDistanceM;
  snapshot.speed_deci_kph = input.speedDeciKPH;
  snapshot.speed_limit_kph = input.speedLimitKPH;
  snapshot.heading_cdeg = input.headingCentiDegrees;
  snapshot.accuracy_dm = input.accuracyDecimeters;
  snapshot.cross_track_dm = input.crossTrackDecimeters;
  snapshot.roundabout_exit = input.roundaboutExit;
  snapshot.road_name = input.roadName.UTF8String ?: "";
  snapshot.instruction = input.instructionText.UTF8String ?: "";
  return snapshot;
}

std::string Utf8Prefix(NSString *value, std::size_t maximum_bytes) {
  std::string bytes = value.UTF8String ?: "";
  if (bytes.size() <= maximum_bytes) return bytes;

  // The BLE payload uses byte-sized UTF-8 lengths.  Trim on a code-point
  // boundary so long Chinese titles cannot make the shared decoder reject an
  // otherwise valid MediaState.
  std::size_t end = maximum_bytes;
  while (end > 0 &&
         (static_cast<unsigned char>(bytes[end]) & 0xC0U) == 0x80U) {
    --end;
  }
  bytes.resize(end);
  return bytes;
}

moto::ble::MediaState MediaStateFromInput(MotoBLEMediaStateInput *input) {
  moto::ble::MediaState state;
  if (input.connected) state.flags |= moto::ble::MediaConnected;
  if (input.playing) state.flags |= moto::ble::MediaPlaying;
  if (input.likeAvailable) state.flags |= moto::ble::MediaLikeAvailable;
  if (input.liked) state.flags |= moto::ble::MediaLiked;
  state.track_token = input.trackToken;
  state.position_s = input.positionSeconds;
  state.duration_s = input.durationSeconds;
  state.source_name = Utf8Prefix(input.sourceName, 31);
  state.track_title = Utf8Prefix(input.trackTitle, 63);
  state.artist_name = Utf8Prefix(input.artistName, 47);
  return state;
}

moto::ble::MapRoadClass MapRoadClassFromName(NSString *name) {
  if ([name isEqualToString:@"motorway"]) return moto::ble::MapRoadClass::Motorway;
  if ([name isEqualToString:@"primary"]) return moto::ble::MapRoadClass::Primary;
  if ([name isEqualToString:@"secondary"]) return moto::ble::MapRoadClass::Secondary;
  if ([name isEqualToString:@"residential"]) return moto::ble::MapRoadClass::Residential;
  if ([name isEqualToString:@"service"]) return moto::ble::MapRoadClass::Service;
  return moto::ble::MapRoadClass::Other;
}

moto::ble::MapBuildingClass MapBuildingClassFromName(NSString *name) {
  if ([name isEqualToString:@"landmark"]) return moto::ble::MapBuildingClass::Landmark;
  if ([name isEqualToString:@"parking"]) return moto::ble::MapBuildingClass::Parking;
  return moto::ble::MapBuildingClass::Generic;
}

moto::ble::MapScene MapSceneFromInput(MotoBLEMapSceneInput *input) {
  moto::ble::MapScene scene;
  scene.scene_revision = input.sceneRevision;
  scene.view_origin = {input.originLatitudeE6, input.originLongitudeE6};
  scene.radius_m = input.radiusM;
  scene.roads.reserve(input.roads.count);
  for (MotoBLEMapRoadInput *source in input.roads) {
    moto::ble::MapRoadPolyline road;
    road.road_class = MapRoadClassFromName(source.className);
    road.points.reserve(source.points.count);
    for (MotoBLEMapPointInput *point in source.points) {
      road.points.push_back({point.latitudeE6, point.longitudeE6});
    }
    scene.roads.push_back(std::move(road));
  }
  scene.buildings.reserve(input.buildings.count);
  for (MotoBLEMapBuildingInput *source in input.buildings) {
    moto::ble::MapBuildingFootprint building;
    building.building_class = MapBuildingClassFromName(source.className);
    building.points.reserve(source.points.count);
    for (MotoBLEMapPointInput *point in source.points) {
      building.points.push_back({point.latitudeE6, point.longitudeE6});
    }
    scene.buildings.push_back(std::move(building));
  }
  return scene;
}

moto::ble::HomeLocation HomeLocationFromInput(MotoBLEHomeLocationInput *input) {
  moto::ble::HomeLocation home;
  // The wire contract is GCJ-02 only; the device does not convert frames and
  // refuses WGS84 rather than drawing the blip a few hundred metres away.
  home.coordinate_system = moto::ble::CoordinateSystem::Gcj02;
  home.revision = input.revision;
  home.has_home = input.hasHome;
  home.position = {input.latitudeE6, input.longitudeE6};
  return home;
}

moto::ble::GnssInput GnssInputFromInput(MotoBLEGnssInput *input) {
  moto::ble::GnssInput fix;
  fix.phone_monotonic_ms = input.phoneMonotonicMs;
  fix.fix_age_ms = input.fixAgeMs;
  // Fields without their flag stay zero; the shared validator refuses
  // anything else, and a speed above its ceiling as well.
  if (input.speedCmps >= 0) {
    fix.flags |= moto::ble::GnssInputHasSpeed;
    fix.speed_cmps = static_cast<std::uint16_t>(
        std::min<int32_t>(input.speedCmps, moto::ble::kMaxGnssSpeedCmps));
    if (input.speedAccuracyCmps >= 0) {
      fix.flags |= moto::ble::GnssInputHasSpeedAccuracy;
      fix.speed_accuracy_cmps = static_cast<std::uint16_t>(
          std::min<int32_t>(input.speedAccuracyCmps, 0xFFFF));
    }
  }
  return fix;
}

std::string Utf8String(NSString *value) {
  const char *bytes = value.UTF8String;
  return bytes != nullptr ? std::string(bytes) : std::string();
}

moto::ble::WifiConfig WifiConfigFromInput(MotoBLEWifiConfigInput *input) {
  moto::ble::WifiConfig config;
  if (!input.enabled) {
    config.policy = moto::ble::WifiPolicy::Disabled;
    return config;
  }
  // No trimming or clamping: the shared validator refuses an SSID or a
  // password the hotspot would not accept, and the error reaches the rider.
  config.policy = moto::ble::WifiPolicy::OnDemand;
  config.ssid = Utf8String(input.ssid);
  config.psk = Utf8String(input.password);
  return config;
}

moto::ble::SettingsState SettingsStateFromInput(MotoBLESettingsStateInput *input) {
  moto::ble::SettingsState state;
  // Theme and units are mapped explicitly rather than cast: the device's
  // `moto_ui_theme_t` is not this enum, and a reinterpreted value would be a
  // silently wrong palette rather than a refused message.
  switch (input.theme) {
    case 0: state.theme = moto::ble::SettingsTheme::Official; break;
    case 1: state.theme = moto::ble::SettingsTheme::Auto; break;
    case 2: state.theme = moto::ble::SettingsTheme::Day; break;
    case 3: state.theme = moto::ble::SettingsTheme::Night; break;
    default: state.theme = static_cast<moto::ble::SettingsTheme>(input.theme); break;
  }
  switch (input.units) {
    case 0: state.units = moto::ble::SettingsUnits::Kph; break;
    case 1: state.units = moto::ble::SettingsUnits::Mph; break;
    default: state.units = static_cast<moto::ble::SettingsUnits>(input.units); break;
  }
  state.brightness_percent = input.brightnessPercent;
  state.lean_full_scale_deg = input.leanFullScaleDeg;
  state.performance_target_kph = input.performanceTargetKph;
  state.visible_pages = input.visiblePages;
  state.lean_zero_offset_cdeg = input.leanZeroOffsetCdeg;
  state.ambient_night = input.ambientNight;
  state.show_buildings = input.showBuildings;
  state.revision = input.revision;
  return state;
}

NSArray<NSData *> *EncodeMessage(CodecStorage *storage,
                                 const moto::ble::Message &message,
                                 std::uint8_t flags,
                                 NSError **error) {
  auto payload = moto::ble::encode_message(message);
  if (!payload.ok()) {
    if (error != nullptr) *error = ProtocolError(payload.error, payload.offset);
    return nil;
  }
  const auto sequence = storage->sequence.next();
  auto frames = moto::ble::fragment_message(
      moto::ble::message_type(message), sequence,
      moto::ble::ByteView(payload.value), storage->maximum_frame_size, flags);
  if (!frames.ok()) {
    if (error != nullptr) *error = ProtocolError(frames.error, frames.offset);
    return nil;
  }
  storage->last_encoded_sequence = sequence;

  NSMutableArray<NSData *> *result =
      [NSMutableArray arrayWithCapacity:frames.value.size()];
  for (const auto &frame : frames.value) {
    [result addObject:[NSData dataWithBytes:frame.data() length:frame.size()]];
  }
  return result;
}

moto::ble::ConnectionStatus PhoneConnectionStatus(
    CodecStorage *storage,
    moto::ble::ConnectionState state,
    std::uint32_t session_id) {
  moto::ble::ConnectionStatus status;
  status.role = moto::ble::EndpointRole::Phone;
  status.state = state;
  status.capabilities = moto::ble::CapabilityNavigation |
                        moto::ble::CapabilityRouteGeometry |
                        moto::ble::CapabilityTraffic |
                        moto::ble::CapabilityMediaState |
                        moto::ble::CapabilityTouchCommands |
                        moto::ble::CapabilityMusicCommands |
                        moto::ble::CapabilityCommandAck |
                        moto::ble::CapabilityMapScene |
                        // Asks the device for AttitudeSample (0x15) at 10 Hz;
                        // ESP32BLECentral already decodes and expires it.
                        moto::ble::CapabilityAttitudeTelemetry |
                        // Asks for the device's 0-100 runs (0x19), shown on
                        // the race page.
                        moto::ble::CapabilityPerformanceRun |
                        // Shows the device's WifiStatus (0x1B) on the
                        // Device -> WiFi screen.
                        moto::ble::CapabilityWifiHotspot |
                        // Offers a firmware image (0x1C) and shows the device's
                        // OtaStatus (0x1D) on the Device -> Firmware screen.
                        moto::ble::CapabilityOta |
                        // Reads FirmwareInfo (0x1E), so that screen can tell
                        // an installed release from a new one.
                        moto::ble::CapabilityFirmwareInfo;
  status.session_id = session_id;
  status.max_frame_size =
      static_cast<std::uint16_t>(storage->maximum_frame_size);
  status.heartbeat_interval_ms = 1'000;
  return status;
}

}  // namespace

@implementation MotoBLENavigationSnapshotInput

- (instancetype)init {
  self = [super init];
  if (self) {
    _stateName = @"idle";
    _networkName = @"offline";
    _displayPageName = @"navigation";
    _maneuverName = @"unknown";
    _trafficName = @"unknown";
    _roadName = @"";
    _instructionText = @"";
  }
  return self;
}

@end

@implementation MotoBLEMediaStateInput

- (instancetype)init {
  self = [super init];
  if (self) {
    _sourceName = @"APPLE MUSIC";
    _trackTitle = @"";
    _artistName = @"";
  }
  return self;
}

@end

@implementation MotoBLEMapPointInput
@end

@implementation MotoBLEMapRoadInput
- (instancetype)init {
  self = [super init];
  if (self) {
    _className = @"other";
    _points = @[];
  }
  return self;
}
@end

@implementation MotoBLEMapBuildingInput
- (instancetype)init {
  self = [super init];
  if (self) {
    _className = @"generic";
    _points = @[];
  }
  return self;
}
@end

@implementation MotoBLEMapSceneInput
- (instancetype)init {
  self = [super init];
  if (self) {
    _roads = @[];
    _buildings = @[];
  }
  return self;
}
@end

@implementation MotoBLEHomeLocationInput
@end

@implementation MotoBLEGnssInput
- (instancetype)init {
  if ((self = [super init])) {
    _speedCmps = -1;
    _speedAccuracyCmps = -1;
  }
  return self;
}
@end

@implementation MotoBLESettingsStateInput
@end

@implementation MotoBLEWifiConfigInput
- (instancetype)init {
  if ((self = [super init])) {
    _enabled = YES;
    _ssid = @"";
    _password = @"";
  }
  return self;
}
@end

@interface MotoBLEDeviceCommand ()
@property(nonatomic, readwrite) uint16_t sequence;
@property(nonatomic, readwrite) BOOL ackRequested;
@property(nonatomic, readwrite) uint8_t kind;
@property(nonatomic, readwrite) uint16_t commandID;
@property(nonatomic, readwrite) uint8_t page;
@property(nonatomic, readwrite) uint16_t x;
@property(nonatomic, readwrite) uint16_t y;
@property(nonatomic, readwrite) uint32_t eventTimeMs;
@end

@implementation MotoBLEDeviceCommand
@end

@interface MotoBLEConnectionStatus ()
@property(nonatomic, readwrite) uint8_t role;
@property(nonatomic, readwrite) uint8_t state;
@property(nonatomic, readwrite) uint8_t minimumVersion;
@property(nonatomic, readwrite) uint8_t maximumVersion;
@property(nonatomic, readwrite) uint32_t capabilities;
@property(nonatomic, readwrite) uint32_t sessionID;
@property(nonatomic, readwrite) uint16_t maximumFrameSize;
@property(nonatomic, readwrite) uint16_t heartbeatIntervalMs;
@end

@implementation MotoBLEConnectionStatus
@end

@interface MotoBLEHeartbeat ()
@property(nonatomic, readwrite) uint32_t sessionID;
@end

@implementation MotoBLEHeartbeat
@end

@interface MotoBLEAttitudeSample ()
@property(nonatomic, readwrite) uint32_t monotonicMilliseconds;
@property(nonatomic, readwrite) int16_t leanCentiDegrees;
@property(nonatomic, readwrite) int16_t pitchCentiDegrees;
@property(nonatomic, readwrite) int16_t longitudinalMilliG;
@property(nonatomic, readwrite) int16_t lateralMilliG;
@end

@implementation MotoBLEAttitudeSample
@end

@interface MotoBLEPerformanceRun ()
@property(nonatomic, readwrite) uint8_t state;
@property(nonatomic, readwrite) uint16_t targetKph;
@property(nonatomic, readwrite) uint32_t runID;
@property(nonatomic, readwrite) uint32_t elapsedMilliseconds;
@property(nonatomic, readwrite) uint32_t bestMilliseconds;
@end

@implementation MotoBLEPerformanceRun
@end

@interface MotoBLEWifiStatus ()
@property(nonatomic, readwrite) uint8_t state;
@property(nonatomic, readwrite) uint8_t failure;
@property(nonatomic, readwrite) BOOL hasCredentials;
@property(nonatomic, readwrite, copy) NSString *ipv4Address;
@property(nonatomic, readwrite) int8_t rssiDbm;
@end

@implementation MotoBLEWifiStatus
@end

@implementation MotoBLEOtaCommandInput
@end

@interface MotoBLEOtaStatus ()
@property(nonatomic, readwrite) uint8_t state;
@property(nonatomic, readwrite) uint8_t failure;
@property(nonatomic, readwrite) uint8_t progressPercent;
@property(nonatomic, readwrite) uint32_t bytesReceived;
@end

@implementation MotoBLEOtaStatus
@end

@interface MotoBLEFirmwareInfo ()
@property(nonatomic, readwrite, copy) NSString *version;
@property(nonatomic, readwrite, copy) NSString *build;
@end

@implementation MotoBLEFirmwareInfo
@end

namespace {

MotoBLEWifiStatus *WifiStatusFromShared(const moto::ble::WifiStatus &status) {
  MotoBLEWifiStatus *value = [[MotoBLEWifiStatus alloc] init];
  value.state = static_cast<uint8_t>(status.state);
  value.failure = static_cast<uint8_t>(status.failure);
  value.hasCredentials =
      (status.flags & moto::ble::WifiStatusHasCredentials) != 0U;
  value.ipv4Address =
      status.state == moto::ble::WifiLinkState::Connected
          ? [NSString stringWithFormat:@"%u.%u.%u.%u", status.ipv4[0],
                                       status.ipv4[1], status.ipv4[2],
                                       status.ipv4[3]]
          : @"";
  value.rssiDbm = status.rssi_dbm;
  return value;
}

MotoBLEOtaStatus *OtaStatusFromShared(const moto::ble::OtaStatus &status) {
  MotoBLEOtaStatus *value = [[MotoBLEOtaStatus alloc] init];
  value.state = static_cast<uint8_t>(status.state);
  value.failure = static_cast<uint8_t>(status.failure);
  value.progressPercent = status.progress_percent;
  value.bytesReceived = status.bytes_received;
  return value;
}

MotoBLEFirmwareInfo *FirmwareInfoFromShared(
    const moto::ble::FirmwareInfo &info) {
  MotoBLEFirmwareInfo *value = [[MotoBLEFirmwareInfo alloc] init];
  // The shared validator admits printable ASCII only, so UTF-8 cannot fail.
  value.version = [NSString stringWithUTF8String:info.version.c_str()] ?: @"";
  value.build = [NSString stringWithUTF8String:info.build.c_str()] ?: @"";
  return value;
}

// The digest is 32 bytes on the wire and 64 hex characters everywhere a human
// or a shell computes it. Parsed here so Swift never restates the layout.
BOOL OtaCommandFromInput(MotoBLEOtaCommandInput *input,
                         moto::ble::OtaCommand *command) {
  if (command == nullptr) return NO;
  command->url = Utf8String(input.url);
  command->size_bytes = input.sizeBytes;
  const std::string hex = Utf8String(input.sha256Hex);
  if (hex.size() != moto::ble::kOtaSha256Bytes * 2U) return NO;
  auto nibble = [](char character) -> int {
    if (character >= '0' && character <= '9') return character - '0';
    if (character >= 'a' && character <= 'f') return character - 'a' + 10;
    if (character >= 'A' && character <= 'F') return character - 'A' + 10;
    return -1;
  };
  for (std::size_t i = 0; i < moto::ble::kOtaSha256Bytes; ++i) {
    const int high = nibble(hex[i * 2U]);
    const int low = nibble(hex[i * 2U + 1U]);
    if (high < 0 || low < 0) return NO;
    command->sha256[i] = static_cast<std::uint8_t>((high << 4) | low);
  }
  return YES;
}

}  // namespace

@interface MotoBLEAcknowledgement ()
@property(nonatomic, readwrite) uint16_t acknowledgedSequence;
@property(nonatomic, readwrite) uint8_t status;
@end

@implementation MotoBLEAcknowledgement
@end

@interface MotoBLEInboundMessage ()
@property(nonatomic, readwrite) uint16_t sequence;
@property(nonatomic, readwrite) BOOL ackRequested;
@property(nonatomic, readwrite) BOOL complete;
@property(nonatomic, readwrite) BOOL duplicate;
@property(nonatomic, readwrite, nullable) MotoBLEConnectionStatus *connectionStatus;
@property(nonatomic, readwrite, nullable) MotoBLEHeartbeat *heartbeat;
@property(nonatomic, readwrite, nullable) MotoBLEAcknowledgement *acknowledgement;
@property(nonatomic, readwrite, nullable) MotoBLEAttitudeSample *attitudeSample;
@property(nonatomic, readwrite, nullable) MotoBLEPerformanceRun *performanceRun;
@property(nonatomic, readwrite, nullable) MotoBLEWifiStatus *wifiStatus;
@property(nonatomic, readwrite, nullable) MotoBLEOtaStatus *otaStatus;
@property(nonatomic, readwrite, nullable) MotoBLEFirmwareInfo *firmwareInfo;
@property(nonatomic, readwrite, nullable) MotoBLEDeviceCommand *deviceCommand;
@end

@implementation MotoBLEInboundMessage
@end

@implementation MotoBLEProtocolCodec {
  void *_storage;
}

- (uint16_t)lastEncodedSequence {
  return static_cast<CodecStorage *>(_storage)->last_encoded_sequence;
}

+ (NSString *)serviceUUIDString {
  return [NSString stringWithUTF8String:moto::ble::kServiceUuid];
}

+ (NSString *)phoneToDeviceUUIDString {
  return [NSString stringWithUTF8String:moto::ble::kPhoneToDeviceUuid];
}

+ (NSString *)deviceToPhoneUUIDString {
  return [NSString stringWithUTF8String:moto::ble::kDeviceToPhoneUuid];
}

- (instancetype)initWithMaximumFrameSize:(NSUInteger)maximumFrameSize {
  self = [super init];
  if (self) {
    _storage = new CodecStorage(maximumFrameSize);
  }
  return self;
}

- (void)setMaximumFrameSize:(NSUInteger)maximumFrameSize {
  auto *storage = static_cast<CodecStorage *>(_storage);
  storage->maximum_frame_size = std::clamp<std::size_t>(
      maximumFrameSize, moto::ble::kFrameOverhead + 1,
      moto::ble::kMaxBleAttributeValueSize);
}

- (void)resetInboundState {
  auto *storage = static_cast<CodecStorage *>(_storage);
  storage->reassembler.reset();
}

- (void)dealloc {
  delete static_cast<CodecStorage *>(_storage);
}

- (NSArray<NSData *> *)encodePhoneStartingWithSessionID:(uint32_t)sessionID
                                                   error:(NSError **)error {
  auto *storage = static_cast<CodecStorage *>(_storage);
  const auto status = PhoneConnectionStatus(
      storage, moto::ble::ConnectionState::Starting, sessionID);
  return EncodeMessage(storage, moto::ble::Message{status}, 0, error);
}

- (NSArray<NSData *> *)encodePhoneReadyWithSessionID:(uint32_t)sessionID
                                                error:(NSError **)error {
  auto *storage = static_cast<CodecStorage *>(_storage);
  const auto status = PhoneConnectionStatus(
      storage, moto::ble::ConnectionState::Ready, sessionID);
  return EncodeMessage(storage, moto::ble::Message{status}, 0, error);
}

- (NSArray<NSData *> *)encodeHeartbeatWithSessionID:(uint32_t)sessionID
                                         monotonicMs:(uint32_t)monotonicMs
                                               error:(NSError **)error {
  auto *storage = static_cast<CodecStorage *>(_storage);
  moto::ble::Heartbeat heartbeat;
  heartbeat.session_id = sessionID;
  heartbeat.monotonic_ms = monotonicMs;
  heartbeat.status_flags = 0;
  return EncodeMessage(storage, moto::ble::Message{heartbeat}, 0, error);
}

- (NSArray<NSData *> *)encodeAckForSequence:(uint16_t)sequence
                                     status:(uint8_t)status
                                  commandID:(uint16_t)commandID
                                      error:(NSError **)error {
  auto *storage = static_cast<CodecStorage *>(_storage);
  moto::ble::Ack ack;
  ack.acknowledged_sequence = sequence;
  ack.status = static_cast<moto::ble::AckStatus>(status);
  ack.command_id = commandID;
  return EncodeMessage(storage, moto::ble::Message{ack}, 0, error);
}

- (NSArray<NSData *> *)encodeNavigationSnapshot:(MotoBLENavigationSnapshotInput *)input
                                            error:(NSError **)error {
  auto *storage = static_cast<CodecStorage *>(_storage);
  const moto::ble::NavigationSnapshot snapshot = SnapshotFromInput(input);
  return EncodeMessage(storage, moto::ble::Message{snapshot}, 0, error);
}

- (NSArray<NSData *> *)encodeRouteGeometryFromSnapshot:(MotoNavCoreSnapshot *)input
                                                   error:(NSError **)error {
  if (!input.hasRouteView || input.routeViewPoints.count == 0 ||
      input.routeID.length == 0) {
    return @[];
  }
  auto *storage = static_cast<CodecStorage *>(_storage);
  moto::ble::RouteGeometry geometry;
  geometry.route_token = moto::ble::route_token(input.routeID.UTF8String ?: "");
  geometry.route_generation = input.routeGeneration;
  geometry.total_point_count = static_cast<std::uint16_t>(
      std::min<NSUInteger>(input.routeViewPoints.count,
                           moto::ble::kMaxRoutePointsPerChunk));
  geometry.view_origin = {
      CoordinateE6(input.routeViewOriginLatitudeDeg),
      CoordinateE6(input.routeViewOriginLongitudeDeg)};
  geometry.points.reserve(geometry.total_point_count);
  for (NSUInteger index = 0; index < geometry.total_point_count; ++index) {
    MotoNavPointValue *point = input.routeViewPoints[index];
    geometry.points.push_back(
        {CoordinateE6(point.latitudeDeg), CoordinateE6(point.longitudeDeg)});
  }
  return EncodeMessage(storage, moto::ble::Message{geometry}, 0, error);
}

- (NSArray<NSData *> *)encodeMediaState:(MotoBLEMediaStateInput *)input
                                  error:(NSError **)error {
  auto *storage = static_cast<CodecStorage *>(_storage);
  return EncodeMessage(storage,
                       moto::ble::Message{MediaStateFromInput(input)},
                       0, error);
}

- (NSArray<NSData *> *)encodeMapScene:(MotoBLEMapSceneInput *)input
                                 error:(NSError **)error {
  auto *storage = static_cast<CodecStorage *>(_storage);
  return EncodeMessage(storage,
                       moto::ble::Message{MapSceneFromInput(input)},
                       moto::ble::AckRequested, error);
}

- (NSArray<NSData *> *)encodeHomeLocation:(MotoBLEHomeLocationInput *)input
                                    error:(NSError **)error {
  auto *storage = static_cast<CodecStorage *>(_storage);
  // No Ack: the message is idempotent and ordered by revision, and a lost one
  // is replaced by the next address change or the next session's resend. An
  // Ack would confirm delivery, not that a blip is on screen.
  return EncodeMessage(storage,
                       moto::ble::Message{HomeLocationFromInput(input)},
                       0, error);
}

- (NSArray<NSData *> *)encodeSettingsState:(MotoBLESettingsStateInput *)input
                                    error:(NSError **)error {
  auto *storage = static_cast<CodecStorage *>(_storage);
  // No Ack: the device echoes the state it applied, and that echo is the only
  // confirmation worth having — an Ack would confirm delivery, not that the
  // glass changed.
  return EncodeMessage(storage,
                       moto::ble::Message{SettingsStateFromInput(input)},
                       0, error);
}

- (NSArray<NSData *> *)encodeGnssInput:(MotoBLEGnssInput *)input
                                 error:(NSError **)error {
  auto *storage = static_cast<CodecStorage *>(_storage);
  // No Ack: a fix is superseded by the next one within a second, and a
  // retried fix would only arrive older.
  return EncodeMessage(storage,
                       moto::ble::Message{GnssInputFromInput(input)},
                       0, error);
}

- (NSArray<NSData *> *)encodeWifiConfig:(MotoBLEWifiConfigInput *)input
                                  error:(NSError **)error {
  auto *storage = static_cast<CodecStorage *>(_storage);
  moto::ble::WifiConfig config = WifiConfigFromInput(input);
  // Acked: the rider is told whether the device stored it, and a refusal
  // (unbonded link, NVS failure) must not look like success.
  NSArray<NSData *> *frames = EncodeMessage(
      storage, moto::ble::Message{config}, moto::ble::AckRequested, error);
  std::fill(config.psk.begin(), config.psk.end(), '\0');
  return frames;
}

- (NSData *)encodeWifiConfigPayloadForTesting:(MotoBLEWifiConfigInput *)input
                                        error:(NSError **)error {
  const auto payload =
      moto::ble::encode_message(moto::ble::Message{WifiConfigFromInput(input)});
  if (!payload.ok()) {
    if (error != nullptr) *error = ProtocolError(payload.error, payload.offset);
    return nil;
  }
  return [NSData dataWithBytes:payload.value.data() length:payload.value.size()];
}

- (MotoBLEWifiStatus *)decodeWifiStatusPayloadForTesting:(NSData *)payload
                                                   error:(NSError **)error {
  const auto decoded = moto::ble::decode_message(
      moto::ble::MessageType::WifiStatus,
      moto::ble::ByteView(static_cast<const std::uint8_t *>(payload.bytes),
                          payload.length));
  const auto *status =
      decoded.ok() ? std::get_if<moto::ble::WifiStatus>(&decoded.value)
                   : nullptr;
  if (status == nullptr) {
    if (error != nullptr) *error = ProtocolError(decoded.error, decoded.offset);
    return nil;
  }
  return WifiStatusFromShared(*status);
}

- (NSArray<NSData *> *)encodeOtaCommand:(MotoBLEOtaCommandInput *)input
                                  error:(NSError **)error {
  auto *storage = static_cast<CodecStorage *>(_storage);
  moto::ble::OtaCommand command;
  if (!OtaCommandFromInput(input, &command)) {
    if (error != nullptr) {
      *error = [NSError errorWithDomain:kMotoBLEErrorDomain
                                   code:4001
                               userInfo:@{
                                 NSLocalizedDescriptionKey :
                                     @"固件摘要必须是 64 位十六进制 SHA-256。"
                               }];
    }
    return nil;
  }
  // Acked: the rider is told whether the device accepted the update, and a
  // refusal (unbonded link, busy, image too large) must not look like success.
  return EncodeMessage(storage, moto::ble::Message{command},
                       moto::ble::AckRequested, error);
}

- (NSData *)encodeOtaCommandPayloadForTesting:(MotoBLEOtaCommandInput *)input
                                        error:(NSError **)error {
  moto::ble::OtaCommand command;
  if (!OtaCommandFromInput(input, &command)) {
    if (error != nullptr) {
      *error = [NSError errorWithDomain:kMotoBLEErrorDomain
                                   code:4001
                               userInfo:@{
                                 NSLocalizedDescriptionKey :
                                     @"固件摘要必须是 64 位十六进制 SHA-256。"
                               }];
    }
    return nil;
  }
  const auto payload = moto::ble::encode_message(moto::ble::Message{command});
  if (!payload.ok()) {
    if (error != nullptr) *error = ProtocolError(payload.error, payload.offset);
    return nil;
  }
  return [NSData dataWithBytes:payload.value.data() length:payload.value.size()];
}

- (MotoBLEOtaStatus *)decodeOtaStatusPayloadForTesting:(NSData *)payload
                                                 error:(NSError **)error {
  const auto decoded = moto::ble::decode_message(
      moto::ble::MessageType::OtaStatus,
      moto::ble::ByteView(static_cast<const std::uint8_t *>(payload.bytes),
                          payload.length));
  const auto *status =
      decoded.ok() ? std::get_if<moto::ble::OtaStatus>(&decoded.value)
                   : nullptr;
  if (status == nullptr) {
    if (error != nullptr) *error = ProtocolError(decoded.error, decoded.offset);
    return nil;
  }
  return OtaStatusFromShared(*status);
}

- (MotoBLEFirmwareInfo *)decodeFirmwareInfoPayloadForTesting:(NSData *)payload
                                                       error:(NSError **)error {
  const auto decoded = moto::ble::decode_message(
      moto::ble::MessageType::FirmwareInfo,
      moto::ble::ByteView(static_cast<const std::uint8_t *>(payload.bytes),
                          payload.length));
  const auto *info =
      decoded.ok() ? std::get_if<moto::ble::FirmwareInfo>(&decoded.value)
                   : nullptr;
  if (info == nullptr) {
    if (error != nullptr) *error = ProtocolError(decoded.error, decoded.offset);
    return nil;
  }
  return FirmwareInfoFromShared(*info);
}

- (NSData *)encodeGnssInputPayloadForTesting:(MotoBLEGnssInput *)input
                                       error:(NSError **)error {
  const auto payload =
      moto::ble::encode_message(moto::ble::Message{GnssInputFromInput(input)});
  if (!payload.ok()) {
    if (error != nullptr) *error = ProtocolError(payload.error, payload.offset);
    return nil;
  }
  return [NSData dataWithBytes:payload.value.data() length:payload.value.size()];
}

- (NSData *)encodeSettingsStatePayloadForTesting:(MotoBLESettingsStateInput *)input
                                          error:(NSError **)error {
  const auto payload =
      moto::ble::encode_message(moto::ble::Message{SettingsStateFromInput(input)});
  if (!payload.ok()) {
    if (error != nullptr) *error = ProtocolError(payload.error, payload.offset);
    return nil;
  }
  return [NSData dataWithBytes:payload.value.data() length:payload.value.size()];
}

- (NSData *)encodeHomeLocationPayloadForTesting:(MotoBLEHomeLocationInput *)input
                                          error:(NSError **)error {
  const auto payload =
      moto::ble::encode_message(moto::ble::Message{HomeLocationFromInput(input)});
  if (!payload.ok()) {
    if (error != nullptr) *error = ProtocolError(payload.error, payload.offset);
    return nil;
  }
  return [NSData dataWithBytes:payload.value.data() length:payload.value.size()];
}

- (NSData *)encodeMapScenePayloadForTesting:(MotoBLEMapSceneInput *)input
                                       error:(NSError **)error {
  const auto payload =
      moto::ble::encode_message(moto::ble::Message{MapSceneFromInput(input)});
  if (!payload.ok()) {
    if (error != nullptr) *error = ProtocolError(payload.error, payload.offset);
    return nil;
  }
  return [NSData dataWithBytes:payload.value.data() length:payload.value.size()];
}

- (NSData *)encodeNavigationPayloadForGoldenCheck:(MotoBLENavigationSnapshotInput *)input
                                              error:(NSError **)error {
  const auto payload =
      moto::ble::encode_message(moto::ble::Message{SnapshotFromInput(input)});
  if (!payload.ok()) {
    if (error != nullptr) *error = ProtocolError(payload.error, payload.offset);
    return nil;
  }
  return [NSData dataWithBytes:payload.value.data() length:payload.value.size()];
}

- (MotoBLEInboundMessage *)pushDeviceFrame:(NSData *)frame
                              receivedAtMs:(uint64_t)receivedAtMs
                                     error:(NSError **)error {
  auto *storage = static_cast<CodecStorage *>(_storage);
  auto result = storage->reassembler.push(
      moto::ble::ByteView(static_cast<const std::uint8_t *>(frame.bytes), frame.length),
      receivedAtMs);
  if (!result.complete()) {
    if (result.state == moto::ble::ReassemblyState::InProgress ||
        result.state == moto::ble::ReassemblyState::DuplicateFragment ||
        result.state == moto::ble::ReassemblyState::DuplicateMessage) {
      MotoBLEInboundMessage *value = [[MotoBLEInboundMessage alloc] init];
      value.sequence = result.message.sequence;
      value.ackRequested =
          (result.message.flags & moto::ble::AckRequested) != 0U;
      value.complete = NO;
      value.duplicate =
          result.state == moto::ble::ReassemblyState::DuplicateMessage;
      return value;
    }
    if (error != nullptr) *error = ProtocolError(result.error, result.error_offset);
    return nil;
  }

  auto decoded = moto::ble::decode_message(
      result.message.type, moto::ble::ByteView(result.message.payload));
  if (!decoded.ok()) {
    if (error != nullptr) *error = ProtocolError(decoded.error, decoded.offset);
    return nil;
  }
  MotoBLEInboundMessage *inbound = [[MotoBLEInboundMessage alloc] init];
  inbound.sequence = result.message.sequence;
  inbound.ackRequested =
      (result.message.flags & moto::ble::AckRequested) != 0U;
  inbound.complete = YES;
  inbound.duplicate = NO;

  if (const auto *status =
          std::get_if<moto::ble::ConnectionStatus>(&decoded.value)) {
    MotoBLEConnectionStatus *value = [[MotoBLEConnectionStatus alloc] init];
    value.role = static_cast<uint8_t>(status->role);
    value.state = static_cast<uint8_t>(status->state);
    value.minimumVersion = status->minimum_version;
    value.maximumVersion = status->maximum_version;
    value.capabilities = status->capabilities;
    value.sessionID = status->session_id;
    value.maximumFrameSize = status->max_frame_size;
    value.heartbeatIntervalMs = status->heartbeat_interval_ms;
    inbound.connectionStatus = value;
    return inbound;
  }

  if (const auto *heartbeat =
          std::get_if<moto::ble::Heartbeat>(&decoded.value)) {
    MotoBLEHeartbeat *value = [[MotoBLEHeartbeat alloc] init];
    value.sessionID = heartbeat->session_id;
    inbound.heartbeat = value;
    return inbound;
  }

  if (const auto *ack = std::get_if<moto::ble::Ack>(&decoded.value)) {
    MotoBLEAcknowledgement *value = [[MotoBLEAcknowledgement alloc] init];
    value.acknowledgedSequence = ack->acknowledged_sequence;
    value.status = static_cast<uint8_t>(ack->status);
    inbound.acknowledgement = value;
    return inbound;
  }

  const auto *attitude = std::get_if<moto::ble::AttitudeSample>(&decoded.value);
  if (attitude != nullptr) {
    MotoBLEAttitudeSample *value = [[MotoBLEAttitudeSample alloc] init];
    value.monotonicMilliseconds = attitude->monotonic_ms;
    value.leanCentiDegrees = attitude->lean_cdeg;
    value.pitchCentiDegrees = attitude->pitch_cdeg;
    value.longitudinalMilliG = attitude->longitudinal_mg;
    value.lateralMilliG = attitude->lateral_mg;
    inbound.attitudeSample = value;
    return inbound;
  }

  const auto *run = std::get_if<moto::ble::PerformanceRun>(&decoded.value);
  if (run != nullptr) {
    MotoBLEPerformanceRun *value = [[MotoBLEPerformanceRun alloc] init];
    value.state = static_cast<uint8_t>(run->state);
    value.targetKph = run->target_kph;
    value.runID = run->run_id;
    value.elapsedMilliseconds = run->elapsed_ms;
    value.bestMilliseconds = run->best_ms;
    inbound.performanceRun = value;
    return inbound;
  }

  if (const auto *wifi = std::get_if<moto::ble::WifiStatus>(&decoded.value)) {
    inbound.wifiStatus = WifiStatusFromShared(*wifi);
    return inbound;
  }

  if (const auto *ota = std::get_if<moto::ble::OtaStatus>(&decoded.value)) {
    inbound.otaStatus = OtaStatusFromShared(*ota);
    return inbound;
  }

  if (const auto *info =
          std::get_if<moto::ble::FirmwareInfo>(&decoded.value)) {
    inbound.firmwareInfo = FirmwareInfoFromShared(*info);
    return inbound;
  }

  const auto *command = std::get_if<moto::ble::DeviceCommand>(&decoded.value);
  if (command == nullptr) return inbound;

  MotoBLEDeviceCommand *value = [[MotoBLEDeviceCommand alloc] init];
  value.sequence = result.message.sequence;
  value.ackRequested = inbound.ackRequested;
  value.kind = static_cast<uint8_t>(command->kind);
  value.commandID = command->command_id;
  value.page = static_cast<uint8_t>(command->page);
  value.x = command->x;
  value.y = command->y;
  value.eventTimeMs = command->event_time_ms;
  inbound.deviceCommand = value;
  return inbound;
}

- (uint32_t)routeTokenForRouteID:(NSString *)routeID {
  const char *bytes = routeID.UTF8String ?: "";
  return moto::ble::route_token(std::string_view(bytes));
}

@end
