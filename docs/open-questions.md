# Open questions

Decisions still to be made. Once a question is answered, record the answer in an ADR or the relevant doc, then move it to "Resolved".

- **Section A** must be answered before M0 (foundations) starts.
- **Section B** can wait until the milestone given.
- **Section C** lists technical choices Claude will make unless the product owner objects.

---

## A. Blocking M0

### A1. Your actions before M0

- Install the build toolchain and PipeWire's JACK support (one command in [dev-environment.md](dev-environment.md#build-toolchain-to-install-before-m0)).
- Create the GitHub repository whenever convenient. It is not blocking, because M0 starts locally.

### Resolved

- ~~Remaining proposed ADRs~~ → 0005 (AGPL-3.0), 0006 (JUCE-free core), 0007 (verification), 0008 (naming) and 0013 (one repository) accepted on 2026-10-08.

- ~~UI technology~~ → JUCE everywhere, with a rendering spike in M0 ([ADR 0012](decisions/0012-ui-toolkit-juce-everywhere.md)).
- ~~Repository hosting~~ → GitHub; the product owner creates it later.
- ~~Testing setup~~ → by ear, on Linux, on the reference machine ([dev-environment.md](dev-environment.md)). A/B against Live is deferred.

---

## B. Needed later

| # | Question | Needed by | Recommendation |
|---|---|---|---|
| B1 | **Design references.** Screenshots of apps or plugins whose look you love, and what you love about each | Before the M0 design gate | Collect 5–10 references in `docs/design/references/` (screenshots stay local unless they are freely licensed) |
| B2 | **Minimum hardware target**. The reference machine is high-end (HX 370), so budgets need a lower floor | M0 (sets the performance budgets) | A mid-range 2020+ 4-core laptop with integrated GPU; measure on the reference machine with a CPU cap applied |
| B3 | **Product name** and organisation ID | Before the first public build | Shortlist in M1; trademark check |
| B4 | **Who designs factory presets and sounds?** This needs ears and taste | M1 onwards (every device needs presets) | Claude drafts starting presets; you curate and refine; recruit sound designers once the project is public |
| B5 | **MIDI controllers** you own or want supported | M2 | Generic MIDI learn and mapping first; specific controller scripts only on request |
| B6 | **Confirm two merges:** Drift + Analog → Subtractive Synth; Simpler + Sampler → Sampler | M1 device specs | Keep both merged (same job, different depth) |
| B7 | **Distribution costs:** Apple Developer ID ($99/year) for notarised macOS builds; Windows code signing (optional); Flathub listing | M7 | Decide before the 1.0 release; consider donations (Open Collective, GitHub Sponsors) |
| B8 | **Contributions:** when to open to outside contributors; contributor agreement or not | When the repo goes public | No CLA. Use the DCO (`Signed-off-by`) with AGPL; contribution guide after M1 |
| B9 | **Crash reports and telemetry** | M7 | No telemetry; optional, explicit opt-in crash reports |
| B10 | **Languages** | After 1.0 | English only until 1.0; prepare strings for translation from M1 |
| B11 | **Windows/macOS manual testing and Live A/B:** who, on what machine | Before the first public plugin release | CI covers automated tests on all three platforms meanwhile; find a tester or a second machine before release |
| B12 | **Compact device views** (EQ's 3-band view first): what a compact view shows when the full view's bands or settings don't fit its fixed layout, without changing the sound | A design pass over the devices (after M1) | Product owner, 2026-10-10: skip for now; full views only until a dedicated pass. 2026-10-09: the module panel kit (ADR 0022) makes sections collapsible, which covers part of the need; a compact view is still a separate pass |
| B13 | **Saturator Color at low drive.** Color boosts before the curve and cuts the same after, so it's heard only as drive pushes into the curve (SPEC). Should it also act as a plain EQ on a clean signal? | Saturator sign-off (M1) | Product owner, 2026-10-09: agreed. Keep it as is (it shapes which frequencies saturate, like the reference); the dimmed Freq/Gain while Color is off already explain the switch |
| B14 | **Per-track arm buttons.** Live MIDI follows the selected track (auto-arm). Do you also want arm buttons on track headers, e.g. to play two tracks at once or to arm a track that isn't selected? | C5 recording (M2) | Product owner, 2026-10-09: agreed. Add an arm button per track with C5; selecting a track still arms it unless another is armed explicitly |
| B15 | **Notes past the clip's end.** Notes drawn or moved outside the clip are kept but don't play (drawn faded). Should drawing past the end lengthen the clip instead? | C2 feel check (M2) | Product owner, 2026-10-09: agreed; built. Lengthen it to the end of the bar the note ends in when nothing is in the way; leave it as is when another clip follows |
| B16 | **Assistants to test MCP with.** Which assistants do you use or want supported first? | C9 (M2) | Claude Code (connects over HTTP) and Claude Desktop (launches the stdio bridge) first, plus one client running a local model |
| B17 | **MCP from other devices** on your network (a tablet or a second computer driving the DAW) | After C9 | Local machine only for now; network access later, behind its own switch and token |
| B18 | **MCP: which files may an assistant read?** (inserting audio, loading samples, opening projects take paths; [mcp.md](design/mcp.md) Q1) | C9 (M2) | Product owner, 2026-10-09: agreed. Any file the user's account can read, as the DAW itself can; the Assistants popover can narrow it to chosen folders |
| B19 | **MCP: audio in render results**, so assistants that accept audio can listen (mcp.md Q2) | C9 (M2) | Product owner, 2026-10-09: agreed. Allowed, capped at 30 s of 48 kHz stereo; longer renders give measurements only |
| B20 | **MCP: default port** (mcp.md Q3) | C9 (M2) | Product owner, 2026-10-09: agreed. 7377, then the next free one |
| B21 | **MCP: should the stdio relay start the app when it isn't running?** (mcp.md Q4) | C9 (M2) | Product owner, 2026-10-09: agreed. Yes, headless, so batch jobs work; a running app is preferred |
| B22 | **What each parameter means, for assistants.** The module reference gives each module's purpose and each parameter's range, unit and default, but not what a parameter does. Add a one-line description per parameter (in the SPEC tables and `ParamSpec`)? | Before the M2 devices' SPECs are signed off | Product owner, 2026-10-09: agreed; built. One plain sentence per parameter in every SPEC's table and in `ParamSpec::description`, compared by the conformance suite; shown as the control's tooltip and in the MCP module reference |
| B23 | **Should assistant edits show in the UI as they happen** (a toast "Assistant: add notes", or a brief highlight of what changed)? The light by the spark blinks; the edits themselves appear as normal. | After the first real sessions (C9) | Product owner, 2026-10-09: agreed. Without at first; a toast if edits feel like they come from nowhere |
| B24 | **The product's name**, to replace the FossDAW placeholder (ADR 0008, docs/ids.md) before more features bake it in. Candidates from the product owner (Cadence, Forma, Sonora, Strata, Timbre, Prisma, Modus) all collide with existing music software. | Now, before the next M2 step | Product owner, 2026-10-09: **Segno**, with the segno sign as the mark ([ADR 0024](decisions/0024-product-name.md)). Before the first public build: register segnoaudio.org and run a trademark search (classes 9 and 42) |
| B25 | **Mixer: returns and master at the end of the track list (scrolling), or pinned to the bottom of the arrangement?** ([mixer.md](design/mixer.md) Q1) | C3 (M2) | Product owner, 2026-10-09: agreed. At the end, scrolling |
| B26 | **Mixer: solo exclusive on click and Ctrl+click to add, or the other way round?** (mixer.md Q2) | C3 (M2) | Product owner, 2026-10-09: agreed. Exclusive on click, Ctrl+click adds |
| B27 | **Mixer: balance pan (0 dB at centre) or a −3 dB constant-power law?** (mixer.md Q3) | C3 (M2) | Product owner, 2026-10-09: agreed. Balance, 0 dB at centre. Also: a separate mixer view in M2 (mixer.md §6) |
| B28 | **Limiter M/S: the mid is limited to the ceiling and the side takes the room left** (so the decoded output stays under the ceiling and the centre keeps its level). The approved SPEC said "limit mid and side separately", which alone can't keep the decoded channels under the ceiling. | B3 sign-off | Product owner, 2026-10-09: agreed, as built |
| B29 | **Automation: moving an automated control** when not recording. Does its lane turn off (kept, faded) until turned back on, or does the lane always win (the control snaps back)? ([automation.md](design/automation.md) Q1) | C4 (M2) | Product owner, 2026-10-09: agreed. Turn off, with a switch per lane and one "Automation" button in the transport bar to turn them all back on |
| B30 | **Automation and clips:** when a clip moves, does the automation under it stay on the timeline or move with it? (automation.md Q2) | C4 (M2) | Product owner, 2026-10-09: agreed. Stay in M2; moving with clips as an option when range editing comes |
| B31 | **Automation write modes:** touch only, or also latch? (automation.md Q3) | C4 (M2) | Product owner, 2026-10-09: agreed. Touch only; latch with hardware controllers (B5) |
| B32 | **Mute automation:** the engine's mute isn't a parameter. Skip in M2, or add a track "on" parameter now? (automation.md Q4) | C4 (M2) | Product owner, 2026-10-09: agreed. Skip in M2 (automate volume instead); add it with M3's track work |
| B33 | **Where a new automation lane goes** (automation.md §3) | C4d | Product owner, 2026-10-09: agreed. Right under the selected track (or after the selected lane); it stays put |
| B34 | **Folding lanes per track** no longer fits lanes of their own | C4d | Product owner, 2026-10-09: agreed. One show/hide for every lane (Ctrl+Alt+L) |
| B35 | **A lane's context** (the detail panel, live MIDI) | C4d | Product owner, 2026-10-09: agreed. Its first assignment's channel; the inspector shows the lane and its assignments only |
| B36 | **Making a lane** | C4d | Product owner, 2026-10-09: agreed. The picker chooses its first parameter, making lane and assignment in one step; "Empty automation lane" as a row |
| B37 | **Lane names and colours** | C4d | Product owner, 2026-10-09: agreed. Named from their assignments until renamed; colour from the first parameter's channel |
| B38 | **Steps on a lane** | C4d | Product owner, 2026-10-09: agreed. Only when its one assignment is stepped and full-range; otherwise stepped parameters play the curve as steps |
| B39 | **Recording into a partial range** | C4d | Product owner, 2026-10-09: agreed. Clipped to the range |
| B40 | **Control menus** | C4d | Product owner, 2026-10-09: agreed. Remove from a lane rather than clear its points (they belong to the lane); assign to a lane; automate in a new lane |
| B41 | **Deleting a track or module** (not built yet) must take its lane assignments, and re-anchor lanes that sit after a deleted track, in the same undo step | When track and module deletion arrive | Until then, opening a project prunes dangling assignments, and a lane whose track is gone sits after the last track |

---

## C. Technical choices Claude will make (say if you disagree)

| Topic | Choice |
|---|---|
| Audio processing precision | 32-bit float processing; 64-bit where it matters (filter states, long delay lines, accumulators) |
| Project file format | tracktion_engine's edit format plus our own sections; DAWproject import/export in M7 |
| Time-stretch default | Signalsmith Stretch (MIT, bundled); Rubber Band as an optional higher-quality mode later |
| Font | Inter (OFL), bundled |
| Code style | `clang-format` + `clang-tidy`, enforced in CI |
| Versioning | Semantic versioning; state and preset formats versioned separately with migrations |
| Commits | Conventional commit messages; a changelog generated from them |
| Tests | Catch2 v3; Python (managed with `uv`) for analysis and plotting tools |
