# 0044. Presets are files of the module's state, shared by the plugins and the DAW

- **Status:** Proposed (2026-10-11), with [design/presets.md](../design/presets.md) §15
- **Date:** 2026-10-11
- **Builds on:** [0034](0034-device-api-frozen.md) (the frozen device state format, "also the preset format"), [0017](0017-heavy-data-and-non-parameter-state.md) (extra state), [0022](0022-module-panels.md) (the module header), [0040](0040-a-menu-bar-and-everything-by-mouse.md) (the menu bar), [0021](0021-mcp-server.md) (assistants), [0008](0008-naming-and-trade-dress.md) (no other vendors' names in presets)

## Context
- No module has presets yet. The quality bar says a module without good presets isn't done, and M3 starts with them (the M3 plan, A1).
- The product owner agreed that your presets live in one folder shared by the plugins and the DAW, and that Claude drafts the factory presets for the product owner to curate (M3 plan, Q2 and Q3, 2026-10-11).
- In the field (presets.md §2):
  - every DAW and plugin maker puts the preset in the module's header, with previous and next;
  - factory presets are read-only, and user presets are files in a visible folder, mostly under Documents;
  - A/B compare is common in plugins and rare in DAWs, where Live's version turns automation off;
  - "save as default" is universal.

## Decision
- **A preset is a file:** `.segnopreset`, JSON wrapping the module's state in the frozen device state format, with tags, a one-line description and an author. Its name is the file's name.
- **A preset is the sound, not the session.** It holds parameters and extra state. It leaves out:
  - the session data a module declares (the Spectrum's snapshot);
  - the panel's view;
  - the sidechain's source;
  - automation and MIDI mappings.
- **Factory presets** are built into the plugins and the DAW, and are read-only.
- **Your presets** live in `Documents/Segno/Presets/`, shared by the plugins and the DAW. They're found by the module named inside them, whatever subfolder they're in. Favourites and defaults for new modules live there too.
- **In the module's header, on every module in both hosts:**
  - the preset's name, with previous and next, and a dot while the module differs from it;
  - a list with search, tags and favourites; saving and renaming happen in place, without a dialog;
  - an A/B button.
- **Undo and automation:**
  - In the DAW, loading a preset is one undo step, and so is everything loaded while the list is open. So is switching A/B.
  - Automation keeps playing over a preset and over A/B.
- **The commands** are in a new Module menu in the menu bar. Assistants get `list_presets`, `load_preset`, `save_preset` and `compare`.

## Consequences
- **The device contract grows,** as additions ADR 0034 allows, listed in architecture.md: `DeviceInfo::sessionExtraKeys` and `ui::ParamBinding::applyState`.
- **New permanent identifiers** in `docs/ids.md`: the `.segnopreset` extension, and the file's `segnoPreset` version.
- **Factory presets are part of the repository** (`devices/<name>/presets/`). A changed factory preset is a reviewed change, like a golden file.
- **The Sampler's factory presets need sounds:** a small in-house CC0 set (presets.md §15, P11).
- **Later:** CLAP preset discovery and LV2 preset bundles can expose the same files to other hosts' browsers without changing the format.
