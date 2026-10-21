import Foundation

public struct PerformanceRun: Equatable, Sendable {
    public let targetKph: Double
    public let startedAt: TimeInterval
    public let finishedAt: TimeInterval
    public let duration: TimeInterval
    public let peakSpeedKph: Double

    public init(
        targetKph: Double,
        startedAt: TimeInterval,
        finishedAt: TimeInterval,
        duration: TimeInterval,
        peakSpeedKph: Double
    ) {
        self.targetKph = targetKph
        self.startedAt = startedAt
        self.finishedAt = finishedAt
        self.duration = duration
        self.peakSpeedKph = peakSpeedKph
    }
}

public enum PerformanceRunAbortReason: String, Equatable, Sendable {
    case speedDrop
    case timeout
    case sensorGap
}

public enum PerformanceRunEvent: Equatable, Sendable {
    case started
    case aborted(PerformanceRunAbortReason)
    case finished(PerformanceRun)
}

public struct PerformanceRunDetector {
    public let targetKph: Double
    public let startSpeedKph: Double
    public let stationaryHoldSeconds: TimeInterval
    public let abortDropKph: Double
    public let timeoutSeconds: TimeInterval
    public let maximumSampleGapSeconds: TimeInterval

    private struct RunState {
        let startedAt: TimeInterval
        var peakSpeedKph: Double
        var lastSpeedKph: Double
        var lastTimestamp: TimeInterval
    }

    private enum Phase {
        case waiting(stationarySince: TimeInterval?)
        case running(RunState)
    }

    private var phase: Phase = .waiting(stationarySince: nil)
    private var lastTimestamp: TimeInterval?
    private var lastSpeedKph: Double?

    public init(
        targetKph: Double = 100,
        startSpeedKph: Double = 1,
        stationaryHoldSeconds: TimeInterval = 1,
        abortDropKph: Double = 3,
        timeoutSeconds: TimeInterval = 30,
        maximumSampleGapSeconds: TimeInterval = 2
    ) {
        let start = max(startSpeedKph, 0.1)
        self.startSpeedKph = start
        self.targetKph = max(targetKph, start + 1)
        self.stationaryHoldSeconds = max(stationaryHoldSeconds, 0)
        self.abortDropKph = max(abortDropKph, 0)
        self.timeoutSeconds = max(timeoutSeconds, 0.1)
        self.maximumSampleGapSeconds = max(maximumSampleGapSeconds, 0.1)
    }

    @discardableResult
    public mutating func ingest(
        speedKph: Double,
        timestamp: TimeInterval
    ) -> [PerformanceRunEvent] {
        guard speedKph.isFinite, speedKph >= 0, timestamp.isFinite else {
            return []
        }
        if let lastTimestamp, timestamp <= lastTimestamp {
            return []
        }

        if let lastTimestamp,
           timestamp - lastTimestamp > maximumSampleGapSeconds {
            let wasRunning: Bool
            if case .running = phase {
                wasRunning = true
            } else {
                wasRunning = false
            }
            phase = .waiting(stationarySince: nil)
            self.lastTimestamp = timestamp
            lastSpeedKph = speedKph
            return wasRunning ? [.aborted(.sensorGap)] : []
        }

        let previousTimestamp = lastTimestamp
        let previousSpeed = lastSpeedKph
        lastTimestamp = timestamp
        lastSpeedKph = speedKph

        switch phase {
        case .waiting(let stationarySince):
            guard speedKph >= startSpeedKph else {
                phase = .waiting(stationarySince: stationarySince ?? timestamp)
                return []
            }
            guard let stationarySince,
                  timestamp - stationarySince >= stationaryHoldSeconds
            else {
                phase = .waiting(stationarySince: nil)
                return []
            }

            let startedAt: TimeInterval
            if let previousTimestamp, let previousSpeed,
               speedKph > previousSpeed {
                let fraction = min(
                    max(
                        (startSpeedKph - previousSpeed) /
                            (speedKph - previousSpeed),
                        0
                    ),
                    1
                )
                startedAt = previousTimestamp +
                    fraction * (timestamp - previousTimestamp)
            } else {
                startedAt = timestamp
            }
            phase = .running(
                RunState(
                    startedAt: startedAt,
                    peakSpeedKph: speedKph,
                    lastSpeedKph: speedKph,
                    lastTimestamp: timestamp
                )
            )
            return [.started]

        case .running(var run):
            run.peakSpeedKph = max(run.peakSpeedKph, speedKph)

            if timestamp - run.startedAt > timeoutSeconds {
                phase = .waiting(stationarySince: nil)
                return [.aborted(.timeout)]
            }

            if speedKph >= targetKph {
                let finishedAt: TimeInterval
                if let previousTimestamp, let previousSpeed,
                   speedKph > previousSpeed {
                    let fraction = min(
                        max(
                            (targetKph - previousSpeed) /
                                (speedKph - previousSpeed),
                            0
                        ),
                        1
                    )
                    finishedAt = previousTimestamp +
                        fraction * (timestamp - previousTimestamp)
                } else {
                    finishedAt = timestamp
                }
                phase = .waiting(stationarySince: nil)
                return [
                    .finished(
                        PerformanceRun(
                            targetKph: targetKph,
                            startedAt: run.startedAt,
                            finishedAt: finishedAt,
                            duration: max(0, finishedAt - run.startedAt),
                            peakSpeedKph: run.peakSpeedKph
                        )
                    )
                ]
            }

            if run.peakSpeedKph - speedKph > abortDropKph {
                phase = .waiting(stationarySince: nil)
                return [.aborted(.speedDrop)]
            }

            run.lastSpeedKph = speedKph
            run.lastTimestamp = timestamp
            phase = .running(run)
            return []
        }
    }

    public mutating func reset() {
        phase = .waiting(stationarySince: nil)
        lastTimestamp = nil
        lastSpeedKph = nil
    }
}
