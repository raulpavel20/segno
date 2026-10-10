# 0039. Export renders in the background; files land where you choose

- **Status:** Accepted (product owner, 2026-10-10, with every recommendation of design/export.md §13)
- **Date:** 2026-10-10
- **Builds on:** [0030](0030-projects-are-folders.md) (projects are folders), [0035](0035-bounce-keeps-its-source.md) (offline renders on a copy of the project), ui-principles §2 (nothing modal)

## Context
M2 ends with a short track made in Segno and exported to a file, so C8 adds export. Every DAW surveyed (Live 12, Logic, Bitwig 5, REAPER 7, Pro Tools, Studio Pro 8, Cubase 14) exports the mix and per-track stems, of the song, the loop or a selection, as WAV, AIFF and compressed formats, from a modal dialog.

They differ on several points:
- whether stems include return and master effects (a checkbox in Live, four modes in Cubase);
- tails (Logic and Studio One render until silent);
- dither (automatic below 32-bit only in Live);
- loudness: only Studio Pro and REAPER normalise to a loudness target, and only REAPER reports loudness after exporting;
- exporting again with the last settings (one key in REAPER).

## Decision
- **A panel, not a dialog.** Export is a popover (Ctrl+Shift+E). The render runs offline on a copy of the project in the background, with progress in the window and cancel, so playback and editing go on. The settings are kept with the project, so exporting again is Ctrl+Shift+E, Enter.
- **What:** the mix, or tracks as stems. With all tracks, the returns are stems of their own, and the stems add up to the mix. A checkbox renders each track through the returns and master instead.
- **Range:** the song, to its last clip and then the tail until silent (10 s at most), or exactly the loop.
- **Formats:**
  - WAV, FLAC or AIFF, at 16, 24 or 32-bit float (WAV only).
  - The project's sample rate or a standard one, rendered at that rate.
  - Dither is automatic below 32-bit float.
  - Normalising, to a peak or a loudness target, is a gain and never a limiter. The true peak always wins.
- **Files:**
  - They go to the project's `Exports/` folder, or any place on disk chosen in the system's save dialog, remembered with the project.
  - They are never overwritten silently.
  - Every export reports its length, loudness, true peak and clipping.
- **Assistants** export with `export_audio`, only into `Exports/` or the location the user chose.

The design is [design/export.md](../design/export.md).

## Consequences
- The offline render gains stems (a track or return through its chain and fader, optionally through the returns and master), a sample rate, and a pipeline: measure, normalise, dither, write in a format.
- `model::Export` holds the rules (settings, ranges, names, gains, dither) under `[fast]` tests.
- The shell gains a popover with the shared property rows, a progress line on the top bar, and a toast with Show.
- MP3, Ogg and Opus, time selections, markers, render-as-loop, real-time export and metadata come later (export.md §16).
