# Recording: design (M2 step C5)

How Segno records audio and MIDI: projects as folders, inputs and arming, monitoring, the record button with count-in and punch, takes on take lanes and comping, capturing MIDI played without recording, and what comes later. The proposed decisions are [ADR 0030](../decisions/0030-projects-are-folders.md) (a project is a folder) and [ADR 0031](../decisions/0031-recording-keeps-every-take.md) (every take is kept, on take lanes; the track plays plain clips). It builds on one kind of track ([ADR 0019](../decisions/0019-one-kind-of-track.md)), live MIDI following the selection ([daw-shell.md](daw-shell.md)), the track header kit and time-view kit ([ui-foundations.md](ui-foundations.md), ADR 0023), the selection model ([selection.md](selection.md), ADR 0027), the mixer ([mixer.md](mixer.md)) and automation recording ([automation.md](automation.md) §8).

**Status:** approved by the product owner (2026-10-10), with every recommendation of §16. Built in parts (§17): C5a and C5b built (2026-10-10); C5c after C6's fades, with selecting several take lanes at once (§6: for now a take lane is selected alone). What comes after M2 is mapped in §15.

**Revision proposed (2026-10-10, §19):** after trying C5b, takes move onto their clips ([ADR 0032](../decisions/0032-takes-belong-to-clips.md), proposed), Capture MIDI captures the last phrase onto any track, and the automation buttons move next to Record.

**Where the field is (2026):** every current DAW keeps loop takes and lets you comp them (Live 12's take lanes, Logic's take folders with swipe comping, Cubase's lanes, Studio One's layers, Reaper's fixed lanes, Bitwig's comp lanes); most capture MIDI played while not recording (Live, Logic, Cubase, Bitwig, Reaper). Segno matches that (ADR 0011) and goes further where the integrated DAW helps: nothing a pass replaces is ever lost, takes move to new tracks in one action for doubles and layers, and assistants can compare takes by rendering and measuring them (§12).

## 1. Principles

1. **Never lose a performance.** Every pass and every loop lap is kept as a take. What a recording replaces is kept too. MIDI played while not recording can be captured afterwards. Undo never deletes a recorded file.
2. **Afterwards, a recording is ordinary clips.** A track plays its main lane: plain clips, never overlapping (ADR 0019). Takes sit on take lanes under the track, and comping copies their parts into the main lane. Everything that edits clips (moving, splitting, fades, bounce) works on recordings unchanged.
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
| **Record** | The Record button in the transport bar (between play/stop and loop), Ctrl+R or F9. From stopped: plays from the playhead (after the count-in) and records the armed tracks. While playing: punches in at once; pressing it again punches out and keeps playing |
| **Stop** | Space or the stop button: ends the pass |
| **Count-in** | Off, 1 beat, 2 beats, 1 bar, 2 bars (the Record button's menu; a setting kept between projects). Only from a stopped transport. The metronome clicks during the count-in even when it's off, and the position display counts down |
| **Punch in/out** | A toggle in the Record button's menu: records only inside the loop brace (the brace is the punch range, as in Live); playback starts at the playhead, so the bars before it are your pre-roll |
| **Loop recording** | With looping on, each lap becomes a take (§6). The loop must be at least 2 seconds |
| **MIDI loop mode** | The Record button's menu: **New takes** (default) or **Overdub**: each lap's notes merge into one clip, for building a beat lap by lap |

- **While recording,** each recording track shows its new clip growing in the `record` colour, drawing the waveform or the notes as they arrive.
- **When a pass ends,** each track has a clip named after the track. It is selected, so the inspector shows it.
- **One undo step per pass** ("Record"): clips, takes, and any automation recorded in the same pass (automation arm, automation.md §8).
- **Files:** WAV, 24-bit, at the project's sample rate (Audio settings: WAV, AIFF or FLAC; 16, 24 or 32-bit float). Named after the track and the pass: `Recordings/Vocals 3.wav`; loop laps `Vocals 3_take_2.wav`.
- **In time:** recordings are moved back by the latency the interface reports. Audio settings add a manual **recording offset** (audio and MIDI, ±500 ms) for interfaces that report it wrongly; a measured round-trip test comes later (§15).
- **Disk:** recording warns when the disk holds less than ten minutes at the current settings. If writing fails, recording stops and keeps what was written.

## 6. Takes (ADR 0031)

Takes live on **take lanes** under their track, as in Live 12, Cubase and Reaper. The track's main lane is what plays; take lanes are an archive of every pass that you can pick from at any time.

```
┌ header ─────────────────────┬ lanes ─────────────────────────────────────────────┐
│▌ Vocals              ● M S ▐│ ███ Vocals 3 ██████ ███ Vocals 2 ██████            │
│   Takes · 3           ★     │ ░░░ take 3 ░░░░░░░░ ░░░ take 3 ░░░░░░░░            │
│   Take 2                    │ ░░░ take 2 ░░░░░░░░ ░░░ take 2 ░░░░░░░░            │
│   Take 1                    │ ░░░ take 1 ░░░░░░░░ ░░░ take 1 ░░░░░░░░            │
└─────────────────────────────┴────────────────────────────────────────────────────┘
```

- **Every pass makes a take lane on each recording track,** holding what it recorded; a loop pass makes one lane per lap. The main lane plays the newest.
- **Nothing is replaced unseen:** when a pass records over clips that aren't on a take lane yet (an earlier edit, a dropped file), they go to a take lane named "Before take N" first.
- **Showing takes:** a track with takes shows "Takes · N" in its header; a click opens its take lanes under it, as thin rows drawn in the track's colour, dimmed (below the track, above any automation lane after it). They open by themselves after a pass that made several takes.
- **Choosing a take:** click a take on a take lane: it replaces the main lane across its time range. **Alt+↑ / Alt+↓** steps through the takes for the selected range, while it plays, to audition them in turn.
- **Rating:** a star on each take lane marks favourites (the header and the inspector show them).
- **Take lane menu:** Rename, Star, Delete take, **Copy to new track**, **Move to new track** (the new track copies this track's chain and mix: a double or a layer in one action). Selecting several take lanes copies or moves each to its own track.
- **The track's menu:** Show takes, **Spread takes to new tracks** (every take on its own track, for stacking), Delete unused takes (those the main lane no longer uses; files stay).
- **MIDI takes** work the same. In Overdub mode laps merge into one clip and make no takes.
- **A lap cut short** (the pass stopped mid-lap) is kept too; its clip ends where its recording does, so choosing among takes for a full range skips it.
- **Takes and editing:** moving, splitting or deleting clips in the main lane leaves take lanes as they are; take lanes keep their time position.

## 7. Comping

Choose the best parts of several takes into one performance, as Logic's swipe comping and Live 12's take lanes do.

- **Drag across a take lane** to use that range of the take: the main lane takes it, cutting what was there (ADR 0019), with short crossfades at the joins (C6's clip fades).
- **Take lanes show what's in use:** the parts of each take that the main lane plays are drawn in the track colour, the rest dimmed.
- **Every swipe is one undo step.** Double-click a part to use the whole take there.
- **The main lane stays plain clips:** comping makes clips that point into the take files, so a comp is edited, faded, bounced or exported like any clip, and needs no rendering.
- **Flatten** is just "Delete unused takes" (§6).

## 8. Capture MIDI

- **Segno always listens:** MIDI played into the armed (or softly armed) track while not recording is kept for the last 60 seconds.
- **Capture MIDI** (a button beside Record, lit when there's something to capture; Ctrl+Shift+R) turns it into a clip on that track:
  - while playing: where it was played, against the song;
  - while stopped: at the playhead, starting on the beat of the first note.
- **One undo step.** It works on every track you played since the last capture.
- Later (§15): when the project is empty and stopped, set the tempo from what was played.

## 9. Transport bar and menus

- **Record button:** filled in `record` while recording, ringed while a count-in or punch waits; its right-click menu (also its small arrow and the command palette) holds Count-in ▸, Punch in/out, MIDI loop recording ▸ (New takes, Overdub) and Recording settings… (Audio settings).
- **Capture MIDI button:** beside Record, dim until there's something to capture.
- **Position display:** counts down during a count-in ("−2.3").

## 10. Selection and the context panels

- **A track's inspector** gains INPUT (audio input, MIDI input and channel, Monitor, Records) between TRACK and MIX.
- **A take** (clicking a take lane's header, or a take on it) is selectable like a lane: the inspector shows TAKE (name, star, length, recorded at, "Use this take") and the detail panel the track's modules.
- **Delete** after clicking a take lane's header deletes the take (one undo step; the file stays).

## 11. Keyboard and actions

| Action | ID | Shortcut |
|---|---|---|
| Record | `transport.record` | Ctrl+R, F9 |
| Capture MIDI | `transport.capture` | Ctrl+Shift+R |
| Arm selected tracks | `track.arm` | Alt+R |
| Count-in on/off (the last length) | `transport.countIn` | Ctrl+Alt+C |
| Punch in/out | `transport.punch` | Ctrl+Alt+U |
| Show takes of the selected track | `view.takes` | Ctrl+Alt+K |
| Previous / next take for the selected range | `take.previous` / `take.next` | Alt+↑ / Alt+↓ |
| Spread takes to new tracks | `take.spread` | Ctrl+Alt+Shift+K |

## 12. Assistants (MCP)

Every feature comes with its tools and a test through MCP (ADR 0021). Assistants can't play, but they can set up and run a recording for you, and help choose takes.

| Tool | Arguments | Result |
|---|---|---|
| `set_track_input` | `track`, `audioInput?`, `midiInput?`, `channel?`, `monitor?`, `records?` | The track's inputs; the inputs available (`get_project` lists them too) |
| `arm_tracks` | `tracks`, `on?` | Which tracks are armed. Not an undo step |
| `record` | `countIn?`, `punch?`, `bars?` (stop after this many bars) | Starts recording; `stop` ends it. The result lists each recording track |
| `capture_midi` | `track?` | The clip made |
| `get_takes` | `track` | Its take lanes: name, star, time range, file, and which parts the main lane uses |
| `use_take` | `track`, `take`, `from?`, `to?` | Comps that take (or its range) into the main lane. One undo step |
| `move_takes_to_tracks` | `track`, `takes`, `copy?` | The new tracks |
| `delete_takes` | `track`, `takes` or `unused` | Files stay |

**Comparing takes:** `render` and `measure_mix` already render and measure a time range; with `get_takes`, an assistant can render each take, compare level, timing against the grid or pitch, and suggest one ("take 3 is the most even"). The user decides; `use_take` applies it.

## 13. Model and view

- **`model::Takes`** (JUCE-free, `[fast]` tests): which take a range of the main lane comes from, the clip edits a swipe makes (cuts and joins, as ADR 0019's `clearRange`), "Before take N" for what a pass would replace, take numbering, and stepping through takes.
- **The snapshot:** `TrackInfo` gains its inputs, monitor mode, armed state (by button, or softly by selection) and `takeLanes` (id, name, star, clips, and the ranges the main lane uses from each). Recording in progress is a `RecordingInfo` per track (start, the growing end), read every frame.
- **Rows** (`model::ArrangementRows`): take rows follow their track while its takes are shown, before the automation lanes after it. Keys "t<track>/<take>".
- **Drawing:** take lanes reuse the clip painting (waveforms through the waveform cache, notes) in the track's colour, dimmed; the recording clip draws from the engine's live thumbnail and notes.

## 14. Engine (tracktion)

- **Inputs:** each track's inputs are tracktion input-device instances targeting its track (`InputDeviceInstance::setTarget`), armed with `setRecordingEnabled`. Audio inputs are tracktion's wave inputs per channel or pair; MIDI uses the virtual "All MIDI Ins", single devices or the computer keyboard, as live MIDI does today.
- **Recording:** `TransportControl::record` and `stopRecording`; `TransportControl::Listener::recordingFinished` hands over the new clips. Segno then turns tracktion's per-clip loop takes into take lanes, keeps what the pass replaced, and updates the main lane, all inside the pass's undo step.
- **Take lanes are Segno's own state** in the project (an `FDTAKES` element per track with its lanes and their clips, not played), written through the undo manager, like automation lanes (ADR 0029). The main lane holds ordinary tracktion clips. tracktion's own take switching and comp manager need a tracktion Project for audio and would render comps; Segno's take lanes need neither, and a comp is plain clips.
- **Files:** `EngineBehaviour::getFileForNewAudioRecording` names each file in the project's `Recordings` folder; tracktion splits loop recordings into one file per lap. Format and bit depth through `WaveInputDevice`. Segno writes relative source paths itself (relative to the project's folder): tracktion resolves them from the folder but writes them from the project file.
- **MIDI modes:** New takes is tracktion's "neither merge nor replace" (a clip per lap); Overdub is its merge. Replacing is always Segno's (through take lanes), never tracktion's.
- **Monitoring and MIDI channels per track:** tracktion sets monitoring per input device, and its MIDI channel setting rewrites channels. A small engine patch (`cmake/patches/tracktion_engine-per-track-inputs.patch`) gives each track's input destination its own monitoring and MIDI channel (heard and recorded); Segno routes every track's inputs from its settings (`Session::Impl::applyInputs`).
- **Count-in** is `Edit::setCountInMode` (a global setting in tracktion, matching a setting kept between projects); **punch** is `Edit::recordingPunchInOut` with the loop range.
- **Latency:** tracktion moves audio by the driver's reported input and output latency and the graph's latency, plus the manual offset (`setRecordAdjustmentMs`); MIDI by its manual offset (`setManualAdjustmentMs`) minus the graph's latency.
- **Capture MIDI:** tracktion's retrospective MIDI buffer (`RetrospectiveMidiBuffer`, `TransportControl::applyRetrospectiveRecord`), set to 60 s.
- **Live display:** `RecordingThumbnailManager` for waveforms, `getRecordingNotes` for notes.
- **Engine issues avoided:** tracktion drops audio takes it can't resolve when switching takes without a Project, and lists a loop's first lap twice; Segno reads the takes once, deduplicated, and never switches tracktion takes.

## 15. What comes when

| Part | Scope | When |
|---|---|---|
| **C5a Recording** | Project folders; inputs; arm buttons and soft arm; monitoring; Record, count-in, punch; several tracks at once; latency placement and offsets; input metering; live drawing; MCP | M2 |
| **C5b Takes** | Take lanes for every pass and lap; what a pass replaces kept; choosing and auditioning takes; stars; copy, move and spread takes to new tracks; delete unused takes; MIDI overdub; Capture MIDI; MCP | M2 |
| **C5c Comping** | Swipe comping on take lanes with crossfades (after C6's fades); MCP `use_take` with ranges; selecting several take lanes to copy or move them together (what's in use is shown since C5b) | M2, after C6 |
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
| Q7 | **Punch range** | The loop brace (no second range) |
| Q8 | **Loop takes** | Every lap kept on its own take lane; the main lane plays the newest |
| Q9 | **MIDI loop recording** | New takes by default; Overdub as an option |
| Q10 | **Takes model** | Take lanes per track, Segno's own state; the main lane is plain clips (ADR 0031) |
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
- **Takes:** a loop pass of three laps makes three take lanes, the newest playing; what a pass records over is kept as a take; choosing, auditioning, starring, deleting, copying, moving and spreading takes (new tracks copy the chain); MIDI overdub merges laps.
- **Comping:** a swipe makes main-lane clips from the take's file at the right offsets with joins faded; each swipe is one undo step; the in-use ranges are right (`[fast]` for the model).
- **Undo:** a pass is one step (clips, take lanes, automation); undo never deletes files; redo restores the clips.
- **Capture MIDI** makes a clip of the last notes, playing and stopped.
- **Projects:** a saved project folder moved elsewhere opens with its recordings; recordings made before the first save move into the new folder.
- **MCP:** each tool in §12.
- **Performance:** recording 16 tracks at once at 48 kHz keeps the frame budget and never drops audio (Release).
- **Manual:** `manual-tests/M2.md`, "Recording (C5)".

## 19. Review of C5b (product owner, 2026-10-10): proposed

Trying C5b, the product owner found:
- **Takes in the wrong place:** a take is another version of a clip, so takes belong on the clip, not under the track.
- **Clutter:** "Takes · N" showed for a single take; it also sat too close to the header's buttons and didn't say it opens something.
- **Capture MIDI:** with a track without an instrument selected, it did nothing; and it wasn't clear what it captures.
- **Two unexplained buttons** after the metronome: Automation arm and Turn automation back on (C4).

What changes, once approved. It supersedes §6, §8 and §10–§14 where they differ; they're rewritten when this is built.

### 19.1 Takes belong to clips (ADR 0032)

```
┌ header ───────────┬ lanes ──────────────────────────────────────────────────┐
│▌ Vocals   M S ●   │ ▐ Vocals ─────────── ★ 3/4 ▾ ▌   ▐ Vocals ─────────────▌ │
│                   │ ▐ ∿∿∿∿∿∿∿∿∿∿∿∿∿∿∿∿∿∿∿∿∿∿∿∿∿∿ ▌   ▐ ∿∿∿∿∿∿∿∿∿∿∿∿∿∿∿∿∿∿∿ ▌ │
│                   │ ░ Take 4 ░░░░░░░░░░░░░░░░░░░░░    one take: a plain clip  │
│                   │ █ Take 3 ★ ██████████████████                            │
│                   │ ░ Take 2 ░░░░░░░░░░░░░░░░░░░░░                            │
│                   │ ░ Take 1 ░░░░░░░░░░░░░░░░░░░░░                            │
└───────────────────┴──────────────────────────────────────────────────────────┘
```

- **A pass makes a clip with its takes:** what it recorded, one take per loop lap. A clip with a single take looks and acts like any clip: nothing about takes shows.
- **Recording over a clip adds to its takes;** what a pass records over that has no takes yet is kept as a take of the new clip first ("Before take N"), so nothing is lost.
- **The badge:** from two takes, the clip's title strip ends with the take it plays and how many, and a chevron: "3/4 ▾" (★ first when that take is starred; "Comp ▾" when its parts play different takes). On a narrow clip it shrinks to "▾" before the name is cut. A click opens the take menu:
  - the takes, newest first, the one playing ticked, starred ones with ★: choosing one plays it;
  - Show takes / Hide takes;
  - Star, Rename take…;
  - Copy take to new track, Move take to new track, Spread takes to new tracks;
  - Delete take, Keep only this take (the others are deleted; files stay).
- **Takes open under their clip** (the menu, a double-click on the badge, or Ctrl+Alt+K on a selected clip): one thin strip per take (24 px, a new shell metric) across the clip's time range. The track's row grows by the tallest open stack and shrinks back when they close; the header column shows nothing more. Each strip shows its take's name and star (a click on the star toggles it), the parts the clip plays in the track's colour and the rest dimmed (`clipDimmed`).
  - A click on a strip plays that take and selects it (TAKE in the inspector, as built); right-click opens its menu; Delete deletes it.
  - They open by themselves after a pass that made several takes, for the clips it made.
- **Alt+↑ / Alt+↓** on a selected clip play its previous or next take, as built.
- **A clip and its takes are edited as one,** in the same undo step:

| Command on the clip | Its takes |
|---|---|
| Move (in time or to another track) | Move with it |
| Trim its start or end | Trimmed to the same range (files untouched: trimming back brings them back) |
| Split | Split at the same point; the right halves become the right clip's takes |
| Duplicate | Copied |
| Delete | Deleted with it (undo brings them back; files stay) |
| Another clip placed over part of it (ADR 0019) | The clip is cut; its takes stay whole, so choosing a take there fills it again |
| Copy, move or spread takes to new tracks | The new tracks get plain clips |

- **A pass over part of a clip with takes** joins its takes (they widen to cover both); the clip plays the new take where it recorded and its earlier take elsewhere. Those parts draw as one clip (one title strip, a thin line where the take changes) and select, move, trim at the outer edges, split and delete together. Moving the line between parts is comping (C5c).
- **What goes:** take rows, "Takes · N" in the track header, and the takes items in the track's menu (they're in the clip's take menu; `view.takes` and `take.spread` act on the selected clip).
- **Model and engine:** a track's `FDTAKES` holds a `STACK` per clip with takes, with its `TAKE`s and their clips; a main-lane clip names the take clip it plays (`fdTake`), and through it its stack. Whether a stack is open is view state on it. The snapshot gives each track its stacks (takes, range, open) and each clip its stack. Every clip command carries the stacks of the clips it edits (engine tests for each row of the table).
- **MCP:** the take tools stay; `get_takes` lists takes per clip (`track` or `clip`).

### 19.2 Capture MIDI

**As built (C5b):** tracktion keeps the last 60 seconds of each MIDI input and, on Capture, turns all of it into a clip on the tracks the input is routed to: the armed track, or the selected track only if it has an instrument (live MIDI follows only those). So:
- with a track without an instrument selected, nothing is routed and nothing happens;
- a note, 30 s of silence and then a phrase come back as one clip with the 30 s gap in it.

**Proposed:**
- **Segno listens to every MIDI input and the computer keyboard** all the time, whether or not anything is armed or has an instrument: the last 2 minutes of notes, pedal, bends and controllers (a tap on each MIDI input in the engine patch, read through a lock-free queue).
- **It captures the last phrase:** what was played since the last pause of 2 bars or more (at the project tempo) with no note or pedal held. While the transport plays, nothing from before it started. In the example above: just the phrase.
- **Never twice:** a capture, or a recording pass, uses up what was played before it.
- **Where it goes:** each armed MIDI track gets what came in on its input; with none armed, the selected track, whatever it holds. A track without an instrument gets the default one in the same undo step, as a drawn MIDI clip does (ADR 0019), and says so. With nothing (or a return or the master) selected, a new track.
- **The button** lights while there's a phrase to capture; its tooltip names where it goes ("Capture MIDI to Bass (Ctrl+Shift+R)").
- **Placement** as built: while playing, where it was played; stopped, at the playhead on the beat of the first note; trimmed to whole beats. One undo step.

### 19.3 The automation buttons in the transport bar

They're C4's (automation.md §7–§8): **Automation arm** (Ctrl+Shift+A: while armed and playing, moving a control records into its lane) and **Turn automation back on** (Ctrl+Alt+A: moving an automated control by hand turns its automation off; this turns every one back on).
- **Move them next to Record and Capture:** arm is a way of recording, so it reads better there.
- **Turn automation back on shows only while some automation is off,** lit in the automation colour; its place stays reserved so nothing in the bar moves when it appears.

### 19.4 Decisions

| # | Question | Recommendation |
|---|---|---|
| R1 | Where takes live | On their clip (ADR 0032) |
| R2 | A clip with one take | Shows nothing about takes |
| R3 | Seeing every take | Under the clip; the track's row grows while they're open |
| R4 | A pass over part of a clip with takes | Joins its takes; the clip plays both parts, as one clip |
| R5 | What Capture MIDI takes | The last phrase (after a pause of 2 bars), and only since playback started while playing |
| R6 | Where it goes | Armed tracks, else the selected track (getting the default instrument if it has none), else a new track |
| R7 | The automation buttons | Next to Record; "Turn automation back on" only while needed |

**Building it** (C5b revised, before C7): takes on clips (model, engine clip commands, drawing, menus, MCP, tests), then Capture MIDI, then the transport bar.
