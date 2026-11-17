import MeterBridgeCore
import SwiftUI

struct ContentView: View {
    @EnvironmentObject private var model: AppModel

    var body: some View {
        VStack(spacing: 0) {
            DeviceBar()
            Divider()
            HStack(alignment: .top, spacing: 0) {
                SourceList()
                    .frame(width: 380)
                Divider()
                OverlayPlaceholder()
            }
        }
        .background(Color(nsColor: .windowBackgroundColor))
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

/// Stand-in for step 2's overlay spectrograph.
private struct OverlayPlaceholder: View {
    @EnvironmentObject private var model: AppModel

    var body: some View {
        VStack(spacing: 12) {
            Image(systemName: "waveform.path.ecg.rectangle")
                .font(.system(size: 44, weight: .light))
                .foregroundStyle(.tertiary)
            Text("Overlay spectrograph")
                .font(.headline)
            Text("Coming in step 2: all enabled sources drawn on one display, each in its own colour.")
                .font(.callout)
                .foregroundStyle(.secondary)
                .multilineTextAlignment(.center)
                .frame(maxWidth: 320)
            HStack(spacing: 14) {
                ForEach(model.setup.sources.filter(\.isEnabled)) { source in
                    Label(source.name, systemImage: "circle.fill")
                        .foregroundStyle(source.color)
                        .font(.caption)
                }
            }
        }
        .frame(maxWidth: .infinity, maxHeight: .infinity)
        .background(Color.black.opacity(0.35))
    }
}

extension SourceConfig {
    var color: Color {
        let c = SourcePalette.color(colorIndex)
        return Color(red: c.r, green: c.g, blue: c.b)
    }
}
