# Recording: design (M2 step C5)

How Segno records audio and MIDI: projects as folders, inputs and arming, monitoring, the record button with count-in and punch, takes on take lanes and comping, capturing MIDI played without recording, and what comes later. The decisions are [ADR 0030](../decisions/0030-projects-are-folders.md) (a project is a folder), [ADR 0031](../decisions/0031-recording-keeps-every-take.md) (every take is kept; the track plays plain clips), [ADR 0032](../decisions/0032-takes-belong-to-clips.md) (takes belong to clips) and [ADR 0033](../decisions/0033-no-instrument-by-itself.md) (Segno never adds an instrument by itself). It builds on one kind of track ([ADR 0019](../decisions/0019-one-kind-of-track.md)), live MIDI following the selection ([daw-shell.md](daw-shell.md)), the track header kit and time-view kit ([ui-foundations.md](ui-foundations.md), ADR 0023), the selection model ([selection.md](selection.md), ADR 0027), the mixer ([mixer.md](mixer.md)) and automation recording ([automation.md](automation.md) §8).

**Status:** approved by the product owner (2026-10-10), with every recommendation of §16. Built in parts (§17): C5a and C5b built (2026-10-10); C5c after C6's fades, with selecting several take lanes at once (§6: for now a take lane is selected alone). What comes after M2 is mapped in §15.

**Revised after trying C5b (product owner, 2026-10-10, §19):** takes belong to their clips (ADR 0032); Capture MIDI captures the last phrase onto any track and never adds an instrument (ADR 0033); punch is the loop brace; the top bar is sectioned, with automation arm in red beside Record and no "Turn automation back on" button. The sections below describe the revised design, built the same day (C5b revised).

**Where the field is (2026):** every current DAW keeps loop takes and lets you comp them (Live 12's take lanes, Logic's take folders with swipe comping, Cubase's lanes, Studio One's layers, Reaper's fixed lanes, Bitwig's comp lanes); most capture MIDI played while not recording (Live, Logic, Cubase, Bitwig, Reaper). Segno matches that (ADR 0011) and goes further where the integrated DAW helps: nothing a pass replaces is ever lost, takes move to new tracks in one action for doubles and layers, and assistants can compare takes by rendering and measuring them (§12).

## 1. Principles

1. **Never lose a performance.** Every pass and every loop lap is kept as a take. What a recording replaces is kept too. MIDI played while not recording can be captured afterwards. Undo never deletes a recorded file.
2. **Afterwards, a recording is ordinary clips.** A track plays its main lane: plain clips, never overlapping (ADR 0019). A clip's takes belong to it and open under it, and choosing or comping copies their parts into the main lane. Everything that edits clips (moving, splitting, fades, bounce) works on recordings unchanged.
3. **Arm what you play.** Selecting an instrument track arms it for live MIDI, as today. Arm buttons arm anything else, and several tracks at once.
4. **What you hear is what you get, in time.** Monitoring goes through the track's chain. Recordings are placed compensating the interface's latency, so they line up with what you heard.
5. **One way to do each thing; nothing modal.** Recording options live in the Record button's menu and the inspector, not in dialogs.

## 2. Projects are folders (ADR 0030)

Recorded audio needs a home next to the project, as in every modern DAW.

```
Song/
  Song.segno        the project
  Recordings/       recorded audio (and take files)
  Bounces/          bounce in place (C7)
  Samples/          files copied in by "Collect all and save" (later, §15)
```

- **Saving a new project** asks for a name and a place, and creates the folder `Name/` holding `Name.segno`.
- **Before the first save,** recordings go to a temporary project folder (`Documents/Segno/Unsaved/<date and time>/`). Saving moves them into the new project folder. If Segno quits without saving, they stay there, and crash recovery (M7) finds them.
- **Clips refer to files in the project folder relative to it**, so a project folder can be moved, copied or zipped as a whole; files elsewhere keep their full path.
- **Opening a project** opens `Name.segno`; Segno finds the folder from it. A loose `.segno` from before C5 keeps working, with its recordings in a `Recordings` folder beside it, until it is saved with Save As.
- **Files outside the folder** (audio dropped from elsewhere) stay where they are until "Collect all and save" (later, §15).
- **Nothing is deleted behind your back:** undo, deleting clips and deleting takes never delete files. "Remove unused recordings" (later, §15) cleans up on request, to the system's trash.

## 3. Inputs

A track can listen to an audio input and a MIDI input (ADR 0019); it records one kind at a time, because a track's clips never overlap.

| Setting | Choices | Default |
|---|---|---|
| **Audio input** | None, or each audio input as the audio device presents it: mono inputs, or stereo pairs where the device is set up so (Audio settings), with the driver's names | None |
| **MIDI input** | All MIDI inputs, each MIDI device, Computer keyboard, None | All MIDI inputs |
| **MIDI channel** | All, 1–16 | All |
| **Monitor** | Auto, In, Off (§4) | Auto |
| **Records** | MIDI or Audio, shown only when a track has both inputs (a vocoder: keys and voice) | MIDI with an instrument, else Audio |

- **Where:** the inspector's TRACK section (an INPUT group), and a compact input row at the top of each mixer strip. A track's header shows its input in its subtitle while armed ("In 1/2", "Keyboard").
- **What it records follows from the inputs:** an instrument and a MIDI input records MIDI; an audio input without an instrument records audio. No track type is chosen.
- **Audio inputs** come from the audio device's enabled inputs (Audio settings, Ctrl+,). Stereo pairs record stereo files; mono inputs record mono files.
- **MIDI from every device** reaches an armed track through "All MIDI inputs" (one merged input, so notes never double, as today); picking one device, or a channel, separates players or controllers.

## 4. Arming and monitoring

**Arming**
- **The arm button** is the third button of the track header kit, after mute and solo: a dot that fills in the `record` colour when armed. It is in the inspector and the mixer strips too. Alt+R arms the selected tracks.
- **A click arms that track alone; Ctrl+click arms or disarms one more.** Several armed tracks record together, each from its own input.
- **Selection still arms live MIDI** (B14): selecting an instrument track arms it softly (a ring, not a fill), so you can play it at once. Once any track is armed by its button, selecting other tracks doesn't move the arm.
- **Recording records the armed tracks,** or with none armed, the softly armed (selected) one.
- **Arming or disarming while recording** punches that track in or out at once.
- **Armed audio tracks meter their input** in the header and the strip when nothing is playing back on them, so levels can be set before recording; the meter's clip light shows input overs.

**Monitoring** (per track)
- **Auto** (default): you hear the input while the track is armed. MIDI always plays the instrument while armed, as today.
- **In:** always hear the input, armed or not (playing through effects, a live set).
- **Off:** never (when the interface monitors directly).
- **The input goes through the track's chain,** volume and sends: you record dry and hear it with its effects.
- **Latency you hear** is the interface's plus any latency in the chain. "Reduced latency when monitoring" (later, §15) bypasses modules that add latency on monitored tracks.

## 5. Recording

| Action | How |
|---|---|
| **Record** | The Record button in the top bar's centre (after play and stop), Ctrl+R or F9. From stopped: plays from the playhead (after the count-in) and records the armed tracks. While playing: punches in at once; pressing it again punches out and keeps playing |
| **Stop** | Space or the stop button: ends the pass |
| **Count-in** | The Count-in button beside the metronome (Ctrl+Alt+C) turns it on or off; its right-click menu (and the Record button's) sets the length: 1 beat, 2 beats, 1 bar (default), 2 bars. A setting kept between projects. Only from a stopped transport. The metronome clicks during the count-in even when it's off, and the position display counts down |
| **Loop recording** | With looping on, **recording happens only inside the loop brace** and each lap becomes a take (§6). Start before the brace for a lead-in: you hear it, but it isn't recorded. The loop must be at least 2 seconds. (This is punch in/out: there is no separate punch toggle) |
| **MIDI loop mode** | The Record button's menu: **Each lap: a new take** (default) or **Each lap: add to the clip (overdub)**: what you play merges into the MIDI clip already there, for building a beat lap by lap; no takes |

- **While recording,** each recording track shows its new clip growing in the `record` colour, drawing the waveform or the notes as they arrive.
- **When a pass ends,** each track has a clip named after the track. It is selected, so the inspector shows it.
- **One undo step per pass** ("Record"): clips, takes, and any automation recorded in the same pass (automation arm, automation.md §8).
- **Files:** WAV, 24-bit, at the project's sample rate (Audio settings: WAV, AIFF or FLAC; 16, 24 or 32-bit float). Named after the track and the pass: `Recordings/Vocals 3.wav`; loop laps `Vocals 3_take_2.wav`.
- **In time:** recordings are moved back by the latency the interface reports. Audio settings add a manual **recording offset** (audio and MIDI, ±500 ms) for interfaces that report it wrongly; a measured round-trip test comes later (§15).
- **Disk:** recording warns when the disk holds less than ten minutes at the current settings. If writing fails, recording stops and keeps what was written.

## 6. Takes (ADRs 0031, 0032)

A take is another version of a clip, so takes belong to their clip, as in Logic's take folders and Reaper's takes.

```
┌ header ───────────┬ lanes ──────────────────────────────────────────────────┐
│▌ Vocals   M S ●   │ ▐ Vocals ───────────── 3/4 ▾ ▌   ▐ Vocals ─────────────▌ │
│                   │ ▐ ∿∿∿∿∿∿∿∿∿∿∿∿∿∿∿∿∿∿∿∿∿∿∿∿∿∿ ▌   ▐ ∿∿∿∿∿∿∿∿∿∿∿∿∿∿∿∿∿∿∿ ▌ │
│                   │ ░ Take 4 ░░░░░░░░░░░░░░░░░░░░░    one take: a plain clip  │
│                   │ █ Take 3 ████████████████████                            │
│                   │ ░ Take 2 ░░░░░░░░░░░░░░░░░░░░░                            │
│                   │ ░ Take 1 ░░░░░░░░░░░░░░░░░░░░░                            │
└───────────────────┴──────────────────────────────────────────────────────────┘
```

- **A pass makes a clip with its takes:** what it recorded, one take per loop lap; the clip plays the newest complete lap. A lap cut short is kept too, ending where its recording does.
- **One take shows nothing:** such a clip looks and acts like any clip.
- **Recording over a clip adds to its takes.** What a pass records over that has no takes yet is kept as a take of the clip first ("Before take N", trimmed to the pass), so nothing is lost. A pass over part of a clip with takes joins them: the clip's range widens to cover both, and it plays the new take where it recorded and its earlier take elsewhere.
- **The badge:** from two takes, the clip's title strip ends with the take it plays and how many, then a chevron: "3/4 ▾" ("Comp ▾" when its parts play different takes). On a narrow clip it shrinks to the chevron before the clip's name is cut. **A click shows or hides the clip's takes; a right-click opens the take menu:**
  - Show takes / Hide takes;
  - the takes, newest first, the one playing ticked: choosing one plays it;
  - Rename take…;
  - Copy take to new track, Move take to new track, Spread takes to new tracks (new tracks copy this track's chain and mix: a double or a layer in one action);
  - Delete take, Keep only this take (the others are deleted; files stay).
- **Takes open under their clip** (a click on the badge, the menu, or Ctrl+Alt+K with the clip selected): one strip per take (24 px), newest on top, across the clip's range. The track's row grows by the tallest open set of takes and shrinks back when they close; the header column shows nothing more. Opening a clip's takes closes any overlapping ones already open on that track. They open by themselves after a pass that made several takes.
  - Each strip shows its take's name, the parts the clip plays in the track's colour and the rest dimmed.
  - A click on a strip plays that take and selects it; right-click opens the take menu for it; Delete deletes it.
- **Choosing while it plays:** Alt+↑ / Alt+↓ on a selected clip play its previous or next take (skipping takes too short for its range), so you can audition them in turn.
- **A clip and its takes are edited as one,** in the same undo step:

| Command on the clip | Its takes |
|---|---|
| Move (in time or to another track) | Move with it |
| Trim its start or end | The clip's range follows; the takes are untouched, so trimming back out brings them back |
| Split | Split at the same point; the right halves become the right clip's takes |
| Duplicate | Copied |
| Delete | Deleted with it (undo brings them back; files stay) |
| Another clip placed over part of it (ADR 0019) | The clip is cut; its takes stay whole, so choosing a take there fills it again. A clip cut in two keeps its takes on the left part; the right part becomes a plain clip |

- **A clip playing parts of several takes** (a partial pass, or comping in C5c) draws as one clip: one title strip, a thin line where the take changes. Its parts select, move, split and delete together; only its outer edges trim. Moving the line between parts is comping (§7).
- **MIDI takes** work the same. In overdub mode laps merge into one clip and make no takes.

## 7. Comping

Choose the best parts of several takes into one performance, as Logic's swipe comping and Live 12's take lanes do.

- **Drag across a take's strip** to use that range of the take: the main lane takes it, cutting what was there (ADR 0019), with short crossfades at the joins (C6's clip fades).
- **Strips show what's in use:** the parts of each take that the clip plays are drawn in the track colour, the rest dimmed (since C5b).
- **Every swipe is one undo step.** Double-click a part to use the whole take there.
- **The main lane stays plain clips:** comping makes clips that point into the take files, so a comp is edited, faded, bounced or exported like any clip, and needs no rendering.
- **Flatten** is just "Delete unused takes" (§6).

## 8. Capture MIDI

- **Segno always listens** to every MIDI input and the computer keyboard, whether or not a track is armed or has an instrument: the last 2 minutes of notes, pedal, bends and controllers.
- **Capture MIDI** (the button beside Record, lit while there's something to capture; Ctrl+Shift+R) turns **the last phrase** into a clip: what was played since the last pause of 2 bars or more (at the project's tempo) with no note or sustain pedal held. While the transport plays, nothing from before it started counts. So a note, 30 seconds of silence and then a phrase capture just the phrase.
- **Never twice:** a capture, or a recording pass, uses up what was played before it.
- **Where it goes:** each armed MIDI track gets what came in on its input and channel; with none armed, the selected track, whatever it holds; with nothing (or a return or the master) selected, a new track. **No instrument is ever added** (ADR 0033): a track without one gets a silent MIDI clip, until you add an instrument. The button's tooltip names where it goes ("Capture MIDI to Bass").
- **Where it lands:** played while the transport played, where it was played, against the song; otherwise at the playhead, on the beat of the first note. The clip runs from the beat before the first note to the beat after the last. One undo step.
- Later (§15): when the project is empty and stopped, set the tempo from what was played.

## 9. The top bar and menus

The top bar ([daw-shell.md](daw-shell.md), "Top bar") puts the transport in its centre: play, stop, **Record**, **Capture MIDI**, Automation arm, the position display, Loop and Follow; Metronome and **Count-in** sit with the tempo on the left.
- **Record button:** filled in `record` while recording; its right-click menu (also the command palette) holds Count-in ▸ (the lengths), MIDI loop recording ▸ (Each lap: a new take, Each lap: add to the clip) and Recording settings… (Audio settings).
- **Capture MIDI button:** beside Record, dim until there's something to capture; its tooltip names where it will go.
- **Count-in button:** beside the metronome; on and off, right-click for the length.
- **Position display:** counts the beats down during a count-in ("−2"), in `record`.

## 10. Selection and the context panels

- **A track's inspector** gains INPUT (audio input, MIDI input and channel, Monitor, Records) between TRACK and MIX.
- **A take** (a click on its strip) is selected with its track as the context: the inspector shows TAKE (name, start, length, how much of it is in use, "Use this take") above the track's sections, and the detail panel the track's modules.
- **Delete** with a take selected deletes the take (one undo step; the file stays).
- **A clip with several parts** is selected whole (§6): the inspector's CLIP shows it as one clip.

## 11. Keyboard and actions

| Action | ID | Shortcut |
|---|---|---|
| Record | `transport.record` | Ctrl+R, F9 |
| Capture MIDI | `transport.capture` | Ctrl+Shift+R |
| Arm selected tracks | `track.arm` | Alt+R |
| Count-in on/off (the last length) | `transport.countIn` | Ctrl+Alt+C |
| Show takes of the selected clip | `view.takes` | Ctrl+Alt+K |
| Previous / next take of the selected clip | `take.previous` / `take.next` | Alt+↑ / Alt+↓ |
| Spread the selected clip's takes to new tracks | `take.spread` | Ctrl+Alt+Shift+K |

## 12. Assistants (MCP)

Every feature comes with its tools and a test through MCP (ADR 0021). Assistants can't play, but they can set up and run a recording for you, and help choose takes.

| Tool | Arguments | Result |
|---|---|---|
| `set_track_input` | `track`, `audioInput?`, `midiInput?`, `channel?`, `monitor?`, `records?` | The track's inputs; the inputs available (`get_project` lists them too) |
| `arm_tracks` | `tracks`, `on?` | Which tracks are armed. Not an undo step |
| `record` | `countIn?`, `bars?` (stop after this many bars) | Starts recording (inside the loop brace while looping); `stop` ends it. The result lists each recording track |
| `capture_midi` | `track?` (used when nothing is armed) | The clips made and their tracks |
| `get_takes` | `track` or `clip` | Per clip with takes: its range, parts, whether open, the take it plays (or "comp"), and each take's name, clips, file and the parts in use |
| `use_take` | `track`, `take` (C5c: `from?`, `to?`) | The clip plays that take across its range. One undo step |
| `move_takes_to_tracks` | `track`, `takes`, `copy?` | The new tracks, with plain clips |
| `delete_takes` | `track`, `takes`, `unused` or `keep` | Files stay |

**Comparing takes:** `render` and `measure_mix` already render and measure a time range; with `get_takes`, an assistant can render each take, compare level, timing against the grid or pitch, and suggest one ("take 3 is the most even"). The user decides; `use_take` applies it.

## 13. Model and view

- **`model::Takes`** (JUCE-free, `[fast]` tests): what a clip plays of each take, the badge ("3/4", "Comp"), the strips' height and hit-testing, a clip's parts as one group, stepping through takes, take numbering, and which take replaces a deleted one.
- **`model::StackEdits`** (`[fast]`): what each clip command does to the clips' takes (moves, outer trims, splits, copies) and how a track's takes settle afterwards (an emptied set goes; parts cut off become plain clips).
- **`model::Capture`** (`[fast]`): the last phrase, where a capture goes and where it lands.
- **The snapshot:** `TrackInfo` gains its inputs, monitor mode, armed state and `stacks` (each clip's takes: range, open, takes, parts); `ClipInfo` its stack and the take clip it plays. Recording in progress is read every frame (`liveRecordings`).
- **Rows** (`model::ArrangementRows`): tracks, lanes, returns and the master; a track's row is taller by its open takes (`TrackLayout`'s extra height).
- **Drawing:** strips reuse the clip painting (waveforms through the waveform cache, notes), dimmed with `clipDimmed` except the parts in use; the recording clip draws from the engine's live thumbnail.

## 14. Engine (tracktion)

- **Inputs:** each track's inputs are tracktion input-device instances targeting its track (`InputDeviceInstance::setTarget`), armed with `setRecordingEnabled`. Audio inputs are tracktion's wave inputs per channel or pair; MIDI uses the virtual "All MIDI Ins", single devices or the computer keyboard, as live MIDI does today.
- **Recording:** `TransportControl::record` and `stopRecording`; `TransportControl::Listener::recordingFinished` hands over the new clips. Segno then turns tracktion's per-clip loop takes into the clip's takes, keeps what the pass replaced, and updates the main lane, all inside the pass's undo step. While looping, tracktion's loop recording records only the brace; a small patch (`cmake/patches/tracktion_engine-loop-record-lead-in.patch`) rolls playback into the loop from a playhead before it, the lead-in, keeping the count-in's clicks to their own range. tracktion's own punch (`Edit::recordingPunchInOut`) stays off: it can't be combined with loop recording and punches out after one lap.
- **Takes are Segno's own state** in the project, written through the undo manager like automation lanes (ADR 0029): a track's `FDTAKES` holds a `STACK` per clip with takes (its range `from`/`to`, and `fdOpen`, view state), its `TAKE`s (name) and their clip states (absolute times, not played). A main-lane clip names the take clip it plays (`fdTake`), and through it its stack. Every clip command carries the stacks of the clips it edits in the same undo step. tracktion's own take switching and comp manager need a tracktion Project for audio and would render comps; Segno's takes need neither, and a comp is plain clips.
- **Projects from C5b** (format 3, take lanes per track) are converted when opened: overlapping takes become one clip's takes.
- **Files:** `EngineBehaviour::getFileForNewAudioRecording` names each file in the project's `Recordings` folder; tracktion splits loop recordings into one file per lap. Format and bit depth through `WaveInputDevice`. Segno writes relative source paths itself (relative to the project's folder): tracktion resolves them from the folder but writes them from the project file.
- **MIDI modes:** "a new take" is tracktion's "neither merge nor replace" (a clip per lap); overdub is its merge. Replacing is always Segno's (through takes), never tracktion's.
- **Monitoring and MIDI channels per track:** tracktion sets monitoring per input device, and its MIDI channel setting rewrites channels. A small engine patch (`cmake/patches/tracktion_engine-per-track-inputs.patch`) gives each track's input destination its own monitoring and MIDI channel (heard and recorded); Segno routes every track's inputs from its settings (`Session::Impl::applyInputs`).
- **Count-in** is `Edit::setCountInMode` (a global setting in tracktion, matching a setting kept between projects).
- **Latency:** tracktion moves audio by the driver's reported input and output latency and the graph's latency, plus the manual offset (`setRecordAdjustmentMs`); MIDI by its manual offset (`setManualAdjustmentMs`) minus the graph's latency.
- **Capture MIDI:** Segno's own buffer, fed by a listener on each physical MIDI input (`PhysicalMidiInputDevice::Listener`, called for every message) and by the computer keyboard, through lock-free queues; tracktion's retrospective buffer is off, because it only fills tracks the input is routed to.
- **Live display:** `RecordingThumbnailManager` for waveforms, `getRecordingNotes` for notes.
- **Engine issues avoided:** tracktion drops audio takes it can't resolve when switching takes without a Project, and lists a loop's first lap twice; Segno reads the takes once, deduplicated, and never switches tracktion takes.

## 15. What comes when

| Part | Scope | When |
|---|---|---|
| **C5a Recording** | Project folders; inputs; arm buttons and soft arm; monitoring; Record, count-in, punch; several tracks at once; latency placement and offsets; input metering; live drawing; MCP | M2 |
| **C5b Takes** | Every pass and lap kept as the clip's takes; what a pass replaces kept; choosing and auditioning takes; copy, move and spread takes to new tracks; keep only one take; MIDI overdub; Capture MIDI; MCP | M2 |
| **C5c Comping** | Swipe comping across a clip's take strips with crossfades (after C6's fades); MCP `use_take` with ranges; selecting several takes to copy or move them together (what's in use is shown since C5b) | M2, after C6 |
| Collect all and save; remove unused recordings | Copy outside files into the project folder; clean up unused files to the trash | M3 (with the browser) |
| Reduced latency when monitoring | Bypass modules that add latency on monitored tracks | M3 |
| Round-trip latency test | Play a click, record it back through a loopback, set the offset | M3 |
| Record quantise | Quantise MIDI as it's recorded | M4 (with groove and quantise) |
| Resampling | A track's or the master's output as an input | M4 |
| External instruments | A hardware synth as a module: MIDI out and its audio back in, latency compensated | M4 |
| Comping several tracks together | One comp across a group (multi-mic drums), from tracktion's track comp groups | M4 |
| MPE recording | Per-note expression | M5 (MPE across the DAW) |
| Recovery of unsaved recordings | Crash recovery finds them | M7 |
| **Ideas beyond the field** | **Capture audio** (armed audio inputs always buffered, like Capture MIDI); **stack mode** (each loop lap straight onto a new track); **tempo from captured MIDI**; **record trigger** (start when the input crosses a level); assistants that **rank takes** by analysis | [innovation-backlog.md](../innovation-backlog.md) |

## 16. Decisions from the review (product owner, 2026-10-10: every recommendation agreed)

| # | Question | Answer |
|---|---|---|
| Q1 | **Project format** for recordings | A project is a folder holding `Name.segno` and `Recordings/` (ADR 0030) |
| Q2 | **What a track records** when it has both inputs | One kind at a time (ADR 0019: no overlaps); a "Records" choice, MIDI by default with an instrument |
| Q3 | **Arming** | Arm buttons, exclusive by click, Ctrl+click for more; selection still soft-arms live MIDI unless a track is armed by its button (B14) |
| Q4 | **Monitoring** | Per track: Auto (default), In, Off |
| Q5 | **Record shortcut** | Ctrl+R and F9 (single letters stay free for the computer keyboard) |
| Q6 | **Count-in** | Off by default; 1–2 beats or 1–2 bars in the Record menu, kept between projects |
| Q7 | **Punch range** | The loop brace (no second range); since the C5b review, looping *is* punching: no separate toggle (§19) |
| Q8 | **Loop takes** | Every lap kept on its own take lane; the main lane plays the newest |
| Q9 | **MIDI loop recording** | New takes by default; Overdub as an option |
| Q10 | **Takes model** | Segno's own state; the main lane is plain clips (ADR 0031); takes belong to their clip (ADR 0032, after the C5b review) |
| Q11 | **Recording over existing clips** | They are kept as a take lane first ("Before take N"), so nothing is lost |
| Q12 | **Comping** | Swipe on take lanes, crossfaded joins; in M2 after C6 |
| Q13 | **Stacking** | Copy, move or spread takes to new tracks that copy the track's chain and mix |
| Q14 | **Capture MIDI** | In M2 (C5b), 60 s, while playing or stopped |
| Q15 | **File format** | WAV 24-bit by default; AIFF, FLAC and 16/32-bit float in Audio settings |
| Q16 | **Files and undo** | Undo and deletes never remove files; cleanup on request, later |

## 17. Building it

1. **Spike** (first): record audio and MIDI into a test project through `test_utilities::EnginePlayer`; loop takes read back from tracktion's clip; per-track monitoring (patch or gating); latency placement against a loopback in the test (a known offset).
2. **C5a**, then **C5b**, then (after C6) **C5c**, each built with its MCP tools and tests.

## 18. Tests (Done when)

- **Recording:** audio from a test input and MIDI from a virtual input land on the armed tracks as clips, at the right time (a known latency moved back exactly), in the project's `Recordings` folder; several tracks at once; count-in starts the pass after its bars; punch records only inside the brace; arming while recording punches in.
- **Takes:** a loop pass of three laps makes a clip with three takes, the newest playing; what a pass records over is kept as a take; a partial pass joins a clip's takes; moving, trimming, splitting, duplicating and deleting a clip carry its takes; choosing, auditioning, starring, deleting, copying, moving and spreading takes (new tracks copy the chain); MIDI overdub merges laps; C5b projects convert.
- **Capture MIDI:** only the last phrase; onto a track without an instrument (none added); armed tracks by input and channel; a new track when nothing is selected; where it was played while playing.
- **Comping:** a swipe makes main-lane clips from the take's file at the right offsets with joins faded; each swipe is one undo step; the in-use ranges are right (`[fast]` for the model).
- **Undo:** a pass is one step (clips, take lanes, automation); undo never deletes files; redo restores the clips.
- **Capture MIDI** makes a clip of the last notes, playing and stopped.
- **Projects:** a saved project folder moved elsewhere opens with its recordings; recordings made before the first save move into the new folder.
- **MCP:** each tool in §12.
- **Performance:** recording 16 tracks at once at 48 kHz keeps the frame budget and never drops audio (Release).
- **Manual:** `manual-tests/M2.md`, "Recording (C5)".

## 19. Review of C5b (product owner, 2026-10-10: agreed, with changes)

Trying C5b, the product owner found takes in the wrong place (a take is another version of a clip, not of a track), "Takes · N" cluttering headers even for a single take, Capture MIDI doing nothing on a track without an instrument and capturing a whole minute, the automation buttons unexplained, and the top bar crowded on the left. The design above (§5, §6, §8–§14) is the result.

| # | Question | Decision |
|---|---|---|
| R1 | Where takes live | On their clip (ADR 0032) |
| R2 | A clip with one take | Shows nothing about takes |
| R3 | Seeing every take | Under the clip; the track's row grows while they're open |
| R4 | A pass over part of a clip with takes | Joins its takes; the clip plays both parts, as one clip |
| R5 | What Capture MIDI takes | The last phrase (after a pause of 2 bars), and only since playback started while playing |
| R6 | Where it goes | Armed tracks, else the selected track, else a new track. **No instrument is added** (ADR 0033: Segno never adds an instrument by itself, for drawn clips and dropped MIDI files either) |
| R7 | The automation buttons | Automation arm lights in `record`, beside Record and Capture. **No "Turn automation back on" button:** moving a control turns off just its parameter's assignment, and the lane's own switch turns it back on; "Turn all automation back on" stays in the palette (Ctrl+Alt+A) |
| R8 | Punch in/out | Tied to the loop: while looping, only the brace records. The punch toggle, its shortcut and its menu item go |
| R9 | The top bar | Three sections over the whole width: song settings left (tempo, signature, metronome, count-in), the transport centred (play, stop, record, capture, automation arm, the position, loop, follow), views and app right ([daw-shell.md](daw-shell.md), "Top bar") |
| R10 | The MIDI loop recording labels | "Each lap: a new take" and "Each lap: add to the clip (overdub)" |
| R11 | Stars on takes | Removed after trying them (product owner, 2026-10-10): clutter that didn't earn its place |
| R12 | The badge's clicks | A click shows or hides the clip's takes; a right-click opens the take menu |

**Storage details** (from the implementation review): a clip's takes keep their own range (`from`/`to`), so trimming the clip and trimming back out loses nothing; a clip cut in two by another clip keeps its takes on its left part.

**Built** (2026-10-10, before C7): no instrument added; punch tied to the loop (with a lead-in, through a small engine patch), arm in red, the new labels; the sectioned top bar; takes on clips (model, engine and every clip command, the C5b conversion, MCP, the arrangement); Capture MIDI on its own buffer.
