#import <Foundation/Foundation.h>

NS_ASSUME_NONNULL_BEGIN

@class MotoNavCoreSnapshot;

@interface MotoBLENavigationSnapshotInput : NSObject
@property(nonatomic, copy) NSString *stateName;
@property(nonatomic, copy) NSString *networkName;
@property(nonatomic, copy) NSString *displayPageName;
@property(nonatomic, copy) NSString *maneuverName;
@property(nonatomic, copy) NSString *trafficName;
@property(nonatomic) BOOL hasDestination;
@property(nonatomic) BOOL hasFix;
@property(nonatomic) BOOL gnssStale;
@property(nonatomic) BOOL offRoute;
@property(nonatomic) BOOL routeRequestInFlight;
@property(nonatomic) BOOL trafficRequestInFlight;
@property(nonatomic) BOOL hasRouteView;
/// headingCentiDegrees is the phone's compass, not a GNSS course. Set only
/// for a peer that advertises CapabilityCompassHeading.
@property(nonatomic) BOOL headingIsCompass;
@property(nonatomic) uint32_t routeToken;
@property(nonatomic) uint32_t routeGeneration;
@property(nonatomic) uint32_t maneuverID;
@property(nonatomic) uint32_t distanceToManeuverM;
@property(nonatomic) uint32_t remainingDistanceM;
@property(nonatomic) uint32_t remainingDurationS;
@property(nonatomic) uint32_t routeProgressM;
@property(nonatomic) uint32_t totalDistanceM;
@property(nonatomic) uint16_t speedDeciKPH;
@property(nonatomic) uint16_t speedLimitKPH;
@property(nonatomic) uint16_t headingCentiDegrees;
@property(nonatomic) uint16_t accuracyDecimeters;
@property(nonatomic) uint16_t crossTrackDecimeters;
@property(nonatomic) uint8_t roundaboutExit;
@property(nonatomic, copy) NSString *roadName;
@property(nonatomic, copy) NSString *instructionText;
@end

/// Phone-side projection of the Apple Music fallback player.  The device shows
/// it only while it cannot read Apple Media Service itself.  The Objective-C++
/// bridge owns the binary layout so Swift never duplicates the shared v1 wire
/// protocol.
@interface MotoBLEMediaStateInput : NSObject
@property(nonatomic) BOOL connected;
@property(nonatomic) BOOL playing;
@property(nonatomic) BOOL likeAvailable;
@property(nonatomic) BOOL liked;
@property(nonatomic) uint32_t trackToken;
@property(nonatomic) uint16_t positionSeconds;
@property(nonatomic) uint16_t durationSeconds;
@property(nonatomic, copy) NSString *sourceName;
@property(nonatomic, copy) NSString *trackTitle;
@property(nonatomic, copy) NSString *artistName;
@end

@interface MotoBLEMapPointInput : NSObject
@property(nonatomic) int32_t latitudeE6;
@property(nonatomic) int32_t longitudeE6;
@end

@interface MotoBLEMapRoadInput : NSObject
@property(nonatomic, copy) NSString *className;
@property(nonatomic, copy) NSArray<MotoBLEMapPointInput *> *points;
@end

@interface MotoBLEMapBuildingInput : NSObject
@property(nonatomic, copy) NSString *className;
@property(nonatomic, copy) NSArray<MotoBLEMapPointInput *> *points;
@end

@interface MotoBLEMapSceneInput : NSObject
@property(nonatomic) uint32_t sceneRevision;
@property(nonatomic) int32_t originLatitudeE6;
@property(nonatomic) int32_t originLongitudeE6;
@property(nonatomic) uint16_t radiusM;
@property(nonatomic, copy) NSArray<MotoBLEMapRoadInput *> *roads;
@property(nonatomic, copy) NSArray<MotoBLEMapBuildingInput *> *buildings;
@end

/// Saved home address (message 0x16). Latitude and longitude must already be
/// GCJ-02: the device refuses any other frame rather than drawing the blip in
/// the wrong place. `revision` is monotonic across app launches.
@interface MotoBLEHomeLocationInput : NSObject
@property(nonatomic) uint32_t revision;
@property(nonatomic) BOOL hasHome;
@property(nonatomic) int32_t latitudeE6;
@property(nonatomic) int32_t longitudeE6;
@end

/// One Core Location fix for the handlebar's speed fusion. The codec stamps
/// the phone's monotonic clock; `fixAgeMs` is how long before that the fix was
/// taken. Speeds are cm/s; a negative value means unknown.
@interface MotoBLEGnssInput : NSObject
@property(nonatomic) uint32_t phoneMonotonicMs;
@property(nonatomic) uint16_t fixAgeMs;
@property(nonatomic) int32_t speedCmps;
@property(nonatomic) int32_t speedAccuracyCmps;
@end

/// The iPhone Personal Hotspot the round screen joins on demand (message
/// 0x1A). iOS cannot read the hotspot's own name or password, so the rider
/// types them once. `enabled == NO` tells the device to forget the network;
/// SSID and password are then ignored.
@interface MotoBLEWifiConfigInput : NSObject
@property(nonatomic) BOOL enabled;
@property(nonatomic, copy) NSString *ssid;
@property(nonatomic, copy) NSString *password;
@end

@interface MotoBLESettingsStateInput : NSObject
/// Raw values match `moto::ble::SettingsState`; the shared encoder refuses a
/// value outside the reviewed table rather than clamping it into a scale the
/// design never reviewed.
@property(nonatomic) uint8_t theme;
@property(nonatomic) uint8_t brightnessPercent;
@property(nonatomic) uint8_t units;
@property(nonatomic) uint8_t leanFullScaleDeg;
@property(nonatomic) uint16_t performanceTargetKph;
@property(nonatomic) uint8_t visiblePages;
@property(nonatomic) int16_t leanZeroOffsetCdeg;
@property(nonatomic) BOOL ambientNight;
@property(nonatomic) BOOL showBuildings;
/// Monotonic and non-zero: zero means "never seen" to the device.
@property(nonatomic) uint32_t revision;
@end

@interface MotoBLEDeviceCommand : NSObject
@property(nonatomic, readonly) uint16_t sequence;
@property(nonatomic, readonly) BOOL ackRequested;
@property(nonatomic, readonly) uint8_t kind;
@property(nonatomic, readonly) uint16_t commandID;
@property(nonatomic, readonly) uint8_t page;
@property(nonatomic, readonly) uint16_t x;
@property(nonatomic, readonly) uint16_t y;
@property(nonatomic, readonly) uint32_t eventTimeMs;
@end

@interface MotoBLEConnectionStatus : NSObject
@property(nonatomic, readonly) uint8_t role;
@property(nonatomic, readonly) uint8_t state;
@property(nonatomic, readonly) uint8_t minimumVersion;
@property(nonatomic, readonly) uint8_t maximumVersion;
@property(nonatomic, readonly) uint32_t capabilities;
@property(nonatomic, readonly) uint32_t sessionID;
@property(nonatomic, readonly) uint16_t maximumFrameSize;
@property(nonatomic, readonly) uint16_t heartbeatIntervalMs;
@end

@interface MotoBLEHeartbeat : NSObject
@property(nonatomic, readonly) uint32_t sessionID;
@end

@interface MotoBLEAcknowledgement : NSObject
@property(nonatomic, readonly) uint16_t acknowledgedSequence;
@property(nonatomic, readonly) uint8_t status;
@end

/// Lean / pitch / G sample from the device IMU (message 0x15). Centi-degrees
/// and milli-g stay in their wire units; the Swift side owns display units.
@interface MotoBLEAttitudeSample : NSObject
@property(nonatomic, readonly) uint32_t monotonicMilliseconds;
@property(nonatomic, readonly) int16_t leanCentiDegrees;
@property(nonatomic, readonly) int16_t pitchCentiDegrees;
@property(nonatomic, readonly) int16_t longitudinalMilliG;
@property(nonatomic, readonly) int16_t lateralMilliG;
@end

/// The round screen's 0-100 clock (message 0x19). It owns the clock; the
/// phone shows what it measured. state: 0 armed, 1 running, 2 finished.
@interface MotoBLEPerformanceRun : NSObject
@property(nonatomic, readonly) uint8_t state;
@property(nonatomic, readonly) uint16_t targetKph;
@property(nonatomic, readonly) uint32_t runID;
@property(nonatomic, readonly) uint32_t elapsedMilliseconds;
@property(nonatomic, readonly) uint32_t bestMilliseconds;
@end

/// The round screen's WiFi link (message 0x1B). state: 0 off, 1 connecting,
/// 2 connected, 3 failed. failure: 0 none, 1 no network stored, 2 network not
/// found, 3 wrong password, 4 timeout, 5 out of memory, 6 lost the network.
/// `ipv4Address` is empty unless connected.
@interface MotoBLEWifiStatus : NSObject
@property(nonatomic, readonly) uint8_t state;
@property(nonatomic, readonly) uint8_t failure;
@property(nonatomic, readonly) BOOL hasCredentials;
@property(nonatomic, readonly, copy) NSString *ipv4Address;
@property(nonatomic, readonly) int8_t rssiDbm;
@end

/// The firmware image the round screen should fetch over the hotspot (message
/// 0x1C). `sha256Hex` is the image's SHA-256 as 64 hex characters; the bridge
/// converts it to the 32 wire bytes, so Swift never restates the layout.
/// `sizeBytes == 0` means "not stated": the device learns the total from the
/// response instead of refusing a mismatch.
@interface MotoBLEOtaCommandInput : NSObject
@property(nonatomic, copy) NSString *url;
@property(nonatomic, copy) NSString *sha256Hex;
@property(nonatomic) uint32_t sizeBytes;
@end

/// The round screen's update progress (message 0x1D). state: 0 idle,
/// 1 downloading, 2 verifying, 3 ready, 4 failed. failure: 0 none, 1 no wifi,
/// 2 no credentials, 3 bad url, 4 network, 5 hash mismatch, 6 image rejected,
/// 7 out of memory, 8 busy, 9 too large.
@interface MotoBLEOtaStatus : NSObject
@property(nonatomic, readonly) uint8_t state;
@property(nonatomic, readonly) uint8_t failure;
@property(nonatomic, readonly) uint8_t progressPercent;
@property(nonatomic, readonly) uint32_t bytesReceived;
@end

/// What the round screen runs (message 0x1E): its firmware version and the
/// commit the image was built from, "-dirty" for an edited tree.
@interface MotoBLEFirmwareInfo : NSObject
@property(nonatomic, readonly, copy) NSString *version;
@property(nonatomic, readonly, copy) NSString *build;
@end

/// One successfully decoded transport message, or one valid partial/duplicate
/// frame. Sequence and ACK metadata stay attached to the shared decoder output.
@interface MotoBLEInboundMessage : NSObject
@property(nonatomic, readonly) uint16_t sequence;
@property(nonatomic, readonly) BOOL ackRequested;
@property(nonatomic, readonly) BOOL complete;
@property(nonatomic, readonly) BOOL duplicate;
@property(nonatomic, readonly, nullable) MotoBLEConnectionStatus *connectionStatus;
@property(nonatomic, readonly, nullable) MotoBLEHeartbeat *heartbeat;
@property(nonatomic, readonly, nullable) MotoBLEAcknowledgement *acknowledgement;
@property(nonatomic, readonly, nullable) MotoBLEAttitudeSample *attitudeSample;
@property(nonatomic, readonly, nullable) MotoBLEPerformanceRun *performanceRun;
@property(nonatomic, readonly, nullable) MotoBLEWifiStatus *wifiStatus;
@property(nonatomic, readonly, nullable) MotoBLEOtaStatus *otaStatus;
@property(nonatomic, readonly, nullable) MotoBLEFirmwareInfo *firmwareInfo;
@property(nonatomic, readonly, nullable) MotoBLEDeviceCommand *deviceCommand;
@end

/// Thin Objective-C++ bridge over shared/ble_protocol. No wire constants or
/// binary payload layouts are reimplemented in Swift.
@interface MotoBLEProtocolCodec : NSObject
@property(nonatomic, readonly) uint16_t lastEncodedSequence;

+ (NSString *)serviceUUIDString;
+ (NSString *)phoneToDeviceUUIDString;
+ (NSString *)deviceToPhoneUUIDString;

- (instancetype)initWithMaximumFrameSize:(NSUInteger)maximumFrameSize;

- (void)setMaximumFrameSize:(NSUInteger)maximumFrameSize;

/// Drops only device-to-phone fragment/sequence history. Outbound sequence
/// numbers are deliberately preserved so a new phone session is not mistaken
/// for a retransmission by the peripheral.
- (void)resetInboundState;

- (nullable NSArray<NSData *> *)encodePhoneStartingWithSessionID:(uint32_t)sessionID
                                                          error:(NSError **)error;

- (nullable NSArray<NSData *> *)encodePhoneReadyWithSessionID:(uint32_t)sessionID
                                                        error:(NSError **)error;

- (nullable NSArray<NSData *> *)encodeHeartbeatWithSessionID:(uint32_t)sessionID
                                                  monotonicMs:(uint32_t)monotonicMs
                                                        error:(NSError **)error;

- (nullable NSArray<NSData *> *)encodeAckForSequence:(uint16_t)sequence
                                              status:(uint8_t)status
                                           commandID:(uint16_t)commandID
                                               error:(NSError **)error;

- (nullable NSArray<NSData *> *)encodeNavigationSnapshot:(MotoBLENavigationSnapshotInput *)snapshot
                                                    error:(NSError **)error;

- (nullable NSArray<NSData *> *)encodeRouteGeometryFromSnapshot:(MotoNavCoreSnapshot *)snapshot
                                                           error:(NSError **)error;

- (nullable NSArray<NSData *> *)encodeMediaState:(MotoBLEMediaStateInput *)state
                                           error:(NSError **)error;

- (nullable NSArray<NSData *> *)encodeMapScene:(MotoBLEMapSceneInput *)scene
                                          error:(NSError **)error;

- (nullable NSArray<NSData *> *)encodeHomeLocation:(MotoBLEHomeLocationInput *)home
                                             error:(NSError **)error;

- (nullable NSArray<NSData *> *)encodeSettingsState:(MotoBLESettingsStateInput *)settings
                                              error:(NSError **)error;

// Spelled out: Swift would otherwise prune the type name to `encode(_:)`.
- (nullable NSArray<NSData *> *)encodeGnssInput:(MotoBLEGnssInput *)fix
                                          error:(NSError **)error
    NS_SWIFT_NAME(encodeGnssInput(_:));

/// Test hook for checking the shared GnssInput payload without duplicating
/// its binary layout in Swift.
- (nullable NSData *)encodeGnssInputPayloadForTesting:(MotoBLEGnssInput *)fix
                                                error:(NSError **)error;

/// Sent with AckRequested; `lastEncodedSequence` is the sequence the device's
/// Ack will name. The device refuses it (InvalidState) on a link that is not
/// encrypted and bonded.
- (nullable NSArray<NSData *> *)encodeWifiConfig:(MotoBLEWifiConfigInput *)config
                                           error:(NSError **)error
    NS_SWIFT_NAME(encodeWifiConfig(_:));

/// Sent with AckRequested; `lastEncodedSequence` is the sequence the device's
/// Ack will name. Refused (InvalidState) on a link that is not encrypted and
/// bonded, and refused (InvalidState) while another update is running.
- (nullable NSArray<NSData *> *)encodeOtaCommand:(MotoBLEOtaCommandInput *)command
                                           error:(NSError **)error
    NS_SWIFT_NAME(encodeOtaCommand(_:));

/// Test hooks for the golden check: the shared OtaCommand encoder and the
/// shared OtaStatus decoder, without restating either layout in Swift.
- (nullable NSData *)encodeOtaCommandPayloadForTesting:(MotoBLEOtaCommandInput *)command
                                                 error:(NSError **)error;
- (nullable MotoBLEOtaStatus *)decodeOtaStatusPayloadForTesting:(NSData *)payload
                                                          error:(NSError **)error;
/// Test hook for the golden check: the shared FirmwareInfo decoder.
- (nullable MotoBLEFirmwareInfo *)decodeFirmwareInfoPayloadForTesting:(NSData *)payload
                                                                error:(NSError **)error;

/// Test hooks for the golden check: the shared WifiConfig encoder and the
/// shared WifiStatus decoder, without restating either layout in Swift.
- (nullable NSData *)encodeWifiConfigPayloadForTesting:(MotoBLEWifiConfigInput *)config
                                                 error:(NSError **)error;
- (nullable MotoBLEWifiStatus *)decodeWifiStatusPayloadForTesting:(NSData *)payload
                                                            error:(NSError **)error;

/// Test hook for checking the shared SettingsState payload without duplicating
/// its binary layout in Swift.
- (nullable NSData *)encodeSettingsStatePayloadForTesting:(MotoBLESettingsStateInput *)settings
                                                    error:(NSError **)error;

/// Test hook for checking the shared HomeLocation payload without duplicating
/// its binary layout in Swift.
- (nullable NSData *)encodeHomeLocationPayloadForTesting:(MotoBLEHomeLocationInput *)home
                                                   error:(NSError **)error;

/// Test hook for checking the shared MapScene payload without duplicating its
/// binary layout in Swift.
- (nullable NSData *)encodeMapScenePayloadForTesting:(MotoBLEMapSceneInput *)scene
                                                error:(NSError **)error;

/// Exposes the shared payload encoder only for the bundled golden-vector check.
- (nullable NSData *)encodeNavigationPayloadForGoldenCheck:(MotoBLENavigationSnapshotInput *)snapshot
                                                      error:(NSError **)error;

- (nullable MotoBLEInboundMessage *)pushDeviceFrame:(NSData *)frame
                                       receivedAtMs:(uint64_t)receivedAtMs
                                              error:(NSError **)error;

- (uint32_t)routeTokenForRouteID:(NSString *)routeID;

@end

NS_ASSUME_NONNULL_END
