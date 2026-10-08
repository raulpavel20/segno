#pragma once

namespace fd::ui
{
/// Flat toggle: dim surface when off, solid `active` fill when on (ui-principles §4, ADR 0014).
class Toggle : public juce::Button
{
public:
    explicit Toggle(const juce::String& text = {});

    void paintButton(juce::Graphics& g, bool highlighted, bool down) override;
};
} // namespace fd::ui
