import MeterBridgeCore
import SwiftUI

/// One source (A-D): on/off, colour, name, stereo pair, and an L/R meter.
struct SourceRow: View {
    @EnvironmentObject private var model: AppModel
    let source: SourceConfig
    let level: StereoLevel?

    private var pairIsValid: Bool {
        source.isValid(forChannelCount: model.currentDevice?.inputChannelCount ?? 0)
    }

    var body: some View {
        VStack(alignment: .leading, spacing: 8) {
            HStack(spacing: 8) {
                Toggle("", isOn: edit(\.isEnabled))
                    .toggleStyle(.switch)
                    .labelsHidden()
                    .controlSize(.small)

                Text(source.letter)
                    .font(.system(size: 13, weight: .bold, design: .monospaced))
                    .frame(width: 22, height: 22)
                    .background(RoundedRectangle(cornerRadius: 5).fill(source.color))
                    .foregroundStyle(.black)

                TextField("Name", text: edit(\.name))
                    .textFieldStyle(.roundedBorder)

                colorMenu
                pairMenu
            }

            StereoMeter(level: source.isEnabled ? level : nil, color: source.color)
                .opacity(source.isEnabled ? 1 : 0.35)

            if source.isEnabled && !pairIsValid {
                Text("Inputs \(source.channelLabel) aren't on this device.")
                    .font(.caption)
                    .foregroundStyle(.orange)
            }
        }
        .padding(10)
        .background(RoundedRectangle(cornerRadius: 8).fill(Color.white.opacity(0.04)))
        .overlay(RoundedRectangle(cornerRadius: 8).stroke(source.color.opacity(source.isEnabled ? 0.5 : 0.1)))
    }

    private var colorMenu: some View {
        Menu {
            ForEach(SourcePalette.colors.indices, id: \.self) { i in
                Button {
                    var s = source; s.colorIndex = i; model.update(source: s)
                } label: {
                    Text(["Blue", "Orange", "Green", "Magenta"][i % 4])
                }
            }
        } label: {
            Image(systemName: "paintpalette")
        }
        .menuStyle(.borderlessButton)
        .frame(width: 28)
        .help("Colour")
    }

    private var pairMenu: some View {
        Picker("", selection: Binding(
            get: { source.leftChannel / 2 },
            set: { pair in
                var s = source
                s.leftChannel = pair * 2
                s.rightChannel = pair * 2 + 1
                model.update(source: s)
            }
        )) {
            ForEach(pairChoices, id: \.self) { pair in
                Text("In \(pair * 2 + 1)-\(pair * 2 + 2)").tag(pair)
            }
        }
        .labelsHidden()
        .frame(width: 92)
        .help("Stereo input pair")
    }

    /// The device's pairs, plus the saved one even if the device lacks it,
    /// so the picker never loses its selection.
    private var pairChoices: [Int] {
        var pairs = model.stereoPairs
        let current = source.leftChannel / 2
        if !pairs.contains(current) { pairs.append(current) }
        return pairs.sorted()
    }

    private func edit<T>(_ keyPath: WritableKeyPath<SourceConfig, T>) -> Binding<T> {
        Binding(
            get: { source[keyPath: keyPath] },
            set: { value in
                var s = source
                s[keyPath: keyPath] = value
                model.update(source: s)
            }
        )
    }
}

/// Two horizontal bars (L, R): solid = RMS, thin bright tick = peak.
struct StereoMeter: View {
    let level: StereoLevel?
    let color: Color

    static let minDB: Float = -60

    var body: some View {
        VStack(spacing: 3) {
            bar(label: "L", rms: level?.rmsL, peak: level?.peakL)
            bar(label: "R", rms: level?.rmsR, peak: level?.peakR)
        }
    }

    private func bar(label: String, rms: Float?, peak: Float?) -> some View {
        HStack(spacing: 6) {
            Text(label)
                .font(.system(size: 9, weight: .semibold, design: .monospaced))
                .foregroundStyle(.secondary)
                .frame(width: 10)
            GeometryReader { geo in
                let w = geo.size.width
                ZStack(alignment: .leading) {
                    RoundedRectangle(cornerRadius: 2).fill(Color.white.opacity(0.07))
                    RoundedRectangle(cornerRadius: 2)
                        .fill(color.opacity(0.85))
                        .frame(width: w * fraction(rms))
                    Rectangle()
                        .fill(isClipping(peak) ? Color.red : Color.white)
                        .frame(width: 2)
                        .offset(x: max(0, w * fraction(peak) - 2))
                        .opacity(peak == nil ? 0 : 1)
                }
            }
            .frame(height: 7)
            Text(peak.map { $0 <= LevelMath.floorDB + 1 ? "-∞" : String(format: "%.1f", $0) } ?? "")
                .font(.system(size: 9, design: .monospaced))
                .foregroundStyle(isClipping(peak) ? Color.red : Color.secondary)
                .frame(width: 34, alignment: .trailing)
        }
    }

    private func fraction(_ db: Float?) -> CGFloat {
        guard let db else { return 0 }
        return CGFloat(min(max((db - Self.minDB) / -Self.minDB, 0), 1))
    }

    private func isClipping(_ db: Float?) -> Bool {
        (db ?? LevelMath.floorDB) >= -0.1
    }
}
