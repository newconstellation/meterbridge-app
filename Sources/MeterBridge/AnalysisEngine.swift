import Accelerate
import CoreGraphics
import Foundation
import MeterBridgeCore

/// What the spectrum views draw for one source.
struct SourceSpectrum {
    var curve: [Float]        // smoothed, dB, one per curve point
    var curvePeaks: [Float]   // peak-hold, dB
    var rawCurve: [Float]     // instantaneous, dB (feeds the spectrogram)
    var bands: [Float]        // 1/3-octave, smoothed, dB
    var bandPeaks: [Float]    // 1/3-octave peak-hold, dB
}

/// Loudness numbers for one source (LUFS / LU / dBTP).
struct LoudnessReading {
    var momentary: Double
    var shortTerm: Double
    var shortTermMax: Double
    var integrated: Double
    var range: Double
    var truePeakNow: Float
    var truePeakMax: Float

    static let empty = LoudnessReading(
        momentary: -.infinity, shortTerm: -.infinity, shortTermMax: -.infinity,
        integrated: -.infinity, range: 0, truePeakNow: LevelMath.floorDB, truePeakMax: LevelMath.floorDB)
}

/// Pulls audio from the capture rings each UI tick and turns it into
/// spectra and loudness per source. Runs on the main actor; the work per
/// tick is a few small FFTs and filters, well under a millisecond.
@MainActor
final class AnalysisEngine {
    nonisolated static let fftSize = 8192
    nonisolated static let curvePoints = 240

    let sampleRate: Double
    let curveMap: FrequencyMap
    let bandMap: FrequencyMap
    private let analyzer: SpectrumAnalyzer
    private var states: [Int: SourceState] = [:]

    private var left: [Float]
    private var right: [Float]
    private var mono: [Float]

    private final class SourceState {
        let leftChannel: Int
        let rightChannel: Int
        let meter: LoudnessMeter
        var cursor: Int
        var curve = SpectrumBallistics(count: AnalysisEngine.curvePoints)
        var bands = SpectrumBallistics(count: FrequencyMap.thirdOctaveCentres.count)

        init(source: SourceConfig, sampleRate: Double, cursor: Int) {
            leftChannel = source.leftChannel
            rightChannel = source.rightChannel
            meter = LoudnessMeter(sampleRate: sampleRate)
            self.cursor = cursor
        }
    }

    init(sampleRate: Double) {
        let rate = sampleRate > 0 ? sampleRate : 48_000
        self.sampleRate = rate
        analyzer = SpectrumAnalyzer(size: Self.fftSize, sampleRate: rate)
        curveMap = .logCurve(points: Self.curvePoints, fftSize: Self.fftSize, sampleRate: rate)
        bandMap = .thirdOctave(fftSize: Self.fftSize, sampleRate: rate)
        left = [Float](repeating: 0, count: Self.fftSize)
        right = left
        mono = left
    }

    func resetLoudness() {
        for state in states.values { state.meter.reset() }
    }

    func process(capture: InputCapture, sources: [SourceConfig], elapsed: Float)
        -> (spectra: [Int: SourceSpectrum], loudness: [Int: LoudnessReading])
    {
        var spectra: [Int: SourceSpectrum] = [:]
        var loudness: [Int: LoudnessReading] = [:]
        let activeIDs = Set(sources.map(\.id))
        states = states.filter { activeIDs.contains($0.key) }

        for source in sources {
            let ringL = capture.rings[source.leftChannel]
            let ringR = capture.rings[source.rightChannel]

            let state: SourceState
            if let existing = states[source.id],
               existing.leftChannel == source.leftChannel, existing.rightChannel == source.rightChannel {
                state = existing
            } else {
                state = SourceState(source: source, sampleRate: sampleRate,
                                    cursor: min(ringL.totalWritten, ringR.totalWritten))
                states[source.id] = state
            }

            feedLoudness(state, ringL, ringR)
            loudness[source.id] = LoudnessReading(
                momentary: state.meter.momentary,
                shortTerm: state.meter.shortTerm,
                shortTermMax: state.meter.shortTermMax,
                integrated: state.meter.integrated,
                range: state.meter.loudnessRange,
                truePeakNow: LevelMath.toDB(state.meter.recentTruePeak),
                truePeakMax: LevelMath.toDB(state.meter.truePeak))

            if let spectrum = analyzeSpectrum(state, ringL, ringR, elapsed: elapsed) {
                spectra[source.id] = spectrum
            }
        }
        return (spectra, loudness)
    }

    /// Feeds every new sample since the last tick into the loudness meter.
    private func feedLoudness(_ state: SourceState, _ ringL: SampleRing, _ ringR: SampleRing) {
        let end = min(ringL.totalWritten, ringR.totalWritten)
        let maxLag = InputCapture.ringCapacity / 2
        if end - state.cursor > maxLag { state.cursor = end - maxLag } // fell behind: skip ahead
        let chunk = Self.fftSize
        while state.cursor < end {
            let n = min(chunk, end - state.cursor)
            let ok = left.withUnsafeMutableBufferPointer { l in
                right.withUnsafeMutableBufferPointer { r in
                    ringL.read(from: state.cursor, count: n, into: l.baseAddress!)
                        && ringR.read(from: state.cursor, count: n, into: r.baseAddress!)
                }
            }
            guard ok else { state.cursor = end; return }
            left.withUnsafeBufferPointer { l in
                right.withUnsafeBufferPointer { r in
                    state.meter.process(left: l.baseAddress!, right: r.baseAddress!, count: n)
                }
            }
            state.cursor += n
        }
    }

    /// FFT of the latest window of (L+R)/2.
    private func analyzeSpectrum(_ state: SourceState, _ ringL: SampleRing, _ ringR: SampleRing,
                                 elapsed: Float) -> SourceSpectrum? {
        let n = Self.fftSize
        let ok = left.withUnsafeMutableBufferPointer { l in
            right.withUnsafeMutableBufferPointer { r in
                ringL.readLatest(count: n, into: l.baseAddress!)
                    && ringR.readLatest(count: n, into: r.baseAddress!)
            }
        }
        guard ok else { return nil }

        var half: Float = 0.5
        mono.withUnsafeMutableBufferPointer { m in
            vDSP_vadd(left, 1, right, 1, m.baseAddress!, 1, vDSP_Length(n))
            vDSP_vsmul(m.baseAddress!, 1, &half, m.baseAddress!, 1, vDSP_Length(n))
        }
        let power = analyzer.power(of: mono)
        let rawCurve = curveMap.levels(fromPower: power)
        let rawBands = bandMap.levels(fromPower: power)
        state.curve.update(with: rawCurve, elapsed: elapsed)
        state.bands.update(with: rawBands, elapsed: elapsed)
        return SourceSpectrum(
            curve: state.curve.levels, curvePeaks: state.curve.peaks, rawCurve: rawCurve,
            bands: state.bands.levels, bandPeaks: state.bands.peaks)
    }
}

/// A scrolling spectrogram image, newest column on the right.
@MainActor
final class SpectrogramModel: ObservableObject {
    let width = 600
    let rows = AnalysisEngine.curvePoints

    @Published private(set) var image: CGImage?

    private var pixels: [UInt8]
    private var ordered: [UInt8]
    private var writeX = 0

    init() {
        pixels = [UInt8](repeating: 0, count: width * rows * 4)
        ordered = pixels
        for i in stride(from: 3, to: pixels.count, by: 4) { pixels[i] = 255 }
    }

    func clear() {
        for i in pixels.indices { pixels[i] = i % 4 == 3 ? 255 : 0 }
        image = nil
    }

    /// `column` is RGBA for `rows` pixels, top first.
    func push(column: [UInt8]) {
        guard column.count == rows * 4 else { return }
        for y in 0..<rows {
            let dst = (y * width + writeX) * 4
            let src = y * 4
            pixels[dst] = column[src]
            pixels[dst + 1] = column[src + 1]
            pixels[dst + 2] = column[src + 2]
            pixels[dst + 3] = 255
        }
        writeX = (writeX + 1) % width

        // Unroll the circular buffer so the oldest column is on the left.
        let rowBytes = width * 4
        let splitBytes = writeX * 4
        pixels.withUnsafeBufferPointer { src in
            ordered.withUnsafeMutableBufferPointer { dst in
                for y in 0..<rows {
                    let base = y * rowBytes
                    let tail = rowBytes - splitBytes
                    // oldest part: [writeX, width)
                    UnsafeMutableRawPointer(dst.baseAddress! + base)
                        .copyMemory(from: src.baseAddress! + base + splitBytes, byteCount: tail)
                    // newest part: [0, writeX)
                    UnsafeMutableRawPointer(dst.baseAddress! + base + tail)
                        .copyMemory(from: src.baseAddress! + base, byteCount: splitBytes)
                }
            }
        }
        image = makeImage()
    }

    private func makeImage() -> CGImage? {
        let data = Data(ordered) as CFData
        guard let provider = CGDataProvider(data: data),
              let space = CGColorSpace(name: CGColorSpace.sRGB) else { return nil }
        return CGImage(
            width: width, height: rows, bitsPerComponent: 8, bitsPerPixel: 32,
            bytesPerRow: width * 4, space: space,
            bitmapInfo: CGBitmapInfo(rawValue: CGImageAlphaInfo.noneSkipLast.rawValue),
            provider: provider, decode: nil, shouldInterpolate: true, intent: .defaultIntent)
    }
}
