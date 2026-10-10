# 0032. Takes belong to clips

- **Status:** Accepted (product owner, 2026-10-10, after trying C5b); a pass over part of a clip, and a clip's range when another clip cuts it, superseded by [0037](0037-a-pass-makes-its-own-clip.md)
- **Date:** 2026-10-10
- **Supersedes in part:** [0031](0031-recording-keeps-every-take.md): where takes live and how they show (take lanes as rows under the track, "Takes · N" in the track header). The rest of 0031 stands: every pass and lap is kept, what a pass replaces is kept, the main lane plays plain clips, takes are Segno's own state, takes move to new tracks, a pass is one undo step.
- **Builds on:** [0019](0019-one-kind-of-track.md) (one kind of track, no overlaps), [0023](0023-ui-foundations.md) (UI foundations)

## Context
C5b built take lanes as rows under their track, shown through "Takes · N" in the track's header. Trying it, the product owner found it in the wrong place: a take is an alternative version of a *clip*, so a user looks for takes on the clip they recorded, not on the track. The header button also showed for a single take, which is clutter: one take is just a clip.

Logic's take folders and Reaper's takes live on the clip (region, item) the same way; Live, Cubase and Studio One keep lanes per track. Segno follows the clip.

## Decision
- **A clip's takes are its own.** A recording pass makes a clip whose takes are what the pass recorded (one per loop lap). A later pass over that clip adds its takes to it, and what it records over that has no takes yet is kept as a take of it first ("Before take N").
- **One take shows nothing.** A clip with a single take looks and acts like any clip. From two takes, its title strip shows the take it plays and how many, with a chevron ("3/4 ▾"); a click opens the clip's takes.
- **Takes open under their clip,** one thin strip per take across the clip's time range; the track's row grows to fit and shrinks back when they close. There are no take rows in the header column and no takes button in the track header.
- **A clip and its takes are edited as one:** moving, trimming, splitting, duplicating or deleting the clip does the same to its takes, in the same undo step. If a clip plays parts of several takes (a pass over part of it, or comping in C5c), those parts select, move and delete together, as one clip.
- **Storage:** a track's `FDTAKES` holds one `STACK` per clip with takes: its range (`from`/`to`), its `TAKE`s and their clips. A main-lane clip names the take clip it plays (`fdTake`), and through it its stack. Whether a stack's takes are shown is view state.
- **The range is the clip's, stored:** trimming the clip sets it and never touches the takes, so trimming back out loses nothing. Another clip placed over part of the clip leaves the range and the takes whole; if it cuts the clip in two, the left part keeps the takes and the right part becomes a plain clip, so a set of takes always has one clip.

The design is [design/recording.md](../design/recording.md) §6, §10–§14 and §19. C5b projects (take lanes per track) are converted when opened.

## Consequences
- The arrangement draws takes inside a track's row instead of as rows (`model::ArrangementRows` loses take rows; the row height adds the open stacks).
- Every clip command (move, trim, split, duplicate, delete, and clearing what a placed clip covers) carries the stacks of the clips it edits, in the engine, under test.
- Comping (C5c) swipes across a clip's take strips; the comp stays plain clips that form one clip in the UI.
- MCP keeps its take tools; `get_takes` lists takes per clip.
