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
| [0019](0019-one-kind-of-track.md) | One kind of track: audio and MIDI clips together, never overlapping (the placed clip wins); instruments add to the track's audio, never mute it | Accepted |
| [0020](0020-latency-follows-lookahead.md) | Lookahead latency follows its setting (dynamics devices): non-automatable, zero when off, hosts told on the message thread; an exception under 0016 | Accepted |
| [0021](0021-mcp-server.md) | A built-in MCP server: assistants can drive the whole DAW and every device in it; only in the DAW, never in the plugins; opt-in and local | Accepted |
| [0022](0022-module-panels.md) | Module panels: one layout grid, titled sections with lines, collapse and fold, view state outside module state; "Modules" in the UI | Proposed |
| [0023](0023-ui-foundations.md) | UI foundations: colours by role, text styles, motion and input tokens, shell metrics and shared components, one time-view kit, a track header layout model, and a UI lint | Accepted |

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
