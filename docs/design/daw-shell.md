# DAW shell: design (M1 slice C1–C8)

How the M1 DAW is put together. The decisions behind it are in [ADR 0018](../decisions/0018-daw-app-structure.md) (structure), [ADR 0015](../decisions/0015-rendering-opengl-for-the-daw.md) (rendering) and [ADR 0004](../decisions/0004-tech-stack.md) (tracktion_engine). The look and behaviour come from [ui-principles.md](ui-principles.md).

## Code layout

```
app/src/
  Main.cpp      application, main window, OpenGL attach (software fallback: --software)
  engine/       everything that includes tracktion_engine
    DevicePlugin    te::Plugin adapter for fd::Device
    Session         one open project: owns the te::Edit, commands, snapshots, transport
    AppBehaviour    te::UIBehaviour: progress and warnings shown in the shell, never modal
  model/        JUCE-free, unit-tested ([fast])
    TempoMap        seconds <-> beats <-> bars
    Timeline        time <-> pixels, zoom and scroll (animated), adaptive grid, snapping
    TrackLayout     track heights, vertical scroll, hit testing
    Snapshot        tracks, clips, tempo: what views draw
  ui/           JUCE components; no tracktion includes (checked by a test)
    Shell           the window content: panels, splitters, adaptive layout, key routing
    ActionRegistry  every action: id, name, shortcuts, callbacks
    TransportBar, BrowserPanel, InspectorPanel, DetailPanel, ArrangementView
```

## Threads and data

- **Message thread:** owns the Session and the UI state; runs every command.
- **Engine threads:** tracktion's audio and render threads. Our only shared state with them is through tracktion itself and `fd::Device` (ADR 0017).
- **GL thread:** renders frames continuously (ADR 0015). The message thread only invalidates regions, from a `VBlankAttachment`.
- **Snapshots:**
  - The Session listens to the Edit's state tree and, after any change, rebuilds a `Snapshot` asynchronously (coalesced to once per message loop).
  - Views get a `shared_ptr<const Snapshot>`, store it, and repaint.
  - Paint code reads only the snapshot and view state.
- **Per frame (vblank, message thread):** the shell reads the transport position and play state from the Session, then invalidates the playhead strip and anything animating (zoom).

## Layout (C1)

```
┌ TransportBar 40 ──────────────────────────────────────────────────────────────┐
├ Browser 220 ┬ Arrangement ───────────────────────────────────────┬ Inspector 260 ┤
│ (rail 44)   │ loop strip 12 + ruler 24                           │ (collapsible) │
│             │ track header 180 │ lanes                           │               │
├─────────────┴────────────────────────────────────────────────────┴───────────────┤
│ Detail panel: module chain (panel height + header 24 + 24)  ⇄  clip view (Tab)    │
└──────────────────────────────────────────────────────────────────────────────────┘
```

- Splitters resize the browser and inspector. In the module view, the detail panel's height follows the module panel height (232 px panels: 280 px).
- **Adaptive:**
  - below 1440 logical px wide, the browser collapses to its rail and the inspector closes (the overlay inspector comes in M3);
  - the user's explicit toggles win until the window crosses the threshold again.
- **Focus mode** shows only the transport bar and the centre view.
- **The centre view** is the arrangement or the mixer view (M2 C3c, [mixer.md](mixer.md) §6), switched by the Arrange | Mix switch in the transport bar or Ctrl+Alt+X.
- Panel sizes are per window in M1; per project from C8.
- Panels open and close with a 150 ms ease-out (instant with "reduce motion").

## Actions and shortcuts (C1)

`ActionRegistry` holds every action; the shell routes key presses to it unless a text field has focus. "Ctrl" means Cmd on macOS. Shortcuts are defaults; remapping comes with preferences.

| Action | ID | Default |
|---|---|---|
| Play / stop | `transport.playStop` | Space |
| Return to start | `transport.toStart` | Home |
| Loop on/off | `transport.loop` | Ctrl+L |
| Metronome | `transport.metronome` | Ctrl+M |
| Undo / redo | `edit.undo` / `edit.redo` | Ctrl+Z / Ctrl+Shift+Z, Ctrl+Y |
| Delete | `edit.delete` | Delete, Backspace |
| Duplicate | `edit.duplicate` | Ctrl+D |
| Split at playhead or selection | `edit.split` | Ctrl+E |
| Select all | `edit.selectAll` | Ctrl+A |
| Add track | `track.add` | Ctrl+T |
| Computer keyboard as MIDI | `midi.computerKeyboard` | Ctrl+Shift+K |
| Scan for plugins | `app.scanPlugins` | Ctrl+Alt+P |
| Fold module (the selected one) | `module.fold` | Ctrl+Alt+M |
| Expand module sections | `module.expand` | Ctrl+Alt+E |
| Show browser / inspector / detail | `view.browser` / `view.inspector` / `view.detail` | Ctrl+Alt+B / Ctrl+Alt+I / Ctrl+Alt+D |
| Device chain ⇄ clip view (arrangement only) | `view.detailMode` | Tab |
| Focus mode | `view.focus` | Ctrl+Alt+F |
| Zoom in / out / to fit | `view.zoomIn` / `view.zoomOut` / `view.zoomFit` | = / − / Ctrl+0 |
| Follow playhead | `view.follow` | Ctrl+Shift+F |
| Snap to grid | `view.snap` | Ctrl+4 |
| New / open / save / save as | `file.new` / `file.open` / `file.save` / `file.saveAs` | Ctrl+N / Ctrl+O / Ctrl+S / Ctrl+Shift+S |
| Audio settings | `app.audioSettings` | Ctrl+, |
| Mute / solo selected tracks (C3) | `track.mute` / `track.solo` | Alt+M / Alt+S |
| Add automation lane... (C4) | `automation.addLane` | Ctrl+Shift+L |
| Show last touched parameter's lane (C4) | `automation.showLastTouched` | Alt+L |
| Show / fold automation lanes (C4) | `view.lanes` | Ctrl+Alt+L |
| Automation arm (C4) | `automation.arm` | Ctrl+Shift+A |
| Turn automation back on (C4) | `automation.reEnable` | Ctrl+Alt+A |
| Add return track (C3) | `track.addReturn` | Ctrl+Alt+T |
| Show or hide returns and master (C3) | `view.returns` | Ctrl+Alt+R |
| Arrangement ⇄ mixer view (C3) | `view.mixer` | Ctrl+Alt+X |

Each step adds its actions as it lands.

**Command palette** (Ctrl+K, M2 step C1): a popover at the top of the window listing every action by fuzzy search (`model::rankPalette`, tested). It matches names, and categories at a slightly lower score; word starts and consecutive letters rank higher; equal matches go to the most recently used. Each row shows the shortcut, a dot for toggles that are on, and a dimmed name for actions that can't run now. Arrows move, Enter runs, Escape or a click elsewhere closes. Recent use is kept for the session; saving it with the user's settings comes with preferences.

## Timeline (C2)

`model::Timeline` maps seconds to pixels for the arrangement and ruler.
- **State:** visible start (seconds), pixels per second, width. Zoom is clamped to 0.5–20 000 px/s; the start is never negative.
- **Zoom anchored at a point:** the time under the mouse (or the playhead, for keyboard zoom) stays put. Wheel and pinch zoom continuously and immediately. Button and keyboard zoom animate towards a target with an exponential ease (~80 ms), advanced once per frame.
- **Scroll:** horizontal scroll by pixels; it can follow the playhead later (C5).
- **Adaptive grid:** from the ladder 1/32 beat … 1/2 beat, 1 beat, 1, 2, 4, 8, 16, 32, 64 bars, the finest step whose lines are at least 10 px apart. Lines are tagged bar, beat or subdivision. Bar labels are thinned so they stay at least 56 px apart.
- **Snapping:** to the nearest line of the current grid (what you see is what you snap to). Holding Alt suspends it; `view.snap` turns it off.
- **Tempo:** `TempoMap` handles one tempo and time signature in M1. Tempo changes later only replace the TempoMap.

`model::TrackLayout` handles the vertical axis: per-track heights (default 64 px, minimum 24), vertical scroll, the y of each track, and which track (and where in it) a y falls on.

## Input (C2)

| Input | Effect |
|---|---|
| Wheel / two-finger scroll | Vertical scroll; horizontal deltas scroll in time |
| Shift + wheel | Horizontal scroll |
| Ctrl + wheel, pinch | Zoom at the mouse |
| Drag in the ruler | Vertical drag zooms around the mouse, horizontal drag scrolls (one gesture) |
| Click in the ruler | Moves the playhead (C5) |

## Arrangement drawing

Layers, back to front:
1. lane background (one tone step above the canvas; the selected track a step lighter);
2. grid lines (bar, beat, subdivision at falling contrast, all quieter than the track separators);
3. track separators: one pixel of `line`, lighter than the lanes, continued through the headers;
4. clips: flat clip colour, name, and the waveform in a lighter tint (C3);
5. selection;
6. loop brace (C5);
7. playhead.

Waveforms come from a background-built peak cache per file, plus cached drawing per clip and zoom level (C3), within the 8 ms budget (ADR 0015).

## Clip editing (C4)

- **Hit testing and drag maths** live in `model/Editing` (tested):
  - the grabbed clip snaps and the rest of the selection follows by the same amount;
  - nothing moves before time zero or off the track list;
  - trims stay within the source file and leave at least 10 ms.
- **Selection:** click; Shift or Ctrl toggles; a drag on empty lanes box-selects (Shift adds); the selected track follows the click. One selection serves both views and the context panels; the full rules are in [selection.md](selection.md).
- **During a drag**, clips are drawn at their new place from the model's result. On release the Session applies it as one undo step, and the preview stays until the new snapshot arrives (no flicker).
- **Commands:** delete, duplicate (copies placed right after the selection), split at the playhead (the selected clips, or the selected track's clips under the playhead), select all.

## Projects (C8)

- **`.segno`** (project files; `.fdproj` before ADR 0024) is tracktion's edit XML (open-questions C: "tracktion_engine's edit format plus our own sections"). The extra parts:
  - our devices' parameters and non-parameter state (`fdExtra`, ADR 0017);
  - the UI's view state as JSON on the root (`fdView`): zoom and scroll, snap, follow, panels open and their widths;
  - each module panel's view state on its plugin (`fdPanel`): collapsed sections, folded.
- **Commands:** Ctrl+N new, Ctrl+O open, Ctrl+S save (asks for a file the first time), Ctrl+Shift+S save as. A project file on the command line opens directly.
- **Unsaved changes** (Save / Don't save / Cancel) are asked before New, Open and Quit: the one modal the UI principles allow for this. The window title shows the project's name, with `*` while unsaved.
  - The Session tracks changes itself, because tracktion's own flag updates on a timer.
  - Device parameter changes count as changes.
- **Replacing the project:** the Session tells the UI first (`onProjectWillChange`), so every panel and device handle is released before the old Edit is destroyed; then `onProjectChanged` brings the new snapshot and view.
- **Media stays where it is.** Clips reference their files; collect-and-save comes with the browser (M3).
- **Tests:** a full round trip compares tracks, clips, devices (order, power, parameters, the Sampler's sample), tempo, time signature, loop and view state. A shell test restores zoom and panels.

## One kind of track ([ADR 0019](../decisions/0019-one-kind-of-track.md))

- Every track holds audio and MIDI clips. Its header shows its instrument, "No instrument" when it has MIDI and nothing to play it, or nothing.
- **No overlaps on a track, of any kind:** the clip you place (move, trim, duplicate, drop) cuts what it covers, in the same undo step (`model::clearRange`, tested). Crossfades between neighbouring audio clips come with clip fades (M2).
- **Signal flow:** audio clips enter at the start of the chain and MIDI clips feed the first instrument. The instrument adds its sound and never mutes the audio:
  - our devices render into a scratch buffer that is mixed in;
  - third-party instruments get the incoming audio added back by a tracktion patch (`cmake/patches/tracktion_engine-instrument-audio-passthrough.patch`).

  Render tests check that audio passes through a silent instrument, ours and a hosted VST3, to below −120 dBFS.
- **MIDI needs an instrument:** a MIDI file dropped on a track without one adds the Subtractive Synth, in the same undo step, and says so.

## Instrument tracks and live MIDI (C7)

- **Instruments are devices:** any track becomes playable by adding an instrument to its chain (+ in the detail panel). There is one "Add track" (Ctrl+T); nothing is preloaded.
- **Live MIDI follows the selection.** Selecting an instrument track routes live MIDI to it, with monitoring always on; selecting anything else routes it nowhere. This is not an edit, so it adds no undo step.
  - Only tracktion's virtual inputs are routed: "All MIDI Ins" (every physical device, merged) and our "Computer keyboard". Routing physical devices too would double every note.
  - The device list arrives asynchronously and changes when devices are plugged in; the Session re-applies the routing each time.
- **Computer keyboard** (Ctrl+Shift+K, or the keyboard button at the right of the transport bar, with a MIDI activity light beside it):
  - turning it on selects the first track with an instrument if none is selected, or says how to add one;
  - A W S E D F T G Y H U J K O L P ; play from C (C3 at the start);
  - Z / X shift octaves, C / V change velocity;
  - keys held with Ctrl or Alt stay shortcuts.
- **MIDI files:** `.mid` dropped on a track becomes a clip with all of the file's tracks merged. Dropped below the tracks, it creates an instrument track. The clip shows its notes across its own pitch range; editing them is the piano roll (M2).

## Device chain (C6)

The UI calls instruments and effects **modules** ([ADR 0022](../decisions/0022-module-panels.md)): the "Modules" tab, "Add module", "Module on/off".

- **The chain:** the detail panel shows the selected track's modules left to right, scrolling horizontally.
  - Our devices show their panels, bound through `Session::openDevice`, which gives a `DeviceHandle`. That keeps tracktion out of the UI, and edits go through the engine (automation, undo).
  - Third-party plugins show as compact cards: power, name and "Open editor". The editor opens in a floating window, the only kind the app has.
  - Panels of devices that stay in the chain are kept across changes (their analysers keep running).
- **Editing:** each edit is one undo step.
  - Add with the + button: our instruments and effects, then a Plugins submenu grouped by manufacturer, with "Scan for plugins" at its end.
  - Reorder by dragging a module's header background or its folded strip (a marker shows where it lands). Controls in the header (fold, power, settings) never start a drag.
  - Bypass with the power toggle.
  - **Fold** a module to a 28 px strip with the chevron in its header; collapse a section by clicking its title ([device-panels.md](device-panels.md)). Both are stored per module in the project (`fdPanel`), never as undo steps or unsaved changes.
  - Click a header to select a device; Delete removes it when the chain was the last area clicked (otherwise Delete applies to clips).
  - **Sidechain** ([ADR 0025](../decisions/0025-sidechain-routing.md)): a module with a sidechain input (Compressor, Gate) has a "Sidechain" picker in its header: None, or another track. The list leaves out any track whose sound already depends on this one through sidechains, so routing can't loop. The source is heard after its modules and fader, before its mute. A change is one undo step ("Set sidechain") and rebuilds the playback graph. Rendering one track (the MCP `render` tool) includes the tracks feeding its sidechains, muted.
- **tracktion's own volume and meter plugins** belong to the mixer and are not shown, nor are the sends and return inputs. New devices go before them.

## Mixer view (C3c, [mixer.md](mixer.md) §6)

- The centre shows the arrangement or the mixer view: the transport bar's "Arrange | Mix" switch or Ctrl+Alt+X. The browser and the detail panel stay; each view keeps its own scroll.
- **Context panels** ([selection.md](selection.md), ADR 0027): the detail panel shows only the selected channel's modules (a plain "Modules" title, no clip view, no Tab); the inspector isn't shown, since the strips carry every channel control. Clip edits, zoom and Tab are off. Back in the arrangement, both panels return to what they showed.
- Strips (`ChannelStrip`) for the tracks, then the return tracks after a divider, scroll horizontally; the master is pinned at the right. A click selects the channel (shared with the arrangement, and clearing any selected clips); a module name selects the channel and that module in the detail panel, opening it if closed; Left and Right move the selection; clicking the peak readout resets it.
- The header's Modules, Sends and Narrow view options (`ui::ViewOption`), the current view and the mixer's scroll are view state, saved with the project.
- Strips, header controls and the inspector hold the engine's meters: they are dropped before a project is replaced, and the inspector drops its own while the mixer view is shown.

## Automation lanes (C4b, [automation.md](automation.md))

- **Rows:** `model::ArrangementRows` lists the arrangement's rows: each track with its unfolded lanes, then the returns and the master with theirs. Every row is drawn from the track header kit; clip editing maps rows to tracks (a drag across a lane row moves a clip to the lane's track).
- **Lanes** draw the engine's curve (`model::Automation`) over the reserved gradient, with points as dots (`ui/AutomationLane`). Gestures are §5 of the design: double-click adds or deletes, drag moves (snapped; Alt free; Shift fine), a segment drags up or down (Alt bends it), Ctrl+drag draws, an empty drag box-selects. A gesture previews and applies one `Session::setAutomation`, one undo step.
- **Headers:** the lane's on switch and its parameter's bar (`ui/LaneControls`, edits through `Session::setLaneValue`), the value now (refreshed each frame while it plays); the fold chevron on a channel; dragging a row's bottom edge resizes it.
- **Showing lanes:** the track header's right-click menu or Ctrl+Shift+L opens the lane picker (`ui/LanePicker`, a searchable list like the palette); Alt+L shows the last touched parameter's lane; Ctrl+Alt+L folds or shows every lane. Delete after clicking a lane's header removes it.
- **Keys** follow the lane when it was used last: Delete, Ctrl+D, Ctrl+A and the arrows (↑/↓ 1 %, Shift 0.1 %; ←/→ a grid step) act on its points.

## Automated controls and recording (C4c, [automation.md](automation.md) §6–§8)

- **The indicator** (`ui::AutomationState`, `ui::paintAutomationIndicator`): knobs, bars, faders, toggles, segments and choices show it, from the host binding (`ParamBinding::automationState`) for module panels and from the snapshot's lanes for the mixer's controls.
- **Right-click menus** on every control (`Knob::onPopupMenu`, `Toggle::onPopupMenu`): Show automation lane (the lane, selected, in the arrangement, leaving the mixer view), Clear automation, Turn automation back on, Reset to default. Plugin hosts get only Reset.
- **Following:** automated mixer controls read their lane's played value each frame (`LaneFollower`); module panels refresh from the engine as before.
- **Gestures** on mixer controls are lane gestures (`Session::LaneGesture`): one undo step, and a touch while recording. A knob's double-click reset joins the gesture its second press began, so gestures never nest.
- **Transport:** Automation arm and Turn automation back on (lit while a lane is off), between the loop and the tempo.

## Mixer in the arrangement (C3b, [mixer.md](mixer.md))

- **Rows:** the tracks, then (after a 2 px divider) the return tracks and the master, on sub-lane backgrounds; they scroll with the tracks. Ctrl+Alt+R hides and shows them. Clicking one selects it (and clears any selected clips): the inspector shows its channel and the detail panel its chain. They hold no clips.
- **Header controls** fill the track header kit's slots: mute and solo (not on the master), volume and pan bars (values only; from 80 px tall), and the meter on the right edge. They are shared components (`ui/ChannelControls`) on a layer that clips to the header column, one set per channel, placed every frame from the layout.
- **Inspector:** titled sections with a line between them ([selection.md](selection.md) §5): the clip (if one is selected), the track, return or master, then MIX (volume and pan bars with the meter, mute and solo) and, for a track, SENDS (a bar and a Pre switch per return, or "Add return track").
- **Snapshots** ignore device parameter and state changes (only a device's power state and sidechain source show), so turning a knob never rebuilds the arrangement.
- **Scanning** (`Ctrl+Alt+P`): VST3, plus LV2 on Linux, in the default folders.
  - One file per tick on the message thread, as JUCE's own plugin list does, because some formats must load there.
  - A plugin that crashes the scan is skipped next time (JUCE's dead man's pedal).
  - Results persist through tracktion's settings.
  - Scanning in a separate process is M3.

## Transport (C5)

- **Transport bar:** the mark and name at the left (the segno sign and SEGNO, [ADR 0024](../decisions/0024-product-name.md)), then play/stop, stop-and-return, loop and metronome toggles, then tempo and time signature as `ui::NumberField`s (drag, Shift for fine, wheel, or type; each edit is one undo step), the position in bars and time, and CPU load.
- **Assistants** (C9, [mcp.md](mcp.md)): a spark button left of the settings button, with an activity light that is lit for 3 s after an assistant's request. It opens the Assistants popover (also the command palette's "Assistants"):
  - **On**, remembered between launches; the status line says where it listens and how many assistants have connected.
  - **Read only**: assistants can read, render and measure, but not edit.
  - The setup lines to copy: Claude Code's `claude mcp add` command (HTTP, with the token), and the configuration for apps that start the DAW themselves (`Segno --mcp-stdio`).
  - **Connected**: each client by name and version, and when it was last seen.
  - **New token**: every client must be set up again.
  - Escape or a click elsewhere closes it.
- **Every assistant edit is one undo step** named after its tool ("Assistant: add notes"), in the same history as the user's edits.
- **Loop brace:** lives in a 12 px strip above the bar numbers.
  - Drag on empty strip: draws a brace (snapped) and turns looping on.
  - Drag the brace: moves it; drag its edges: resizes it.
  - Click the brace: toggles looping.
  - Ctrl+L with no brace yet loops four bars from the playhead's bar.
- **Follow:** while playing, when the playhead passes 88 % of the view (or leaves it), the view pages so the playhead sits at 12 %, eased over ~150 ms. It never moves while you drag or zoom.
- Loop range, loop state and metronome are live transport values read each frame, like the playhead, so they don't rebuild snapshots.

## Waveforms (C3)

- **Peaks** (`model::Peaks`):
  - built per file on a background thread (`WaveformCache`) by streaming the file through `libs/audiofile`, never loading it whole;
  - min/max of all channels at 16, 64, 256 … frames per bin;
  - a pixel column reads a handful of bins at any zoom.
- **Tiles** (`WaveformTiles`, OpenGL only):
  - waveforms are rendered into pre-coloured images, 512 logical px of one file's time at device resolution, shared by every clip of that file;
  - while zooming, tiles come from power-of-two zoom levels and are drawn scaled down by at most 2×;
  - when the zoom settles (150 ms), they are re-rendered at the exact zoom, up to 12 per frame, and level tiles stand in until then;
  - least recently used tiles are evicted beyond 192 MB, and the GL texture cache is raised to 256 MB to match.
- **Software renderer:** waveforms are drawn directly as one polygon per clip, because scaling images costs more there than drawing.
- **What we measured:**
  - Under OpenGL, filling paths dominated the frame (about 4.5 ms of 6 ms with 32 visible tracks).
  - One rectangle per column was slower than one polygon per clip.
  - Drawing alpha masks "with the current brush" is rasterised on the CPU (24 ms per frame), hence pre-coloured tiles.
  - Paint code only draws what intersects the invalidated area, so a playhead repaint costs a few columns.

## Performance

The arrangement's budget (ADR 0015): 32 visible tracks zooming continuously, under 8 ms per frame, in Release on the reference machine. `Segno --benchmark [--display N] [--software]` fills a project with 32 tracks of 8 clips, shows them all (focus mode), zooms in and out for 6 s, and prints paint time and frame rate. `--stats` shows the same numbers live.

| 2026-10-10, Release | Paint avg | Paint p95 | Frames |
|---|---|---|---|
| OpenGL, laptop panel 2880×1920 @2× | 1.9 ms | 5.9 ms | 123 fps (120 Hz panel) |
| OpenGL, external 3840×2160 @2× | 1.9 ms | 6.3 ms | 123 fps (120 Hz) |
| Software, external 3840×2160 @2× | 13.8 ms | 21.5 ms | 78 fps |

A software-renderer benchmark also runs offscreen in the integration tests (`"[.benchmark]"`, report only: offscreen software painting at 4K is bound by fill rate).

## Piano roll (M2 step C2)

- **Creating clips:**
  - double-click an empty spot on a lane creates a one-bar MIDI clip from the grid line at or before it (Alt: exactly there);
  - it never covers another clip: it starts after a clip it lands in and ends where the next one starts;
  - Ctrl+Shift+M creates one at the playhead on the selected track;
  - a track without an instrument gets the default one, in the same undo step (ADR 0019);
  - double-clicking a MIDI clip opens it in the clip view.
- **Where it lives:** the detail panel follows the selection ([selection.md](selection.md) §4). In the arrangement, a single MIDI clip shows the clip view with the piano roll; anything else shows the modules. Tab switches until the selection changes. The mixer view never shows the clip view.
  - The clip view's height can be dragged at its top edge (default 320 px; saved with the view). The device view keeps the device panel height. The panel eases between the two.
  - While a clip is open, live MIDI goes to the clip's track, so previews and the computer keyboard play its instrument.
- **Layout:**
  - keyboard on the left (click or drag over the keys to hear them); C keys are labelled (C3 = 60);
  - the ruler on top, with the song's bar numbers and the clip's extent in its colour; click to move the playhead, drag to zoom and scroll as in the arrangement;
  - the note grid: rows for black keys a tone darker, a line under every C and between E and F;
  - outside the clip the grid is shaded; notes there are kept but don't play, and are drawn faded;
  - drawing or moving notes past the clip's end lengthens the clip to the end of the bar the last note ends in, in the same undo step. If another clip on the track is in that range, the clip stays as it is and those notes stay outside (B15). The rule lives in `Session::setNotes`, so it holds for every way of writing notes;
  - the velocity lane at the bottom.
- **Notes:** filled in the clip's colour, mixed toward the control tone at low velocity; the selection is lighter with an outline. Notes wide enough show their name.
- **Editing.** Pure functions on a plain list of notes (`model/NoteEditing`, tested); the view draws the result live. Every gesture ends in one `Session::setNotes`, which applies the difference as one undo step.

  | Input | Effect |
  |---|---|
  | Double-click empty grid | Draw a note one grid step long (drag right while still held to lengthen it) |
  | Click a note / Shift or Ctrl + click / drag on empty grid | Select / toggle / box select. A click on a note in a multiple selection keeps the group for a drag and selects just that note if released without one (the arrangement does the same with clips) |
  | Drag a note's body | Move: the grabbed note snaps, the others follow; vertical moves pitch (Alt: no snapping) |
  | Drag a note's left or right edge | Resize (snapped; at least one grid step) |
  | Double-click a note, Delete | Delete |
  | Ctrl+D | Duplicate the selection after itself: by its span rounded up to the grid, or to whole bars once it spans half a bar (a phrase stays on its beats) |
  | Ctrl+A | Select all notes |
  | ↑ / ↓ (Shift: octave) | Transpose the selection |
  | ← / → | Nudge by one grid step |
  | Ctrl+U | Quantise the selection (or all notes) to the grid |
  | Drag a selected note's stem in the velocity lane | Move the selection's velocities together |
  | Click or drag elsewhere in the velocity lane | Draw velocities on the notes the mouse passes (only the selected ones, if any are selected) |

  Delete, Ctrl+D and Ctrl+A act on notes when the clip view was used last, as Delete already does for devices.
- **Overlaps:** two notes of the same pitch never overlap (MIDI can't tell them apart). The notes an edit places win: another note that starts inside one is removed, one that starts before it is shortened. Overlaps that came in with a MIDI file stay until edited.
- **Hearing:** clicking or moving a note plays it through the track's instrument, via the same live-MIDI route as the computer keyboard (the clip's track is selected while editing).
- **Zoom and scroll:**
  - Ctrl+wheel or pinch zooms time at the mouse; Shift+wheel scrolls time; the wheel scrolls pitches; Alt+wheel changes the row height.
  - Opening a clip fits it (at least a bar) and centres its notes.
  - The grid uses the arrangement's ladder with coarser spacing (lines at least 56 px apart), since its step is also the length of a drawn note: about 1/16 with a bar across the panel. Snapping follows the arrangement's snap toggle.
- **Data:** snapshots carry each note's start and length in beats from the clip's content start, the song beat of that start, and velocities in MIDI steps (k/127), so a list round-trips exactly. The arrangement's preview converts with the tempo map. The Session matches notes by value when applying a new list: unchanged notes stay untouched, changed ones keep their other properties (colour, mute).

## Later steps (outline)

- **C4 clip editing** (built, see below).
- **C5 transport** (built, see below).
- **C6 device chain** (built, see below).
- **C7 instrument tracks** (built, see below).
- **C8 save/open** (built, see below).
