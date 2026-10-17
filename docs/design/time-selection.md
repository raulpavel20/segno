# Time selection: a stretch of time, selected and edited

A **time selection** is a stretch of time on chosen rows. Drag across bars 9 to 17 over the drums and the bass, and those eight bars are selected on those rows, whatever clips lie there. Commands then act on exactly that stretch. Delete clears it. Ctrl+D repeats it right after itself. Ctrl+E splits the clips at its edges. Ctrl+L loops it. Export renders it. Song-wide versions insert or remove time across the whole song. The same idea works inside a clip: the audio editor already selects time this way, and the piano roll gains it.

**Status:** approved by the product owner (2026-10-17), every recommendation of T1–T24 (§23), with [ADR 0073](../decisions/0073-a-time-selection-is-a-stretch-of-time-on-chosen-rows.md) accepted. Assistants keep naming the stretch: the product owner kept the selection out of their reach (selection.md §1), "fine as it is". Step 0 (drawn, and the one check) is next. Groups ([groups.md](groups.md), approved) are built after it, and their rows use it (§15).

What the product owner asked (2026-10-17), on reviewing the groups design:
- "I think we need to implement selecting a stretch of time."
- "Let's first write the plan and spec for this feature, selecting a stretch of time everywhere it makes sense and the user would expect. And after we implement that, we will come back to [groups] and implement it."

## 1. What's there now

**What you can select today:**
- **Whole clips** ([selection.md](selection.md) §2): a click, Shift or Ctrl+click, or a box dragged on empty lanes, which selects every clip it touches.
- **Points on a lane** by a box of time and value ([automation.md](automation.md) §5). The clip view's controller lanes work the same way, through one shared piece (`LaneGesture`).
- **Notes** in the piano roll, by a box of time and pitch. Selected notes get time handles in the ruler that stretch them ([piano-roll.md](piano-roll.md) §9.2).
- **A range in the audio editor:** a drag across the waveform ([audio-editor.md](audio-editor.md) §5). Split, Delete, Crop to range, Silence, Fade to range, Loop range and a gain handle act on it. Its time handles stretch the audio.
- **Sections on the song row** ([arranger.md](arranger.md)). Moving, copying, inserting and deleting a section takes everything in its time with it, on every track and lane:
  - clips crossing an edge are split there;
  - automation keeps its value at the edges;
  - the rest of the song makes room or closes up.

  That machinery is general (`model::planMove`, `planCopy`, `planDelete`, `planInsert`, and pasting a `SongPiece`) and works on any stretch.

**What has waited for this,** each written as "comes with range selection":
- [ADR 0028](../decisions/0028-automation-lanes.md): "range editing (a time range across clips and lanes) … moving automation with clips … come later";
- open question B30: automation moving with clips, "an option when range editing comes";
- [export.md](export.md) §3: "a selected time range comes with range selection";
- [bounce.md](bounce.md) §2: "time ranges across clips and gaps come with range selection";
- [fades.md](fades.md): "fades on a time range across clips (one gesture fading several)";
- [arranger.md](arranger.md) A11 and §16: Select clips in section "bridges to clip commands until range selection comes", and a section's time to bounce or export;
- [groups.md](groups.md) §6.5: editing on a group's row.

**The arrangement's gestures today** ([daw-shell.md](daw-shell.md), "Clip editing"):
- a clip's body or title strip: a click selects the clip, a drag moves it, Ctrl+drag copies it;
- its edges trim; its handles fade, set gain or loop; a line between two takes moves the handover;
- a drag on an empty lane box-selects clips;
- a click in the ruler moves the playhead, and a drag there zooms and scrolls;
- a drag across empty time in the song row's title band makes a section;
- the playhead moves only by the ruler, the transport and the keys, never by selecting.

## 2. How others do it (October 2026)

| | Called | Made with | Across tracks | A drag in the ruler | Beside a clip selection | Automation follows by |
|---|---|---|---|---|---|---|
| **Live 12.4** | Time selection | No tool. A drag on the background or a clip's body selects time; "only the clip bar is draggable" | Drag down; Shift+click extends; Ctrl/Cmd+click toggles a track or lane inside it (12.4) | Scrolls and zooms; no selection | A selected clip acts as time over its span | Lock Envelopes off; lanes in the selection |
| **Bitwig 6.1** | Time selection vs object selection | Tool 2 (Time Selection), or the Pointer below a clip's header if a preference is on | Drag; Shift+↑/↓ adds tracks; Shift+Ctrl+↑/↓ all | With tool 2, all tracks (users report) | Two modes; Ctrl+T converts | A per-lane setting and a global one (key 0) |
| **Logic 12** | Marquee selection | The Marquee tool, the Pointer's lower half (a preference), or the marquee ruler | Drag; Shift+click a lane adds or removes it | The marquee ruler selects all tracks | Separate | Two points at each edge; "Move Track Automation with Regions" |
| **Studio Pro 8.1** | Range | The Range tool (2), or the upper half of an event with Link on | Drag; Shift adds more ranges, apart from each other | Sets the loop | Ranges and whole events together | "Automation follows events" (on) |
| **Cubase 15** | Selection range | The Range tool (2), or the upper area with Combine Selection Tools | Drag; Ctrl+click adds or removes a track; one range at a time | Sets the locators | Separate | "Automation Follows Events" |
| **Pro Tools 2026** | Edit selection | The Selector, or the Smart Tool's upper half | Drag; Shift+click tracks | Selects all tracks | Time Grabber or Object Grabber | "Automation Follows Edit"; edge points added |
| **REAPER 7.8** | Time selection; razor areas | A drag in empty track space (all tracks); razor areas per track with Alt+right-drag | Time selection is always all tracks; razor areas skip tracks | Sets the loop | Independent; razor areas take priority | "Move envelope points with media items" |

| Command | Live (Windows keys) | Elsewhere |
|---|---|---|
| Cut, Copy, Paste, Delete, Duplicate | Ctrl+X, C, V, Delete, Ctrl+D. Paste overwrites; the gap stays | The same in all: plain commands leave the gap |
| Split at the edges | Ctrl+E | Bitwig and Pro Tools Ctrl+E; Logic Opt-Cmd-T; Cubase Shift+X |
| Crop | Crop Selected Clips, Ctrl+Shift+J | Logic Cmd-\; Cubase Edit > Range > Crop; Pro Tools Trim to Selection |
| Deactivate (mute) | 0 | Cubase mutes a range |
| Fade or crossfade | Create Fade/Crossfade, Ctrl+Alt+F | Everywhere, from the range |
| Insert time | Insert Silence, Ctrl+I | Everywhere: Studio Pro Ctrl+Alt+I, Cubase and Pro Tools Ctrl+Shift+E, Logic between locators |
| Remove time, closing the gap | Delete Time Ctrl+Shift+Delete; Cut Time Ctrl+Shift+X | Cubase Shift+Backspace and Ctrl+Shift+X; Studio Pro Ctrl+Alt+D; Logic Cut Section Between Locators |
| Copy, paste and duplicate time | Copy Time Ctrl+Shift+C (new in 12.4, asked for since 2016); Paste Time Ctrl+Shift+V; Duplicate Time Ctrl+Shift+D | Cubase Paste Time Ctrl+Shift+V; Studio Pro Duplicate and Insert Alt+D |
| Loop the selection | Loop Selection, Ctrl+L | Bitwig Ctrl+L; Cubase and Studio Pro P; Logic Cmd-U |
| Zoom to it | Z (X goes back) | Every DAW that documents it has one key |
| Bounce it | Bounce to New Track, Ctrl+B; Paste Bounced Audio | Most render a range |
| Export it | Render Start and Length taken from it | Bitwig uses it by default; REAPER, Pro Tools and FL offer it |
| A new MIDI clip over it | Insert Empty MIDI Clip(s), Ctrl+Shift+M | REAPER |
| Punch | Through the loop | Logic, Cubase, Pro Tools and REAPER punch on the selection |

- **In Live's clip editors:** a drag in the MIDI editor's background selects a timespan *and* the notes inside. Duplicate Time, Delete Time and Insert Time work inside the clip, and Ctrl+E splits notes at the edges. In the sample editor, a selection crops the sample, warps or places markers. A time selection made on a clip in the arrangement zooms the clip editor to it.
- **"…Time" commands are song-wide in Live:** they "act upon all tracks", time-signature markers included. In Cubase they act only on the tracks in the range (users report). Studio Pro moves tempo and markers only when the range spans every track.

**Most agree on:**
- **A time selection is a stretch of time across a set of tracks.** It's made by dragging down across them and adjusted with Shift. Live, Logic and Cubase let the tracks skip some.
- **Plain Cut and Delete leave the gap.** Closing a gap or making room is a separately named command, and Insert Silence exists nearly everywhere.
- **Split, loop and zoom** each take one key.
- **Export and bounce** take the selection.
- **The two kinds of selection are told apart by where the mouse is:** a clip's header against its body, or its upper half against its lower.
- **Automation in the range gets points at the edges** so its shape outside stays right.
- **Whether automation moves with an edit is a global switch** in every DAW (Lock Envelopes, Automation Follows Events, and the like).

**Where they differ:**
- **Tool or place.** Bitwig, Cubase, Studio Pro and Pro Tools have a tool, usually key 2, and a combined mode as an option. Live has no tool: where a drag starts decides. Users report that Bitwig's old hover zones confused people, and its 6.0 made them a preference.
- **Which half means time:** the body or lower half in Live, Bitwig and Logic; the upper half in Pro Tools, Cubase and Studio Pro.
- **The ruler:** it selects time on every track in Logic, Pro Tools and FL. It sets the loop in Cubase, Studio Pro and REAPER. In Live it only scrolls and zooms.
- **Numbers:** Cubase, Pro Tools and REAPER show the selection's start, end and length, editable. Live shows lengths only in its status bar.

Sources:
- **Live:** [arrangement view](https://www.ableton.com/en/live-manual/12/arrangement-view/), [shortcuts](https://www.ableton.com/en/live-manual/12/live-keyboard-shortcuts/), [automation](https://www.ableton.com/en/live-manual/12/automation/), [editing MIDI](https://www.ableton.com/en/live-manual/12/editing-midi/), [bounce](https://www.ableton.com/en/live-manual/12/bounce-to-audio/), [export](https://www.ableton.com/en/live-manual/12/managing-files-and-sets/), [Live 12 release notes](https://www.ableton.com/en/release-notes/live-12/), [Live 10 release notes](https://www.ableton.com/en/release-notes/live-10/).
- **Bitwig:** [6.0 release notes](https://downloads.bitwig.com/6.0/Release-Notes-6.0.html), [5.2 release notes](https://downloads.bitwig.com/5.2/Release-Notes-5.2.html), the [user guide](https://www.bitwig.com/userguide/latest/) (5.3).
- **Logic:** [select parts of regions](https://support.apple.com/guide/logicpro/select-parts-of-regions-lgcpf7c0ae21/mac), [add or remove gaps](https://support.apple.com/guide/logicpro/add-or-remove-gaps-lgcp21e2cb21/mac).
- **Studio Pro:** [mouse tools](https://fenderstudiopromanual.fender.com/en/Content/Editing_Topics/Arrange_View_Mouse_Tools.htm), [adding time](https://fenderstudiopromanual.fender.com/en/Content/Arranging_Topics/Adding_Time_to_the_Arrangement.htm).
- **Cubase:** [range editing](https://www.steinberg.help/r/cubase-pro/15.0/en/cubase_nuendo/topics/range_editing/range_editing_c.html), [combined tools](https://www.steinberg.help/r/cubase-pro/15.0/en/cubase_nuendo/topics/parts_events/parts_and_events_combine_selection_tools_mode_c.html).
- **Pro Tools:** [reference guide 2026.4](https://resources.avid.com/SupportFiles/PT/Pro_Tools_Reference_Guide_2026.4.pdf).
- **REAPER:** [user guide 7.81](https://www.reaper.fm/userguide/ReaperUserGuide781c.pdf), [what's new](https://www.reaper.fm/whatsnew.txt).

**What we take from Live:**
- **No tool.** Where a drag starts decides what it does (§5), as everywhere else in Segno, which has no tool palette ([audio-editor.md](audio-editor.md) principle 5).
- **The title strip moves a clip; its body selects time.** This matches the audio editor, where a drag across the waveform already selects a range.
- **A selection across rows,** adjusted with Shift and Ctrl.
- **Plain commands that leave the gap,** and song-wide "… time" commands with Live's names and keys.
- **Ctrl+L loops the selection.**
- **Export and bounce from the selection.**
- **In the piano roll:** the drag selects the notes and the stretch at once.
- **A selection made in the arrangement shows in the clip view.**

**Beyond Live:**
- **No global switch for automation.** Segno's lanes are rows of their own ([ADR 0029](../decisions/0029-independent-automation-lanes.md)), so a lane moves when it's in the selection, and you can see that it is (§11).
- **Its start, end and length in the inspector,** typed or dragged (§7). Live shows lengths only in its status bar.
- **One machine** for the song-wide commands and the sections' (§9). Section marks move with time too.

**Left out:**
- **Tools** (Bitwig, Cubase, Studio Pro, Pro Tools).
- **Several separate stretches** (Studio Pro's Shift-added ranges, REAPER's razor areas). One stretch on chosen rows covers what's asked, and keeps one meaning for every command.
- **Selecting from the ruler** (Logic, Pro Tools): Segno's ruler zooms and scrolls, as Live's does. The song row selects across the whole song instead (§5).

## 3. Principles

1. **One stretch of time on chosen rows.** A time selection is a start, an end and a set of rows. Commands act on what's in it, on those rows, cutting at its edges.
2. **Where a drag starts decides.** No tools and no modes. A clip's title strip moves the clip, its body selects time, and empty space selects time. That's Live's split, and it matches the audio editor's.
3. **Plain commands leave the gap; "… time" commands move the song.** Delete clears a stretch; Delete time removes it and closes up. The second kind is always song-wide and lives in the Song menu, beside the sections.
4. **What moves is what's selected.** Automation moves when its lane is in the selection, and stays when it isn't. No hidden switch decides.
5. **One meaning everywhere.** The arrangement, the piano roll and the audio editor select time with the same gesture and show it the same way, and their commands share names and keys.
6. **Everything by mouse, nothing modal** ([ADR 0040](../decisions/0040-a-menu-bar-and-everything-by-mouse.md)). There's a right-click menu on the selection, every command is in the menu bar, and the properties are in the inspector. Assistants get the same ([ADR 0021](../decisions/0021-mcp-server.md)).

## 4. What a time selection is

- **A start and an end** on the song's timeline, snapped to the grid (Alt: free), and **a set of rows**: tracks and automation lanes, next to each other or not.
- **What's in it** is everything on those rows between its start and end: the parts of clips, with their takes, fades and loops, and the points of lanes.
- **One selection at a time** ([ADR 0027](../decisions/0027-selection-and-context-panels.md)). A time selection replaces selected clips, points, takes or sections, and they replace it.
- **Its channel** is that of the row where it was started: a track, or a lane's first assignment. The detail panel shows that channel's modules, unless the selection lies in one clip (§14.3).
- **View state, as every selection is:** it's never saved and never an undo step. After a command it follows the result (§8).
- **Rows, with or without time.** The selection's set of rows is the one the groups design selects tracks with ([groups.md](groups.md) §8): several selected tracks are rows without a stretch, and a time selection is rows with one. It's one set, kept once.

## 5. Making one

### 5.1 In the arrangement

| Where the press is | A click | A drag |
|---|---|---|
| **A clip's title strip** | Selects the clip (Shift or Ctrl adds or removes it) | **Moves the clip** (Ctrl copies it). Inside a time selection, it moves the selected stretch instead (§10) |
| **A clip's body** | Selects the clip | **Selects time** from where it was pressed |
| **A clip's edges, fade and gain handles, loop corner, or a line between two takes** | As now | As now |
| **Empty space in a track's row** | Selects the track, as now | **Selects time** (in place of today's box of clips) |
| **An automation lane, away from points** | Selects the lane, as now | **Selects time**; the lane's points inside it are selected with it (§11) |
| **A point** | As now | As now |
| **The song row's overview** (under its title band) | Selects the master, as now | **Selects time on every track and lane** |
| **The song row's title band** | As now | A new section, as now |
| **A take's strip** | As now (comping) | As now: a drag uses that take there |
| **The ruler** | Moves the playhead | Zooms and scrolls, as now |

- **A drag covers the rows it crosses:** every track and lane between where it was pressed and the pointer. Return tracks hold no clips and aren't rows of a time selection.
- **Moving clips needs the title strip.** It's the clip's 15 px band, or 35 % of a short clip, as Live's clip bar is. Today a drag on the body moves the clip too. This is the main change to the gestures you know (§16).
- **A drag shorter than the drag threshold is a click.**

### 5.2 In the clip view

- **The audio editor:** a drag across the waveform selects time, as it already does (§14.2).
- **The piano roll:** a drag on empty space selects the notes in its box, as now. Its stretch also becomes the clip view's time selection (§14.1).

## 6. Changing it

- **Shift+click** extends or shortens it to the click, on its rows and to the row clicked, as in Live. **Shift+drag** does the same while dragging.
- **Ctrl+click a row inside its stretch,** or a row's header, adds that row or takes it out. This is Live 12.4's toggle, and it allows rows apart from each other.
- **Shift and Ctrl change whichever selection there is.** With a time selection, Shift+click and Ctrl+click act on it as above, even on a clip. With clips selected, they add or remove clips, as now.
- **Ctrl+A** with a time selection widens it to every track and lane, keeping its stretch. With none, Ctrl+A selects every clip, as now.
- **Shift+← and Shift+→** move its end by a grid step.
- **The inspector's Start, End and Length** (§7) move its edges: typed, or dragged as numbers are.
- **Edit ▸ Select loop** selects the loop brace's stretch on every track and lane (Live's Select Loop Brace Contents).
- **Song ▸ Select section's time** selects a section's stretch on every track and lane. It takes the place of Select clips in section, which was the bridge until this came ([arranger.md](arranger.md) A11).
- **Clearing it:** a click anywhere else, Escape (when no bounce is running), or Edit ▸ Select none.
- **There are no handles on its edges.** A drag there would fight the clips' trim edges under it, and Live has none either. Shift and the inspector do the job.

## 7. How it shows

```
┌ ruler ────┬───────────────────────── 9 ──────────────────────────── 17 ──────────────┐
│           │                         ▕▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▏              │ ← its stretch, a band in the ruler
├ Drums    ▮│█████████████████████████▕▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▏██████████████│ ← a selected row: the fill over what's there
├ Bass     ▮│█████        ████████████▕▓▓▓▓▓        ▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓  ▏     █████████│
├ Keys     ▮│██████████████████████████████████████████████████████████████████████████│ ← not in it: no fill
├ Cutoff    │──────────────────────╱‾‾▕‾‾‾‾‾‾‾‾‾‾╲______________________▏______________│ ← a lane in it: its points selected
└───────────┴──────────────────────────────────────────────────────────────────────────┘
```

- **On each of its rows,** the stretch is filled in `selectionFill`, with a `selectionStroke` line at each edge. It's the look of the audio editor's range, so it reads the same everywhere. Rows left out have no fill, even between rows that are in.
- **In the ruler,** a band in `selectionFill` spans it. It has no handles: in the audio editor the ruler's handles stretch audio, and here nothing stretches (§20).
- **Clips stay as they are under the fill.** A clip wholly inside isn't drawn "selected": what's selected is the time.
- **The inspector's TIME section:**
  - **Start, End and Length,** in bars.beats.16ths, as fields to drag or type. They move the selection, never the music;
  - **Rows:** "2 tracks, 1 lane".

  Below TIME come its channel's sections, as below a clip's CLIP: TRACK, MIX, SENDS.
- **The song row's overview** tints its span when every row is in the selection.

## 8. What acts on it: the stretch on its rows

Each command is one undo step, named for itself, and acts only on the selection's rows. The rest of the song stays where it is.

| Command | Key | Does |
|---|---|---|
| **Delete** | Delete | Clears the stretch. Clips crossing its edges are split there and the parts inside go. Points inside go, and a point at each edge keeps the curve outside as it was. The gap stays |
| **Cut, Copy** | Ctrl+X, Ctrl+C | Puts the stretch on the clipboard as it is: clip parts with their takes and fades, lane curves. Cut also clears it |
| **Paste** | Ctrl+V | A copied stretch lands at the playhead, on the selection's rows, or from the selected track down: tracks onto tracks and lanes onto lanes, in order. It overwrites what it lands on, as placed clips do ([ADR 0019](../decisions/0019-one-kind-of-track.md)). It's then the selection. Clips, notes and points on the clipboard paste as now |
| **Duplicate** | Ctrl+D | A copy right after the selection's end, on its rows, overwriting. The selection moves onto the copy, so Ctrl+D again goes on |
| **Split at selection** | Ctrl+E | Splits the clips on its rows at its edges. The parts inside become the selected clips. Without a selection, it splits at the playhead, as now |
| **Crop to selection** | Ctrl+Shift+J | The clips crossing it lose what's outside it (Live's Crop Selected Clips) |
| **Mute** | 0 | Splits at its edges and mutes the parts inside (Live's Deactivate) |
| **Reverse** | (Audio ▸ Reverse) | Splits at its edges and reverses the audio parts inside |
| **Fade to selection** | Ctrl+Shift+X | On each of its tracks, where it lies: from a clip's start, a fade-in to its end; to a clip's end, a fade-out from its start; across the seam of two clips, a crossfade as long as it. It's Crossfade's key, and the item's label says which it does |
| **Bounce in place, Bounce to new track** | Ctrl+B, Ctrl+Shift+B | §13 |
| **New MIDI clip** | Ctrl+Shift+M | An empty MIDI clip over the stretch on each selected track |
| **Loop selection** | Ctrl+L | §12 |
| **Zoom to selection** | Ctrl+0 | §12 |

- **After a command,** the selection follows the result:
  - Delete leaves the empty stretch selected;
  - Duplicate and Paste select what they made;
  - Split selects the parts inside, as clips.
- **A command that can't act** is dimmed, saying why when its key is pressed anyway. For example, Fade to selection when no clip edge or seam is in it.
- **The right-click menu on the selection** lists Cut, Copy, Paste, Delete, Duplicate, Split, Crop, Fade, Mute, Bounce in place, Bounce to new track, New MIDI clip, Loop selection, Zoom to selection, Add section, and Export selection…. That's the in-place route; the menu bar has them all too (§17).

## 9. Song-wide: inserting and removing time

These act on **every track and lane, and the song's section marks,** whatever rows the selection has. They use the selection's stretch only. They're the sections' commands for any stretch, run by the same plans (`model::planInsert`, `planDelete`, `planCopy`, pasting a `SongPiece`). In the **Song** menu, with Live's names and keys:

| Command | Key | The song (B selected) |
|---|---|---|
| **Insert time** | Ctrl+I | A B C → A ▢ B C: empty time as long as the selection, at its start; everything after moves later |
| **Delete time** | Ctrl+Shift+Delete | A B C → A C: the stretch gone from every row, the gap closing |
| **Cut time** | – | Copy time, then Delete time |
| **Copy time** | Ctrl+Shift+C | The stretch on every track and lane to the clipboard, with the section marks wholly inside it |
| **Paste time** | Ctrl+Shift+V | A copied stretch at the playhead; the music after it moves later |
| **Duplicate time** | Ctrl+Shift+D | A B C → A B B C: a copy right after the selection, the rest moving later |

- **Always the whole song.** Live's "… Time" commands always act on every track. Cubase's act on the tracks in the range, and users trip on its tempo track. Studio Pro moves tempo and markers only when the range covers every track. Song-wide here means nothing is half-moved.
- **At the edges,** as for sections ([arranger.md](arranger.md) §9): clips crossing an edge are split, and automation keeps its value there.
- **Section marks move with time:**
  - a mark inside a deleted stretch goes;
  - a section across a deleted stretch shrinks;
  - time inserted inside a section lengthens it;
  - sections wholly inside a copied stretch come along, named as section copies are ("Chorus 2").
- **Tempo and time-signature changes,** once the song has them, move with time as sections do ([arranger.md](arranger.md) §16).
- **Without a selection** they're dimmed: "Select a stretch of time first". Insert time takes its length from the selection, as Live's and Cubase's do.
- **Cut time has no key:** Live's Ctrl+Shift+X is Crossfade here, and Crossfade is used far more often.

## 10. Moving and copying a stretch by dragging

- **Drag a clip's title strip inside the selection,** and the stretch moves: everything in it, on its rows, split at its edges. Lanes in the selection move along, so a section of a song keeps its automation.
- **It lands where it's dropped:** snapped to the grid (Alt: free), and down or up by whole rows, tracks onto tracks and lanes onto lanes. It overwrites what it lands on, and the place it left stays empty.
- **Ctrl+drag copies it.**
- **While dragging,** the stretch is drawn at its new place. Escape cancels.
- **One undo step,** "Move" or "Copy".
- **← and →** move the stretch's content by one grid step, as they move notes, points and sections. This is Live's default.
- A title strip **outside** the selection still moves its own clip, and the time selection clears.

## 11. Automation

- **A lane is in the selection when you put it there:** by dragging across it, Ctrl+clicking it, or Ctrl+A. Its points inside the stretch are cut, copied, duplicated, moved and pasted with the clips.
- **A lane that isn't in the selection stays where it is.** That answers B30: automation moves with clips when you select it with them. There's no global switch like Live's Lock Envelopes, Cubase's Automation Follows Events or Pro Tools' Automation Follows Edit. The fill shows which lanes are in.
- **Moving whole clips** (selected as clips, not as time) leaves automation where it is on the timeline, as now.
- **At the edges:** a point is added at each edge, at the curve's value there, before anything is cut or moved. So the curve outside keeps its shape, and inside the stretch keeps its own (Logic and Pro Tools do the same).
- **A drag in a lane** selects time. Its points inside are selected with it, so dragging one moves them all, and ↑ and ↓ nudge their values. That replaces the lane's box of points by time and value. Selecting a few points stays a click, Shift or Ctrl+click.
- **The clip view's controller lanes** edit the same way, through the shared `LaneGesture` (§14.1).
- **The song-wide commands** (§9) move every lane, as sections do.

## 12. Playing it

- **The playhead:** when stopped, a new time selection puts the playhead at its start, so Space plays it. While playing, it doesn't move the playhead. Live's insert marker, Logic's marquee and Bitwig's default (Time selections move the playhead) all do this.
- **Loop selection (Ctrl+L):** with a selection, the loop brace takes its stretch and looping turns on (Live, Bitwig). Without one, Ctrl+L toggles the loop, as now.
  - The item reads "Loop selection" while there's a selection.
  - Punching is the loop ([recording.md](recording.md), R8), so recording into a selection is Ctrl+L, then Record.
- **Zoom to selection (Ctrl+0):** View ▸ Zoom to fit fits the selection while there is one, and reads "Zoom to selection".

## 13. What has waited for it

- **Export** ([export.md](export.md) §3): the Range row gains **Selection**: exactly the stretch, with no tail, as Loop has none.
  - It's offered while there's a selection. It's chosen when the panel opens with one, as Live and Bitwig do.
  - If the remembered range was Selection and there's none now, the range is Song.
  - The mix is the whole mix, whatever the rows. With stems, the selection's tracks are the "selected tracks".
- **Bounce** ([bounce.md](bounce.md)) acts on each selected track's stretch:
  - **Bounce to new track:** one clip per track, spanning the stretch, on the new track, with the stretch's source parts muted (split at the edges first). Live 12.2 does the same.
  - **Bounce in place:** the stretch becomes one clip spanning it, and the parts it replaces become its "Before bounce" take, as a comp's parts do ([bounce.md](bounce.md) §4).
  - That a take can hold several parts with gaps is checked first, in step 0 (§21).
- **Fades across clips** ([fades.md](fades.md)): Fade to selection (§8) fades or crossfades every clip edge and seam it covers, on every selected track, in one step.
- **Sections:** Song ▸ Add section makes a section from the selection, when there's one (before the loop and the playhead). Song ▸ Select section's time selects a section's stretch (§6).
- **Groups** (§15).

## 14. In the clip view

### 14.1 The piano roll

- **A drag on empty space** selects the notes in its box, as now, and its stretch becomes the clip view's time selection. A drag with no notes inside selects time alone. This is Live's MIDI editor.
- **How it shows:** a light `selectionFill` across every row of the roll in the stretch. The selected notes' time handles stay in the ruler, as now. The stretch has no band of its own there, so the ruler holds one thing.
- **What uses it:**
  - **Ctrl+D** duplicates the selected notes right after the stretch's end, and moves the stretch along, as in the arrangement. Today it rounds the notes' span up to the grid.
  - **Ctrl+E** splits the notes crossing its edges (Live's).
  - **Insert time, Delete time, Duplicate time** (Ctrl+I, Ctrl+Shift+Delete, Ctrl+Shift+D) act inside the clip: the notes and controller points after the stretch move, and the clip grows or shrinks with them. These are the Song menu's keys, routed to the clip when the clip view was used last.
  - **Crop clip to selection** and **Clip loop to selection:** the clip's edges, or its loop, take the stretch.
  - **The Generate tools** use the stretch as their span, before the clip's loop.
- **Note commands** (quantise, transpose, the Transform tools, arrows, Delete) act on the selected notes, as now.
- **Controller lanes:** a drag selects their points in the stretch (§11).

### 14.2 The audio editor

- **Its range is the clip view's time selection:** the same drag across the waveform, and the same fill and edge lines. Its time handles keep stretching the audio, which is the audio editor's own job.
- **One vocabulary:**
  - Crop to range becomes **Crop to selection**;
  - Fade to range becomes **Fade to selection**;
  - Loop range becomes **Clip loop to selection**, so it isn't confused with Loop selection, which sets the loop brace.

  Split, Delete, Silence, Markers at transients and the gain handle are unchanged. `edit_audio_range` keeps its released name.

### 14.3 From the arrangement to the clip view

- **A time selection inside one clip,** on one track, shows that clip in the clip view with the same stretch selected there, scrolled into view. Live zooms its clip editor to such a selection.
- After that, the two are separate, as a panel's own selection always is ([ADR 0027](../decisions/0027-selection-and-context-panels.md)). A stretch selected in the clip view doesn't change the arrangement's.

## 15. Groups (with P9)

- **A time selection on a group's row covers the group and every row in it,** folded or not ([groups.md](groups.md) §6.5).
- Every command of §8 then acts on all of them: select bars 9 to 17 on a folded Drums group and Ctrl+D repeats them on every drum track.
- Selecting across a folded group includes its hidden rows.
- **The group's miniature** shows the stretch, as any row does.

## 16. What changes from today

| Today | With time selection |
|---|---|
| A drag on a clip's body moves the clip | It selects time. The title strip moves the clip, as Live's clip bar does |
| A drag on empty lanes box-selects every clip it touches | It selects that stretch. Inside it, a title-strip drag moves the stretch (§10); a click, Shift or Ctrl+click still selects whole clips |
| A drag in an automation lane boxes points by time and value | It selects time, and the lane's points in it (§11) |
| Delete on clips touched by a box deletes them whole | Delete on a time selection clears exactly the stretch |
| Ctrl+E splits at the playhead | With a selection, it splits at its edges |
| Ctrl+L toggles the loop | With a selection, it loops it |
| Ctrl+0 zooms to the song | With a selection, it zooms to it |
| Ctrl+Shift+X crossfades two selected clips | With a selection, it fades or crossfades to it |
| Ctrl+A selects every clip | With a selection, it widens it to every row |
| Song ▸ Select clips in section | Song ▸ Select section's time |
| The audio editor's Crop to range, Fade to range, Loop range | Crop to selection, Fade to selection, Clip loop to selection |
| The piano roll's Ctrl+D rounds the notes' span to the grid | It places the copy after the box's stretch |

Nothing changes in the mixer view.

## 17. Menu bar and keys ([ADR 0040](../decisions/0040-a-menu-bar-and-everything-by-mouse.md))

| Menu | Items (new or changed) | Keys |
|---|---|---|
| **Edit** | Cut, Copy, Paste, Delete, Duplicate, Split, Select all: on a time selection when it's what was used last (§8). New: **Select none**, **Select loop** | Escape for Select none (while no bounce runs) |
| **Clip** | New: **Crop to selection**. On a selection: New MIDI clip, Bounce in place, Bounce to new track, Mute, and Crossfade, which reads **Fade to selection** | Ctrl+Shift+J |
| **Audio** | Crop to selection, Fade to selection, Clip loop to selection (renamed); Reverse on a selection | |
| **Notes** | New: **Time ▸** Insert time, Delete time, Duplicate time; Crop clip to selection; Clip loop to selection | The Song menu's keys, shared (`Action::keysFrom`) |
| **Song** | New: **Insert time**, **Delete time**, **Cut time**, **Copy time**, **Paste time**, **Duplicate time**; Add section from the selection; **Select section's time** in place of Select clips in section | Ctrl+I, Ctrl+Shift+Delete, –, Ctrl+Shift+C, Ctrl+Shift+V, Ctrl+Shift+D |
| **Transport** | Loop, which reads **Loop selection** with one | Ctrl+L |
| **View** | Zoom to fit, which reads **Zoom to selection** with one | Ctrl+0 |
| **File** | Export…, whose Range offers Selection (§13) | |

- **Every new key is free today** and is Live's: Ctrl+I, Ctrl+Shift+Delete, Ctrl+Shift+C, Ctrl+Shift+V, Ctrl+Shift+D, Ctrl+Shift+J. The changed ones keep their keys and act on the selection when there is one, their labels saying so (`Action::label`).
- **The keys go to the area used last** ([selection.md](selection.md) §6). The arrangement with a time selection takes the time commands, and the clip view takes the clip's.
- **The right-click menu on a selection** is in §8.

## 18. Assistants (MCP, [ADR 0021](../decisions/0021-mcp-server.md))

Assistants never read the selection ([selection.md](selection.md) §1), so the tools name the stretch and its rows:

| Tool | What |
|---|---|
| `edit_range` | `from`, `to`, `rows` (track and lane ids, or `"all"`), `action`: delete, duplicate, split, crop, mute, reverse, fade, new_midi_clip. As §8, one undo step; returns what changed |
| `copy_range` | `from`, `to`, `rows`, `at`, `onto?` (the row the first lands on), `move?`. A paste that overwrites, or a move (§10) |
| `edit_time` | `action`: insert, delete, duplicate, copy; `from`, `to`, and `at` for copy. Song-wide, as §9; returns the song's sections as they are after |
| `edit_clip_time` | `clip`, `action`: insert, delete, duplicate, split; `from`, `to`. Inside a MIDI clip, as §14.1 |
| `bounce_clips` | Gains `from`, `to` and `tracks`, to bounce a stretch (§13) |
| `export_audio` | `range` gains `{from, to}` |

Each tool comes with a test that drives it through MCP. The section tools stay as they are.

## 19. Model and engine

- **`model::Selection`** gains the rows (a set of row keys, tracks and lanes) and an optional stretch (start, end). A time selection is rows with a stretch. [groups.md](groups.md) §8's several tracks are rows without one.
- **`model/TimeEdits`** (new, JUCE-free, `[fast]` tests): a pure plan for each command of §8 on rows and a stretch:
  - the clip cuts and pieces, building on `clearRange`;
  - copies keeping takes, fades and loops;
  - lane curves with their edge points;
  - where a paste or a drag lands, and onto which rows;
  - fade lengths from where the stretch lies.
- **Song-wide commands** are the sections' plans over any stretch, plus the rules for section marks (§9).
- **`model/NoteEditing`** gains time inserted, deleted and duplicated inside a clip, and notes split at edges.
- **The arrangement's hit zones** (`model::hitClip`) gain the title strip as a zone of its own.
- **The Session** gets one command per row of §8 and §9, each one undo step. The clipboard gains two kinds: a stretch on rows, and song time.
- **The engine:**
  - nothing new for editing: splits, clip copies and automation points are what sections and clip edits already use;
  - bouncing a stretch renders each track's range, which `Session::render` already does;
  - a "Before bounce" take holding several parts with gaps is the one thing to check first (§21).

## 20. Not now

- **Stretching time with a selection's edge:** Bitwig's Alt+drag scales everything in it, automation and partial clips too. The audio editor's and the piano roll's handles do it inside a clip.
- **Shaping automation in a selection:** Live's handles scale, stretch and skew an envelope's selected part.
- **Several separate stretches:** Studio Pro's Shift-added ranges, REAPER's razor areas.
- **Consolidate** (Live's Ctrl+J), joining a stretch into one clip. Bounce in place covers audio; joining MIDI clips comes when asked.
- **Converting** between time and the clips in it (Bitwig's Ctrl+T).
- **Ripple modes** (REAPER, Studio Pro): the song-wide commands cover inserting and removing time.
- **Comping from a selection** (Live's take-lane selection): swipe comping on take strips does it.
- **Separating stems from a selection** (Live 12.4): with stem separation, when it comes.
- **A gain handle on a selection in the arrangement,** as the audio editor's range has.

## 21. Steps

0. **Drawn, and one check, before anything is built.**
   - **Drawn** with the real arrangement, clip view and inspector, in every state:
     - a selection on two tracks and a lane, with a track between them left out;
     - a clip's title strip being dragged inside a selection, and outside one;
     - a drag in the song row's overview;
     - TIME in the inspector;
     - the right-click menu;
     - the piano roll with a box of notes and its stretch;
     - the audio editor renamed;
     - at standard and compact widths, zoomed.

     The gestures change what you know (§16), so they're approved before they're built.
   - **The check:** a bounced stretch's "Before bounce" take holding several parts with gaps, switched to and back, saved and opened.
1. **The arrangement:**
   - making and changing a selection (§5–§7);
   - the plain commands (§8) and the song-wide ones (§9);
   - dragging a stretch (§10), automation (§11), playing it (§12);
   - the menus, the right-click menu, the inspector, and MCP.

   Signed off after the product owner's hands-on tasks:
   - select bars 9 to 17 on the drums and bass with their filter lane, and Ctrl+D it;
   - delete half a long clip;
   - insert two bars of silence before the chorus, and delete them again;
   - drag a stretch to another place and another track;
   - loop a selection and record into it;
   - Ctrl+click a track in and out of a selection.
2. **What has waited for it and the clip view:**
   - export, bounce, fades, New MIDI clip and sections (§13);
   - the piano roll's time selection, the audio editor's renaming, and the link from the arrangement (§14).

   Signed off after these tasks:
   - export a selection;
   - bounce a stretch across two clips and a gap, in place and to a new track;
   - fade across three tracks' clip edges at once;
   - duplicate and delete time inside a MIDI clip.

Then groups ([groups.md](groups.md), P9).

## 22. Tests (done when)

| What | Target |
|---|---|
| Plans | Every command of §8 and §9 as a pure plan under `[fast]` tests: cuts at edges, copies with takes, fades and loops, lanes with edge points, landing rows, section marks |
| Sound | A stretch deleted, duplicated or moved renders as the same edit made by hand with splits and moves (null < −120 dBFS); Delete time then Insert time of the same length leaves the song nulling against itself, apart from the gap |
| Automation | Edge points keep the curve outside unchanged (exact values), inside the stretch's shape is kept; lanes not in the selection unchanged |
| Gestures | Title strip moves, body and empty space select time, lanes select time and points, the song row's overview selects every row; Shift, Ctrl and Ctrl+A change it as §6 says |
| Keys | Each command routed by the area used last; labels change with a selection |
| Undo | One step a command, named for it; the selection never one |
| Clip view | Notes: time inserted, deleted, duplicated, notes split at edges; the audio editor's renamed commands as before |
| Export and bounce | A selection exports exactly its stretch; a bounced stretch nulls against its source |
| UI | Fills, lines and the ruler band drawn from the theme's roles; the layout and UI lints pass; screenshots of every state of step 0, zoomed |
| Performance | Dragging a selection across 64 tracks within the arrangement's frame budget |
| MCP | Every tool of §18 driven in a test |

## 23. Questions for review

| # | Question | Recommendation |
|---|---|---|
| T1 | **The name** | "Time selection" in docs and MCP. Menus say "selection" ("Loop selection", "Crop to selection"); the inspector's section is TIME. The audio editor's range is renamed to match (§14.2) |
| T2 | **How it's made** | Live's places, no tool: a drag on a clip's body or empty space selects time; the title strip moves clips (§5). Tools (Bitwig, Cubase, Studio Pro, Pro Tools) are a mode to keep in mind; halves are split the other way round in half the DAWs and fail on short rows (Logic's users report) |
| T3 | **Today's box of clips** | Replaced by the time selection. A title-strip drag inside it moves the stretch; clicks still select whole clips (§10, §16) |
| T4 | **Rows** | Dragged across; Ctrl+click adds or removes one, apart from the others; Shift+click extends; Ctrl+A takes every row (§6) |
| T5 | **Automation** | A lane moves when it's in the selection; no global switch; points at the edges keep the curve outside. This answers B30 (§11) |
| T6 | **A lane's drag** | Selects time, and the lane's points in it; the box by value goes. The clip view's controller lanes the same (§11) |
| T7 | **Across the whole song** | A drag in the song row's overview, Ctrl+A, Edit ▸ Select loop, Song ▸ Select section's time (§5, §6) |
| T8 | **The ruler** | Stays zoom and scroll, as Live's (§5) |
| T9 | **Edges** | No handles: Shift+click, Shift+←/→, and the inspector's fields (§6) |
| T10 | **Plain commands** | Delete, Cut, Copy, Paste (overwriting, at the playhead), Duplicate (after itself, the selection following), Split, Crop, Mute, Reverse, Fade, on its rows; the gap stays (§8) |
| T11 | **Song-wide commands** | Insert, Delete, Cut, Copy, Paste and Duplicate time in the Song menu, with Live's keys (Cut time without one); always the whole song, section marks included; the sections' plans (§9) |
| T12 | **Dragging a stretch** | By a title strip inside it; overwrites where it lands; Ctrl copies; ← → nudge by a grid step (§10) |
| T13 | **The playhead** | Stopped, a new selection puts it at the start, so Space plays it (Live, Logic, Bitwig's default) (§12) |
| T14 | **Loop and punch** | Ctrl+L loops the selection; punching follows the loop (§12) |
| T15 | **Zoom** | Ctrl+0 zooms to the selection when there is one (§12) |
| T16 | **Showing it** | The audio editor's fill and edge lines on its rows, a band in the ruler, TIME in the inspector with Start, End, Length and Rows (§7) |
| T17 | **Export** | Selection in the Range row, chosen when the panel opens with one; exact, no tail (§13) |
| T18 | **Bounce** | Both bounces on a stretch; in place keeps the parts as a "Before bounce" take, checked first (§13, §21) |
| T19 | **Fades** | Fade to selection on Crossfade's key: fade-in, fade-out or crossfade by where it lies, on every selected track (§8, §13) |
| T20 | **The piano roll** | A box drag selects its notes and its stretch; Ctrl+D, Ctrl+E, the time commands inside the clip, Crop and Clip loop, and the Generate tools use it (§14.1) |
| T21 | **The audio editor** | Its range is the same selection, renamed to match; its handles still stretch audio (§14.2) |
| T22 | **From the arrangement to the clip view** | A selection inside one clip shows it in the clip view with the stretch selected; then each is its own (§14.3) |
| T23 | **Rows shared with selected tracks** | One set of rows, with or without a stretch, also used for groups' several selected tracks (§4, [groups.md](groups.md) §8) |
| T24 | **Building it** | Drawn and checked first (step 0), then the arrangement (1), then what waited and the clip view (2), each signed off; groups after (§21) |
