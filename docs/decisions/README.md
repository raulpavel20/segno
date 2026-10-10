# Architecture Decision Records

Each file records one decision: the situation, what was decided, and what follows from it. ADRs are **append-only**. To change a decision, add a new ADR that supersedes the old one, and update the old one's status line to point to it.

**Statuses**
- **Accepted:** decided by the product owner.
- **Proposed:** recommended, awaiting product-owner review.
- **Superseded by NNNN:** replaced by a later ADR.

| # | Decision | Status |
|---|---|---|
| [0001](0001-arrangement-only-scope.md) | Arrangement-first DAW; no Session view | Accepted |
| [0002](0002-device-suite-and-daw-in-parallel.md) | Device suite (plugins) and DAW built in parallel from one device library | Accepted |
| [0003](0003-platforms-and-plugin-formats.md) | Linux, Windows, macOS; CLAP, VST3, LV2, AU, standalone | Accepted |
| [0004](0004-tech-stack.md) | C++20, JUCE 9, tracktion_engine, CPM-pinned dependencies | Accepted |
| [0005](0005-license-agpl-3.md) | License: AGPL-3.0 | Accepted |
| [0006](0006-juce-free-core-and-module-boundaries.md) | DSP and device core never depend on JUCE | Accepted |
| [0007](0007-verification-without-ears.md) | Automated, measurable verification + human sign-off | Accepted |
| [0008](0008-naming-and-trade-dress.md) | Own names and visual identity; nothing taken from Ableton | Accepted |
| [0009](0009-dark-first-design-language.md) | Dark-first, flat, minimal, contextual design language | Superseded by 0014 |
| [0010](0010-live-is-the-floor-not-the-ceiling.md) | Live is the minimum for coverage; best-in-class is the aim; consolidate into deeper devices | Superseded by 0011 |
| [0011](0011-calibrated-ambition-and-consolidation.md) | Match Live by default; go beyond selectively (mainly the mixing core); merge only devices that do the same job | Accepted |
| [0012](0012-ui-toolkit-juce-everywhere.md) | JUCE is the only UI toolkit; standard vs OpenGL rendering decided by an M0 spike | Accepted |
| [0013](0013-single-repository.md) | One repository (monorepo) for suite + DAW, with independent builds and releases | Accepted |
| [0014](0014-flat-darker-ableton-adjacent.md) | Flat everywhere, darker, close to Ableton's visual grammar; gradients only as a listed accent effect | Accepted |
| [0015](0015-rendering-opengl-for-the-daw.md) | OpenGL renderer for the DAW window (GL-thread driven), software for plugin editors; cached heavy views; frame budgets | Accepted |
| [0016](0016-constant-device-latency.md) | Constant, whole-sample latency per device instance; parameters never change latency | Accepted |
| [0017](0017-heavy-data-and-non-parameter-state.md) | Heavy data via `AssetHandoff` (frees never on the audio thread); synchronous load in `readExtraState`; UI edits to extra state through the host binding | Accepted |
| [0018](0018-daw-app-structure.md) | DAW app: `Session` facade over tracktion, immutable snapshots for views, JUCE-free tested `app/src/model`, own `ActionRegistry` | Accepted |
| [0019](0019-one-kind-of-track.md) | One kind of track: audio and MIDI clips together, never overlapping (the placed clip wins); instruments add to the track's audio, never mute it | Accepted; item 4 (a default instrument with MIDI) superseded by 0033 |
| [0020](0020-latency-follows-lookahead.md) | Lookahead latency follows its setting (dynamics devices): non-automatable, zero when off, hosts told on the message thread; an exception under 0016 | Accepted |
| [0021](0021-mcp-server.md) | A built-in MCP server: assistants can drive the whole DAW and every device in it; only in the DAW, never in the plugins; opt-in and local | Accepted |
| [0022](0022-module-panels.md) | Module panels: one layout grid, titled sections with lines, collapse and fold, view state outside module state; "Modules" in the UI | Accepted |
| [0023](0023-ui-foundations.md) | UI foundations: colours by role, text styles, motion and input tokens, shell metrics and shared components, one time-view kit, a track header layout model, and a UI lint | Accepted |
| [0024](0024-product-name.md) | The product is called Segno: the name, the mark and the identifiers derived from it | Accepted |
| [0025](0025-sidechain-routing.md) | Sidechain routing is host routing, chosen in the module's header: tracks in the DAW, Off or External in plugins; never a parameter | Accepted |
| [0026](0026-mixing-in-the-arrangement.md) | Mixing in the arrangement (track headers, inspector) and in a mixer view; return tracks and a master below the tracks; one volume taper, balance pan, solo in place (never undoable) | Accepted |
| [0027](0027-selection-and-context-panels.md) | One selection shared by both views; the detail panel and inspector follow the subject, per view (the mixer view: modules only, no inspector); inspector sections with titles and lines | Accepted |
| [0028](0028-automation-lanes.md) | Automation: every lane visible, as rows like tracks under their track; a straight line is a straight move of the control (normalised parameters, the volume taper); the drawn curve is what plays; moving an automated control turns its lane off | Accepted; placement and ownership superseded by 0029 |
| [0029](0029-independent-automation-lanes.md) | Automation lanes are rows of their own among the tracks, driving assigned parameters on any channel, each through its own range; one lane per parameter; the lane is the source of truth; popovers close on any outside click | Accepted |
| [0030](0030-projects-are-folders.md) | A project is a folder: `Name.segno` with `Recordings/` (later bounces and collected samples); clips refer to files relative to it; files are never deleted implicitly | Accepted |
| [0031](0031-recording-keeps-every-take.md) | Recording keeps every pass and loop lap on take lanes under the track (Segno's own state); the track's main lane is plain clips; choosing and comping write clips; takes move to new tracks | Accepted; where takes live and show superseded by 0032 |
| [0032](0032-takes-belong-to-clips.md) | Takes belong to clips: a clip shows its takes from two on ("3/4 ▾") and opens them under itself; a clip and its takes are edited as one; no take rows | Accepted |
| [0033](0033-no-instrument-by-itself.md) | Segno never adds an instrument by itself: a MIDI clip on a track without one is silent until you add one (supersedes 0019 item 4) | Accepted |
| [0034](0034-device-api-frozen.md) | The device API (`libs/device`) and the device state format are frozen: additions that keep devices and saved states unchanged are allowed; anything else needs an ADR and a migration | Accepted |
| [0035](0035-bounce-keeps-its-source.md) | Bounce in place renders up to where the clip enters the chain and keeps the source as a take; bounce to a new track prints the whole chain before the fader with the source muted; clips can be muted | Accepted |

## Template

```markdown
# NNNN. Title

- **Status:** Proposed | Accepted | Superseded by NNNN
- **Date:** YYYY-MM-DD

## Context
What situation or problem forces a decision. Facts, constraints, options considered.

## Decision
What we decided, stated plainly.

## Consequences
What becomes easier, what becomes harder, what we must now do.
```
