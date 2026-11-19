#include "MeterModules.h"
#include "Theme.h"
#include "../PluginProcessor.h"

namespace
{
    int choiceIndex (juce::AudioProcessorValueTreeState& params, const char* id)
    {
        return static_cast<int> (params.getRawParameterValue (id)->load());
    }

    /// Hosts one view that fills the module.
    template <typename View>
    class ViewModule : public MeterModule
    {
    public:
        ViewModule() { addAndMakeVisible (view); }
        void resized() override { view.setBounds (getLocalBounds()); }

    protected:
        View view;
    };

    //==========================================================================
    class LoudnessModule final : public ViewModule<LoudnessPanel>
    {
    public:
        void update (const ModuleContext& c) override
        {
            const auto& s = c.state;
            LoudnessPanel::Readings r;
            r.momentary = s.momentary.load();
            r.shortTerm = s.shortTerm.load();
            r.integrated = s.integrated.load();
            r.loudnessRange = s.loudnessRange.load();
            r.momentaryMax = s.momentaryMax.load();
            r.shortTermMax = s.shortTermMax.load();
            r.truePeakMax = s.truePeakMax.load();
            r.samplePeakMax = s.samplePeakMax.load();
            r.target = Choices::loudnessTargetValues[static_cast<size_t> (choiceIndex (c.params, ParamIds::loudnessTarget))];
            r.truePeakCeiling = Choices::truePeakCeilingValues[static_cast<size_t> (choiceIndex (c.params, ParamIds::truePeakCeiling))];
            view.setReadings (r);

            if (c.historyTick)
                view.pushHistory (r.shortTerm, r.momentary);
        }

        void resetDisplay() override { view.clearHistory(); }
    };

    //==========================================================================
    class RadarModule final : public ViewModule<RadarView>
    {
    public:
        RadarModule()
        {
            view.onTurnButtonClicked = [this] { showTurnMenu(); };
        }

        void update (const ModuleContext& c) override
        {
            params = &c.params;
            const auto& s = c.state;

            RadarView::Readings r;
            r.momentary = s.momentary.load();
            r.shortTerm = s.shortTerm.load();
            r.integrated = s.integrated.load();
            r.loudnessRange = s.loudnessRange.load();
            r.truePeakMax = s.truePeakMax.load();
            r.target = Choices::loudnessTargetValues[static_cast<size_t> (choiceIndex (c.params, ParamIds::loudnessTarget))];
            r.truePeakCeiling = Choices::truePeakCeilingValues[static_cast<size_t> (choiceIndex (c.params, ParamIds::truePeakCeiling))];

            const int turn = juce::jlimit (0, Choices::radarTurns.size() - 1, choiceIndex (c.params, ParamIds::radarTurn));
            const int perTurn = juce::roundToInt (Choices::radarTurnMinutes[static_cast<size_t> (turn)] * 600.0f);
            if (turn != lastTurn)
            {
                lastTurn = turn;
                view.setTurnLabel (Choices::radarTurns[turn] + " / turn");
            }

            if (c.shortTermHistory != nullptr)
            {
                r.elapsedSeconds = static_cast<double> (c.shortTermHistory->size()) / 10.0;
                view.setReadings (r);

                // The sweep only changes at the 10 Hz loudness step.
                const auto size = c.shortTermHistory->size();
                if (size != lastSize || turn != lastDrawnTurn || ! juce::exactlyEqual (r.target, lastTarget))
                {
                    lastSize = size;
                    lastDrawnTurn = turn;
                    lastTarget = r.target;
                    points.resize (static_cast<size_t> (perTurn));
                    std::uint64_t first = 0;
                    const int n = c.shortTermHistory->readLatest (points.data(), perTurn, first);
                    view.setHistory (points.data(), n, first, perTurn);
                }
            }
            else
            {
                view.setReadings (r);
            }

            view.repaint();
        }

    private:
        void showTurnMenu()
        {
            if (params == nullptr)
                return;

            juce::PopupMenu menu;
            menu.addSectionHeader ("One turn of the radar");
            for (int i = 0; i < Choices::radarTurns.size(); ++i)
                menu.addItem (i + 1, Choices::radarTurns[i], true, i == lastTurn);

            menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (view.getTurnButton()),
                                [safeThis = juce::Component::SafePointer<RadarModule> (this)] (int result)
                                {
                                    if (safeThis == nullptr || result <= 0 || safeThis->params == nullptr)
                                        return;
                                    if (auto* p = safeThis->params->getParameter (ParamIds::radarTurn))
                                        p->setValueNotifyingHost (p->convertTo0to1 (static_cast<float> (result - 1)));
                                });
        }

        juce::AudioProcessorValueTreeState* params = nullptr;
        std::vector<float> points;
        std::uint64_t lastSize = ~std::uint64_t (0);
        int lastTurn = -1, lastDrawnTurn = -1;
        float lastTarget = 0.0f;
    };

    //==========================================================================
    /// LED peak + average (bar or arc), on the mono sum or on L and R.
    class LedPeakAverageModule final : public ViewModule<LedPeakAverageMeter>
    {
    public:
        explicit LedPeakAverageModule (bool isStereo, LedPeakAverageMeter::Shape shape = LedPeakAverageMeter::Shape::arc)
            : stereo (isStereo), prefix (shape == LedPeakAverageMeter::Shape::arc ? "Arched LED Peak + Average" : "LED Bar Peak + Average")
        {
            view.setShape (shape);
        }

        void update (const ModuleContext& c) override
        {
            const int refChoice = choiceIndex (c.params, ParamIds::vuReference);
            if (refChoice != lastChoice)
            {
                lastChoice = refChoice;
                reference = Choices::vuReferenceValues[static_cast<size_t> (refChoice)];
                view.setFullScale (-reference);
                view.setReferenceText ("0 = " + Choices::vuReferences[refChoice] + "   peak: instant, falls 13 dB/s   average: RMS 600 ms");
            }

            const float ceiling = Choices::truePeakCeilingValues[static_cast<size_t> (choiceIndex (c.params, ParamIds::truePeakCeiling))];
            const int channels = juce::jlimit (1, 2, c.state.numChannels.load());

            if (stereo && channels == 2)
            {
                view.setTitle (prefix + "  stereo");
                view.setPhaseAlarm (c.state.correlation.load() < 0.0f);
                view.setNumFaces (2);
                view.setFaceLabel (0, "L");
                view.setFaceLabel (1, "R");
                for (int ch = 0; ch < 2; ++ch)
                {
                    const auto& s = c.state.channels[static_cast<size_t> (ch)];
                    view.setValues (ch, s.needlePeak.load() - reference, s.needleAverage.load() - reference, s.truePeak.load() > ceiling);
                }
            }
            else
            {
                view.setTitle (prefix + (channels == 2 ? "  mono sum" : "  mono"));
                view.setNumFaces (1);
                view.setFaceLabel (0, channels == 2 ? "L + R" : "MONO");
                const bool over = c.state.channels[0].truePeak.load() > ceiling
                               || (channels == 2 && c.state.channels[1].truePeak.load() > ceiling);
                view.setValues (0, c.state.monoPeak.load() - reference, c.state.monoAverage.load() - reference, over);
            }

            view.repaint();
        }

    private:
        bool stereo;
        juce::String prefix;
        int lastChoice = -1;
        float reference = -18.0f;
    };

    //==========================================================================
    class PeakAverageModule final : public ViewModule<NeedleGauge>
    {
    public:
        void update (const ModuleContext& c) override
        {
            const auto& ch = c.state.channels;

            if (c.state.numChannels.load() >= 2)
                view.setStereoValues (ch[0].needlePeak.load(), ch[0].needleAverage.load(),
                                      ch[1].needlePeak.load(), ch[1].needleAverage.load());
            else
                view.setValues (ch[0].needlePeak.load(), ch[0].needleAverage.load());
        }
    };

    //==========================================================================
    class SpectrumModule final : public ViewModule<SpectrumView>
    {
    public:
        void update (const ModuleContext& c) override
        {
            if (! juce::exactlyEqual (c.sampleRate, sampleRate))
            {
                sampleRate = c.sampleRate;
                view.prepare (sampleRate);
            }

            if (c.spectrumTilt != tilt)
            {
                tilt = c.spectrumTilt;
                view.setTiltEnabled (tilt);
            }

            if (c.numSpectrumSamples > 0)
                view.pushSamples (c.spectrumSamples, c.numSpectrumSamples);

            view.repaint();
        }

        void resetDisplay() override { view.resetPeakHold(); }

    private:
        double sampleRate = 0.0;
        bool tilt = true;
    };

    //==========================================================================
    class PhaseModule final : public ViewModule<PhasePanel>
    {
    public:
        void update (const ModuleContext& c) override
        {
            view.setValues (c.state.correlation.load(), c.state.balanceDb.load(), c.state.width.load());

            // Draw only the newest samples so the trace stays current.
            constexpr int maxPoints = 1600;
            if (c.numScopeSamples > 0)
            {
                const int skip = juce::jmax (0, c.numScopeSamples - maxPoints);
                view.pushSamples (c.scopeLeft + skip, c.scopeRight + skip, c.numScopeSamples - skip);
            }

            view.repaint();
        }
    };

    //==========================================================================
    /// Analogue VU meters: round-scale faces, side by side or stacked
    /// (the view picks whichever gives bigger meters).
    class VuModule final : public ViewModule<AnalogVuMeter>
    {
    public:
        void update (const ModuleContext& c) override
        {
            const int refChoice = choiceIndex (c.params, ParamIds::vuReference);
            if (refChoice != lastChoice)
            {
                lastChoice = refChoice;
                reference = Choices::vuReferenceValues[static_cast<size_t> (refChoice)];
                view.setTitle ("VU  " + Choices::vuReferences[refChoice]);
            }

            const float ceiling = Choices::truePeakCeilingValues[static_cast<size_t> (choiceIndex (c.params, ParamIds::truePeakCeiling))];
            const int channels = juce::jlimit (1, 2, c.state.numChannels.load());
            view.setNumChannels (channels);

            for (int ch = 0; ch < channels; ++ch)
            {
                const auto& s = c.state.channels[static_cast<size_t> (ch)];
                view.setValue (ch, s.vu.load() - reference, s.truePeak.load() > ceiling);
            }

            view.repaint();
        }

    private:
        int lastChoice = -1;
        float reference = -18.0f;
    };

    //==========================================================================
    class BankModule final : public MeterModule
    {
    public:
        explicit BankModule (MeterKind k)
            : kind (k),
              bank (MeterKinds::displayName (k),
                    k == MeterKind::vu ? Theme::vuAccent : k == MeterKind::ppm ? Theme::ppmAccent : Theme::kAccent, 2)
        {
            bank.setBarLabel (0, "L");
            bank.setBarLabel (1, "R");
            addAndMakeVisible (bank);
        }

        void resized() override { bank.setBounds (getLocalBounds()); }

        void update (const ModuleContext& c) override
        {
            updateScale (c.params);

            const int numChannels = c.state.numChannels.load();
            bank.setNumBarsVisible (numChannels);

            for (int ch = 0; ch < numChannels; ++ch)
            {
                const auto& s = c.state.channels[static_cast<size_t> (ch)];
                switch (kind)
                {
                    case MeterKind::vu:  bank.setValue (ch, s.vu.load() - reference); break;
                    case MeterKind::ppm: bank.setValue (ch, s.ppm.load() - reference); break;
                    case MeterKind::kSystem:
                    case MeterKind::loudness:
                    case MeterKind::peakAverage:
                    case MeterKind::arcMono:
                    case MeterKind::arcStereo:
                    case MeterKind::barMono:
                    case MeterKind::barStereo:
                    case MeterKind::spectrum:
                    case MeterKind::phase:
                    default:             bank.setValue (ch, s.rms.load() - reference); break;
                }
            }

            bank.repaint();
        }

    private:
        void updateScale (juce::AudioProcessorValueTreeState& params)
        {
            const int paramId = kind == MeterKind::vu ? choiceIndex (params, ParamIds::vuReference)
                              : kind == MeterKind::ppm ? choiceIndex (params, ParamIds::ppmType)
                                                       : choiceIndex (params, ParamIds::kScale);
            if (paramId == lastChoice)
                return;

            lastChoice = paramId;

            switch (kind)
            {
                case MeterKind::vu:
                    reference = Choices::vuReferenceValues[static_cast<size_t> (paramId)];
                    bank.setScale ({ -20.0f, 3.0f, 0.0f, 1.0f, { -20.0f, -10.0f, -7.0f, -5.0f, -3.0f, -1.0f, 0.0f, 1.0f, 3.0f }, "VU" });
                    bank.setTitle ("VU  0 = " + Choices::vuReferences[paramId]);
                    break;

                case MeterKind::ppm:
                    // Shown in dB relative to alignment (-18 dBFS; DIN uses -9 dBFS as 0 dB).
                    reference = paramId == 2 ? -9.0f : -18.0f;
                    if (paramId >= 2)
                        bank.setScale ({ -42.0f, 9.0f, 0.0f, 6.0f, { -42.0f, -36.0f, -30.0f, -24.0f, -18.0f, -12.0f, -6.0f, 0.0f, 6.0f, 9.0f }, "dB" });
                    else
                        bank.setScale ({ -24.0f, 12.0f, 8.0f, 9.0f, { -24.0f, -12.0f, -8.0f, -4.0f, 0.0f, 4.0f, 8.0f, 12.0f }, "dB" });
                    bank.setTitle ("PPM " + Choices::ppmTypes[paramId]);
                    break;

                case MeterKind::kSystem:
                case MeterKind::loudness:
                case MeterKind::peakAverage:
                case MeterKind::arcMono:
                case MeterKind::arcStereo:
                case MeterKind::barMono:
                case MeterKind::barStereo:
                case MeterKind::spectrum:
                case MeterKind::phase:
                default:
                    reference = -Choices::kScaleValues[static_cast<size_t> (paramId)];
                    bank.setScale ({ -30.0f, Choices::kScaleValues[static_cast<size_t> (paramId)], 0.0f, 4.0f,
                                     { -30.0f, -20.0f, -10.0f, -6.0f, 0.0f, 4.0f, 8.0f, 12.0f }, "K" });
                    bank.setTitle (Choices::kScales[paramId]);
                    break;
            }
        }

        MeterKind kind;
        BarMeterBank bank;
        int lastChoice = -1;
        float reference = 0.0f;
    };
} // namespace

std::unique_ptr<MeterModule> MeterModule::create (MeterKind kind)
{
    switch (kind)
    {
        case MeterKind::loudness:    return std::make_unique<LoudnessModule>();
        case MeterKind::peakAverage: return std::make_unique<PeakAverageModule>();
        case MeterKind::spectrum:    return std::make_unique<SpectrumModule>();
        case MeterKind::phase:       return std::make_unique<PhaseModule>();
        case MeterKind::vu:          return std::make_unique<VuModule>();
        case MeterKind::ppm:
        case MeterKind::kSystem:     return std::make_unique<BankModule> (kind);
        case MeterKind::radar:       return std::make_unique<RadarModule>();
        case MeterKind::arcMono:     return std::make_unique<LedPeakAverageModule> (false);
        case MeterKind::arcStereo:   return std::make_unique<LedPeakAverageModule> (true);
        case MeterKind::barMono:     return std::make_unique<LedPeakAverageModule> (false, LedPeakAverageMeter::Shape::bar);
        case MeterKind::barStereo:   return std::make_unique<LedPeakAverageModule> (true, LedPeakAverageMeter::Shape::bar);
    }
    return std::make_unique<LoudnessModule>();
}

//==============================================================================
MeterSlot::MeterSlot()
{
    menuButton.setTooltip ("Choose the meter for this slot");
    menuButton.setColour (juce::TextButton::buttonColourId, Theme::face.withAlpha (0.6f));
    menuButton.setColour (juce::TextButton::textColourOffId, Theme::textPrimary);
    menuButton.onClick = [this] { showKindMenu(); };
    addAndMakeVisible (menuButton);
    setKind (kind);
}

void MeterSlot::setKind (MeterKind newKind)
{
    if (module != nullptr && newKind == kind)
        return;

    kind = newKind;
    module = MeterModule::create (kind);
    addAndMakeVisible (*module);
    menuButton.toFront (false);
    resized();
}

void MeterSlot::resized()
{
    if (module != nullptr)
        module->setBounds (getLocalBounds());

    // Sits at the right end of the panel's title strip.
    menuButton.setBounds (getWidth() - 28, 2, 24, static_cast<int> (Theme::titleHeight) - 4);
}

void MeterSlot::showKindMenu()
{
    juce::PopupMenu menu;
    menu.addSectionHeader ("Show in this slot");

    int itemId = 1;
    for (auto k : MeterKinds::all())
        menu.addItem (itemId++, MeterKinds::displayName (k), true, k == kind);

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (menuButton),
                        [safeThis = juce::Component::SafePointer<MeterSlot> (this)] (int result)
                        {
                            if (safeThis == nullptr || result <= 0)
                                return;

                            const auto chosen = MeterKinds::all()[static_cast<size_t> (result - 1)];
                            safeThis->setKind (chosen);
                            if (safeThis->onKindChosen)
                                safeThis->onKindChosen (chosen);
                        });
}
