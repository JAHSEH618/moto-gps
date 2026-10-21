import XCTest
@testable import MotoNavigationCore

final class ChinaCoordinateTransformTests: XCTestCase {
    private let beijingWGS84 = WGS84Point(longitudeDeg: 116.397_389, latitudeDeg: 39.908_722)

    func testBeijingOffsetMatchesThePublishedGCJShift() {
        let gcj = ChinaCoordinateTransform.wgs84ToGCJ02(beijingWGS84)
        // The transform moves a Beijing point roughly 500 m north-east.
        XCTAssertGreaterThan(gcj.longitudeDeg - beijingWGS84.longitudeDeg, 0.004)
        XCTAssertLessThan(gcj.longitudeDeg - beijingWGS84.longitudeDeg, 0.008)
        XCTAssertGreaterThan(gcj.latitudeDeg - beijingWGS84.latitudeDeg, 0.0005)
        XCTAssertLessThan(gcj.latitudeDeg - beijingWGS84.latitudeDeg, 0.004)
    }

    func testRoundTripReturnsTheOriginalPoint() {
        let gcj = ChinaCoordinateTransform.wgs84ToGCJ02(beijingWGS84)
        let restored = ChinaCoordinateTransform.gcj02ToWGS84(gcj)
        XCTAssertEqual(restored.longitudeDeg, beijingWGS84.longitudeDeg, accuracy: 1e-6)
        XCTAssertEqual(restored.latitudeDeg, beijingWGS84.latitudeDeg, accuracy: 1e-6)
    }

    func testOutsideMainlandChinaIsUnchanged() {
        // The transform must not displace a point the offset does not apply to;
        // a home in Tokyo would otherwise drift by half a kilometre.
        let tokyo = WGS84Point(longitudeDeg: 139.691_7, latitudeDeg: 35.689_5)
        let gcj = ChinaCoordinateTransform.wgs84ToGCJ02(tokyo)
        XCTAssertEqual(gcj.longitudeDeg, tokyo.longitudeDeg)
        XCTAssertEqual(gcj.latitudeDeg, tokyo.latitudeDeg)
        XCTAssertFalse(ChinaCoordinateTransform.isInMainlandChina(tokyo))
    }

    func testJinanHomeConvertsOnce() {
        // A home saved in Jinan: the device refuses anything but GCJ-02, and
        // converting twice would be visible as a 500 m blip offset.
        let home = WGS84Point(longitudeDeg: 117.122_880, latitudeDeg: 36.674_512)
        let once = ChinaCoordinateTransform.wgs84ToGCJ02(home)
        let twice = ChinaCoordinateTransform.wgs84ToGCJ02(
            WGS84Point(longitudeDeg: once.longitudeDeg, latitudeDeg: once.latitudeDeg)
        )
        XCTAssertGreaterThan(abs(once.longitudeDeg - home.longitudeDeg), 0.003)
        XCTAssertGreaterThan(abs(twice.longitudeDeg - once.longitudeDeg), 0.003)
    }
}

final class HomeLocationBroadcastTests: XCTestCase {
    private let home = WGS84Point(longitudeDeg: 117.122_880, latitudeDeg: 36.674_512)

    func testCapabilityBitIsNine() {
        XCTAssertEqual(HomeLocationBroadcast.capabilityHomeLocation, 1 << 9)
    }

    func testRevisionSkipsZero() {
        XCTAssertEqual(HomeLocationBroadcast.nextRevision(after: 0), 1)
        XCTAssertEqual(HomeLocationBroadcast.nextRevision(after: 41), 42)
        XCTAssertEqual(HomeLocationBroadcast.nextRevision(after: .max), 1)
    }

    func testClearedHomeIsStillAValidBroadcast() {
        let cleared = HomeLocationBroadcast(
            revision: 7,
            address: nil
        )
        XCTAssertFalse(cleared.hasHome)
        XCTAssertNil(cleared.coordinate)
        XCTAssertEqual(cleared.revision, 7)
    }

    func testAddressIsConvertedOnceAndKeepsTheRevision() {
        let broadcast = HomeLocationBroadcast(revision: 3, address: home)
        XCTAssertTrue(broadcast.hasHome)
        XCTAssertEqual(broadcast.revision, 3)
        let expected = ChinaCoordinateTransform.wgs84ToGCJ02(home)
        XCTAssertEqual(broadcast.coordinate?.longitudeDeg, expected.longitudeDeg)
        XCTAssertEqual(broadcast.coordinate?.latitudeDeg, expected.latitudeDeg)
    }

    func testCapabilityGate() {
        let broadcast = HomeLocationBroadcast(revision: 1, address: home)
        XCTAssertFalse(broadcast.supports(capabilities: 0))
        XCTAssertTrue(
            broadcast.supports(
                capabilities: HomeLocationBroadcast.capabilityHomeLocation | 0x1
            )
        )
    }
}
