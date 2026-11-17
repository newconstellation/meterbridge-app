import Foundation

/// Basic level measurements used by the step-1 meters. Spectrum and
/// loudness (LUFS) come in later steps.
public enum LevelMath {
    /// Floor used for silence so meters never show -infinity.
    public static let floorDB: Float = -120

    public static func toDB(_ linear: Float) -> Float {
        guard linear > 0 else { return floorDB }
        return max(floorDB, 20 * log10(linear))
    }

    /// Largest absolute sample value.
    public static func peak(_ samples: UnsafeBufferPointer<Float>) -> Float {
        var p: Float = 0
        for s in samples { p = max(p, abs(s)) }
        return p
    }

    /// Root-mean-square of the samples.
    public static func rms(_ samples: UnsafeBufferPointer<Float>) -> Float {
        guard !samples.isEmpty else { return 0 }
        var sum: Float = 0
        for s in samples { sum += s * s }
        return (sum / Float(samples.count)).squareRoot()
    }

    public static func peak(_ samples: [Float]) -> Float {
        samples.withUnsafeBufferPointer { peak($0) }
    }

    public static func rms(_ samples: [Float]) -> Float {
        samples.withUnsafeBufferPointer { rms($0) }
    }
}

/// Stereo level reading for one source, in dBFS.
public struct StereoLevel: Hashable, Sendable {
    public var peakL: Float
    public var peakR: Float
    public var rmsL: Float
    public var rmsR: Float

    public static let silent = StereoLevel(
        peakL: LevelMath.floorDB, peakR: LevelMath.floorDB,
        rmsL: LevelMath.floorDB, rmsR: LevelMath.floorDB
    )

    public init(peakL: Float, peakR: Float, rmsL: Float, rmsR: Float) {
        self.peakL = peakL
        self.peakR = peakR
        self.rmsL = rmsL
        self.rmsR = rmsR
    }

    /// Meter ballistics: rise instantly, fall at `releaseDBPerSecond`.
    public func smoothed(toward new: StereoLevel, elapsed: Float, releaseDBPerSecond: Float = 24) -> StereoLevel {
        func step(_ old: Float, _ target: Float) -> Float {
            target >= old ? target : max(target, old - releaseDBPerSecond * elapsed)
        }
        return StereoLevel(
            peakL: step(peakL, new.peakL), peakR: step(peakR, new.peakR),
            rmsL: step(rmsL, new.rmsL), rmsR: step(rmsR, new.rmsR)
        )
    }
}
