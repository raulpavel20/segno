# 0018. DAW app structure: a Session facade, snapshot-fed views, our own action registry

- **Status:** Accepted
- **Date:** 2026-10-10
- **Builds on:** [0004](0004-tech-stack.md) (tracktion_engine API stays in `app/src/engine/`), [0015](0015-rendering-opengl-for-the-daw.md) (OpenGL, painting off the message thread's schedule)

## Context
M1's DAW slice (C1–C8) turns the M0 skeleton into a shell with an arrangement, a transport, a device chain and save/open. Three things need deciding before any of it is built.

1. **Where the tracktion model meets our UI.** ADR 0004 keeps tracktion_engine API use inside `app/src/engine/`, so the engine can be upgraded or worked around in one place. The arrangement view, however, needs tracks, clips, tempo and the playhead every frame.
2. **What painting may touch.** With OpenGL (ADR 0015) frames are driven by the GL thread. Paint code must not walk tracktion's live `Edit` (a `ValueTree` graph that the message thread mutates and the engine reads), and must take no lock the audio thread uses.
3. **How actions are defined.** The UI principles require one click or one shortcut for every frequent action, remappable shortcuts, shortcuts in tooltips, and later a command palette that lists every action (M2).

## Decision
1. **`engine::Session` is the one facade over tracktion** for an open project.
   - It owns the `te::Edit`.
   - It exposes **commands** (add track, insert file, move, trim, split, delete, transport, save) as plain C++ calls. Every edit is undoable through the Edit's undo manager, one transaction per user gesture.
   - It publishes **immutable snapshots** of what views draw: tracks, clips, tempo and time signature, as plain structs in `app/src/model/`, with no tracktion or JUCE types. It rebuilds a snapshot, coalesced, after the Edit changes.
   - Live values (playhead position, transport state) are read from the Session on the message thread once per display frame.
   - `app/src/ui/` never includes tracktion headers. A test enforces this.
2. **Views draw only from snapshots and their own view state.** Paint code reads a `shared_ptr<const Snapshot>` and plain members set on the message thread; it never calls into the Session or the Edit.
3. **`app/src/model/` is JUCE-free and unit-tested** (`[fast]`): the timeline mapping, zoom and scroll, the adaptive grid, snapping, track layout and the snapshot types. The interactive feel lives in tested models; components stay thin.
4. **Our own `ActionRegistry`**, not JUCE's `ApplicationCommandManager`.
   - Actions have stable string IDs (`transport.playStop`), a name, a category, default shortcuts, and `perform`/`isEnabled`/`isToggled` callbacks.
   - String IDs survive reordering and serve as keys for user shortcut maps and the M2 command palette, which needs names and categories for search anyway. JUCE's integer command IDs and target chain add ceremony without helping either.
   - The shell routes key presses to the registry. Buttons take their tooltip (name and shortcut) from it.

## Consequences
- One translation layer, from tracktion state to snapshot, has to be maintained. It is also what keeps the GL thread away from live engine state.
- Snapshots are rebuilt on change, not per frame. A project with thousands of clips rebuilds in well under a millisecond (a few small structs per clip).
- UI and model code can be tested without an engine. Engine behaviour (commands, undo, save) is tested through the Session in the integration tests.
- When tracktion's model gains features we use (comping, clip effects), they surface through Session and the snapshot types first.
