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

    for (int i = 0; i < Sources::count; ++i)
    {
        processors[(size_t) i] = std::make_unique<PrismMeterProcessor>();
        applySlot (i);
    }

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

/// Points a slot's route at its source's channels. A new source in the slot
/// starts its loudness readings fresh. A mono source feeds the same channel
/// to both sides of the meter (dual mono, as if panned centre).
void AudioEngine::applySlot (int slot)
{
    auto& route = routes[(size_t) slot];
    auto& config = configs[(size_t) slot];
    const auto* def = findSource (slotIds[(size_t) slot]);

    const bool newSource = config.sourceId != (def != nullptr ? def->id : juce::String());

    config = {};
    if (def != nullptr)
    {
        config.enabled = true;
        config.sourceId = def->id;
        config.name = def->name;
        config.colour = def->colour;
        config.stereo = def->stereo;
        route.left.store (juce::jlimit (0, maxDeviceChannels - 1, def->left));
        route.right.store (juce::jlimit (0, maxDeviceChannels - 1, def->rightChannel()));
    }
    route.enabled.store (config.enabled);

    if (newSource && processors[(size_t) slot] != nullptr)
        processors[(size_t) slot]->requestReset();
}

int AudioEngine::readStereoSamples (int slot, float* left, float* right, int maxSamples) noexcept
{
    auto& route = routes[(size_t) slot];
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

juce::String AudioEngine::getDeviceName() const
{
    if (auto* device = deviceManager.getCurrentAudioDevice())
        return device->getName();
    return "No input device";
}

void AudioEngine::resetAll()
{
    for (auto& p : processors)
        p->requestReset();
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

    for (auto& p : processors)
    {
        p->setRateAndBufferSizeDetails (rate, chunkSize);
        p->prepareToPlay (rate, chunkSize);
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
    for (auto& p : processors)
    {
        p->setRateAndBufferSizeDetails (rate, chunkSize);
        p->prepareToPlay (rate, chunkSize);
    }
}

void AudioEngine::processInputs (const float* const* inputs, int numInputs, int numSamples) noexcept
{
    auto inputFor = [&] (int deviceChannel) -> const float*
    {
        const int i = activeIndex[(size_t) deviceChannel];
        return (i >= 0 && i < numInputs) ? inputs[i] : nullptr;
    };

    for (size_t s = 0; s < routes.size(); ++s)
    {
        auto& route = routes[s];
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

            processors[s]->processBlock (route.buffer, route.midi);

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
