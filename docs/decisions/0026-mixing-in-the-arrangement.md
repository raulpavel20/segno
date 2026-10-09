# 0026. Mixing in the arrangement: headers, inspector, returns and a master

- **Status:** Proposed
- **Date:** 2026-10-09
- **Builds on:** [0001](0001-arrangement-only-scope.md) (arrangement first), [0019](0019-one-kind-of-track.md) (one kind of track), [0023](0023-ui-foundations.md) (the track header kit), [0021](0021-mcp-server.md) (MCP tools for every DAW feature)

## Context
M2 needs a mixer (step C3): volume, pan, mute and solo per track, return tracks with sends, a master track with its own chain (where the Limiter goes), and meters. Decisions:
- where mixing happens in a single-window, arrangement-first DAW;
- how the special tracks fit the one-kind-of-track model;
- the gain and pan laws;
- what solo does;
- what is undoable.

The design is in [design/mixer.md](../design/mixer.md).

## Decision
- **You mix where you arrange.**
  - Track headers carry mute, solo, a meter and (when tall enough) volume and pan.
  - The inspector shows the selected track's whole channel, sends included.
  - No separate mixer view in M2. A channel-strip layout in the detail panel is a later step.
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
- **Assistants** get `set_mix`, `add_return_track` and `set_send`, and see each track's mix in `get_project`.

## Consequences
- No new window or view to learn; the inspector grows into the channel strip.
- Mixing many tracks at once is less direct than a mixer view until the channel-strip layout comes. Tall track headers and the inspector cover M2's goal (finish a track).
- The track list gains kinds; every track-list consumer (the arrangement, the snapshot, MCP) handles returns and the master.
- Group tracks, return-to-return sends and sidechains from returns wait for routing work with racks (M4).
