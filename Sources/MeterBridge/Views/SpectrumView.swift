import MeterBridgeCore
import SwiftUI

/// The left-hand screen: overlay curves, 1/3-octave bands, or the
/// "who's playing what" spectrogram, for all visible sources at once.
struct SpectrumScreen: View {
    @EnvironmentObject private var model: AppModel

    var body: some View {
        ScreenBox(padding: 6) {
            switch model.viewMode {
            case .curves:
                SpectrumCanvas(mode: .curves, sources: model.visibleSources, spectra: model.spectra)
            case .bars:
                SpectrumCanvas(mode: .bars, sources: model.visibleSources, spectra: model.spectra)
            case .spectrogram:
                SpectrogramView(spectrogram: model.spectrogram)
            }
        }
    }
}

/// Shared axis maths for the spectrum displays.
enum SpectrumAxis {
    static let minHz = 20.0
    static let maxHz = 20_000.0
    static let topDB: Float = 0
    static let bottomDB: Float = -96
    static let dbLines: [Float] = [0, -12, -24, -36, -48, -60, -72, -84, -96]
    static let hzLabels: [(Double, String)] = [
        (31.5, "31.5"), (63, "63"), (125, "125"), (250, "250"), (500, "500"),
        (1000, "1k"), (2000, "2k"), (4000, "4k"), (8000, "8k"), (16000, "16k"),
    ]

    static func x(_ hz: Double, in width: CGFloat) -> CGFloat {
        let t = (log10(hz) - log10(minHz)) / (log10(maxHz) - log10(minHz))
        return CGFloat(min(max(t, 0), 1)) * width
    }

    static func y(_ db: Float, in height: CGFloat) -> CGFloat {
        let t = (min(max(db, bottomDB), topDB) - topDB) / (bottomDB - topDB)
        return CGFloat(t) * height
    }
}

struct SpectrumCanvas: View {
    enum Mode { case curves, bars }
    let mode: Mode
    let sources: [SourceConfig]
    let spectra: [Int: SourceSpectrum]

    private let leftGutter: CGFloat = 30
    private let bottomGutter: CGFloat = 16

    var body: some View {
        Canvas { ctx, size in
            let plot = CGRect(x: leftGutter, y: 4, width: size.width - leftGutter - 4,
                              height: size.height - bottomGutter - 4)
            drawGrid(ctx, plot)
            switch mode {
            case .curves: drawCurves(ctx, plot)
            case .bars: drawBars(ctx, plot)
            }
        }
        .overlay(alignment: .topTrailing) {
            if sources.isEmpty {
                Text("No sources on. Open Inputs and switch on A to D.")
                    .font(.caption)
                    .foregroundStyle(Theme.caption)
                    .padding(10)
            }
        }
    }

    private func drawGrid(_ ctx: GraphicsContext, _ plot: CGRect) {
        for db in SpectrumAxis.dbLines {
            let y = plot.minY + SpectrumAxis.y(db, in: plot.height)
            var line = Path()
            line.move(to: CGPoint(x: plot.minX, y: y))
            line.addLine(to: CGPoint(x: plot.maxX, y: y))
            ctx.stroke(line, with: .color(Theme.grid), lineWidth: 1)
            ctx.draw(Text(String(format: "%.0f", db)).font(.system(size: 9).monospacedDigit())
                        .foregroundColor(Theme.gridLabel),
                     at: CGPoint(x: plot.minX - 4, y: y), anchor: .trailing)
        }
        for (hz, label) in SpectrumAxis.hzLabels {
            let x = plot.minX + SpectrumAxis.x(hz, in: plot.width)
            var line = Path()
            line.move(to: CGPoint(x: x, y: plot.minY))
            line.addLine(to: CGPoint(x: x, y: plot.maxY))
            ctx.stroke(line, with: .color(Theme.grid.opacity(0.5)), lineWidth: 1)
            ctx.draw(Text(label).font(.system(size: 9).monospacedDigit()).foregroundColor(Theme.gridLabel),
                     at: CGPoint(x: x, y: plot.maxY + 3), anchor: .top)
        }
    }

    private func drawCurves(_ ctx: GraphicsContext, _ plot: CGRect) {
        let freqs = curveFrequencies
        for source in sources {
            guard let spectrum = spectra[source.id], spectrum.curve.count == freqs.count else { continue }
            let color = source.color
            let points = zip(freqs, spectrum.curve).map { hz, db in
                CGPoint(x: plot.minX + SpectrumAxis.x(hz, in: plot.width),
                        y: plot.minY + SpectrumAxis.y(db, in: plot.height))
            }
            guard let first = points.first, let last = points.last else { continue }

            var line = Path()
            line.move(to: first)
            for p in points.dropFirst() { line.addLine(to: p) }

            var fill = line
            fill.addLine(to: CGPoint(x: last.x, y: plot.maxY))
            fill.addLine(to: CGPoint(x: first.x, y: plot.maxY))
            fill.closeSubpath()
            ctx.fill(fill, with: .linearGradient(
                Gradient(colors: [color.opacity(0.35), color.opacity(0.05)]),
                startPoint: CGPoint(x: 0, y: plot.minY), endPoint: CGPoint(x: 0, y: plot.maxY)))
            ctx.stroke(line, with: .color(color), lineWidth: 1.6)

            // Peak-hold trace
            var peaks = Path()
            for (i, (hz, db)) in zip(freqs, spectrum.curvePeaks).enumerated() {
                let p = CGPoint(x: plot.minX + SpectrumAxis.x(hz, in: plot.width),
                                y: plot.minY + SpectrumAxis.y(db, in: plot.height))
                if i == 0 { peaks.move(to: p) } else { peaks.addLine(to: p) }
            }
            ctx.stroke(peaks, with: .color(color.opacity(0.55)),
                       style: StrokeStyle(lineWidth: 1, dash: [2, 3]))
        }
    }

    private func drawBars(_ ctx: GraphicsContext, _ plot: CGRect) {
        let centres = FrequencyMap.thirdOctaveCentres
        let shown = sources.filter { spectra[$0.id] != nil }
        guard !shown.isEmpty else { return }
        let slot = plot.width / CGFloat(centres.count)
        let gap: CGFloat = max(1, slot * 0.18)
        let barWidth = (slot - gap) / CGFloat(shown.count)

        for (band, _) in centres.enumerated() {
            let slotX = plot.minX + CGFloat(band) * slot + gap / 2
            for (k, source) in shown.enumerated() {
                guard let spectrum = spectra[source.id], band < spectrum.bands.count else { continue }
                let x = slotX + CGFloat(k) * barWidth
                let top = plot.minY + SpectrumAxis.y(spectrum.bands[band], in: plot.height)
                let rect = CGRect(x: x, y: top, width: max(1, barWidth - 1), height: plot.maxY - top)
                ctx.fill(Path(rect), with: .linearGradient(
                    Gradient(colors: [source.color, source.color.opacity(0.45)]),
                    startPoint: CGPoint(x: 0, y: top), endPoint: CGPoint(x: 0, y: plot.maxY)))
                let peakY = plot.minY + SpectrumAxis.y(spectrum.bandPeaks[band], in: plot.height)
                ctx.fill(Path(CGRect(x: x, y: peakY - 1, width: max(1, barWidth - 1), height: 2)),
                         with: .color(.white.opacity(0.85)))
            }
        }
        // Band labels under the bars (every other band to stay legible).
        for (band, hz) in centres.enumerated() where band % 3 == 0 {
            let x = plot.minX + (CGFloat(band) + 0.5) * slot
            let label = hz >= 1000 ? String(format: hz.truncatingRemainder(dividingBy: 1000) == 0 ? "%.0fk" : "%.1fk", hz / 1000)
                                   : String(format: hz < 100 && hz.truncatingRemainder(dividingBy: 1) != 0 ? "%.1f" : "%.0f", hz)
            ctx.draw(Text(label).font(.system(size: 9).monospacedDigit()).foregroundColor(Theme.accent.opacity(0.9)),
                     at: CGPoint(x: x, y: plot.maxY + 3), anchor: .top)
        }
    }

    /// Centre frequencies of the curve points (matches AnalysisEngine).
    private var curveFrequencies: [Double] {
        let n = AnalysisEngine.curvePoints
        let ratio = pow(SpectrumAxis.maxHz / SpectrumAxis.minHz, 1 / Double(n - 1))
        return (0..<n).map { SpectrumAxis.minHz * pow(ratio, Double($0)) }
    }
}

/// Scrolling spectrogram: time runs right to left, newest on the right.
struct SpectrogramView: View {
    @ObservedObject var spectrogram: SpectrogramModel

    var body: some View {
        GeometryReader { geo in
            ZStack(alignment: .topLeading) {
                if let image = spectrogram.image {
                    Image(decorative: image, scale: 1)
                        .resizable()
                        .interpolation(.medium)
                        .frame(width: geo.size.width, height: geo.size.height)
                } else {
                    Color.black
                }
                // Frequency guides
                ForEach(SpectrumAxis.hzLabels, id: \.0) { hz, label in
                    let y = geo.size.height * (1 - SpectrumAxis.x(hz, in: 1))
                    Rectangle()
                        .fill(Color.white.opacity(0.08))
                        .frame(width: geo.size.width, height: 1)
                        .offset(y: y)
                    Text(label)
                        .font(.system(size: 9).monospacedDigit())
                        .foregroundStyle(Theme.gridLabel)
                        .offset(x: 4, y: y - 11)
                }
            }
        }
        .clipShape(RoundedRectangle(cornerRadius: 4))
        .overlay(alignment: .bottomTrailing) {
            Text("Brightest colour = loudest source · white-ish = two sources clashing")
                .font(.system(size: 9))
                .foregroundStyle(Theme.caption)
                .padding(6)
        }
    }
}
