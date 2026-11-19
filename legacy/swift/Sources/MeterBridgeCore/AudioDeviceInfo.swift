import Foundation

/// A snapshot of an audio input device, independent of Core Audio types
/// so it can be stored and tested.
public struct AudioDeviceInfo: Identifiable, Hashable, Codable, Sendable {
    /// Core Audio's stable UID string (survives reboots and reconnects).
    public var uid: String
    public var name: String
    public var inputChannelCount: Int
    public var nominalSampleRate: Double

    public var id: String { uid }

    public init(uid: String, name: String, inputChannelCount: Int, nominalSampleRate: Double) {
        self.uid = uid
        self.name = name
        self.inputChannelCount = inputChannelCount
        self.nominalSampleRate = nominalSampleRate
    }

    /// Number of complete stereo pairs (1-2, 3-4, ...) the device offers.
    public var stereoPairCount: Int { inputChannelCount / 2 }

    /// True for Dante Virtual Soundcard, so it can be preselected.
    public var looksLikeDante: Bool {
        name.localizedCaseInsensitiveContains("dante")
    }
}
