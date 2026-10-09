#pragma once

namespace fd::ui
{
/// A tiny all-caps label in the mono (design-language.md, "Type"): section titles, sub-labels,
/// meter captions. `colour` defaults to textTertiary.
void paintCapsLabel(juce::Graphics& g, const juce::String& text, juce::Rectangle<float> area,
                    juce::Justification justification = juce::Justification::centredLeft,
                    std::optional<juce::Colour> colour = std::nullopt);

/// The font of paintCapsLabel (for measuring).
[[nodiscard]] juce::Font capsLabelFont();
} // namespace fd::ui
