import Foundation

enum BLEProtocolGoldenCheck {
    enum CheckError: LocalizedError {
        case fixtureMissing
        case vectorMissing
        case homeVectorMissing
        case settingsVectorMissing
        case gnssVectorMissing
        case wifiVectorMissing
        case mismatch(expected: String, actual: String)

        var errorDescription: String? {
            switch self {
            case .fixtureMissing: "BLE v1 黄金帧资源不存在"
            case .vectorMissing: "BLE v1 黄金帧缺少 navigation.payload"
            case .homeVectorMissing: "BLE v1 黄金帧缺少 home.payload"
            case .settingsVectorMissing: "BLE v1 黄金帧缺少 settings.payload"
            case .gnssVectorMissing: "BLE v1 黄金帧缺少 gnss.payload"
            case .wifiVectorMissing: "BLE v1 黄金帧缺少 wifi_config/wifi_status.payload"
            case let .mismatch(expected, actual):
                "BLE v1 编码不一致：expected \(expected), actual \(actual)"
            }
        }
    }

    static func verify(codec: MotoBLEProtocolCodec, bundle: Bundle = .main) throws {
        guard let url = bundle.url(
            forResource: "ble-navigation-v1.golden",
            withExtension: "txt"
        ) else { throw CheckError.fixtureMissing }
        try verify(codec: codec, fixtureURL: url)
    }

    static func verify(codec: MotoBLEProtocolCodec, fixtureURL: URL) throws {
        let fixture = try String(contentsOf: fixtureURL, encoding: .utf8)
        guard let expected = fixture
            .split(separator: "\n")
            .first(where: { $0.hasPrefix("navigation.payload=") })?
            .split(separator: "=", maxSplits: 1)
            .last
            .map(String.init)
        else { throw CheckError.vectorMissing }

        let input = MotoBLENavigationSnapshotInput()
        input.stateName = "navigating"
        input.networkName = "online"
        input.displayPageName = "navigation"
        input.maneuverName = "right"
        input.trafficName = "slow"
        input.hasDestination = true
        input.hasFix = true
        input.hasRouteView = true
        input.routeToken = codec.routeToken(forRouteID: "web-beijing-fixture")
        input.routeGeneration = 0x01020304
        input.maneuverID = 42
        input.distanceToManeuverM = 376
        input.remainingDistanceM = 12_345
        input.remainingDurationS = 1_020
        input.routeProgressM = 2_100
        input.totalDistanceM = 14_445
        input.speedDeciKPH = 483
        input.speedLimitKPH = 50
        input.headingCentiDegrees = 9_123
        input.accuracyDecimeters = 38
        input.crossTrackDecimeters = 125
        input.roadName = "东长安街"
        input.instructionText = "前方路口右转"

        let payload = try codec.encodeNavigationPayload(forGoldenCheck: input)
        let actual = payload.map { String(format: "%02x", $0) }.joined()
        guard actual == expected else {
            throw CheckError.mismatch(expected: expected, actual: actual)
        }

        guard let expectedHome = fixture
            .split(separator: "\n")
            .first(where: { $0.hasPrefix("home.payload=") })?
            .split(separator: "=", maxSplits: 1)
            .last
            .map(String.init)
        else { throw CheckError.homeVectorMissing }

        let home = MotoBLEHomeLocationInput()
        home.revision = 3
        home.hasHome = true
        home.latitudeE6 = 36_674_512
        home.longitudeE6 = 117_122_880
        let homePayload = try codec.encodeHomeLocationPayload(forTesting: home)
        let actualHome = homePayload.map { String(format: "%02x", $0) }.joined()
        guard actualHome == expectedHome else {
            throw CheckError.mismatch(expected: expectedHome, actual: actualHome)
        }

        // SettingsState is the one message whose payload is not fixed by the
        // wire revision: it grew from 14 to 16 bytes without a bump. Pinning it
        // here is what turns that decision into something a test can fail on.
        guard let expectedSettings = fixture
            .split(separator: "\n")
            .first(where: { $0.hasPrefix("settings.payload=") })?
            .split(separator: "=", maxSplits: 1)
            .last
            .map(String.init)
        else { throw CheckError.settingsVectorMissing }

        let settings = MotoBLESettingsStateInput()
        settings.theme = 3
        settings.brightnessPercent = 65
        settings.units = 1
        settings.leanFullScaleDeg = 40
        settings.performanceTargetKph = 60
        settings.visiblePages = 0x1f
        settings.leanZeroOffsetCdeg = -125
        settings.ambientNight = true
        settings.showBuildings = true
        settings.revision = 5
        let settingsPayload = try codec.encodeSettingsStatePayload(forTesting: settings)
        let actualSettings = settingsPayload.map { String(format: "%02x", $0) }.joined()
        guard actualSettings == expectedSettings else {
            throw CheckError.mismatch(expected: expectedSettings, actual: actualSettings)
        }

        guard let expectedGnss = fixture
            .split(separator: "\n")
            .first(where: { $0.hasPrefix("gnss.payload=") })?
            .split(separator: "=", maxSplits: 1)
            .last
            .map(String.init)
        else { throw CheckError.gnssVectorMissing }

        let gnss = MotoBLEGnssInput()
        gnss.phoneMonotonicMs = 0x1234_5678
        gnss.fixAgeMs = 320
        gnss.speedCmps = 1_525
        gnss.speedAccuracyCmps = 40
        let gnssPayload = try codec.encodeGnssInputPayload(forTesting: gnss)
        let actualGnss = gnssPayload.map { String(format: "%02x", $0) }.joined()
        guard actualGnss == expectedGnss else {
            throw CheckError.mismatch(expected: expectedGnss, actual: actualGnss)
        }

        func vector(_ key: String) -> String? {
            fixture
                .split(separator: "\n")
                .first(where: { $0.hasPrefix("\(key)=") })?
                .split(separator: "=", maxSplits: 1)
                .last
                .map(String.init)
        }
        guard let expectedWifiConfig = vector("wifi_config.payload"),
              let expectedWifiStatus = vector("wifi_status.payload")
        else { throw CheckError.wifiVectorMissing }

        // The fixture's password is a test value, not a credential.
        let wifi = MotoBLEWifiConfigInput()
        wifi.ssid = "MOTO iPhone"
        wifi.password = "ride-safe-2026"
        let wifiPayload = try codec.encodeWifiConfigPayload(forTesting: wifi)
        let actualWifiConfig = wifiPayload.map { String(format: "%02x", $0) }.joined()
        guard actualWifiConfig == expectedWifiConfig else {
            throw CheckError.mismatch(expected: expectedWifiConfig, actual: actualWifiConfig)
        }

        // The phone never encodes WifiStatus, so the decode is what is pinned.
        var statusBytes = Data()
        var index = expectedWifiStatus.startIndex
        while index < expectedWifiStatus.endIndex {
            let next = expectedWifiStatus.index(index, offsetBy: 2)
            guard let byte = UInt8(expectedWifiStatus[index..<next], radix: 16) else {
                throw CheckError.wifiVectorMissing
            }
            statusBytes.append(byte)
            index = next
        }
        let status = try codec.decodeWifiStatusPayload(forTesting: statusBytes)
        let actualWifiStatus = "state=\(status.state) failure=\(status.failure) "
            + "stored=\(status.hasCredentials) ip=\(status.ipv4Address) rssi=\(status.rssiDbm)"
        let expectedDecoded = "state=2 failure=0 stored=true ip=172.20.10.2 rssi=-58"
        guard actualWifiStatus == expectedDecoded else {
            throw CheckError.mismatch(expected: expectedDecoded, actual: actualWifiStatus)
        }
    }
}
