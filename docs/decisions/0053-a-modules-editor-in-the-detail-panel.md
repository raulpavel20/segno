# 0053. A module's editor opens in the detail panel

- **Status:** Proposed (2026-10-14). The Wavetable SPEC's W9, approved 2026-10-13, chose the bottom panel for its table editor and asked for this ADR; the details are up for review with [table-editor.md](../design/table-editor.md)
- **Date:** 2026-10-14
- **Builds on:** [0022](0022-module-panels.md) (module panels: six rows tall, sections on the grid), [0027](0027-selection-and-context-panels.md) (the detail panel follows the selection)

## Context
- Some modules need an editor bigger than their panel: the Wavetable's tables now; later a sampler's zones, a drum rack's pads, a spectral module's drawing.
- Module panels stay six rows tall (ADR 0022), so the editor can't live inside the panel.
- The leading synths open a separate window over the synth, which hides the module you're shaping, and a floating window is one more thing to manage.
- The detail panel already holds a big editor: the piano roll, in the clip view, below the arrangement.

## Decision
- A module's editor opens in the **detail panel**, as a third view beside Modules and Clip, named for what it edits ("Table"). Tab goes round the three; it closes with its Done, or when the module goes.
- Its height is dragged at its top edge like the clip view's, and kept with the view.
- The module stays in view above it (the panel scrolls the chain to it), so the edit is heard and seen at once.
- **In the plugins**, where there's no detail panel, the window grows downward by the editor's height, under the panel, and shrinks back on Done.
- The editor is built from the shared kit, laid out on its own grid, and checked by a layout lint like the panels'.

## Consequences
- The detail panel gains a third mode, and the menu bar's Tab item says "modules / clip view / editor".
- One place for a module's large editing, in the DAW and the plugins alike, with nothing floating.
- A module with an editor declares it to the kit (its name, and the component), so the host shows it.
