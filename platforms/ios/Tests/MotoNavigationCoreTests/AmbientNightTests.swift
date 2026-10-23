import Foundation
import XCTest
@testable import MotoNavigationCore

final class AmbientNightTests: XCTestCase {
    private let beijing = WGS84Point(longitudeDeg: 116.4, latitudeDeg: 39.9)

    private func local(_ hour: Int, _ minute: Int = 0, month: Int, day: Int) -> Date {
        var components = DateComponents()
        components.year = 2026
        components.month = month
        components.day = day
        components.hour = hour
        components.minute = minute
        components.timeZone = TimeZone(identifier: "Asia/Shanghai")!
        return Calendar(identifier: .gregorian).date(from: components)!
    }

    func testNoonInJuneIsDaylight() {
        let noon = local(12, month: 6, day: 21)

        // Beijing sits at 39.9°N, so the June sun is well over 70° at noon.
        XCTAssertGreaterThan(
            AmbientNight.solarElevationDegrees(at: beijing, date: noon),
            60
        )
        XCTAssertFalse(AmbientNight.isNight(at: beijing, date: noon))
    }

    func testMidnightInJuneIsNight() {
        let midnight = local(0, month: 6, day: 21)

        XCTAssertLessThan(
            AmbientNight.solarElevationDegrees(at: beijing, date: midnight),
            -10
        )
        XCTAssertTrue(AmbientNight.isNight(at: beijing, date: midnight))
    }

    func testTheSwitchHappensAtCivilTwilightRatherThanAtSunset() {
        let afternoon = local(18, 30, month: 6, day: 21)
        let evening = local(20, 30, month: 6, day: 21)

        XCTAssertFalse(AmbientNight.isNight(at: beijing, date: afternoon))
        XCTAssertTrue(AmbientNight.isNight(at: beijing, date: evening))
    }

    /// Winter sunset in Beijing is around 16:55 local, almost three hours
    /// earlier than in June; a fixed hour would be wrong for half the year.
    func testWinterAfternoonIsAlreadyNight() {
        XCTAssertTrue(
            AmbientNight.isNight(at: beijing, date: local(18, month: 12, day: 21))
        )
        XCTAssertFalse(
            AmbientNight.isNight(at: beijing, date: local(11, month: 12, day: 21))
        )
    }

    /// Southern hemisphere: June is winter and December is summer.
    func testSouthernHemisphereSeasonsAreInverted() {
        let sydney = WGS84Point(longitudeDeg: 151.2, latitudeDeg: -33.9)

        XCTAssertFalse(AmbientNight.isNight(at: sydney, date: local(12, month: 12, day: 21)))
        XCTAssertTrue(AmbientNight.isNight(at: sydney, date: local(0, month: 6, day: 21)))
    }

    /// The same instant built in two zones is the same sun. This is the
    /// regression guard for reading the phone's zone offset back into the
    /// formula, which would move sunset by hours.
    func testTheAnswerDoesNotDependOnHowTheInstantWasBuilt() {
        var shanghai = DateComponents()
        shanghai.year = 2026
        shanghai.month = 6
        shanghai.day = 21
        shanghai.hour = 22
        shanghai.timeZone = TimeZone(identifier: "Asia/Shanghai")!
        let viaShanghai = Calendar(identifier: .gregorian).date(from: shanghai)!

        var utc = DateComponents()
        utc.year = 2026
        utc.month = 6
        utc.day = 21
        utc.hour = 14
        utc.timeZone = TimeZone(secondsFromGMT: 0)!
        let viaUTC = Calendar(identifier: .gregorian).date(from: utc)!

        XCTAssertEqual(viaShanghai, viaUTC)
        XCTAssertEqual(
            AmbientNight.solarElevationDegrees(at: beijing, date: viaShanghai),
            AmbientNight.solarElevationDegrees(at: beijing, date: viaUTC),
            accuracy: 1e-9
        )
    }
}
