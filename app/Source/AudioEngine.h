#pragma once

#include "AppSettings.h"
#include "PluginProcessor.h"

#include <juce_audio_devices/juce_audio_devices.h>

#include <array>
#include <atomic>
#include <memory>
#include <vector>

/// Opens the chosen input device and runs one Prism meter processor for every
/// source in the library (up to maxMeters), so any view can show any source
/// at once, with its loudness history already running. The audio callback only copies each source's two channels into a
/// preallocated buffer and hands it to that source's processor: no locks, no
/// allocation, no I/O on the audio thread.
class AudioEngine final : private juce::AudioIODeviceCallback,
                          private juce::ChangeListener
{
public:
    explicit AudioEngine (AppSettings&);
    ~AudioEngine() override;

    juce::AudioDeviceManager& getDeviceManager() noexcept { return deviceManager; }
    /// Most sources the library can meter at once.
    static constexpr int maxMeters = 32;

    PrismMeterProcessor& getProcessor (int meter) noexcept { return *meters[(size_t) meter].processor; }
    bool isMeterInUse (int meter) const noexcept { return meterOwner[(size_t) meter].isNotEmpty(); }

    /// The meter measuring a library source, or -1.
    int meterFor (const juce::String& sourceId) const noexcept;

    /// Default overlay inputs (A-F) for new overlay windows: the last set picked in any overlay.
    SourceConfig getSource (int slot) const { return configs[(size_t) slot]; }
    /// Any library source, by id.
    SourceConfig getSourceById (const juce::String& sourceId) const;

    /// The source library. Changing it re-routes any slot showing an edited source.
    const std::vector<SourceDef>& getLibrary() const noexcept { return library; }
    void setLibrary (std::vector<SourceDef>);
    const SourceDef* findSource (const juce::String& id) const;

    /// Puts a library source (or nothing, with an empty id) in a view slot.
    void setSlot (int slot, const juce::String& sourceId);
    juce::String getSlotSourceId (int slot) const { return slotIds[(size_t) slot]; }

    /// Called on the message thread after the library or a slot changes.
    std::function<void()> onSourcesChanged;

    /// Left / right samples per meter for the overlay's spectra. Single
    /// producer (audio thread), single consumer (the UI hub).
    int readStereoSamples (int meter, float* left, float* right, int maxSamples) noexcept;

    /// Input devices of the current driver type (Core Audio on a Mac).
    juce::StringArray getInputDeviceNames() const;
    juce::String getCurrentInputDeviceName() const;
    /// Opens another input device; returns an error message, or empty on success.
    juce::String setInputDevice (const juce::String& name);
    juce::String setSampleRate (double rate);
    juce::String setBufferSize (int samples);

    /// Names of the open device's input channels ("1", "2" ... when it has none).
    juce::StringArray getInputChannelNames() const;
    juce::String getDeviceName() const;
    double getSampleRate() const noexcept { return sampleRate.load(); }

    /// Clears integrated loudness and peak holds on every source.
    void resetAll();

    /// Called on the message thread after the device or its channels change.
    std::function<void()> onDeviceChanged;

    void saveState();

    /// Runs the meters without a device (snapshot tool, tests): sets the rate
    /// and maps device channels 1:1, then processInputs() feeds audio.
    void prepareOffline (double sampleRate, int numInputs);
    void processInputs (const float* const* inputs, int numInputs, int numSamples) noexcept;

private:
    void audioDeviceIOCallbackWithContext (const float* const* inputs, int numInputs, float* const* outputs,
                                           int numOutputs, int numSamples,
                                           const juce::AudioIODeviceCallbackContext&) override;
    void audioDeviceAboutToStart (juce::AudioIODevice*) override;
    void audioDeviceStopped() override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;

    void openAllInputs();
    void applySlot (int slot);
    void assignMeters();

    static constexpr int maxDeviceChannels = 256;
    static constexpr int chunkSize = 2048;

    struct Meter
    {
        std::unique_ptr<PrismMeterProcessor> processor;
        std::atomic<bool> enabled { false };
        std::atomic<int> left { 0 }, right { 1 };
        juce::AudioBuffer<float> buffer { 2, chunkSize };
        juce::MidiBuffer midi;

        static constexpr int fifoSize = 1 << 15;
        juce::AbstractFifo fifo { fifoSize };
        std::vector<float> fifoLeft = std::vector<float> (fifoSize, 0.0f), fifoRight = std::vector<float> (fifoSize, 0.0f);
    };

    AppSettings& settings;
    juce::AudioDeviceManager deviceManager;
    std::array<Meter, maxMeters> meters;
    std::array<juce::String, maxMeters> meterOwner;   // source id per meter (message thread)
    std::array<SourceConfig, Sources::count> configs;
    std::vector<SourceDef> library;
    std::array<juce::String, Sources::count> slotIds;

    /// Device channel number -> index into the callback's input array, or -1.
    /// Written only while the device is stopped.
    std::array<int, maxDeviceChannels> activeIndex {};
    std::atomic<double> sampleRate { 48000.0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioEngine)
};
