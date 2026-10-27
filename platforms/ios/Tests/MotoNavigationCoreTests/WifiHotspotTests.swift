import XCTest
@testable import MotoNavigationCore

final class WifiHotspotTests: XCTestCase {
    func testCapabilityIsProtocolBit14() {
        XCTAssertEqual(WifiHotspot.capability, 1 << 14)
        XCTAssertTrue(WifiHotspot.supports(capabilities: 0x4000 | 0x1))
        XCTAssertFalse(WifiHotspot.supports(capabilities: 0x3FFF))
    }

    func testCredentialRulesMatchTheSharedEncoder() {
        XCTAssertNil(WifiHotspot.validate(ssid: "MOTO iPhone", password: "ride-safe-2026"))
        XCTAssertEqual(WifiHotspot.validate(ssid: "", password: "12345678"), .ssidEmpty)
        XCTAssertNil(WifiHotspot.validate(ssid: String(repeating: "x", count: 32), password: "12345678"))
        XCTAssertEqual(
            WifiHotspot.validate(ssid: String(repeating: "x", count: 33), password: "12345678"),
            .ssidTooLong
        )
        // 11 CJK characters are 33 UTF-8 bytes: the limit is bytes, not characters.
        XCTAssertEqual(
            WifiHotspot.validate(ssid: String(repeating: "摩", count: 11), password: "12345678"),
            .ssidTooLong
        )
        XCTAssertNil(WifiHotspot.validate(ssid: String(repeating: "摩", count: 10), password: "12345678"))

        XCTAssertEqual(WifiHotspot.validate(ssid: "a", password: "1234567"), .passwordTooShort)
        XCTAssertNil(WifiHotspot.validate(ssid: "a", password: String(repeating: "~", count: 63)))
        XCTAssertNil(WifiHotspot.validate(ssid: "a", password: String(repeating: "F", count: 64)))
        XCTAssertEqual(
            WifiHotspot.validate(ssid: "a", password: String(repeating: "g", count: 64)),
            .passwordTooLong
        )
        XCTAssertEqual(
            WifiHotspot.validate(ssid: "a", password: String(repeating: "1", count: 65)),
            .passwordTooLong
        )
        XCTAssertEqual(WifiHotspot.validate(ssid: "a", password: "密码密码密码"), .passwordCharacters)
        XCTAssertEqual(WifiHotspot.validate(ssid: "a", password: "pass\tword"), .passwordCharacters)
    }

    func testStatusRefusesUnknownRawValues() {
        let known = WifiHotspotStatus(
            rawState: 2, rawFailure: 0, deviceHasNetwork: true,
            ipv4Address: "172.20.10.2", rssiDbm: -58
        )
        XCTAssertEqual(known?.state, .connected)
        XCTAssertEqual(known?.rssiDbm, -58)
        XCTAssertNil(WifiHotspotStatus(
            rawState: 4, rawFailure: 0, deviceHasNetwork: true, ipv4Address: "", rssiDbm: 0
        ))
        XCTAssertNil(WifiHotspotStatus(
            rawState: 3, rawFailure: 7, deviceHasNetwork: true, ipv4Address: "", rssiDbm: 0
        ))
    }

    func testFailuresTellTheRiderWhatToDo() {
        let notFound = WifiHotspotStatus(state: .failed, failure: .networkNotFound, deviceHasNetwork: true)
        XCTAssertTrue(notFound.detail?.contains("最大兼容性") == true)
        let wrongPassword = WifiHotspotStatus(state: .failed, failure: .authFailed, deviceHasNetwork: true)
        XCTAssertTrue(wrongPassword.detail?.contains("密码") == true)
        XCTAssertEqual(WifiHotspotStatus(state: .off, deviceHasNetwork: false).title, "未设置")
        XCTAssertEqual(WifiHotspotStatus(state: .off, deviceHasNetwork: true).title, "已关闭")
        XCTAssertNil(WifiHotspotStatus(state: .connected, deviceHasNetwork: true, ipv4Address: "172.20.10.2").detail)
    }
}
