# Export: design (M2 step C8)

How Segno turns a project into audio files to share: **the mix**, or **each track as a stem**, of the whole song or the loop, as WAV, FLAC or AIFF.

The proposed decision is ADR 0039 (§14). It builds on:
- the offline render the MCP `render` tool and bounce already use (`Session::render`, [mcp.md](mcp.md) §7, [bounce.md](bounce.md) §8);
- the loudness and true-peak measurements of `model::AudioMeasure` (EBU R 128);
- projects as folders ([ADR 0030](../decisions/0030-projects-are-folders.md));
- the shared popover, property rows and progress line ([ui-foundations.md](ui-foundations.md));
- the rule that nothing is modal ([ui-principles.md](ui-principles.md) §2).

**Status:** proposed (2026-10-10). The questions in §13 await the product owner's review.

**Where the field is (2026)**, from the manuals of Live 12, Logic, Bitwig 5, REAPER 7, Pro Tools, Studio Pro 8 (Studio One) and Cubase 14:
- **What every DAW offers:**
  - the master mix and per-track stems in one place (Live's "All individual tracks", Bitwig's track list, Cubase's channel list, REAPER's stems; Logic and Studio One use a second command for stems);
  - the whole song, the loop or a time selection;
  - WAV and AIFF at 16, 24 or 32 bits, plus compressed formats;
  - a choice of sample rate, peak normalising, an offline render, and a choice of folder and name.
- **Where they differ:**
  - **Return effects in stems:** Live has an "Include return and main effects" checkbox; Cubase offers four modes, from dry to "+ master".
  - **Tails:** Logic's "Include audio tail" and Studio One's "Auto tail" render until the effects fall silent.
  - **Dither:**
    - Live offers it only below 32-bit, triangular by default;
    - Logic and Bitwig have it in the dialog;
    - Cubase and Pro Tools need a plug-in.
  - **Loudness:**
    - Studio Pro normalises to a loudness target, with presets for streaming services ("true peak always wins");
    - REAPER normalises to LUFS and is the only one that reports loudness and true peak after an export.
  - **Exporting again:** REAPER renders again with the last settings on one key.
- **What all of them share:** a modal dialog. Cubase's "Keep dialog open" and REAPER's render queue are the nearest to working on while it renders.

Segno does the baseline, and goes further in three ways:
- the export panel is a popover and the render runs in the background, so you keep working;
- the last settings are one shortcut away;
- every export reports its loudness and true peak, and can be normalised to a loudness target.

## 1. Principles

1. **What you hear is what you get.** The mix export renders the project exactly as it plays: an export at 32-bit float without normalising nulls against an offline render of the same range.
2. **Never in the way.** An export renders offline on a copy of the project. Playback, editing and saving go on, progress shows in the window, and nothing is modal.
3. **Again in one step.** The settings are remembered with the project. Ctrl+Shift+E opens the panel with them, and Enter exports.
4. **Files go where you want them:** the project's `Exports/` folder by default, or any place on disk, chosen in the system's save dialog like in any other app (product owner, 2026-10-10).
5. **Know what you made.** Every export reports its length, loudness, true peak and clipping, from the same measurements assistants use.

## 2. What it exports

- **The mix:** the master's output, after its modules and fader (the default).
- **Tracks (stems):** each track as its own file, through its modules, fader and pan, as it sits in the mix:
  - **Selected tracks** or **all tracks**. With all tracks, each return track is a stem of its own too, so the stems, played together through the master, make the mix.
  - **Through the returns and master** (off by default): each stem includes what its sends add in the return tracks, and the master's modules. It is then the track as heard on its own in the mix, but the stems no longer add up to the mix (Live's "Include return and main effects").
- A track's lanes and sidechains play as in the mix. A muted track or clip exports silent. Solo doesn't affect an export: stems are the tracks, not the solo.

## 3. Range

- **Song** (the default): from the song's start (bar 1) to the end of the last clip on any track, then on until the mix falls silent: below −100 dBFS, at most 10 s, as bounce does ([bounce.md](bounce.md) §7). The panel shows the length it will be ("3:24 + tail").
- **Loop:** exactly the loop brace, with no tail, for loops and sections. Offered only while there is a loop brace.
- **A selected time range** comes with range selection ([ADR 0028](../decisions/0028-automation-lanes.md)). Markers, regions and "Render as loop" (a tail folded onto the start) are in §16.

## 4. Format

| Option | Choices | Default |
|---|---|---|
| **Format** | WAV, FLAC, AIFF | WAV |
| **Bit depth** | 16, 24, 32-bit float (WAV only; FLAC and AIFF hold 16 and 24) | 24 |
| **Sample rate** | The project's, 44.1, 48, 88.2, 96 kHz | The project's |
| **Dither** | Automatic (triangular, whenever the bit depth is below 32-bit float), Off | Automatic |
| **Normalise** | Off; Peak (to −1 dBTP); Loudness (to a target LUFS, −14 by default, never above −1 dBTP) | Off |

- **Sample rate:** the project is rendered at the chosen rate, so modules and samples run at it: there's no resampling step afterwards.
- **Dither:** triangular (TPDF) noise of one step of the target depth, added after normalising, just before the samples are written (Live's default). Noise shaping comes later.
- **Normalise:**
  - **Peak** raises or lowers the export so its true peak sits at −1 dBTP.
  - **Loudness** sets its integrated loudness to the target, but never lets its true peak pass −1 dBTP. If the ceiling stops it short of the target, the report says so ("−15.2 LUFS, held by the −1 dBTP ceiling"), as Studio Pro's "true peak always wins".
  - Normalising is a gain only, never a limiter: a level you hear the same, louder or softer.
  - Stems are each normalised on their own.
- MP3, Ogg and Opus, and several formats in one pass, are in §16.

## 5. Where files go

- **By default:** `Exports/` in the project's folder (ADR 0030). An unsaved project exports into its unsaved folder, and the files move in with it when it's saved, as recordings do.
- **Anywhere on disk:** the panel's **Save to** row shows the folder, with **Choose…**. Choose… opens the system's save dialog (asynchronous, like Save As) at the current folder with the current name, so you can pick any folder and type a name, as in any other app.
  - The choice is remembered with the project: relative when it's inside the project's folder, absolute otherwise.
  - **Back to Exports** returns to the default.
- **Names:**
  - the mix after the project: `Song.wav`;
  - stems after the project and track: `Song - Bass.wav`;
  - a loop export adds the loop's bars: `Song - Bars 9-17.wav`.
  - The name is a field in the panel (stems add their track's name to it).
- **Never overwritten silently:** if a file exists, a number is added (`Song 2.wav`) and the report names the file. In the save dialog you can still choose to replace a file, and the system dialog asks first.

## 6. The export panel

A popover at the top of the window, like the command palette ([daw-shell.md](daw-shell.md)), opened with **Ctrl+Shift+E** or the palette's **Export…**. Its rows are the shared property rows (`PropertyList`, [ui-foundations.md](ui-foundations.md)), in three sections:

```
┌ Export ─────────────────────────────────────────────────┐
│ WHAT     The mix ▾            (Tracks: Selected ▾ / All) │
│          □ Through the returns and master   (stems only) │
│ RANGE    Song ▾   0:00 – 3:24 + tail                     │
│ FORMAT   WAV ▾  24-bit ▾  Project rate (48 kHz) ▾        │
│          Dither: Automatic ▾   Normalise: Off ▾          │
│ SAVE TO  ~/Music/Segno/Song/Exports   Choose…            │
│ NAME     Song                                  .wav      │
│                                   [ Export ]  (Enter)    │
│ Last: Song.wav · 3:31 · −13.9 LUFS · −1.0 dBTP · Show    │
└──────────────────────────────────────────────────────────┘
```

- **Enter exports;** Escape or a click elsewhere closes the panel. The export goes on in the background either way.
- **The last export's report** stays at the bottom (§8), with **Show**, which reveals the file in the file manager.
- Every choice is kept with the project (§9). Opened again, the panel is as you left it, so exporting again is Ctrl+Shift+E, Enter.

## 7. Running an export

- **Offline, on a copy** of the project as it was when Export was pressed (`Session::render`). Edits made meanwhile don't change it, and playback, editing and saving go on. Stems render one after another in the same job.
- **Each file:** rendered at 32-bit float, measured, gained (normalise), dithered and written in its format, then moved into place. A cancelled or failed export leaves no file.
- **Progress:**
  - while the panel is open, a line along its bottom;
  - while it's closed, the same `ui::paintProgress` line along the bottom of the top bar, only after `motion.progressDelay`, so quick exports don't flicker.
- **Cancel:** the panel's Export button reads **Cancel** while it runs. Escape with no popover open cancels it too, as it cancels bounces.
- **One export at a time.** Export is dimmed while one runs, and says why.
- **When it's done:** a toast ("Exported Song.wav · −13.9 LUFS · −1.0 dBTP", with **Show**), and the report in the panel. Failures say why ("The disk is full", "Can't write to that folder").

## 8. The report

After every export, for each file: its name, length, sample peak, true peak, integrated loudness, and clipped samples (if any), from `model::measureAudio`, the measurements the MCP `render` tool returns. A file with clipped samples, or a true peak above 0 dBTP, is flagged in `warning` ("Clips: 0.4 dBTP over"). Only REAPER reports this today; it answers "is it loud enough, and is it clean?" without a meter plug-in.

## 9. Remembered settings

- **With the project,** in its view state (`fdView`, as zoom and panels are). Changing an export setting is neither an undo step nor an unsaved change.
- **For new projects,** the last settings used become the defaults (the app's settings), except the location and name, which belong to the project.

## 10. Keys and actions

| Action | ID | Shortcut |
|---|---|---|
| Export… (the panel) | `project.export` | Ctrl+Shift+E |

Ctrl+Shift+E is free: Ctrl+E is split, and Ctrl+Shift+R, Live's export key, is Capture MIDI here. It's in the command palette. While the panel is open, Enter exports.

## 11. Assistants (MCP)

| Tool | Arguments | Result |
|---|---|---|
| `export_audio` | `what` ("mix" or "tracks"), `tracks?` (default all), `throughReturns?`, `range` ("song", "loop", or `{start, end}`), `format?`, `bitDepth?`, `sampleRate?`, `dither?`, `normalise?` (`{mode: "off"/"peak"/"loudness", target?}`), `name?` | Waits for the export. Each file's path and report (§8). Never an undo step |

- Without arguments it uses the project's remembered settings, as Ctrl+Shift+E then Enter does.
- **An assistant exports only into the project's `Exports/` folder, or the location the user chose in the panel.** It can't pick a path on disk itself.
- Exporting doesn't change the project, so it's allowed in read-only mode, like `render`.

## 12. Model and engine

- **`model::Export`** (JUCE-free, `[fast]` tests):
  - the settings, their defaults and which combinations are valid (FLAC and AIFF at 16 or 24 bits);
  - the song's end (the last clip's end) and each export's range;
  - the file names, with stems and numbering;
  - the normalise gain from the measurements (peak, loudness, the true-peak ceiling);
  - TPDF dither and float-to-integer conversion, deterministic for tests.
- **`Session::exportAudio(settings)`** returns a job (progress, cancel), like `render` and `bounce`. For each file:
  - it renders on the Edit copy at the export's sample rate (the mix; one track through its chain, fader and pan, its sidechain sources playing muted, as `renderOneTrack` does; or the track through the returns and master, with the other tracks muted);
  - it runs to the range's end, then the tail until −100 dBFS, at most 10 s;
  - it measures, gains, dithers and writes the file with JUCE's WAV, FLAC or AIFF writer into a temporary file in the target folder, then renames it into place;
  - `done` gets the files and their reports.
- **The panel** is a `ui::Popover` of shell parts. The location uses `juce::FileChooser` asynchronously, and **Show** uses `juce::File::revealToUser`.

## 13. Questions and recommendations

| # | Question | Recommendation |
|---|---|---|
| Q1 | **What can be exported** | The mix, or tracks as stems (selected or all, returns as stems of their own with all), each through its modules and fader (§2) |
| Q2 | **Return and master effects in stems** | Off by default, so stems add up to the mix; one checkbox, "Through the returns and master", for each track as heard in the mix (§2) |
| Q3 | **Range** | Song (to the last clip, then the tail until silent, 10 s at most) or Loop (exact, no tail); a time selection with range selection later (§3) |
| Q4 | **Formats** | WAV, FLAC, AIFF; 16, 24 or 32-bit float (WAV); MP3, Ogg and Opus later (§4, §16) |
| Q5 | **Sample rate** | The project's by default, or a standard rate, rendered at that rate (§4) |
| Q6 | **Dither** | Automatic: triangular whenever the bit depth is below 32-bit float; can be turned off (§4) |
| Q7 | **Normalise** | Off by default; Peak (−1 dBTP) or Loudness (target LUFS, −14 by default, never above −1 dBTP); a gain, never a limiter (§4) |
| Q8 | **Where files go** | `Exports/` in the project by default; any place on disk through the system's save dialog, remembered with the project (§5) |
| Q9 | **Existing files** | Never overwritten silently: a number is added; replacing is a choice in the save dialog (§5) |
| Q10 | **The UI** | A popover panel, not a modal dialog; the render runs in the background with progress and cancel; settings remembered, so exporting again is Ctrl+Shift+E, Enter (§6, §7) |
| Q11 | **After an export** | A toast and a report: length, loudness, true peak, clipping, and Show in folder (§8) |
| Q12 | **Adding the export back into the project** | Not in M2: bounce to a new track does that inside the project ([bounce.md](bounce.md)) |
| Q13 | **Real-time export** (for outboard gear) | Not in M2: there's no external hardware yet (M4) |
| Q14 | **Shortcut** | Ctrl+Shift+E (§10) |
| Q15 | **Assistants** | `export_audio`, into the project's exports or the chosen location only, allowed in read-only mode (§11) |

## 14. Proposed ADR 0039: Export renders in the background; files land where you choose

- Export is a popover panel, not a modal dialog. The render runs offline on a copy of the project in the background, with progress in the window and cancel.
- It exports the mix, or tracks as stems that add up to the mix (optionally through the returns and master), of the song (with its tail) or the loop, as WAV, FLAC or AIFF.
- Dither is automatic below 32-bit float. Normalising to a peak or a loudness target is a gain, never a limiter, and the true peak always wins.
- Files go to the project's `Exports/` folder or any place on disk chosen in the system's save dialog, are never overwritten silently, and every export reports its loudness and true peak.
- The settings are kept with the project, so exporting again is one shortcut and Enter.

## 15. Tests (Done when)

- **Nulls:**
  - the mix at 32-bit float, unnormalised, nulls against `Session::render` of the same range;
  - all stems plus the return stems, summed through the master's modules (none in the test project), null against the mix;
  - a FLAC export decodes to the same samples as a WAV export at the same depth.
- **Range:** the song ends at the last clip plus a tail that runs until silent (a reverb's tail kept, 10 s at most); a loop export is exactly the brace's length.
- **Format:** each format and depth writes a file the readers open at the right rate, depth and length; rendering at 44.1 kHz in a 48 kHz project gives 44.1 kHz audio of the same length.
- **Dither:** at 16 bits, silence becomes noise of about one step; with dither off, exact rounding; never at 32-bit float.
- **Normalise:**
  - peak lands at −1 dBTP (±0.1 dB);
  - loudness lands at the target (±0.1 LU) unless the ceiling holds it, which the report says;
  - stems are each normalised on their own.
- **Files:**
  - the default folder;
  - a chosen folder outside the project, remembered after save and reopen;
  - names with stems and loops;
  - an existing file never overwritten;
  - an unsaved project's exports moving in when saved;
  - a cancelled export leaving no file.
- **Running:** playback isn't interrupted; edits during an export don't change it; progress, cancel, and one at a time.
- **Report:** the values match `measureAudio` of the file; clipping is flagged.
- **Model:** settings, ranges, names, gains and dither (`[fast]`).
- **UI:** the panel at the standard and compact widths, nothing touching (screenshots, zoomed); the shell test exports the mix end to end with Ctrl+Shift+E, Enter.
- **MCP:** `export_audio` with defaults, stems and loudness normalising; refused outside the allowed folders.
- **Manual:** `manual-tests/M2.md`, "Export (C8)".

## 16. What comes when

| Part | Scope | When |
|---|---|---|
| **C8 Export** | §2–§12 | M2 |
| MP3, Ogg, Opus | Compressed formats, through an encoder whose licence fits AGPL; several formats in one pass | M3 |
| A time selection, markers, regions | Export a selected range, or each marked region as its own file | With range selection and markers |
| Render as loop | The tail folded onto the start, so a loop repeats seamlessly (Live's "Render as loop") | When asked |
| Noise shaping | Shaped dither at 16 bits | When asked |
| Real-time export | For external instruments and effects | M4 (external hardware) |
| Add to project | The export as a new track, or into the browser | M3 (browser) |
| Metadata | Title, artist, ISRC, loudness tags | M7 |
