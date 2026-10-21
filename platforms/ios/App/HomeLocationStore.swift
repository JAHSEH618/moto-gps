import Foundation
import MotoNavigationCore

/// The rider's home address, persisted locally. The phone owns the address; the
/// device only ever owns the *intent* to go home (BLE `HomeShortcut`), so the
/// address never has to survive a round trip.
///
/// The revision is persisted with the address: the device orders `HomeLocation`
/// by it, so restarting the app must not restart the sequence and let a replayed
/// message overwrite a newer home.
@MainActor
final class HomeLocationStore: ObservableObject {
    @Published private(set) var place: PlaceSearchResult?
    @Published private(set) var revision: UInt32 = 0

    /// Called after every mutation so the owner can mirror the address to the
    /// device. Views write this store directly, so one subscription here is the
    /// only path that cannot be forgotten at a call site.
    var onChange: ((PlaceSearchResult?) -> Void)?

    private let defaults: UserDefaults
    private let storageKey = "moto.home.place.v1"
    private let revisionKey = "moto.home.revision.v1"

    init(defaults: UserDefaults = .standard) {
        self.defaults = defaults
        if let data = defaults.data(forKey: storageKey) {
            place = try? JSONDecoder().decode(PlaceSearchResult.self, from: data)
        }
        let stored = (defaults.object(forKey: revisionKey) as? NSNumber)?
            .uint32Value ?? 0
        // An install that saved a home before the revision existed still has
        // to start above zero, or its first message would mean "never seen".
        revision = max(stored, place == nil ? 0 : 1)
        if revision != stored {
            defaults.set(NSNumber(value: revision), forKey: revisionKey)
        }
    }

    var isSet: Bool { place != nil }

    func setHome(_ place: PlaceSearchResult) {
        self.place = place
        if let data = try? JSONEncoder().encode(place) {
            defaults.set(data, forKey: storageKey)
        }
        advanceRevision()
        onChange?(place)
    }

    func clear() {
        place = nil
        defaults.removeObject(forKey: storageKey)
        advanceRevision()
        onChange?(nil)
    }

    private func advanceRevision() {
        revision = HomeLocationBroadcast.nextRevision(after: revision)
        defaults.set(NSNumber(value: revision), forKey: revisionKey)
    }
}
