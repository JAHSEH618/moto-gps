import Foundation
import MotoNavigationCore

/// The display settings this phone sends to the round instrument, persisted
/// locally. The device is the authority on what is applied — it echoes the state
/// it accepted — so this store only has to remember the rider's choice and a
/// revision that never moves backwards.
@MainActor
final class DeviceSettingsStore {
    private(set) var theme: DeviceSettingsTheme
    private(set) var showBuildings: Bool
    /// Whether it is dark where the rider is. An observation rather than a
    /// preference: `auto` on the device can only resolve against it.
    private(set) var ambientNight: Bool
    private(set) var revision: UInt32

    private let defaults: UserDefaults
    private enum Key {
        static let theme = "moto.device.theme.v1"
        static let showBuildings = "moto.device.buildings.v1"
        static let revision = "moto.device.settings-revision.v1"
    }

    init(defaults: UserDefaults = .standard) {
        self.defaults = defaults
        if let stored = defaults.object(forKey: Key.theme) as? NSNumber,
           let parsed = DeviceSettingsTheme(rawValue: UInt8(clamping: stored.intValue))
        {
            theme = parsed
        } else {
            theme = .auto
        }
        // On unless the rider turned it off: the GTA radar's city-block look
        // comes from the building layer.
        showBuildings = defaults.object(forKey: Key.showBuildings) as? Bool ?? true
        ambientNight = false
        revision = UInt32(truncatingIfNeeded: defaults.integer(forKey: Key.revision))
        if revision == 0 {
            // Zero means "never seen" on the wire and is not encodable, so a
            // fresh install gets a real one before the first send rather than
            // a message the device silently ignores.
            revision = DeviceSettingsBroadcast.nextRevision(after: 0, now: Date())
            defaults.set(Int(revision), forKey: Key.revision)
        }
    }

    var broadcast: DeviceSettingsBroadcast {
        DeviceSettingsBroadcast(
            revision: revision,
            theme: theme,
            units: .kph,
            ambientNight: ambientNight,
            showBuildings: showBuildings
        )
    }

    /// Applies a change and bumps the revision only when something actually
    /// moved. The device drops a revision it has already applied, so a no-op
    /// bump would be wasted radio rather than a bug — but it also would keep
    /// the "did the glass change" echo meaningless.
    @discardableResult
    func apply(
        theme newTheme: DeviceSettingsTheme? = nil,
        showBuildings newBuildings: Bool? = nil,
        ambientNight newAmbientNight: Bool? = nil
    ) -> Bool {
        var changed = false
        if let newTheme, newTheme != theme {
            theme = newTheme
            defaults.set(Int(newTheme.rawValue), forKey: Key.theme)
            changed = true
        }
        if let newBuildings, newBuildings != showBuildings {
            showBuildings = newBuildings
            defaults.set(newBuildings, forKey: Key.showBuildings)
            changed = true
        }
        if let newAmbientNight, newAmbientNight != ambientNight {
            ambientNight = newAmbientNight
            changed = true
        }
        guard changed else { return false }

        revision = DeviceSettingsBroadcast.nextRevision(after: revision, now: Date())
        defaults.set(Int(revision), forKey: Key.revision)
        return true
    }
}
