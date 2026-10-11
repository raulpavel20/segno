# 0041. Tracks and clips are managed in place

- **Status:** Accepted (product owner, 2026-10-11, with every recommendation of [design/tracks-and-clips.md](../design/tracks-and-clips.md) §13); "the lanes right after a track move with it" superseded by [0042](0042-every-row-moves-on-its-own.md) the same day: every track and lane moves on its own
- **Date:** 2026-10-11
- **Amends:** [0029](0029-independent-automation-lanes.md): "a lane stays where it was put, whatever it drives". The lanes right after a track now move with it, and a lane that drives only a deleted track goes with it. The rest of 0029 stands.
- **Builds on:** [0019](0019-one-kind-of-track.md) (clips never overlap), [0025](0025-sidechain-routing.md) (sidechains), [0027](0027-selection-and-context-panels.md) (one selection), [0032](0032-takes-belong-to-clips.md) (takes on clips), [0040](0040-a-menu-bar-and-everything-by-mouse.md) (the menu bar, never the catch-all), [0021](0021-mcp-server.md) (assistants' tools)

## Context
- Tracks and return tracks can't be renamed, deleted, reordered, duplicated or recoloured. Clips can't be renamed or recoloured, and there's no clipboard.
- The gap was noted in ADR 0029, in B41 and in mcp.md, but no milestone step owned it. The product owner found it on 2026-10-11.
- Every DAW has these, in much the same places (design §2).

## Decision
- **In place first:**
  - a name is renamed where it is shown (double-click), or with F2 for the selection;
  - a track is moved by dragging its header, deleted with Delete and duplicated with Ctrl+D when its header was used last;
  - it's coloured from its own right-click menu and the inspector.
- **The menu bar lists every command** (Edit: Cut, Copy, Paste, Rename…; Track: Duplicate track, Delete track; Colour ▸).
- **Each change is one undo step, and nothing asks first.** A deletion's toast offers Undo.
- **Deleting takes what only made sense with it:** clips and takes, modules, sidechains fed from it, sends to a deleted return, and lanes that drive only it. Shared lanes lose its assignments, and lanes after it re-anchor.
- ~~**The lanes right after a track move with it.** A lane dragged by its own header moves alone.~~ Superseded by [0042](0042-every-row-moves-on-its-own.md): every track and lane moves on its own.
- **Clips follow their track's colour** until given their own.
- **The clipboard** holds clips, notes or points from the area used last. It pastes at the playhead: clips on the selected track (rows kept, cutting what they land on), notes in the shown clip, points on the selected lane. It lasts while the app runs, across projects.
- **Assistants** get `set_track`, `delete_tracks`, `move_track`, `duplicate_track`, `set_clips` and `copy_clips`.

## Consequences
- **New pieces:**
  - Session commands for each, with a clipboard that holds copies (a cut still pastes);
  - pure models for row moves, paste placement and what a deletion takes, under `[fast]` tests;
  - a shared in-place name editor and a colour grid in fd_ui.
- **Changed behaviour:** Delete and Ctrl+D act on a track when its header was used last. That's a change from nothing happening.
- **Later:** several tracks at once comes with multi-track selection, and groups, folders and the system clipboard come later still.
