import Accelerate
import Foundation

/// Hann-windowed real FFT using Accelerate. Returns per-bin level in dBFS,
/// scaled so a full-scale sine centred on a bin reads about 0 dB.
public final class SpectrumAnalyzer {
    public let size: Int
    public let sampleRate: Double
    private let log2n: vDSP_Length
    private let setup: FFTSetup
    private var window: [Float]
    private var windowed: [Float]
    private var real: [Float]
    private var imag: [Float]
    private var power: [Float]

    public init(size: Int = 8192, sampleRate: Double) {
        precondition(size > 0 && size & (size - 1) == 0, "FFT size must be a power of two")
        self.size = size
        self.sampleRate = sampleRate
        log2n = vDSP_Length(log2(Double(size)))
        guard let fft = vDSP_create_fftsetup(log2n, FFTRadix(kFFTRadix2)) else {
            fatalError("vDSP_create_fftsetup failed")
        }
        setup = fft
        window = [Float](repeating: 0, count: size)
        vDSP_hann_window(&window, vDSP_Length(size), Int32(vDSP_HANN_DENORM))
        windowed = [Float](repeating: 0, count: size)
        real = [Float](repeating: 0, count: size / 2)
        imag = [Float](repeating: 0, count: size / 2)
        power = [Float](repeating: 0, count: size / 2)
    }

    deinit { vDSP_destroy_fftsetup(setup) }

    public var binCount: Int { size / 2 }

    public func frequency(ofBin bin: Int) -> Double {
        Double(bin) * sampleRate / Double(size)
    }

    /// Linear power per bin (amplitude², where a full-scale sine ≈ 1).
    /// `samples` must hold `size` values.
    public func power(of samples: UnsafePointer<Float>) -> [Float] {
        let n = vDSP_Length(size)
        let half = size / 2
        vDSP_vmul(samples, 1, window, 1, &windowed, 1, n)

        real.withUnsafeMutableBufferPointer { rp in
            imag.withUnsafeMutableBufferPointer { ip in
                var split = DSPSplitComplex(realp: rp.baseAddress!, imagp: ip.baseAddress!)
                windowed.withUnsafeBufferPointer { wp in
                    wp.baseAddress!.withMemoryRebound(to: DSPComplex.self, capacity: half) { cp in
                        vDSP_ctoz(cp, 2, &split, 1, vDSP_Length(half))
                    }
                }
                vDSP_fft_zrip(setup, &split, 1, log2n, FFTDirection(kFFTDirection_Forward))
                ip[0] = 0 // imag[0] carries the Nyquist bin; drop it
                power.withUnsafeMutableBufferPointer { pp in
                    vDSP_zvmags(&split, 1, pp.baseAddress!, 1, vDSP_Length(half))
                }
            }
        }
        // zrip output is 2x the DFT; Hann coherent gain is 0.5. So a sine of
        // amplitude A gives |X| = A * N / 2. Normalise power to A².
        var scale = 1 / (Float(half) * Float(half))
        power.withUnsafeMutableBufferPointer { pp in
            vDSP_vsmul(pp.baseAddress!, 1, &scale, pp.baseAddress!, 1, vDSP_Length(half))
        }
        return power
    }

    public func power(of samples: [Float]) -> [Float] {
        samples.withUnsafeBufferPointer { power(of: $0.baseAddress!) }
    }
}

/// Maps FFT bins onto display points/bands on a log-frequency axis.
public struct FrequencyMap: Sendable {
    public enum Mode: Sendable { case peak, energy }

    public let frequencies: [Double]          // centre of each output point
    private let ranges: [(lo: Int, hi: Int)]  // inclusive bin ranges
    private let mode: Mode

    /// `count` points spaced logarithmically from `minHz` to `maxHz`, each
    /// taking the loudest bin in its slice. Good for smooth curves.
    public static func logCurve(points count: Int, minHz: Double = 20, maxHz: Double = 20_000,
                                fftSize: Int, sampleRate: Double) -> FrequencyMap {
        let ratio = pow(maxHz / minHz, 1 / Double(count - 1))
        let centres = (0..<count).map { minHz * pow(ratio, Double($0)) }
        let half = sqrt(ratio)
        return FrequencyMap(centres: centres, lowerFactor: 1 / half, upperFactor: half,
                            fftSize: fftSize, sampleRate: sampleRate, mode: .peak)
    }

    /// ISO 1/3-octave bands from 25 Hz to 20 kHz, summing energy per band.
    public static func thirdOctave(fftSize: Int, sampleRate: Double) -> FrequencyMap {
        let factor = pow(2, 1.0 / 6)
        return FrequencyMap(centres: thirdOctaveCentres, lowerFactor: 1 / factor, upperFactor: factor,
                            fftSize: fftSize, sampleRate: sampleRate, mode: .energy)
    }

    public static let thirdOctaveCentres: [Double] = [
        25, 31.5, 40, 50, 63, 80, 100, 125, 160, 200, 250, 315, 400, 500, 630, 800,
        1000, 1250, 1600, 2000, 2500, 3150, 4000, 5000, 6300, 8000, 10000, 12500, 16000, 20000,
    ]

    init(centres: [Double], lowerFactor: Double, upperFactor: Double,
         fftSize: Int, sampleRate: Double, mode: Mode) {
        let binHz = sampleRate / Double(fftSize)
        let lastBin = fftSize / 2 - 1
        frequencies = centres
        self.mode = mode
        ranges = centres.map { f in
            let lo = Int((f * lowerFactor / binHz).rounded(.down))
            let hi = Int((f * upperFactor / binHz).rounded(.up))
            let clampedLo = min(max(lo, 1), lastBin)
            let clampedHi = min(max(hi, clampedLo), lastBin)
            return (clampedLo, clampedHi)
        }
    }

    public var count: Int { frequencies.count }

    /// Reduces per-bin power to one dB value per point.
    public func levels(fromPower power: [Float]) -> [Float] {
        ranges.map { r in
            var v: Float = 0
            for b in r.lo...r.hi {
                switch mode {
                case .peak: v = max(v, power[b])
                case .energy: v += power[b]
                }
            }
            // Summing Hann-windowed bins over-counts energy by the window's
            // equivalent noise bandwidth (1.5 bins); undo that.
            if mode == .energy { v /= 1.5 }
            return LevelMath.toDB(v.squareRoot())
        }
    }
}

/// Display ballistics for a spectrum: instant rise, steady fall, and a
/// peak-hold marker per point.
public struct SpectrumBallistics: Sendable {
    public var levels: [Float]
    public var peaks: [Float]
    private var peakAge: [Float]

    public var releaseDBPerSecond: Float = 30
    public var peakHoldSeconds: Float = 1.5
    public var peakReleaseDBPerSecond: Float = 15

    public init(count: Int) {
        levels = [Float](repeating: LevelMath.floorDB, count: count)
        peaks = levels
        peakAge = [Float](repeating: 0, count: count)
    }

    public mutating func update(with new: [Float], elapsed: Float) {
        if new.count != levels.count { self = SpectrumBallistics(count: new.count) }
        for i in new.indices {
            let fallen = levels[i] - releaseDBPerSecond * elapsed
            levels[i] = max(new[i], fallen)
            if levels[i] >= peaks[i] {
                peaks[i] = levels[i]
                peakAge[i] = 0
            } else {
                peakAge[i] += elapsed
                if peakAge[i] > peakHoldSeconds {
                    peaks[i] = max(levels[i], peaks[i] - peakReleaseDBPerSecond * elapsed)
                }
            }
        }
    }
}

/// Colours one spectrogram column: each row takes the colour of the loudest
/// source there, brightness from its level. Where the runner-up is within
/// `collisionDB`, the two colours are mixed so clashes stand out.
public enum SpectrogramPainter {
    public struct Layer {
        public var levels: [Float]   // dB per row, bottom row first
        public var color: SourcePalette.RGB
        public init(levels: [Float], color: SourcePalette.RGB) {
            self.levels = levels
            self.color = color
        }
    }

    /// Returns RGBA bytes for `rows` pixels, top row first.
    public static func column(layers: [Layer], rows: Int,
                              floorDB: Float = -90, ceilingDB: Float = -12,
                              collisionDB: Float = 4) -> [UInt8] {
        var out = [UInt8](repeating: 0, count: rows * 4)
        for row in 0..<rows {
            var best = -1, second = -1
            var bestDB = -Float.infinity, secondDB = -Float.infinity
            for (i, layer) in layers.enumerated() where row < layer.levels.count {
                let v = layer.levels[row]
                if v > bestDB {
                    second = best; secondDB = bestDB
                    best = i; bestDB = v
                } else if v > secondDB {
                    second = i; secondDB = v
                }
            }
            var r = 0.0, g = 0.0, b = 0.0
            if best >= 0 {
                let t = Double(min(max((bestDB - floorDB) / (ceilingDB - floorDB), 0), 1))
                let intensity = t * t // perceptual-ish: keep the noise floor dark
                var c = layers[best].color
                if second >= 0, bestDB - secondDB < collisionDB, secondDB > floorDB {
                    let o = layers[second].color
                    c = SourcePalette.RGB(r: (c.r + o.r) / 2 + 0.15, g: (c.g + o.g) / 2 + 0.15, b: (c.b + o.b) / 2 + 0.15)
                }
                r = c.r * intensity; g = c.g * intensity; b = c.b * intensity
            }
            let y = rows - 1 - row // flip: low frequencies at the bottom
            out[y * 4 + 0] = UInt8(min(255, r * 255))
            out[y * 4 + 1] = UInt8(min(255, g * 255))
            out[y * 4 + 2] = UInt8(min(255, b * 255))
            out[y * 4 + 3] = 255
        }
        return out
    }
}
