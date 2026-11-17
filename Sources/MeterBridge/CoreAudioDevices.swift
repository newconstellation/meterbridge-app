import CoreAudio
import Foundation
import MeterBridgeCore

/// Thin wrappers around the Core Audio HAL for finding input devices.
enum CoreAudioDevices {
    private static let system = AudioObjectID(kAudioObjectSystemObject)

    /// Every device that has at least one input channel.
    static func inputDevices() -> [AudioDeviceInfo] {
        allDeviceIDs().compactMap(info(for:)).filter { $0.inputChannelCount > 0 }
    }

    /// Looks up the live Core Audio id for a stored device UID.
    static func deviceID(forUID uid: String) -> AudioDeviceID? {
        allDeviceIDs().first { stringProperty($0, kAudioDevicePropertyDeviceUID) == uid }
    }

    /// Calls `handler` on the main queue whenever devices are added or removed.
    /// Returns a token; pass it to `removeDeviceListObserver` to stop.
    static func addDeviceListObserver(_ handler: @escaping () -> Void) -> AudioObjectPropertyListenerBlock {
        var addr = address(kAudioHardwarePropertyDevices)
        let block: AudioObjectPropertyListenerBlock = { _, _ in handler() }
        AudioObjectAddPropertyListenerBlock(system, &addr, DispatchQueue.main, block)
        return block
    }

    static func removeDeviceListObserver(_ block: @escaping AudioObjectPropertyListenerBlock) {
        var addr = address(kAudioHardwarePropertyDevices)
        AudioObjectRemovePropertyListenerBlock(system, &addr, DispatchQueue.main, block)
    }

    // MARK: - Helpers

    private static func address(
        _ selector: AudioObjectPropertySelector,
        scope: AudioObjectPropertyScope = kAudioObjectPropertyScopeGlobal
    ) -> AudioObjectPropertyAddress {
        AudioObjectPropertyAddress(mSelector: selector, mScope: scope, mElement: kAudioObjectPropertyElementMain)
    }

    private static func allDeviceIDs() -> [AudioDeviceID] {
        var addr = address(kAudioHardwarePropertyDevices)
        var size: UInt32 = 0
        guard AudioObjectGetPropertyDataSize(system, &addr, 0, nil, &size) == noErr, size > 0 else { return [] }
        var ids = [AudioDeviceID](repeating: 0, count: Int(size) / MemoryLayout<AudioDeviceID>.size)
        guard AudioObjectGetPropertyData(system, &addr, 0, nil, &size, &ids) == noErr else { return [] }
        return ids
    }

    private static func info(for id: AudioDeviceID) -> AudioDeviceInfo? {
        guard let uid = stringProperty(id, kAudioDevicePropertyDeviceUID) else { return nil }
        let name = stringProperty(id, kAudioObjectPropertyName) ?? "Unknown device"
        return AudioDeviceInfo(
            uid: uid,
            name: name,
            inputChannelCount: inputChannelCount(id),
            nominalSampleRate: sampleRate(id)
        )
    }

    private static func stringProperty(_ id: AudioObjectID, _ selector: AudioObjectPropertySelector) -> String? {
        var addr = address(selector)
        var value: Unmanaged<CFString>?
        var size = UInt32(MemoryLayout<Unmanaged<CFString>?>.size)
        let status = withUnsafeMutablePointer(to: &value) { ptr in
            AudioObjectGetPropertyData(id, &addr, 0, nil, &size, ptr)
        }
        guard status == noErr, let cf = value else { return nil }
        return cf.takeRetainedValue() as String
    }

    private static func inputChannelCount(_ id: AudioDeviceID) -> Int {
        var addr = address(kAudioDevicePropertyStreamConfiguration, scope: kAudioDevicePropertyScopeInput)
        var size: UInt32 = 0
        guard AudioObjectGetPropertyDataSize(id, &addr, 0, nil, &size) == noErr, size > 0 else { return 0 }
        let raw = UnsafeMutableRawPointer.allocate(
            byteCount: Int(size), alignment: MemoryLayout<AudioBufferList>.alignment)
        defer { raw.deallocate() }
        guard AudioObjectGetPropertyData(id, &addr, 0, nil, &size, raw) == noErr else { return 0 }
        let list = UnsafeMutableAudioBufferListPointer(raw.assumingMemoryBound(to: AudioBufferList.self))
        return list.reduce(0) { $0 + Int($1.mNumberChannels) }
    }

    private static func sampleRate(_ id: AudioDeviceID) -> Double {
        var addr = address(kAudioDevicePropertyNominalSampleRate)
        var rate: Float64 = 0
        var size = UInt32(MemoryLayout<Float64>.size)
        guard AudioObjectGetPropertyData(id, &addr, 0, nil, &size, &rate) == noErr else { return 0 }
        return rate
    }
}
