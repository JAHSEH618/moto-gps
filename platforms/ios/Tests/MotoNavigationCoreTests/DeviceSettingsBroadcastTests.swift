import Foundation
import XCTest
@testable import MotoNavigationCore

final class DeviceSettingsBroadcastTests: XCTestCase {
    private let epoch = Date(timeIntervalSince1970: 1_700_000_000)

    private func settings(
        revision: UInt32 = 5,
        brightness: UInt8 = 65,
        pages: UInt8 = DeviceSettingsBroadcast.allPages & ~0x20
    ) -> DeviceSettingsBroadcast {
        DeviceSettingsBroadcast(
            revision: revision,
            theme: .night,
            brightnessPercent: brightness,
            units: .mph,
            leanFullScale: .deg40,
            performanceTarget: .kph60,
            visiblePages: pages,
            leanZeroOffsetCdeg: -125,
            ambientNight: true,
            showBuildings: true
        )
    }

    /// The wire values are a fixed table; the enums are what keeps an
    /// unreviewed scale or target from reaching the instrument.
    func testTablesMatchTheProtocolDoc() {
        XCTAssertEqual(DeviceSettingsTheme.allCases.map(\.rawValue), [0, 1, 2, 3])
        XCTAssertEqual(DeviceSettingsLeanScale.allCases.map(\.rawValue), [30, 40, 45, 50])
        XCTAssertEqual(DeviceSettingsPerformanceTarget.allCases.map(\.rawValue), [50, 60, 100])
        XCTAssertEqual(DeviceSettingsBroadcast.allPages, 0x3f)
        XCTAssertEqual(DeviceSettingsBroadcast.capabilityDeviceSettings, 1 << 10)
    }

    func testHydrationKeepsWhatTheRiderPicked() {
        let value = settings()

        XCTAssertEqual(value.revision, 5)
        XCTAssertEqual(value.theme, .night)
        XCTAssertEqual(value.brightnessPercent, 65)
        XCTAssertEqual(value.units, .mph)
        XCTAssertEqual(value.leanFullScale, .deg40)
        XCTAssertEqual(value.performanceTarget, .kph60)
        XCTAssertEqual(value.leanZeroOffsetCdeg, -125)
        XCTAssertTrue(value.ambientNight)
        XCTAssertTrue(value.showBuildings)
    }

    func testDefaultsAreTheDevicesOwnDefaults() {
        let value = DeviceSettingsBroadcast(revision: 1)

        XCTAssertEqual(value.theme, .auto)
        XCTAssertEqual(value.brightnessPercent, 80)
        XCTAssertEqual(value.units, .kph)
        XCTAssertEqual(value.leanFullScale, .deg45)
        XCTAssertEqual(value.performanceTarget, .kph100)
        XCTAssertEqual(value.visiblePages, DeviceSettingsBroadcast.allPages)
        XCTAssertFalse(value.ambientNight)
        XCTAssertFalse(value.showBuildings)
    }

    /// The device refuses a page mask without navigation, so the phone never
    /// constructs one: a refused state is a settings screen that does nothing.
    func testNavigationCannotBeHidden() {
        let value = settings(pages: 0x20)

        XCTAssertEqual(value.visiblePages & 0x01, 0x01)
    }

    func testRangesAreClampedLocally() {
        XCTAssertEqual(settings(brightness: 140).brightnessPercent, 100)
        XCTAssertEqual(
            DeviceSettingsBroadcast(revision: 1, leanZeroOffsetCdeg: 4_000).leanZeroOffsetCdeg,
            3_000
        )
        XCTAssertEqual(
            DeviceSettingsBroadcast(revision: 1, leanZeroOffsetCdeg: -4_000).leanZeroOffsetCdeg,
            -3_000
        )
    }

    func testRevisionSkipsZeroAndNeverGoesBackwards() {
        XCTAssertEqual(
            DeviceSettingsBroadcast.nextRevision(after: 0, now: Date(timeIntervalSince1970: 1)),
            1
        )
        // Wall-clock seconds are the floor, so a watermark below them is
        // simply overtaken.
        XCTAssertEqual(
            DeviceSettingsBroadcast.nextRevision(after: 41, now: epoch),
            1_700_000_000
        )
        // A watermark above the clock still only moves forward by one.
        XCTAssertEqual(
            DeviceSettingsBroadcast.nextRevision(after: 1_700_000_000, now: epoch),
            1_700_000_001
        )
        // Wrapping must not land on the reserved zero.
        XCTAssertEqual(
            DeviceSettingsBroadcast.nextRevision(after: UInt32.max, now: epoch),
            1_700_000_000
        )
    }

    /// A reinstall loses the phone's watermark but not the device's, so the
    /// first revision of a fresh install has to clear anything the last one
    /// could have sent.
    func testFreshInstallStartsAboveAnyPreviousSession() {
        let fresh = DeviceSettingsBroadcast.nextRevision(after: 0, now: epoch)

        XCTAssertGreaterThan(fresh, 200)
    }

    func testCapabilityGateUsesBitTen() {
        XCTAssertTrue(settings().supports(capabilities: 1 << 10))
        XCTAssertFalse(settings().supports(capabilities: 1 << 9))
        XCTAssertFalse(settings().supports(capabilities: 0))
    }

    /// One theme, two surfaces: the phone resolves `auto` from the same flag
    /// the device does, so the two can never disagree about day and night.
    func testPhoneLookFollowsTheRoundScreenTheme() {
        for night in [false, true] {
            XCTAssertEqual(DeviceSettingsTheme.official.phoneLook(ambientNight: night), .original)
            XCTAssertEqual(DeviceSettingsTheme.day.phoneLook(ambientNight: night), .gtaDay)
            XCTAssertEqual(DeviceSettingsTheme.night.phoneLook(ambientNight: night), .gtaNight)
        }
        XCTAssertEqual(DeviceSettingsTheme.auto.phoneLook(ambientNight: false), .gtaDay)
        XCTAssertEqual(DeviceSettingsTheme.auto.phoneLook(ambientNight: true), .gtaNight)
    }
}
