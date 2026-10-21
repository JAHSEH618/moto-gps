import Foundation

/// WGS84 <-> GCJ-02 conversion for mainland China.
///
/// The app works in WGS84 (CoreLocation, the gateway contract) while the
/// device draws in GCJ-02. The conversion has to happen exactly once per value
/// — converting twice shifts a home blip by hundreds of metres — so it lives
/// here, next to the coordinate types, and is covered by `swift test`.
public enum ChinaCoordinateTransform {
    private static let earthAxisM = 6_378_245.0
    private static let eccentricitySquared = 0.006693421622965943

    /// GCJ-02 to WGS84, solved by bisection because the forward transform is
    /// not analytically invertible. 32 iterations converge far below one
    /// millimetre.
    public static func gcj02ToWGS84(_ point: GCJ02Point) -> WGS84Point {
        var minimumLongitude = point.longitudeDeg - 0.02
        var maximumLongitude = point.longitudeDeg + 0.02
        var minimumLatitude = point.latitudeDeg - 0.02
        var maximumLatitude = point.latitudeDeg + 0.02
        var candidate = WGS84Point(
            longitudeDeg: point.longitudeDeg,
            latitudeDeg: point.latitudeDeg
        )

        for _ in 0 ..< 32 {
            candidate = WGS84Point(
                longitudeDeg: (minimumLongitude + maximumLongitude) * 0.5,
                latitudeDeg: (minimumLatitude + maximumLatitude) * 0.5
            )
            let projected = wgs84ToGCJ02(candidate)
            if projected.longitudeDeg < point.longitudeDeg {
                minimumLongitude = candidate.longitudeDeg
            } else {
                maximumLongitude = candidate.longitudeDeg
            }
            if projected.latitudeDeg < point.latitudeDeg {
                minimumLatitude = candidate.latitudeDeg
            } else {
                maximumLatitude = candidate.latitudeDeg
            }
        }
        return candidate
    }

    /// WGS84 to GCJ-02. Outside mainland China the offset is zero, so a point
    /// in Tokyo or Taipei is returned unchanged rather than being displaced by
    /// a transform that does not apply there.
    public static func wgs84ToGCJ02(_ point: WGS84Point) -> GCJ02Point {
        guard isInMainlandChina(point) else {
            return GCJ02Point(
                longitudeDeg: point.longitudeDeg,
                latitudeDeg: point.latitudeDeg
            )
        }

        let longitudeOffset = point.longitudeDeg - 105
        let latitudeOffset = point.latitudeDeg - 35
        var latitudeDelta = transformLatitude(longitudeOffset, latitudeOffset)
        var longitudeDelta = transformLongitude(longitudeOffset, latitudeOffset)
        let radianLatitude = point.latitudeDeg / 180 * .pi
        var magic = sin(radianLatitude)
        magic = 1 - eccentricitySquared * magic * magic
        let squareRootMagic = sqrt(magic)
        latitudeDelta = latitudeDelta * 180 /
            ((earthAxisM * (1 - eccentricitySquared) /
                (magic * squareRootMagic)) * .pi)
        longitudeDelta = longitudeDelta * 180 /
            ((earthAxisM / squareRootMagic) * cos(radianLatitude) * .pi)
        return GCJ02Point(
            longitudeDeg: point.longitudeDeg + longitudeDelta,
            latitudeDeg: point.latitudeDeg + latitudeDelta
        )
    }

    /// The conventional bounding box of the transform. It is deliberately
    /// generous: a point just outside it must not be shifted by kilometres.
    public static func isInMainlandChina(_ point: WGS84Point) -> Bool {
        point.longitudeDeg >= 72.004 && point.longitudeDeg <= 137.8347 &&
            point.latitudeDeg >= 0.8293 && point.latitudeDeg <= 55.8271
    }

    private static func transformLatitude(
        _ longitude: Double,
        _ latitude: Double
    ) -> Double {
        var result = -100 + 2 * longitude + 3 * latitude +
            0.2 * latitude * latitude + 0.1 * longitude * latitude +
            0.2 * sqrt(abs(longitude))
        result += (20 * sin(6 * longitude * .pi) +
            20 * sin(2 * longitude * .pi)) * 2 / 3
        result += (20 * sin(latitude * .pi) +
            40 * sin(latitude / 3 * .pi)) * 2 / 3
        result += (160 * sin(latitude / 12 * .pi) +
            320 * sin(latitude * .pi / 30)) * 2 / 3
        return result
    }

    private static func transformLongitude(
        _ longitude: Double,
        _ latitude: Double
    ) -> Double {
        var result = 300 + longitude + 2 * latitude +
            0.1 * longitude * longitude + 0.1 * longitude * latitude +
            0.1 * sqrt(abs(longitude))
        result += (20 * sin(6 * longitude * .pi) +
            20 * sin(2 * longitude * .pi)) * 2 / 3
        result += (20 * sin(longitude * .pi) +
            40 * sin(longitude / 3 * .pi)) * 2 / 3
        result += (150 * sin(longitude / 12 * .pi) +
            300 * sin(longitude / 30 * .pi)) * 2 / 3
        return result
    }
}
