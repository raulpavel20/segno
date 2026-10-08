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

/// The mono, for every value, number and readout (fixed width, so changing numbers don't
/// jitter) and for tiny all-caps section labels (design-language.md, "Type").
[[nodiscard]] juce::Font mono(float sizePx, Weight weight = Weight::Regular);
} // namespace fd::ui::fonts
