# The audio editor: design (M3 step P6)

The clip view for an audio clip, grown from a waveform viewer into an editor. It sets a clip's level, pitch and direction; shapes its level over time; makes it follow the song's tempo, with markers that pin its moments to beats; quantises it; finds its tempo and key; and slices it. With the piano roll (P4), both kinds of clip are fully editable. It builds on:
- today's audio clip view ([clip-loops.md](clip-loops.md) §6, ADR 0048): the waveform, the loop brace, the editing grid ([ADR 0047](../decisions/0047-the-editing-grid-in-each-editor.md));
- fades and crossfades ([ADR 0036](../decisions/0036-crossfades-at-the-boundary.md), [fades.md](fades.md)), takes and comping ([ADR 0032](../decisions/0032-takes-belong-to-clips.md), [ADR 0038](../decisions/0038-comping-you-can-listen-through.md)), bounce ([ADR 0035](../decisions/0035-bounce-keeps-its-source.md));
- the piano roll's surface ([ADR 0058](../decisions/0058-the-piano-roll-writes-in-key-and-in-chords.md), [piano-roll.md](piano-roll.md)): its bar, its bottom lane, its time handles and its Tools pane (§25);
- the song's key ([ADR 0057](../decisions/0057-the-song-has-one-key.md), [key-and-scale.md](key-and-scale.md)) and the theory in `libs/music`;
- one selection and the context panels ([ADR 0027](../decisions/0027-selection-and-context-panels.md), [selection.md](selection.md)), automation's gestures ([automation.md](automation.md));
- the menu bar as the complete list ([ADR 0040](../decisions/0040-a-menu-bar-and-everything-by-mouse.md)) and assistants' tools ([ADR 0021](../decisions/0021-mcp-server.md));
- the UI foundations ([ADR 0023](../decisions/0023-ui-foundations.md)): every panel sized from its content, nothing touching.

**Status:** approved by the product owner (2026-10-15), with every recommendation of §18 (AE1–AE25); the decision is [ADR 0063](../decisions/0063-audio-is-edited-as-properties-warped-in-real-time.md). With the approval came a rule for every editor, learned from the piano roll: its tools on its bar, never two clicks deep, and a bar with room filled (§20, ui-principles.md §1). The bar of §4 and the ways into the Tools pane (§11) follow it. Worked on a branch of its own (`audio-clip-editor`) beside main's modulators. The visual mockup (the editor at standard width with a range, the inspector, the Tempo tool, the arrangement's gain handle, a recording without Warp at 1024 px, the bar at five widths) was approved by the product owner the same day. Then three stages (§16), each signed off on its own. **P6a is built and signed off** by the product owner (2026-10-15, §21); P6b, warping, is next, its engine spike first (§15).

## 1. Why

- The product owner (2026-10-15): "Now that the piano roll is fully featured, there is another side to that panel, the audio clip side. We need to finish that as well to have both MIDI and audio fully editable. Right now it's just a visualiser." And: research "how that feature and functionality exists now in modern DAWs, what the standard is and what should we offer in that panel. With attention to detail and focus on a modern workflow."
- **Today** the audio clip view (C10) shows the clip's file as a waveform with the loop brace, the ruler and the playhead. Its own words: "It edits nothing else yet. It's the home of M4's warping."
  - An audio clip has no gain, pitch, reverse or tempo: `model::ClipInfo` has no such fields, and the Session never calls tracktion's `setGainDB`, `setPitchChange`, `setIsReversed` or anything that warps.
  - Selecting an audio clip shows the track's modules; only a double-click or Tab opens the view.
  - Loops and samples dropped into a song play at their file's speed (browser.md R8 waited for warping), and an audio loop keeps its length in seconds when the tempo changes.
  - So an audio clip can be cut, moved, faded, looped and comped, and nothing else. Every DAW in §2 also sets its level and pitch, makes it follow the tempo and fixes its timing.
- Warping is on M4's list ("warping/time-stretch"). This step brings it forward, as P1–P5 brought their pieces forward.

## 2. Where the field is (2026)

Checked against the vendors' manuals and release notes in October 2026: Live 12.4, Bitwig 6.1, Logic 12.4, Cubase 15, Fender Studio Pro 8.1, Pro Tools 2026.4, FL Studio 2026, REAPER 7.82; Ardour 9.8 and Waveform 14 for the free and the tracktion-based.

| | Live 12 | Bitwig 6 | Logic 12 | Cubase 15 | Studio Pro 8 | Pro Tools | FL Studio 2026 | REAPER 7 |
|---|---|---|---|---|---|---|---|---|
| Clip gain | ✓ | ✓ per event | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ |
| A gain line inside the clip | clip envelope | ✓ per event | – | ✓ | ✓ | ✓ | – | ✓ |
| Transpose and fine tune | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ |
| A pitch line inside the clip | clip envelope | ✓ per event | – | – | ✓ (8.1) | – | – | ✓ |
| Follows the song's tempo | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ |
| Markers pinning audio to beats | warp | beat | flex | warp | bend | warp | in NewTime | stretch |
| A slicing mode for drums | Beats | Slice | Slicing | hitpoint slices | – | Rhythmic | Slice stretch | – |
| Formants kept when transposing | Complex Pro | Elastique Pro | Flex Pitch | Pro Formant | Sound, Solo | – | Stretch pro | élastique |
| Audio quantise | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | – | ✓ |
| The song's tempo from a clip | ✓ | – | ✓ | – | ✓ | – | – | – |
| Edits on a range in the editor | crop, split | split | in its file editor | ✓ | ✓ | ✓ | in Edison | ✓ |
| Stem separation | ✓ (Suite) | announced | ✓ | ✓ | ✓ | preview | ✓ | – |
| Audio to MIDI | ✓ | – | ✓ | ✓ | ✓ | ✓ | – | – |
| Notes' pitch edited in audio | – | – | Flex Pitch | VariAudio | Melodyne (ARA) | Melodyne (ARA) | – | ARA |

(– : not found in the documentation read; not proof it's missing.)

- **Two layers everywhere.** Settings on the clip that leave its file alone (gain, transpose and fine tune, reverse, fades, normalise as gain), set in an inspector; and an editor for markers, ranges and pitch. Changing the file itself is rare: only Logic's Audio File Editor does it, behind a separate tab. Cubase stacks offline processes it can undo, Pro Tools and REAPER render new files.
- **Warping:**
  - Live's is the reference: its automatic warping and tempo matching are widely praised. Double-click adds a warp marker; hovering a transient shows a grey marker that becomes real when dragged; Shift+drag moves the audio under a marker; Ctrl+U quantises to the grid ([Ableton](https://www.ableton.com/en/manual/audio-clips-tempo-and-warping/)).
  - **The shared gesture:** transients are found, a dragged marker stretches the audio between its neighbours, and a modifier moves a marker without stretching (Logic's Option, Studio Pro's Alt). There's always a reset ([Apple](https://support.apple.com/guide/logicpro/time-stretch-using-flex-markers-lgcpdca9f4c5/mac), [Fender](https://fenderstudiopromanual.fender.com/en/Content/Editing_Topics/Transient_Detection_and.htm)).
  - **Modes:** every DAW offers 4–10 algorithms with names to learn (Live's Beats, Tones, Texture, Re-Pitch, Complex, Complex Pro; Logic's seven). They do three jobs: slicing at transients for drums (Live's Beats, Logic's Slicing, Bitwig's Slice, Pro Tools' Rhythmic), stretching anything (Complex, Polyphonic, élastique), and varispeed (Re-Pitch, Speed, Varispeed, Tape). Live uses Beats for every clip unless the default is changed; Logic can choose for you ("Automatic").
  - **On import:** Live warps loops and long samples automatically, with markers on each bar; Logic's Smart Tempo asks whether the project or the file leads; REAPER 7.79 asks. Bitwig's tempo detection on import is reported weaker than Live's.
- **The gain line** inside the clip is standard: Cubase's event volume curve, Studio Pro's clip gain curve, Pro Tools' clip gain line, REAPER's take envelope, Bitwig's per-event gain. Logic is the exception. Studio Pro 8.1 added pitch curves the same way, and Live has Gain and Transposition clip envelopes ([Steinberg](https://www.steinberg.help/r/cubase-pro/15.0/en/cubase_nuendo/topics/fades_crossfades_and_envelopes/fades_event_envelopes_c.html), [Fender](https://fenderstudiopromanual.fender.com/en/Content/Editing_Topics/Clips_and_Clip_Gain_Curves.htm)).
- **Normalising is gain now:** Logic (by peak or loudness, per region or together) and Bitwig normalise by setting the clip's gain; FL Studio 2026 normalises selected clips one by one or as a group.
- **Newest:** four-stem separation is in Live 12.3 (Suite), Logic, Cubase 15, Studio Pro and FL Studio; Bitwig has announced it and Pro Tools showed it. Audio to MIDI is in Live, Logic, Cubase, Studio Pro 8 (reviewed as "terrific up to a point", with "plenty of missed notes") and Pro Tools.
- **Complaints:** Live edits one audio clip at a time, and its warp markers only in its clip view; Bitwig's import detection; FL Studio's warp markers in a separate plugin (NewTime); Ardour has no warping at all; Waveform's warp markers were hard to see.
- **The key:** Logic plays Apple Loops in the project's key; Studio Pro and Cubase make audio follow a chord track through Melodyne or VariAudio; Live 12 shows a clip's scale without changing what plays. None finds the key of any audio clip and plays it in the song's.

**What this means for Segno:** take the standard (gain and a gain line, transpose, reverse, warping with markers, quantise, tempo detection, normalise) in Live's model, which Segno's users know. Fix what users dislike: one mode for every clip, modes named after algorithms, markers that are hard to see. Add what Segno's song key makes possible: any audio played in the song's key. Leave note-by-note pitch editing and stem separation to their own steps (§17).

## 3. Principles

1. **The file is never changed.** Every edit is a setting of the clip, kept in the project; files are read as they are ([ADR 0030](../decisions/0030-projects-are-folders.md)). What makes new audio (reversing, bouncing) writes a new file and keeps the source.
2. **Nothing is stretched that needn't be.** A clip at its own tempo and pitch plays its file sample for sample, with Warp on or off. The stretcher runs only where the speed or the pitch differs.
3. **Heard as you change it.** Warping runs in real time, so a marker dragged is heard on the next pass, with no render to wait for.
4. **What's drawn is what plays.** The waveform is drawn warped, and through the clip's gain, its gain line and its fades. The transients drawn are the ones every tool uses.
5. **One surface, the piano roll's gestures.** No tool palette: double-click adds, drag moves, Alt ignores the grid, Delete removes. Time handles stretch a range as they stretch notes; the gain line is edited as automation is.
6. **The piano roll's places.** Properties in the inspector, frequent commands on the bar, tools in the Tools pane (previewed live, Apply one undo step), every command in the menu bar and in the bar's Audio ⌄.
7. **In time by default, in key when asked.** Loops follow the song's tempo when they're placed. A clip follows the song's key only when you ask, because a key found in audio is a guess.
8. **Every panel sized to its content,** nothing touching, at standard and compact widths; screenshots reviewed zoomed in before sign-off.

## 4. The editor

```
┌ Modules · Clip  Drum loop ─ Audio ⌄  Quantise  [Tempo] [Normalise] [Slice]  Gain −3.0 dB  Transpose +2 st  Reverse  ● Warp  Beats ⌄  92.00 BPM  ×2  ÷2  ● Snap  1/16 ⌄ ┐
├────────┬────────────────────────────────────────────────────────────────────────────────────────────┤
│        │ ⟦ loop brace ⟧                                                                              │ loop strip
│        │ 1           2           3     ▕━━━━━━━━━━▏      5                                           │ ruler: a range's time handles (§7.6)
│        │ ◆   ╷   ╷   ◆   ╷   ╷   ╷  ◆  ╷   ╷     ◆                                                    │ marker strip: warp markers ◆, transients ╷
├────────┼────────────────────────────────────────────────────────────────────────────────────────────┤
│    0   │ ▁▅█▆▃▂▁   ▁▅█▆▃▂▁  ░▅█▆▃▂░  ▁▅█▆▃▂▁                                                         │ left channel
│  −12   │                    ░ range ░                                                               │
│    0   │ ▁▅█▆▃▂▁   ▁▅█▆▃▂▁  ░▅█▆▃▂░  ▁▅█▆▃▂▁                                                         │ right channel
├────────┼────────────────────────────────────────────────────────────────────────────────────────────┤
│ Gain ⌄ │ ─────────────────╲__╱───────────────                                                       │ the gain line (§6.2)
└────────┴────────────────────────────────────────────────────────────────────────────────────────────┘
```

- **It opens when one audio clip is selected,** as a MIDI clip opens the piano roll (selection.md §4's table changes: one audio clip shows the Clip view). Today only a double-click or Tab opens it.
- **The bar** keeps the tabs and the clip's name on the left and, on the right, every tool used while working on a clip, one click each (§20), in groups, in the piano roll's look:
  - **Commands:** **Audio ⌄**, a `ui::MenuButton` that opens the whole Audio menu (§13), built by the same function as the menu bar's, as Notes ⌄ is; **Quantise**, one click, which moves the clip's transients to the grid with the last settings (§7.7), in the place it has for notes.
  - **Tools:** **Tempo**, **Normalise** and **Slice**, each an icon and a word (`ui::Toggle`, as Transform and Generate are on the piano roll's bar). Each opens the Tools pane at its tool, and is lit while the pane shows it (§11).
  - **The clip's sound:** **Gain** and **Transpose** (number fields, dragged or typed: "−3.0 dB", "+2 st") and **Reverse** (a toggle). Fine, Polarity and Channels, set less often, are in the inspector.
  - **Warping:** **Warp** (lit while the clip follows the song's tempo), and while it's on, the clip's **mode** (§7.3), its **tempo** (§7.1, a number field, "92.00 BPM") and **×2** and **÷2**, the most common correction.
  - **The view:** **Snap** and the grid's step, as today.
  - **While a range is selected** (§5), its commands come first: **Split**, **Crop**, **Silence**, **Fade**, **Loop**. Nothing else moves: the groups sit at the bar's right, and the clip's name gives them room, as Draw's chord kinds appear on the piano roll's bar while drawing chords.
  - Gain, Transpose, Reverse, Warp, the mode and the tempo are properties of the clip, so they're in the inspector too (§12). They're on the bar because they're what an audio editor is used for, as Live keeps them beside its waveform: one state, two controls.
  - **At narrow widths** the clip's name gives way first, then the least used, each still in the Audio menu and the inspector: Normalise, ×2 and ÷2, Slice, Tempo, Reverse, the mode, Transpose, Gain, Quantise, then the tempo. Audio ⌄, Warp, Snap and the step stay. A range's commands stay while it's selected: it's what you're doing.
- **The level column** (on the left, `shell.keyboardWidth` wide, where the piano roll has its keys): each channel's level scale in dB (0, −6, −12, −24 …), in `textTertiary`. Dragging it up or down makes the waveform taller or shorter, to see a quiet tail or a fade's end; a double-click resets it (Studio Pro's level scale). Its foot holds the lane's chooser, as the piano roll's holds Velocity ⌄.
- **The marker strip** (under the ruler, `shell.markerStripHeight`, a new metric): warp markers as handles while the clip warps, transients as short ticks always (§7.4, §7.5).
- **The waveform:** a row per channel (two for a stereo file, one for mono), drawn through the clip's gain, gain line and fades, and warped as it plays. The played range is bright and the rest faded, as today. The editor builds its own peaks per channel; the arrangement keeps its merged ones.
- **The bottom lane:** the gain line (§6.2), as tall as the piano roll's lane (`shell.velocityHeight`). Its chooser, **Gain ⌄**, is there for more lines later (pitch, §17).
- **Zoom and scroll** as today (the time-view kit). Opening another clip fits the view to it, or to its loop.

## 5. Selection and ranges

- **In the editor you select** warp markers (a click; Shift or Ctrl+click adds; a box dragged in the marker strip), a time range (a drag across the waveform), or points of the gain line (in the lane, as automation's). Selecting one kind clears the others. None of them changes the main selection (selection.md: a panel's own selections).
- **A range** is dragged across the waveform, snapped to the grid (Alt: free). It's drawn in `selectionFill` over the waveform, with a time handle at each end in the ruler, as the piano roll's selected notes have (§7.6). A click clears it; Edit ▸ Select all (Ctrl+A) makes the whole clip the range.
- **A gain handle on the range:** a handle at the range's top centre, the clip's gain handle's look (§10). Dragged down or up, it lowers or raises the range's level on the gain line, with a ramp at each edge and a tag ("Range −4.5 dB"): a breath or a plosive tamed in one gesture (Cubase 14 puts a volume handle on its audio segments the same way).
- **The range's commands** are on the bar while it's selected (§4), and in the Audio menu and the waveform's right-click menu.
- **What acts on a range** (the bar, the Audio menu, the waveform's right-click menu, keys), each one undo step:

| Command | Does |
|---|---|
| Split (Ctrl+E) | Splits the clip at the range's edges; with no range, at the playhead, as today |
| Delete | Removes that part of the clip; what's on either side stays where it is |
| Crop to range | Moves the clip's edges to the range |
| Silence | The gain line falls to silence over the range, with a 4 ms ramp at each edge (the edge fades' length) |
| The range's gain handle; Gain ▸ Up 1 dB, Down 1 dB | Raises or lowers the gain line over the range, with the same ramps |
| Fade to range | A range at the clip's start becomes its fade-in; at its end, its fade-out |
| Loop range | The clip loops the range |
| Markers at transients | A warp marker on every transient in the range (Live's) |

- Edit's Split, Delete and Select all act here when the audio editor was used last, as they act on notes after the piano roll.

## 6. The clip's sound

### 6.1 Gain

- **Gain** (−∞ … +24 dB, 0 by default) is the clip's level. It moves, copies and splits with the clip; each take keeps its own.
- **Set in** the inspector (dragged or typed), on the clip in the arrangement (§10), by Normalise (§6.4), and by Audio ▸ Gain ▸ (Up 1 dB, Down 1 dB: the gain line over a range when there's one, else the clip's gain). Several selected clips move by the same amount.
- **Drawn as heard:** the waveform in the arrangement and in the editor is scaled by it, as it already is by fades, and cut at the row's edge.

### 6.2 The gain line

- **A line of points in dB across the clip,** shaping its level on top of Gain (0 dB: no change; −∞ … +12 dB). It's edited in the bottom lane with automation's gestures, the shared piece the controller lanes use: double-click adds a point, drag moves, Shift for fine, Alt for free, Shift+drag draws a line, a segment bends as automation's do.
- **It belongs to the audio:** each point is pinned to a moment of the file, not of the song. It loops with a looped clip, stretches with warping, splits with the clip and flips with Reverse, so the dip on a breath stays on the breath (Ardour stretches its gain envelope the same way).
- Empty, it's a flat dashed line at 0 dB, as an empty automation lane is.
- **Played by Segno's own clip node,** the one that already plays fades: sample-accurate and real-time safe.

### 6.3 Reverse, polarity, channels

- **Reverse** (the inspector, the Audio menu): the clip plays its audio backwards. The engine reads a reversed copy it writes into the project's folder (tracktion's `setIsReversed`). Warp markers, transients and the gain line flip with the audio. The fades stay at the clip's edges, so a reversed hit's tail fades in.
- **Polarity** (the inspector, the Audio menu): inverts the signal, for lining up microphones on one source.
- **Channels** (the inspector, for stereo files): Stereo, Left, Right, Mono (both summed), Swapped. For a stereo file with a different microphone on each side.

### 6.4 Normalise

- **Sets the selected clips' gain** so each reaches a target: its peak (−1 dBFS by default) or its loudness (in LUFS). **Each** clip on its own, or **Together**, keeping their balance (the loudest reaches the target). It's gain, so it's undone or changed like any gain (as in Logic and Bitwig).
- A tool in the Tools pane, its waveforms growing as it's set (§11), and Audio ▸ Normalise with the last settings.
- It measures with `libs/dsp`'s loudness, the Loudness Meter's.

## 7. Warping

### 7.1 Warp and the clip's tempo

- **Warp** (per clip). On: the clip follows the song's tempo. Its audio is laid out in beats, so a 2-bar loop stays 2 bars at any tempo. Off: it plays at its file's speed, in seconds, as today.
- **The clip's tempo** is the tempo its file was played at:
  - a file's loop metadata (a WAV's acid chunk, an AIFF's beats);
  - else a tempo in its name ("Groove_92bpm", "92 BPM");
  - else found from its audio (§7.8);
  - else, for a recording, a bounce or a comp, the song's tempo when it was made.

  It's shown on the bar and in the inspector, typed or dragged, with ×2 and ÷2 in its menu.
- **Turning Warp off** keeps the clip's start and plays the file at its speed again. Its markers are kept for when it's turned back on.
- The song has one tempo today ([arranger.md](arranger.md)). When tempo changes come, warped clips follow them with no change to this design.

### 7.2 What warps by default

| Placed in the song | Warp | Why |
|---|---|---|
| A loop: a tempo from its metadata, its name or its audio, and a whole number of bars at it (1, 2, 4, 8 or 16) | On, at its tempo | It's meant to play in time. Previews in the browser play in time too, as browser.md R8 waited for |
| A one-shot: shorter than a bar, or with no steady beat | Off | A hit or a note shouldn't move with the tempo |
| A long file from elsewhere: a song, a stem, someone else's recording | Off | Finding the tempo of a whole song isn't reliable enough to stretch it unasked. The Tempo tool warps it in one step (§7.8) |
| A recording made in Segno, a bounce, a comp | On, at the song's tempo | It was made in time. At that tempo it plays untouched (§3.2), and a later tempo change keeps it in time, as in Live |

Settings › Audio has "Loops follow the tempo" and "Recordings follow the tempo", both on.

### 7.3 Modes

Three, each named for what it's for:

| Mode | For | How it sounds | Its setting |
|---|---|---|---|
| **Beats** | Drums, percussion, rhythmic parts | Each piece between transients plays at its own speed from its transient. Slower, each hit's tail fades before the next; faster, each hit is cut at the next. Hits never smear | **Tails:** Fade (the default), or Loop (the tail repeats to fill the gap) |
| **Tones** | Everything else: voices, instruments, pads, whole mixes | Stretched smoothly (Signalsmith Stretch) | **Formants:** keep a voice's character when it's transposed (§8.1); off by default |
| **Repitch** | Tape and turntable effects; the cleanest result for small tempo changes | Speed and pitch move together, like a record played faster; nothing is stretched | None. Transpose is dimmed: the speed sets the pitch |

- **Chosen by the audio when Warp goes on** (Logic's Automatic): Beats when the clip is percussive (strong, frequent transients against little sustained sound: a measure in `libs/dsp`), else Tones. The mode field shows what was chosen, one click changes it, and the clip keeps it. Live uses Beats for every clip unless its default is changed, so voices and pads need their mode changed by hand.
- **Why three:** the DAWs' 4–10 algorithms do three jobs (§2), and three choices named for their job are easier to choose from than algorithms' names. Better quality goes inside the modes, not into more of them (§17).
- **Beats is Segno's own player.** tracktion has no slicing mode. The player plays the pieces of §7.4 through the clip-playback hook Segno already patches in for fades (§15).

### 7.4 Transients

- **Found in the background** when a file is first shown, by an onset detector in `libs/dsp` (spectral flux with an adaptive threshold). tracktion's own detector reads one channel against a fixed threshold, and ignores its sensitivity setting.
- **Drawn** as short ticks in the marker strip, in `textTertiary`. Hovering one shows where a marker would go: a dashed line down through the waveform.
- **Sensitivity** (per clip, 0–100 %, 50 by default) finds fewer or more of them. It's in the inspector, and a row of the Quantise and Slice tools: one value, shown by each, the ticks changing as it's dragged. It drives several things, but never out of sight: what it changes is drawn as you change it (the piano roll's lesson, §20 there).
- **Right-click in the strip:** Add transient here, Remove transient. Rarely needed, so it's only in that menu.
- **Every tool that uses transients** (Beats, Quantise, Markers at transients, Slice) uses the ones drawn.

### 7.5 Warp markers

A warp marker pins a moment of the audio to a beat; between markers, the audio stretches evenly. A warped clip always has its start pinned.

| Gesture | Does |
|---|---|
| Drag a transient's tick, or the dashed line shown over it | Makes it a marker and moves it: the audio stretches between it and its neighbouring markers. Snapped to the grid; Alt: free |
| Drag a marker | Moves its moment of the audio to another time, stretching each side |
| Shift + drag a marker | Slides the audio under it: the marker stays where it is and pins another moment of the audio (Live's) |
| Double-click in the marker strip or on the waveform | Adds a marker: on a transient within a few pixels, else where clicked |
| Double-click a marker | Removes it |
| Click a marker; Shift or Ctrl+click; a box in the strip | Selects markers. Delete removes them; ← and → nudge them by a grid step |

- **While dragging,** a tag says what it's becoming ("× 1.25 · 96.0 BPM": the stretch and tempo of the piece before the marker; `ui::paintValueTag`).
- **Limits:** a piece stretches between ×0.25 and ×4; a drag stops there.
- **Right-click a marker or the strip:** Warp from here (one tempo from this marker on: the markers after it go), Set the downbeat here (this moment becomes the clip's first beat), Markers at transients (in the range, or the whole clip), Remove markers (in the range, or all but the first), Quantise.
- **Without Warp** there are no markers. Dragging a transient on such a clip turns Warp on, in the same undo step.
- Markers are drawn in `textPrimary`, a handle in the strip with a line down through the waveform; a selected marker in `active`. They're the strongest thing on the waveform (Waveform's users found its markers hard to see).

### 7.6 Time handles

- A range's time handles (§5) **stretch the range's audio** when dragged: a marker at each end, the markers inside scaled with it (Bitwig's time selection, the piano roll's handles). The tag reads "× 1.5".

### 7.7 Quantise

- **Quantise** (Ctrl+U, the bar's Quantise, the Tools pane) moves each transient in the range, or the clip, towards the nearest grid line by its strength, through warp markers (Live, Bitwig, Logic, Cubase). Its settings are the Quantise tool's (§11): Strength (100 %), Swing (0 %) and the clip's Sensitivity.
- It turns Warp on if it's off. The markers it made stay, to adjust by hand. One undo step.
- Grooves come with M4, as for notes.

### 7.8 Tempo

- **The Tempo tool** (§11, or the tempo's Detect…) finds a tempo and the first downbeat (`libs/dsp`: how strongly each moment starts something, then the tempo whose beats line up best, between 60 and 180 BPM, preferring a whole number of bars for a short clip). The preview draws its beats over the waveform, with ×2 and ÷2 to correct it, and plays the clip warped to it. Apply sets the clip's tempo, puts its first marker on the downbeat and turns Warp on.
- **×2 and ÷2** in the tempo's menu: the most common correction.
- **Song tempo from this clip** (the tempo's menu, Audio ▸ Tempo ▸): sets the song's tempo to the clip's, so the clip plays untouched and everything else follows (Live's Lead, Logic's Apply Region Tempo to Project Tempo, Studio Pro's Extract to Tempo Track). One undo step.
- **One tempo per clip,** with markers for drift. Making the song follow a free-time recording's changing tempo waits for tempo changes in the song (§17).

## 8. Pitch and key

### 8.1 Transpose, Fine, Formants

- **Transpose** (−48 … +48 semitones) and **Fine** (−50 … +50 cents), per clip: in the inspector, and Audio ▸ Transpose ▸ (a semitone or an octave, up or down) with the piano roll's keys (Ctrl+↑/↓, Shift+↑/↓) when the audio editor was used last.
- **The speed stays,** with or without Warp: a clip without Warp is pitch-shifted where it is. In Repitch, Transpose is dimmed.
- **Formants** (in Tones) keep a voice's character when it's transposed; without them, a voice an octave up sounds smaller. tracktion's Signalsmith wrapper doesn't call the library's formant setting, so this is a small engine patch.

### 8.2 The clip's key

- **Found in the background:** from the file's metadata (an acid chunk's root note), its name ("Am", "F#min"), else its audio: how much of each pitch class sounds across the file, matched against the key profiles the Key popover already uses (`libs/music`). A clip with no clear key (drums, noise) has none.
- **Shown in the inspector** ("A minor", or "A minor · found"), with a choice to correct it or set None.
- **The Key popover's "From your notes"** weighs audio clips with a key too (key-and-scale.md §11 left this for audio).

### 8.3 Follow key

- **Follow key** (per clip, off by default; the inspector, the Audio menu): the clip plays in the song's key. Its key moves to the song's by the smallest step (at most 6 semitones either way). A key and its relative count as the same: an A minor loop in C major doesn't move. It moves again when the song's key changes, adds to Transpose, and the inspector says by how much ("Follows the key: +2").
- **Off by default:** a key found in audio is a guess, and a loop that moves by itself would surprise. Segno never sets the song's key by itself (ADR 0057), and doesn't move audio by itself either. Turn it on per clip, or for every loop placed with Settings › Audio › "Loops follow the key" (off).
- Logic does this for Apple Loops, which carry their key. Segno does it for any audio, with the key found from the sound (§2).

## 9. Slicing

- **Slice** (a tool, §11) cuts the clip into clips on its track at its transients, its warp markers, every grid step or every bar; the cuts are drawn over the waveform as it's set, and Apply is one undo step.
- **The slices meet seamlessly,** so they get no edge fades (fades.md §2) and the track sounds the same until they're moved: the start of re-sequencing a beat, or fixing one late hit.
- **Into a Sampler** (a slice on each key, and a MIDI clip that plays them) comes with M4's slicing Sampler and Drum Rack (§17); the tool's "Into" setting gains it then.

## 10. In the arrangement

- **Gain on the clip:** a handle at the top centre of an audio clip, under its title strip, with the fade handles' size and look (`shell.fadeHandleSize`), shown on hover and on selected clips. Dragging it up or down sets the gain, with a tag ("Gain −3.5 dB"); several selected clips move by the same amount; a double-click resets it to 0 dB. Bitwig sets an event's gain from its top edge; Pro Tools and FL Studio keep a gain control at a clip's bottom edge. At the top it sits with the fade handles, away from the waveform's busiest part.
- **Drawn as heard:** waveforms are scaled by the gain and the gain line, and drawn warped: each piece between markers at its own scale.
- **Edits keep warping:** moving, trimming, splitting, duplicating and looping a warped clip keep its markers with its audio; a split clip's parts null against the whole.
- **Not on the clip:** markers and transients (the editor's), and a sign that a clip is warped or transposed. The inspector and the editor show both; a badge on every clip hasn't earned its place.

## 11. The Tools pane for audio

The piano roll's Tools pane ([piano-roll.md](piano-roll.md) §25) shows the audio's tools when the clip view shows an audio clip: tiles with their icons, the chosen tool's settings under them, the result heard and seen as it's set, Apply (Return) one undo step named for the tool, Escape lets it go, the settings remembered. There's one tab: Generate has nothing to do on audio.

- **Opened by the tools' own buttons on the bar** (Tempo, Normalise, Slice: §4), at that tool, in one click; the piano roll's fourteen tools need its two buttons and tiles, the audio's four don't. Quantise's settings are its tile, reached from any open pane, or Audio ▸ Quantise… (Ctrl+Shift+U).

| Tool | Settings (default) | While it's set |
|---|---|---|
| Quantise | Strength % (100), Swing % (0), Sensitivity (the clip's) | The markers move; the clip plays quantised |
| Tempo | The tempo found, ×2, ÷2; the downbeat (dragged on the waveform) | The beats drawn over the waveform; the clip plays warped to them |
| Normalise | To: Peak, Loudness (Peak); Target (−1 dBFS, or −14 LUFS); Clips: Each, Together (Each) | The waveforms grow or shrink |
| Slice | At: Transients, Warp markers, Grid, Bars (Transients); Sensitivity (the clip's); Into: Clips | The cuts drawn over the waveform |

- **One description per tool,** as for notes: `model/AudioToolkit` beside `model/NoteToolkit`, read by the pane, assistants and the remembered settings.
- **Normalise** acts on every selected audio clip; the others act on the clip the editor shows.

## 12. The inspector's AUDIO section

Under CLIP, for one audio clip (CLIP keeps Name, Colour, Start, Length, Offset, Loop, Mute and the fades):

| Row | Values |
|---|---|
| Gain | dB (−∞ … +24) |
| Transpose | semitones, with Fine (cents) beside it, as a fade's Curve sits beside its length |
| Warp | On, with the mode beside it |
| Tempo | BPM (×2 and ÷2 in its menu); while it warps |
| Tails, or Formants | the mode's setting; while it warps |
| Key | the clip's key, with Follow key beside it |
| Transients | Sensitivity % |
| Reverse | On |
| Polarity | Inverted |
| Channels | Stereo, Left, Right, Mono, Swapped; stereo files only |
| File | "48 kHz · 24-bit · stereo · 8.2 s", read only; its right-click menu has Show in browser, Show in folder |

- **Several audio clips** get AUDIO too, with Gain, Transpose, Warp and Reverse: a range of values where they differ ("−6 – 0 dB"), moved together, as the NOTES section does for notes.
- **Sized as every section is:** labels in the property column, nothing touching at the inspector's narrowest.

## 13. Menus, keys and actions (ADR 0040)

A new **Audio** menu (Alt+A), between Notes and Song. **The same menu opens from the bar's Audio ⌄** (§4). The menu is the complete list and the second route: what's used while working is on the bar (§4, §20). Its clip properties (Warp, mode, tempo, Transpose, Follow key, Gain, Normalise, Reverse, Polarity) act on the selected audio clips from the arrangement too; what needs the editor (ranges, markers) is dimmed with the reason elsewhere.

| Item | Shortcut |
|---|---|
| Warp | – |
| Warp mode ▸ (Beats, Tones, Repitch) | – |
| Tempo ▸ (Double, Half; Detect… (the Tools pane at Tempo); Song tempo from this clip) | – |
| — | |
| Quantise | Ctrl+U |
| Quantise… (the Tools pane at Quantise) | Ctrl+Shift+U |
| Warp markers ▸ (Markers at transients, Warp from here, Set the downbeat here, Remove markers) | – |
| — | |
| Transpose ▸ (Up a semitone, Down a semitone; Up an octave, Down an octave) | Ctrl+↑, Ctrl+↓, Shift+↑, Shift+↓ |
| Follow key | – |
| — | |
| Gain ▸ (Up 1 dB, Down 1 dB, Silence) | – |
| Normalise… (the Tools pane at Normalise) | – |
| Reverse | – |
| Polarity | – |
| — | |
| Crop to range | – |
| Fade to range | – |
| Loop range | – |
| Slice… (the Tools pane at Slice) | – |

- **Shared keys:** Ctrl+U, Ctrl+Shift+U and the transpose keys are the Notes menu's. Each key goes to whichever kind of clip the clip view shows, so no key does two things at once; how both menus list them is settled while building, with the shortcut test.
- **Edit's** Split (Ctrl+E), Delete and Select all act on the range and the markers when the audio editor was used last (§5).
- **View** gains Clip view lane ▸ Gain, and Waveform height ▸ (Taller, Shorter, Reset).
- **Right-click** on the waveform: the range's commands, Quantise, Warp markers ▸, Reverse, Slice…; on a marker, the marker's (§7.5); in the lane, automation's.
- **Settings › Audio** gains "Loops follow the tempo" (on), "Recordings follow the tempo" (on) and "Loops follow the key" (off).

## 14. Assistants (MCP)

Each tool is one named undo step, with a test that drives it through MCP (ADR 0021):

| Tool | Does |
|---|---|
| `get_clip` | Gains an audio clip's gain, transpose, fine, reverse, polarity, channels, warp, mode and its setting, tempo, key, follow key, sensitivity, warp markers (each a time in the file and its beat), the gain line and the file's format |
| `set_audio` | `clips`, and any of `gain`, `transpose`, `fine`, `reverse`, `polarity`, `channels`, `warp`, `mode`, `tails`, `formants`, `tempo`, `key`, `follow_key`, `sensitivity` |
| `set_warp_markers` | `clip`, `markers` (a time in the file and a beat each), `replace?`; or `at_transients`, `warp_from`, `downbeat` |
| `get_transients` | A clip's transients, as times in its file |
| `get_gain_line` / `set_gain_line` | A clip's gain line: points (a time in the file, dB, bend) |
| `edit_audio_range` | `clip`, `from`, `to`, `action`: split, delete, crop, silence, gain, fade, loop, markers |
| `list_audio_tools` / `transform_audio` | The Tools pane's tools by name, with their settings, as `list_note_tools` and `transform_notes` |
| `detect_tempo` | A clip's tempo, downbeat and how sure it is; changes nothing |
| `detect_key` | Gains audio clips |

## 15. Model and engine

- **`model::ClipInfo`** gains the clip's audio settings (gain, transpose, fine, reversed, polarity, channels, warp, mode, tails, formants, tempo, key, follow key, sensitivity), its warp markers and its gain line. **`model/AudioEditing`** holds the pure functions, `[fast]`-tested as `NoteEditing` is: marker drags and slides, time handles, quantise, Warp from here, the downbeat, every range command, slice positions, normalise gains, and the gain line pinned to the audio through loops, warps, splits and reverse.
- **`libs/dsp`** (JUCE-free, `[fast]`): onsets with a sensitivity, tempo and downbeat, how percussive a sound is (choosing Beats or Tones), a file's pitch-class profile; peaks and loudness already exist. **`libs/music`:** a key from a pitch-class profile, beside the key from notes.
- **The engine: tracktion's real-time path** (`setUsesProxy(false)`; it renders stretched proxy files otherwise). It already plays warp markers, follows a tempo and a key, and shifts pitch in real time:
  - **Warp:** `setAutoTempo`, with the file's `LoopInfo` (tempo, beats, signature) set before the stretch mode. That keeps a clip's loop: tracktion only drops a loop when its `LoopInfo` can't loop (B65). Markers through `WarpTimeManager`.
  - **Tones:** Signalsmith Stretch, already built in (`TRACKTION_ENABLE_TIMESTRETCH_SIGNALSMITH`), with its formant setting patched into tracktion's wrapper. **Repitch:** resampling, no stretcher. **Beats:** Segno's own player, a node behind the clip-playback hook.
  - **Transpose and Reverse:** `setPitchChange`, `setIsReversed`.
  - **At its own tempo and pitch,** a clip reads its file directly (§3.2), checked by a null test; if tracktion stretches at a ratio of 1, a small patch skips it.
  - **Gain, the gain line, polarity and channels** play in Segno's clip node (the fades'), after the stretch: sample-accurate in song time and real-time safe.
- **The waveform:** the editor's peaks per channel. Warped drawing maps each piece between markers onto its span; the arrangement draws warped clips from its tiles piece by piece.
- **Step 0: the engine spike** (as racks had), before P6b is built, each point kept as a test:
  1. A warped clip at its own tempo nulls against the same clip unwarped, sample for sample.
  2. A warped clip plays its fades, edge fades and crossfades as an unwarped one does (our clip node on the real-time path).
  3. A looped clip keeps its loop when it warps (B65), in beats, lap after lap.
  4. Splitting a warped clip nulls against the whole. A moved marker is heard on the next block.
  5. Bounces and exports of warped clips match playback: sample for sample where nothing stretches, within a measured residual where it does.
  6. 32 warped stereo clips in Tones play within the audio budget on the reference machine, and the stretcher's latency is compensated (tracktion reports it since 3.5).
  7. Repitch, and transposing without Warp, work on the real-time path.
  8. Warped waveforms draw within the arrangement's frame budget.

  If a point fails, the design changes before building, and says how.

## 16. Stages

| Stage | What | Why first |
|---|---|---|
| **P6a: the editor and the clip's sound** | §4–§6 and §10's gain handle: the editor opens on selection; the level column, a row per channel, the marker strip; ranges and their commands; gain, the gain line, reverse, polarity, channels, normalise; the Audio menu and Audio ⌄; the Tools pane for audio (Normalise); the AUDIO section; MCP | Useful at once, and nothing in it stretches: no engine risk |
| **P6b: warping** | Step 0 (§15); then §7 and §8.1: Warp, the three modes, the clip's tempo and what warps by default, transients, markers, time handles, Quantise, the Tempo tool, transpose, fine and formants; previews and drops in time; MCP | The heart of an audio editor, gated by its spike |
| **P6c: key and slices** | §8.2, §8.3 and §9: the clip's key, Follow key, the Key popover hearing audio; Slice; MCP | Builds on transients and pitch |

A visual mockup of the editor comes first, for approval before P6a is built. Each stage is built, reviewed in screenshots at standard and compact widths, tried hands-on, and signed off before the next.

## 17. Later, and left out

| Later | Why not now |
|---|---|
| **Stem separation** (vocals, drums, bass, other) | Now standard (§2), and the next step after P6 is recommended to be it. It needs its own design: a model of about 80 MB (htdemucs: MIT code, run through demucs.cpp or ONNX Runtime) shipped in the Library ([ADR 0055](../decisions/0055-factory-content-is-files-in-a-read-only-library.md)), taking about 1.5× a track's length on a CPU, with progress where it works. RoFormer models separate better, but their weights are often non-commercial, which a free program can't ship |
| **Audio to MIDI** (melody, chords, drums) | With stems, on a small open model (Spotify's Basic Pitch, Apache-2.0, to be verified). Reviews say even Studio Pro 8's misses many notes: promise little |
| **A pitch line** in the clip (risers, tape stops) | The stretcher takes one pitch per clip; a line needs engine work inside it. Live, Bitwig, Studio Pro 8.1 and REAPER have one |
| **Slices into a Sampler or a Drum Rack** | With M4's slicing Sampler and Drum Rack |
| **Notes' pitch edited in audio** (Melodyne-style) | Not in Live; Logic and Cubase build their own; the rest host Melodyne through ARA 2. tracktion can host ARA and the SDK is Apache-2.0: with M4's plugin hosting |
| **Several clips in the editor;** quantising several microphones of one drum kit together | Bitwig 6's layered editing, Logic's Q-reference. After several MIDI clips can be edited at once |
| **The song's tempo following a free-time recording** | Needs tempo changes in the song |
| **Higher-quality stretching** (Rubber Band's R3 engine: GPL-2.0-or-later, which AGPL allows) | Only if Tones falls short by ear; one more engine patch |
| **Editing in another program** (Audacity, ocenaudio) | When asked: the clip's audio copied into the project, opened there, and read again when it's saved |
| **A pan line** | Rare on a clip; the track's pan and automation do it |
| **Stretching a clip by its edge in the arrangement** (Bitwig, FL Studio, Studio Pro) | When asked; the editor's time handles do it today |

| Left out | Why |
|---|---|
| Changing files in place | §3.1; only Logic does it, behind a separate editor |
| Spectral editing | A repair tool's job (RX, SpectraLayers), not an arrangement DAW's |
| Effects on a clip (Pro Tools, Ardour 9, REAPER) | Modules belong to tracks ([ADR 0019](../decisions/0019-one-kind-of-track.md)); revisited if asked |

## 18. Questions and recommendations

| # | Question | Recommendation |
|---|---|---|
| AE1 | **Scope** | Everything in §4–§9, with warping brought forward from M4: without it an audio editor is only settings |
| AE2 | **When it opens** | When one audio clip is selected, as a MIDI clip does (§4) |
| AE3 | **Layout** | The level column (drag to zoom the height), a row per channel, a marker strip under the ruler, the gain line in the bottom lane (§4) |
| AE4 | **The bar** | ~~Audio ⌄, Quantise, Transform, then Warp with its mode and tempo, then Snap~~ *Revised at approval (§20):* Audio ⌄, Quantise; Tempo, Normalise, Slice; Gain, Transpose, Reverse; Warp, the mode, the tempo, ×2, ÷2; Snap; a range's commands while one is selected. Properties on the bar and in the inspector, one state (§4) |
| AE5 | **Where properties live** | An AUDIO section in the inspector, for one clip or several (§12) |
| AE6 | **Gain on the clip in the arrangement** | A handle at its top centre, the fade handles' look, a tag while dragging (§10) |
| AE7 | **The gain line** | In the bottom lane, edited as automation is; pinned to the audio, so it loops, stretches, splits and flips with it (§6.2) |
| AE8 | **Ranges** | Dragged on the waveform; Split, Delete, Crop, Silence, Gain, Fade, Loop and Markers at transients act on them (§5) |
| AE9 | **Nothing stretched at its own tempo** | A rule, with a null test: a clip at its tempo and pitch reads its file sample for sample (§3.2) |
| AE10 | **Real time or rendered** | Real time (tracktion's real-time path), heard at once; bounces and exports render through the same path (§15) |
| AE11 | **What warps by default** | Loops, recordings, bounces and comps on; one-shots and long files from elsewhere off; two settings for it (§7.2) |
| AE12 | **Modes** | Three, named for their job: Beats (Segno's own player), Tones (Signalsmith), Repitch (§7.3) |
| AE13 | **The default mode** | Chosen by the audio when Warp goes on (Beats if percussive, else Tones), shown, one click to change (§7.3) |
| AE14 | **Markers' gestures** | Live's: drag a transient to make a marker, double-click adds and removes, Shift+drag slides the audio; Alt frees from the grid, as everywhere in Segno (§7.5) |
| AE15 | **Transients** | Segno's own detector in `libs/dsp`; one sensitivity per clip, seen wherever it's set (§7.4) |
| AE16 | **Tempo** | Found for loops when placed; for long files, on request with the Tempo tool; ×2, ÷2; Song tempo from this clip (§7.8) |
| AE17 | **Transpose** | Keeps the speed, with or without Warp; Formants in Tones (§8.1) |
| AE18 | **Key** | Found, shown and correctable; Follow key per clip, off by default, a setting for loops (§8.2, §8.3) |
| AE19 | **Slicing** | Into clips on the track now; into a Sampler with M4 (§9) |
| AE20 | **Tools** | The piano roll's Tools pane, with Quantise, Tempo, Normalise and Slice; *at approval (§20):* opened by each tool's own button on the bar, not one Transform button (§11) |
| AE21 | **A menu** | A new Audio menu between Notes and Song (Alt+A), opened from Audio ⌄ too; keys shared with Notes go to the clip shown (§13) |
| AE22 | **The stretcher** | Signalsmith Stretch (MIT, already built in), with its formants patched in; Rubber Band only if Tones falls short (§15, §17) |
| AE23 | **Next** | Stem separation, with audio to MIDI, designed as the step after P6, its models in the Library (§17) |
| AE24 | **Assistants** | The tools of §14 |
| AE25 | **Stages** | P6a, P6b (its spike first), P6c, each signed off; a visual mockup before P6a (§16) |

## 19. Tests (Done when)

- **`libs/dsp` (`[fast]`):**
  - onsets within 5 ms of the true ones in generated drum patterns, more of them as sensitivity rises;
  - the tempo of generated loops between 60 and 180 BPM within 0.1 BPM;
  - percussive and sustained material told apart (drum patterns from pads and held notes);
  - the pitch-class profile and the key of generated chord progressions.
- **`model/AudioEditing` (`[fast]`):** marker drags, slides and their limits; time handles; quantise at each strength; Warp from here; the downbeat; every range command; slice positions; normalise gains (each, together); the gain line through loops, warps, splits and reverse.
- **The engine:**
  - the spike's eight points (§15);
  - Beats plays each piece from its transient (a click track slowed down keeps every click sample for sample);
  - Follow key moves by the smallest step;
  - gain, the gain line, polarity and channels exact to the sample;
  - Reverse.
- **Assistants:** each tool of §14 driven through MCP.
- **UI:**
  - the bar at 1800, 1280, 1024, 820, 700 and 600 px, nothing touching, options giving way in order;
  - the level column, the marker strip with dense transients, the Tools pane's audio tiles, the AUDIO section at the inspector's narrowest;
  - screenshots zoomed in, for each stage;
  - the menu-bar test and the shortcut test.
- **Performance:**
  - the arrangement's benchmark with 32 tracks of warped loops within 8 ms a frame;
  - the editor with a 10-minute stereo file within the frame budget;
  - 32 warped clips playing within the audio budget.

## 20. Approved, with the tools on screen (2026-10-15)

The product owner approved every recommendation of §18 and added a rule, learned from the piano roll:
- "We should try to keep tools easily accessible in the work area, on screen. Piling everything in submenus leads to bad UI, things I use often should not be buried 2 clicks deep."
- "And if there is room on screen in the tool bars, why keep them empty? There is no benefit."

**What it changed here** (the rule itself is in [ui-principles.md](ui-principles.md) §1, for every editor):
- **The bar (§4)** holds what an audio editor is used for, one click each: Gain, Transpose and Reverse; ×2 and ÷2 beside the tempo; Tempo, Normalise and Slice as buttons of their own. The design had them in the inspector, the tempo's menu and the Tools pane, two clicks away.
- **A range's commands** (Split, Crop, Silence, Fade, Loop) come onto the bar while a range is selected, and the range has a gain handle of its own (§5). The design had them in the Audio menu and the right-click menu.
- **The Tools pane (§11)** opens at a tool from that tool's button; there's no Transform button for audio.
- **What stays in the menus and the inspector:** what's set once or rarely: Fine, Polarity, Channels, Formants, Tails, Sensitivity, Follow key, the marker commands (on the marker's right-click, where it acts).
- **The mockup** shows the bar at standard and compact widths, so what gives way, and when, is reviewed before it's built.

## 21. P6a built: the editor and the clip's sound (2026-10-15)

Built on the branch, with its tests (the model's `[fast]` ones, the engine's, the assistants' and the editor's by mouse) and screenshots reviewed zoomed in at 1800, 1280, 1024, 820, 700 and 600 px. Signed off by the product owner (2026-10-15): "Everything looks good! Signed off."

### 21.1 Settled while building

- **It opens on selection:** one click on an audio clip shows it in the Clip view, as a MIDI clip shows the piano roll ([selection.md](selection.md) §4).
- **A row for each signal the clip plays,** not each channel of its file: two for Stereo and Swapped (named L and R as they play), one for Left, Right and Mono (Mono draws the two channels' mean). What you see is what's heard.
- **The level column** follows the mockup: each row's top edge is labelled with the level it stands for (0 at full scale), −∞ is in its middle, and the steps between (−3, −6, −12 …) show where they fit, a label's height and an item gap apart, with their ticks mirrored below the middle. Dragged up 48 px, the rows draw twice as tall (1× to 64×); a double-click, or View ▸ Waveform height ▸ Reset, puts full scale back at the row's edge. The rows' height is the session's view, not kept with the project.
- **The marker strip comes with the markers** (P6b), so P6a has no empty strip. So do the range's time handles in the ruler (§7.6): in P6a a range is the `selectionFill` over the rows with a `selectionStroke` line at each edge.
- **The range's gain handle** hangs from the top of the first row at the range's middle: 0.1 dB a pixel (Shift: 0.01), its tag "Range +3.0 dB", one undo step (Gain). Gain ▸ Up and Down act on the range the same way while there's one.
- **What a range command leaves:** Split selects the range's new clip and the editor shows it; Crop and Delete show what's left. A command that can't act is dimmed on the bar, saying why: Fade on a range in the clip's middle, Delete and Crop on a clip with takes (its parts are one clip), Loop on a clip with takes. Edit ▸ Split reads "Split at range" while it splits at the range's edges.
- **The gain line uses the volume fader's taper:** −∞ to +6 dB, 0 dB four fifths up the lane, as a volume lane reads. The design said +12 dB; more than +6 is the clip's Gain's job (up to +24 dB). Its points are kept in the file's time, so they loop, split and flip with the audio; a reversed clip shows its line mirrored.
- **Reverse writes a reversed copy of its own** into the project's `Reversed` folder (32-bit float, reused while it matches its file), the clip's forward file kept beside it; its offset and loop mirror so it plays the same audio. Not tracktion's `setIsReversed`: its copy lived in a folder the project didn't keep. A clip with takes plays its takes, so its Reverse is dimmed, saying why. The inspector's format is the forward file's.
- **Normalise:** Audio ▸ Normalise… opens the Tools pane at it, as §13 has it (§6.4 said "with the last settings"); it measures what the clip plays (its loop, while it loops). The bar's Normalise is lit while the pane shows it; clicked again, the pane closes. While it's set, the waveforms show the result; it's heard once applied.
- **The bar** in P6a: a range's commands while there's one; Audio ⌄; Normalise; Gain and Reverse; Snap and the step. Gain is a `BarField` as wide as its value, so the gap after it is the bar's own. At narrow widths the clip's name gives way first (never to a bare "…"), then Normalise, Reverse and Gain. Quantise, Tempo, Slice, Transpose and the warping controls come with P6b and P6c.
- **The inspector's AUDIO:** Gain, Reverse, Polarity, Channels (stereo files), then the file in two rows so neither is cut: **File** ("stereo · 4.0 s") and **Format** ("48 kHz · 24-bit").
- **Waveforms drawn direct are drawn at the screen's pixels,** a column per physical pixel as the arrangement's tiles are, so the editor and the arrangement's fades are as sharp on a dense screen.
- **The waveform's right-click menu:** Split, Crop, Delete, Silence, Fade, Loop; Gain ▸, Normalise…, Reverse; then Audio ▸ (the whole menu) and Grid ▸. The lane's head and View ▸ Clip view lane ▸ list Gain.
- **Assistants:** `set_audio` (gain, gain by, reverse, polarity, channels), `get_gain_line`, `set_gain_line`, `edit_audio_range`, `list_audio_tools`, `transform_audio` (Normalise); `get_clip` gives an audio clip's `sound` and `file`.

### 21.2 Left for later in P6

- **Several audio clips in AUDIO** (§12: a range of values where they differ, moved together): the section shows for one clip; the Audio menu and the arrangement's gain handle already act on several.
- **Normalise heard while it's set,** not only seen.
- **Crossfades drawn through the clips' gain:** a crossfade's region in the arrangement draws its sum without the two clips' gain and gain line; what plays has them.
- **The editor's frame time in a Release build:** the hidden benchmark ("audio editor paints a 10-minute stereo file zooming", §19) paints at the arrangement's cost a pixel in the dev build (89 ms a frame at 3400 × 840, the arrangement's 226 ms at 3400 × 2104), after the faded file and the played part each read only their own columns; the Release reading waits for a `rel` build.
