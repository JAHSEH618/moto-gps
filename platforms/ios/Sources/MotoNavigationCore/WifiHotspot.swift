import Foundation

/// The iPhone Personal Hotspot the round screen joins on demand
/// (BLE WifiConfig 0x1A / WifiStatus 0x1B, capability bit 14). iOS does not
/// let an app read its own hotspot name or password, so the rider types them
/// once and the phone hands them to the device over the bonded BLE link.
public enum WifiHotspot {
    /// Protocol bit 14, advertised by both ends.
    public static let capability: UInt32 = 1 << 14

    public static func supports(capabilities: UInt32) -> Bool {
        capabilities & capability != 0
    }

    public enum CredentialError: Error, Equatable, Sendable {
        case ssidEmpty
        case ssidTooLong
        case passwordTooShort
        case passwordTooLong
        case passwordCharacters

        public var message: String {
            switch self {
            case .ssidEmpty: "请填写热点名称。"
            case .ssidTooLong: "热点名称最长 32 字节（约 10 个汉字）。"
            case .passwordTooShort: "热点密码至少 8 位。"
            case .passwordTooLong: "热点密码最多 63 位。"
            case .passwordCharacters: "热点密码只能包含英文字母、数字和半角符号。"
            }
        }
    }

    /// The same rules the shared encoder enforces (802.11 SSID length, WPA2
    /// passphrase), checked here only so the form can say what is wrong
    /// before anything is sent. The shared encoder remains the authority.
    public static func validate(ssid: String, password: String) -> CredentialError? {
        let ssidBytes = ssid.utf8.count
        if ssidBytes == 0 { return .ssidEmpty }
        if ssidBytes > 32 || ssid.utf8.contains(0) { return .ssidTooLong }
        let bytes = Array(password.utf8)
        if bytes.count < 8 { return .passwordTooShort }
        if bytes.count == 64 {
            let hex = bytes.allSatisfy { byte in
                (0x30...0x39).contains(byte) || (0x41...0x46).contains(byte) ||
                    (0x61...0x66).contains(byte)
            }
            return hex ? nil : .passwordTooLong
        }
        if bytes.count > 63 { return .passwordTooLong }
        if !bytes.allSatisfy({ (0x20...0x7E).contains($0) }) { return .passwordCharacters }
        return nil
    }
}

/// What the device last reported about its WiFi link.
public struct WifiHotspotStatus: Equatable, Sendable {
    public enum State: UInt8, Sendable {
        case off = 0
        case connecting = 1
        case connected = 2
        case failed = 3
    }

    public enum Failure: UInt8, Sendable {
        case none = 0
        case noCredentials = 1
        case networkNotFound = 2
        case authFailed = 3
        case timeout = 4
        case noMemory = 5
        case disconnected = 6
    }

    public var state: State
    public var failure: Failure
    /// The device has a hotspot stored. Nothing about it comes back: the
    /// password never leaves the device.
    public var deviceHasNetwork: Bool
    /// Dotted IPv4, empty unless connected.
    public var ipv4Address: String
    public var rssiDbm: Int

    public init(
        state: State,
        failure: Failure = .none,
        deviceHasNetwork: Bool,
        ipv4Address: String = "",
        rssiDbm: Int = 0
    ) {
        self.state = state
        self.failure = failure
        self.deviceHasNetwork = deviceHasNetwork
        self.ipv4Address = ipv4Address
        self.rssiDbm = rssiDbm
    }

    /// Nil for a raw value this app does not know (a newer firmware).
    public init?(
        rawState: UInt8,
        rawFailure: UInt8,
        deviceHasNetwork: Bool,
        ipv4Address: String,
        rssiDbm: Int8
    ) {
        guard let state = State(rawValue: rawState),
              let failure = Failure(rawValue: rawFailure)
        else { return nil }
        self.init(
            state: state,
            failure: failure,
            deviceHasNetwork: deviceHasNetwork,
            ipv4Address: ipv4Address,
            rssiDbm: Int(rssiDbm)
        )
    }

    public var title: String {
        switch state {
        case .off: deviceHasNetwork ? "已关闭" : "未设置"
        case .connecting: "正在连接"
        case .connected: "已连接"
        case .failed: "连接失败"
        }
    }

    /// One line on what to do next, for the states that need it.
    public var detail: String? {
        switch (state, failure) {
        case (.off, _):
            return deviceHasNetwork
                ? "圆屏只在需要传输数据时才打开 WiFi，用完自动关闭。"
                : nil
        case (.connecting, _):
            return "请保持个人热点开启。"
        case (.connected, _):
            return nil
        case (.failed, .noCredentials):
            return "圆屏上还没有热点，请先保存。"
        case (.failed, .networkNotFound):
            return "没有找到热点。请打开个人热点，并开启“最大兼容性”（2.4 GHz）。"
        case (.failed, .authFailed):
            return "密码不正确。请核对后重新保存。"
        case (.failed, .timeout):
            return "连接超时。请靠近手机后重试。"
        case (.failed, .noMemory):
            return "圆屏内存不足，WiFi 未能启动。"
        case (.failed, .disconnected), (.failed, .none):
            return "与热点的连接已断开。"
        }
    }
}
