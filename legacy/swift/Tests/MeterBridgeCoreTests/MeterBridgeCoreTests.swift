import XCTest
@testable import MeterBridgeCore

final class SourceConfigTests: XCTestCase {
    func testDefaultsAreFourConsecutivePairs() {
        let d = SourceConfig.defaults()
        XCTAssertEqual(d.count, 4)
        XCTAssertEqual(d.map(\.letter), ["A", "B", "C", "D"])
        XCTAssertEqual(d.map(\.channelLabel), ["1-2", "3-4", "5-6", "7-8"])
        XCTAssertEqual(d.filter(\.isEnabled).count, 1)
    }

    func testValidityDependsOnDeviceChannelCount() {
        let c = SourceConfig.defaults()[3] // inputs 7-8
        XCTAssertFalse(c.isValid(forChannelCount: 2))
        XCTAssertFalse(c.isValid(forChannelCount: 7))
        XCTAssertTrue(c.isValid(forChannelCount: 8))
        XCTAssertTrue(c.isValid(forChannelCount: 64))
    }

    func testPaletteWrapsAround() {
        XCTAssertEqual(SourcePalette.color(0), SourcePalette.color(4))
        XCTAssertEqual(SourcePalette.color(-1), SourcePalette.color(3))
    }
}

final class BridgeSetupTests: XCTestCase {
    let dante = AudioDeviceInfo(uid: "dvs", name: "Dante Virtual Soundcard", inputChannelCount: 64, nominalSampleRate: 48000)
    let mic = AudioDeviceInfo(uid: "mic", name: "MacBook Pro Microphone", inputChannelCount: 1, nominalSampleRate: 48000)
    let out = AudioDeviceInfo(uid: "spk", name: "Speakers", inputChannelCount: 0, nominalSampleRate: 48000)

    func testResolvePrefersSavedDevice() {
        let setup = BridgeSetup(deviceUID: "mic")
        XCTAssertEqual(setup.resolveDevice(from: [dante, mic])?.uid, "mic")
    }

    func testResolveFallsBackToDante() {
        let setup = BridgeSetup(deviceUID: "gone")
        XCTAssertEqual(setup.resolveDevice(from: [mic, dante])?.uid, "dvs")
    }

    func testResolveIgnoresOutputOnlyDevices() {
        XCTAssertNil(BridgeSetup().resolveDevice(from: [out]))
        XCTAssertEqual(BridgeSetup().resolveDevice(from: [out, mic])?.uid, "mic")
    }

    func testActiveSourcesFiltersDisabledAndMissingChannels() {
        var setup = BridgeSetup()
        for i in 0..<4 { setup.sources[i].isEnabled = true }
        XCTAssertEqual(setup.activeSources(channelCount: 4).map(\.id), [0, 1])
        setup.sources[1].isEnabled = false
        XCTAssertEqual(setup.activeSources(channelCount: 64).map(\.id), [0, 2, 3])
    }

    func testNormalizedRestoresMissingSources() {
        var custom = SourceConfig.defaults()[2]
        custom.name = "Nord"
        let broken = BridgeSetup(deviceUID: "dvs", sources: [custom])
        let fixed = broken.normalized()
        XCTAssertEqual(fixed.sources.map(\.id), [0, 1, 2, 3])
        XCTAssertEqual(fixed.sources[2].name, "Nord")
        XCTAssertEqual(fixed.deviceUID, "dvs")
    }

    func testStoreRoundTrip() {
        let defaults = UserDefaults(suiteName: "MeterBridgeTests-\(UUID().uuidString)")!
        let store = SetupStore(defaults: defaults)
        XCTAssertEqual(store.load(), BridgeSetup())

        var setup = BridgeSetup(deviceUID: "dvs")
        setup.sources[1].name = "Prophet"
        setup.sources[1].leftChannel = 10
        setup.sources[1].rightChannel = 11
        store.save(setup)
        XCTAssertEqual(store.load(), setup)
    }
}

final class LevelMathTests: XCTestCase {
    func testDecibels() {
        XCTAssertEqual(LevelMath.toDB(1), 0, accuracy: 0.0001)
        XCTAssertEqual(LevelMath.toDB(0.5), -6.0206, accuracy: 0.001)
        XCTAssertEqual(LevelMath.toDB(0), LevelMath.floorDB)
    }

    func testPeakAndRMSOfSine() {
        let n = 48_000
        let sine = (0..<n).map { Float(sin(2 * Double.pi * 1_000 * Double($0) / 48_000)) }
        XCTAssertEqual(LevelMath.peak(sine), 1, accuracy: 0.001)
        // A full-scale sine has RMS 1/sqrt(2), about -3.01 dBFS.
        XCTAssertEqual(LevelMath.toDB(LevelMath.rms(sine)), -3.0103, accuracy: 0.01)
    }

    func testBallisticsRiseInstantlyAndFallSlowly() {
        let quiet = StereoLevel.silent
        let loud = StereoLevel(peakL: -6, peakR: -6, rmsL: -12, rmsR: -12)
        let up = quiet.smoothed(toward: loud, elapsed: 1.0 / 30)
        XCTAssertEqual(up, loud)

        let down = loud.smoothed(toward: .silent, elapsed: 0.5, releaseDBPerSecond: 24)
        XCTAssertEqual(down.peakL, -18, accuracy: 0.001)
        XCTAssertEqual(down.rmsL, -24, accuracy: 0.001)
    }
}
