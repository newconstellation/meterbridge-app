import Foundation

/// One of the four colour-coded sources (A to D) shown on the overlay.
/// Channels are zero-based indexes into the input device's channels.
public struct SourceConfig: Identifiable, Hashable, Codable, Sendable {
    public static let maxSources = 4

    public var id: Int            // 0...3 → A...D
    public var name: String
    public var colorIndex: Int    // index into SourcePalette
    public var leftChannel: Int
    public var rightChannel: Int
    public var isEnabled: Bool

    public init(id: Int, name: String, colorIndex: Int, leftChannel: Int, rightChannel: Int, isEnabled: Bool) {
        self.id = id
        self.name = name
        self.colorIndex = colorIndex
        self.leftChannel = leftChannel
        self.rightChannel = rightChannel
        self.isEnabled = isEnabled
    }

    public var letter: String {
        String(UnicodeScalar(UInt8(65 + id)))
    }

    /// Human label for the pair, e.g. "3-4" (one-based like a patchbay).
    public var channelLabel: String {
        "\(leftChannel + 1)-\(rightChannel + 1)"
    }

    /// Whether both channels exist on a device with `channelCount` inputs.
    public func isValid(forChannelCount channelCount: Int) -> Bool {
        leftChannel >= 0 && rightChannel >= 0
            && leftChannel < channelCount && rightChannel < channelCount
    }

    /// Default setup: A to D on consecutive stereo pairs 1-2, 3-4, 5-6, 7-8.
    public static func defaults() -> [SourceConfig] {
        (0..<maxSources).map { i in
            SourceConfig(
                id: i,
                name: "Keys \(i + 1)",
                colorIndex: i,
                leftChannel: i * 2,
                rightChannel: i * 2 + 1,
                isEnabled: i == 0
            )
        }
    }
}

/// Distinct, colour-blind-friendly colours for the overlay, as sRGB 0...1.
/// Kept here (not as SwiftUI Colors) so the core stays UI-free.
public enum SourcePalette {
    public struct RGB: Hashable, Sendable {
        public let r: Double, g: Double, b: Double
    }

    public static let colors: [RGB] = [
        RGB(r: 0.20, g: 0.60, b: 1.00), // blue
        RGB(r: 1.00, g: 0.62, b: 0.10), // orange
        RGB(r: 0.25, g: 0.85, b: 0.45), // green
        RGB(r: 0.93, g: 0.30, b: 0.75), // magenta
    ]

    public static func color(_ index: Int) -> RGB {
        colors[((index % colors.count) + colors.count) % colors.count]
    }
}
