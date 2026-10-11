# Automation: design (M2 step C4)

How parameters change over time in Segno: automation lanes in the arrangement, what they drive, breakpoints and curves, recording moves while playing, and how automated controls look and behave. The decisions are [ADR 0028](../decisions/0028-automation-lanes.md) (the engine, the scale, the controls) and [ADR 0029](../decisions/0029-independent-automation-lanes.md) (lanes are rows of their own, with assignments). It builds on the track header kit and the time-view kit ([ui-foundations.md](ui-foundations.md), ADR 0023), the selection model ([selection.md](selection.md), ADR 0027), the mixer ([mixer.md](mixer.md), ADR 0026) and the reserved gradient (ui-principles §4).

**Status:** C4a–C4c approved and built (2026-10-09). After the product owner's first test, lanes became independent rows with assignments (ADR 0029, approved 2026-10-09, with every recommendation of §14): part C4d (§15), built 2026-10-09; signed off by the product owner (2026-10-10).

## 1. Principles

1. **An automation lane is a row of its own,** like a track: a header in the track header column and a curve in the lane area, on the same grid. It belongs to no track. It holds a curve on its own, and drives the parameters **assigned** to it: any number, on any tracks, each through its own range.
2. **One lane per parameter.** A parameter is driven by at most one lane, so what moves a control is never ambiguous. Assigning a parameter that's already on a lane moves it.
3. **A straight line is a straight move of the control.** An assigned parameter follows the lane across its range on the control's travel (the knob's arc, the fader's throw), so a straight segment moves the knob or fader evenly. A filter sweep drawn as a line sweeps like turning the knob, not linearly in hertz.
4. **What you see is what plays.** The curve drawn in a lane comes from the same function the engine plays, and a test holds the two together, for every assignment and range.
5. **One way to do each thing.** Points are edited with the same gestures as clips and notes (double-click to add, drag to move, Shift for fine, Alt to bypass snap). The controls in the modules and the mixer are the same controls, with an indicator when automated.

## 2. What can be assigned

| Channel | Parameters |
|---|---|
| **Track** | Every automatable parameter of every module in its chain (ours: those flagged `Automatable`; third-party plugins: those the plugin marks automatable); the track's volume, pan and each send |
| **Return track** | Its modules' parameters, volume and pan |
| **Master** | Its modules' parameters, volume and pan |

- **Not automatable:** settings that change a module's latency (lookahead, true peak; ADR 0020) and quality settings (ADR 0016). They can't be assigned, aren't recorded, and their controls offer no automation.
- **Mute** isn't automatable in M2: the engine's mute isn't a parameter (B32).
- **Stepped parameters** (choices, toggles, integer steps) play a lane's curve as steps: the value holds until the curve crosses to the next one. A lane whose one assignment is stepped and full-range edits as steps (§5).
- **Automation of MIDI clip content** (control changes, pitch bend) belongs to the clip view, later.

## 3. Lanes in the arrangement

```
┌ header ─────────────────────┬ lanes ─────────────────────────────────────────────┐
│▌ Bass                  M S ▐│ ███ clip ████     ███ clip ███████                 │
│▌ Subtractive Synth         ▐│                                                    │
│▌ Filter sweep           ●  ▐│       ●───────────●                                │
│▌ 2 parameters         38 % ▐│ ──────╯░░░░░░░░░░░╲░░░░░░●────────────────────      │
│▌ Drums                 M S ▐│ ███ clip ███ ███ clip ███ ███ clip ███             │
│▌ Volume                 ●  ▐│ ●─────────────────────────────╮                     │
│▌ Drums · Mixer    −6.0 dB  ▐│ ░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░╰─●──────────────     │
└─────────────────────────────┴────────────────────────────────────────────────────┘
```

- **Placement: among the tracks.** A lane is added like a track: at the end of the tracks, after the last track and any lanes there. It stays where it was put, whatever it drives; tracks added later go after it. Lanes always sit above the returns and the master. Reordering came with C11 ([tracks-and-clips.md](tracks-and-clips.md) §6, §19): every track and lane moves on its own ([ADR 0042](../decisions/0042-every-row-moves-on-its-own.md)), so moving a track leaves the lanes where they are, and a lane dragged by its header can land between any two rows among the tracks and lanes.
- **The lane header** uses the track header kit (`model::TrackHeaderLayout`), so lanes look like tracks:

  | Slot | Contents |
  |---|---|
  | Colour | The lane's own colour: the next palette colour at creation, from the same rotation as new tracks |
  | Name | Its name: "Automation 1", "Automation 2"… when made, one past the highest in use, whatever it drives; or set by hand, never empty ([tracks-and-clips.md](tracks-and-clips.md) §18; until 2026-10-11 it was taken from its assignments) |
  | Buttons | The lane's **on** switch (a round indicator, like a module's power): filled while every assignment plays; a ring when some or all are off (§7). A click turns all on when any is off, else all off |
  | Subtitle | What it drives: "Bass · Synth" for one assignment (its channel and module), "3 parameters" for several, "Not assigned" for none; "· Steps" when it edits as steps. The value now on the right, in the readout style: in the parameter's units for one assignment, otherwise a percentage |
  | Controls (height ≥ 80) | With exactly one assignment, that parameter's control as a bar |

- **Background:** lane rows use the `subLane` role. A selected lane uses `laneSelected`, like a selected track.
- **Heights:** lanes default to 56 px, 24 px minimum. Any row is resized by dragging its bottom edge in the header column; Alt+drag resizes all rows of that kind. Heights are view state, saved with the project.
- **Making a lane:**
  - **Add automation lane** (Ctrl+Shift+L, or the right-click menu of any row header or of the empty space below the rows, next to Add track and Add return track): an empty lane, like a new track, at the end of the tracks and selected. Then assign parameters to it: **Assign parameter…** in the inspector (or its header's menu, or Ctrl+Alt+Shift+L) opens the parameter picker, a searchable list like the command palette with a channel row at its top that switches to any track, return or the master; or right-click a control → **Assign to automation lane ▸**;
  - right-click any automatable control → **Automate in a new lane**: a new lane, at the end of the tracks, already driving it;
  - **Automate last touched parameter** (Alt+L): its lane, or a new one for it;
  - recording (§8) makes a lane for a parameter that has none.
  A new lane is brought into sight and selected.
- **The header's right-click menu** reads like a track's: Rename…, Assign parameter…, Turn lane on/off, **Delete automation lane**, Colour ▸ (the palette), then the Add items.
- **Deleting a lane:** **Delete automation lane** in its header's right-click menu, or Delete after clicking its header. Its assigned parameters keep their current values. One undo step, "Delete automation lane".
- **Showing and hiding:** Ctrl+Alt+L hides or shows every lane (view state, saved with the project).
- **Empty lanes** (without points) draw the value now as a dashed line (its one assignment's, mapped back). While the lane is selected, a hint says "Double-click to add a point", or for a lane that drives nothing yet, how to assign a parameter; unselected lanes stay quiet.
- **The mixer view** shows no lanes; its controls show the automation indicator (§6) and follow the played value.

## 4. Drawing a lane

- **Grid and playhead:** the arrangement's (the time-view kit); lanes scroll and zoom with the tracks.
- **The curve:** a 1.5 px line in the lane's own colour (its row colour, like a track's) over the reserved gradient fill (ui-principles §4): the same colour, faint under the line (20 %) and fading out at the lane's floor (`automationLine`, `automationFillTop`, `automationFillBottom`). Before the first point and after the last, the line runs flat to the lane's edges.
- **Points:** 6 px dots in the lane's colour; a hovered point grows to 8 px; selected points get a `textPrimary` ring.
- **Value scale:** a lane runs from 0 at its floor to 1 at its top. With one assignment, values read in that parameter's units across its range ("1.20 kHz", "−6.0 dB", "25L"); otherwise as a percentage.
- **Off lanes** (every assignment off) draw faded: the line at `fadedAlpha`, a flat faint fill.

## 5. Editing points

The same gestures as clips and notes (daw-shell.md, the piano roll):

| Input | Effect |
|---|---|
| Double-click an empty spot | Add a point there (time snapped to the grid; Alt: free) |
| Click a point; Shift or Ctrl + click | Select; toggle |
| Drag on an empty spot | Box-select points (Shift adds) |
| Drag a point (or the selection) | Move in time (snapped; Alt: free) and value (Shift: fine). Points never pass their neighbours |
| Drag a segment (the line between two points) | Move both its points up or down together |
| Alt + drag a segment | Bend it (−100 % … +100 %); Alt + double-click straightens it. A lane that edits as steps says it holds each value instead |
| Ctrl + drag across the lane | Draw: one point per grid step it passes (freehand with snap off) |
| Double-click a point, Delete | Delete the selected points |
| Ctrl+D | Duplicate the selection after itself (its span rounded up to the grid) |
| Ctrl+A | Select all points in the lane |
| ↑ / ↓ (Shift: fine) | Nudge the selected values by 1 % of the lane (0.1 %) |
| ← / → | Nudge the selected points by one grid step |

- **Every gesture is one undo step** ("Move points", "Add point", "Bend", "Draw automation"); its assigned parameters follow in the same step.
- **Steps:** a lane whose one assignment is stepped and full-range edits as steps: values snap to the parameter's steps and hold until the next point. Any other lane bends and ramps freely, and a stepped parameter on it plays the curve as steps.
- **Clips and automation:** moving, duplicating or deleting clips leaves automation where it is on the timeline (B30).

## 6. Automated controls

Every control of an assigned parameter shows it, wherever it appears (module panels, track headers, the inspector, the mixer view, lane headers):

- **The indicator:** a 4 px disc in the `automation` colour (left of a knob's name; a bar's, fader's, toggle's or choice's top-left corner). It is filled while its assignment plays and a **ring** while it's off (§7), so it doesn't rely on colour alone.
- **Following the played value:** during playback, assigned controls move with their lane. Module panels refresh from the engine; the mixer's controls read the played value every frame while their assignment plays.
- **Right-click** on any control (ui-principles §7) opens its menu:
  - assigned: **Show automation lane**, **Turn automation back on** (when off), **Remove from automation lane**;
  - always: **Assign to automation lane ▸** (the lanes; its own is ticked; choosing another moves it), **Automate in a new lane**;
  - **Reset to default**.

  In a plugin host, outside the DAW, the menu only offers Reset (the host automates our plugins its own way).
- The host binding (`ui::ParamBinding`) tells controls a parameter's automation state and adds the automation items to the menu, so module panels stay the same code in both hosts.

## 7. Moving an automated control

When you move a control whose assignment plays, while not recording:

- **its assignment turns off** and the control keeps the value you set. The lane's other assignments keep playing. The control's indicator becomes a ring, the lane's on switch a ring, and a lane with every assignment off fades. Nothing is lost: the points and the assignment stay;
- **you turn it back on where it is:** the lane's on switch (a ring while partly off) turns its assignments back on, an assignment's switch in the inspector turns just that one back on, and the control's menu has "Turn automation back on". There is no button for it in the top bar (product owner, after trying C5b); "Turn all automation back on" turns every one back on from the command palette or Ctrl+Alt+A;
- this is a listening state, like solo: saved with the project, never an undo step and never an unsaved change on its own.

## 8. Recording

- **Automation arm** (the top bar's arm button beside Record and Capture, lit in the `record` colour because it records; Ctrl+Shift+A): while armed and playing, moving a control writes its moves into its lane.
  - A control on a lane: its moves are written into that lane, mapped back through its range (moves beyond the range are clipped to it). The lane's other assignments follow.
  - A control without a lane: a new lane is made for it, at the end of the tracks.
- **Touch** is the only mode in M2 (B31): a lane is written while you hold the control; letting go stops writing, and the lane continues as it was. Moves without a held gesture (typing a value, the mouse wheel) are written as they happen, and writing stops a moment after the last one.
- **Each pass is one undo step** ("Record automation"), however many controls were moved.
- **Thinning:** recorded moves are thinned to the points needed to redraw them within 0.2 % of the travel.
- **Looping:** recording across the loop's end continues at its start, overwriting what the previous lap wrote.
- **One lane, one recording:** two controls on the same lane moved in one pass both write into it; where they overlap in time, the later one wins.
- Recording audio and MIDI (C5) arms tracks separately; Automation arm is independent of it.

## 9. Selection and the context panels

Lanes and points join the one selection (ADR 0027, [selection.md](selection.md)):

| Click | Channel | Clips | Lane | Points |
|---|---|---|---|---|
| A lane's header, or an empty spot in it | Its first assignment's channel (none if it has none) | None | That lane | None |
| A point (or a box of points) in a lane | The same | None | That lane | Those points |
| A track header, a clip | As in selection.md | | None | None |

**The detail panel**, for a lane: its first assignment's channel's **modules**, with that module selected (a mixer parameter: the chain as it is; no assignment: nothing). Live MIDI follows that channel as for a track.

**The inspector**, for a lane (no TRACK, MIX or SENDS: a lane belongs to no track):
- **POINT** with one point selected (Time in bars.beats.16ths, Value as a field you can type into, Bend), or **POINTS** with several ("Selected: N");
- **LANE:** Name (a field: type to rename), Points, the value now, whether it plays;
- **ASSIGNMENTS:** one entry per assignment:
  - its on switch, "Bass › Cutoff" (a click shows its module below), and a remove button;
  - **Range:** from and to, as fields in the parameter's units (drag or type; to below from inverts);

  then **Assign parameter…**, which opens the picker for a channel (the lane's first assignment's, or for a lane that drives nothing the channel selected last; its channel row switches). Parameters already on another lane are marked "on Filter sweep"; choosing one moves it here.

Delete, Ctrl+D, Ctrl+A and the arrows act on points when a lane was used last; Delete after clicking a lane's header removes the lane.

## 10. Keyboard and actions

| Action | ID | Shortcut |
|---|---|---|
| Automation arm | `automation.arm` | Ctrl+Shift+A |
| Turn all automation back on | `automation.reEnable` | Ctrl+Alt+A (palette; no button) |
| Add automation lane | `automation.addLane` | Ctrl+Shift+L |
| Assign parameter to the selected lane… | `automation.assign` | Ctrl+Alt+Shift+L |
| Automate last touched parameter | `automation.showLastTouched` | Alt+L |
| Show automation lanes | `view.lanes` | Ctrl+Alt+L |

## 11. Assistants (MCP)

Each tool comes with a test that drives it through MCP (ADR 0021). A **lane** is named by its id ("a:12"). A **parameter** is a module's parameter (`module` and `parameter`, an id or a name) or a channel's mix parameter (`track` and `parameter`: `volume`, `pan`, or `send` with `return`). Positions are musical (beats or "5.1.1"); values are plain values in a parameter's unit or its text ("1.2 kHz", "-6 dB", "25L"); bend is −1 … 1.

| Tool | Arguments | Result |
|---|---|---|
| `add_automation_lane` | `after?` (a track or a lane; by default at the end of the tracks, like a new track), `name?`, `parameters?` (each with `from?`, `to?`) | The lane's id. One undo step |
| `set_automation_lane` | `lane`, `name` | Renames it |
| `remove_automation_lane` | `lane` | Its parameters keep their values |
| `assign_automation` | `lane`, a parameter, `from?`, `to?` (the parameter's units or text) | The assignment; `movedFrom` if it was on another lane |
| `unassign_automation` | a parameter | |
| `get_automation` | `lane` or a parameter, `from?`, `to?` | The points (position, lane value 0–1, and value and text in the parameter's units: the given one's, or the lane's single assignment's), bend, the assignments with their ranges, whether it plays |
| `set_automation` | `lane` or a parameter, `points`, `from?`, `to?` | Replaces the points (all, or in the range). By parameter: values in its units, mapped through its range (a value outside the range is an error naming it), and a lane is made for it if it has none. The result lists the other parameters the lane moves |
| `clear_automation` | `lane` or a parameter, `from?`, `to?` | Removes points; by parameter it refuses on a lane with other assignments (use the lane, or `unassign_automation`) |
| `set_automation_on` | `lane`, a parameter, or `all` | Turns assignments back on (or off). Not an undo step |

`get_project` lists `automationLanes` (`lane`, its id; name, after, point count, whether it plays, and each assignment's track, module, parameter, return, range as values and text, and on), and `get_module` gives each parameter's `automation` (its lane, point count, range, on). Assistants never read or change whether lanes are shown (view state).

## 12. Model and view

- **`model::Automation`** (JUCE-free, `[fast]` tests):
  - the curve function, exactly the engine's;
  - point editing as pure functions on a list of points;
  - `AssignmentRange` and `mapValues` (a lane's points through a range: values only, bends kept, exact for any range including inverted ones);
  - `replaceRange`;
  - `spliceRecorded` (a recorded region back into a lane, through the range, keeping every untouched point).
- **The snapshot** lists `automationLanes` in row order. Each lane has an id, name, colour, the track it follows, its points (time in seconds, value 0 … 1, curve), and its assignments (channel, parameter key, range, on, and names for display). Lane edits change the snapshot; played values don't.
- **Rows** (`model::ArrangementRows`): track, lane, return and master rows with stable keys ("c12", "a40"). Lanes follow the track they sit after; clip editing maps a lane row to the track above it.
- **Lane headers** are the track header kit with the lane's on switch, readout and bar.

## 13. Engine (tracktion)

- **Lanes are Segno's own state** in the project: an `FDAUTOMATION` element with one `LANE` per lane (id, name, colour, the track it follows), holding its `POINTS` and `ASSIGN` entries (channel, parameter key, from, to). They are written through the edit's undo manager; project format 3.
- **The lane is the source of truth.** Each assigned parameter's tracktion automation curve is a derived copy: the lane's points through the assignment's range (`from + v × (to − from)`), then onto the parameter's own units (as C4a: normalised for modules, the taper for volume and sends, −1 … 1 for pan). tracktion's bend formula is linear in values, so the copy plays exactly what the lane draws, even inverted.
- **Derivation happens inside each command's undo step**, so undo restores a lane and every curve it drives together. Curves are updated in place (only what changed), never emptied and refilled, so editing a lane never rebuilds the playback graph.
- **Opening a project** converts older ones (format 2: one lane per parameter that had points or a shown lane, after its channel, full range), drops assignments whose channel or module is gone, and derives every curve, without marking the project unsaved.
- **Recording** writes into the parameter's curve, as tracktion does. When a pass on that parameter ends (tracktion's punch-out), the recorded region is spliced back into its lane through the range, and the lane's other assignments are derived again, inside the pass's undo step.
- **Unchanged from C4a:** our modules' parameters are normalised in the DAW; volume and sends use Segno's taper; playback bends as drawn and a one-point curve holds (the tracktion patches); stepped parameters are discrete; non-automatable parameters are refused; played values never dirty the project; a module with automation runs in 128-sample sub-blocks; arming sets every track to touch; on/off is tracktion's curve bypass, per assignment.
- **Tempo:** lanes and curves are both in seconds. A future tempo remap must move lanes and derive again.

## 14. Decisions from the reviews (product owner, 2026-10-09)

| # | Question | Answer |
|---|---|---|
| Q1 | **Moving an automated control** when not recording | Agreed (B29): its assignment turns off until turned back on; one button turns every one back on |
| Q2 | **Automation and clips** | Agreed (B30): automation stays on the timeline in M2 |
| Q3 | **Write modes** | Agreed (B31): touch only; latch with hardware controllers |
| Q4 | **Mute automation** | Agreed (B32): not in M2 |
| Q5 | **Lanes under their track, or rows of their own?** | Product owner, after testing: rows of their own among the tracks, with assignments (ADR 0029) |
| Q6 | **Where a lane sits** | Among the tracks, staying put (where a new one goes: Q18) |
| Q7 | **A range per assignment** | Yes, in the parameter's units; inverting allowed |
| Q8 | **Lanes per parameter** | One; assigning moves it |
| Q9 | **The name** | "Automation lane" |
| Q10–Q17 | Recommendations accepted with the plan (B33–B40): new lanes under the selected track; Ctrl+Alt+L instead of per-track folding; a lane's context is its first assignment's channel; the picker makes lane and assignment in one step; names come from assignments; steps only for one full-range stepped assignment; recording into a range clips; control menus remove from a lane rather than clear | Agreed |
| Q18 | **Making a lane, after the first build** | Product owner, after testing: a lane is added like a track, not from a track: empty, at the end of the tracks, with its own palette colour; parameters are assigned afterwards (the picker's add mode and "Empty automation lane" go). Every row header and the empty space below the rows get a right-click menu with Add track, Add automation lane and Add return track (the mixer view's empty space too) |

## 15. Building it

| Part | Scope | Status |
|---|---|---|
| **C4a Engine and model** | Engine patches; normalised parameters and the taper; per-parameter curves; recording passes; MCP | Built |
| **C4b Lanes in the arrangement** | Rows; lane headers; drawing; editing; selection | Built (rows under their track, superseded by C4d) |
| **C4c Controls and recording** | The indicator, menus, following, the transport buttons | Built |
| **C4d Independent lanes** | Lanes as rows of their own with assignments and ranges; storage, derivation, conversion, recording into lanes; the picker's channel row and modes; the inspector's assignments; control menus; MCP lane tools; popovers that close on an outside click | Built |

## 16. Tests (Done when)

- **The curve** equals tracktion's for every assignment, through full, partial and inverted ranges, at many times, in drawing and in playback; a rendered ramp follows the lane.
- **Control units:** a straight segment sweeps a skewed knob evenly; a volume segment passes the taper's halfway position halfway.
- **Assignments:** one lane drives parameters on two tracks; assigning moves a parameter between lanes in one undo step; unassigning leaves the parameter's value.
- **Undo:** each gesture and command is one step, restoring the lane and every curve it drives; a recorded pass is one step; on/off and showing lanes are never steps.
- **Recording:** into a lane with two assignments, the recorded region is spliced and the other parameter follows; a parameter without a lane gets one; looping.
- **Override:** moving a control turns its assignment off and only it.
- **Projects:** lanes, ids, order, ranges and on/off survive saving and opening without an unsaved mark; a format-2 project converts and sounds the same; deleted modules' assignments are pruned.
- **Played values** never mark the project unsaved or rebuild a snapshot; editing a lane never rebuilds the playback graph.
- **MCP:** each tool in §11.
- **Performance:** 64 tracks with two lanes each within the frame budget (`[.benchmark]`, Release, software renderer: p95 about 3.5 ms at 1700 × 720).
- **Manual:** [manual-tests/M2.md](../manual-tests/M2.md), "Automation (C4)".
