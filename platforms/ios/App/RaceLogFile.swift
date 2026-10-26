import Foundation
import MotoNavigationCore

/// The race log on disk: one JSON file in Application Support. It holds only
/// what the round screen measured (lean, G, 0-100 times), never a position.
enum RaceLogFile {
    private static var url: URL? {
        FileManager.default
            .urls(for: .applicationSupportDirectory, in: .userDomainMask)
            .first?
            .appendingPathComponent("RaceLog.json")
    }

    /// An unreadable file starts a fresh log rather than blocking the app; the
    /// next save replaces it.
    static func load() -> RaceLog {
        guard let url, let data = try? Data(contentsOf: url),
              let log = try? JSONDecoder().decode(RaceLog.self, from: data)
        else { return RaceLog() }
        return log
    }

    static func save(_ log: RaceLog) {
        guard let url else { return }
        do {
            try FileManager.default.createDirectory(
                at: url.deletingLastPathComponent(), withIntermediateDirectories: true
            )
            try JSONEncoder().encode(log).write(to: url, options: .atomic)
        } catch {
            #if DEBUG
            print("[RaceLog] save failed: \(error)")
            #endif
        }
    }
}
