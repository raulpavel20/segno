# Module panels: the layout kit

Every instrument and effect panel is built from the same parts on the same grid, so all modules look and behave as one product ([ADR 0022](../decisions/0022-module-panels.md), [ui-principles §7](ui-principles.md)). This document is the layout spec. The values live as tokens in `fd_ui::Theme` (`DeviceGrid`), and the layout lint (`tests/panels`) enforces them.

In the UI, instruments and effects are **modules**. Code and docs say *device* (`fd::Device`, `DevicePanel`).

## Anatomy

```
┌─ ‹ ● Saturator ──────────────────────────────── Quality 4× ▾ ┐  header 28
│                                                               │  padding 8
│  SHAPE                    │ COLOR ⏻ │ OUTPUT │ IN OUT         │  titles 20 (+4)
│  ┌──────────────────┐  ◜◝ │  ◜◝     │  ◜◝    │ ▮  ▮           │
│  │ transfer curve   │ Drive│ Freq   │ Output │ ▮  ▮           │  row 0–2: knob row
│  │ 3 × 4 cells      │ 12 dB│ 1.0 kHz│ 0.0 dB │ ▮  ▮           │
│  │                  │  ◜◝ │  ◜◝     │  ◜◝    │ ▮  ▮           │
│  └──────────────────┘ Bias │ Gain   │ Mix    │ ▮  ▮           │  row 3–5: knob row
│  [Soft] [Warm] [Tape] 0.00 │ +3 dB  │ 100 %  │ ▮  ▮           │
│  [Hard] [Fold] [Rect]      │        │        │                │  content 160
│                                                               │  padding 12
└───────────────────────────────────────────────────────────────┘  = 232
```

- **Header (28 px), left to right:**
  - fold chevron (DAW only);
  - power;
  - name;
  - the **presets** ([presets.md](presets.md), ADR 0044), right-aligned before the header settings: previous and next, the preset's name (`deviceGrid.presetFieldMin` to `presetFieldMax` wide, taking the room the name leaves), the A/B toggle. Where the host has none (the gallery, snapshots) they don't show;
  - **header settings** for module-wide quality and display options, which never change the sound's character (oversampling quality, analyzer), right-aligned. A setting that matters only in some modes may show only then (`DevicePanel::showHeaderWhen`: the EQ's Quality, only while there's a delay); the panel's width keeps room for it, so nothing else moves;
  - for a module with a sidechain input, the **Sidechain** picker, first among the header settings, 128 px wide whatever the track names ([ADR 0025](../decisions/0025-sidechain-routing.md)). Its choices come from the host: None and the tracks in the DAW, Off and External in a plugin.
  - for a module whose delay is a choice (the EQ's linear phase), the **latency badge** at the right end while it adds one: "+107 ms" in `warning` on `warningFill`, its tooltip saying what that means for playing live (`DevicePanel::showLatency`). It takes no room while there's no delay. A header setting that changes the delay names each choice's delay in its menu (`ParamComboBox::setMenuText`), the box showing only the choice.

  The header background is the drag handle for reordering in the DAW.
- **Body (204 px):**
  - 8 px top padding;
  - the **section title row** (20 px, then a 4 px gap);
  - **content:** 6 rows, 160 px;
  - 12 px bottom padding.
- **Sections** sit side by side, left to right, each with 12 px padding on either side. A **1 px line** in the `line` colour separates neighbouring sections, centred in the 24 px between their contents. A section that matters only sometimes may show only then (`DevicePanel::showSectionWhen`: a rack's CHAINS, from its second chain, [racks.md](racks.md) §5a); unlike a header setting, it takes its room only while it shows, and the panel's host lays the chain out again when it comes or goes.
- **The folded strip** reads the module's name upwards; a module may add what it holds after the name, fainter (`DevicePanel::setFoldedDetail`: a rack's "4 modules"). A rack's panel shows the rack's name in place of the module's (`setDisplayName`).
- **Meter strip:** modules with meters end with a strip at the right edge.
  - Widths: 24 px for one meter, 44 px for an in/out pair.
  - Labels: `OUT`, or `IN` and `OUT`, in the title row.
  - Meters are 10 px wide and fill the content height.

## The grid

| Token | Value | Note |
|---|---|---|
| Panel height | 232 | `Theme::devicePanelHeight` |
| Header | 28 | `Theme::deviceHeaderHeight` |
| Header buttons | 16 | `DeviceGrid::headerButton`: fold, previous and next preset |
| Preset field | 112 to 200 | `DeviceGrid::presetFieldMin`, `presetFieldMax` (ADR 0044) |
| Column | 48 wide, 4 apart | span of *n* columns = 52*n* − 4 (48, 100, 152, 204, 256, 308…) |
| Row | 20 tall, 8 apart | span of *m* rows = 28*m* − 8 (20, 48, 76, 104, 132, 160) |
| Content | 6 rows | 160 px |
| Section | *n* columns | 52*n* + 20 wide (12 px padding each side) |
| Section title | 20 + 4 | mono caps 10 px semibold, `textTertiary` |
| Collapsed section | 24 | title rotated, reading upwards |
| Folded module | 28 | DAW only |
| Meter strip | 24 / 44 | one meter / in and out |
| Display text | 6 inset, 14 tall | `DeviceGrid::displayInset`, `displayTextHeight`: a display's legend, readouts and axis labels |
| Display handle | 11 | `DeviceGrid::displayHandle`: a point dragged on a display (the Compressor's threshold, the Filter's cutoff), filled `surface3` and ringed in the value colour, or `active` while held or hovered |
| Source handle | 14 tall | `DeviceGrid::sourceHandle`: a modulation source's handle, a chip with its arrow and name ("Modulation", below) |
| Power | 14 | `DeviceGrid::power`: a power toggle's square (a module's, a section's, a rack's chain's) |
| Modulation ring | 1.5, 1 apart | `Theme::knobRingThickness`, `knobRingGap`: the ring outside a modulated knob's arc |

Three rows are exactly one knob tall (76 px), so a knob lines up with a stack of three line controls beside it.

## Controls on the grid

Each control is placed by its top-left cell and its span: `(column, row, columns, rows)`.

| Control | Span | Rules |
|---|---|---|
| **Knob** | 1 × 3 (48×76) | Rows 0 or 3 only. Fixed block: name 16, arc area 44 (38 px arc), value 16. The value text fits its column ([design-language.md](design-language.md), "Type"). |
| **Toggle** | *n* × 1, *n* ≥ 1 | Text + 8 px must fit. Without text it's an on/off dot, centred (a source's On in a list). |
| **Segmented** | whole cells per segment, 1 row per line of segments | 4 px between segments, 8 px between lines (the grid's own gaps). Text + 6 px must fit a segment. |
| **Selector** | as Segmented | Which of several the sections after it show (an oscillator, an LFO): a segmented control's look, but view state, never a parameter (`ui::Selector`). |
| **Combo** | *n* × 1, *n* ≥ 2 | The widest choice + 26 px (padding and chevron) must fit. Where nothing else names it, it shows its parameter's short name inside, before its choice, as a bar does (`ParamComboBox::setInlineName`): the name + 8 px is added. A combo for a setting that isn't a parameter (a module editor's Fill, Grid) is a `ui::ChoiceBox`, the same look and rule, its name set with `setInlineName(name)`. |
| **Bar** (value bar) | *n* × 1, *n* ≥ 2 | Name + widest value + 16 px must fit. |
| **Display** (curve, envelope, waveform, spectrum) | any *n* × *m* | Its own label, if any, sits inside the well. A display can carry one **accessory** (e.g. the Mod envelope's Cycle toggle) in its label line. Analysers (the EQ, the Spectrum) draw with `ui::SpectrumPlot`: one log-frequency axis, grid and set of strokes, so they read alike. |
| **Sub-label** | *n* × 1 | Mono caps, like a section title, for a named group inside a section (the LFO inside MODULATION). Use sparingly; a new section is usually clearer. |

- Every cell a control covers is its own: controls never overlap.
- A knob's name must be at most 46 px in Inter 11 medium. Use the parameter's short name (a display name, which can change; IDs can't).
- **A cut filter's curve:** a low and a high cut with a power switch show as `ui::CutFilterCurve` (the Delay's feedback filter, the Reverb's filter going in): one look, dragged near a cutoff to move it.
- **A spectrum behind a curve:** the device's FIFO through `ui::FifoSpectrum` (the shared analyser, 150 ms averaging), drawn faint on a `ui::SpectrumPlot` in the spectrum roles (the EQ's, the Filter's).
- **One knob, two parameters:** where a switch picks which of two parameters a knob turns (the Delay's Left and Right turn milliseconds, or note values while Sync is on), the knob follows the switch (`ParamKnob::follow`), checked on every refresh. Its name, value text, default, steps and automation follow the parameter it turns now; the two never share a knob's cell by overlapping.
- A section's title, plus its power toggle, must fit its content width.

## Sections

- **Identity:**
  - **Id:** lowercase, stored in projects and plugin state, so never renamed (`docs/ids.md`).
  - **Title:** short, usually one noun (SHAPE, COLOR, OUTPUT). A section can retitle itself (the EQ's BAND 3).
- **Power:** optional, in the title row, bound to an on/off parameter. When off, the section's content is drawn at 35 % (the title and the power stay bright). Controls the module dims for other reasons inside it aren't dimmed twice.
  - Where what it switches isn't a parameter (a rack's Morph, racks.md §18.3), `PanelSection::setPowerSwitch` takes whether it's on and what a click asks for instead; it looks and dims the same.
- **Collapse:** click the title, or press Return when it has focus. Alt+click collapses or expands every section of the module.
  - A collapsed section is a 24 px strip with its title rotated. The power toggle stays at the top of the strip.
  - Its controls keep working (automation, MCP); only the view changes.
- **Grouping guideline:** a section is a group the user thinks of as one thing: an oscillator, a filter, the envelopes, the output. Merge small related groups instead of making many one-column sections. One-column sections are for groups with their own power (COLOR) or for the output.

## Folding and widths

- A panel's width is computed: the sum of its sections (expanded or collapsed) plus the meter strip. It is never less than the header needs; where the header needs more, the meter strip sits at the right edge and the room is left after the last section.
- **In the DAW chain:** the fold chevron folds the whole module to a 28 px strip showing its power and its name, rotated. Click the strip to unfold it; drag it to reorder.
- **Width changes:**
  - the DAW chain relays out at once;
  - a plugin editor asks its host to resize, and lays the panel out from the left at whatever width the host grants;
  - on a screen narrower than the editor, the editor scrolls horizontally.

## Pages

For a module too wide to read without scrolling ([ADR 0051](../decisions/0051-pages-for-very-wide-modules.md), the product owner, 2026-10-13). Pages live alongside sections; they never replace them.
- **Who:** a module opts in when its sections side by side would pass about 1 600 px. Today: the Wavetable Synth. Every other module keeps one row of sections.
- **What a page is:** a named group of the module's sections ("Oscillators", "Filter & Amp", "Modulation"). Every section belongs to exactly one page; none is shown on every page. Sections collapse inside a page as anywhere.
- **The tabs:** in the header, right after the module's name and before the presets, in the detail panel's tab look: the shared `TabRow` (`Control` text with a text button's padding; the active tab in `textPrimary`, a hovered one in `textSecondary`, the rest in `textTertiary`). A click turns the page.
- **One width:** the module is as wide as its widest page (with that page's sections as collapsed or expanded as they are). A narrower page leaves room after its last section, as the header rule does, so the modules after it never move when the page turns.
- **Turning pages from elsewhere:**
  - dragging a modulation source onto a tab and holding it there opens that page;
  - when the DAW shows a parameter in its module (an automation lane's name, an assistant's change), the module turns to the page that holds it.
- **Mouse and keys** (ADR 0040): View > Next Module Page (Ctrl+Alt+Page Down) and Previous Module Page (Ctrl+Alt+Page Up) act on the selected module; the tabs are the mouse's way.
- **Folded,** a module shows its strip whatever its page.
- **Declaring:** `addPage("oscillators", "Oscillators")` before the first section starts a page; each `addSection` after it is on that page, up to the next `addPage`. A display placed with `place()` that sets a parameter says so with `holdsParam(param)`, so `showParameter` finds its page (controls placed with `knob`, `combo` and the rest are known already). A panel that shows one of several (the selected oscillator) overrides `showParameter` to select it first.

## Modulation

For a module with its own modulation (the Wavetable Synth; M4's DAW-wide modulators reuse it). The routes live in the module's parameters (its matrix); the panel shows and edits them through the host binding, like any edit. Built once in `fd_device_ui`: the panel gives the kit a `ModulationModel` (its sources, and its routes to parameters).
- **Rings:** a modulated knob shows the range it moves as a ring just outside its arc (`knobRingThickness`, `knobRingGap` from the arc), inside its own cell, in the modulation gradient (`effectStart` → `effectEnd`; ui-principles §4). The range is the sum of its routes: from the value to the value plus the amount, both ways for a source that swings both ways (an LFO, a random source), clamped to the knob's ends.
- **Three ways to make a route**, each enough on its own (the product owner, 2026-10-14: a bare arrow in a corner wasn't found):
  1. **Drag a source's handle onto a knob.** A display's source shows its handle as a named chip in the display's top-right corner ("⤧ LFO 1", "⤧ Amp Env"): the four-way arrow and the source's name on `surface3`, `deviceGrid.sourceHandle` tall, lit in `effectEnd` while hovered. Dragging the display itself still edits it (the curve, the envelope); the chip is what's picked up. On a knob that's a source (a macro), its name is the handle: the pointer becomes a hand over it, and the name lights.
  2. **Right-click a knob: Modulate with ▸**, the module's sources by group (envelopes, LFOs, random sources, macros, what's played), makes a route from that source at 50 %. This is how a macro is set to move a knob without dragging.
  3. **The matrix's Add**, then its source and target.
- **While a source is dragged,** every knob it can move shows a faint ring over its whole sweep (`textTertiary`), so it's clear where it can go; the knob under the pointer lights (`active`), and letting go there makes a route at 50 % (the first free route in the matrix; when none is free, nothing, and the chip's tooltip says the matrix is full). A knob on another page: hold the drag on its page tab for `motion.springLoad` (500 ms) and the page turns. Escape cancels.
- **Hover:** hovering a source's handle shows only its rings, the others dimmed; hovering a modulated knob lights the handles of the sources that move it.
- **Setting the amount:** drag the ring (the band outside the arc, where the pointer becomes the resize cursor): up and right more, down and left less, Shift finely, as a knob. It sets the route of the source last hovered, or the knob's first route. The matrix sets any route exactly.
- **Right-click a modulated knob:** its routes ("LFO 1 → 30 %"), each with Remove, then Modulate with, after its automation items.
- **What the lint checks:** rings stay inside their cell; handles never overlap a display's label.

## A module's editor

For a module whose editing needs more room than six rows: the Wavetable's table editor, later a sampler's zones ([ADR 0053](../decisions/0053-a-modules-editor-in-the-detail-panel.md)). A `ui::ModuleEditor`:

- **The panel asks the host for it** (`DeviceUiContext::showEditor`): the DAW shows it in the detail panel as a view of its own, the plugins under the panel, the window grown for it. The host draws its header: the tab (`tabName()`) with an × (`closeTooltip()` says what closing does), what it edits (`title()`), the editor's commands. It closes with the ×, or when the module goes.
- **A card like the panel, in panes side by side:** titled columns on the module grid (`addPane(title, columns)`): the panes at the sides a number of columns wide, the one marked flexible (0 columns) taking the width left. The padding, title row, rows and gaps are the panel's, so its controls are the panel's controls and line up as theirs do.
- **Placed in cells** (`place(pane, component, column, row, columns, rows)`), as in a section. A negative row counts from the last row, and a span of 0 or less runs to the bottom (or the right edge) less that many: a view that grows with the editor's height, a strip kept at the bottom. A component can sit in a pane's title row (`placeInTitle`: a "+" at its right end, a view's tabs after its title). Sub-labels name groups of rows.
- **Its height:** at least the rows its panes need (`setMinimumRows`), which is also the height it opens at; taller, flexible spans grow. The DAW drags it taller at the panel's top edge.
- **The host also gives it** a held note for a Preview (`DeviceUiContext::playNote`) and the user's Segno folder (`segnoFolder`). The panel's menu-bar commands reach it through `DevicePanel::performCommand`, audio handed to the module through `takeAudio`, and a file dropped on the module from the browser through `takeFile`.
- **Its layout lint** (as the panels', `tests/panels/TableEditorTests.cpp`) runs at its narrowest and a wide width, at its least height and taller, in every state: every placement inside its pane's content and clear of the others, the title rows' components clear of the titles, every text fitting its control.

## View state

| Host | Stored in | Rules |
|---|---|---|
| DAW | the module's plugin tree, property `fdPanel` (JSON) | Saved with the project; never an undo step, never an unsaved change, never a snapshot rebuild |
| Plugin | top-level `"view"` key in the plugin state | Written only when not empty, so state without it is byte-identical; presets and `fd-render` ignore it |

Format: `{"collapsed": ["color", "shape"], "folded": true, "page": "modulation"}`. Ids are sorted, and default values are omitted (the first page is the default). Unknown ids are ignored. Page ids, like section ids, are stored and never renamed.

## Keyboard

- Tab moves through section titles and controls left to right.
- Return on a section title collapses or expands it.
- Every control keeps its own rules (§7): type a number, use the arrows.
- In the DAW: an action folds the selected module, another expands all of its sections (both in the command palette).

## Declaring a panel

Panels declare sections and placements; the kit does the geometry.

```cpp
SaturatorPanel::SaturatorPanel(ui::DeviceUiContext& ctx) : DevicePanel(ctx)
{
    auto& shape = addSection("shape", "Shape", 4);
    shape.place(curve_, 0, 0, 3, 4);                    // the transfer curve, 3 × 4 cells
    shape.segmented(Saturator::Curve, 0, 4, 3, 2);      // 6 curves, 3 per row
    shape.knob(Saturator::Drive, 3, 0);
    shape.knob(Saturator::Bias, 3, 3);

    auto& color = addSection("color", "Color", 1, Saturator::Color); // with a power toggle
    color.knob(Saturator::ColorFreq, 0, 0);
    color.knob(Saturator::ColorGain, 0, 3);

    auto& out = addSection("output", "Output", 1);
    out.knob(Saturator::Output, 0, 0);
    out.knob(Saturator::Mix, 0, 3);

    addHeaderCombo(Saturator::Quality);
    setMeters({&in_, &out_});
}
```

## Section maps

Columns are in brackets. Each module's SPEC lists its map. The four M2 modules are drafts until they're built.

| Module | Sections | Width |
|---|---|---|
| Utility | LEVEL [1] Gain / Balance · STEREO [1] Width / Mono · CHANNELS [1] Ø L, Ø R, DC / Mute · meter | 240 |
| Saturator | SHAPE [4] curve, curve buttons, Drive / Bias · COLOR ⏻ [1] Freq / Gain · OUTPUT [1] Output / Mix · in and out meters · header: Quality | 416 |
| EQ | CURVE [10] display (with Listen, the compared track and Make room; the dynamic and spectral bands' moves on the live curve) · BAND *n* ⏻ [3] Freq, Gain, Q / Type, Slope, Place (captioned combos) · DYNAMICS ⏻ [4] Range, Thresh, Attack, Release / Spectral, Auto, Detail (a spectral band) or the trigger meter (any other), Trigger · OUTPUT [2] Output, Scale / Auto Gain, its correction, Delta · header: Sidechain, Analyzer, Mode (Processing), Quality (Resolution, while there's a delay), the latency badge | ≈ 1 070 |
| Subtractive Synth | OSC 1 [4] · OSC 2 [4] · FILTER [4] · ENVELOPES [6] · MODULATION [6] (three slots, LFO) · VOICE [3] · meter | ≈ 1550 |
| Sampler | SAMPLE [6] · PITCH [1] · LEVEL [1] · REGION [4] · FADES [1] · ENVELOPES [6] · FILTER ⏻ [4] · VOICE [2] · meter | ≈ 1480 |
| Compressor | CURVE [3] transfer curve · HISTORY [4] · COMPRESSION [3] Threshold, Ratio, Knee / Makeup, Auto Makeup, Character · TIME [2] Attack, Release / Auto Release, Lookahead · DETECTOR [2] Peak \| RMS, Link · SIDECHAIN [3] Filter, Listen / Freq, Q, Gain · OUTPUT [1] Output / Mix · in and out meters · header: Sidechain | 1120 |
| Limiter | HISTORY [6] · LIMIT [3] Gain, Ceiling, Release / Auto Release, True Peak · MODE [3] Mode (stacked), Channels, Lookahead · LOUDNESS [3] readouts, Reset · output meter | 884 |
| Gate | HISTORY [6] with threshold and return lines · GATE [3] Threshold, Return, Range / Mode (stacked), Ratio, Flip · TIME [3] Attack, Hold, Release / Lookahead · SIDECHAIN [3] Filter, Listen / Freq, Q, Gain · in and out meters · header: Sidechain | 904 |
| Limiter (draft) | HISTORY [6] · LIMIT [3] · MODE [3] · LOUDNESS [3] · meters | – |
| Loudness Meter | LOUDNESS [6] momentary and short-term bars / readouts (Integrated large, then two columns) · HISTORY [8] the song's loudness · STEREO [3] Correlation / Balance · MEASURE [4] Target, Peak Ceiling (bars) / Reset, Pause / Song \| Running / Scale, Units (sub-labels and segments) | 1172 |
| Spectrum | SPECTRUM [12] display with a legend · CURVES [4] Side Overlay / Freeze, Clear / Reference…, Clear / the reference's name · SETTINGS [5] a sub-label and dropdown per row (Channel, Resolution, Peak hold, Range, Peak labels, Slope), Average · header: Compare (the sidechain picker) | 1152 |
| Delay | TIME [5] repeats display, Left, Right, Swing / Sync, Link, Stereo · FEEDBACK [3] Amount, Drive, Wobble / Mode, Freeze · FILTER ⏻ [2] curve / Low Cut, High Cut · MODULATION [3] Rate, Time, Filter / Shape · DUCKING [1] Ducking / Release · OUTPUT [1] Dry/Wet / Output · in and out meters · header: Time Change | 944 |
| Reverb | REVERB [7] decay display, Mode (two lines of two), Freeze / Decay, Size, Predelay, Diffusion, Early, Motion, Width · DECAY BANDS [2] Low, High / Freq, Freq · FILTER ⏻ [2] curve / Low Cut, High Cut · DUCKING [1] Ducking / Release · OUTPUT [1] Dry/Wet / Output · in and out meters | 820 |
| Chorus | MODULATION [6] voices display, Mode (stacked) / Rate, Depth, Shape, Offset, Sync · VOICES [3] Delay, Width, Warmth / Feedback (bar), Invert, Low Cut, Freq (bar) · OUTPUT [1] Dry/Wet / Output · in and out meters | 624 |
| Filter | FILTER [6] response display / Freq, Sweep or Vowel, Res, Morph, Drive, Type, Circuit, Slope · LFO ⏻ [3] Rate, Amount, Offset or Spin / Shape, Sync, Stereo, Phase (bar) · ENVELOPE ⏻ [3] Amount, Attack, Release · OUTPUT [2] Dry/Wet, Output / Clip · in and out meters · header: Sidechain | 852 |
| Phaser & Flanger | PHASER, FLANGER or DOUBLER (titled by the mode) [6] response display, Mode (stacked) / Center or Time, Notches, Spread, Warmth, Feedback (bar), Invert, Through Zero · MODULATION [3] Rate, Amount, Offset or Spin / Shape, Sync, Stereo · ENVELOPE ⏻ [3] Amount, Attack, Release · OUTPUT [2] Dry/Wet, Output / Low Cut, Freq (bar) · in and out meters | 852 |
| Auto Pan & Tremolo | PANNING or TREMOLO (titled by the mode) [6] movement display, Mode (stacked), Shape / Rate, Amount, Edge, Phase, Attack, Sync, Invert · STEREO & TONE [3] Offset or Spin, Stereo, Harmonic, Vintage · OUTPUT [2] Output / Low Cut, Freq (bar) · in and out meters | 676 |

## Checked by the layout lint

`fd_panel_tests` builds every registered panel. It checks it expanded, with each section collapsed alone, with all sections collapsed, and folded; a module with pages, on every page; the EQ is also checked with a cut band selected. The checks:
- **Width:** `preferredWidth()` equals the computed sum (the widest page's, for a module with pages) and is a multiple of 4.
- **Pages:** every section on exactly one page; the tabs in the header, clear of the name and the presets.
- **Placement:**
  - every control covers whole cells, inside its section's content and the body;
  - no two controls overlap;
  - no bound control sits outside a section.
- **Sizes:** knobs are 48×76 and sit in rows 0 or 3; line controls are 20 px tall.
- **Text:** every rule above, measured with the bundled fonts and a 1–2 px margin.
- **View state:** it round-trips, and bad JSON falls back to the default.
- **Tokens:** they add up to 232.
