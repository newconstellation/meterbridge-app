#pragma once

#include "MeterHub.h"

#include <prism/SpectrumAnalyzer.h>

#include <array>
#include <vector>

/// Up to four sources' spectra as coloured traces on one graph, with the sum
/// of the shown sources over time as a background, and an inputs sidebar
/// (show / solo, integrated loudness, momentary bar).
///
/// Background styles:
/// - split:     traces on a clean graph, the summed waterfall underneath it
///              (newest row at the top, time running down).
/// - quietBlue: the summed waterfall behind the traces in one dim blue hue.
/// - grey3d:    the sum as a grey 3D surface behind the traces (loud ridges warm
///              into heat colours), newest slice
///              at the front, older slices stepping back into the distance.
class OverlayView final : public juce::Component
{
public:
    enum class Style { split, quietBlue, grey3d };

    OverlayView();

    void update (MeterHub&);

    void setStyle (Style);
    Style getStyle() const noexcept { return style; }
    static juce::String styleId (Style);
    static Style styleFromId (const juce::String&);

    /// Spectrum averaging (response time) choices, in seconds.
    static constexpr std::array<float, 5> averagingChoices { 0.0f, 0.1f, 0.3f, 1.0f, 3.0f };
    void setAveragingIndex (int index);
    int getAveragingIndex() const noexcept { return averagingIndex; }

    /// Bit i set = source i shown.
    int getShownMask() const noexcept;
    void setShownMask (int mask);

    std::function<void()> onSettingsChanged;

    void resetDisplay();

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override { resetDisplay(); }

private:
    static constexpr int fftOrder = 13;   // 8192 points
    static constexpr int hopSize = 2048;
    static constexpr int tracePoints = 240;
    static constexpr int historyCols = 360;
    static constexpr int historyRows = 160;
    static constexpr float topDb = 0.0f, bottomDb = -84.0f;

    // Fixed level scale for every background, so history never changes once drawn.
    static constexpr float heatTopDb = -12.0f, heatRangeDb = 60.0f;

    // 3D: each slice is the loudest of a few history rows, frozen once committed.
    static constexpr int slices3d = 40;
    static constexpr int rowsPerSlice = 3;
    static constexpr int sliceCols = historyCols / 2;

    /// One side (L or R) of a slot's analysis.
    struct Side
    {
        prism::SpectrumAnalyzer analyzer;
        std::vector<float> ring, frame;
        int ringWrite = 0, samplesSinceFrame = 0;
    };

    /// A slot's spectrum. A stereo source keeps L and R apart and averages
    /// their power per frequency, so out-of-phase content never cancels.
    struct Channel
    {
        std::array<Side, 2> sides;
        bool stereo = true;
        std::array<float, tracePoints> trace {}, peak {};
    };

    void prepare (double sampleRate);
    bool pushSamples (Side&, const float* samples, int n);   // true when a new frame was analysed
    float sideLevel (const Side&, float hzLo, float hzHi, bool peak) const noexcept;
    float bandLevel (const Channel&, float hzLo, float hzHi, bool peak) const noexcept;
    void showSlotMenu (int slot);
    bool isVisible (int source) const noexcept;
    void addHistoryRow();
    void commitRowTo3d (const float* row);
    void applyAveraging();
    void rebuildWaterfallImage();

    juce::Rectangle<float> plotArea (juce::Rectangle<float> face, bool withHzLabels) const;
    void drawAxes (juce::Graphics&, juce::Rectangle<float> face, juce::Rectangle<float> plot, bool hzLabels) const;
    void drawTraces (juce::Graphics&, juce::Rectangle<float> plot) const;
    void drawGrey3d (juce::Graphics&, juce::Rectangle<float> plot) const;
    void drawSidebar (juce::Graphics&, juce::Rectangle<float> area);

    static float freqAt (float norm) noexcept;   // 0..1 -> 20 Hz..20 kHz, log
    static float normAt (float hz) noexcept;

    Style style = Style::grey3d;
    double sampleRate = 48000.0;
    std::array<Channel, Sources::count> channels;
    std::array<bool, Sources::count> shown { true, true, true, true };
    std::array<bool, Sources::count> enabled {};
    std::array<juce::String, Sources::count> names;
    std::array<juce::Colour, Sources::count> colours;
    AudioEngine* engine = nullptr;
    std::array<float, Sources::count> integrated {}, momentary {};
    int solo = -1;

    std::vector<float> history = std::vector<float> ((size_t) (historyCols * historyRows), -150.0f);
    int historyNewest = 0;   // row index of the newest line
    std::vector<float> slices = std::vector<float> ((size_t) (slices3d * sliceCols), -150.0f);
    std::array<float, sliceCols> pendingSlice {};
    int pendingRows = 0;
    int sliceNewest = 0;
    int averagingIndex = 2;
    juce::Image waterfall { juce::Image::RGB, historyCols, historyRows, true };

    juce::TextButton styleButton, averagingButton;
    juce::Rectangle<float> mainPanel, sidePanel;
    std::array<juce::Rectangle<float>, Sources::count> eyeRects, soloRects, nameRects;
};
