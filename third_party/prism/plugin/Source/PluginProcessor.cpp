#include "PluginProcessor.h"
#include "PluginEditor.h"

PrismMeterProcessor::PrismMeterProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      parameters (*this, nullptr, "PARAMS", createParameterLayout())
{
    ppmTypeParam = parameters.getRawParameterValue (ParamIds::ppmType);
}

juce::AudioProcessorValueTreeState::ParameterLayout PrismMeterProcessor::createParameterLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { ParamIds::vuReference, 1 }, "VU Reference",
                                                        Choices::vuReferences, 1));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { ParamIds::ppmType, 1 }, "PPM Type",
                                                        Choices::ppmTypes, 0));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { ParamIds::kScale, 1 }, "K-System Scale",
                                                        Choices::kScales, 1));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { ParamIds::loudnessTarget, 1 }, "Loudness Target",
                                                        Choices::loudnessTargets, 0));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { ParamIds::truePeakCeiling, 1 }, "True Peak Ceiling",
                                                        Choices::truePeakCeilings, 1));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { ParamIds::radarTurn, 1 }, "Radar Turn Time",
                                                        Choices::radarTurns, 2));
    return layout;
}

bool PrismMeterProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();

    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;

    return out == layouts.getMainInputChannelSet();
}

void PrismMeterProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentSampleRate.store (sampleRate);
    const int numChannels = juce::jlimit (1, MeterState::maxMeterChannels, getTotalNumInputChannels());

    loudness.prepare (sampleRate, numChannels);
    lastLoudnessStep = loudness.getStepCount();
    stereo.prepare (sampleRate);

    for (int ch = 0; ch < MeterState::maxMeterChannels; ++ch)
    {
        const auto i = static_cast<size_t> (ch);
        truePeak[i].prepare (sampleRate);
        vu[i].prepare (sampleRate);
        ppm[i].prepare (sampleRate);
        rms[i].setOptions (0.3, true);
        rms[i].prepare (sampleRate);
        needles[i].prepare (sampleRate);
    }

    monoNeedle.prepare (sampleRate);
    peakHoldSamples = static_cast<int> (sampleRate * 0.3);
    recentPeakGain.fill (0.0f);
    recentPeakAge.fill (0);
    lastPpmType = -1;
    monoScratch.assign (static_cast<size_t> (juce::jmax (1, samplesPerBlock)), 0.0f);
    spectrumFifo.reset();
    scopeFifo.reset();
    meterState.numChannels.store (numChannels);
}

void PrismMeterProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int numSamples = buffer.getNumSamples();
    const int numChannels = juce::jmin (getTotalNumInputChannels(), MeterState::maxMeterChannels);

    for (auto i = getTotalNumInputChannels(); i < getTotalNumOutputChannels(); ++i)
        buffer.clear (i, 0, numSamples);

    if (resetRequested.exchange (false))
    {
        loudness.resetIntegrated();
        shortTermHistory.clear();
        for (auto& tp : truePeak)
            tp.resetPeaks();
    }

    const int ppmChoice = static_cast<int> (ppmTypeParam->load());
    if (ppmChoice != lastPpmType)
    {
        for (auto& p : ppm)
            p.setType (static_cast<prism::PpmMeter::Type> (ppmChoice));
        lastPpmType = ppmChoice;
    }

    if (numSamples == 0 || numChannels == 0)
        return;

    const float* const* channels = buffer.getArrayOfReadPointers();

    loudness.process (channels, numChannels, numSamples);

    // One radar point per 100 ms step. A block longer than a step repeats the
    // newest value, which is all the meter kept.
    for (const auto steps = loudness.getStepCount(); lastLoudnessStep < steps; ++lastLoudnessStep)
        shortTermHistory.push (static_cast<float> (loudness.getShortTerm()));

    for (int ch = 0; ch < numChannels; ++ch)
    {
        const auto i = static_cast<size_t> (ch);
        truePeak[i].process (channels[ch], numSamples);

        const auto blockPeak = static_cast<float> (truePeak[i].getBlockTruePeak());
        recentPeakAge[i] += numSamples;
        if (blockPeak >= recentPeakGain[i] || recentPeakAge[i] > peakHoldSamples)
        {
            recentPeakGain[i] = blockPeak;
            recentPeakAge[i] = 0;
        }
        vu[i].process (channels[ch], numSamples);
        ppm[i].process (channels[ch], numSamples);
        rms[i].process (channels[ch], numSamples);
        needles[i].process (channels[ch], numSamples);
    }

    if (numChannels == 2)
        stereo.process (channels[0], channels[1], numSamples);

    pushSpectrumSamples (channels, numChannels, numSamples);
    pushScopeSamples (channels, numChannels, numSamples);
    publishReadings (numChannels);
}

void PrismMeterProcessor::publishReadings (int numChannels) noexcept
{
    auto& s = meterState;
    s.momentary.store (static_cast<float> (loudness.getMomentary()));
    s.shortTerm.store (static_cast<float> (loudness.getShortTerm()));
    s.integrated.store (static_cast<float> (loudness.getIntegrated()));
    s.loudnessRange.store (static_cast<float> (loudness.getLoudnessRange()));
    s.momentaryMax.store (static_cast<float> (loudness.getMomentaryMax()));
    s.shortTermMax.store (static_cast<float> (loudness.getShortTermMax()));

    float tpMax = 0.0f, spMax = 0.0f;

    for (int ch = 0; ch < numChannels; ++ch)
    {
        const auto i = static_cast<size_t> (ch);
        auto& c = s.channels[i];
        c.vu.store (static_cast<float> (vu[i].getReadingDbfs()));
        c.ppm.store (static_cast<float> (ppm[i].getReadingDbfs()));
        c.rms.store (static_cast<float> (rms[i].getReadingDbfs()));
        c.needlePeak.store (static_cast<float> (needles[i].getPeakDbfs()));
        c.needleAverage.store (static_cast<float> (needles[i].getAverageDbfs()));
        c.truePeak.store (static_cast<float> (prism::gainToDb (recentPeakGain[i])));
        tpMax = juce::jmax (tpMax, truePeak[i].getTruePeak());
        spMax = juce::jmax (spMax, truePeak[i].getSamplePeak());
    }

    s.monoPeak.store (static_cast<float> (monoNeedle.getPeakDbfs()));
    s.monoAverage.store (static_cast<float> (monoNeedle.getAverageDbfs()));
    s.truePeakMax.store (static_cast<float> (prism::gainToDb (tpMax)));
    s.samplePeakMax.store (static_cast<float> (prism::gainToDb (spMax)));

    if (numChannels == 2)
    {
        s.correlation.store (static_cast<float> (stereo.getCorrelation()));
        s.balanceDb.store (static_cast<float> (stereo.getBalanceDb()));
        s.width.store (static_cast<float> (stereo.getWidth()));
    }
    else
    {
        s.correlation.store (1.0f);
        s.balanceDb.store (0.0f);
        s.width.store (0.0f);
    }
}

void PrismMeterProcessor::pushSpectrumSamples (const float* const* channels, int numChannels, int numSamples) noexcept
{
    const int chunk = static_cast<int> (monoScratch.size());

    for (int offset = 0; offset < numSamples; offset += chunk)
    {
        const int n = juce::jmin (chunk, numSamples - offset);
        const float gain = 1.0f / static_cast<float> (numChannels);

        for (int i = 0; i < n; ++i)
        {
            float sum = 0.0f;
            for (int ch = 0; ch < numChannels; ++ch)
                sum += channels[ch][offset + i];
            monoScratch[static_cast<size_t> (i)] = sum * gain;
        }

        monoNeedle.process (monoScratch.data(), n);

        // If the editor is closed nobody drains the FIFO; drop samples instead of blocking.
        const auto scope = spectrumFifo.write (juce::jmin (n, spectrumFifo.getFreeSpace()));
        if (scope.blockSize1 > 0)
            std::copy_n (monoScratch.data(), scope.blockSize1, spectrumBuffer.data() + scope.startIndex1);
        if (scope.blockSize2 > 0)
            std::copy_n (monoScratch.data() + scope.blockSize1, scope.blockSize2, spectrumBuffer.data() + scope.startIndex2);
    }
}

void PrismMeterProcessor::pushScopeSamples (const float* const* channels, int numChannels, int numSamples) noexcept
{
    const float* left = channels[0];
    const float* right = channels[numChannels > 1 ? 1 : 0];

    // Drop what does not fit rather than block when the editor is closed.
    const auto scope = scopeFifo.write (juce::jmin (numSamples, scopeFifo.getFreeSpace()));
    if (scope.blockSize1 > 0)
    {
        std::copy_n (left, scope.blockSize1, scopeLeft.data() + scope.startIndex1);
        std::copy_n (right, scope.blockSize1, scopeRight.data() + scope.startIndex1);
    }
    if (scope.blockSize2 > 0)
    {
        std::copy_n (left + scope.blockSize1, scope.blockSize2, scopeLeft.data() + scope.startIndex2);
        std::copy_n (right + scope.blockSize1, scope.blockSize2, scopeRight.data() + scope.startIndex2);
    }
}

int PrismMeterProcessor::readScopeSamples (float* left, float* right, int maxSamples) noexcept
{
    const auto scope = scopeFifo.read (juce::jmin (maxSamples, scopeFifo.getNumReady()));
    if (scope.blockSize1 > 0)
    {
        std::copy_n (scopeLeft.data() + scope.startIndex1, scope.blockSize1, left);
        std::copy_n (scopeRight.data() + scope.startIndex1, scope.blockSize1, right);
    }
    if (scope.blockSize2 > 0)
    {
        std::copy_n (scopeLeft.data() + scope.startIndex2, scope.blockSize2, left + scope.blockSize1);
        std::copy_n (scopeRight.data() + scope.startIndex2, scope.blockSize2, right + scope.blockSize1);
    }
    return scope.blockSize1 + scope.blockSize2;
}

int PrismMeterProcessor::readSpectrumSamples (float* dest, int maxSamples) noexcept
{
    const auto scope = spectrumFifo.read (juce::jmin (maxSamples, spectrumFifo.getNumReady()));
    if (scope.blockSize1 > 0)
        std::copy_n (spectrumBuffer.data() + scope.startIndex1, scope.blockSize1, dest);
    if (scope.blockSize2 > 0)
        std::copy_n (spectrumBuffer.data() + scope.startIndex2, scope.blockSize2, dest + scope.blockSize1);
    return scope.blockSize1 + scope.blockSize2;
}

juce::AudioProcessorEditor* PrismMeterProcessor::createEditor()
{
    return new PrismMeterEditor (*this);
}

void PrismMeterProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = parameters.copyState();
    state.setProperty ("stateVersion", 1, nullptr);
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void PrismMeterProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (parameters.state.getType()))
            parameters.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new PrismMeterProcessor();
}
