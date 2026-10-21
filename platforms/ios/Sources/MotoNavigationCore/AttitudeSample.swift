import Foundation

/// Lean, pitch and acceleration as the device measured them.
///
/// The wire layout is not repeated here: `MotoBLEProtocolBridge` decodes
/// `AttitudeSample` (0x15) from `shared/ble_protocol` and hands these values
/// across. This type owns the units and the freshness rule, which is the part
/// the UI has to get right.
public struct AttitudeSample: Equatable, Sendable {
    public let monotonicMilliseconds: UInt32
    public let leanCentiDegrees: Int16
    public let pitchCentiDegrees: Int16
    public let longitudinalMilliG: Int16
    public let lateralMilliG: Int16

    public init(
        monotonicMilliseconds: UInt32,
        leanCentiDegrees: Int16,
        pitchCentiDegrees: Int16,
        longitudinalMilliG: Int16,
        lateralMilliG: Int16
    ) {
        self.monotonicMilliseconds = monotonicMilliseconds
        self.leanCentiDegrees = leanCentiDegrees
        self.pitchCentiDegrees = pitchCentiDegrees
        self.longitudinalMilliG = longitudinalMilliG
        self.lateralMilliG = lateralMilliG
    }

    public var leanDegrees: Double {
        Double(leanCentiDegrees) / 100
    }

    public var pitchDegrees: Double {
        Double(pitchCentiDegrees) / 100
    }

    public var longitudinalG: Double {
        Double(longitudinalMilliG) / 1_000
    }

    public var lateralG: Double {
        Double(lateralMilliG) / 1_000
    }

    /// The device reports positive Lean for a right-hand lean and negative for
    /// a left-hand lean; the LVGL Lean page uses the same sign.
    public var leanSide: LeanSide {
        if leanCentiDegrees > 0 { return .right }
        if leanCentiDegrees < 0 { return .left }
        return .upright
    }
}

public enum LeanSide: String, Sendable {
    case left
    case upright
    case right
}

/// A sample plus the moment the phone received it.
///
/// A BLE link can go quiet without reporting a disconnect, so a lean value has
/// to expire on its own. Showing the last known lean after the link stalled
/// would tell the rider they are upright while they are not.
public struct AttitudeReadout: Equatable, Sendable {
    /// The device samples the IMU faster than this; anything older means the
    /// telemetry stopped arriving.
    public static let defaultMaximumAge: TimeInterval = 0.5

    public let sample: AttitudeSample
    public let receivedAt: Date

    public init(sample: AttitudeSample, receivedAt: Date) {
        self.sample = sample
        self.receivedAt = receivedAt
    }

    public func age(at now: Date) -> TimeInterval {
        now.timeIntervalSince(receivedAt)
    }

    public func isStale(
        at now: Date,
        maximumAge: TimeInterval = AttitudeReadout.defaultMaximumAge
    ) -> Bool {
        let age = age(at: now)
        // A readout stamped in the future means the clock moved, not that the
        // data is fresh; refuse it for the same reason as an old one.
        return age < 0 || age > maximumAge
    }
}

/// Holds the latest readout and forgets it when the link drops.
public struct AttitudeTelemetry: Equatable, Sendable {
    public private(set) var readout: AttitudeReadout?

    public init(readout: AttitudeReadout? = nil) {
        self.readout = readout
    }

    public mutating func receive(_ sample: AttitudeSample, at receivedAt: Date) {
        readout = AttitudeReadout(sample: sample, receivedAt: receivedAt)
    }

    public mutating func clear() {
        readout = nil
    }

    /// The sample to display, or `nil` when nothing has arrived yet or the last
    /// sample has expired.
    public func sample(
        at now: Date,
        maximumAge: TimeInterval = AttitudeReadout.defaultMaximumAge
    ) -> AttitudeSample? {
        guard let readout, !readout.isStale(at: now, maximumAge: maximumAge)
        else { return nil }
        return readout.sample
    }
}
