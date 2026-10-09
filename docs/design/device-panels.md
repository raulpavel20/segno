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
  - **header settings** for module-wide quality and display options, which never change the sound's character (oversampling quality, analyzer), right-aligned.

  The header background is the drag handle for reordering in the DAW.
- **Body (204 px):**
  - 8 px top padding;
  - the **section title row** (20 px, then a 4 px gap);
  - **content:** 6 rows, 160 px;
  - 12 px bottom padding.
- **Sections** sit side by side, left to right, each with 12 px padding on either side. A **1 px line** in the `line` colour separates neighbouring sections, centred in the 24 px between their contents.
- **Meter strip:** modules with meters end with a strip at the right edge.
  - Widths: 24 px for one meter, 44 px for an in/out pair.
  - Labels: `OUT`, or `IN` and `OUT`, in the title row.
  - Meters are 10 px wide and fill the content height.

## The grid

| Token | Value | Note |
|---|---|---|
| Panel height | 232 | `Theme::devicePanelHeight` |
| Header | 28 | `Theme::deviceHeaderHeight` |
| Column | 48 wide, 4 apart | span of *n* columns = 52*n* − 4 (48, 100, 152, 204, 256, 308…) |
| Row | 20 tall, 8 apart | span of *m* rows = 28*m* − 8 (20, 48, 76, 104, 132, 160) |
| Content | 6 rows | 160 px |
| Section | *n* columns | 52*n* + 20 wide (12 px padding each side) |
| Section title | 20 + 4 | mono caps 10 px semibold, `textTertiary` |
| Collapsed section | 24 | title rotated, reading upwards |
| Folded module | 28 | DAW only |
| Meter strip | 24 / 44 | one meter / in and out |

Three rows are exactly one knob tall (76 px), so a knob lines up with a stack of three line controls beside it.

## Controls on the grid

Each control is placed by its top-left cell and its span: `(column, row, columns, rows)`.

| Control | Span | Rules |
|---|---|---|
| **Knob** | 1 × 3 (48×76) | Rows 0 or 3 only. Fixed block: name 16, arc area 44 (38 px arc), value 16. The value text fits its column ([design-language.md](design-language.md), "Type"). |
| **Toggle** | *n* × 1, *n* ≥ 1 | Text + 8 px must fit. |
| **Segmented** | whole cells per segment, 1 row per line of segments | 4 px between segments, 8 px between lines (the grid's own gaps). Text + 6 px must fit a segment. |
| **Combo** | *n* × 1, *n* ≥ 2 | The widest choice + 26 px (padding and chevron) must fit. |
| **Bar** (value bar) | *n* × 1, *n* ≥ 2 | Name + widest value + 16 px must fit. |
| **Display** (curve, envelope, waveform, spectrum) | any *n* × *m* | Its own label, if any, sits inside the well. A display can carry one **accessory** (e.g. the Mod envelope's Cycle toggle) in its label line. |
| **Sub-label** | *n* × 1 | Mono caps, like a section title, for a named group inside a section (the LFO inside MODULATION). Use sparingly; a new section is usually clearer. |

- Every cell a control covers is its own: controls never overlap.
- A knob's name must be at most 46 px in Inter 11 medium. Use the parameter's short name (a display name, which can change; IDs can't).
- A section's title, plus its power toggle, must fit its content width.

## Sections

- **Identity:**
  - **Id:** lowercase, stored in projects and plugin state, so never renamed (`docs/ids.md`).
  - **Title:** short, usually one noun (SHAPE, COLOR, OUTPUT). A section can retitle itself (the EQ's BAND 3).
- **Power:** optional, in the title row, bound to an on/off parameter. When off, the section's content is drawn at 35 % (the title and the power stay bright). Controls the module dims for other reasons inside it aren't dimmed twice.
- **Collapse:** click the title, or press Return when it has focus. Alt+click collapses or expands every section of the module.
  - A collapsed section is a 24 px strip with its title rotated. The power toggle stays at the top of the strip.
  - Its controls keep working (automation, MCP); only the view changes.
- **Grouping guideline:** a section is a group the user thinks of as one thing: an oscillator, a filter, the envelopes, the output. Merge small related groups instead of making many one-column sections. One-column sections are for groups with their own power (COLOR) or for the output.

## Folding and widths

- A panel's width is computed: the sum of its sections (expanded or collapsed) plus the meter strip. It is never less than the header needs.
- **In the DAW chain:** the fold chevron folds the whole module to a 28 px strip showing its power and its name, rotated. Click the strip to unfold it; drag it to reorder.
- **Width changes:**
  - the DAW chain relays out at once;
  - a plugin editor asks its host to resize, and lays the panel out from the left at whatever width the host grants;
  - on a screen narrower than the editor, the editor scrolls horizontally.

## View state

| Host | Stored in | Rules |
|---|---|---|
| DAW | the module's plugin tree, property `fdPanel` (JSON) | Saved with the project; never an undo step, never an unsaved change, never a snapshot rebuild |
| Plugin | top-level `"view"` key in the plugin state | Written only when not empty, so state without it is byte-identical; presets and `fd-render` ignore it |

Format: `{"collapsed": ["color", "shape"], "folded": true}`. Ids are sorted, and default values are omitted. Unknown ids are ignored.

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
| Saturator | SHAPE [4] curve, curve buttons, Drive / Bias · COLOR ⏻ [1] Freq / Gain · OUTPUT [1] Output / Mix · in and out meters · header: Quality | 412 |
| EQ | CURVE [10] display · BAND *n* ⏻ [3] Freq, Gain, Q / Type, Slope, Placement · OUTPUT [1] Output / Scale · header: Analyzer | 788 |
| Subtractive Synth | OSC 1 [4] · OSC 2 [4] · FILTER [4] · ENVELOPES [6] · MODULATION [6] (three slots, LFO) · VOICE [3] · meter | ≈ 1550 |
| Sampler | SAMPLE [6] · PITCH [1] · LEVEL [1] · REGION [4] · FADES [1] · ENVELOPES [6] · FILTER ⏻ [4] · VOICE [2] · meter | ≈ 1480 |
| Compressor (draft) | CURVE [3] · HISTORY [4] · COMPRESSION [3] · TIME [2] · DETECTOR [2] · SIDECHAIN [3] · OUTPUT [1] · meters | – |
| Gate (draft) | HISTORY [6] · GATE [3] · TIME [3] · SIDECHAIN [3] · meters | – |
| Limiter (draft) | HISTORY [6] · LIMIT [3] · MODE [3] · LOUDNESS [3] · meters | – |
| Spectrum (draft) | SPECTRUM [12] display · SETTINGS [3] | – |

## Checked by the layout lint

`fd_panel_tests` builds every registered panel. It checks it expanded, with each section collapsed alone, with all sections collapsed, and folded; the EQ is also checked with a cut band selected. The checks:
- **Width:** `preferredWidth()` equals the computed sum and is a multiple of 4.
- **Placement:**
  - every control covers whole cells, inside its section's content and the body;
  - no two controls overlap;
  - no bound control sits outside a section.
- **Sizes:** knobs are 48×76 and sit in rows 0 or 3; line controls are 20 px tall.
- **Text:** every rule above, measured with the bundled fonts and a 1–2 px margin.
- **View state:** it round-trips, and bad JSON falls back to the default.
- **Tokens:** they add up to 232.
