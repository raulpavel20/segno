#pragma once

namespace fd::ui::fonts
{
/// Weights of the bundled Inter variable font ('wght' axis).
enum class Weight
{
    Regular = 400,
    Medium = 500,
    SemiBold = 600,
    Bold = 700
};

/// The bundled typeface at a given weight (cached; message thread only).
[[nodiscard]] juce::Typeface::Ptr typeface(Weight weight = Weight::Regular);

/// `sizePx` is the em size, as in CSS. `tabular` enables tabular numerals for values.
[[nodiscard]] juce::Font font(float sizePx, Weight weight = Weight::Regular, bool tabular = false);
} // namespace fd::ui::fonts
