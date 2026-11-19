import AppKit
import SwiftUI

@main
struct MeterBridgeApp: App {
    @StateObject private var model = AppModel()

    init() {
        // Show a normal Dock icon and window even when launched from `swift run`.
        NSApplication.shared.setActivationPolicy(.regular)
    }

    var body: some Scene {
        WindowGroup("Meter Bridge") {
            ContentView()
                .environmentObject(model)
                .frame(minWidth: 1180, minHeight: 640)
                .preferredColorScheme(.dark)
        }
        .windowStyle(.hiddenTitleBar)
    }
}
