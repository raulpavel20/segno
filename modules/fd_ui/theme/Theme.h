#pragma once

namespace fd::ui
{
/// Design tokens (docs/design/ui-principles.md §4–5, ADR 0014). The only source of colours,
/// sizes and type sizes for every component. Flat: no token is meant for a gradient except
/// effectStart/effectEnd, which are reserved for the listed accent effects (automation,
/// modulation). The default theme is "Steel" (docs/design/design-language.md).
struct Theme
{
    juce::String name;
    juce::String description;

    // Surface ladder: close, neutral steps; higher = lighter.
    juce::Colour surface0; // app background, arrangement canvas
    juce::Colour surface1; // panels (browser, detail, inspector)
    juce::Colour surface2; // device bodies, cards
    juce::Colour surface3; // controls: knob tracks, off buttons; hover
    juce::Colour line;     // the rare divider

    juce::Colour textPrimary;
    juce::Colour textSecondary;
    juce::Colour textTertiary;

    // Colour roles.
    juce::Colour value;    // knob arcs, slider/fader fills, curve lines
    juce::Colour active;   // on states, device power, selection
    juce::Colour onActive; // text/icons drawn on an `active` fill
    juce::Colour effectStart;
    juce::Colour effectEnd; // the only gradient pair (automation, modulation)
    juce::Colour accentInstrument;
    juce::Colour accentMidi;
    juce::Colour accentModulation;
    juce::Colour record;

    juce::Colour meterNormal;
    juce::Colour meterHot;
    juce::Colour meterClip;

    /// Flat clip/track colours, tuned for the dark canvas.
    juce::Array<juce::Colour> clipColours;

    // Geometry (logical px).
    int grid = 4;
    float radiusControl = 2.5f;
    float radiusPanel = 4.0f;
    int devicePanelHeight = 168;
    int deviceHeaderHeight = 24;
    float knobMaxDiameter = 38.0f;
    float knobArcThickness = 2.5f;

    // Type scale (px, em size).
    float textXs = 11.0f;
    float textSm = 12.0f;
    float textMd = 13.0f;
    float textLg = 15.0f;
    float textXl = 18.0f;

    [[nodiscard]] static const Theme& current() noexcept;
    static void setCurrent(const Theme& theme) noexcept;

    /// Available themes. The first ("Steel") is the default; the others are alternatives kept
    /// for comparison in tools/gallery.
    [[nodiscard]] static const juce::Array<Theme>& candidates();
};
} // namespace fd::ui
