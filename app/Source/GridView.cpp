#include "GridView.h"

namespace
{
    constexpr int gap = 8;
}

GridView::GridView (const juce::String& viewId, const juce::StringArray& savedKinds)
    : preset (AppViews::grid (viewId))
{
    for (size_t i = 0; i < preset.slots.size(); ++i)
    {
        auto slot = std::make_unique<MeterSlot>();
        const auto fallback = preset.slots[i].kind;
        slot->setKind ((int) i < savedKinds.size() ? MeterKinds::fromId (savedKinds[(int) i], fallback) : fallback);
        slot->onKindChosen = [this] (MeterKind)
        {
            if (onLayoutChanged)
                onLayoutChanged();
        };
        addAndMakeVisible (*slot);
        slots.push_back (std::move (slot));
    }
}

juce::StringArray GridView::getKindIds() const
{
    juce::StringArray ids;
    for (const auto& slot : slots)
        ids.add (MeterKinds::toId (slot->getKind()));
    return ids;
}

void GridView::update (MeterHub& hub, int source)
{
    const auto context = hub.contextFor (source);
    for (auto& slot : slots)
        if (auto* m = slot->getModule())
            m->update (context);
}

void GridView::resetDisplay()
{
    for (auto& slot : slots)
        if (auto* m = slot->getModule())
            m->resetDisplay();
}

void GridView::resized()
{
    const auto area = getLocalBounds();

    // Same weighted-grid maths as Prism's editor.
    auto edges = [] (const std::vector<float>& weights, int start, int length)
    {
        float total = 0.0f;
        for (float w : weights)
            total += w;

        const int usable = length - gap * ((int) weights.size() - 1);
        std::vector<int> positions { start };
        float running = 0.0f;
        for (size_t i = 0; i < weights.size(); ++i)
        {
            running += weights[i];
            positions.push_back (start + juce::roundToInt (running / total * (float) usable) + gap * (int) (i + 1));
        }
        return positions;
    };

    const auto xs = edges (preset.columnWeights, area.getX(), area.getWidth());
    const auto ys = edges (preset.rowWeights, area.getY(), area.getHeight());

    for (size_t i = 0; i < slots.size(); ++i)
    {
        const auto& spec = preset.slots[i];
        slots[i]->setBounds (juce::Rectangle<int>::leftTopRightBottom (
            xs[(size_t) spec.col], ys[(size_t) spec.row],
            xs[(size_t) (spec.col + spec.colSpan)] - gap, ys[(size_t) (spec.row + spec.rowSpan)] - gap));
    }
}
