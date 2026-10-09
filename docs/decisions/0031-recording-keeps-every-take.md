# 0031. Recording keeps every take, on take lanes; the track plays plain clips

- **Status:** Proposed (awaiting the product owner's review)
- **Date:** 2026-10-09
- **Builds on:** [0019](0019-one-kind-of-track.md) (one kind of track, no overlaps), [0029](0029-independent-automation-lanes.md) (Segno's own state in the project, derived playback), [0030](0030-projects-are-folders.md) (project folders)

## Context
Every current DAW keeps loop takes and lets you comp them: Live 12's take lanes, Logic's take folders, Cubase's lanes, Studio One's layers, Reaper's fixed lanes, Bitwig's comp lanes. The product owner asked for this to be planned in full: keep every take, pick favourites, comp the best parts, and copy takes onto new tracks to stack them.

tracktion_engine records a loop pass as one clip with a take per lap and has a comp manager. But its audio takes and comps need a tracktion `Project` (without one, switching takes drops them), comps render to temporary files, and takes belong to a single clip, so passes over different time ranges don't line up as lanes.

ADR 0019 says a track plays one clip at a time, never overlapping.

## Decision
- **Every pass and every loop lap is kept** as a take on a **take lane** under its track. The track's **main lane** plays the newest take.
- **What a pass records over is kept** as a take lane first, so recording never destroys anything.
- **The main lane is plain clips.** Choosing a take, or comping parts of several (swiping across take lanes), writes ordinary clips into the main lane that point into the take files, cutting what they cover (ADR 0019) and with short crossfades at the joins. Everything that edits clips works on the result.
- **Take lanes are Segno's own state** in the project (`FDTAKES` per track, not played), written through the undo manager like automation lanes (ADR 0029). tracktion's per-clip takes are turned into take lanes when a pass ends; tracktion's take switching and comp manager aren't used.
- **Takes move to tracks:** a take, several, or all of them can be copied or moved onto new tracks that copy the track's chain and mix, for doubles and layers.
- **MIDI works the same,** with Overdub (laps merged into one clip, no takes) as an option.
- **A recording pass is one undo step;** a swipe is one undo step; files are never deleted by undo (ADR 0030).

The design is [design/recording.md](../design/recording.md) §6–§7, §13–§14.

## Consequences
- Comps need no rendering and no tracktion Project; a comp can be bounced, faded or exported like any clip.
- Take lanes are a new kind of row under a track (`model::ArrangementRows`), shown on demand.
- Segno writes the conversion from tracktion's takes to take lanes, and the comp edits (a pure, tested `model::Takes`).
- Comping with crossfades waits for clip fades (C6).
- Comping several tracks together (multi-mic recordings) can later use tracktion's track comp groups.
