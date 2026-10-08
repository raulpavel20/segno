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
│ Detail panel: device chain (device panel height + 2 × 12)  ⇄  clip view (Tab)    │
└──────────────────────────────────────────────────────────────────────────────────┘
```

- Splitters resize the browser and inspector. The detail panel's height is fixed by the device panel height.
- **Adaptive:**
  - below 1440 logical px wide, the browser collapses to its rail and the inspector closes (the overlay inspector comes in M3);
  - the user's explicit toggles win until the window crosses the threshold again.
- **Focus mode** shows only the transport bar and the arrangement.
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
| Add audio track / instrument track | `track.addAudio` / `track.addInstrument` | Ctrl+T / Ctrl+Shift+T |
| Show browser / inspector / detail | `view.browser` / `view.inspector` / `view.detail` | Ctrl+Alt+B / Ctrl+Alt+I / Ctrl+Alt+D |
| Device chain ⇄ clip view | `view.detailMode` | Tab |
| Focus mode | `view.focus` | Ctrl+Alt+F |
| Zoom in / out / to fit | `view.zoomIn` / `view.zoomOut` / `view.zoomFit` | = / − / Ctrl+0 |
| Follow playhead | `view.follow` | Ctrl+Shift+F |
| Snap to grid | `view.snap` | Ctrl+4 |
| New / open / save / save as | `file.new` / `file.open` / `file.save` / `file.saveAs` | Ctrl+N / Ctrl+O / Ctrl+S / Ctrl+Shift+S |
| Audio settings | `app.audioSettings` | Ctrl+, |

Each step adds its actions as it lands; the command palette (M2) lists the registry.

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
1. lane background (alternating tone steps);
2. grid lines (bar, beat, subdivision at falling contrast);
3. clips: flat clip colour, name, and the waveform in a lighter tint (C3);
4. selection;
5. loop brace (C5);
6. playhead.

Waveforms come from a background-built peak cache per file, plus cached drawing per clip and zoom level (C3), within the 8 ms budget (ADR 0015).

## Clip editing (C4)

- **Hit testing and drag maths** live in `model/Editing` (tested):
  - the grabbed clip snaps and the rest of the selection follows by the same amount;
  - nothing moves before time zero or off the track list;
  - trims stay within the source file and leave at least 10 ms.
- **Selection:** click; Shift or Ctrl toggles; a drag on empty lanes box-selects (Shift adds); the selected track follows the click.
- **During a drag**, clips are drawn at their new place from the model's result. On release the Session applies it as one undo step, and the preview stays until the new snapshot arrives (no flicker).
- **Commands:** delete, duplicate (copies placed right after the selection), split at the playhead (the selected clips, or the selected track's clips under the playhead), select all.

## Transport (C5)

- **Transport bar:** play/stop, stop-and-return, loop and metronome toggles, then tempo and time signature as `ui::NumberField`s (drag, Shift for fine, wheel, or type; each edit is one undo step), the position in bars and time, and CPU load.
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

The arrangement's budget (ADR 0015): 32 visible tracks zooming continuously, under 8 ms per frame, in Release on the reference machine. `FossDAW --benchmark [--display N] [--software]` fills a project with 32 tracks of 8 clips, shows them all (focus mode), zooms in and out for 6 s, and prints paint time and frame rate. `--stats` shows the same numbers live.

| 2026-10-10, Release | Paint avg | Paint p95 | Frames |
|---|---|---|---|
| OpenGL, laptop panel 2880×1920 @2× | 1.9 ms | 5.9 ms | 123 fps (120 Hz panel) |
| OpenGL, external 3840×2160 @2× | 1.9 ms | 6.3 ms | 123 fps (120 Hz) |
| Software, external 3840×2160 @2× | 13.8 ms | 21.5 ms | 78 fps |

A software-renderer benchmark also runs offscreen in the integration tests (`"[.benchmark]"`, report only: offscreen software painting at 4K is bound by fill rate).

## Later steps (outline)

- **C4 clip editing** (built, see below).
- **C5 transport** (built, see below).
- **C6 device chain:** the detail panel shows the selected track's chain. Native panels plus third-party cards; add (menu, until the browser lands), reorder by drag, bypass, delete; VST3/LV2 scan in a child process.
- **C7 instrument tracks:** MIDI input monitoring through the track's instrument; `.mid` import as a read-only preview clip.
- **C8 save/open:** `.fdproj` is tracktion's edit XML plus our view state; a round-trip test.
