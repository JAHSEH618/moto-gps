import Foundation

/// Whether it is dark at a place and a moment, computed from the sun's position
/// rather than from the phone's appearance setting.
///
/// The device has no clock and no light sensor, and `SettingsTheme.auto` can
/// only resolve against the fact the phone supplies. System dark mode is a poor
/// proxy: a phone in dark mode at noon would put the instrument into its night
/// palette while the rider is in daylight.
public enum AmbientNight {
    /// Civil twilight. The screen switches when reading the road starts to need
    /// the night palette, which is earlier than sunset and later than nautical
    /// dusk — the sun is 6° below the horizon.
    public static let civilTwilightDegrees = -6.0

    /// The time zone is deliberately not a parameter: true solar time is
    /// computed from UTC and east-positive longitude, so the answer does not
    /// depend on the phone's clock settings.
    public static func isNight(at point: WGS84Point, date: Date = Date()) -> Bool {
        solarElevationDegrees(at: point, date: date) < civilTwilightDegrees
    }

    /// NOAA's approximation: good to a few tenths of a degree, which is an
    /// order of magnitude finer than the only decision it feeds.
    public static func solarElevationDegrees(at point: WGS84Point, date: Date) -> Double {
        let seconds = date.timeIntervalSince1970
        var calendar = Calendar(identifier: .gregorian)
        calendar.timeZone = TimeZone(secondsFromGMT: 0) ?? .gmt
        let dayOfYear = Double(calendar.ordinality(of: .day, in: .year, for: date) ?? 1)
        // Fractional year in radians, taken at the instant rather than at
        // midnight so the declination is not a day stale.
        let hoursUTC = (seconds.truncatingRemainder(dividingBy: 86_400) + 86_400)
            .truncatingRemainder(dividingBy: 86_400) / 3_600
        let gamma = 2 * Double.pi / 365 * (dayOfYear - 1 + (hoursUTC - 12) / 24)

        let declination = 0.006_918
            - 0.399_912 * cos(gamma) + 0.070_257 * sin(gamma)
            - 0.006_758 * cos(2 * gamma) + 0.000_907 * sin(2 * gamma)
            - 0.002_697 * cos(3 * gamma) + 0.001_480 * sin(3 * gamma)

        let equationOfTimeMinutes = 229.18 * (0.000_075
            + 0.001_868 * cos(gamma) - 0.032_077 * sin(gamma)
            - 0.014_615 * cos(2 * gamma) - 0.040_849 * sin(2 * gamma))

        // True solar time in minutes, from UTC and east-positive longitude;
        // the zone offset cancels out of this form, so a wrong device time zone
        // cannot move the sunrise.
        let solarMinutes = hoursUTC * 60 + equationOfTimeMinutes
            + 4 * point.longitudeDeg
        let hourAngleRadians = (solarMinutes / 4 - 180) * Double.pi / 180

        let latitude = point.latitudeDeg * Double.pi / 180
        let sine = sin(latitude) * sin(declination)
            + cos(latitude) * cos(declination) * cos(hourAngleRadians)
        return asin(max(-1, min(1, sine))) * 180 / Double.pi
    }
}
