import Foundation

/// Theme as the rider picks it. Deliberately not the phone's Vice / Heat
/// palette: this is the device's own Auto / Day / Night, and Vice is what the
/// device shows at night while Heat is what it shows by day.
public enum DeviceSettingsTheme: UInt8, CaseIterable, Sendable {
    case official = 0
    case auto = 1
    case day = 2
    case night = 3
}

public enum DeviceSettingsUnits: UInt8, CaseIterable, Sendable {
    case kph = 0
    case mph = 1
}

/// The lean gauge's full scale. The wire carries a fixed table rather than a
/// range, so an unreviewed scale cannot reach the instrument.
public enum DeviceSettingsLeanScale: UInt8, CaseIterable, Sendable {
    case deg30 = 30
    case deg40 = 40
    case deg45 = 45
    case deg50 = 50
}

/// The 0–100 target. Same reasoning as the lean scale: three reviewed values.
public enum DeviceSettingsPerformanceTarget: UInt16, CaseIterable, Sendable {
    case kph50 = 50
    case kph60 = 60
    case kph100 = 100
}

/// One outbound `SettingsState` (BLE `0x17`): the display settings the rider
/// chose, plus the revision the device orders them by.
///
/// The device is the authority on what is applied. It applies a state whose
/// revision is newer than the one it holds, echoes what it applied, and ignores
/// anything it has already seen — so the phone's only obligation is a revision
/// that keeps increasing, including across a reinstall.
public struct DeviceSettingsBroadcast: Equatable, Sendable {
    /// Capability bit 10. A peer that does not advertise it has never seen
    /// this message type, so it is never sent one.
    public static let capabilityDeviceSettings: UInt32 = 1 << 10

    /// Every page, navigation included.
    public static let allPages: UInt8 = 0x3f

    public let revision: UInt32
    public let theme: DeviceSettingsTheme
    public let brightnessPercent: UInt8
    public let units: DeviceSettingsUnits
    public let leanFullScale: DeviceSettingsLeanScale
    public let performanceTarget: DeviceSettingsPerformanceTarget
    public let visiblePages: UInt8
    public let leanZeroOffsetCdeg: Int16
    /// The phone's day/night fact, not a preference: the device has neither a
    /// clock nor a light sensor, so `auto` can only resolve against this.
    public let ambientNight: Bool
    public let showBuildings: Bool

    public init(
        revision: UInt32,
        theme: DeviceSettingsTheme = .auto,
        brightnessPercent: UInt8 = 80,
        units: DeviceSettingsUnits = .kph,
        leanFullScale: DeviceSettingsLeanScale = .deg45,
        performanceTarget: DeviceSettingsPerformanceTarget = .kph100,
        visiblePages: UInt8 = allPages,
        leanZeroOffsetCdeg: Int16 = 0,
        ambientNight: Bool = false,
        showBuildings: Bool = false
    ) {
        self.revision = revision
        self.theme = theme
        self.brightnessPercent = min(brightnessPercent, 100)
        self.units = units
        self.leanFullScale = leanFullScale
        self.performanceTarget = performanceTarget
        // Navigation cannot be hidden; the device refuses a state without it,
        // so the mask is normalised here rather than sent and rejected.
        self.visiblePages = visiblePages | 0x01
        self.leanZeroOffsetCdeg = max(-3000, min(3000, leanZeroOffsetCdeg))
        self.ambientNight = ambientNight
        self.showBuildings = showBuildings
    }

    public func supports(capabilities: UInt32) -> Bool {
        capabilities & Self.capabilityDeviceSettings != 0
    }

    /// Next revision for a phone that keeps a watermark.
    ///
    /// Revision zero means "never seen" to the device and is not encodable, so
    /// it is skipped. `now` is the floor because the watermark alone cannot
    /// survive an app reinstall: a fresh install starting at 1 would be
    /// silently ignored by a device still holding the previous session's
    /// hundreds. Seconds since the epoch make the first revision of any install
    /// newer than anything a phone could have sent before.
    public static func nextRevision(after current: UInt32, now: Date) -> UInt32 {
        let seconds = UInt32(truncatingIfNeeded: max(0, Int64(now.timeIntervalSince1970)))
        let next = max(current &+ 1, seconds)
        return next == 0 ? 1 : next
    }
}
