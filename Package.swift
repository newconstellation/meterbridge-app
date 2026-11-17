// swift-tools-version:5.9
import PackageDescription

let package = Package(
    name: "MeterBridge",
    platforms: [.macOS(.v15)],
    products: [
        .executable(name: "MeterBridge", targets: ["MeterBridge"]),
        .library(name: "MeterBridgeCore", targets: ["MeterBridgeCore"]),
    ],
    targets: [
        // Pure, platform-light logic: source setup, channel validation,
        // persistence and level math. Unit tested.
        .target(name: "MeterBridgeCore"),

        // The SwiftUI Mac app: Core Audio device discovery, capture, UI.
        .executableTarget(
            name: "MeterBridge",
            dependencies: ["MeterBridgeCore"]
        ),

        .testTarget(
            name: "MeterBridgeCoreTests",
            dependencies: ["MeterBridgeCore"]
        ),
    ]
)
