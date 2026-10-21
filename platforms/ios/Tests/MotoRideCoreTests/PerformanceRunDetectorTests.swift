import XCTest
@testable import MotoRideCore

final class PerformanceRunDetectorTests: XCTestCase {
    private func feedConstant(
        _ detector: inout PerformanceRunDetector,
        speedKph: Double,
        from start: TimeInterval,
        to end: TimeInterval,
        step: TimeInterval = 0.1
    ) -> [PerformanceRunEvent] {
        var events: [PerformanceRunEvent] = []
        var time = start
        while time <= end + 1e-9 {
            events += detector.ingest(speedKph: speedKph, timestamp: time)
            time += step
        }
        return events
    }

    private func feedRamp(
        _ detector: inout PerformanceRunDetector,
        fromSpeed: Double,
        toSpeed: Double,
        from start: TimeInterval,
        to end: TimeInterval,
        step: TimeInterval = 0.1
    ) -> [PerformanceRunEvent] {
        var events: [PerformanceRunEvent] = []
        let sampleCount = max(1, Int(round((end - start) / step)))
        for index in 0...sampleCount {
            let time = index == sampleCount
                ? end
                : start + Double(index) * step
            let fraction = (time - start) / (end - start)
            let speed = fromSpeed + (toSpeed - fromSpeed) * fraction
            events += detector.ingest(speedKph: speed, timestamp: time)
        }
        return events
    }

    private func finishedRuns(_ events: [PerformanceRunEvent]) -> [PerformanceRun] {
        events.compactMap {
            if case let .finished(run) = $0 { return run }
            return nil
        }
    }

    private func abortReasons(_ events: [PerformanceRunEvent]) -> [PerformanceRunAbortReason] {
        events.compactMap {
            if case let .aborted(reason) = $0 { return reason }
            return nil
        }
    }

    func testAcceleratingFromStationaryProducesAExactFourSecondRun() {
        var detector = PerformanceRunDetector()
        var events = feedConstant(&detector, speedKph: 0, from: 0, to: 1.0)
        events += feedRamp(
            &detector,
            fromSpeed: 1,
            toSpeed: 100,
            from: 1.1,
            to: 5.1
        )

        XCTAssertEqual(events.filter { $0 == .started }.count, 1)
        let runs = finishedRuns(events)
        XCTAssertEqual(runs.count, 1)
        guard let run = runs.first else { return }
        XCTAssertEqual(run.targetKph, 100)
        XCTAssertEqual(run.duration, 4.0, accuracy: 0.001)
        XCTAssertEqual(run.peakSpeedKph, 100, accuracy: 0.001)
    }

    func testNoStartWithoutAOneSecondStationaryHold() {
        var detector = PerformanceRunDetector()
        var events = feedConstant(&detector, speedKph: 0, from: 0, to: 0.5)
        events += feedRamp(
            &detector,
            fromSpeed: 1,
            toSpeed: 50,
            from: 0.6,
            to: 2.0
        )
        XCTAssertFalse(events.contains(.started))
        XCTAssertTrue(finishedRuns(events).isEmpty)
    }

    func testAbortsWhenSpeedDropsMoreThanThreeKph() {
        var detector = PerformanceRunDetector()
        var events = feedConstant(&detector, speedKph: 0, from: 0, to: 1.0)
        events += feedRamp(
            &detector,
            fromSpeed: 1,
            toSpeed: 50,
            from: 1.1,
            to: 2.0
        )
        events += detector.ingest(speedKph: 45, timestamp: 2.1)

        XCTAssertEqual(abortReasons(events), [.speedDrop])
        XCTAssertTrue(finishedRuns(events).isEmpty)
    }

    func testAbortsAfterThirtySeconds() {
        var detector = PerformanceRunDetector()
        var events = feedConstant(&detector, speedKph: 0, from: 0, to: 1.0)
        events += detector.ingest(speedKph: 20, timestamp: 1.1)
        events += feedConstant(&detector, speedKph: 20, from: 1.2, to: 32.2)

        XCTAssertEqual(abortReasons(events), [.timeout])
        XCTAssertTrue(finishedRuns(events).isEmpty)
    }

    func testAbortsOnSensorGap() {
        var detector = PerformanceRunDetector()
        var events = feedConstant(&detector, speedKph: 0, from: 0, to: 1.0)
        events += detector.ingest(speedKph: 20, timestamp: 1.1)
        events += detector.ingest(speedKph: 20, timestamp: 4.2)

        XCTAssertEqual(abortReasons(events), [.sensorGap])
    }

    func testTargetIsConfigurableForScooters() {
        var detector = PerformanceRunDetector(targetKph: 50)
        var events = feedConstant(&detector, speedKph: 0, from: 0, to: 1.0)
        events += feedRamp(
            &detector,
            fromSpeed: 1,
            toSpeed: 60,
            from: 1.1,
            to: 3.1
        )

        let runs = finishedRuns(events)
        XCTAssertEqual(runs.count, 1)
        guard let run = runs.first else { return }
        XCTAssertEqual(run.targetKph, 50)
        XCTAssertEqual(run.duration, 1.661017, accuracy: 0.01)
    }

    func testSecondRunRequiresANewStationaryHold() {
        var detector = PerformanceRunDetector()
        var events = feedConstant(&detector, speedKph: 0, from: 0, to: 1.0)
        events += feedRamp(
            &detector,
            fromSpeed: 1,
            toSpeed: 100,
            from: 1.1,
            to: 5.1
        )

        // A new acceleration without stopping must not start another run.
        events += feedConstant(&detector, speedKph: 50, from: 5.2, to: 6.0)
        XCTAssertEqual(events.filter { $0 == .started }.count, 1)

        // Stop, hold for one second, then run again.
        events += feedConstant(&detector, speedKph: 0, from: 6.1, to: 7.2)
        events += feedRamp(
            &detector,
            fromSpeed: 1,
            toSpeed: 100,
            from: 7.3,
            to: 11.3
        )
        XCTAssertEqual(events.filter { $0 == .started }.count, 2)
        XCTAssertEqual(finishedRuns(events).count, 2)
    }

    func testNonFiniteAndNegativeSpeedsAreIgnored() {
        var detector = PerformanceRunDetector()
        XCTAssertTrue(detector.ingest(speedKph: .nan, timestamp: 0).isEmpty)
        XCTAssertTrue(detector.ingest(speedKph: -4, timestamp: 0.1).isEmpty)

        var events = feedConstant(&detector, speedKph: 0, from: 0.2, to: 1.3)
        events += feedRamp(
            &detector,
            fromSpeed: 1,
            toSpeed: 100,
            from: 1.4,
            to: 5.4
        )
        XCTAssertEqual(finishedRuns(events).count, 1)
    }
}
