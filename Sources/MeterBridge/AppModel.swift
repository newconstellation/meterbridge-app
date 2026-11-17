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

    // Analysis results, refreshed ~30 times a second.
    @Published private(set) var spectra: [Int: SourceSpectrum] = [:]
    @Published private(set) var loudness: [Int: LoudnessReading] = [:]
    @Published private(set) var sessionStart = Date()
    let spectrogram = SpectrogramModel()

    // View state.
    enum ViewMode: String, CaseIterable, Identifiable {
        case curves = "Overlay", bars = "Bands", spectrogram = "Spectrogram"
        var id: String { rawValue }
    }
    @Published var viewMode: ViewMode = .curves
    @Published var showInputs = true
    @Published var hiddenSources: Set<Int> = []
    @Published var soloSource: Int?
    @Published var loudnessSourceID: Int = 0
    /// Loudness target in LUFS; integrated readings above it turn red.
    @Published var targetLUFS: Double = -18

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
    private var engine: AnalysisEngine?

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
            engine = AnalysisEngine(sampleRate: newCapture.sampleRate)
            spectrogram.clear()
            sessionStart = Date()
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
        engine = nil
        isCapturing = false
        levels = [:]
        spectra = [:]
        loudness = [:]
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

        let active = setup.activeSources(channelCount: min(snap.peak.count, capture.rings.count))
        if let engine {
            let result = engine.process(capture: capture, sources: active, elapsed: elapsed)
            spectra = result.spectra
            loudness = result.loudness

            let layers = visibleSources.compactMap { source -> SpectrogramPainter.Layer? in
                guard let spectrum = result.spectra[source.id] else { return nil }
                return SpectrogramPainter.Layer(levels: spectrum.rawCurve, color: SourcePalette.color(source.colorIndex))
            }
            spectrogram.push(column: SpectrogramPainter.column(layers: layers, rows: spectrogram.rows))
        }
        if !active.contains(where: { $0.id == loudnessSourceID }), let first = active.first {
            loudnessSourceID = first.id
        }
    }

    // MARK: - View helpers

    /// Enabled, patched sources that the spectrum views should draw,
    /// honouring solo and hide.
    var visibleSources: [SourceConfig] {
        let active = setup.activeSources(channelCount: currentDevice?.inputChannelCount ?? 0)
        if let solo = soloSource, let s = active.first(where: { $0.id == solo }) { return [s] }
        return active.filter { !hiddenSources.contains($0.id) }
    }

    var activeSources: [SourceConfig] {
        setup.activeSources(channelCount: currentDevice?.inputChannelCount ?? 0)
    }

    func toggleHidden(_ id: Int) {
        if hiddenSources.contains(id) { hiddenSources.remove(id) } else { hiddenSources.insert(id) }
    }

    func toggleSolo(_ id: Int) {
        soloSource = soloSource == id ? nil : id
    }

    /// Clears integrated loudness, range, true-peak max and the session clock.
    func resetSession() {
        engine?.resetLoudness()
        spectrogram.clear()
        sessionStart = Date()
    }

    /// True when any visible source is above -60 dBFS.
    var hasSignal: Bool {
        levels.values.contains { max($0.peakL, $0.peakR) > -60 }
    }
}
