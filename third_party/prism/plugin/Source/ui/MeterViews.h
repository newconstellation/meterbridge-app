#pragma once

#include <prism/SpectrumAnalyzer.h>

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <vector>

//==============================================================================
/// A bank of segmented LED-style bars sharing one scale (VU, PPM, K-System).
/// Segments light green below the warn level, yellow up to the over level and
/// red above it; unlit segments stay faintly visible like a hardware meter.
class BarMeterBank final : public juce::Component
{
public:
    struct Scale
    {
        float minValue = -40.0f;
        float maxValue = 3.0f;
        float warnFrom = 0.0f;
        float overFrom = 3.0f;
        std::vector<float> ticks;
        juce::String unit;
    };

    BarMeterBank (juce::String title, juce::Colour accent, int numBars);

    void setScale (Scale newScale);
    void setTitle (juce::String newTitle);
    void setBarLabel (int index, juce::String label);
    void setValue (int index, float value);
    void setNumBarsVisible (int n);

    void paint (juce::Graphics&) override;

private:
    float toNorm (float value) const noexcept;
    juce::Colour zoneColour (float value) const noexcept;

    juce::String title;
    juce::Colour accent;
    Scale scale;
    std::vector<float> values, holds;
    std::vector<int> holdAge;
    std::vector<juce::String> labels;
    int visibleBars;
};

//==============================================================================
/// Loudness readouts, segmented momentary / short-term bars and a scrolling
/// short-term history against the target.
class LoudnessPanel final : public juce::Component
{
public:
    struct Readings
    {
        float momentary = -150.0f, shortTerm = -150.0f, integrated = -150.0f;
        float loudnessRange = 0.0f, momentaryMax = -150.0f, shortTermMax = -150.0f;
        float truePeakMax = -150.0f, samplePeakMax = -150.0f;
        float target = -14.0f, truePeakCeiling = -1.0f;
    };

    void setReadings (const Readings& r);

    /// Call at a steady rate (10 Hz) to extend the history graph.
    void pushHistory (float shortTermLufs, float momentaryLufs);
    void clearHistory();

    void paint (juce::Graphics&) override;

private:
    static constexpr int historyLength = 600; // 60 s at 10 Hz

    Readings readings;
    std::array<float, historyLength> shortHistory {}, momentaryHistory {};
    int historyWrite = 0;
    int historyCount = 0;
};

//==============================================================================
/// Dual-needle peak and average gauge on an ivory analogue face (our design).
/// With two channels and a wide enough area it shows L and R as two dials side
/// by side; otherwise one dial shows the louder channel.
class NeedleGauge final : public juce::Component
{
public:
    void setValues (float peakDb, float averageDb);
    void setStereoValues (float peakL, float averageL, float peakR, float averageR);
    void paint (juce::Graphics&) override;

private:
    void drawDial (juce::Graphics&, juce::Rectangle<float> area, int channel, const juce::String& label) const;
    void updateHolds();

    bool stereo = false;
    float peak[2] { -60.0f, -60.0f }, average[2] { -60.0f, -60.0f }, hold[2] { -60.0f, -60.0f };
    int holdFrames[2] { 0, 0 };
};

//==============================================================================
/// LED peak + average meter (our design, after the public idea of showing
/// peak and average at once on one LED display). The average lights as a
/// solid bar of LEDs in green / amber / red, and the peak as a single LED in
/// a contrasting ice-white riding above it, so the gap between them is the
/// crest factor. A dimmer marker holds the highest peak for two seconds, an
/// OVER lamp follows the true-peak ceiling, and readouts give peak, average
/// and crest.
///
/// Two shapes: long straight bar graphs (horizontal, or vertical in tall
/// slots) with a calibrated scale between them, or a wide arched LED monitor
/// per channel in a recessed bezel (one LED per dB, -25 to +20, with a
/// dB-re-reference scale above and a dBFS scale below). Clicking the meter
/// cycles peak hold: auto 3 s, hold until reset, off.
/// One channel for a mono (or mono-sum) meter, two for stereo.
class LedPeakAverageMeter final : public juce::Component
{
public:
    enum class Shape { bar, arc };
    void setShape (Shape newShape) { shape = newShape; }

    void setTitle (juce::String newTitle);
    void setNumFaces (int n);

    /// Readings in dB relative to the reference (0 = reference level).
    /// fullScale is where 0 dBFS sits on that relative scale.
    void setValues (int face, float peakRel, float averageRel, bool over);
    void setFullScale (float relDb);
    void setFaceLabel (int face, juce::String label);
    void setReferenceText (juce::String text);
    void setPhaseAlarm (bool alarm) { phaseAlarm = alarm; }

    enum class HoldMode { autoReset, untilReset, off };
    void mouseDown (const juce::MouseEvent&) override;

    void paint (juce::Graphics&) override;

    /// Position 0..1 around the arc for a relative level: the low end is
    /// compressed and the range around 0 spread out, so the working range
    /// gets most of the LEDs.
    static float arcPosition (float relDb) noexcept;

    static constexpr float archedAspect = 3.0f;
    static constexpr int numBarSegments = 96;
    static const juce::Colour peakColour;
    static constexpr float scaleMin = -40.0f, scaleMax = 20.0f;

private:
    void drawFace (juce::Graphics&, juce::Rectangle<float> area, int face, bool ownPlate) const;
    void drawBars (juce::Graphics&, juce::Rectangle<float> area) const;
    juce::Colour zoneColour (float relDb) const noexcept;
    juce::Colour archedLedColour (float relDb) const noexcept;

    Shape shape = Shape::bar;
    juce::String title { "Peak + Average" };
    juce::String referenceText;
    int numFaces = 2;
    HoldMode holdMode = HoldMode::autoReset;
    bool phaseAlarm = false;
    float fullScale = 18.0f;
    std::array<float, 2> peak { -60.0f, -60.0f }, average { -60.0f, -60.0f }, hold { -60.0f, -60.0f };
    std::array<int, 2> holdFrames { 0, 0 }, overFrames { 0, 0 };
    std::array<juce::String, 2> labels { "L", "R" };
};

//==============================================================================
/// Classic analogue VU meters, one per channel (our design): a backlit warm
/// face behind glass in a dark bezel, the standard -20 to +3 VU scale laid out
/// on the voltage law with a red zone above 0 VU, a secondary percent scale,
/// a PEAK lamp per channel and a numeric reading under each meter.
class AnalogVuMeter final : public juce::Component
{
public:
    void setTitle (juce::String newTitle);
    void setNumChannels (int n);

    /// vu is in VU (0 = the reference level); peakOver lights the PEAK lamp.
    void setValue (int channel, float vu, bool peakOver);

    void paint (juce::Graphics&) override;

    /// Needle position 0..1 across the scale for a VU value (voltage law,
    /// -20 VU at 0 and +3 VU at 1), allowed to pin slightly past both ends.
    static float needlePosition (float vu) noexcept;


private:
    void drawMeter (juce::Graphics&, juce::Rectangle<float> area, int channel, const juce::String& label) const;


    juce::String title { "VU" };
    int numChannels = 2;
    std::array<float, 2> values { -40.0f, -40.0f };
    std::array<int, 2> lampFrames { 0, 0 };
};

//==============================================================================
/// Vectorscope (mid up, side across) with phosphor persistence, plus the
/// correlation bar and balance / width readouts.
class PhasePanel final : public juce::Component
{
public:
    PhasePanel();

    void setValues (float correlation, float balanceDb, float width);
    void pushSamples (const float* left, const float* right, int numSamples);

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void fadeTrace();

    juce::Image trace;
    float correlation = 0.0f, balance = 0.0f, width = 0.0f;
    float autoGain = 1.0f;
};

//==============================================================================
/// Log-frequency spectrum with a frequency-coloured fill, peak hold and tilt,
/// and a scrolling spectrogram below it.
class SpectrumView final : public juce::Component
{
public:
    SpectrumView();

    void prepare (double sampleRate);
    void pushSamples (const float* samples, int numSamples);

    void setTiltEnabled (bool shouldTilt) { tilt = shouldTilt; repaint(); }
    void resetPeakHold() { analyzer.reset(); }

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDoubleClick (const juce::MouseEvent&) override { resetPeakHold(); }

private:
    static constexpr int fftOrder = 13; // 8192 points
    static constexpr int hopSize = 2048;
    static constexpr int spectrogramRows = 160;

    float tiltAt (float hz) const noexcept;
    float levelBetween (float hzLo, float hzHi, bool peak) const noexcept;
    void addSpectrogramColumn();

    prism::SpectrumAnalyzer analyzer;
    std::vector<float> ring, frame;
    int ringWrite = 0;
    int samplesSinceFrame = 0;
    double sampleRate = 48000.0;
    bool tilt = true;
    bool verticalSpectrogram = true; // waterfall by default; false = classic left-to-right

    juce::Image spectrogram;
};
