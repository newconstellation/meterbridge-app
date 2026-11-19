import SwiftUI

/// Hardware-metering-station look: black chassis, dark navy screens with a
/// thin steel border, cyan readouts.
enum Theme {
    static let chassis = Color(red: 0.07, green: 0.07, blue: 0.08)
    static let screen = Color(red: 0.02, green: 0.04, blue: 0.08)
    static let screenBorder = Color(red: 0.32, green: 0.40, blue: 0.50)
    static let grid = Color.white.opacity(0.10)
    static let gridLabel = Color.white.opacity(0.55)
    static let readout = Color(red: 0.75, green: 0.93, blue: 1.0)
    static let accent = Color(red: 0.21, green: 0.85, blue: 0.96)
    static let warn = Color(red: 1.0, green: 0.30, blue: 0.24)
    static let caption = Color.white.opacity(0.75)

    static func digits(_ size: CGFloat, weight: Font.Weight = .semibold) -> Font {
        .system(size: size, weight: weight, design: .rounded).monospacedDigit()
    }

    static let label = Font.system(size: 10, weight: .semibold)
}

/// A dark "screen" inset with a thin border, like an LCD in a rack unit.
struct ScreenBox<Content: View>: View {
    var padding: CGFloat = 8
    @ViewBuilder var content: Content

    var body: some View {
        content
            .padding(padding)
            .background(RoundedRectangle(cornerRadius: 6).fill(Theme.screen))
            .overlay(RoundedRectangle(cornerRadius: 6).stroke(Theme.screenBorder, lineWidth: 1))
    }
}

/// Small screw head, purely decorative.
struct Screw: View {
    var body: some View {
        Circle()
            .fill(RadialGradient(colors: [Color(white: 0.35), Color(white: 0.08)],
                                 center: .topLeading, startRadius: 1, endRadius: 9))
            .overlay(Circle().stroke(Color.black, lineWidth: 1))
            .frame(width: 11, height: 11)
    }
}

extension Double {
    /// "-17.6", or "--" for silence.
    var lufsText: String {
        isFinite ? String(format: "%.1f", self) : "--"
    }
}

extension Float {
    var dbText: String {
        self > -119 ? String(format: "%.1f", self) : "--"
    }
}
