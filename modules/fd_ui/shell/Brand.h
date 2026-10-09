#pragma once

namespace fd::ui
{
/// The product's identity (ADR 0024): its name, its mark (the segno sign, the point the music
/// returns to) and the two together. The mark is drawn from one vector definition everywhere:
/// the top left of the DAW, the app icon, docs.
namespace brand
{
inline constexpr const char* name = "Segno";
} // namespace brand

/// The mark alone, fitted (aspect kept, centred) into the square around `area`'s centre.
void paintMark(juce::Graphics& g, juce::Rectangle<float> area, juce::Colour colour);

/// The mark in the accent, then the name in capitals (the Wordmark style), left-aligned in
/// `area` and centred vertically; the mark is as tall as `area`.
void paintBrand(juce::Graphics& g, juce::Rectangle<int> area);
/// The width paintBrand takes for a given height.
[[nodiscard]] int brandWidth(int height);

/// The app icon at `size` pixels: the mark on a dark rounded square.
[[nodiscard]] juce::Image appIcon(int size);
} // namespace fd::ui
