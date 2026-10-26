import Foundation
import XCTest
@testable import MotoNavigationCore

final class RaceTelemetryTests: XCTestCase {
    private var calendar: Calendar = {
        var calendar = Calendar(identifier: .gregorian)
        calendar.timeZone = TimeZone(identifier: "Asia/Tokyo")!
        return calendar
    }()

    /// 2026-10-26 12:00 in Tokyo.
    private let noon = Date(timeIntervalSince1970: 1_793_070_000)

    private func sample(lean: Int16 = 0, lateral: Int16 = 0, longitudinal: Int16 = 0) -> AttitudeSample {
        AttitudeSample(
            monotonicMilliseconds: 1,
            leanCentiDegrees: lean,
            pitchCentiDegrees: 0,
            longitudinalMilliG: longitudinal,
            lateralMilliG: lateral
        )
    }

    private func finished(id: UInt32, ms: UInt32) -> DevicePerformanceRun {
        DevicePerformanceRun(state: .finished, targetKph: 100, runID: id,
                             elapsedMilliseconds: ms, bestMilliseconds: ms)
    }

    func testAnEmptyLogClaimsNothing() {
        let summary = RaceLog().summary(.all, now: noon, calendar: calendar)
        XCTAssertTrue(summary.isEmpty)
        XCTAssertNil(summary.bestRun)
        XCTAssertNil(summary.averageRunSeconds)
        XCTAssertNil(summary.maxLeftDegrees)
        XCTAssertNil(summary.uprightShare)
    }

    func testTimeAtLeanIsCountedFromTheGapBetweenSamples() {
        var log = RaceLog()
        // 30° right for 1 s at 10 Hz, then 12° left for 0.5 s.
        for step in 0...10 {
            log.record(sample(lean: 3_000), at: noon.addingTimeInterval(Double(step) * 0.1), calendar: calendar)
        }
        for step in 1...5 {
            log.record(sample(lean: -1_200), at: noon.addingTimeInterval(1 + Double(step) * 0.1), calendar: calendar)
        }
        let summary = log.summary(.today, now: noon, calendar: calendar)
        XCTAssertEqual(summary.rightSeconds[6], 1.0, accuracy: 0.000_1)   // 30..35°
        XCTAssertEqual(summary.leftSeconds[2], 0.5, accuracy: 0.000_1)    // 10..15°
        XCTAssertEqual(summary.seconds, 1.5, accuracy: 0.000_1)
        XCTAssertEqual(summary.maxRightDegrees ?? 0, 30, accuracy: 0.000_1)
        XCTAssertEqual(summary.maxLeftDegrees ?? 0, 12, accuracy: 0.000_1)
    }

    func testAStalledLinkIsNotTimeAtTheLastLean() {
        var log = RaceLog()
        log.record(sample(lean: 4_000), at: noon, calendar: calendar)
        log.record(sample(lean: 4_000), at: noon.addingTimeInterval(30), calendar: calendar)
        XCTAssertEqual(log.summary(.all, now: noon, calendar: calendar).seconds, 0)
    }

    func testGIsKeptPerDirection() {
        var log = RaceLog()
        log.record(sample(lateral: 800), at: noon, calendar: calendar)
        log.record(sample(longitudinal: 400), at: noon, calendar: calendar)
        log.record(sample(lateral: -600), at: noon, calendar: calendar)
        log.record(sample(longitudinal: -900), at: noon, calendar: calendar)
        let summary = log.summary(.all, now: noon, calendar: calendar)
        XCTAssertEqual(summary.envelope[0], 0.8, accuracy: 0.000_1)
        XCTAssertEqual(summary.envelope[4], 0.4, accuracy: 0.000_1)
        XCTAssertEqual(summary.envelope[8], 0.6, accuracy: 0.000_1)
        XCTAssertEqual(summary.envelope[12], 0.9, accuracy: 0.000_1)
        XCTAssertEqual(summary.maxAccelerationG ?? 0, 0.4, accuracy: 0.000_1)
        XCTAssertEqual(summary.maxBrakingG ?? 0, 0.9, accuracy: 0.000_1)
        XCTAssertEqual(summary.maxLateralLeftG ?? 0, 0.6, accuracy: 0.000_1)
        XCTAssertEqual(summary.maxLateralRightG ?? 0, 0.8, accuracy: 0.000_1)
    }

    func testAFinishedRunIsRecordedOnceEvenWhenTheDeviceResendsIt() {
        var log = RaceLog()
        XCTAssertTrue(log.record(finished(id: 1, ms: 4_870), at: noon))
        // The handshake after a reconnect repeats the same state.
        XCTAssertFalse(log.record(finished(id: 1, ms: 4_870), at: noon.addingTimeInterval(60)))
        // Armed and running are not results.
        XCTAssertFalse(log.record(DevicePerformanceRun(state: .running, targetKph: 100, runID: 2,
                                                       elapsedMilliseconds: 1_000, bestMilliseconds: 4_870),
                                  at: noon))
        XCTAssertTrue(log.record(finished(id: 2, ms: 4_610), at: noon.addingTimeInterval(120)))
        let summary = log.summary(.all, now: noon, calendar: calendar)
        XCTAssertEqual(summary.runs.count, 2)
        XCTAssertEqual(summary.bestRun?.seconds ?? 0, 4.61, accuracy: 0.000_1)
        XCTAssertEqual(summary.averageRunSeconds ?? 0, 4.74, accuracy: 0.000_1)
    }

    func testScopesSelectDaysAndRuns() {
        var log = RaceLog()
        let tenDaysAgo = noon.addingTimeInterval(-10 * 86_400)
        let threeDaysAgo = noon.addingTimeInterval(-3 * 86_400)
        log.record(sample(lean: 5_000), at: tenDaysAgo, calendar: calendar)
        log.record(finished(id: 1, ms: 5_200), at: tenDaysAgo)
        log.record(sample(lean: 3_500), at: threeDaysAgo, calendar: calendar)
        log.record(finished(id: 2, ms: 4_900), at: threeDaysAgo)
        log.record(sample(lean: 2_000), at: noon, calendar: calendar)

        let today = log.summary(.today, now: noon, calendar: calendar)
        XCTAssertEqual(today.maxRightDegrees ?? 0, 20, accuracy: 0.000_1)
        XCTAssertTrue(today.runs.isEmpty)
        let week = log.summary(.week, now: noon, calendar: calendar)
        XCTAssertEqual(week.maxRightDegrees ?? 0, 35, accuracy: 0.000_1)
        XCTAssertEqual(week.runs.count, 1)
        let all = log.summary(.all, now: noon, calendar: calendar)
        XCTAssertEqual(all.maxRightDegrees ?? 0, 50, accuracy: 0.000_1)
        XCTAssertEqual(all.runs.count, 2)
    }

    func testTheLogSurvivesARoundTripAndStillDeduplicates() throws {
        var log = RaceLog()
        log.record(sample(lean: 2_500), at: noon, calendar: calendar)
        log.record(finished(id: 7, ms: 5_010), at: noon)
        let data = try JSONEncoder().encode(log)
        var restored = try JSONDecoder().decode(RaceLog.self, from: data)
        XCTAssertEqual(restored.days, log.days)
        XCTAssertEqual(restored.runs, log.runs)
        XCTAssertFalse(restored.record(finished(id: 7, ms: 5_010), at: noon))
        restored.clear()
        XCTAssertTrue(restored.isEmpty)
    }
}
