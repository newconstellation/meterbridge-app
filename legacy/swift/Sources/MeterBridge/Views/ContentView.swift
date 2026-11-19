import MeterBridgeCore
import SwiftUI

struct ContentView: View {
    @EnvironmentObject private var model: AppModel

    var body: some View {
        VStack(spacing: 0) {
            DeviceBar()
            Divider()
            HStack(alignment: .top, spacing: 0) {
                if model.showInputs {
                    SourceList()
                        .frame(width: 380)
                        .transition(.move(edge: .leading))
                    Divider()
                }
                MeterStation()
            }
        }
        .background(Theme.chassis)
        .animation(.easeInOut(duration: 0.2), value: model.showInputs)
    }
}

/// Top bar: input device picker and status line.
private struct DeviceBar: View {
    @EnvironmentObject private var model: AppModel

    var body: some View {
        HStack(spacing: 12) {
            Text("METER BRIDGE")
                .font(.system(size: 13, weight: .bold, design: .monospaced))
                .tracking(2)

            Picker("Input", selection: Binding(
                get: { model.currentDevice?.uid ?? "" },
                set: { model.selectDevice(uid: $0) }
            )) {
                if model.devices.isEmpty { Text("No input devices").tag("") }
                ForEach(model.devices) { device in
                    Text("\(device.name)  (\(device.inputChannelCount) in)").tag(device.uid)
                }
            }
            .frame(maxWidth: 360)

            Button {
                model.refreshDevices()
                model.restartCapture()
            } label: {
                Image(systemName: "arrow.clockwise")
            }
            .help("Rescan devices")

            Toggle(isOn: $model.showInputs) {
                Label("Inputs", systemImage: "slider.horizontal.3")
            }
            .toggleStyle(.button)
            .help("Show or hide input routing")

            Picker("View", selection: $model.viewMode) {
                ForEach(AppModel.ViewMode.allCases) { Text($0.rawValue).tag($0) }
            }
            .pickerStyle(.segmented)
            .labelsHidden()
            .frame(width: 280)

            Spacer()

            Circle()
                .fill(model.isCapturing ? Color.green : Color.orange)
                .frame(width: 8, height: 8)
            Text(model.statusMessage)
                .font(.caption)
                .foregroundStyle(.secondary)
                .lineLimit(1)
                .truncationMode(.middle)
        }
        .padding(.horizontal, 16)
        .padding(.vertical, 10)
        .padding(.top, 18) // room for the hidden title bar's traffic lights
    }
}

/// Sources A to D, each a stereo pair with a name, colour and meter.
private struct SourceList: View {
    @EnvironmentObject private var model: AppModel

    var body: some View {
        ScrollView {
            VStack(spacing: 10) {
                ForEach(model.setup.sources) { source in
                    SourceRow(source: source, level: model.levels[source.id])
                }
            }
            .padding(14)
        }
    }
}

/// The main "rack unit": spectrum screen on the left, loudness on the
/// right, and a strip of source chips with live meters underneath.
private struct MeterStation: View {
    @EnvironmentObject private var model: AppModel

    var body: some View {
        VStack(spacing: 10) {
            HStack(alignment: .top, spacing: 12) {
                SpectrumScreen()
                    .frame(minWidth: 380, maxWidth: .infinity, maxHeight: .infinity)
                LoudnessPanel()
                    .frame(width: 430)
            }
            SourceStrip()
        }
        .padding(16)
        .background(
            RoundedRectangle(cornerRadius: 10)
                .fill(LinearGradient(colors: [Color(white: 0.11), Color(white: 0.05)],
                                     startPoint: .top, endPoint: .bottom))
        )
        .overlay(alignment: .topLeading) { Screw().padding(5) }
        .overlay(alignment: .topTrailing) { Screw().padding(5) }
        .overlay(alignment: .bottomLeading) { Screw().padding(5) }
        .overlay(alignment: .bottomTrailing) { Screw().padding(5) }
        .padding(12)
    }
}

/// One chip per enabled source: colour, name, live L/R meter, hide and solo.
private struct SourceStrip: View {
    @EnvironmentObject private var model: AppModel

    var body: some View {
        HStack(spacing: 10) {
            if model.activeSources.isEmpty {
                Text("Switch on a source in Inputs to start metering.")
                    .font(.caption)
                    .foregroundStyle(Theme.caption)
            }
            ForEach(model.activeSources) { source in
                let hidden = model.soloSource.map { $0 != source.id } ?? model.hiddenSources.contains(source.id)
                ScreenBox(padding: 6) {
                    VStack(alignment: .leading, spacing: 4) {
                        HStack(spacing: 6) {
                            Text(source.letter)
                                .font(.system(size: 11, weight: .bold, design: .monospaced))
                                .frame(width: 18, height: 18)
                                .background(RoundedRectangle(cornerRadius: 4).fill(source.color))
                                .foregroundStyle(.black)
                            Text(source.name)
                                .font(.system(size: 12, weight: .semibold))
                                .foregroundStyle(Theme.readout)
                                .lineLimit(1)
                            Spacer(minLength: 4)
                            Button {
                                model.toggleHidden(source.id)
                            } label: {
                                Image(systemName: model.hiddenSources.contains(source.id) ? "eye.slash" : "eye")
                            }
                            .buttonStyle(.plain)
                            .foregroundStyle(Theme.caption)
                            .help("Hide from the spectrum")
                            Button {
                                model.toggleSolo(source.id)
                            } label: {
                                Text("S")
                                    .font(.system(size: 10, weight: .heavy))
                                    .frame(width: 16, height: 16)
                                    .background(RoundedRectangle(cornerRadius: 3)
                                        .fill(model.soloSource == source.id ? Color.yellow : Color.white.opacity(0.1)))
                                    .foregroundStyle(model.soloSource == source.id ? Color.black : Theme.caption)
                            }
                            .buttonStyle(.plain)
                            .help("Solo on the spectrum")
                        }
                        StereoMeter(level: model.levels[source.id], color: source.color)
                    }
                }
                .opacity(hidden ? 0.45 : 1)
                .frame(maxWidth: 260)
            }
            Spacer(minLength: 0)
        }
    }
}

extension SourceConfig {
    var color: Color {
        let c = SourcePalette.color(colorIndex)
        return Color(red: c.r, green: c.g, blue: c.b)
    }
}
