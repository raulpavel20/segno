# 0030. A project is a folder

- **Status:** Proposed (awaiting the product owner's review)
- **Date:** 2026-10-09
- **Builds on:** [0018](0018-daw-app-structure.md) (the Session saves and opens projects)

## Context
Until now a project was one `.segno` file, and audio clips pointed at files wherever they were dropped from. Recording (M2 step C5) writes new audio files, and bounce in place (C7) will too: they need a home that travels with the project. Every current DAW keeps one (Live's project folder, Logic's package, Bitwig's and Studio One's song folders, Reaper's project directory).

tracktion_engine can name recorded files through `EngineBehaviour::getFileForNewAudioRecording` and can store clip sources relative to the edit file. Its own project system (a `Project`, or a `FolderBasedProject`) isn't needed for this.

## Decision
- **A project is a folder:** `Name/Name.segno` with `Recordings/` (recorded audio, loop laps), and later `Bounces/` (C7) and `Samples/` (files copied in by "Collect all and save").
- **Saving a new project creates its folder.** Before the first save, recordings go to a temporary project folder (`Documents/Segno/Unsaved/<date and time>/`), moved into the project's folder when it's saved, and kept for crash recovery (M7) if it never is.
- **Clips refer to files relative to the project folder**, so a project folder can be moved, copied or zipped whole.
- **Files are never deleted implicitly:** not by undo, not by deleting clips or takes. Cleaning up unused files is an explicit command (later), to the system's trash.
- **Loose `.segno` files** from before keep opening; their recordings go to a `Recordings` folder beside them until they are saved with Save As.

The design is [design/recording.md](../design/recording.md) §2.

## Consequences
- Recordings, bounces and collected samples have one predictable place; a project can be archived or moved as one folder.
- Save As of a new project asks for a name and a parent folder, and creates the project's folder.
- "Collect all and save" and "Remove unused recordings" become possible (scheduled with the browser, M3).
- Opening goes through `Name.segno` as before; the format inside it is unchanged by this decision.
