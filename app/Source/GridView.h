#pragma once

#include "AppLayouts.h"
#include "MeterHub.h"

#include <memory>
#include <vector>

/// A weighted grid of Prism meter slots (Quad, Row of four, Meter panel...).
/// Every slot shows one source; each slot's menu picks its tool.
class GridView final : public juce::Component
{
public:
    GridView (const juce::String& viewId, const juce::StringArray& savedKinds);

    void update (MeterHub&, int source);
    void resetDisplay();

    /// Tool ids per slot, for saving.
    juce::StringArray getKindIds() const;
    std::function<void()> onLayoutChanged;

    void resized() override;

private:
    DisplayPreset preset;
    std::vector<std::unique_ptr<MeterSlot>> slots;
};
