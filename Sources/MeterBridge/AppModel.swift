import AVFoundation
import CoreAudio
import Foundation
import MeterBridgeCore

/// App state: the device list, the A-D setup, live levels, and capture.
@MainActor
final class AppModel: ObservableObject {
    @Published private(set) var devices: [AudioDeviceInfo] = []
    @Published private(set) var currentDevice: AudioDeviceInfo?
    @Published private(set) var levels: [Int: StereoLevel] = [:]
    @Published private(set) var statusMessage: String = "Starting…"
    @Published private(set) var isCapturing = false

    @Published var setup: BridgeSetup {
        didSet {
            store.save(setup)
            if setup.deviceUID != oldValue.deviceUID { restartCapture() }
        }
    }

    private let store = SetupStore()
    private var capture: InputCapture?
    private var meterTimer: Timer?
    private var deviceObserver: AudioObjectPropertyListenerBlock?
    private var lastTick = Date()

    init() {
        setup = SetupStore().load()
        refreshDevices()
        deviceObserver = CoreAudioDevices.addDeviceListObserver { [weak self] in
            MainActor.assumeIsolated { self?.devicesChanged() }
        }
        requestMicrophoneAccessThenStart()
    }

    // MARK: - Devices

    func refreshDevices() {
        devices = CoreAudioDevices.inputDevices().sorted {
            // Dante first, then alphabetical.
            if $0.looksLikeDante != $1.looksLikeDante { return $0.looksLikeDante }
            return $0.name.localizedCaseInsensitiveCompare($1.name) == .orderedAscending
        }
    }

    private func devicesChanged() {
        let before = currentDevice?.uid
        refreshDevices()
        let stillThere = devices.contains { $0.uid == before }
        if !stillThere || capture == nil { restartCapture() }
    }

    func selectDevice(uid: String) {
        setup.deviceUID = uid
    }

    // MARK: - Sources

    func update(source: SourceConfig) {
        guard setup.sources.indices.contains(source.id) else { return }
        setup.sources[source.id] = source
    }

    /// Stereo pairs offered for patching: 1-2, 3-4, ... for the current device.
    var stereoPairs: [Int] {
        Array(0..<max(currentDevice?.stereoPairCount ?? 0, 0))
    }

    // MARK: - Capture

    private func requestMicrophoneAccessThenStart() {
        switch AVCaptureDevice.authorizationStatus(for: .audio) {
        case .authorized:
            restartCapture()
        case .notDetermined:
            statusMessage = "Waiting for audio input permission…"
            AVCaptureDevice.requestAccess(for: .audio) { [weak self] granted in
                Task { @MainActor in
                    if granted { self?.restartCapture() } else { self?.showPermissionDenied() }
                }
            }
        default:
            showPermissionDenied()
        }
    }

    private func showPermissionDenied() {
        statusMessage = "Audio input is blocked. Allow Meter Bridge in System Settings › Privacy & Security › Microphone."
    }

    func restartCapture() {
        stopCapture()
        guard let device = setup.resolveDevice(from: devices) else {
            currentDevice = nil
            statusMessage = "No audio input devices found. Is Dante Virtual Soundcard running?"
            return
        }
        currentDevice = device
        guard let id = CoreAudioDevices.deviceID(forUID: device.uid) else {
            statusMessage = "\(device.name) is no longer available."
            return
        }
        do {
            let newCapture = try InputCapture(deviceID: id)
            try newCapture.start()
            capture = newCapture
            isCapturing = true
            let rate = newCapture.sampleRate > 0 ? String(format: " · %.0f Hz", newCapture.sampleRate) : ""
            statusMessage = "Listening to \(device.name) · \(newCapture.channelCount) inputs\(rate)"
            startMeterTimer()
        } catch {
            statusMessage = "Couldn't open \(device.name): \(error.localizedDescription)"
        }
    }

    private func stopCapture() {
        meterTimer?.invalidate()
        meterTimer = nil
        capture?.stop()
        capture = nil
        isCapturing = false
        levels = [:]
    }

    private func startMeterTimer() {
        lastTick = Date()
        let timer = Timer(timeInterval: 1.0 / 30.0, repeats: true) { [weak self] _ in
            MainActor.assumeIsolated { self?.tick() }
        }
        RunLoop.main.add(timer, forMode: .common)
        meterTimer = timer
    }

    private func tick() {
        guard let capture else { return }
        let now = Date()
        let elapsed = Float(now.timeIntervalSince(lastTick))
        lastTick = now

        let snap = capture.takeSnapshot()
        var next: [Int: StereoLevel] = [:]
        for source in setup.activeSources(channelCount: snap.peak.count) {
            let reading = StereoLevel(
                peakL: LevelMath.toDB(snap.peak[source.leftChannel]),
                peakR: LevelMath.toDB(snap.peak[source.rightChannel]),
                rmsL: LevelMath.toDB(snap.rms[source.leftChannel]),
                rmsR: LevelMath.toDB(snap.rms[source.rightChannel]))
            let previous = levels[source.id] ?? .silent
            next[source.id] = previous.smoothed(toward: reading, elapsed: elapsed)
        }
        levels = next
    }
}
