// Renders a Meter Bridge window to a PNG with four synthetic keyboards, for
// design reviews and CI screenshots. No audio device needed.
//
//   MeterBridgeSnapshot <view> <out.png> [seconds] [overlayStyle] [width] [height]
//   view: quad | strip4 | panel | arc | overlay | settings
//   overlayStyle: grey3d | split | quietBlue

#include "AppSettings.h"
#include "AudioEngine.h"
#include "DisplayWindow.h"
#include "MeterHub.h"
#include "SettingsWindow.h"

#include <cmath>

namespace
{
    /// A keyboard voice: a note with harmonics that changes every so often.
    struct Keyboard
    {
        std::vector<float> roots;
        float noteSeconds;
        std::vector<float> harmonics;   // amplitudes
        float gain;
        double phase[16] {};

        float next (double time, double sampleRate)
        {
            const int note = (int) (time / noteSeconds) % (int) roots.size();
            const double sinceNote = std::fmod (time, (double) noteSeconds);
            const float env = (float) std::exp (-sinceNote * 1.2) * 0.8f + 0.2f;
            float out = 0.0f;
            for (size_t h = 0; h < harmonics.size(); ++h)
            {
                phase[h] += juce::MathConstants<double>::twoPi * roots[(size_t) note] * (double) (h + 1) / sampleRate;
                out += harmonics[h] * (float) std::sin (phase[h]);
            }
            return out * env * gain;
        }
    };
}

int main (int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI gui;

    const juce::String view = argc > 1 ? argv[1] : "overlay";
    const juce::File out = juce::File::getCurrentWorkingDirectory().getChildFile (argc > 2 ? argv[2] : "snapshot.png");
    const double seconds = argc > 3 ? juce::String (argv[3]).getDoubleValue() : 6.0;
    const juce::String style = argc > 4 ? argv[4] : "grey3d";
    const int width = argc > 5 ? juce::String (argv[5]).getIntValue() : 1920;
    const int height = argc > 6 ? juce::String (argv[6]).getIntValue() : 720;

    AppSettings settings;
    AudioEngine engine (settings);
    engine.getDeviceManager().closeAudioDevice();

    const char* names[] { "Keys 1 · Nord", "Keys 2 · Prophet", "Keys 3 · Rhodes", "Keys 4 · Juno", "Vocal mic" };
    std::vector<SourceDef> library;
    for (int s = 0; s < 5; ++s)
        library.push_back ({ "src" + juce::String (s), juce::String::fromUTF8 (names[s]), Sources::paletteColour (s),
                             s < 4, s * 2, s * 2 + 1 });
    engine.setLibrary (library);
    for (int s = 0; s < Sources::count; ++s)
        engine.setSlot (s, library[(size_t) s].id);
    engine.prepareOffline (48000.0, 8);

    MeterHub hub (engine);

    juce::ValueTree state ("Window");
    state.setProperty ("view", view, nullptr);
    state.setProperty ("overlayStyle", style, nullptr);
    DisplayContent content (hub, state);
    content.setSize (width, height);

    std::array<Keyboard, Sources::count> keys {
        Keyboard { { 55.0f, 73.4f, 65.4f, 49.0f }, 1.6f, { 1.0f, 0.5f, 0.3f, 0.2f, 0.1f }, 0.35f },
        Keyboard { { 220.0f, 196.0f, 261.6f, 246.9f }, 1.1f, { 0.6f, 0.4f, 0.35f, 0.3f, 0.25f, 0.2f, 0.15f, 0.12f, 0.1f }, 0.18f },
        Keyboard { { 440.0f, 392.0f, 523.3f, 349.2f }, 0.9f, { 0.8f, 0.2f, 0.12f, 0.05f }, 0.16f },
        Keyboard { { 130.8f, 164.8f, 146.8f, 110.0f }, 1.3f, { 0.7f, 0.5f, 0.4f, 0.3f, 0.2f, 0.15f, 0.1f }, 0.2f },
    };

    constexpr double rate = 48000.0;
    constexpr int block = 1600;   // 30 blocks a second, one UI tick each
    juce::AudioBuffer<float> buffer (8, block);
    juce::Random noise (1);
    double time = 0.0;

    for (int b = 0; b < (int) (seconds * rate / block); ++b)
    {
        for (int s = 0; s < Sources::count; ++s)
        {
            double t = time;
            for (int i = 0; i < block; ++i, t += 1.0 / rate)
            {
                const float v = keys[(size_t) s].next (t, rate) + (noise.nextFloat() - 0.5f) * 0.002f;
                buffer.setSample (s * 2, i, v);
                buffer.setSample (s * 2 + 1, i, v * 0.9f);
            }
        }
        engine.processInputs (buffer.getArrayOfReadPointers(), 8, block);
        time += block / rate;
        hub.tick();
    }

    juce::Image image;
    if (view == "settings")
    {
        SettingsContent settingsView (engine, settings);
        image = settingsView.createComponentSnapshot (settingsView.getLocalBounds(), true, 1.5f);
    }
    else
    {
        image = content.createComponentSnapshot (content.getLocalBounds(), true, 1.5f);
    }
    juce::FileOutputStream stream (out);
    if (! stream.openedOk())
        return 1;
    stream.setPosition (0);
    stream.truncate();
    juce::PNGImageFormat().writeImageToStream (image, stream);
    std::cout << "Wrote " << out.getFullPathName() << std::endl;
    return 0;
}
