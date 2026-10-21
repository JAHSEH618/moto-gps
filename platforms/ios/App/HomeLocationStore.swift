import Foundation

/// The rider's home address, persisted locally. The phone owns the address; the
/// device only ever owns the *intent* to go home (BLE `HomeShortcut`), so the
/// address never has to survive a round trip.
final class HomeLocationStore: ObservableObject {
    @Published private(set) var place: PlaceSearchResult?

    private let defaults: UserDefaults
    private let storageKey = "moto.home.place.v1"

    init(defaults: UserDefaults = .standard) {
        self.defaults = defaults
        if let data = defaults.data(forKey: storageKey) {
            place = try? JSONDecoder().decode(PlaceSearchResult.self, from: data)
        }
    }

    var isSet: Bool { place != nil }

    func setHome(_ place: PlaceSearchResult) {
        self.place = place
        if let data = try? JSONEncoder().encode(place) {
            defaults.set(data, forKey: storageKey)
        }
    }

    func clear() {
        place = nil
        defaults.removeObject(forKey: storageKey)
    }
}
