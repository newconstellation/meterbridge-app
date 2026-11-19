import MeterBridgeCore
import SwiftUI

/// The right-hand screen: session clock and status, then loudness for the
/// selected source: momentary and true-peak bars, and integrated /
/// short-term / range readouts.
struct LoudnessPanel: View {
    @EnvironmentObject private var model: AppModel

    private var source: SourceConfig? {
        model.activeSources.first { $0.id == model.loudnessSourceID } ?? model.activeSources.first
    }

    private var reading: LoudnessReading {
        source.flatMap { model.loudness[$0.id] } ?? .empty
    }

    var body: some View {
        VStack(spacing: 8) {
            HStack(spacing: 8) {
                ScreenBox {
                    TimelineView(.periodic(from: .now, by: 0.1)) { context in
                        Text(Self.clock(context.date.timeIntervalSince(model.sessionStart)))
                            .font(Theme.digits(26))
                            .foregroundStyle(Theme.readout)
                            .shadow(color: Theme.accent.opacity(0.6), radius: 4)
                            .frame(maxWidth: .infinity)
                    }
                }
                ScreenBox {
                    Text(statusText)
                        .font(.system(size: 22, weight: .heavy, design: .rounded))
                        .foregroundStyle(model.hasSignal ? Theme.readout : Theme.caption)
                        .shadow(color: Theme.accent.opacity(model.hasSignal ? 0.6 : 0), radius: 4)
                        .frame(maxWidth: .infinity)
                }
            }

            sourcePicker

            ScreenBox {
                VStack(alignment: .leading, spacing: 6) {
                    LoudnessBar(label: "M", value: reading.momentary, range: -42...0,
                                target: model.targetLUFS, color: source?.color ?? Theme.accent,
                                valueText: reading.momentary.lufsText)
                    LoudnessScale(range: -42...0, target: model.targetLUFS)
                    LoudnessBar(label: "dBTP", value: Double(reading.truePeakNow), range: -42...0,
                                target: -1, color: Theme.accent,
                                valueText: reading.truePeakMax.dbText,
                                marker: Double(reading.truePeakMax),
                                warn: reading.truePeakMax > -1)
                }
            }

            HStack(spacing: 8) {
                ReadoutBox(title: "INTEGRATED", unit: "LUFS",
                           value: reading.integrated.lufsText,
                           corner: "TARGET \(Int(model.targetLUFS))",
                           warn: reading.integrated.isFinite && reading.integrated > model.targetLUFS + 1,
                           big: true)
                ReadoutBox(title: "SHORT", unit: "LUFS",
                           value: reading.shortTerm.lufsText,
                           corner: "MAX \(reading.shortTermMax.lufsText)")
                ReadoutBox(title: "RANGE", unit: "LU",
                           value: reading.range > 0 ? String(format: "%.1f", reading.range) : "--",
                           corner: "")
            }
        }
    }

    private var statusText: String {
        if !model.isCapturing { return "STOPPED" }
        return model.hasSignal ? "SIGNAL" : "NO SIGNAL"
    }

    private var sourcePicker: some View {
        HStack(spacing: 6) {
            Text("LOUDNESS FOR")
                .font(Theme.label)
                .foregroundStyle(Theme.caption)
            ForEach(model.activeSources) { s in
                Button {
                    model.loudnessSourceID = s.id
                } label: {
                    HStack(spacing: 4) {
                        Circle().fill(s.color).frame(width: 7, height: 7)
                        Text(s.name).lineLimit(1)
                    }
                    .font(.system(size: 11, weight: .semibold))
                    .padding(.horizontal, 8).padding(.vertical, 3)
                    .background(Capsule().fill(s.id == source?.id ? s.color.opacity(0.3) : Color.white.opacity(0.06)))
                    .overlay(Capsule().stroke(s.id == source?.id ? s.color : .clear))
                }
                .buttonStyle(.plain)
            }
            Spacer()
            Button {
                model.resetSession()
            } label: {
                Label("Reset", systemImage: "arrow.counterclockwise")
                    .font(.system(size: 11, weight: .semibold))
            }
            .buttonStyle(.plain)
            .foregroundStyle(Theme.caption)
            .help("Reset integrated loudness, range, true-peak max and the clock")
        }
    }

    static func clock(_ seconds: TimeInterval) -> String {
        let t = max(0, seconds)
        let h = Int(t) / 3600, m = Int(t) / 60 % 60, s = Int(t) % 60
        let tenths = Int((t - floor(t)) * 10)
        return String(format: "%02d:%02d:%02d.%d", h, m, s, tenths)
    }
}

/// Horizontal bar with a label on the left and a value on the right.
struct LoudnessBar: View {
    let label: String
    let value: Double
    let range: ClosedRange<Double>
    let target: Double
    let color: Color
    let valueText: String
    var marker: Double? = nil
    var warn = false

    var body: some View {
        HStack(spacing: 6) {
            Text(label)
                .font(Theme.label)
                .foregroundStyle(Theme.caption)
                .frame(width: 30, alignment: .trailing)
            GeometryReader { geo in
                let w = geo.size.width
                ZStack(alignment: .leading) {
                    // Segmented background like an LED ladder
                    HStack(spacing: 2) {
                        ForEach(0..<40, id: \.self) { _ in
                            Rectangle().fill(Color.white.opacity(0.06))
                        }
                    }
                    Rectangle()
                        .fill(LinearGradient(colors: [color.opacity(0.7), color], startPoint: .leading, endPoint: .trailing))
                        .frame(width: w * fraction(value))
                        .mask(HStack(spacing: 2) {
                            ForEach(0..<40, id: \.self) { _ in Rectangle() }
                        })
                    Rectangle()
                        .fill(Color.white.opacity(0.8))
                        .frame(width: 1.5)
                        .offset(x: w * fraction(target))
                    if let marker {
                        Rectangle()
                            .fill(warn ? Theme.warn : Color.white)
                            .frame(width: 2)
                            .offset(x: max(0, w * fraction(marker) - 2))
                    }
                }
            }
            .frame(height: 14)
            Text(valueText)
                .font(Theme.digits(12))
                .foregroundStyle(warn ? Theme.warn : Theme.readout)
                .frame(width: 44, alignment: .trailing)
        }
    }

    private func fraction(_ v: Double) -> CGFloat {
        guard v.isFinite else { return 0 }
        return CGFloat(min(max((v - range.lowerBound) / (range.upperBound - range.lowerBound), 0), 1))
    }
}

/// Tick labels under the momentary bar.
struct LoudnessScale: View {
    let range: ClosedRange<Double>
    let target: Double

    var body: some View {
        HStack(spacing: 6) {
            Color.clear.frame(width: 30)
            GeometryReader { geo in
                ForEach(Array(stride(from: range.lowerBound, through: range.upperBound, by: 6)), id: \.self) { v in
                    Text(String(format: "%.0f", v))
                        .font(.system(size: 9).monospacedDigit())
                        .foregroundStyle(v == target ? Theme.readout : Theme.gridLabel)
                        .position(x: geo.size.width * CGFloat((v - range.lowerBound) / (range.upperBound - range.lowerBound)),
                                  y: 6)
                }
            }
            .frame(height: 12)
            Color.clear.frame(width: 44)
        }
    }
}

/// A boxed numeric readout: title top-right, unit bottom-right, small note
/// top-left, big number in the middle.
struct ReadoutBox: View {
    let title: String
    let unit: String
    let value: String
    let corner: String
    var warn = false
    var big = false

    var body: some View {
        ScreenBox {
            VStack(spacing: 2) {
                HStack {
                    Text(corner).font(.system(size: 9, weight: .medium)).foregroundStyle(Theme.caption)
                    Spacer()
                    Text(title).font(Theme.label).foregroundStyle(Theme.caption)
                }
                Text(value)
                    .font(Theme.digits(big ? 38 : 30, weight: .bold))
                    .foregroundStyle(warn ? Theme.warn : Theme.readout)
                    .shadow(color: (warn ? Theme.warn : Theme.accent).opacity(0.5), radius: 5)
                    .minimumScaleFactor(0.5)
                    .lineLimit(1)
                    .frame(maxWidth: .infinity, minHeight: 50)
                HStack {
                    Spacer()
                    Text(unit).font(Theme.label).foregroundStyle(Theme.caption)
                }
            }
        }
        .frame(maxWidth: big ? .infinity : 150)
    }
}
