import XCTest

/// A fresh install opens on the home cover, not on a system alert. Location is
/// asked for by the first action that needs a position, Bluetooth by the first
/// that needs the round screen, and Don't Allow always leaves a way to
/// Settings. The simulator has no Bluetooth and never shows that alert, so
/// Bluetooth is checked only where an iPhone raises it.
final class PermissionTimingUITests: XCTestCase {
    private let springboard = XCUIApplication(bundleIdentifier: "com.apple.springboard")

    override func setUp() {
        continueAfterFailure = false
    }

    func testFreshInstallAsksAtTheSearchAndTheDeviceSheet() throws {
        let app = freshInstall()
        app.launch()
        let search = app.textFields["destination-search-field"]
        XCTAssertTrue(search.waitForExistence(timeout: 5))
        XCTAssertFalse(springboard.alerts.firstMatch.waitForExistence(timeout: 3),
                       "Launch raised a system alert before the rider did anything")
        keepScreenshot(named: "Fresh install — launch")

        search.tap()
        let location = systemAlert(about: ["location", "位置"])
        keepScreenshot(named: "Fresh install — location asked at the search")
        refuse(location)

        search.typeText("奥体")
        XCTAssertTrue(app.buttons["前往设置开启定位"].waitForExistence(timeout: 5))
        keepScreenshot(named: "Location refused — search results")
        app.buttons["清空搜索"].tap()

        app.buttons["device-details-button"].tap()
        refuseBluetoothIfAsked(at: "the device sheet")

        let sheet = app.scrollViews["device-details-sheet"]
        XCTAssertTrue(sheet.waitForExistence(timeout: 3))
        XCTAssertTrue(app.staticTexts["未获授权"].waitForExistence(timeout: 3))
        let settings = app.buttons["前往设置开启定位"]
        for _ in 0 ..< 6 where !settings.isHittable { sheet.swipeUp() }
        keepScreenshot(named: "Location refused — device sheet")
        settings.tap()
        XCTAssertTrue(XCUIApplication(bundleIdentifier: "com.apple.Preferences")
            .wait(for: .runningForeground, timeout: 5))
    }

    func testFreshInstallAsksAtTheSafehouseRoute() throws {
        let app = freshInstall()
        // A saved home and a gateway address. Refusing location fails the
        // preview before anything is sent, so the gateway is never reached.
        let home = #"{"id":"","name":"测试之家","address":"历下区","city":"济南市","district":"历下区","display_area":"济南市 历下区","location":{"coordinate_system":"WGS84","longitude_deg":117.12,"latitude_deg":36.65},"distance_m":null}"#
        app.launchArguments = [
            "-moto.home.place.v1", "<\(Data(home.utf8).map { String(format: "%02x", $0) }.joined())>",
            "-MotoGPS.GatewayBaseURL.v1", "https://gateway.test/api/",
        ]
        app.launch()
        let safehouse = app.buttons["home-shortcut-button"]
        XCTAssertTrue(safehouse.waitForExistence(timeout: 5))
        XCTAssertFalse(springboard.alerts.firstMatch.waitForExistence(timeout: 3),
                       "Launch raised a system alert before the rider did anything")

        safehouse.tap()
        let location = systemAlert(about: ["location", "位置"])
        keepScreenshot(named: "Fresh install — location asked at the safehouse route")
        refuse(location)

        XCTAssertTrue(app.staticTexts["需要当前位置才能规划路线，请在系统设置中允许定位"]
            .waitForExistence(timeout: 5))
        XCTAssertTrue(app.buttons["前往设置"].exists)
        keepScreenshot(named: "Location refused — route preview")
    }

    func testFreshInstallAsksWhenTheDemoStarts() throws {
        let app = freshInstall()
        app.launch()
        let demo = app.buttons["demo-navigation-button"]
        XCTAssertTrue(demo.waitForExistence(timeout: 5))
        XCTAssertFalse(springboard.alerts.firstMatch.waitForExistence(timeout: 3),
                       "Launch raised a system alert before the rider did anything")
        for _ in 0 ..< 4 where !demo.isHittable { app.swipeUp() }

        demo.tap()
        var asked: [String] = []
        while springboard.alerts.firstMatch.waitForExistence(timeout: 5) {
            let alert = springboard.alerts.firstMatch
            asked.append(alert.label)
            keepScreenshot(named: "Demo — \(alert.label)")
            refuse(alert)
        }
        XCTAssertTrue(asked.contains { $0.localizedCaseInsensitiveContains("location") || $0.contains("位置") },
                      "The demo did not ask for location: \(asked)")
        XCTAssertTrue(app.buttons["结束导航"].waitForExistence(timeout: 15))
    }

    private func freshInstall() -> XCUIApplication {
        let app = XCUIApplication()
        app.resetAuthorizationStatus(for: .location)
        app.resetAuthorizationStatus(for: .bluetooth)
        return app
    }

    private func systemAlert(about words: [String],
                             file: StaticString = #filePath, line: UInt = #line) -> XCUIElement {
        let alert = springboard.alerts.firstMatch
        XCTAssertTrue(alert.waitForExistence(timeout: 5), "No system alert", file: file, line: line)
        XCTAssertTrue(words.contains { alert.label.localizedCaseInsensitiveContains($0) },
                      "Unexpected alert: \(alert.label)", file: file, line: line)
        return alert
    }

    private func refuseBluetoothIfAsked(at place: String) {
        let alert = springboard.alerts.firstMatch
        guard alert.waitForExistence(timeout: 3) else { return }
        XCTAssertTrue(["bluetooth", "蓝牙"].contains { alert.label.localizedCaseInsensitiveContains($0) },
                      "Unexpected alert: \(alert.label)")
        keepScreenshot(named: "Fresh install — Bluetooth asked at \(place)")
        refuse(alert)
    }

    private func refuse(_ alert: XCUIElement) {
        // "Don’t Allow" in English (typographic apostrophe), 不允许 in Chinese.
        let refuse = alert.buttons.matching(
            NSPredicate(format: "label BEGINSWITH[c] 'don' OR label == '不允许'")
        ).firstMatch
        XCTAssertTrue(refuse.waitForExistence(timeout: 3), "No refuse button in \(alert.label)")
        refuse.tap()
        let gone = NSPredicate(format: "exists == false")
        wait(for: [expectation(for: gone, evaluatedWith: refuse)], timeout: 5)
    }

    private func keepScreenshot(named name: String) {
        // The whole screen: a system alert is not part of the app's own tree.
        let attachment = XCTAttachment(screenshot: XCUIScreen.main.screenshot())
        attachment.name = name
        attachment.lifetime = .keepAlways
        add(attachment)
    }
}
