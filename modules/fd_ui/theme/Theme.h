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
    int meterWidth = 10, meterGap = 10, meterStripPadding = 14; // meter strip: 6 + meters + 8
    int displayInset = 6;       // a display's text and marks, from its well's edges
    int displayTextHeight = 14; // a display's line of text (a legend, a readout, an axis label)
    int displayHandle = 11; // a point dragged on a display (the Compressor's threshold, the Filter's cutoff)
    int sourceHandle = 14;  // a modulation source's handle: a chip, its arrow and name ("Modulation")
    // A Drum Kit's pads (drum-kit.md §5, ui::PadGrid): the gap between pads, a pad's name and note
    // from its edges; the overview's cells (ui::PadOverview), the gap between them.
    int padGap = 4, padInset = 5;
    int overviewGap = 1;
    int power = 14;         // a power toggle's square: a module's, a section's, a rack's chain's
    int headerPadding = 8;
    int headerButton = 16;                          // a header's icon buttons: fold, previous and next preset
    int presetFieldMin = 112, presetFieldMax = 200; // the preset's name field (presets.md §6)

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

/// Colours by role (docs/design/ui-foundations.md, ADR 0023): every colour a view draws.
/// Derived from the base palette in Theme.cpp, the only place colours are mixed.
struct ColourRoles
{
    juce::Colour lane, laneSelected, subLane, strip;
    juce::Colour keyRowLight, keyRowDark, shade;
    // A row outside the key while the piano roll shows the key's rows (piano-roll.md §21.2): its
    // hatching, over the row's own shade.
    juce::Colour keyRowHatch;
    juce::Colour gridBar, gridBeat, gridSub, gridOctave, gridMinor;
    juce::Colour selectionFill, selectionStroke, dropTarget, textHighlight;
    juce::Colour playhead, marker, loopInactive, onClip;
    juce::Colour takeUnused, takeUnusedContent; // the parts of a clip's takes it doesn't play: no clip colour
    juce::Colour keyWhite, keyBlack, keySeam;
    juce::Colour curveFill, selectionCurve, spectrumFill, spectrumLine, spectrumPre, outside, loopTint;
    // The Spectrum's other curves (devices/spectrum/SPEC.md): apart by hue and dash, not colour alone.
    juce::Colour spectrumCompare, spectrumSide, spectrumSnapshot, spectrumReference, spectrumCollision;
    // The Loudness Meter (devices/loudness-meter/SPEC.md): short-term in the value colour,
    // momentary fainter behind it, the target and its band; louder than the target is `warning`.
    juce::Colour loudnessShortTerm, loudnessMomentary, loudnessTarget, loudnessTargetBand;
    // The Tuner (devices/tuner/SPEC.md): in tune, the note and needle; the ±3 cents band.
    juce::Colour tunerInTune, tunerInTuneBand;
    juce::Colour focusRing, hoverWash, dropWell, valueHot, valueText, warning, overlay, shadow;
    juce::Colour warningFill; // behind a warning's text: a module's latency badge
    juce::Colour rackFrame;   // the tray a rack's modules sit in, in the chain (racks.md §5)
    // Automation (docs/design/automation.md §6): the indicator on automated controls. Lanes draw in
    // their own colour (Theme::automationLine and the fill functions).
    juce::Colour automation;
    // MIDI learn (docs/design/midi-learn.md §4): map mode's outlines, a mapped control's source in
    // its corner, the control waiting to learn. A hue no theme's accents or gradient use.
    juce::Colour midiMap, midiMapFill; // the outline and label; the tint over a mapped control
    // Warping (audio-editor.md §7.5): a warp marker's line down through the waveform, under its
    // handle (textPrimary) in the marker strip.
    juce::Colour warpMarkerLine;

    // States.
    float hoverLift = 0.06f, pressedLift = 0.12f;
    float dimAlpha = 0.35f;   // ignored or switched off; never compounded
    float fadedAlpha = 0.4f;  // exists but doesn't play
    float ghostAlpha = 0.28f; // another part's notes behind the clip's own (piano-roll.md §8)
    float echoAlpha = 0.55f;  // each copy's trace behind the one before (modulators.md §9)
    float fillAlpha = 0.14f;  // the flat fill under a curve
    float depthFar = 0.25f, depthNear = 0.6f; // a 3D view's lines, farthest and nearest (atDepth)
};

/// Animation and input constants (docs/design/ui-foundations.md): one feel everywhere.
struct Motion
{
    double panel = 0.04;  // s, time constant: panels opening and resizing (~150 ms to settle)
    double follow = 0.05; // s: the view following the playhead
    double settle = 0.15; // s: detail that waits until motion stops
    double progressDelay = 0.25; // s: work done sooner shows no progress (no flicker)
    double springLoad = 0.5;     // s: a drag held on a tab opens it (a module's page)
};

struct Input
{
    int dragThreshold = 3;      // px: a press becomes a drag
    float edgeGrab = 6.0f;      // px: grabbing an edge (at most a third of the target)
    double wheelScroll = 400.0; // px per wheel unit
    double wheelZoom = 2.0;     // zoom factor per wheel unit (Ctrl)
    double rulerZoom = 0.012;   // log zoom per px dragged vertically on a ruler
    float bendTravel = 60.0f;   // px dragged for a whole bend (automation segments, fades)
    int cursorIconSize = 32;    // px: an icon drawn as a mouse cursor (ui::cursorFor), a system cursor's size
    float valueTravel = 200.0f; // px dragged for a control's whole range (knobs, bars, a display's values)
    float fineFactor = 0.1f;    // how much finer a drag is with Shift held
};

/// The shell's sizes (docs/design/ui-foundations.md).
struct ShellMetrics
{
    int transportHeight = 40;
    int titleBarHeight = 28;    // the title bar: the brand, the menus, the name, the window buttons (ADR 0040)
    int windowButtonWidth = 40; // a window button (minimise, maximise, close), the title bar's height
    int resizeBorder = 4;       // Linux: the resize grips inside the window's edges, outside the content
    int rulerHeight = 24, loopStripHeight = 12;
    int trackHeaderWidth = 180;
    int panelHeaderHeight = 24;
    int padding = 12;
    int rowHeight = 20;
    int propertyLabelWidth = 80;
    // The inspector: its width when it opens and its narrowest, where nothing its rows show is cut
    // (an audio clip's tempo beside ×2 and ÷2 needs 252); then its widest.
    int inspectorWidth = 260, inspectorMaxWidth = 520;
    int listRowHeight = 28;
    int listTextInset = 10; // a list row's text from its edges (palette and preset rows, their headings)
    int popoverWidth = 560, popoverTop = 56;
    int formPopoverWidth = 420;   // a popover of property rows (the export panel)
    int presetListWidth = 320,
        presetListRows = 10; // a module's preset list (presets.md §7): rows shown before it scrolls
    int listIconSize = 12;   // an icon button at the end of a list row (a preset's star)
    // The Settings page (settings.md §4): its index, its column (at most this wide), a setting's
    // name and control columns.
    int settingsIndexWidth = 160, settingsColumnWidth = 720, settingsLabelWidth = 200,
        settingsControlWidth = 280;
    // A shortcut in Settings > Keys, at the controls' right edge, so long names get the rest.
    int settingsKeyWidth = 160;
    int browserIndent = 16;     // a browser row in from its parent: an icon and its gap (ADR 0046, 0059)
    int browserWaveHeight = 32; // the preview strip's waveform
    int toastHeight = 36, toastWidth = 420;
    int keyboardWidth = 56, velocityHeight = 56;
    // One octave of keys (ui::OctaveKeys: the Key popover's root, key-and-scale.md §6): a white key's
    // width and height, and a black key's (wide enough for its name, "C♯", clear of its edges).
    int octaveKeyWidth = 28, octaveKeyHeight = 52, octaveBlackKeyWidth = 20, octaveBlackKeyHeight = 32;
    int railWidth = 44, splitter = 5;
    int barPadding = 8, barInset = 6; // a bar's (the transport's) edges: sides, top and bottom
    int itemGap = 4, groupGap = 12;   // between items in a group, between groups
    int buttonSize = 28;              // icon buttons in bars and rails
    int toolIconSize = 16;            // a tool's icon in its tile (ui::Tile)
    int toggleIconSize = 14;          // an icon before a toggle's label (Toggle::setIcon)
    int toolsDockFrom = 900;          // the clip view's Tools pane docks from this width, else floats
    int fieldHeight = 24;             // text fields (search)
    // Text inside controls and rows.
    int fieldTextInset = 6;     // text from a combo box's, input button's or field's left edge
    int buttonTextPadding = 8;  // a text button's padding on each side of its label (ui::Toggle::widthFor)
    int propertyFieldInset = 1; // a field in a property row, from the row's top and bottom
    int lightSize = 6;          // an activity light (MIDI input, assistants)
    int hatchSpacing = 6;       // between hatching's lines (ui::paintHatch)
    // Clips and lanes in time views.
    int clipGap = 1;              // a clip from its row's top, from the separator line below it, and from a clip touching its end (the seam)
    int clipOutline = 1;          // a selected clip's outline; a fade's square sits inside it
    int clipTitleHeight = 15;     // a clip's title strip: at most this...
    float clipTitleShare = 0.35f; // ...and at most this share of the clip's height
    int clipTitleMinText = 10;    // a title strip shorter than this shows no text
    int clipTextInset = 4;        // a clip's name and badge, from its edges
    int laneTextInset = 8;        // text in a lane (a hint, a take's name), from its visible left edge
    int takeStripHeight = 24;     // a take's strip under its clip (recording.md §6)
    int songRowHeight = 40;       // the song row (ADR 0049, arranger.md §5): its height by default...
    int songRowMinHeight = 32;    // ...and its least
    int songTitleHeight = 16;     // its title band, where the sections' names are
    int songOverviewInset = 3;    // its miniature of the tracks, clear of the title band and the divider
    int songBracket = 2;          // a section's bracket: its bar along the top and the tick down each end...
    int songBracketGap = 2;       // ...and the gap between two brackets that touch
    int takeIconSize = 12;        // the audition button in a take strip's name chip (ADR 0038)
    int clipButtonSize = 12;      // a button on a clip: cancel its bounce (menu-bar.md §15)
    int dropLine = 2;             // where a dragged module or row lands: a line in `active`
    int dragScrollEdge = 24;      // a drag this near a scrolled view's edge scrolls it...
    int dragScrollSpeed = 12;     // ...this far at a time, at most (a modulator's handle, modulators.md §6)
    int rackInset = 6;            // a rack's frame around its panels in the chain (racks.md §5)...
    int rackLabelHeight = 16;     // ...and its strip under them, naming the chain shown
    int progressHeight = 2;       // the progress line along the bottom of what's being worked on
    int fadeHandleSize = 7;       // a fade's or crossfade's handle on a clip (fades.md §3)
    int loopNotch = 4;            // a looped clip's notch at each seam: its depth, half its width (clip-loops.md §5)
    // A selection's time handle in a ruler (piano-roll.md §9.2, audio-editor.md §7.6): its width, and
    // how far its top stays under the ruler's middle (its corners rounded as much).
    int timeHandleWidth = 4, timeHandleInset = 1;
    // The audio editor's marker strip under its ruler (audio-editor.md §4, §7.4, §7.5): a warp
    // marker's handle (a pin pointing down at its moment, inset from the strip's top and foot), and a
    // transient's tick at the strip's foot.
    int markerStripHeight = 16, warpMarkerWidth = 9, warpMarkerInset = 2, transientTick = 6;
    int dashLength = 4; // a dashed line's dashes and gaps (an automation lane's value, a transient's guide)
    float shadowRadius = 18.0f, shadowOffset = 6.0f;
};

/// Design tokens (docs/design/ui-principles.md §4–5, ADR 0014). The only source of colours,
/// sizes and type sizes for every component. Flat: gradients are only the listed accent effects
/// (ui-principles §4): modulation (effectStart/effectEnd) and the fill under an automation lane's
/// curve (in the lane's colour, automationFillTop/Bottom). The default theme is "Steel"
/// (docs/design/design-language.md).
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
    int faderCapHeight = 12; // a vertical fader's cap (the mixer view)
    float radiusPanel = 4.0f;
    int devicePanelHeight = 232;  // = deviceHeaderHeight + deviceGrid.bodyHeight()
    int deviceHeaderHeight = 28;
    DeviceGrid deviceGrid;
    float knobMaxDiameter = 38.0f;
    float knobValueSize = 10.0f; // every knob's value text (mono), unless it can't fit
    float knobArcThickness = 2.5f;
    float knobRingThickness = 1.5f, knobRingGap = 1.0f; // a modulated knob's ring, outside its arc

    ColourRoles roles;
    Motion motion;
    Input input;
    ShellMetrics shell;

    // Role functions: colours that depend on content (docs/design/ui-foundations.md).
    /// Clip/track colour `index` of the palette (wraps).
    [[nodiscard]] juce::Colour clipColour(int index) const;
    /// A clip's body: its colour at low brightness (a step up when selected).
    [[nodiscard]] juce::Colour clipBody(juce::Colour clip, bool selected) const;
    /// A clip the main lane doesn't play (a take lane's, recording.md §6): its colour faded into
    /// the sub-lane, then drawn like any clip.
    [[nodiscard]] juce::Colour clipDimmed(juce::Colour clip) const;
    /// Content drawn on a clip (waveform, notes): its colour towards white.
    [[nodiscard]] juce::Colour clipContent(juce::Colour clip) const;
    /// A note: from the control tone to the clip colour by velocity; lighter when selected.
    [[nodiscard]] juce::Colour noteFill(juce::Colour clip, float velocity, bool selected) const;
    /// Content that exists but doesn't play.
    [[nodiscard]] juce::Colour faded(juce::Colour c) const { return c.withMultipliedAlpha(roles.fadedAlpha); }
    /// Another track's notes in the piano roll, faint behind the clip's own (piano-roll.md §8).
    [[nodiscard]] juce::Colour ghost(juce::Colour c) const { return c.withMultipliedAlpha(roles.ghostAlpha); }
    /// A modulator's copy `step` behind the first (0 is the first): each a step fainter.
    [[nodiscard]] juce::Colour echo(juce::Colour c, int step) const
    {
        return c.withMultipliedAlpha(std::pow(roles.echoAlpha, static_cast<float>(std::max(0, step))));
    }
    /// What's gone but still shown a moment (the Tuner's last note after the sound stops), or
    /// switched off: at `dimAlpha`, never compounded.
    [[nodiscard]] juce::Colour dimmed(juce::Colour c) const { return c.withMultipliedAlpha(roles.dimAlpha); }
    /// The flat fill under a curve drawn in `c`.
    [[nodiscard]] juce::Colour fill(juce::Colour c) const { return c.withMultipliedAlpha(roles.fillAlpha); }
    /// A line of a 3D view (a wavetable's frames) `nearness` of the way from the back (0) to the
    /// front (1): the farther, the fainter.
    [[nodiscard]] juce::Colour atDepth(juce::Colour c, float nearness) const
    {
        return c.withMultipliedAlpha(roles.depthFar +
                                     (roles.depthNear - roles.depthFar) * std::clamp(nearness, 0.0f, 1.0f));
    }
    /// Hover and press states of any surface or fill.
    [[nodiscard]] juce::Colour lift(juce::Colour c, bool hovered, bool pressed) const;
    /// An automation lane drawn in its colour (automation.md §4): its line and points (faded while
    /// off), and the reserved gradient under the curve, from the line to the lane's floor (a flat
    /// faint fill while off).
    [[nodiscard]] juce::Colour automationLine(juce::Colour lane, bool on) const;
    [[nodiscard]] juce::Colour automationFillTop(juce::Colour lane, bool on) const;
    [[nodiscard]] juce::Colour automationFillBottom(juce::Colour lane, bool on) const;
    /// A spectrogram's point (devices/spectrum/SPEC.md, stage 2): `level` 0 (the bottom of the
    /// range) to 1 (its top), from the display's background through the analyser's grey to near
    /// white; tinted with the collision colour where it collides with a compared track.
    [[nodiscard]] juce::Colour spectrogram(float level, bool collides) const;

    // Type scale (px, em size).
    float textXs = 11.0f;
    float textSm = 12.0f;
    float textMd = 13.0f;
    float textLg = 15.0f;
    float textXl = 18.0f;
    float textDisplay = 40.0f; // the one figure a display is about (the Tuner's note)

    [[nodiscard]] static const Theme& current() noexcept;
    static void setCurrent(const Theme& theme) noexcept;

    /// Available themes. The first ("Steel") is the default; the others are alternatives kept
    /// for comparison in tools/gallery.
    [[nodiscard]] static const juce::Array<Theme>& candidates();
};
} // namespace fd::ui
