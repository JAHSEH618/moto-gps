import Foundation
import XCTest
@testable import MotoNavigationCore

final class AttitudeSampleTests: XCTestCase {
    private let receivedAt = Date(timeIntervalSince1970: 1_000)

    private func sample(
        leanCentiDegrees: Int16 = 1_250,
        pitchCentiDegrees: Int16 = -300,
        longitudinalMilliG: Int16 = 420,
        lateralMilliG: Int16 = -125
    ) -> AttitudeSample {
        AttitudeSample(
            monotonicMilliseconds: 5_000,
            leanCentiDegrees: leanCentiDegrees,
            pitchCentiDegrees: pitchCentiDegrees,
            longitudinalMilliG: longitudinalMilliG,
            lateralMilliG: lateralMilliG
        )
    }

    func testSampleReportsDegreesAndG() {
        let value = sample()

        XCTAssertEqual(value.leanDegrees, 12.5, accuracy: 0.000_1)
        XCTAssertEqual(value.pitchDegrees, -3.0, accuracy: 0.000_1)
        XCTAssertEqual(value.longitudinalG, 0.42, accuracy: 0.000_1)
        XCTAssertEqual(value.lateralG, -0.125, accuracy: 0.000_1)
    }

    func testLeanSideFollowsTheDeviceSignConvention() {
        XCTAssertEqual(sample(leanCentiDegrees: 1).leanSide, .right)
        XCTAssertEqual(sample(leanCentiDegrees: -1).leanSide, .left)
        XCTAssertEqual(sample(leanCentiDegrees: 0).leanSide, .upright)
    }

    func testTelemetryReportsNothingBeforeTheFirstSample() {
        let telemetry = AttitudeTelemetry()

        XCTAssertNil(telemetry.sample(at: receivedAt))
        XCTAssertNil(telemetry.readout)
    }

    func testTelemetryKeepsAFreshSample() {
        var telemetry = AttitudeTelemetry()
        telemetry.receive(sample(), at: receivedAt)

        XCTAssertEqual(
            telemetry.sample(at: receivedAt.addingTimeInterval(0.4)),
            sample()
        )
        XCTAssertEqual(telemetry.readout?.receivedAt, receivedAt)
    }

    func testTelemetryDropsASampleOlderThanItsMaximumAge() {
        var telemetry = AttitudeTelemetry()
        telemetry.receive(sample(), at: receivedAt)

        // Exactly at the limit is still fresh; anything beyond it is not, so a
        // stalled link cannot leave a plausible-looking lean on screen.
        XCTAssertNotNil(telemetry.sample(at: receivedAt.addingTimeInterval(0.5)))
        XCTAssertNil(telemetry.sample(at: receivedAt.addingTimeInterval(0.51)))
    }

    func testTelemetryHonoursACallerChosenMaximumAge() {
        var telemetry = AttitudeTelemetry()
        telemetry.receive(sample(), at: receivedAt)

        XCTAssertNil(
            telemetry.sample(at: receivedAt.addingTimeInterval(0.2), maximumAge: 0.1)
        )
    }

    func testTelemetryForgetsTheSampleOnDisconnect() {
        var telemetry = AttitudeTelemetry()
        telemetry.receive(sample(), at: receivedAt)
        telemetry.clear()

        XCTAssertNil(telemetry.readout)
        XCTAssertNil(telemetry.sample(at: receivedAt))
    }

    func testReadoutStampedInTheFutureIsStale() {
        let readout = AttitudeReadout(sample: sample(), receivedAt: receivedAt)

        // A wall clock stepping backwards must not make a dead sample look new.
        XCTAssertLessThan(readout.age(at: receivedAt.addingTimeInterval(-60)), 0)
        XCTAssertTrue(readout.isStale(at: receivedAt.addingTimeInterval(-60)))
    }
}
