#pragma once

namespace fd::ui
{
/// The module grid (docs/design/device-panels.md, ADR 0022): every module panel's geometry.
/// Panels place controls on cells; nothing in a panel computes its own positions.
struct DeviceGrid
{
    int column = 48, columnGap = 4; // a cell's width and the gap between columns
    int row = 20, rowGap = 8;       // a line control's height and the gap between rows
    int rows = 6;                   // content rows: two knob rows
    int sectionPadding = 12;        // on each side of a section's content
    int titleHeight = 20, titleGap = 4;
    int bodyTop = 8, bodyBottom = 12;
    int knobRows = 3;               // a knob is 1 column x 3 rows
    int knobName = 16, knobValue = 16; // the knob block: name strip, arc area, value strip
    int collapsedWidth = 24;        // a collapsed section
    int foldedWidth = 28;           // a folded module (DAW chain)
    int meterWidth = 10, meterGap = 6, meterStripPadding = 14; // meter strip: 6 + meters + 8
    int headerPadding = 8;

    /// Width of `columns` cells and the gaps between them.
    [[nodiscard]] constexpr int width(int columns) const noexcept { return columns * (column + columnGap) - columnGap; }
    /// Height of `rowCount` rows and the gaps between them.
    [[nodiscard]] constexpr int height(int rowCount) const noexcept { return rowCount * (row + rowGap) - rowGap; }
    /// A section's width: its columns plus padding on both sides.
    [[nodiscard]] constexpr int sectionWidth(int columns) const noexcept { return width(columns) + 2 * sectionPadding; }
    [[nodiscard]] constexpr int meterStripWidth(int meters) const noexcept
    {
        return meters * meterWidth + (meters - 1) * meterGap + meterStripPadding;
    }
    /// Top of the content rows, from the top of the body (below the header).
    [[nodiscard]] constexpr int contentTop() const noexcept { return bodyTop + titleHeight + titleGap; }
    /// The body's height: padding, title, content, padding.
    [[nodiscard]] constexpr int bodyHeight() const noexcept { return contentTop() + height(rows) + bodyBottom; }
    [[nodiscard]] constexpr int knobHeight() const noexcept { return height(knobRows); }
};

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
    int devicePanelHeight = 232;  // = deviceHeaderHeight + deviceGrid.bodyHeight()
    int deviceHeaderHeight = 28;
    DeviceGrid deviceGrid;
    float knobMaxDiameter = 38.0f;
    float knobValueSize = 10.0f; // every knob's value text (mono), unless it can't fit
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
