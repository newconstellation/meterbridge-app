import XCTest
@testable import MeterBridgeCore

private func sine(freq: Double, amplitudeDB: Double, seconds: Double, sampleRate: Double = 48_000,
                  phase: Double = 0) -> [Float] {
    let a = pow(10, amplitudeDB / 20)
    let n = Int(seconds * sampleRate)
    return (0..<n).map { Float(a * sin(2 * .pi * freq * Double($0) / sampleRate + phase)) }
}

final class SampleRingTests: XCTestCase {
    func testWriteThenReadBack() {
        let ring = SampleRing(capacity: 16)
        let input: [Float] = (0..<10).map(Float.init)
        input.withUnsafeBufferPointer { ring.write($0.baseAddress!, count: 10) }
        var out = [Float](repeating: -1, count: 4)
        XCTAssertTrue(out.withUnsafeMutableBufferPointer { ring.read(from: 3, count: 4, into: $0.baseAddress!) })
        XCTAssertEqual(out, [3, 4, 5, 6])
        XCTAssertTrue(out.withUnsafeMutableBufferPointer { ring.readLatest(count: 4, into: $0.baseAddress!) })
        XCTAssertEqual(out, [6, 7, 8, 9])
    }

    func testRejectsUnwrittenAndOverwrittenSamples() {
        let ring = SampleRing(capacity: 8)
        let input: [Float] = (0..<20).map(Float.init)
        input.withUnsafeBufferPointer { ring.write($0.baseAddress!, count: 20) }
        var out = [Float](repeating: 0, count: 4)
        XCTAssertFalse(out.withUnsafeMutableBufferPointer { ring.read(from: 18, count: 4, into: $0.baseAddress!) })
        XCTAssertFalse(out.withUnsafeMutableBufferPointer { ring.read(from: 2, count: 4, into: $0.baseAddress!) })
        XCTAssertTrue(out.withUnsafeMutableBufferPointer { ring.read(from: 14, count: 4, into: $0.baseAddress!) })
        XCTAssertEqual(out, [14, 15, 16, 17])
    }
}

final class LoudnessMeterTests: XCTestCase {
    /// EBU Tech 3341 test 1: stereo 1 kHz sine at -23 dBFS reads -23 LUFS.
    func testSineAtMinus23ReadsMinus23LUFS() {
        let s = sine(freq: 1000, amplitudeDB: -23, seconds: 20)
        let meter = LoudnessMeter(sampleRate: 48_000)
        meter.process(left: s, right: s)
        XCTAssertEqual(meter.momentary, -23, accuracy: 0.1)
        XCTAssertEqual(meter.shortTerm, -23, accuracy: 0.1)
        XCTAssertEqual(meter.integrated, -23, accuracy: 0.15)
    }

    func testWorksAt44100() {
        let s = sine(freq: 1000, amplitudeDB: -23, seconds: 10, sampleRate: 44_100)
        let meter = LoudnessMeter(sampleRate: 44_100)
        meter.process(left: s, right: s)
        XCTAssertEqual(meter.integrated, -23, accuracy: 0.15)
    }

    func testSilenceIsGatedOutOfIntegrated() {
        let tone = sine(freq: 1000, amplitudeDB: -20, seconds: 10)
        let silence = [Float](repeating: 0, count: 480_000)
        let meter = LoudnessMeter(sampleRate: 48_000)
        meter.process(left: tone, right: tone)
        meter.process(left: silence, right: silence)
        XCTAssertEqual(meter.integrated, -20, accuracy: 0.15)
        XCTAssertLessThan(meter.momentary, -70) // silence reads as silence
    }

    /// EBU Tech 3342 test 1: 20 s at -20 then 20 s at -30 → LRA about 10 LU.
    func testLoudnessRange() {
        let loud = sine(freq: 1000, amplitudeDB: -20, seconds: 20)
        let quiet = sine(freq: 1000, amplitudeDB: -30, seconds: 20)
        let meter = LoudnessMeter(sampleRate: 48_000)
        meter.process(left: loud, right: loud)
        meter.process(left: quiet, right: quiet)
        XCTAssertEqual(meter.loudnessRange, 10, accuracy: 1)
    }

    func testResetClearsEverything() {
        let s = sine(freq: 1000, amplitudeDB: -20, seconds: 5)
        let meter = LoudnessMeter(sampleRate: 48_000)
        meter.process(left: s, right: s)
        meter.reset()
        XCTAssertFalse(meter.integrated.isFinite)
        XCTAssertEqual(meter.truePeak, 0)
    }

    /// A sine at fs/4 sampled 45° off its peaks: sample peak ~0.707,
    /// but the true (inter-sample) peak is ~1.0.
    func testTruePeakFindsInterSamplePeaks() {
        let s = sine(freq: 12_000, amplitudeDB: 0, seconds: 0.5, phase: .pi / 4)
        XCTAssertEqual(LevelMath.peak(s), 0.7071, accuracy: 0.01)
        let meter = LoudnessMeter(sampleRate: 48_000)
        meter.process(left: s, right: s)
        XCTAssertGreaterThan(meter.truePeak, 0.95)
        XCTAssertLessThan(meter.truePeak, 1.1)
    }
}

final class SpectrumTests: XCTestCase {
    func testBinCentredSineReadsItsLevel() {
        let size = 8192
        let rate = 48_000.0
        let bin = 100
        let freq = Double(bin) * rate / Double(size)
        let s = sine(freq: freq, amplitudeDB: -6.0206, seconds: Double(size) / rate)
        let analyzer = SpectrumAnalyzer(size: size, sampleRate: rate)
        let power = analyzer.power(of: Array(s.prefix(size)))
        let loudest = power.indices.max { power[$0] < power[$1] }!
        XCTAssertEqual(loudest, bin)
        XCTAssertEqual(LevelMath.toDB(power[bin].squareRoot()), -6.02, accuracy: 0.5)
        // Far from the tone the spectrum is near silent.
        XCTAssertLessThan(LevelMath.toDB(power[bin * 10].squareRoot()), -80)
    }

    func testThirdOctaveBandsPutToneInTheRightBand() {
        let size = 8192
        let rate = 48_000.0
        let s = sine(freq: 1000, amplitudeDB: -12, seconds: 1)
        let analyzer = SpectrumAnalyzer(size: size, sampleRate: rate)
        let map = FrequencyMap.thirdOctave(fftSize: size, sampleRate: rate)
        XCTAssertEqual(map.count, 30)
        let levels = map.levels(fromPower: analyzer.power(of: Array(s.prefix(size))))
        let loudest = levels.indices.max { levels[$0] < levels[$1] }!
        XCTAssertEqual(map.frequencies[loudest], 1000)
        XCTAssertEqual(levels[loudest], -12, accuracy: 1.5)
    }

    func testLogCurveSpansTheAudioRange() {
        let map = FrequencyMap.logCurve(points: 240, fftSize: 8192, sampleRate: 48_000)
        XCTAssertEqual(map.count, 240)
        XCTAssertEqual(map.frequencies.first!, 20, accuracy: 0.01)
        XCTAssertEqual(map.frequencies.last!, 20_000, accuracy: 1)
    }

    func testBallisticsHoldPeaksThenFall() {
        var b = SpectrumBallistics(count: 1)
        b.update(with: [-10], elapsed: 0.033)
        XCTAssertEqual(b.levels[0], -10)
        b.update(with: [-100], elapsed: 0.5)
        XCTAssertEqual(b.levels[0], -25, accuracy: 0.01) // 30 dB/s release
        XCTAssertEqual(b.peaks[0], -10)                    // still held
        b.update(with: [-100], elapsed: 1.5)               // hold expired
        XCTAssertLessThan(b.peaks[0], -10)
    }

    func testSpectrogramColoursRowsByLoudestSource() {
        let blue = SourcePalette.color(0), orange = SourcePalette.color(1)
        let layers = [
            SpectrogramPainter.Layer(levels: [-12, -80], color: blue),
            SpectrogramPainter.Layer(levels: [-80, -12], color: orange),
        ]
        let px = SpectrogramPainter.column(layers: layers, rows: 2)
        // Row 0 (bottom) is drawn last (y = 1); row 1 (top) first (y = 0).
        let top = Array(px[0..<4]), bottom = Array(px[4..<8])
        XCTAssertGreaterThan(bottom[2], bottom[0]) // blue dominates the low row
        XCTAssertGreaterThan(top[0], top[2])       // orange dominates the high row
        XCTAssertEqual(top[3], 255)
    }
}
