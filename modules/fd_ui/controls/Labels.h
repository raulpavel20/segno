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

/// What follows a name while what it names has changes not kept: " •" (the project's name in the
/// title bar while unsaved; a module's preset while it differs from the preset, ADR 0044).
[[nodiscard]] const juce::String& modifiedMark();
/// `name` in the current font and colour, cut to fit `area`, then the modified mark when
/// `modified`: the mark is never cut away.
void paintNameWithMark(juce::Graphics& g, const juce::String& name, bool modified, juce::Rectangle<int> area,
                       juce::Justification justification = juce::Justification::centredLeft);
/// The width paintNameWithMark needs in `font`.
[[nodiscard]] int nameWithMarkWidth(const juce::Font& font, const juce::String& name, bool modified);
} // namespace fd::ui
