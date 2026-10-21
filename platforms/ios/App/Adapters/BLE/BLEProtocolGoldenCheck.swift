import Foundation

enum BLEProtocolGoldenCheck {
    enum CheckError: LocalizedError {
        case fixtureMissing
        case vectorMissing
        case homeVectorMissing
        case mismatch(expected: String, actual: String)

        var errorDescription: String? {
            switch self {
            case .fixtureMissing: "BLE v1 黄金帧资源不存在"
            case .vectorMissing: "BLE v1 黄金帧缺少 navigation.payload"
            case .homeVectorMissing: "BLE v1 黄金帧缺少 home.payload"
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
    }
}
