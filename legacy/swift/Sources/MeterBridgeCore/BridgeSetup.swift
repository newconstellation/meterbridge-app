import Foundation

/// Everything the user sets up: which device, and how A to D are patched.
public struct BridgeSetup: Codable, Hashable, Sendable {
    public var deviceUID: String?
    public var sources: [SourceConfig]

    public init(deviceUID: String? = nil, sources: [SourceConfig] = SourceConfig.defaults()) {
        self.deviceUID = deviceUID
        self.sources = sources
    }

    /// Sources that are switched on and patched to channels the device has.
    public func activeSources(channelCount: Int) -> [SourceConfig] {
        sources.filter { $0.isEnabled && $0.isValid(forChannelCount: channelCount) }
    }

    /// Repairs a setup loaded from disk: always exactly four sources with
    /// ids 0...3, in order, whatever was stored.
    public func normalized() -> BridgeSetup {
        let defaults = SourceConfig.defaults()
        let fixed = (0..<SourceConfig.maxSources).map { i in
            sources.first(where: { $0.id == i }) ?? defaults[i]
        }
        return BridgeSetup(deviceUID: deviceUID, sources: fixed)
    }

    /// Picks the device to use: the saved one if still present, otherwise
    /// Dante Virtual Soundcard, otherwise the first device with inputs.
    public func resolveDevice(from devices: [AudioDeviceInfo]) -> AudioDeviceInfo? {
        let withInputs = devices.filter { $0.inputChannelCount > 0 }
        if let uid = deviceUID, let saved = withInputs.first(where: { $0.uid == uid }) {
            return saved
        }
        return withInputs.first(where: \.looksLikeDante) ?? withInputs.first
    }
}

/// Saves and loads the setup as JSON in UserDefaults.
public struct SetupStore {
    public static let key = "MeterBridge.setup.v1"
    private let defaults: UserDefaults

    public init(defaults: UserDefaults = .standard) {
        self.defaults = defaults
    }

    public func load() -> BridgeSetup {
        guard let data = defaults.data(forKey: Self.key),
              let setup = try? JSONDecoder().decode(BridgeSetup.self, from: data)
        else { return BridgeSetup() }
        return setup.normalized()
    }

    public func save(_ setup: BridgeSetup) {
        if let data = try? JSONEncoder().encode(setup) {
            defaults.set(data, forKey: Self.key)
        }
    }
}
