# Automation: design (M2 step C4)

How parameters change over time in Segno: automation lanes in the arrangement, breakpoints and curves, recording moves while playing, and how automated controls look and behave. The decision is [ADR 0028](../decisions/0028-automation-lanes.md). It builds on the track header kit and the time-view kit ([ui-foundations.md](ui-foundations.md), ADR 0023), the selection model ([selection.md](selection.md), ADR 0027), the mixer ([mixer.md](mixer.md), ADR 0026) and the reserved gradient (ui-principles §4).

**Status:** approved by the product owner (2026-10-09), with every recommendation of §14. Built (2026-10-09) in three parts (§15); awaiting the product owner's feel check.

## 1. Principles

1. **Lanes are rows, like tracks.** Each automated parameter gets its own lane under its track: a header in the track header column and the curve in the lane area, on the same grid, with the same height handling and selection as a track. Every automated parameter of a track shows at once. There is no chooser that shows one parameter at a time.
2. **Nothing is hidden by accident.** A track's lanes fold away together with one click (and every track's at once with one shortcut), but a parameter with automation always has a lane, and a folded track says how many lanes it holds.
3. **A straight line is a straight move of the control.** A lane's height is the control's travel (the knob's arc, the fader's throw), so a straight segment between two points moves the knob or fader evenly. A filter sweep drawn as a line sweeps like turning the knob, not linearly in hertz.
4. **What you see is what plays.** The curve drawn in a lane comes from the same function the engine plays, and a test holds the two together.
5. **One way to do each thing.** Points are edited with the same gestures as clips and notes (double-click to add, drag to move, Shift for fine, Alt to bypass snap). The controls in the modules and the mixer are the same controls, now with an indicator when automated.

## 2. What can be automated

| Channel | Parameters |
|---|---|
| **Track** | Every automatable parameter of every module in its chain (ours: those flagged `Automatable`; third-party plugins: those the plugin marks automatable); the track's volume, pan and each send |
| **Return track** | Its modules' parameters, volume and pan |
| **Master** | Its modules' parameters, volume and pan |

- **Not automatable:** settings that change a module's latency (lookahead, true peak; ADR 0020) and quality settings (ADR 0016). They have no lane, aren't recorded, and their controls offer no automation.
- **Mute** isn't automatable in M2: the engine's mute isn't a parameter (§13, Q4).
- **Stepped parameters** (choices, toggles, integer steps) get **step lanes**: the value holds until the next point, with no curves.
- **Automation of MIDI clip content** (control changes, pitch bend) belongs to the clip view, later.

## 3. Lanes in the arrangement

```
┌ header ─────────────────────┬ lanes ─────────────────────────────────────────────┐
│▌▾ Bass                 M S ▐│ ███ clip ████     ███ clip ███████                 │
│▌  Subtractive Synth        ▐│                                                    │
│▌   Cutoff               ●  ▐│       ●───────────●                                │
│▌   Synth          1.20 kHz ▐│ ──────╯░░░░░░░░░░░╲░░░░░░●────────────────────      │
│▌   Volume               ●  ▐│ ●─────────────────────────────╮                     │
│▌   Mixer          −6.0 dB  ▐│ ░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░╰─●──────────────     │
│▌▸ Drums  · 3 lanes     M S ▐│ ███ clip ███ ███ clip ███ ███ clip ███             │
└─────────────────────────────┴────────────────────────────────────────────────────┘
```

- **Order:** a track's lanes sit directly under it, in the order they were added. Returns' and the master's lanes sit under them, below the tracks.
- **Folding:** each track header has a disclosure chevron in its left padding (where the name doesn't move). ▾ shows the track's lanes, ▸ folds them. It appears only when the track has lanes; a folded track's subtitle adds "· 3 lanes". Alt+click folds or unfolds every track. **Show automation lanes** (Ctrl+Alt+L) does the same from the keyboard and the palette.
- **The lane header** uses the track header kit (`model::TrackHeaderLayout`), so lanes look like tracks:

  | Slot | Contents |
  |---|---|
  | Colour | The track's colour, so lanes read as part of their track |
  | Name | The parameter's name ("Cutoff", "Volume", "Send A"), indented 12 px from the track's name |
  | Buttons | The lane's **on** switch (a round indicator, like a module's power): on, the lane plays; off, the parameter keeps the value you set (§7) |
  | Subtitle | The module's name ("Synth", "Mixer"), and the parameter's current value on the right, in the readout style |
  | Controls (height ≥ 80) | The parameter's own control as a bar: dragging it is the same as dragging the knob in the module |

- **Background:** lane rows use the `subLane` role (already reserved for automation). A selected lane uses `laneSelected`, like a selected track.
- **Heights:** lanes default to 56 px (name and subtitle), 24 px minimum. Any row, track or lane, is resized by dragging its bottom edge in the header column; Alt+drag resizes all rows of that kind. Heights are view state, saved with the project.
- **Showing a lane** (it appears under its track, unfolding the track):
  - right-click any automatable control, in a module, a track header, the inspector or the mixer view → **Show automation lane**;
  - **Add automation lane** (Ctrl+Shift+L, or the track header's right-click menu): a searchable list of the selected track's parameters, like the command palette (one row per parameter: name, module, current value; already-shown lanes are marked);
  - **Show last touched parameter** (palette): the lane of the parameter you last moved, ours or a third-party plugin's;
  - recording (§8) shows every lane it writes to.
- **Removing a lane:** **Remove lane** in the lane header's right-click menu, or Delete after clicking the lane's header. It deletes the lane's points (one undo step, "Remove automation"). An empty lane is just removed.
- **Empty lanes** (added but without points) show the parameter's current value as a dashed line, with the hint "Double-click to add a point". They are view state and stay until removed.
- **The mixer view** shows no lanes; its controls show the automation indicator (§6) and follow the played value.

## 4. Drawing a lane

- **Grid and playhead:** the arrangement's (the time-view kit: `GridPainter`, `Playhead`, `BoxSelection`, `TimeInput`); lanes scroll and zoom with the tracks.
- **The curve:** a 1.5 px line in the `automation` colour, over the reserved gradient fill (ui-principles §4): `automationFillTop` under the line fading to `automationFillBottom` at the lane's floor. Before the first point and after the last, the line runs flat to the lane's edges.
- **Points:** 6 px dots in the `automation` colour; a hovered point grows to 8 px; selected points get a `textPrimary` ring. A segment under the mouse brightens, showing it can be dragged.
- **Value scale:** the lane's height is the control's travel: the knob's normalised range for module parameters, the volume taper for volume and sends (0 dB at 80 %), −1 … 1 for pan. While hovering or dragging, the top and bottom values show at the header edge and the value under the mouse beside it, as the control would show it ("1.20 kHz", "−6.0 dB", "25L").
- **Off lanes** (§7) draw their line and fill faded (`fadedAlpha` roles), with the indicator hollow.
- **New roles** in `fd_ui` (ui-foundations.md): `automation` (`effectStart`), `automationFillTop` and `automationFillBottom` (the pair at 40 % and 4 %, as in the design gate's lane), `automationOff`. No view mixes colours itself (the UI lint).

## 5. Editing points

The same gestures as clips and notes (daw-shell.md, the piano roll):

| Input | Effect |
|---|---|
| Double-click an empty spot | Add a point there (time snapped to the grid; Alt: free) |
| Click a point; Shift or Ctrl + click | Select; toggle |
| Drag on an empty spot | Box-select points (Shift adds) |
| Drag a point (or the selection) | Move in time (snapped; Alt: free) and value (Shift: fine). Points never pass their neighbours; a step parameter's values snap to its steps |
| Drag a segment (the line between two points) | Move both its points up or down together |
| Alt + drag a segment | Bend it: up or down curves the segment (−100 % … +100 %); Alt + double-click straightens it |
| Ctrl + drag across the lane | Draw: one point per grid step it passes (freehand with snap off) |
| Double-click a point, Delete | Delete the selected points |
| Ctrl+D | Duplicate the selection after itself (its span rounded up to the grid), as with notes |
| Ctrl+A | Select all points in the lane |
| ↑ / ↓ (Shift: fine) | Nudge the selected values by 1 % of the travel (0.1 %) |
| ← / → | Nudge the selected points by one grid step |

- **Every gesture is one undo step** ("Move points", "Add point", "Bend", "Draw automation").
- **Values while dragging** show next to the mouse in the parameter's text ("1.20 kHz").
- **Clips and automation:** moving, duplicating or deleting clips leaves automation where it is on the timeline (Q2). Range editing across clips and lanes together (select a time range, then copy, move or delete everything in it) comes later.

## 6. Automated controls

Every control of an automated parameter shows it, wherever it appears (module panels, track headers, the inspector, the mixer view, lane headers):

- **The indicator:** a 4 px disc in the `automation` colour at the control's top-left corner (a knob: left of its name; a bar or fader: at its left or top end; a toggle or a choice: in its corner). It is filled while the lane plays and a **ring** while the lane is off (§7), the same filled-or-ring language as view options and power, so it doesn't rely on colour alone.
- **Following the played value:** during playback, automated controls move with the automation. Module panels already refresh their controls from the engine; the mixer's controls (headers, inspector, strips) read the live value every frame while their lane plays, instead of waiting for a snapshot.
- **Right-click** on any control (ui-principles §7) opens its menu: **Show automation lane**, **Clear automation** (when it has some), **Turn automation back on** (when its lane is off), and **Reset to default**. Modulation and MIDI learn join the menu when they arrive. In a plugin host, outside the DAW, the menu only offers Reset (the host automates our plugins its own way).
- The host binding (`ui::ParamBinding`) tells controls a parameter's automation state and carries the menu's commands, so module panels stay the same code in both hosts.

## 7. Moving an automated control (Q1)

When you move a control whose lane plays, while not recording:

- **the lane turns off** and the control keeps the value you set. The lane's on switch goes off, its curve fades, and the control's indicator becomes a ring. Nothing is lost: the points stay;
- **the transport bar's "Turn automation back on" button lights** (beside Automation arm) while any lane is off. Clicking it (or Ctrl+Alt+A, or **Turn automation back on** in the palette) turns every lane back on; a lane's own switch turns just that one back on;
- this is a listening state, like solo: saved with the project, never an undo step and never an unsaved change on its own.

The alternative is that the lane always wins: the control snaps back to the curve as soon as you let go, even when stopped. That's predictable but makes it impossible to try a different value without editing the lane.

## 8. Recording

- **Automation arm** (the transport bar's arm button, a line through two points; Ctrl+Shift+A): while armed and playing, moving a control writes its moves into its lane, adding the lane if it isn't shown.
- **Touch** is the only mode in M2 (Q3): a lane is written while you hold the control; letting go stops writing, and the existing curve continues from there. Moves without a held gesture (typing a value, the mouse wheel) are written as they happen, and writing stops a moment after the last one.
- **Each pass is one undo step** ("Record automation"), however many controls were moved.
- **Thinning:** recorded moves are thinned to the points needed to redraw them within 0.2 % of the travel, so lanes stay editable.
- **Looping:** recording across the loop's end continues at its start, overwriting what the previous lap wrote.
- Recording audio and MIDI (C5) arms tracks separately; Automation arm is independent of it.

## 9. Selection and the context panels

Lanes and points join the one selection (ADR 0027, [selection.md](selection.md)):

| Click | Channel | Clips | Lane | Points |
|---|---|---|---|---|
| A lane's header | Its track | None | That lane | None |
| A point (or a box of points) in a lane | Its track | None | That lane | Those points |
| An empty spot in a lane | Its track | None | That lane | None |
| A track header, a clip | As in selection.md | | None | None |

The invariant stays: the selected channel is the channel of what was selected last.

**The detail panel**, for a lane: its track's **modules**, with the lane's module selected and scrolled into view (a mixer lane: the chain as it is). **The inspector**, for a lane: a **LANE** section (Parameter, Module, Points, the value now, the on switch), then the track's sections (TRACK, MIX, SENDS). With one point selected, a **POINT** section above it (Time in bars.beats.16ths, Value as a field you can type into in the parameter's units, Bend); with several, "Selected: N". Delete, Ctrl+D, Ctrl+A and the arrows act on points when a lane was used last.

## 10. Keyboard and actions

| Action | ID | Shortcut |
|---|---|---|
| Automation arm | `automation.arm` | Ctrl+Shift+A |
| Turn automation back on | `automation.reEnable` | Ctrl+Alt+A (and its transport button) |
| Add automation lane… | `automation.addLane` | Ctrl+Shift+L |
| Show last touched parameter | `automation.showLastTouched` | Alt+L |
| Show automation lanes (all tracks) | `view.lanes` | Ctrl+Alt+L |

## 11. Assistants (MCP)

Each tool comes with a test that drives it through MCP (ADR 0021). A **target** is a module's parameter (`module` and `parameter`, an id or a name) or a channel's mix parameter (`track` and `parameter`: `volume`, `pan`, or `send` with `return`). Positions are musical (beats or "5.1.1"); values are plain values in the parameter's unit or its text ("1.2 kHz", "-6 dB", "25L"), as for `set_parameters`; bend is −1 … 1.

| Tool | Arguments | Result |
|---|---|---|
| `get_automation` | target, `from?`, `to?` | The lane's points (position, value, text, bend), whether it's on, and its value range |
| `set_automation` | target, `points`, `from?`, `to?` | Replaces the points in the range (all of them without one) with these; shows the lane. One undo step |
| `clear_automation` | target, `from?`, `to?` | Removes points (all, or in the range) |
| `set_automation_on` | target, or `all: true` | Turns a lane (or every lane) back on |

`get_project` and the track resources list each channel's lanes (target, parameter name, module, point count, on), and `get_module` marks each parameter's automation state. Assistants never read or change which lanes are shown or folded (view state).

## 12. Model and view

- **`model::Automation`** (JUCE-free, `[fast]` tests): the curve function (the value at a time for points with bends, exactly the engine's), point editing as pure functions on a list of points (add, move with neighbours as limits, move a segment, bend, draw over grid steps, duplicate, delete, nudge), thinning, and the value ↔ lane-height mapping per scale (normalised, volume taper, send taper, pan).
- **The snapshot** gives each channel its lanes: the target, names, scale, whether stepped, on or off, and the points (time in seconds, value in the control's units 0 … 1, bend). A lane's points change the snapshot; a played value doesn't.
- **Rows:** `model::TrackLayout` becomes a list of rows with kinds (track, lane, return, master) and stable keys, so heights follow their row and clip editing maps rows to tracks instead of assuming row = track. Heights are saved in the project's view state.
- **Lane headers** are the track header kit with a lane option (indent, the on switch, the value readout).

## 13. Engine (tracktion)

- **Curves** are tracktion's automation curves (stored in the project with each parameter's plugin), one per parameter, in seconds, remapped when the tempo changes.
- **Our modules' parameters automate in control units.** Their tracktion parameters become normalised (0 … 1, the knob's travel), converted to plain values (`ParamSpec::fromNormalized`) before each block. That's what plugin hosts already automate (normalised values), so the device sees the same thing in both hosts. Projects saved before C4 are converted when opened.
- **Volume and sends automate on our taper.** tracktion's fader-position mapping becomes ours (a small patch: 0 dB at 80 %, +6 dB at the top), so a straight volume segment is a straight fader move; sends use the lower 80 % of the same scale (0 dB at the top of a send). Projects saved before C4 are converted when opened.
- **Curve shapes:** tracktion draws and plays bends with two different formulas; a patch makes playback use the drawn one, and `model::Automation` matches it (tested against the engine, and by rendering a gain ramp to within 0.01 dB). Bends use tracktion's smooth range; steps use its step shape.
- **A one-point lane** holds its value over the whole song (tracktion plays a curve only from two points; patched).
- **Stepped parameters** are marked discrete for tracktion, so recorded and drawn values hold as steps.
- **Not automatable** parameters (no `Automatable` flag) are refused by the DAW adapter for lanes, recording and MCP.
- **Played values never dirty the project.** tracktion writes the played value back into the project as it plays; the Session ignores those writes for parameters that are following their lane (no unsaved mark, no snapshot rebuild).
- **Timing:** a module with automation is processed in sub-blocks of 128 samples (at 48 kHz, 2.7 ms), and our modules smooth parameter changes (20 ms by default). Adding the first lane to a module, or removing its last, rebuilds the playback graph so the sub-blocks take effect.
- **Modes:** tracktion's tracks read automation by default; arming sets every track to touch, disarming back to read. Recording writes through the edit's undo manager, and the Session closes one transaction per pass.
- **Lane on/off** is tracktion's curve bypass (not undoable, which matches §7).

## 14. Decisions from the review (product owner, 2026-10-09)

| # | Question | Answer |
|---|---|---|
| Q1 | **Moving an automated control** when not recording: does its lane turn off until turned back on (§7), or does the lane always win (the control snaps back)? | Agreed (B29): turn off, with a per-lane switch and one "Automation" button to turn every lane back on |
| Q2 | **Automation and clips:** when a clip moves, does the automation under it stay on the timeline, or move with it? | Agreed (B30): stay in M2; moving with clips as an option when range editing comes |
| Q3 | **Write modes:** touch only in M2, or also latch (keep writing the last value until you stop)? | Agreed (B31): touch only; latch when hardware controllers arrive (B5) |
| Q4 | **Mute automation:** the engine's mute isn't a parameter. Skip it in M2, or add a track "on" parameter now? | Agreed (B32): skip in M2 (automate volume to silence instead); add it with M3's track work |

## 15. Building it

Three parts, each its own commit with its tests, MCP tools and manual checks:

| Part | Scope | Done when |
|---|---|---|
| **C4a Engine and model** | The engine patches; normalised module parameters and the taper, with the project conversion; lanes and points in the snapshot; `model::Automation`; Session commands (points, bends, lanes, on/off, arm, recording passes); played values ignored; the MCP tools of §11 | The curve matches the engine and a rendered gain ramp; a straight segment is a straight control move; one-point lanes hold; step lanes step; non-automatable parameters refused; one undo step per edit and per pass; old projects sound the same after conversion; every tool through MCP |
| **C4b Lanes in the arrangement** | Rows with kinds and keys; lane headers; drawing (roles, gradient); the editing gestures of §5; folding; resizing rows; the add-lane list; selection and the context panels (§9); the actions of §10; view state | Row and lane layout model tests; editing tests (each gesture one undo step); selection rules; 60 fps with 64 tracks of two lanes each; snapshots reviewed |
| **C4c Controls and recording** | The indicator on every control; right-click menus; following played values in the mixer's controls; the Automation button (arm, turn back on) in the transport bar; overriding and turning back on (§7) | Smoke tests of the menus, the override and turning back on, a recorded pass with the mouse; manual feel check |

## 16. Tests (Done when)

- **The curve** of `model::Automation` equals tracktion's at many times for random points and bends (within 1e-6), and a rendered gain ramp follows the drawn curve within 0.01 dB.
- **Control units:** a segment from 100 Hz to 10 kHz on a cutoff sweeps evenly on the knob (its halfway point is the knob's centre value); a volume segment from 0 dB to −24 dB passes the taper's halfway position halfway.
- **Lanes:** a one-point lane holds; a step lane steps exactly at its points; a non-automatable parameter is refused (lanes, recording, MCP).
- **Undo:** each gesture of §5 is one step; a recorded pass is one step; lane on/off and folding are never steps.
- **Recording:** a scripted pass with a held control writes its moves and stops at release (touch); the lane appears; thinning keeps the shape within 0.2 %.
- **Override:** moving an automated control turns its lane off and keeps the value; turning it back on returns to the curve.
- **Projects:** lanes, points, bends, on/off, fold state and heights survive saving and opening; a project saved before C4 sounds the same after conversion (volume, sends and every module parameter).
- **Played values** never mark the project unsaved or rebuild a snapshot.
- **MCP:** each tool in §11.
- **Performance:** 64 tracks with two lanes each paint within the frame budget while playing.
- **Manual:** [manual-tests/M2.md](../manual-tests/M2.md), "Automation (C4)".
