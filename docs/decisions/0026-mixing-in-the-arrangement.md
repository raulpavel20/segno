# 0026. Mixing: in the arrangement and in a mixer view, with returns and a master

- **Status:** Accepted (product owner, 2026-10-09, adding a separate mixer view to the proposal)
- **Date:** 2026-10-09
- **Builds on:** [0001](0001-arrangement-only-scope.md) (arrangement first), [0019](0019-one-kind-of-track.md) (one kind of track), [0023](0023-ui-foundations.md) (the track header kit), [0021](0021-mcp-server.md) (MCP tools for every DAW feature)

## Context
M2 needs a mixer (step C3): volume, pan, mute and solo per track, return tracks with sends, a master track with its own chain (where the Limiter goes), and meters. Decisions:
- where mixing happens in a single-window, arrangement-first DAW, and whether it needs a view of its own;
- how the special tracks fit the one-kind-of-track model;
- the gain and pan laws;
- what solo does;
- what is undoable.

The design is in [design/mixer.md](../design/mixer.md).

## Decision
- **You mix where you arrange, or in a mixer view.**
  - Track headers carry mute, solo, a meter and (when tall enough) volume and pan.
  - The inspector shows the selected track's whole channel, sends included.
  - A **mixer view** shows every channel as a strip (modules, sends, pan, mute and solo, fader and meter), side by side, with the master pinned at the right. It takes the arrangement's place in the centre (Arrange ⇄ Mix); the browser, inspector and detail panel stay, and the selection is shared.
  - All three edit the same channel through the same commands.
- **One volume taper** for every fader and bar: 0 dB at 80 % of the travel, +6 dB at the top, −inf at the bottom.
- **Three kinds of channel:**
  - tracks (clips, chain, sends);
  - return tracks (a chain, no clips, no sends);
  - one master track (the master chain).

  Returns and the master sit below the tracks. ADR 0019 still holds for tracks: returns and the master hold no clips.
- **Laws:**
  - Volume is plain gain (−inf … +6 dB), so a neutral mix nulls against its sources.
  - Pan is a balance control with 0 dB at centre: the far side falls along a quarter-cosine.
- **Solo is solo in place.**
  - It silences tracks that aren't soloed, but never returns or the master.
  - A click is exclusive; Ctrl+click adds.
  - Silenced sidechain sources still key (ADR 0025).
- **Undo:**
  - Volume, pan, sends and mute are mix decisions: undoable, one step per gesture.
  - Solo is a listening aid: saved, never an undo step.
- **Sends** are post-fader by default, pre-fader per send. Returns have no sends, so routing can't loop.
- **Assistants** get `set_mix`, `add_return_track`, `set_send` and `measure_mix`, and see each track's mix in `get_project`.

## Consequences
- No new window: the mixer view is a second view in the same window, with the same context panels around it.
- The channel's controls exist in three layouts (header, inspector, strip). They share components, one layout model each, and one set of Session commands, so they can't drift apart.
- The centre of the window now has two views; focus mode and the adaptive layout apply to whichever is current.
- The track list gains kinds; every track-list consumer (the arrangement, the snapshot, MCP) handles returns and the master.
- Group tracks, return-to-return sends and sidechains from returns wait for routing work with racks (M4).
