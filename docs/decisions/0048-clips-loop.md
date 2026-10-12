# 0048. Clips loop: a loop in the clip, extended in the arrangement, set in the clip view

- **Status:** Accepted (product owner, 2026-10-12)
- **Date:** 2026-10-12
- **Builds on:** [0019](0019-one-kind-of-track.md) (clips never overlap), [0032](0032-takes-belong-to-clips.md) (takes belong to clips), [0036](0036-crossfades-at-the-boundary.md) (crossfades at the boundary, edge fades), [0040](0040-a-menu-bar-and-everything-by-mouse.md) (the menu bar, everything by mouse), [0047](0047-the-editing-grid-in-each-editor.md) (each editor's grid), [0021](0021-mcp-server.md) (assistants)
- **Amends:** [daw-shell.md](../design/daw-shell.md) "Clip editing (C4)" ("trims stay within the source file": not for a looped clip); [fades.md](../design/fades.md) §2 (a loop's seams are edges)
- **Design:** [clip-loops.md](../design/clip-loops.md)

## Context
- A clip can't repeat: an audio clip stops at its file's end, and the only way to repeat material is Duplicate, which makes copies.
- Live, Logic, Bitwig and REAPER all loop a clip inside itself:
  - Live has a Loop switch and a loop brace in the clip view;
  - Logic has a Loop parameter, an edge that repeats the region, and notches at each repetition;
  - Bitwig has Loop, Start and Length in the Inspector, with timeline gestures for them;
  - REAPER loops an item's source past its end.
- The product owner asked for clip loops before the effects go on, with the loop start and end set in a friendly way, and edges that go as far as dragged with clear loop points.
- The engine (tracktion) already loops audio clips in file seconds and MIDI clips in beats, and saves the loop with the project.

## Decision
- **Any clip can loop a part of its content:** a loop start and length, with Loop on or off. A looped clip plays laps of its loop for its whole length, starting from a point inside the loop (its phase). There's no lead-in before the loop.
- **Switching Loop on moves nothing:** the loop is what the clip shows. **Switching it off** removes the repeats: the clip ends where its first lap ends.
- **In the arrangement:**
  - A looped clip's edges go as far as they're dragged, cutting what they cover (ADR 0019).
  - Every lap is drawn, every seam has a notch, and a loop mark shows while Loop is on.
  - Dragging a notch changes the loop's length.
  - Dragging the upper part of a clip's right edge loops it in one gesture.
- **In the clip view:** a loop strip with a brace, which works like the transport's brace, sets the loop's start and end. It's in the piano roll and in a new audio clip view.
- **In the inspector:** Loop, Loop start and Loop length.
- **In the menu bar:** Clip > Loop clips, with no default shortcut. **Assistants** get `loop_clips`.
- **Seams follow the edge-fade rule:**
  - a seam fades over 4 ms where the loop cuts into the file;
  - a loop of the whole file wraps unfaded.
  - A looped clip's spare audio for a crossfade is its own next lap.
- **Clips that can't loop:** a clip with two takes or more, an audio clip the engine already stretches to the tempo, and an audio clip whose file is missing.
- **Tempo:**
  - MIDI loops are in beats.
  - Audio loops are in the file's seconds until M4's warping.
  - Audio is placed at the file's own speed.

## Consequences
- **Trims:** daw-shell.md's "trims stay within the source file" no longer holds for a looped clip.
- **Offsets:** while a clip loops, its offset is the phase within the loop. Everything that read offset as "seconds into the file" goes through one model module (`ClipLoops`).
- **New:**
  - `model::ClipLoops`, a shared `model::BraceDrag` and `model/MusicalTime`;
  - an audio clip view;
  - a loop cursor and a notch metric in `fd_ui`;
  - one MCP tool.
- **Saving:** projects save loops in the engine's own clip properties with no format change. Older projects have none.
- **Open until asked:**
  - folding MIDI overdub into a loop;
  - a lead-in;
  - loops in beats for audio, which comes with M4's warping. The engine drops a clip's loop when a time-stretch mode is set, so warping must keep it.
