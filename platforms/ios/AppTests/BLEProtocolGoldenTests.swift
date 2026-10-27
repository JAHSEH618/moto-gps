import Foundation
import XCTest
@testable import MOTO_GPS

/// Runs the bundled v1 golden check through the Objective-C++ bridge, so the
/// Swift side and shared/ble_protocol agree on every pinned vector, WifiConfig
/// and WifiStatus included.
final class BLEProtocolGoldenTests: XCTestCase {
    func testBundledGoldenVectorsMatchTheSharedCodec() throws {
        try BLEProtocolGoldenCheck.verify(codec: MotoBLEProtocolCodec(maximumFrameSize: 182))
    }

    func testWifiConfigIsAckedAndRefusesWhatTheHotspotWouldNot() throws {
        let codec = MotoBLEProtocolCodec(maximumFrameSize: 182)
        let config = MotoBLEWifiConfigInput()
        config.ssid = "MOTO iPhone"
        config.password = "ride-safe-2026"
        let frames = try codec.encodeWifiConfig(config)
        XCTAssertEqual(frames.count, 1)
        // Header byte 3 is the frame flags: Start | End | AckRequested.
        XCTAssertEqual(frames[0][3] & 0x07, 0x07)
        XCTAssertEqual(frames[0][2], 0x1A)

        config.password = "short"
        XCTAssertThrowsError(try codec.encodeWifiConfig(config))
        config.password = "ride-safe-2026"
        config.ssid = String(repeating: "x", count: 33)
        XCTAssertThrowsError(try codec.encodeWifiConfig(config))

        // Forgetting sends no credential, whatever the fields still hold.
        config.enabled = false
        let forget = try codec.encodeWifiConfigPayload(forTesting: config)
        XCTAssertEqual(forget, Data([0x01, 0x00, 0x00, 0x00]))
    }
}
