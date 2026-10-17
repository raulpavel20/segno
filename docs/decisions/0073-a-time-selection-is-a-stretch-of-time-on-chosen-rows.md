# 0073. A time selection is a stretch of time on chosen rows

- **Status:** Proposed
- **Date:** 2026-10-17
- **Amends:** [0027](0027-selection-and-context-panels.md): the selection gains rows and a stretch of time. [0028](0028-automation-lanes.md): "range editing … moving automation with clips … come later" is answered here. [0049](0049-the-master-row-is-the-song-row.md): Select clips in section becomes Select section's time; the song row's overview selects time on every row.
- **Answers:** open question B30 (automation moving with clips).
- **Builds on:** [0019](0019-one-kind-of-track.md) (placed clips cut what they land on), [0029](0029-independent-automation-lanes.md) and [0042](0042-every-row-moves-on-its-own.md) (lanes are rows of their own), [0035](0035-bounce-keeps-its-source.md) (bounce keeps its source), [0040](0040-a-menu-bar-and-everything-by-mouse.md) (the menu bar), [0063](0063-audio-is-edited-as-properties-warped-in-real-time.md) (the audio editor's ranges), [0072](0072-a-group-is-a-channel-that-sums-tracks.md) (groups), [0021](0021-mcp-server.md) (assistants)
- **Design:** [design/time-selection.md](../design/time-selection.md)

## Context
- Segno selects whole clips, points and notes, but not a stretch of time. A part of a long clip can't be deleted, repeated or moved without splitting it first, and automation can't move with the music except through sections.
- Export, bounce, fades across clips, sections and groups have each waited for "range selection" (design §1).
- The product owner, reviewing groups (2026-10-17), chose the real thing over a stand-in: "selecting a stretch of time everywhere it makes sense and the user would expect", built before groups.
- Every DAW has one (design §2). They tell it from a clip selection by a tool (Bitwig, Cubase, Studio Pro, Pro Tools) or by where a drag starts (Live: the clip bar moves, the body selects time).

## Decision
- **A time selection is a start and an end on chosen rows,** tracks and automation lanes, next to each other or not. It is one selection at a time with clips, points, takes and sections. Its rows are the same set groups use for several selected tracks.
- **Where a drag starts decides; there are no tools.** A drag on a clip's body, or on empty space in a track's or lane's row, selects time. A clip's title strip moves the clip, or, inside a time selection, the stretch. A drag in the song row's overview selects time on every row. The ruler keeps zooming and scrolling.
- **Plain commands act on the stretch on its rows and leave the gap:** Delete, Cut, Copy, Paste (overwriting, at the playhead), Duplicate (after itself), Split, Crop, Mute, Reverse, Fade to selection, Bounce, New MIDI clip.
- **Song-wide "… time" commands move the whole song:** Insert, Delete, Cut, Copy, Paste and Duplicate time, in the Song menu with Live's keys. They always act on every track, lane and section mark, through the sections' plans.
- **Automation moves when its lane is in the selection.** There is no global switch; points at the edges keep the curve outside unchanged.
- **Playing:** stopped, a new selection puts the playhead at its start. Ctrl+L loops the selection, and punching follows. Ctrl+0 zooms to it. Export offers it as a range.
- **The clip view selects time the same way.** The audio editor's range is its time selection, renamed to match. The piano roll's box drag selects notes and its stretch. A selection inside one clip in the arrangement shows in the clip view.
- **Shown** as the audio editor's range is, on its rows, with a band in the ruler and TIME (Start, End, Length, Rows) in the inspector.
- **Assistants** get `edit_range`, `copy_range`, `edit_time` and `edit_clip_time`; `bounce_clips` and `export_audio` take a stretch.

## Consequences
- **Gestures you know change** (design §16):
  - a clip's body no longer moves it: its title strip does;
  - empty space no longer box-selects clips;
  - a lane's drag selects time.

  They're drawn and approved before they're built.
- **New pure plans** (`model/TimeEdits`) under `[fast]` tests. The song-wide commands reuse `model::Sections`. The clipboard gains a stretch and song time.
- **One engine check first:** a bounced stretch's "Before bounce" take holding several parts with gaps.
- **Groups' editing on their rows uses it** ([0072](0072-a-group-is-a-channel-that-sums-tracks.md)).
- **Later, if asked:** stretching time from a selection's edge, shaping automation in a selection, several separate stretches, Consolidate, converting between time and clips.
