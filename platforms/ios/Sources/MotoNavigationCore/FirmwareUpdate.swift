import Foundation

/// Firmware update for the round screen over the iPhone hotspot
/// (BLE OtaCommand 0x1C / OtaStatus 0x1D, capability bit 15).
///
/// The image never crosses BLE. The phone either serves the file from the
/// hotspot it already shares with the device (the rider picked a `.bin` in
/// Files), or hands over a release URL; either way the device fetches it,
/// checks the SHA-256 the phone stated, and only then switches the bootloader
/// to it.
public enum FirmwareUpdate {
    /// Protocol bit 15, advertised by both ends.
    public static let capability: UInt32 = 1 << 15
    /// One OTA slot is 7 MiB; below a megabyte is not an image.
    public static let minimumImageBytes: UInt32 = 1 << 20
    public static let maximumImageBytes: UInt32 = 7 << 20
    /// The port the phone serves the file on, over the hotspot link.
    public static let servingPort: UInt16 = 8123
    /// Path the device fetches when the phone is the server.
    public static let servingPath = "/moto-gps.bin"

    public static func supports(capabilities: UInt32) -> Bool {
        capabilities & capability != 0
    }

    public enum RequestError: Error, Equatable, Sendable {
        case urlTooShort
        case urlTooLong
        case urlNotHTTP
        case imageTooSmall
        case imageTooLarge
        case digestNotHex

        public var message: String {
            switch self {
            case .urlTooShort, .urlTooLong, .urlNotHTTP:
                "下载地址必须是 http:// 或 https:// 开头的绝对地址。"
            case .imageTooSmall:
                "这个文件不像固件镜像（小于 1 MB）。"
            case .imageTooLarge:
                "固件超过圆屏的 7 MB OTA 分区。"
            case .digestNotHex:
                "固件摘要必须是 64 位十六进制 SHA-256。"
            }
        }
    }

    /// The same bounds the shared encoder enforces, checked here only so the
    /// screen can say what is wrong before anything is sent. The shared
    /// encoder stays the authority.
    public static func validate(url: String, sizeBytes: UInt32,
                                sha256Hex: String) -> RequestError? {
        let bytes = Array(url.utf8)
        if bytes.count < 10 { return .urlTooShort }
        if bytes.count > 200 { return .urlTooLong }
        if !bytes.allSatisfy({ (0x21...0x7E).contains($0) }) || bytes.contains(0) {
            return .urlNotHTTP
        }
        guard let schemeEnd = url.range(of: "://") else { return .urlNotHTTP }
        let scheme = url[url.startIndex..<schemeEnd.lowerBound]
        if scheme != "http" && scheme != "https" { return .urlNotHTTP }
        if sizeBytes != 0 {
            if sizeBytes < minimumImageBytes { return .imageTooSmall }
            if sizeBytes > maximumImageBytes { return .imageTooLarge }
        }
        if sha256Hex.count != 64 ||
            !sha256Hex.allSatisfy({ $0.isHexDigit }) {
            return .digestNotHex
        }
        return nil
    }

    /// The URL the device should fetch when the phone serves the image over
    /// the hotspot. The hotspot hands out `172.20.10.1` to the phone; the
    /// caller passes whatever address it found on the bridge interface.
    public static func servingURL(host: String) -> String {
        "http://\(host):\(servingPort)\(servingPath)"
    }
}

/// The release manifest published next to the image on doupaw.site. The
/// phone reads it so the rider never has to copy a digest by hand.
public struct FirmwareRelease: Equatable, Sendable {
    public static let defaultManifestURL = URL(
        string: "https://www.doupaw.site/firmware/latest.json"
    )!

    public let version: String
    public let url: String
    public let sha256Hex: String
    public let sizeBytes: UInt32
    /// The commit the image was built from (the manifest's git_commit); nil
    /// for a manifest that does not say.
    public var gitCommit: String? = nil

    public enum DecodeError: Error, Equatable, LocalizedError, Sendable {
        case unsupportedSchema
        case invalid(String)

        public var errorDescription: String? {
            switch self {
            case .unsupportedSchema:
                "固件清单格式不认识，请更新 App 后再试。"
            case let .invalid(message):
                message
            }
        }
    }

    private struct Manifest: Decodable {
        let schema: String
        let version: String
        let file: String
        let url: String
        let sha256: String
        let size_bytes: UInt32
        let git_commit: String?
    }

    public static func decode(_ data: Data) throws -> FirmwareRelease {
        let manifest: Manifest
        do {
            manifest = try JSONDecoder().decode(Manifest.self, from: data)
        } catch {
            throw DecodeError.invalid("固件清单不完整：\(error.localizedDescription)")
        }
        guard manifest.schema == "moto-gps-firmware/1" else {
            throw DecodeError.unsupportedSchema
        }
        guard !manifest.file.isEmpty,
              manifest.url.hasSuffix("/\(manifest.file)") else {
            throw DecodeError.invalid("固件清单里的文件名和地址不一致。")
        }
        if let error = FirmwareUpdate.validate(url: manifest.url,
                                               sizeBytes: manifest.size_bytes,
                                               sha256Hex: manifest.sha256) {
            throw DecodeError.invalid(error.message)
        }
        let commit = manifest.git_commit?.trimmingCharacters(in: .whitespaces)
        return FirmwareRelease(version: manifest.version,
                               url: manifest.url,
                               sha256Hex: manifest.sha256.lowercased(),
                               sizeBytes: manifest.size_bytes,
                               gitCommit: commit?.isEmpty == false ? commit : nil)
    }

    /// Whether this release is what the round screen already runs. A device
    /// that reports its build (FirmwareInfo 0x1E) answers exactly: its build
    /// against the manifest's commit, so a USB-flashed or "-dirty" build is
    /// not mistaken for the release. Without that report (older firmware, or
    /// a manifest without git_commit) the phone can only compare with the
    /// version it last flashed itself.
    public func isInstalled(on device: DeviceFirmware?,
                            lastFlashedVersion: String) -> Bool {
        if let device, let gitCommit {
            return device.build == gitCommit
        }
        return version == lastFlashedVersion
    }
}

/// What the round screen says it runs (FirmwareInfo 0x1E).
public struct DeviceFirmware: Equatable, Sendable {
    /// The firmware's own version (ESP-IDF PROJECT_VER).
    public let version: String
    /// The commit the image was built from, with "-dirty" for a tree that had
    /// uncommitted changes, or "unknown".
    public let build: String

    public init(version: String, build: String) {
        self.version = version
        self.build = build
    }
}

/// What the device last reported about its update (OtaStatus 0x1D).
public struct FirmwareUpdateStatus: Equatable, Sendable {
    public enum State: UInt8, Sendable {
        case idle = 0
        case downloading = 1
        case verifying = 2
        case ready = 3
        case failed = 4
    }

    public enum Failure: UInt8, Sendable {
        case none = 0
        case noWifi = 1
        case noCredentials = 2
        case badURL = 3
        case network = 4
        case hashMismatch = 5
        case imageRejected = 6
        case noMemory = 7
        case busy = 8
        case tooLarge = 9
    }

    public var state: State
    public var failure: Failure
    public var progressPercent: UInt8
    public var bytesReceived: UInt32

    public init(state: State, failure: Failure, progressPercent: UInt8,
                bytesReceived: UInt32) {
        self.state = state
        self.failure = failure
        self.progressPercent = progressPercent
        self.bytesReceived = bytesReceived
    }

    public var title: String {
        switch state {
        case .idle: "未开始"
        case .downloading: "传输中 \(progressPercent)%"
        case .verifying: "校验中"
        case .ready: "已写入，圆屏正在重启"
        case .failed: "更新失败"
        }
    }

    public var message: String? {
        switch failure {
        case .none: nil
        case .noWifi: "圆屏没连上热点。先在 03 WiFi 里保存热点，并打开「允许其他人加入」和「最大兼容性」。"
        case .noCredentials: "圆屏还没有热点信息。先在 03 WiFi 里保存热点。"
        case .badURL: "圆屏不接受这个下载地址。"
        case .network: "下载中断：热点断了，或文件服务没有应答。"
        case .hashMismatch: "SHA-256 不匹配，镜像未启用。文件可能在传输中损坏。"
        case .imageRejected: "镜像校验失败：这不是这块板子的固件。"
        case .noMemory: "圆屏内存不足，更新中止。"
        case .busy: "圆屏正在更新另一次，请稍后。"
        case .tooLarge: "镜像超过 7 MB 的 OTA 分区。"
        }
    }

    /// True while the device is working: the rider should not start a second
    /// update, and the phone should keep the file server up.
    public var isRunning: Bool {
        state == .downloading || state == .verifying
    }
}

/// What the phone concludes on its own when the device never reports how an
/// update ended: it refused the command, the link died, or it went quiet.
/// Without these the sheet sat on 传输中 0% with its button disabled. The
/// device may still be working, so its next OtaStatus overrides any of them.
public enum FirmwareUpdateProblem: Equatable, Sendable {
    /// The device answered the OtaCommand with a non-zero Ack status.
    case refused(ackStatus: UInt8)
    case linkLost
    case stalled

    /// The device repeats its status every 2 s while it joins the hotspot.
    /// Its quietest stretch after that is a TLS open, the header read and
    /// one read timeout, 15 s each; this outlasts all three.
    public static let stallSeconds: Double = 90

    public var message: String {
        switch self {
        case let .refused(ackStatus):
            switch ackStatus {
            case 2: "圆屏拒绝了这次更新：蓝牙还没加密配对，或者圆屏正在进行另一次更新。断开重连后再试。"
            case 3: "更新命令没有送到圆屏，或者圆屏没能开始更新。请再试一次。"
            default: "圆屏拒绝了这次更新（Ack 状态 \(ackStatus)）。"
            }
        case .linkLost:
            "蓝牙在更新途中断开，这次的结果不确定。重新连上圆屏后再检查一次。"
        case .stalled:
            "圆屏 \(Int(Self.stallSeconds)) 秒没有回报进度，这次更新已放弃。"
        }
    }
}
