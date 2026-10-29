import Foundation

/// One navigation session, recorded automatically (ADR 0002): when it ran,
/// where it was going, how far and how fast the rider actually went, the
/// deepest lean the round screen measured, and the 0-100 runs that finished
/// during it. Runs stay in the race log; a ride only refers to them.
///
/// It never holds a position. Distance is summed from fixes as they arrive and
/// the track itself is thrown away, so a ride list on a lost phone is not a
/// map of where its owner lives.
public struct RideRecord: Codable, Equatable, Identifiable, Sendable {
    public let id: UUID
    public var startedAt: Date
    public var endedAt: Date
    public var destinationName: String?
    public var arrived: Bool
    public var distanceM: Double
    /// Seconds spent above walking pace; stops at lights do not count.
    public var movingSeconds: Double
    public var maxSpeedKph: Double
    /// Deepest lean each side, from the round screen; nil when no sample came.
    public var maxLeanLeftDeg: Double?
    public var maxLeanRightDeg: Double?
    /// Speed at most every `RideRecorder.profileStep` seconds, for the chart.
    public var speedProfile: [RideSpeedSample]
    /// The round screen's finished runs during the ride, by their run id.
    public var runs: [RideRunReference]

    public var durationSeconds: Double { max(0, endedAt.timeIntervalSince(startedAt)) }

    /// Distance over moving time; nil before the bike has moved.
    public var averageMovingSpeedKph: Double? {
        guard movingSeconds >= 1 else { return nil }
        return distanceM / movingSeconds * 3.6
    }
}

public struct RideSpeedSample: Codable, Equatable, Sendable {
    /// Seconds from the start of the ride.
    public var offsetSeconds: Double
    public var speedKph: Double
}

public struct RideRunReference: Codable, Equatable, Sendable {
    public var runID: UInt32
    public var targetKph: Int
    public var elapsedMs: UInt32
}

/// Builds one `RideRecord` from the fixes, attitude samples and runs of a
/// navigation session.
public struct RideRecorder: Sendable {
    /// One profile point per this many seconds is enough for a ride-length
    /// chart and keeps an hour's ride under 400 points.
    public static let profileStep: TimeInterval = 10
    /// Below this a fix is a stop, not movement.
    public static let movingSpeedMps = 1.5
    /// A fix worse than this is not trusted for distance.
    public static let maximumAccuracyM = 50.0
    /// A navigation started and abandoned at once is not a ride.
    public static let minimumDistanceM = 200.0
    public static let minimumSeconds: TimeInterval = 60

    private var record: RideRecord
    private var lastFix: NavigationFix?
    private var lastProfileOffset: Double = -.infinity

    public init(startedAt: Date, destinationName: String?, id: UUID = UUID()) {
        record = RideRecord(
            id: id, startedAt: startedAt, endedAt: startedAt,
            destinationName: destinationName, arrived: false,
            distanceM: 0, movingSeconds: 0, maxSpeedKph: 0,
            maxLeanLeftDeg: nil, maxLeanRightDeg: nil,
            speedProfile: [], runs: []
        )
    }

    public var current: RideRecord { record }

    public mutating func ingest(_ fix: NavigationFix) {
        guard fix.horizontalAccuracyM >= 0, fix.horizontalAccuracyM <= Self.maximumAccuracyM,
              fix.timestamp >= record.startedAt
        else { return }
        if let lastFix {
            let seconds = fix.timestamp.timeIntervalSince(lastFix.timestamp)
            guard seconds > 0 else { return }
            let metres = Self.haversineM(lastFix.coordinate, fix.coordinate)
            // A jump faster than any motorcycle is a fix error, not a ride.
            if metres / seconds <= 90 {
                record.distanceM += metres
                if (fix.speedMps ?? metres / seconds) >= Self.movingSpeedMps {
                    record.movingSeconds += seconds
                }
            }
        }
        lastFix = fix
        record.endedAt = max(record.endedAt, fix.timestamp)
        if let speed = fix.speedMps, speed.isFinite, speed >= 0 {
            let kph = speed * 3.6
            record.maxSpeedKph = max(record.maxSpeedKph, kph)
            let offset = fix.timestamp.timeIntervalSince(record.startedAt)
            if offset - lastProfileOffset >= Self.profileStep {
                lastProfileOffset = offset
                record.speedProfile.append(RideSpeedSample(offsetSeconds: offset, speedKph: kph))
            }
        }
    }

    public mutating func ingest(_ sample: AttitudeSample) {
        let lean = sample.leanDegrees
        guard lean.isFinite else { return }
        switch sample.leanSide {
        case .left: record.maxLeanLeftDeg = max(record.maxLeanLeftDeg ?? 0, -lean)
        case .right: record.maxLeanRightDeg = max(record.maxLeanRightDeg ?? 0, lean)
        case .upright: break
        }
    }

    /// A run the round screen finished while this ride was on. Repeats of the
    /// same run (it is re-sent after every handshake) are kept once.
    public mutating func ingest(_ run: DevicePerformanceRun) {
        guard run.state == .finished,
              !record.runs.contains(where: { $0.runID == run.runID && $0.elapsedMs == run.elapsedMilliseconds })
        else { return }
        record.runs.append(RideRunReference(
            runID: run.runID, targetKph: run.targetKph, elapsedMs: run.elapsedMilliseconds
        ))
    }

    public mutating func markArrived() { record.arrived = true }

    /// The finished ride, or nil when it is too short to be one.
    public func finish(at end: Date) -> RideRecord? {
        var result = record
        result.endedAt = max(result.endedAt, end)
        guard result.distanceM >= Self.minimumDistanceM,
              result.durationSeconds >= Self.minimumSeconds
        else { return nil }
        return result
    }

    static func haversineM(_ first: WGS84Point, _ second: WGS84Point) -> Double {
        let radius = 6_371_008.8
        let lat1 = first.latitudeDeg * .pi / 180
        let lat2 = second.latitudeDeg * .pi / 180
        let dLat = lat2 - lat1
        let dLon = (second.longitudeDeg - first.longitudeDeg) * .pi / 180
        let a = sin(dLat / 2) * sin(dLat / 2) + cos(lat1) * cos(lat2) * sin(dLon / 2) * sin(dLon / 2)
        return 2 * radius * atan2(sqrt(a), sqrt(max(0, 1 - a)))
    }
}

/// Every ride on this phone, newest first.
public struct RideLog: Codable, Equatable, Sendable {
    /// A ride a day for over a year; the file stays a few hundred KB.
    public static let maximumRides = 500

    public private(set) var rides: [RideRecord] = []

    public init(rides: [RideRecord] = []) {
        self.rides = Array(rides.sorted { $0.startedAt > $1.startedAt }.prefix(Self.maximumRides))
    }

    public mutating func append(_ ride: RideRecord) {
        rides.removeAll { $0.id == ride.id }
        rides.insert(ride, at: rides.firstIndex { $0.startedAt < ride.startedAt } ?? rides.endIndex)
        if rides.count > Self.maximumRides { rides.removeLast(rides.count - Self.maximumRides) }
    }

    public mutating func remove(id: UUID) {
        rides.removeAll { $0.id == id }
    }

    public mutating func clear() { rides = [] }

    public var totalDistanceM: Double { rides.reduce(0) { $0 + $1.distanceM } }
}
