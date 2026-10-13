# 0051. Pages for very wide modules

- **Status:** Accepted (product owner, 2026-10-13: tabs in the header, one width for every page, no section on every page)
- **Date:** 2026-10-13
- **Builds on:** [0022](0022-module-panels.md) (module panels: sections on the module grid), [0040](0040-a-menu-bar-and-everything-by-mouse.md) (everything by mouse, in the menu bar)
- **Design:** [device-panels.md](../design/device-panels.md), "Pages"

## Context
- Module panels are one row of sections at a fixed height, and the chain scrolls sideways. That suits most modules: the widest today, the Subtractive Synth, is about 1 550 px.
- The Wavetable Synth's spec came to about 2 000 px with its deep sections collapsed and 2 800 px open. The product owner found scrolling that far annoying and asked for tabs or pages, alongside the collapsible sections, not instead of them.
- The leading synths use top tabs (oscillators, effects, modulation); Live's Wavetable has tabs in its display area.

## Decision
- A module may group its sections into **pages**. It opts in only when its sections side by side would pass about 1 600 px. Every section is on exactly one page; nothing shows on every page.
- The **tabs** sit in the module's header, after its name and before the presets, in the bottom panel's tab look.
- The module keeps **one width**: its widest page's. A narrower page leaves room after its last section, so the modules after it never move.
- The page shown is **view state**: saved with the project or plugin state, never an undo step. View > Next/Previous Module Page and `set_module_view` turn it too. A drag of a modulation source held on a tab opens that page, and showing a parameter in its module turns to its page.
- Sections still collapse inside a page, and the module still folds.

## Consequences
- The Wavetable Synth fits in about 1 200 px on three pages (Oscillators, Filter & Amp, Modulation) instead of 2 000–2 800.
- `DevicePanel` gains pages (`addPage`), the header lays out the tabs, the view state stores the page, and the layout lint checks every page.
- A drag-to-modulate gesture can cross pages, which the modulation UI must allow for.
