import Foundation
import XCTest
@testable import MotoNavigationCore

final class RideLogTests: XCTestCase {
    private let start = Date(timeIntervalSince1970: 1_800_000_000)

    /// Due north from Jinan at `speedMps`, one fix a second.
    private func fixes(seconds: Int, speedMps: Double, from: Int = 0) -> [NavigationFix] {
        (from ..< from + seconds).map { second in
            let northM = Double(second) * speedMps
            return NavigationFix(
                coordinate: WGS84Point(longitudeDeg: 117.0, latitudeDeg: 36.6 + northM / 111_195),
                horizontalAccuracyM: 5,
                speedMps: speedMps,
                courseDeg: 0,
                timestamp: start.addingTimeInterval(Double(second))
            )
        }
    }

    func testARideSumsDistanceMovingTimeAndTopSpeed() throws {
        var recorder = RideRecorder(startedAt: start, destinationName: "浪潮总部")
        // Two minutes at 10 m/s, then 30 s stopped at a light.
        for fix in fixes(seconds: 121, speedMps: 10) { recorder.ingest(fix) }
        let stopped = start.addingTimeInterval(120)
        for second in 1 ... 30 {
            let last = fixes(seconds: 121, speedMps: 10).last!
            recorder.ingest(NavigationFix(
                coordinate: last.coordinate, horizontalAccuracyM: 5, speedMps: 0,
                courseDeg: nil, timestamp: stopped.addingTimeInterval(Double(second))
            ))
        }
        recorder.markArrived()
        let ride = try XCTUnwrap(recorder.finish(at: start.addingTimeInterval(151)))
        XCTAssertEqual(ride.distanceM, 1_200, accuracy: 2)
        XCTAssertEqual(ride.movingSeconds, 120, accuracy: 0.5)
        XCTAssertEqual(ride.maxSpeedKph, 36, accuracy: 0.01)
        XCTAssertEqual(try XCTUnwrap(ride.averageMovingSpeedKph), 36, accuracy: 0.1)
        XCTAssertEqual(ride.durationSeconds, 151, accuracy: 0.01)
        XCTAssertTrue(ride.arrived)
        XCTAssertEqual(ride.destinationName, "浪潮总部")
        // One profile point per ten seconds.
        XCTAssertEqual(ride.speedProfile.count, 16)
        XCTAssertEqual(ride.speedProfile.first?.offsetSeconds, 0)
    }

    func testAJumpingFixIsNotDistance() throws {
        var recorder = RideRecorder(startedAt: start, destinationName: nil)
        for fix in fixes(seconds: 100, speedMps: 5) { recorder.ingest(fix) }
        // 5 km in one second.
        recorder.ingest(NavigationFix(
            coordinate: WGS84Point(longitudeDeg: 117.0, latitudeDeg: 36.65),
            horizontalAccuracyM: 5, speedMps: 5, courseDeg: 0,
            timestamp: start.addingTimeInterval(100)
        ))
        // A wildly inaccurate fix is ignored outright.
        recorder.ingest(NavigationFix(
            coordinate: WGS84Point(longitudeDeg: 117.1, latitudeDeg: 36.7),
            horizontalAccuracyM: 500, speedMps: 50, courseDeg: 0,
            timestamp: start.addingTimeInterval(101)
        ))
        let ride = try XCTUnwrap(recorder.finish(at: start.addingTimeInterval(101)))
        XCTAssertEqual(ride.distanceM, 495, accuracy: 2)
        XCTAssertEqual(ride.maxSpeedKph, 18, accuracy: 0.01)
    }

    func testAnAbandonedNavigationIsNotARide() {
        var recorder = RideRecorder(startedAt: start, destinationName: nil)
        for fix in fixes(seconds: 20, speedMps: 5) { recorder.ingest(fix) }
        XCTAssertNil(recorder.finish(at: start.addingTimeInterval(300)))
    }

    func testLeanAndRunsComeFromTheRoundScreen() throws {
        var recorder = RideRecorder(startedAt: start, destinationName: nil)
        for fix in fixes(seconds: 100, speedMps: 5) { recorder.ingest(fix) }
        for lean in [-1_250, 3_100, -2_800, 900] as [Int16] {
            recorder.ingest(AttitudeSample(
                monotonicMilliseconds: 0, leanCentiDegrees: lean, pitchCentiDegrees: 0,
                longitudinalMilliG: 0, lateralMilliG: 0
            ))
        }
        let run = DevicePerformanceRun(
            state: .finished, targetKph: 100, runID: 7, elapsedMilliseconds: 6_644, bestMilliseconds: 6_644
        )
        recorder.ingest(run)
        recorder.ingest(run)  // re-sent after a reconnect
        recorder.ingest(DevicePerformanceRun(
            state: .running, targetKph: 100, runID: 8, elapsedMilliseconds: 900, bestMilliseconds: 6_644
        ))
        let ride = try XCTUnwrap(recorder.finish(at: start.addingTimeInterval(100)))
        XCTAssertEqual(try XCTUnwrap(ride.maxLeanLeftDeg), 28, accuracy: 0.001)
        XCTAssertEqual(try XCTUnwrap(ride.maxLeanRightDeg), 31, accuracy: 0.001)
        XCTAssertEqual(ride.runs, [RideRunReference(runID: 7, targetKph: 100, elapsedMs: 6_644)])
    }

    func testTheLogKeepsNewestFirstAndSurvivesARoundTrip() throws {
        var log = RideLog()
        for minutes in [10, 30, 20] {
            var recorder = RideRecorder(startedAt: start.addingTimeInterval(Double(minutes) * 60), destinationName: nil)
            for fix in fixes(seconds: 100, speedMps: 5) {
                recorder.ingest(NavigationFix(
                    coordinate: fix.coordinate, horizontalAccuracyM: 5, speedMps: 5, courseDeg: 0,
                    timestamp: fix.timestamp.addingTimeInterval(Double(minutes) * 60)
                ))
            }
            log.append(try XCTUnwrap(recorder.finish(at: start.addingTimeInterval(Double(minutes) * 60 + 100))))
        }
        XCTAssertEqual(log.rides.map { Int($0.startedAt.timeIntervalSince(start) / 60) }, [30, 20, 10])
        let decoded = try JSONDecoder().decode(RideLog.self, from: JSONEncoder().encode(log))
        XCTAssertEqual(decoded, log)
        log.remove(id: log.rides[1].id)
        XCTAssertEqual(log.rides.count, 2)
        XCTAssertEqual(log.totalDistanceM, 990, accuracy: 4)
    }
}
