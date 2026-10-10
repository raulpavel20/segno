# Segno

Segno ([ADR 0024](docs/decisions/0024-product-name.md)) is a FOSS music production suite: a device suite (instruments and effects shipped as plugins) plus an arrangement DAW on tracktion_engine. Both are built from one device library in this monorepo.

**Read `docs/` first.** It is the source of truth for product direction and decisions:
- `docs/vision.md`
- `docs/design/ui-principles.md`
- `docs/devices/device-suite.md`
- `docs/architecture.md`
- `docs/roadmap.md`
- `docs/decisions/` (ADRs)
- the current milestone plan in `docs/milestones/`

Don't contradict an accepted ADR; propose a new ADR instead.

## Build and test

```sh
cmake --preset dev                    # configure (Ninja, Debug, clang, ccache)
cmake --build --preset dev            # build
ctest --preset dev                    # all tests
ctest --preset dev -L fast            # JUCE-free unit + conformance tests only
```

Other presets: `rel` (release), `asan` (ASan + UBSan), `rtsan` (RealtimeSanitizer). Dependencies are pinned in `cmake/Dependencies.cmake` (CPM). Never use floating branches.

Build options: `FD_BUILD_TESTS`, `FD_BUILD_TOOLS`, `FD_BUILD_PLUGINS`, `FD_BUILD_APP` (ADR 0013: the suite and the DAW build independently).

## Hard rules

- **No JUCE below the JUCE line** (ADR 0006). These never include JUCE headers: `libs/dsp`, `libs/device`, `libs/testkit`, `devices/*/dsp`, `tools/render`.
- **`Device::process()` is real-time safe:** no allocation, no locks, no I/O, no exceptions. The conformance suite checks for allocations.
- **The UI never reimplements DSP.** Curves and displays call the same pure functions the audio path uses.
- **Devices never write their own parameters.** All edits go through the host binding.
- **Parameter IDs, device IDs and plugin codes are permanent** once a device's SPEC is approved (`docs/ids.md`).
- **No Ableton or other vendors' names in product names, UI or presets** (ADR 0008).
- **Every new device** gets `devices/<name>/SPEC.md` from `docs/devices/SPEC-TEMPLATE.md` before any code.
- **Golden test files** change only with a reason stated in the commit message.
- **MCP lives only in the DAW** (ADR 0021). Nothing in `libs/`, `devices/`, `modules/`, `plugins/` or `tools/` includes `app/src/mcp`; the plugins never open connections. MCP edits go through `engine::Session` and the device host binding, like the UI's. The CTest checks `mcp_only_in_the_daw` and `app_ui_does_not_include_tracktion` enforce it.
- **Every DAW feature ships with its MCP tools** and a test that drives it through MCP (ADR 0021). Released tool names and arguments change only after a deprecation period.
- **Everything is reachable with the mouse** ([ADR 0040](docs/decisions/0040-a-menu-bar-and-everything-by-mouse.md)). Every action is in the menu bar with its shortcut beside it. A shortcut, the command palette or a right-click menu is never the only way to do something. A feature's design doc says where it sits in the menu bar, and the feature ships with that menu item.
- **UI quality is part of correctness.** Sloppy UI (text touching other elements, uneven margins, one-off looks, hand-placed numbers) doesn't ship, however small the feature. Every UI change goes through the checklist in `docs/design/ui-foundations.md` ("Layout quality"):
  - spacing comes from the shared metrics and layout systems, never from literal numbers in a view;
  - nothing touches or overlaps, at the tightest sizes too;
  - the same thing looks and behaves the same everywhere, built from the shared components;
  - nothing shows that doesn't earn its place;
  - screenshots of the changed area are reviewed, zoomed in, at standard and compact widths.

  When a system doesn't cover a need, extend the system (with its docs and tests) instead of working around it.

## Conventions

- C++20; namespace `fd`. Format with `.clang-format`.
- UI code uses colour roles, text styles, metrics and shared components from `fd_ui` (`docs/design/ui-foundations.md`, ADR 0023): no colour mixing, direct font calls or literal colours outside `modules/fd_ui`; the UI lint (`ui_tokens_only`) must pass. Time views use the shared time-view kit.
- Module panels are declared as sections on the module grid (`docs/design/device-panels.md`, ADR 0022), never with hand-placed geometry; the layout lint (`fd_panel_tests`) must pass. The UI says "module"; code says "device".
- Tests use Catch2 v3. Tag JUCE-free tests `[fast]`; they're registered with the CTest label `fast`.
- License AGPL-3.0-or-later. New files are covered by `REUSE.toml`; third-party code keeps its own license.

## End of every session

Update the status in `docs/milestones/<current>.md` and `docs/roadmap.md`. Record new decisions as ADRs, and open questions in `docs/open-questions.md`.
