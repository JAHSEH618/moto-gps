// swift-tools-version: 6.0

import PackageDescription

let package = Package(
    name: "MotoNavigationCore",
    platforms: [
        .iOS(.v17),
        .macOS(.v14),
    ],
    products: [
        .library(name: "MotoNavigationCore", targets: ["MotoNavigationCore"]),
        .library(name: "MotoRideCore", targets: ["MotoRideCore"]),
        .executable(name: "MotoNavigationCoreChecks", targets: ["MotoNavigationCoreChecks"]),
    ],
    targets: [
        .target(name: "MotoNavigationCore"),
        .target(name: "MotoRideCore"),
        .executableTarget(
            name: "MotoNavigationCoreChecks",
            dependencies: ["MotoNavigationCore"]
        ),
        .testTarget(
            name: "MotoNavigationCoreTests",
            dependencies: ["MotoNavigationCore"]
        ),
        .testTarget(
            name: "MotoRideCoreTests",
            dependencies: ["MotoRideCore"]
        ),
    ]
)
