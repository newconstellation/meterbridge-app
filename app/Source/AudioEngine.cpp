#include "AudioEngine.h"

AudioEngine::AudioEngine (AppSettings& s) : settings (s)
{
    activeIndex.fill (-1);

    library = settings.loadLibrary();
    slotIds = settings.loadSlots();

    bool anySlot = false;
    for (const auto& id : slotIds)
        anySlot = anySlot || findSource (id) != nullptr;
    if (! anySlot && ! library.empty())
        slotIds[0] = library.front().id;   // first run: show the first source in slot A

    for (auto& m : meters)
        m.processor = std::make_unique<PrismMeterProcessor>();

    assignMeters();
    for (int i = 0; i < Sources::count; ++i)
        applySlot (i);

    // Inputs only: Meter Bridge never plays anything.
    const auto saved = settings.loadAudioDeviceState();
    const auto error = deviceManager.initialise (maxDeviceChannels, 0, saved.get(), true);
    if (error.isNotEmpty())
    {
        DBG ("Audio device: " << error);
    }

    openAllInputs();
    deviceManager.addChangeListener (this);
    deviceManager.addAudioCallback (this);
}

AudioEngine::~AudioEngine()
{
    deviceManager.removeAudioCallback (this);
    deviceManager.removeChangeListener (this);
    saveState();
}

void AudioEngine::saveState()
{
    settings.saveLibrary (library);
    settings.saveSlots (slotIds);
    if (auto xml = deviceManager.createStateXml())
        settings.saveAudioDeviceState (xml.get());
}

const SourceDef* AudioEngine::findSource (const juce::String& id) const
{
    if (id.isEmpty())
        return nullptr;
    for (const auto& d : library)
        if (d.id == id)
            return &d;
    return nullptr;
}

void AudioEngine::setLibrary (std::vector<SourceDef> newLibrary)
{
    library = std::move (newLibrary);
    assignMeters();
    for (int i = 0; i < Sources::count; ++i)
        applySlot (i);
    saveState();
    if (onSourcesChanged)
        onSourcesChanged();
}

void AudioEngine::setSlot (int slot, const juce::String& sourceId)
{
    if (slotIds[(size_t) slot] == sourceId)
        return;
    slotIds[(size_t) slot] = sourceId;
    applySlot (slot);
    saveState();
    if (onSourcesChanged)
        onSourcesChanged();
}

int AudioEngine::meterFor (const juce::String& sourceId) const noexcept
{
    if (sourceId.isEmpty())
        return -1;
    for (int m = 0; m < maxMeters; ++m)
        if (meterOwner[(size_t) m] == sourceId)
            return m;
    return -1;
}

/// Gives every library source its own meter. A source keeps its meter (and
/// its loudness history) while it stays in the library; a freed meter that is
/// given to a new source starts fresh. A mono source feeds its one channel to
/// both sides of the meter (dual mono, as if panned centre).
void AudioEngine::assignMeters()
{
    for (int m = 0; m < maxMeters; ++m)
        if (meterOwner[(size_t) m].isNotEmpty() && findSource (meterOwner[(size_t) m]) == nullptr)
        {
            meters[(size_t) m].enabled.store (false);
            meterOwner[(size_t) m] = {};
        }

    for (const auto& def : library)
    {
        int m = meterFor (def.id);
        if (m < 0)
        {
            for (int k = 0; k < maxMeters && m < 0; ++k)
                if (meterOwner[(size_t) k].isEmpty())
                    m = k;
            if (m < 0)
                continue;   // library is larger than maxMeters: this source is not metered
            meterOwner[(size_t) m] = def.id;
            meters[(size_t) m].processor->requestReset();
        }

        auto& meter = meters[(size_t) m];
        meter.left.store (juce::jlimit (0, maxDeviceChannels - 1, def.left));
        meter.right.store (juce::jlimit (0, maxDeviceChannels - 1, def.rightChannel()));
        meter.enabled.store (true);
    }
}

SourceConfig AudioEngine::getSourceById (const juce::String& sourceId) const
{
    SourceConfig config;
    if (const auto* def = findSource (sourceId))
    {
        config.enabled = true;
        config.sourceId = def->id;
        config.name = def->name;
        config.colour = def->colour;
        config.stereo = def->stereo;
        config.meter = meterFor (def->id);
    }
    return config;
}

void AudioEngine::applySlot (int slot)
{
    configs[(size_t) slot] = getSourceById (slotIds[(size_t) slot]);
}

int AudioEngine::readStereoSamples (int meter, float* left, float* right, int maxSamples) noexcept
{
    auto& route = meters[(size_t) meter];
    const auto scope = route.fifo.read (juce::jmin (maxSamples, route.fifo.getNumReady()));
    int done = 0;
    for (auto [start, size] : { std::pair { scope.startIndex1, scope.blockSize1 }, std::pair { scope.startIndex2, scope.blockSize2 } })
    {
        if (size <= 0)
            continue;
        std::copy_n (route.fifoLeft.data() + start, size, left + done);
        std::copy_n (route.fifoRight.data() + start, size, right + done);
        done += size;
    }
    return done;
}

juce::StringArray AudioEngine::getInputChannelNames() const
{
    if (auto* device = deviceManager.getCurrentAudioDevice())
        return device->getInputChannelNames();
    return {};
}

juce::StringArray AudioEngine::getInputDeviceNames() const
{
    if (auto* type = deviceManager.getCurrentDeviceTypeObject())
        return type->getDeviceNames (true);
    return {};
}

juce::String AudioEngine::getCurrentInputDeviceName() const
{
    return deviceManager.getAudioDeviceSetup().inputDeviceName;
}

juce::String AudioEngine::setInputDevice (const juce::String& name)
{
    auto setup = deviceManager.getAudioDeviceSetup();
    if (setup.inputDeviceName == name && deviceManager.getCurrentAudioDevice() != nullptr)
        return {};
    setup.inputDeviceName = name;
    setup.outputDeviceName = {};
    setup.useDefaultInputChannels = true;
    setup.outputChannels.clear();
    setup.useDefaultOutputChannels = false;
    const auto error = deviceManager.setAudioDeviceSetup (setup, true);
    openAllInputs();
    return error;
}

juce::String AudioEngine::setSampleRate (double rate)
{
    auto setup = deviceManager.getAudioDeviceSetup();
    setup.sampleRate = rate;
    return deviceManager.setAudioDeviceSetup (setup, true);
}

juce::String AudioEngine::setBufferSize (int samples)
{
    auto setup = deviceManager.getAudioDeviceSetup();
    setup.bufferSize = samples;
    return deviceManager.setAudioDeviceSetup (setup, true);
}

juce::String AudioEngine::getDeviceName() const
{
    if (auto* device = deviceManager.getCurrentAudioDevice())
        return device->getName();
    return "No input device";
}

void AudioEngine::resetAll()
{
    for (auto& m : meters)
        m.processor->requestReset();
}

/// Every source can pick any channel, so keep all of the device's inputs open.
void AudioEngine::openAllInputs()
{
    auto setup = deviceManager.getAudioDeviceSetup();
    auto* device = deviceManager.getCurrentAudioDevice();
    if (device == nullptr)
        return;

    const int numInputs = juce::jmin (maxDeviceChannels, device->getInputChannelNames().size());
    juce::BigInteger wanted;
    wanted.setRange (0, numInputs, true);

    if (setup.inputChannels != wanted || ! setup.outputChannels.isZero())
    {
        setup.useDefaultInputChannels = false;
        setup.inputChannels = wanted;
        setup.useDefaultOutputChannels = false;
        setup.outputChannels.clear();
        deviceManager.setAudioDeviceSetup (setup, true);
    }
}

void AudioEngine::changeListenerCallback (juce::ChangeBroadcaster*)
{
    openAllInputs();
    saveState();
    if (onDeviceChanged)
        onDeviceChanged();
}

void AudioEngine::audioDeviceAboutToStart (juce::AudioIODevice* device)
{
    activeIndex.fill (-1);
    const auto active = device->getActiveInputChannels();
    int index = 0;
    for (int ch = 0; ch < maxDeviceChannels; ++ch)
        if (active[ch])
            activeIndex[(size_t) ch] = index++;

    const double rate = device->getCurrentSampleRate() > 0 ? device->getCurrentSampleRate() : 48000.0;
    sampleRate.store (rate);

    for (auto& m : meters)
    {
        m.processor->setRateAndBufferSizeDetails (rate, chunkSize);
        m.processor->prepareToPlay (rate, chunkSize);
    }
}

void AudioEngine::audioDeviceStopped() {}

void AudioEngine::audioDeviceIOCallbackWithContext (const float* const* inputs, int numInputs, float* const* outputs,
                                                    int numOutputs, int numSamples,
                                                    const juce::AudioIODeviceCallbackContext&)
{
    for (int ch = 0; ch < numOutputs; ++ch)
        if (outputs[ch] != nullptr)
            juce::FloatVectorOperations::clear (outputs[ch], numSamples);

    processInputs (inputs, numInputs, numSamples);
}

void AudioEngine::prepareOffline (double rate, int numInputs)
{
    activeIndex.fill (-1);
    for (int ch = 0; ch < juce::jmin (numInputs, maxDeviceChannels); ++ch)
        activeIndex[(size_t) ch] = ch;
    sampleRate.store (rate);
    for (auto& m : meters)
    {
        m.processor->setRateAndBufferSizeDetails (rate, chunkSize);
        m.processor->prepareToPlay (rate, chunkSize);
    }
}

void AudioEngine::processInputs (const float* const* inputs, int numInputs, int numSamples) noexcept
{
    auto inputFor = [&] (int deviceChannel) -> const float*
    {
        const int i = activeIndex[(size_t) deviceChannel];
        return (i >= 0 && i < numInputs) ? inputs[i] : nullptr;
    };

    for (size_t s = 0; s < meters.size(); ++s)
    {
        auto& route = meters[s];
        if (! route.enabled.load (std::memory_order_relaxed))
            continue;

        const float* left = inputFor (route.left.load (std::memory_order_relaxed));
        const float* right = inputFor (route.right.load (std::memory_order_relaxed));

        for (int offset = 0; offset < numSamples; offset += chunkSize)
        {
            const int n = juce::jmin (chunkSize, numSamples - offset);
            route.buffer.setSize (2, n, false, false, true); // never reallocates: n <= chunkSize

            auto copy = [&] (int ch, const float* src)
            {
                if (src != nullptr)
                    route.buffer.copyFrom (ch, 0, src + offset, n);
                else
                    route.buffer.clear (ch, 0, n);
            };
            copy (0, left);
            copy (1, right);

            route.processor->processBlock (route.buffer, route.midi);

            // L / R for the overlay. If the UI falls behind, newest samples are dropped.
            const auto scope = route.fifo.write (juce::jmin (n, route.fifo.getFreeSpace()));
            int done = 0;
            for (auto [start, size] : { std::pair { scope.startIndex1, scope.blockSize1 }, std::pair { scope.startIndex2, scope.blockSize2 } })
            {
                if (size <= 0)
                    continue;
                juce::FloatVectorOperations::copy (route.fifoLeft.data() + start, route.buffer.getReadPointer (0, done), size);
                juce::FloatVectorOperations::copy (route.fifoRight.data() + start, route.buffer.getReadPointer (1, done), size);
                done += size;
            }
        }
    }
}
