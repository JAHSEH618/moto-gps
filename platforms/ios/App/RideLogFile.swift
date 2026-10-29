import Foundation
import MotoNavigationCore

/// The ride list on disk: one JSON file in Application Support, beside the
/// race log. Like it, it holds no position: distances and speeds only.
enum RideLogFile {
    private static var url: URL? {
        FileManager.default
            .urls(for: .applicationSupportDirectory, in: .userDomainMask)
            .first?
            .appendingPathComponent("RideLog.json")
    }

    /// An unreadable file starts a fresh list rather than blocking the app; the
    /// next save replaces it.
    static func load() -> RideLog {
        guard let url, let data = try? Data(contentsOf: url),
              let log = try? JSONDecoder().decode(RideLog.self, from: data)
        else { return RideLog() }
        return log
    }

    static func save(_ log: RideLog) {
        guard let url else { return }
        do {
            try FileManager.default.createDirectory(
                at: url.deletingLastPathComponent(), withIntermediateDirectories: true
            )
            try JSONEncoder().encode(log).write(to: url, options: .atomic)
        } catch {
            #if DEBUG
            print("[RideLog] save failed: \(error)")
            #endif
        }
    }
}
