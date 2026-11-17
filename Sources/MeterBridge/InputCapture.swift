import AudioToolbox
import CoreAudio
import Foundation
import os

/// Per-channel peak and RMS collected since the last `takeSnapshot()`.
struct ChannelSnapshot {
    var peak: [Float]   // linear, 0...1+
    var rms: [Float]    // linear
    static let empty = ChannelSnapshot(peak: [], rms: [])
}

enum CaptureError: LocalizedError {
    case noHALUnit
    case coreAudio(String, OSStatus)

    var errorDescription: String? {
        switch self {
        case .noHALUnit: return "Couldn't create the Core Audio input unit."
        case let .coreAudio(step, status): return "\(step) failed (Core Audio error \(status))."
        }
    }
}

/// Reads every input channel of one device through an AUHAL unit
/// (input on, output off). The render callback never blocks or allocates:
/// it accumulates into private buffers and hands them over with a
/// try-lock, so a busy UI can never cause an audio dropout.
final class InputCapture {
    let channelCount: Int
    let sampleRate: Double

    fileprivate var unit: AudioUnit?
    static let maxFrames: UInt32 = 4096
    fileprivate var maxFrames: UInt32 { Self.maxFrames }
    fileprivate let bufferList: UnsafeMutableAudioBufferListPointer

    // Audio-thread-only accumulators.
    fileprivate let localPeak: UnsafeMutablePointer<Float>
    fileprivate let localSumSq: UnsafeMutablePointer<Float>
    fileprivate var localFrames = 0

    // Shared with the UI thread, guarded by `lock`.
    private let sharedPeak: UnsafeMutablePointer<Float>
    private let sharedSumSq: UnsafeMutablePointer<Float>
    private var sharedFrames = 0
    private let lock = OSAllocatedUnfairLock()

    init(deviceID: AudioDeviceID) throws {
        var desc = AudioComponentDescription(
            componentType: kAudioUnitType_Output,
            componentSubType: kAudioUnitSubType_HALOutput,
            componentManufacturer: kAudioUnitManufacturer_Apple,
            componentFlags: 0, componentFlagsMask: 0)
        guard let component = AudioComponentFindNext(nil, &desc) else { throw CaptureError.noHALUnit }

        var newUnit: AudioUnit?
        try check(AudioComponentInstanceNew(component, &newUnit), "Creating input unit")
        guard let unit = newUnit else { throw CaptureError.noHALUnit }

        func set<T>(_ prop: AudioUnitPropertyID, _ scope: AudioUnitScope, _ element: AudioUnitElement,
                    _ value: inout T, _ step: String) throws {
            try check(AudioUnitSetProperty(unit, prop, scope, element, &value,
                                           UInt32(MemoryLayout<T>.size)), step)
        }

        var channelCountValue = 0
        var sampleRateValue: Double = 0
        do {
            var on: UInt32 = 1, off: UInt32 = 0
            try set(kAudioOutputUnitProperty_EnableIO, kAudioUnitScope_Input, 1, &on, "Enabling input")
            try set(kAudioOutputUnitProperty_EnableIO, kAudioUnitScope_Output, 0, &off, "Disabling output")
            var dev = deviceID
            try set(kAudioOutputUnitProperty_CurrentDevice, kAudioUnitScope_Global, 0, &dev, "Selecting device")

            // The device's native format on the input side of bus 1.
            var deviceFormat = AudioStreamBasicDescription()
            var size = UInt32(MemoryLayout<AudioStreamBasicDescription>.size)
            try check(AudioUnitGetProperty(unit, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Input, 1,
                                           &deviceFormat, &size), "Reading device format")

            let channels = Int(deviceFormat.mChannelsPerFrame)
            // Deinterleaved 32-bit float: one buffer per channel.
            var clientFormat = AudioStreamBasicDescription(
                mSampleRate: deviceFormat.mSampleRate,
                mFormatID: kAudioFormatLinearPCM,
                mFormatFlags: kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked | kAudioFormatFlagIsNonInterleaved,
                mBytesPerPacket: 4, mFramesPerPacket: 1, mBytesPerFrame: 4,
                mChannelsPerFrame: UInt32(channels), mBitsPerChannel: 32, mReserved: 0)
            try set(kAudioUnitProperty_StreamFormat, kAudioUnitScope_Output, 1, &clientFormat, "Setting capture format")
            var frames = Self.maxFrames
            try set(kAudioUnitProperty_MaximumFramesPerSlice, kAudioUnitScope_Global, 0, &frames, "Setting buffer size")

            channelCountValue = channels
            sampleRateValue = deviceFormat.mSampleRate
        } catch {
            AudioComponentInstanceDispose(unit)
            throw error
        }

        self.channelCount = channelCountValue
        self.sampleRate = sampleRateValue
        let n = max(channelCountValue, 1)
        let list = AudioBufferList.allocate(maximumBuffers: n)
        for i in 0..<n {
            let data = UnsafeMutablePointer<Float>.allocate(capacity: Int(Self.maxFrames))
            data.initialize(repeating: 0, count: Int(Self.maxFrames))
            list[i] = AudioBuffer(mNumberChannels: 1, mDataByteSize: Self.maxFrames * 4, mData: data)
        }
        func zeroed() -> UnsafeMutablePointer<Float> {
            let p = UnsafeMutablePointer<Float>.allocate(capacity: n)
            p.initialize(repeating: 0, count: n)
            return p
        }
        self.unit = unit
        self.bufferList = list
        self.localPeak = zeroed()
        self.localSumSq = zeroed()
        self.sharedPeak = zeroed()
        self.sharedSumSq = zeroed()
    }

    deinit {
        stop()
        if let unit { AudioComponentInstanceDispose(unit) }
        for buffer in bufferList { buffer.mData?.deallocate() }
        free(bufferList.unsafeMutablePointer)
        localPeak.deallocate(); localSumSq.deallocate()
        sharedPeak.deallocate(); sharedSumSq.deallocate()
    }

    func start() throws {
        guard let unit else { throw CaptureError.noHALUnit }
        var callback = AURenderCallbackStruct(
            inputProc: inputCallback,
            inputProcRefCon: Unmanaged.passUnretained(self).toOpaque())
        try check(AudioUnitSetProperty(unit, kAudioOutputUnitProperty_SetInputCallback, kAudioUnitScope_Global, 0,
                                       &callback, UInt32(MemoryLayout<AURenderCallbackStruct>.size)),
                  "Installing input callback")
        try check(AudioUnitInitialize(unit), "Starting input unit")
        try check(AudioOutputUnitStart(unit), "Starting capture")
    }

    func stop() {
        guard let unit else { return }
        AudioOutputUnitStop(unit)
        AudioUnitUninitialize(unit)
    }

    /// Called from the UI thread: returns levels since the last call and resets.
    func takeSnapshot() -> ChannelSnapshot {
        lock.withLock {
            let frames = sharedFrames
            var peak = [Float](repeating: 0, count: channelCount)
            var rms = [Float](repeating: 0, count: channelCount)
            for c in 0..<channelCount {
                peak[c] = sharedPeak[c]
                rms[c] = frames > 0 ? (sharedSumSq[c] / Float(frames)).squareRoot() : 0
                sharedPeak[c] = 0
                sharedSumSq[c] = 0
            }
            sharedFrames = 0
            return ChannelSnapshot(peak: peak, rms: rms)
        }
    }

    /// Audio thread: fold the local accumulators into the shared ones if the
    /// lock is free right now; otherwise keep them for the next callback.
    fileprivate func publishIfPossible() {
        let published: Bool? = lock.withLockIfAvailable {
            for c in 0..<channelCount {
                sharedPeak[c] = max(sharedPeak[c], localPeak[c])
                sharedSumSq[c] += localSumSq[c]
            }
            sharedFrames += localFrames
            return true
        }
        if published == true {
            for c in 0..<channelCount { localPeak[c] = 0; localSumSq[c] = 0 }
            localFrames = 0
        }
    }
}

/// Core Audio input callback (real-time thread).
private let inputCallback: AURenderCallback = { refCon, flags, timeStamp, _, frameCount, _ in
    let capture = Unmanaged<InputCapture>.fromOpaque(refCon).takeUnretainedValue()
    guard let unit = capture.unit, frameCount <= capture.maxFrames else { return noErr }

    let list = capture.bufferList
    for i in 0..<list.count { list[i].mDataByteSize = frameCount * 4 }
    let status = AudioUnitRender(unit, flags, timeStamp, 1, frameCount, list.unsafeMutablePointer)
    guard status == noErr else { return status }

    let frames = Int(frameCount)
    for c in 0..<capture.channelCount {
        guard let data = list[c].mData?.assumingMemoryBound(to: Float.self) else { continue }
        var peak = capture.localPeak[c]
        var sumSq: Float = 0
        for f in 0..<frames {
            let s = data[f]
            let a = abs(s)
            if a > peak { peak = a }
            sumSq += s * s
        }
        capture.localPeak[c] = peak
        capture.localSumSq[c] += sumSq
    }
    capture.localFrames += frames
    capture.publishIfPossible()
    return noErr
}

private func check(_ status: OSStatus, _ step: String) throws {
    if status != noErr { throw CaptureError.coreAudio(step, status) }
}
