# 0022. Module panels: one layout system, sections, collapse and fold; "Modules" in the UI

- **Status:** Proposed. The product owner decided the term, the height, the sections with separators and collapsing on 2026-10-09; the grid values await sign-off on snapshots and in the app.
- **Date:** 2026-10-09
- **Builds on:** [0014](0014-flat-darker-ableton-adjacent.md) (flat, Ableton-adjacent grammar), [0008](0008-naming-and-trade-dress.md) (own names), [0018](0018-daw-app-structure.md) (Session, snapshots), [0021](0021-mcp-server.md) (MCP coverage)

## Context
The product owner's review of the instrument and effect panels (2026-10-09):
- spacing is inconsistent, "as if every element is placed by hand";
- Utility feels empty, while the Subtractive Synth and Sampler are cluttered;
- it is hard to see which controls belong together;
- the bottom panel is too short.

**The code confirms it.** `DevicePanel` only draws the chrome; every panel cuts its body into rectangles with its own literal numbers. Some examples:
- **Knob cells:** 70×106 in Utility and 42×68 in the Synth.
- **Label spacing:** `Knob` centres its arc in whatever cell it gets. So the gap between a knob's name and its arc is 18 px in Utility and 1 px in the Saturator.
- **Knob rows:** in the two-row panels, the rows touch.
- **Control heights:** 12, 18, 20 and 22 px.
- **Section labels:** only the Synth and Sampler have them.

Nothing stops the next panel from inventing its own numbers again.

**The name.** Ableton calls instruments and effects "devices". We want our own word in the UI ([ADR 0008](0008-naming-and-trade-dress.md)).

## Decision
1. **"Modules" in the UI.** User-facing text calls instruments and effects *modules*: "Add module", "Modules" tab, "Module on/off". Code, docs and IDs keep *device* (`fd::Device`, device IDs, `DevicePanel`), because IDs are permanent and the code word is internal. "Module" is never abbreviated, so it can't be confused with "Mod" (modulation).
2. **One layout system: the module grid.** Every panel is declared as **sections** of controls placed on a fixed grid. No panel computes its own geometry. The values live in `docs/design/device-panels.md` and as tokens in `fd_ui::Theme`:
   - **Panel:** 232 px tall: a 28 px header and a 204 px body (8 top padding, 20 + 4 section title, 160 content, 12 bottom padding).
   - **Columns:** 48 px wide, 4 px apart. **Rows:** 20 px tall, 8 px apart.
   - **Knob:** every knob is exactly one column by three rows (48×76), with its name, arc and value at fixed offsets.
   - **Line controls** (toggles, combos, bars, rows of segmented buttons) are one row tall. Combos and bars span at least two columns.
3. **Sections.**
   - Each section has a stable id (stored in projects) and a title in small mono caps.
   - A **power toggle** in the title, bound to a parameter (Saturator Color, Sampler Filter, EQ band), dims only that section's content.
   - Sections sit side by side with 12 px padding, separated by a **1 px line**. ADR 0014 already allows a line where it reads better; ui-principles §3 is reworded to match.
4. **Collapse and fold.**
   - Clicking a section's title collapses it to a 24 px strip with the title rotated. Clicking again expands it.
   - In the DAW chain, a whole module folds to a 28 px strip, as ui-principles §7 already planned.
   - A panel's width is computed from its sections, so collapsing narrows it. The chain relays out; a plugin editor asks its host for the new size.
5. **Header settings.** Options for quality or display, not sound character, sit right-aligned in the header: the Saturator's Quality, the EQ's Analyzer.
6. **View state is not module state.** Which sections are collapsed, and whether the module is folded, is view state, owned by the panel (`viewState()` / `setViewState()` / `onViewChanged`):
   - **in the DAW**, a property of the module's plugin tree (`fdPanel`): saved with the project, never an undo step, never an unsaved change, never a snapshot rebuild;
   - **in the plugins**, a `"view"` key in the plugin state, written only when it isn't empty. Saved state without it stays byte-identical, and presets and `fd-render` ignore it.
7. **Enforced by a test.** A layout lint (`fd_panel_tests`) builds every registered panel, expanded and in every collapse and fold state, and fails on any of these:
   - a control off the grid or outside its section;
   - overlapping controls;
   - a knob that isn't 48×76, or that sits outside rows 0 and 3;
   - text that doesn't fit, measured with the bundled fonts;
   - a width that isn't the computed sum.

   A module isn't done until the lint passes.

## Consequences
- **Easier:**
  - every panel looks and behaves the same, and a new module (Compressor, Gate, Limiter, Spectrum in M2) is a list of sections, not a layout job;
  - relations are visible: titled groups with lines between them;
  - wide modules stay usable, because sections collapse.
- **Changed:**
  - The bottom panel grows by 64 px (detail panel 216 → 280).
  - Plugin editors grow to `width + 16` × 248.
  - Panel widths change. The Synth and Sampler get wider (about 1550 and 1480 px expanded), because space is what their clutter needed. Utility, the Saturator and the EQ get narrower.
- **Must maintain:**
  - Section ids are stored in projects and plugin state, so they are never renamed (`docs/ids.md`). Unknown ids are ignored.
  - The lint thresholds use 1–2 px margins, because font rasterisers differ across platforms.
- **MCP:** collapsing, folding and reading a module's section layout become module-view tools in step C9 ([ADR 0021](0021-mcp-server.md)).
