#pragma once

namespace fd::ui::fonts
{
/// Weights on the 'wght' axis of both bundled variable fonts.
enum class Weight
{
    Regular = 400,
    Medium = 500,
    SemiBold = 600,
    Bold = 700
};

/// The bundled sans (Inter) at a given weight (cached; message thread only).
[[nodiscard]] juce::Typeface::Ptr typeface(Weight weight = Weight::Regular);
/// The bundled mono (JetBrains Mono) at a given weight (cached; message thread only).
[[nodiscard]] juce::Typeface::Ptr monoTypeface(Weight weight = Weight::Regular);

/// The sans, for names and text. `sizePx` is the em size, as in CSS.
[[nodiscard]] juce::Font font(float sizePx, Weight weight = Weight::Regular);

} // namespace fd::ui::fonts

namespace fd::ui
{
/// Named text styles (docs/design/ui-foundations.md, ADR 0023). Views use these; only fd_ui
/// calls fonts::font / fonts::mono directly.
enum class TextStyle
{
    Title,       // Inter 12 semibold: module and plugin names, popover titles
    Name,        // Inter 12 medium: track, clip and action names in lists
    Body,        // Inter 12: messages, property values, empty states
    Label,       // Inter 11 medium: control labels
    Caption,     // Inter 11: units, categories, header captions, hints
    Control,     // Inter 11 semibold: toggles, segments, combos, tabs
    Field,       // Inter 13: text fields and menus
    Section,     // Mono 10 semibold, all caps: section and panel titles, sub-labels
    Value,       // Mono 11 medium: readouts, ruler numbers, property numbers
    ValueSmall,  // Mono 10: knob values, display axis labels
    ValueMedium, // Mono 13 medium: number fields
    ValueLarge,  // Mono 15 medium: the transport position
    Micro        // Mono 9 semibold: labels inside small graphics
};

[[nodiscard]] juce::Font font(TextStyle style);
} // namespace fd::ui

namespace fd::ui::fonts
{
/// The mono, for every value, number and readout (fixed width, so changing numbers don't
/// jitter) and for tiny all-caps section labels (design-language.md, "Type").
[[nodiscard]] juce::Font mono(float sizePx, Weight weight = Weight::Regular);
} // namespace fd::ui::fonts
