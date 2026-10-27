import Foundation
import XCTest
@testable import MOTO_GPS

@MainActor
final class DeviceSettingsStoreTests: XCTestCase {
    private var suiteName = ""
    private var defaults: UserDefaults!

    override func setUp() {
        super.setUp()
        suiteName = "DeviceSettingsStoreTests.\(UUID().uuidString)"
        defaults = UserDefaults(suiteName: suiteName)
    }

    override func tearDown() {
        defaults.removePersistentDomain(forName: suiteName)
        super.tearDown()
    }

    func testFreshInstallShowsBuildings() {
        XCTAssertTrue(DeviceSettingsStore(defaults: defaults).showBuildings)
    }

    func testRiderTurningBuildingsOffSurvivesRelaunch() {
        DeviceSettingsStore(defaults: defaults).apply(showBuildings: false)
        XCTAssertFalse(DeviceSettingsStore(defaults: defaults).showBuildings)
    }
}
