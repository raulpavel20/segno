# 0042. Every track and lane moves on its own

- **Status:** Accepted (product owner, 2026-10-11)
- **Date:** 2026-10-11
- **Supersedes in part:** [0041](0041-tracks-and-clips-managed-in-place.md): "the lanes right after a track move with it". The rest of 0041 stands.
- **Restores:** [0029](0029-independent-automation-lanes.md): a lane "stays where it was put, whatever it drives".

## Context
- ADR 0041 made the lanes sitting right after a track move with it when the track was dragged.
- That assumes the user keeps a track's lanes under it. Nothing says so: a lane may drive another track, or several, or be put there for no reason at all. Whether carrying them along helps or gets in the way is a guess (product owner, 2026-10-11).
- Moving several rows at once is a selection's job, and comes with multi-track selection.

## Decision
- **Every track and lane moves on its own.** Moving one changes only its own place; every other row stays where it is.
- **A track or a lane can be dropped anywhere among the tracks and lanes**, between any two of them, at the top or at the end. A track no longer snaps past another track's lanes.
- **Return tracks** still move among the returns, and the master stays last.
- **Moving several rows** comes with multi-track selection (tracks-and-clips.md §16).

## Consequences
- The project keeps storing a lane as "after a track". A move rewrites the lanes around it, so each sits after the track now above it and nothing else shifts.
- Deleting a track already works this way: the lanes after it keep their place (B41).
- `move_track` keeps its name and arguments: the track lands right above the given one, and the lanes stay. Assistants get `move_automation_lane` to move a lane, as the user can.
