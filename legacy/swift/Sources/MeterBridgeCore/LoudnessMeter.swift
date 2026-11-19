import Foundation

/// Second-order IIR section (transposed direct form II), in Double.
struct Biquad {
    var b0: Double, b1: Double, b2: Double, a1: Double, a2: Double
    var z1: Double = 0, z2: Double = 0

    init(b0: Double, b1: Double, b2: Double, a1: Double, a2: Double) {
        self.b0 = b0; self.b1 = b1; self.b2 = b2; self.a1 = a1; self.a2 = a2
    }

    @inline(__always)
    mutating func process(_ x: Double) -> Double {
        let y = b0 * x + z1
        z1 = b1 * x - a1 * y + z2
        z2 = b2 * x - a2 * y
        return y
    }

    mutating func reset() { z1 = 0; z2 = 0 }

    /// ITU-R BS.1770 stage 1: high-shelf "head" filter, for any sample rate.
    static func kWeightingShelf(sampleRate fs: Double) -> Biquad {
        let f0 = 1681.974450955533
        let gainDB = 3.999843853973347
        let q = 0.7071752369554196
        let k = tan(Double.pi * f0 / fs)
        let vh = pow(10, gainDB / 20)
        let vb = pow(vh, 0.4996667741545416)
        let a0 = 1 + k / q + k * k
        return Biquad(
            b0: (vh + vb * k / q + k * k) / a0,
            b1: 2 * (k * k - vh) / a0,
            b2: (vh - vb * k / q + k * k) / a0,
            a1: 2 * (k * k - 1) / a0,
            a2: (1 - k / q + k * k) / a0)
    }

    /// ITU-R BS.1770 stage 2: RLB high-pass, for any sample rate.
    static func kWeightingHighPass(sampleRate fs: Double) -> Biquad {
        let f0 = 38.13547087602444
        let q = 0.5003270373238773
        let k = tan(Double.pi * f0 / fs)
        let a0 = 1 + k / q + k * k
        return Biquad(
            b0: 1, b1: -2, b2: 1,
            a1: 2 * (k * k - 1) / a0,
            a2: (1 - k / q + k * k) / a0)
    }
}

/// Histogram of loudness values in 0.1 LU steps, used for gated integrated
/// loudness and loudness range without storing every block.
struct LoudnessHistogram {
    static let minLU = -70.0
    static let maxLU = 10.0
    static let step = 0.1
    static let binCount = Int((maxLU - minLU) / step)

    var counts = [Int](repeating: 0, count: binCount)
    var energies = [Double](repeating: 0, count: binCount)
    var total = 0

    static func bin(forLoudness l: Double) -> Int? {
        guard l >= minLU else { return nil } // absolute gate (-70 LUFS)
        return min(binCount - 1, Int((l - minLU) / step))
    }

    static func loudness(ofBin i: Int) -> Double { minLU + (Double(i) + 0.5) * step }

    mutating func add(energy: Double) {
        guard let i = Self.bin(forLoudness: LoudnessMeter.lufs(fromEnergy: energy)) else { return }
        counts[i] += 1
        energies[i] += energy
        total += 1
    }

    mutating func reset() {
        for i in 0..<Self.binCount { counts[i] = 0; energies[i] = 0 }
        total = 0
    }

    /// Mean energy of blocks at or above `threshold` LUFS.
    func meanEnergy(above threshold: Double) -> (energy: Double, count: Int) {
        let start = Self.bin(forLoudness: threshold) ?? 0
        var sum = 0.0, n = 0
        for i in start..<Self.binCount where counts[i] > 0 {
            sum += energies[i]; n += counts[i]
        }
        return (n > 0 ? sum / Double(n) : 0, n)
    }

    /// Loudness at percentile `p` (0...1) of blocks at or above `threshold`.
    func percentile(_ p: Double, above threshold: Double) -> Double? {
        let start = Self.bin(forLoudness: threshold) ?? 0
        let n = (start..<Self.binCount).reduce(0) { $0 + counts[$1] }
        guard n > 0 else { return nil }
        let target = Int((Double(n - 1) * p).rounded())
        var seen = 0
        for i in start..<Self.binCount {
            seen += counts[i]
            if seen > target { return Self.loudness(ofBin: i) }
        }
        return Self.loudness(ofBin: Self.binCount - 1)
    }
}

/// EBU R128 / ITU-R BS.1770-4 loudness meter for one stereo source:
/// momentary (400 ms), short-term (3 s), gated integrated loudness,
/// loudness range (EBU Tech 3342) and true peak (4x oversampled).
public final class LoudnessMeter {
    public static let silence = -Double.infinity

    public let sampleRate: Double
    private var filtersL: (Biquad, Biquad)
    private var filtersR: (Biquad, Biquad)

    // 100 ms sub-blocks of summed channel energy.
    private let subBlockLength: Int
    private var subBlockSum = 0.0
    private var subBlockFill = 0
    private var recentSubBlocks = [Double](repeating: 0, count: 30) // 3 s
    private var recentIndex = 0
    private var subBlocksSeen = 0

    private var integratedHistogram = LoudnessHistogram()
    private var rangeHistogram = LoudnessHistogram()

    private var truePeakL: TruePeakDetector
    private var truePeakR: TruePeakDetector

    public private(set) var momentary: Double = silence
    public private(set) var shortTerm: Double = silence
    public private(set) var momentaryMax: Double = silence
    public private(set) var shortTermMax: Double = silence
    /// Highest true peak since reset, linear.
    public private(set) var truePeak: Float = 0
    /// True peak of the most recent `process` call, linear (for a live bar).
    public private(set) var recentTruePeak: Float = 0

    public init(sampleRate: Double) {
        self.sampleRate = sampleRate
        let filters = (Biquad.kWeightingShelf(sampleRate: sampleRate), Biquad.kWeightingHighPass(sampleRate: sampleRate))
        filtersL = filters
        filtersR = filters
        subBlockLength = max(1, Int((sampleRate / 10).rounded()))
        truePeakL = TruePeakDetector()
        truePeakR = TruePeakDetector()
    }

    public static func lufs(fromEnergy e: Double) -> Double {
        e > 0 ? -0.691 + 10 * log10(e) : silence
    }

    public func reset() {
        filtersL.0.reset(); filtersL.1.reset(); filtersR.0.reset(); filtersR.1.reset()
        subBlockSum = 0; subBlockFill = 0
        for i in recentSubBlocks.indices { recentSubBlocks[i] = 0 }
        recentIndex = 0; subBlocksSeen = 0
        integratedHistogram.reset(); rangeHistogram.reset()
        truePeakL.reset(); truePeakR.reset()
        momentary = Self.silence; shortTerm = Self.silence
        momentaryMax = Self.silence; shortTermMax = Self.silence
        truePeak = 0; recentTruePeak = 0
    }

    /// Feed `count` stereo samples.
    public func process(left: UnsafePointer<Float>, right: UnsafePointer<Float>, count: Int) {
        var tp: Float = 0
        for i in 0..<count {
            let l = Double(left[i]), r = Double(right[i])
            let kl = filtersL.1.process(filtersL.0.process(l))
            let kr = filtersR.1.process(filtersR.0.process(r))
            subBlockSum += kl * kl + kr * kr
            subBlockFill += 1
            tp = max(tp, truePeakL.process(left[i]), truePeakR.process(right[i]))
            if subBlockFill == subBlockLength { finishSubBlock() }
        }
        recentTruePeak = tp
        truePeak = max(truePeak, tp)
    }

    public func process(left: [Float], right: [Float]) {
        let n = min(left.count, right.count)
        left.withUnsafeBufferPointer { l in
            right.withUnsafeBufferPointer { r in
                process(left: l.baseAddress!, right: r.baseAddress!, count: n)
            }
        }
    }

    private func finishSubBlock() {
        recentSubBlocks[recentIndex] = subBlockSum / Double(subBlockLength)
        recentIndex = (recentIndex + 1) % recentSubBlocks.count
        subBlocksSeen += 1
        subBlockSum = 0
        subBlockFill = 0

        // Momentary: last 4 sub-blocks (400 ms, 75 % overlap gating blocks).
        if subBlocksSeen >= 4 {
            let e = meanOfLast(4)
            momentary = Self.lufs(fromEnergy: e)
            momentaryMax = max(momentaryMax, momentary)
            integratedHistogram.add(energy: e)
        }
        // Short-term: last 30 sub-blocks (3 s), updated at 10 Hz.
        if subBlocksSeen >= 30 {
            let e = meanOfLast(30)
            shortTerm = Self.lufs(fromEnergy: e)
            shortTermMax = max(shortTermMax, shortTerm)
            rangeHistogram.add(energy: e)
        } else if subBlocksSeen >= 4 {
            shortTerm = Self.lufs(fromEnergy: meanOfLast(subBlocksSeen))
        }
    }

    private func meanOfLast(_ n: Int) -> Double {
        var sum = 0.0
        let len = recentSubBlocks.count
        for k in 1...n { sum += recentSubBlocks[(recentIndex - k + len) % len] }
        return sum / Double(n)
    }

    /// Gated integrated loudness (BS.1770-4), or -inf if nothing above the gate.
    public var integrated: Double {
        let ungated = integratedHistogram.meanEnergy(above: LoudnessHistogram.minLU)
        guard ungated.count > 0 else { return Self.silence }
        let relativeGate = Self.lufs(fromEnergy: ungated.energy) - 10
        let gated = integratedHistogram.meanEnergy(above: relativeGate)
        return gated.count > 0 ? Self.lufs(fromEnergy: gated.energy) : Self.silence
    }

    /// Loudness range in LU (EBU Tech 3342), or 0 if not enough data.
    public var loudnessRange: Double {
        let ungated = rangeHistogram.meanEnergy(above: LoudnessHistogram.minLU)
        guard ungated.count > 0 else { return 0 }
        let relativeGate = Self.lufs(fromEnergy: ungated.energy) - 20
        guard let low = rangeHistogram.percentile(0.10, above: relativeGate),
              let high = rangeHistogram.percentile(0.95, above: relativeGate) else { return 0 }
        return max(0, high - low)
    }
}

/// 4x oversampling true-peak detector (BS.1770-4 Annex 2 style) using a
/// 48-tap windowed-sinc interpolator split into 4 polyphase branches.
struct TruePeakDetector {
    static let phases = 4
    static let tapsPerPhase = 12
    static let coefficients: [[Float]] = {
        let n = phases * tapsPerPhase
        let center = Double(n - 1) / 2
        var h = [Double](repeating: 0, count: n)
        for i in 0..<n {
            let t = (Double(i) - center) / Double(phases)
            let sinc = t == 0 ? 1 : sin(Double.pi * t) / (Double.pi * t)
            // Blackman window
            let w = 0.42 - 0.5 * cos(2 * Double.pi * Double(i) / Double(n - 1))
                + 0.08 * cos(4 * Double.pi * Double(i) / Double(n - 1))
            h[i] = sinc * w
        }
        // Each phase is normalised to unity DC gain.
        return (0..<phases).map { p in
            let taps = (0..<tapsPerPhase).map { h[$0 * phases + p] }
            let sum = taps.reduce(0, +)
            return taps.map { Float($0 / sum) }
        }
    }()

    private var history = [Float](repeating: 0, count: tapsPerPhase)
    private var position = 0

    mutating func reset() {
        for i in history.indices { history[i] = 0 }
        position = 0
    }

    /// Pushes one sample and returns the largest |value| among the 4
    /// interpolated points for it.
    mutating func process(_ x: Float) -> Float {
        history[position] = x
        var peak: Float = 0
        for phase in Self.coefficients {
            var acc: Float = 0
            var idx = position
            for k in 0..<Self.tapsPerPhase {
                acc += phase[k] * history[idx]
                idx = idx == 0 ? Self.tapsPerPhase - 1 : idx - 1
            }
            peak = max(peak, abs(acc))
        }
        position = (position + 1) % Self.tapsPerPhase
        return peak
    }
}
