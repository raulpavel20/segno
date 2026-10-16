# Selection and the context panels (M2 step A5)

What you select, and what the context panels (the detail panel at the bottom and the inspector at the right) show for it, in the arrangement and in the mixer view. The decision is [ADR 0027](../decisions/0027-selection-and-context-panels.md). It refines how the panels behave around the mixer view ([ADR 0026](../decisions/0026-mixing-in-the-arrangement.md), [mixer.md](mixer.md)) and builds on the shell ([daw-shell.md](daw-shell.md)) and the UI principles ("context, not dialogs").

**Status:** approved by the product owner (2026-10-09), after the feel check of the mixer found the panels confusing. Built (2026-10-09); signed off by the product owner (2026-10-10).

## 1. One selection

The window has **one selection**, shared by the arrangement and the mixer view:

- **a channel:** a track, a return track or the master (or none);
- **clips:** any number, on any tracks (none in the mixer view, which shows no clips).

Inside the panels there are smaller selections of their own: a module in the chain, notes in the clip view. They never change the main selection.

**The rule that keeps it simple:** the selected channel is always the channel of what you last selected. Whatever you click becomes the selection, and the panels always show that one thing. There is never a selected track that disagrees with a selected clip.

The selection is view state: it is not saved, not an undo step, and assistants never read it (MCP tools name tracks, clips and modules by id).

## 2. What a click selects

**Arrangement**

| Click | Channel | Clips |
|---|---|---|
| A track's header (name zone) | That track | None |
| A return row (header or lane) | That channel | None |
| The song row's header, or empty time in it (ADR 0049) | The master | None |
| A section in the song row (Shift: the run to it) | The master | None; the section(s) selected ([arranger.md](arranger.md) §10) |
| An empty spot in a track's lane | That track | None (a drag box-selects) |
| A clip | The clip's track | That clip (a click inside a multiple selection keeps the group for a drag, and selects just that clip if released without one) |
| Shift or Ctrl + click a clip | The clicked clip's track (if the click removed it: the topmost remaining clip's track) | Adds or removes it |
| A drag box over clips (Shift adds) | The track of the topmost selected clip (none: the track the box started on) | The clips in the box |
| Dragging clips to another track | The grabbed clip's new track | Unchanged |
| A header's mute, solo, volume or pan control | Unchanged (the control takes the click) | Unchanged |
| An automation lane's header, or an empty spot in it (C4) | Its first assignment's channel (none without one), and that lane | None |
| A point in a lane, or a box of points (C4) | The same, the lane and those points | None |

**Mixer view**

| Click or key | Channel | Clips |
|---|---|---|
| A strip (its name, its background) | That channel | None |
| A module name in a strip | That channel, and that module in the detail panel | None |
| Left / Right | The neighbouring strip | None |
| A strip's controls (fader, pan, mute, solo, sends, value) | Unchanged | Unchanged |

**Other ways:** Add return track selects the new return; New MIDI clip (Ctrl+Shift+M or a double-click) selects the new clip; turning on the computer keyboard selects the first instrument track if no instrument track is selected; Delete, Duplicate and Split leave their result selected.

**Notifications:** each action changes the selection once. A box selection updates the arrangement while you drag and tells the panels when you let go, so the chain isn't rebuilt on every mouse move.

## 3. The subject

What the context panels show follows from the selection:

| Selection | Subject |
|---|---|
| One clip | That clip (and its track) |
| Several clips | The clips (and the selected track) |
| A channel, no clips | That channel |
| A take (its strip under a clip, C5b) | That take (and its track) |
| One section, or several (M3 C3) | The section(s) (and the master) |
| Nothing | Nothing |

## 4. The detail panel

| Subject | Arrangement | Mixer view |
|---|---|---|
| One MIDI clip | **Clip** view: the piano roll | **Modules** of its track |
| One audio clip | **Clip** view: the audio editor (M3 P6a, [audio-editor.md](audio-editor.md) §4) | **Modules** of its track |
| A lane (C4) | **Modules** of its first assignment's channel, that module selected (none without an assignment) | the same |
| Several clips, a take | **Modules** of the selected track | **Modules** of the selected track |
| A channel | **Modules** of that channel (a return's or the master's chain too) | **Modules** of that channel |
| Nothing | "Select a track to see its modules" | the same |

- **Arrangement:** the header has two tabs, **Modules** and **Clip**, with "Tab switches" on the right. A change of subject picks the tab from the table; Tab (or a click on a tab) switches until the subject changes again. The header's context names what the tab shows: the track in Modules, the clip in Clip.
- **Mixer view:** clips aren't on screen, so neither is the clip view. The header shows a plain **Modules** title with no tabs, and Tab does nothing. A module-name click in a strip opens the panel if it's closed and selects that module; selecting another strip shows that channel's modules.
- **Switching views** keeps each view's own state: back in the arrangement, the panel shows what it showed before (the clip view, if a clip is still selected and you didn't Tab away from it).

## 5. The inspector

**Arrangement:** the properties of the subject, in sections. Each section has a title in small mono caps (the `Section` text style, `textTertiary`), and a 1 px `line` separates neighbouring sections: the module panel's sections, stacked vertically. They are laid out by the shared `ui::SectionStack` ([ui-foundations.md](ui-foundations.md)).

| Section | Shown for | Contents |
|---|---|---|
| **POINT** | One selected point of a lane (C4) | Time, Value (a field to drag or type), Bend |
| **POINTS** | Several points | Selected: how many |
| **LANE** | A lane (C4) | Name (a field), Points, Value now, whether it plays |
| **ASSIGNMENTS** | A lane (C4) | Each assignment: on switch, "Track › Parameter" (shows its module), remove; its range as from and to fields in the parameter's units. Then "Assign parameter…". A lane shows no TRACK, MIX or SENDS: it belongs to no track |
| **TAKE** | A take (C5b), above its track's sections | Name (a field), Start, Length, In use (all of it, partly, no), "Use this take" |
| **CLIP** | One clip | Name, Start (bars.beats.16ths), Length, Offset; Loop, and while it loops Loop start and Loop length (M3 C10, [clip-loops.md](clip-loops.md) §7) |
| **CLIPS** | Several clips | Selected: how many |
| **SECTION** | One section (M3 C3, ADR 0049), above MASTER and MIX | Name (a field), Colour, Start and Length (fields: they move its marks; the music stays) |
| **SECTIONS** | Several sections | Selected: how many |
| **TRACK** | A track (also under TAKE, CLIP or CLIPS) | Name, Instrument, Clips |
| **RETURN** | A return track | Name, Modules |
| **MASTER** | The master | Modules |
| **MIX** | Any channel | Volume and pan bars with the meter beside them; mute and solo (not on the master) |
| **SENDS** | A track | One bar per return track with its Pre switch, or "Add return track" |

So a selected clip reads top to bottom as CLIP, TRACK, MIX, SENDS; a track as TRACK, MIX, SENDS; a return as RETURN, MIX; the master as MASTER, MIX. With nothing selected: "Nothing selected".

**Mixer view:** no inspector. Every strip already shows its channel's mix and sends, so the inspector would only repeat them; the strips get its width instead. Ctrl+Alt+I and the transport bar's inspector button are disabled there. Back in the arrangement, the inspector is open or closed as it was; the saved `inspector` view state is the arrangement's.

**Later sections** (each comes with its feature): the track's input, output and recording settings (C5), automation (C4), a module's or a note range's properties (M3).

## 6. Keys and the area used last

Delete, Duplicate and Select all act on the area used last: notes in the clip view, a module in the chain, otherwise clips ([daw-shell.md](daw-shell.md)). Selected sections (M3 C3): Delete deletes them with their music, Duplicate copies them after themselves, F2 renames the first ([arranger.md](arranger.md) §12). A track's or return's header clicked last (it alone selected): Delete deletes it and Duplicate duplicates it (C11, [tracks-and-clips.md](tracks-and-clips.md) §5, §7). Cut, Copy and Paste follow the same rule: notes, an automation lane's points, otherwise clips (§9). In the mixer view, actions on arrangement content you can't see are off:

| Off in the mixer view | Still on |
|---|---|
| Delete, Duplicate, Select all on clips; Split; New MIDI clip | Delete on a module in the chain (when the chain was used last) |
| Tab (Modules ⇄ Clip) | Alt+M, Alt+S on the selected channel |
| Show inspector (Ctrl+Alt+I) | Show browser, Show detail panel |
| Zoom in, out, to fit; Show returns; the Song menu's commands on sections | Undo, redo, transport, every track action |

Disabled actions are greyed in the command palette and the transport bar, and their keys do nothing. Tab never moves keyboard focus away from the strips.

## 7. Tests

- **The rules** are a pure function, `model::panelContext(selection, snapshot, view)` in `app/src/model/Selection`: the subject, the channel, the clip, whether the clip view is offered and opens, the live MIDI target and the inspector's sections. Its whole table is unit-tested (`[fast]`), with the helpers that keep the channel and the clips together (`withClips`, `pruned`).
- **The shell** is tested end to end in `AppSmokeTests`: a header click after a clip, return rows, the mixer following a module click and the arrow keys, the clip view coming back after a trip to the mixer, the actions that are off in the mixer view, and one notification per click.
- **By hand:** [manual-tests/M2.md](../manual-tests/M2.md), "Selection and context panels (A5)".

## 8. The inspector at compact widths, and a module's properties (M3 C6, for review)

**Status:** approved by the product owner (2026-10-15), every recommendation of IN1–IN7; built the same day and signed off by the product owner (2026-10-16). The NOTES section the plan also named here came with the piano roll (P4, [piano-roll.md](piano-roll.md) §9.6) and is built.

**What other DAWs do** (research of Live 12, Bitwig 5.3 and 6, Logic, Studio One / Studio Pro, Cubase, FL Studio and Reaper, 2026-10-15):
- **Logic, Bitwig, Studio Pro and Cubase** dock the inspector in a side column. One key toggles it (`I`, F4) and a button does too. None of their manuals describes it floating over the content or hiding itself at narrow widths: below a size, panels push the content, or swap with the browser (Bitwig's smaller display profiles, Logic on iPad).
- **Cubase's Info Line** is a strip above the editor holding the selected notes' fields. It is cheap on a laptop, but it is a second home for the same properties.
- **Only Bitwig has a device inspector:** name, Active, its modulation sources and routings, voices, and for a plugin, Suspend. Elsewhere a device's facts are in its header, its window or the FX chain (Reaper shows a chain's latency in its corner).

### 8.1 Over the arrangement, below 1440 px

Today the inspector closes below 1440 logical px (daw-shell.md, "Adaptive"), and opening it docks it again, taking 260 px from the arrangement. Below 1440 px it now opens **over** the arrangement instead:
- **Where:** along the arrangement's right edge, as wide as it is docked (260 px, or as you've sized it; its edge still resizes it), from under the top bar down to the detail panel. It casts the popovers' shadow (`shadow`) on what's under it. The arrangement keeps its width and scrolls under it.
- **Opened and closed as now:** the top bar's inspector button, Ctrl+Alt+I, View > Show inspector, and the commands that need it. It slides in from the right with the panels' 150 ms ease-out (instant with "reduce motion").
- **It stays open while you work beside it** (IN2): clicking clips, notes, tracks or modules updates it, as when it's docked. It closes by its button, Ctrl+Alt+I, or Escape while it has the keyboard focus.
- **The view knows what it covers** (IN3): follow-playhead pages turn before the playhead goes under it, and Zoom to fit fits the song into what's left visible. Clips under it can still be reached by scrolling.
- **One state** (one place for view state): the inspector is open or closed (`Shell::Panel`). Docked or over the arrangement isn't a state of its own: it follows the window's width. Crossing 1440 px keeps it open or closed as it was, and docks it or lifts it over.
- **The mixer view** still has no inspector.

Why not keep docking at compact widths: at 1280 px a docked inspector leaves the arrangement about 970 px beside the browser's rail. Over the arrangement, the inspector costs nothing until you ask for it, and gives the room back when you close it. It covers the arrangement's right end while it's open; IN3 keeps the playhead out from under it.

### 8.2 MODULE: the module selected in the chain

While the module chain was used last ([§6](#6-keys-and-the-area-used-last)) and a module is selected in it, the inspector shows **MODULE** for that module, above its channel's sections (TRACK, MIX, SENDS). Only what its panel's header doesn't already show (IN5):

| Row | Shows |
|---|---|
| **Type** | The module's type, and for a plugin its maker and format: "Filter", "Diva · u-he · VST3" |
| **Delay** | What it adds to the track's delay, which the DAW compensates: "None", "4 samples · 0.1 ms", "+107 ms" (the latency badge's figure, for every module) |
| **Moved by** | The modulators that move it, one line each, with what they move: "LFO · Freq, Mix". A click on a line selects that modulator |
| **Automated** | Its parameters with automation lanes, one line each, with **Show** (the lane, as a control's right-click shows it) |

- Rows show only where they apply, as the export panel's do: no Moved by without modulators, no Automated without lanes, Delay always.
- **A modulator** shows **ROUTES** under MODULE instead of Moved by: a row for each route, as the route list has them (modulators.md §7.4): the target, its amount bar, Show and Remove. Its amounts are set there too, each drag one undo step (IN6).
- **A rack** shows Type ("Rack · 3 chains") and Delay (its largest chain's).
- The preset, power, sidechain and Fold stay in the module's header, where they are; the inspector doesn't repeat them.

### 8.3 Assistants

Nothing new: what MODULE shows is already in `get_module` (its type and the delay it adds, `latencyMs`), `get_automation` and `get_modulators` (routes). A plugin's maker and format join `get_module`'s type.

### 8.4 Built (2026-10-15)

- **Over the arrangement:** below 1440 px the inspector lies over the arrangement's right end, its shadow a component of its own between them (`ui::paintShadow`, the popovers' shadow alone). The arrangement keeps its width; `ArrangementView::setCoveredRight` keeps follow-playhead's page turns and Zoom to fit clear of it. Its splitter stays at its left edge. Escape closes it while it has the focus. While Settings show in the arrangement's place, it stays out of their way.
- **MODULE** comes from a pure function (`model::moduleFacts`, with its tests): what moves the module (modulators in racks too) and its lanes. The shell checks each frame whether the module selected in the chain (the chain used last) or the project changed. Type and Delay come from the engine (`Session::moduleType`, `moduleLatencyMs`). Delay reads in milliseconds ("0.1 ms", "107 ms"): the inspector doesn't know the rate a module runs at.
- **Moved by** lines are buttons (the modulator's name and what it moves, cut to fit); **Automated** lines name the parameter, with Show.
- **ROUTES** are the route list's own rows (`RouteRows`, which the route list now holds), two lines a route in the inspector's width. An amount's drag isn't cut short as the snapshot follows: the rows are kept while their targets are the same.
- **Assistants:** `get_module` gains `plugin`, a plugin's name, maker and format.

### 8.5 Questions for review

| # | Question | Recommendation |
|---|---|---|
| IN1 | **Below 1440 px** | The inspector opens over the arrangement's right edge (as planned in M3), same width, with the popovers' shadow; the arrangement keeps its width. Docking at compact widths takes ~20 % of a laptop's arrangement for as long as it's open |
| IN2 | **Closing it** | Its button, Ctrl+Alt+I, or Escape while it has focus. A click elsewhere doesn't close it: clicks beside it are how you choose what it shows |
| IN3 | **What it covers** | The arrangement counts the covered part as out of view: follow-playhead turns pages before it, Zoom to fit fits what's left |
| IN4 | **One state** | Open or closed is kept once; docked or over follows the width. Crossing 1440 px keeps it open or closed |
| IN5 | **MODULE** | Type (maker and format for plugins), Delay, Moved by, Automated. Nothing the header shows (preset, power, sidechain) |
| IN6 | **A modulator's ROUTES** | Its routes in the inspector, as the route list has them, editable there; the route list stays as the handle's right-click |
| IN7 | **A strip above the editor** (Cubase's Info Line) | Not now: the inspector, over the arrangement at compact widths, is the one home for properties |
