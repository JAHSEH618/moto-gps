import Foundation

/// The round screen's 0-100 clock as it last reported it (PerformanceRun,
/// 0x19). The device owns the clock; the phone records what it measured, so
/// the two numbers cannot disagree.
public struct DevicePerformanceRun: Equatable, Sendable {
    public enum State: UInt8, Sendable {
        /// At a standstill, waiting to roll away.
        case armed = 0
        case running = 1
        /// The target was reached; `elapsedMilliseconds` is the time.
        case finished = 2
    }

    public let state: State
    public let targetKph: Int
    /// Bumps when a run starts; zero before the first one.
    public let runID: UInt32
    public let elapsedMilliseconds: UInt32
    /// Best since the device built its tree; zero when there is none.
    public let bestMilliseconds: UInt32

    public init(
        state: State,
        targetKph: Int,
        runID: UInt32,
        elapsedMilliseconds: UInt32,
        bestMilliseconds: UInt32
    ) {
        self.state = state
        self.targetKph = targetKph
        self.runID = runID
        self.elapsedMilliseconds = elapsedMilliseconds
        self.bestMilliseconds = bestMilliseconds
    }
}

public enum RaceLogScope: String, CaseIterable, Sendable {
    case today
    case week
    case all
}

/// Everything the round screen has measured while this phone was listening,
/// kept per day: every finished 0-100 run, time spent at each lean angle, the
/// deepest lean each side, and the G-G envelope. Only real samples are
/// counted; a day with nothing recorded does not exist.
public struct RaceLog: Codable, Equatable, Sendable {
    /// Lean bins of 5° a side; the last one holds 55° and beyond.
    public static let leanBinDegrees = 5.0
    public static let leanBins = 12
    /// G-G directions; index 0 points right (lateral +), counter-clockwise,
    /// so index 4 is acceleration and 12 braking.
    public static let sectors = 16
    /// A longer gap between samples means the link stalled; it is not time
    /// spent at the last lean.
    public static let maximumSampleGap: TimeInterval = 0.5
    /// Oldest runs fall off past this many.
    public static let maximumRuns = 500

    public struct Day: Codable, Equatable, Sendable {
        /// `yyyy-MM-dd` in the rider's calendar.
        public let day: String
        public var leftSeconds: [Double]
        public var rightSeconds: [Double]
        public var envelope: [Double]
        public var maxLeftDegrees: Double?
        public var maxRightDegrees: Double?
        public var maxAccelerationG: Double?
        public var maxBrakingG: Double?
        public var maxLateralLeftG: Double?
        public var maxLateralRightG: Double?
        /// Time covered by attitude samples. Telemetry time, not riding time:
        /// a connected screen parked on a bench counts too.
        public var seconds: Double

        init(day: String) {
            self.day = day
            leftSeconds = Array(repeating: 0, count: RaceLog.leanBins)
            rightSeconds = Array(repeating: 0, count: RaceLog.leanBins)
            envelope = Array(repeating: 0, count: RaceLog.sectors)
            seconds = 0
        }
    }

    public struct Run: Codable, Equatable, Sendable {
        public let finishedAt: Date
        public let seconds: Double
        public let targetKph: Int

        public init(finishedAt: Date, seconds: Double, targetKph: Int) {
            self.finishedAt = finishedAt
            self.seconds = seconds
            self.targetKph = targetKph
        }
    }

    private struct RunKey: Codable, Equatable, Sendable {
        let runID: UInt32
        let elapsedMilliseconds: UInt32
    }

    public private(set) var days: [Day] = []
    public private(set) var runs: [Run] = []
    /// The device re-sends its current state after every handshake; this is
    /// how a finished run is recorded once.
    private var lastRun: RunKey?
    private var lastSampleAt: Date?

    private enum CodingKeys: String, CodingKey {
        case days, runs, lastRun
    }

    public init() {}

    public var isEmpty: Bool { days.isEmpty && runs.isEmpty }

    public mutating func record(
        _ sample: AttitudeSample,
        at date: Date,
        calendar: Calendar = .current
    ) {
        let gap = lastSampleAt.map { date.timeIntervalSince($0) } ?? 0
        lastSampleAt = date
        let key = Self.dayKey(date, calendar: calendar)
        if days.last?.day != key { days.append(Day(day: key)) }
        var day = days[days.count - 1]
        defer { days[days.count - 1] = day }

        let lean = sample.leanDegrees
        let magnitude = abs(lean)
        if lean > 0 {
            day.maxRightDegrees = max(day.maxRightDegrees ?? 0, magnitude)
        } else if lean < 0 {
            day.maxLeftDegrees = max(day.maxLeftDegrees ?? 0, magnitude)
        }
        // The first sample after a stall has no duration of its own.
        if gap > 0, gap <= Self.maximumSampleGap {
            let bin = min(Int(magnitude / Self.leanBinDegrees), Self.leanBins - 1)
            if lean < 0 {
                day.leftSeconds[bin] += gap
            } else {
                day.rightSeconds[bin] += gap
            }
            day.seconds += gap
        }

        let x = sample.lateralG
        let y = sample.longitudinalG
        if y > 0 { day.maxAccelerationG = max(day.maxAccelerationG ?? 0, y) }
        if y < 0 { day.maxBrakingG = max(day.maxBrakingG ?? 0, -y) }
        if x > 0 { day.maxLateralRightG = max(day.maxLateralRightG ?? 0, x) }
        if x < 0 { day.maxLateralLeftG = max(day.maxLateralLeftG ?? 0, -x) }
        let total = (x * x + y * y).squareRoot()
        guard total.isFinite, total > 0 else { return }
        let angle = atan2(y, x)
        let turn = (angle < 0 ? angle + 2 * .pi : angle) / (2 * .pi)
        let index = Int((turn * Double(Self.sectors)).rounded()) % Self.sectors
        day.envelope[index] = max(day.envelope[index], total)
    }

    /// Records a finished run once. Returns true when it was new.
    @discardableResult
    public mutating func record(_ run: DevicePerformanceRun, at date: Date) -> Bool {
        guard run.state == .finished, run.elapsedMilliseconds > 0 else { return false }
        let key = RunKey(runID: run.runID, elapsedMilliseconds: run.elapsedMilliseconds)
        guard key != lastRun else { return false }
        lastRun = key
        runs.append(Run(
            finishedAt: date,
            seconds: TimeInterval(run.elapsedMilliseconds) / 1_000,
            targetKph: run.targetKph
        ))
        if runs.count > Self.maximumRuns { runs.removeFirst(runs.count - Self.maximumRuns) }
        return true
    }

    public mutating func clear() {
        self = RaceLog()
    }

    public func summary(
        _ scope: RaceLogScope,
        now: Date,
        calendar: Calendar = .current
    ) -> RaceSummary {
        let since: Date? = {
            let today = calendar.startOfDay(for: now)
            switch scope {
            case .today: return today
            case .week: return calendar.date(byAdding: .day, value: -6, to: today)
            case .all: return nil
            }
        }()
        let firstDay = since.map { Self.dayKey($0, calendar: calendar) }
        let scopedDays = days.filter { firstDay == nil || $0.day >= firstDay! }
        let scopedRuns = runs.filter { since == nil || $0.finishedAt >= since! }
        return RaceSummary(days: scopedDays, runs: scopedRuns)
    }

    static func dayKey(_ date: Date, calendar: Calendar) -> String {
        let parts = calendar.dateComponents([.year, .month, .day], from: date)
        return String(format: "%04d-%02d-%02d", parts.year ?? 0, parts.month ?? 0, parts.day ?? 0)
    }
}

/// One scope of the log, folded for the dashboard.
public struct RaceSummary: Equatable, Sendable {
    public let runs: [RaceLog.Run]
    public let leftSeconds: [Double]
    public let rightSeconds: [Double]
    public let envelope: [Double]
    public let maxLeftDegrees: Double?
    public let maxRightDegrees: Double?
    public let maxAccelerationG: Double?
    public let maxBrakingG: Double?
    public let maxLateralLeftG: Double?
    public let maxLateralRightG: Double?
    public let seconds: Double

    init(days: [RaceLog.Day], runs: [RaceLog.Run]) {
        self.runs = runs
        func sum(_ path: KeyPath<RaceLog.Day, [Double]>, count: Int) -> [Double] {
            days.reduce(into: Array(repeating: 0, count: count)) { total, day in
                for index in 0..<count { total[index] += day[keyPath: path][index] }
            }
        }
        func peak(_ path: KeyPath<RaceLog.Day, Double?>) -> Double? {
            days.compactMap { $0[keyPath: path] }.max()
        }
        leftSeconds = sum(\.leftSeconds, count: RaceLog.leanBins)
        rightSeconds = sum(\.rightSeconds, count: RaceLog.leanBins)
        envelope = days.reduce(into: Array(repeating: 0, count: RaceLog.sectors)) { total, day in
            for index in 0..<RaceLog.sectors { total[index] = max(total[index], day.envelope[index]) }
        }
        maxLeftDegrees = peak(\.maxLeftDegrees)
        maxRightDegrees = peak(\.maxRightDegrees)
        maxAccelerationG = peak(\.maxAccelerationG)
        maxBrakingG = peak(\.maxBrakingG)
        maxLateralLeftG = peak(\.maxLateralLeftG)
        maxLateralRightG = peak(\.maxLateralRightG)
        seconds = days.reduce(0) { $0 + $1.seconds }
    }

    public var isEmpty: Bool { runs.isEmpty && seconds == 0 && maxLeftDegrees == nil && maxRightDegrees == nil }

    public var bestRun: RaceLog.Run? { runs.min { $0.seconds < $1.seconds } }

    public var averageRunSeconds: Double? {
        runs.isEmpty ? nil : runs.reduce(0) { $0 + $1.seconds } / Double(runs.count)
    }

    /// Share of the recorded time spent within 5° of upright.
    public var uprightShare: Double? {
        seconds > 0 ? (leftSeconds[0] + rightSeconds[0]) / seconds : nil
    }
}
