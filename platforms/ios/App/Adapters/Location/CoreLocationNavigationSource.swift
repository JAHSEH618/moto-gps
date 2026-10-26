import CoreLocation
import MotoNavigationCore

@MainActor
final class CoreLocationNavigationSource: NSObject, NavigationLocationSource {
    private static let maximumFixAgeS: TimeInterval = 15
    private let manager = CLLocationManager()
    private var onFix: (@MainActor (NavigationFix) -> Void)?
    private var onFailure: (@MainActor (String) -> Void)?
    private var wantsUpdates = false
    /// The magnetometer, for when there is no GNSS course: a course exists
    /// only while moving, so a parked handlebar used to keep the last one
    /// (the phone said due east, the round screen 60 degrees).
    private static let maximumCompassAgeS: TimeInterval = 3
    private var compassReading: (headingDeg: Double, at: Date)?

    /// The phone's compass heading in degrees from true north (magnetic
    /// north when the true heading is unavailable), if a calibrated reading
    /// is less than three seconds old. In a handlebar mount the phone points
    /// where the bike points.
    var compassHeadingDeg: Double? {
        guard let reading = compassReading,
              Date().timeIntervalSince(reading.at) <= Self.maximumCompassAgeS
        else { return nil }
        return reading.headingDeg
    }

    override init() {
        super.init()
        manager.delegate = self
        manager.activityType = .automotiveNavigation
        manager.desiredAccuracy = kCLLocationAccuracyBestForNavigation
        // Keep fixes flowing even while the bike is parked: with a 2 m
        // distance filter Core Location stops reporting when the rider
        // stands still, leaving the last non-zero speed frozen on the
        // handlebar gauge. kCLDistanceFilterNone keeps ~1 Hz updates so
        // the speed decays to zero within a couple of seconds.
        manager.distanceFilter = kCLDistanceFilterNone
        manager.pausesLocationUpdatesAutomatically = false
        manager.allowsBackgroundLocationUpdates = true
        // The app runs in portrait, and the phone rides the bars in portrait.
        manager.headingOrientation = .portrait
        manager.headingFilter = 2
        #if os(iOS)
        manager.showsBackgroundLocationIndicator = true
        #endif
    }

    func start(
        onFix: @escaping @MainActor (NavigationFix) -> Void,
        onFailure: @escaping @MainActor (String) -> Void
    ) throws {
        self.onFix = onFix
        self.onFailure = onFailure
        wantsUpdates = true

        switch manager.authorizationStatus {
        case .notDetermined:
            manager.requestAlwaysAuthorization()
        case .authorizedAlways:
            startUpdates()
        case .authorizedWhenInUse:
            manager.requestAlwaysAuthorization()
            startUpdates()
        case .restricted, .denied:
            throw NavigationSourceError.permissionDenied
        @unknown default:
            throw NavigationSourceError.unavailable("当前系统无法确认定位权限")
        }
    }

    func stop() {
        wantsUpdates = false
        manager.stopUpdatingLocation()
        manager.stopUpdatingHeading()
        compassReading = nil
        onFix = nil
        onFailure = nil
    }

    /// A true heading needs location updates on the same manager, so the
    /// compass always starts with them.
    private func startUpdates() {
        manager.startUpdatingLocation()
        if CLLocationManager.headingAvailable() {
            manager.startUpdatingHeading()
        }
    }
}

extension CoreLocationNavigationSource: @preconcurrency CLLocationManagerDelegate {
    func locationManagerDidChangeAuthorization(_ manager: CLLocationManager) {
        guard wantsUpdates else { return }
        switch manager.authorizationStatus {
        case .authorizedAlways, .authorizedWhenInUse:
            startUpdates()
        case .restricted, .denied:
            onFailure?("定位权限已关闭，请在系统设置中允许“始终”定位")
        case .notDetermined:
            break
        @unknown default:
            onFailure?("当前系统无法确认定位权限")
        }
    }

    func locationManager(_ manager: CLLocationManager, didUpdateLocations locations: [CLLocation]) {
        let now = Date()
        guard let location = locations
            .filter({
                $0.horizontalAccuracy >= 0 &&
                    $0.coordinate.latitude.isFinite &&
                    $0.coordinate.longitude.isFinite &&
                    abs($0.timestamp.timeIntervalSince(now)) <= Self.maximumFixAgeS
            })
            .max(by: { $0.timestamp < $1.timestamp })
        else { return }

        onFix?(
            NavigationFix(
                coordinate: WGS84Point(
                    longitudeDeg: location.coordinate.longitude,
                    latitudeDeg: location.coordinate.latitude
                ),
                altitudeM: location.verticalAccuracy >= 0 ? location.altitude : nil,
                horizontalAccuracyM: location.horizontalAccuracy,
                speedMps: location.speed >= 0 ? location.speed : nil,
                courseDeg: location.course >= 0 ? location.course : nil,
                timestamp: location.timestamp
            )
        )
    }

    func locationManager(_ manager: CLLocationManager, didUpdateHeading newHeading: CLHeading) {
        // A negative accuracy means the compass is uncalibrated or disturbed;
        // a wrong north is worse than the last course.
        guard newHeading.headingAccuracy >= 0 else { return }
        let heading = newHeading.trueHeading >= 0
            ? newHeading.trueHeading
            : newHeading.magneticHeading
        guard heading.isFinite, heading >= 0 else { return }
        compassReading = (heading, newHeading.timestamp)
    }

    func locationManager(_ manager: CLLocationManager, didFailWithError error: Error) {
        let locationError = error as? CLError
        if locationError?.code == .locationUnknown { return }
        onFailure?(error.localizedDescription)
    }
}
