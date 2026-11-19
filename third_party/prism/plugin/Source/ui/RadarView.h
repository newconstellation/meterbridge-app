#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <cstdint>
#include <functional>

//==============================================================================
/// Loudness radar (our design): short-term loudness swept around a dial, one
/// turn per chosen time, against the loudness target.
///
/// - Distance from the centre is short-term loudness in LU against the target,
///   -36 LU at the centre hole and +18 LU at the edge. The 0 LU ring is the
///   target and is drawn in the tool's accent colour.
/// - The hand shows now; older points fade a little the further they lie
///   behind it. A new turn overwrites the oldest.
/// - The outer ring is a live momentary loudness arc, 0 LU at the top.
/// - The centre shows programme (integrated) loudness against the target.
class RadarView final : public juce::Component
{
public:
    struct Readings
    {
        float momentary = -150.0f, shortTerm = -150.0f, integrated = -150.0f;
        float loudnessRange = 0.0f, truePeakMax = -150.0f;
        float target = -14.0f, truePeakCeiling = -1.0f;
        double elapsedSeconds = 0.0;
    };

    RadarView();

    void setReadings (const Readings& r);

    /// Replaces the sweep with the newest points. values[i] is short-term LUFS for
    /// run index firstIndex + i (10 points a second); pointsPerTurn sets the speed.
    void setHistory (const float* values, int numValues, std::uint64_t firstIndex, int pointsPerTurn);

    void setTurnLabel (const juce::String& text);

    /// Called when the person clicks the turn-time button.
    std::function<void()> onTurnButtonClicked;

    juce::Component& getTurnButton() noexcept { return turnButton; }

    void paint (juce::Graphics&) override;
    void resized() override;

    /// LU against the target that the dial spans, centre to edge.
    static constexpr float minLu = -36.0f, maxLu = 18.0f;

    /// Zone colour for a level in LU against the target.
    static juce::Colour zoneColour (float lu);

private:
    static constexpr int numBins = 360;   // one wedge per degree

    juce::Rectangle<float> dialBounds() const;
    void paintDial (juce::Graphics&, juce::Rectangle<float> dial);
    void paintReadouts (juce::Graphics&, juce::Rectangle<float> area, bool vertical);

    Readings readings;
    std::array<float, numBins> binLu {};      // peak short-term per wedge, LU vs target
    std::array<float, numBins> binAge {};     // 0 at the hand, 1 a full turn behind
    std::array<bool, numBins> binUsed {};
    int handBin = -1;
    float handFraction = 0.0f;                // position of the hand, 0..1 of a turn

    juce::TextButton turnButton;
    juce::Rectangle<float> content, readoutArea;
    bool readoutsBeside = true;
};
