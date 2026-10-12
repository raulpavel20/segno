# The song row and its sections: design (M3 step C3)

How a song's structure gets onto the screen and becomes something you edit. The master's row in the arrangement becomes **the song row**: pinned under the ruler, it shows the song's **sections** (Intro, Verse, Chorus…) over a miniature of every track. Moving, copying or deleting a section takes everything in it along: clips, takes and automation. The idea of putting this on the master's row is the product owner's (2026-10-12). It builds on:
- one kind of track and clips that never overlap ([ADR 0019](../decisions/0019-one-kind-of-track.md)), takes on clips ([ADR 0032](../decisions/0032-takes-belong-to-clips.md)) and clip loops ([ADR 0048](../decisions/0048-clips-loop.md)): a split keeps all three right;
- automation lanes that are rows of their own ([ADR 0028](../decisions/0028-automation-lanes.md), [ADR 0029](../decisions/0029-independent-automation-lanes.md));
- one selection and the context panels ([ADR 0027](../decisions/0027-selection-and-context-panels.md), [selection.md](selection.md));
- the arrangement's grid ([ADR 0047](../decisions/0047-the-editing-grid-in-each-editor.md)) and the time-view kit ([ADR 0023](../decisions/0023-ui-foundations.md));
- the menu bar as the complete list ([ADR 0040](../decisions/0040-a-menu-bar-and-everything-by-mouse.md), [menu-bar.md](menu-bar.md)) and assistants' tools ([ADR 0021](../decisions/0021-mcp-server.md), [mcp.md](mcp.md));
- every edit click-free ([fades.md](fades.md) §1), and no stuck notes ([daw-shell.md](daw-shell.md)).

**Status:** approved by the product owner (2026-10-12), with every recommendation of §15, and the song row shorter than drafted (40 px, not 56: it's always there). The decision is [ADR 0049](../decisions/0049-the-master-row-is-the-song-row.md). Built (2026-10-12): what building settled is in §18. After the product owner's review (2026-10-13), sections are brackets, a dragged one follows the pointer, and sections copy, cut and paste with their music. Signed off by the product owner (2026-10-13).

## 1. Why

- Songs are built from sections. Today, rearranging one (moving the second chorus, adding a verse, cutting the bridge) means selecting clips on every track by hand, moving them, then fixing the automation. It's slow and easy to get wrong.
- Live, our reference, only has locators (named points you jump to). Its users ask for an arranger track, and every other big DAW has one. The innovation backlog lists it for M3, and the roadmap names it.
- The master's row in the arrangement holds no clips. Today it sits at the bottom with the returns, mostly empty, taking a row's height for a volume bar and a meter.

## 2. Where the field is (2026)

- **Studio One:**
  - An Arranger track above the tracks. Drag across it to make a section; it gets a name you can change ([Sound On Sound](https://www.soundonsound.com/node/4916597), [ask.audio](https://ask.audio/articles/experimenting-with-musical-ideas-using-presonus-studio-ones-arranger-and-scratch-pad)).
  - Sections behave like events: drag one to move everything in it (clips, automation, markers), Alt+drag to copy. Its edge can move alone, leaving the music where it is.
  - The Arranger inspector lists the sections top to bottom. Reordering the list rearranges the song.
  - Scratch Pads: side areas to try other arrangements.
- **Cubase:**
  - An Arranger track of events, plus **arranger chains**: a play order (A, A, B, C, B) that plays the song non-linearly without moving anything.
  - "Flatten" turns a chain into a linear arrangement. Steinberg's own help warns to save a copy first, as it may remove events ([Steinberg](https://steinberg.help/nuendo/v12/en/cubase_nuendo/topics/arranger_track/arranger_track_arranger_chain_flattening_t.html)).
- **Logic:**
  - Arrangement markers on the Arrangement track, one of the global tracks above the tracks.
  - Dragging a marker moves its whole section: regions, markers and automation points. Option-drag copies, and the marker to the right shifts over to make room. Markers can be disconnected to move without the music ([Apple](https://support.apple.com/en-bh/guide/logicpro/lgcpf7c0c09c/mac)).
- **Live:**
  - No arranger track: locators to jump between, and time-signature markers ([Sound On Sound](https://soundonsound.com/node/4913966)).
  - **Song-wide things live on the master track:** tempo automation is drawn in the master's automation lanes ([Ableton](https://help.ableton.com/hc/en-us/articles/209772085-Setting-exact-BPM-values-in-the-Arrangement-View)).
  - **The Overview:** a strip above the tracks showing the whole song in miniature, for scrolling and zooming ([Ableton](https://www.ableton.com/en/live-manual/12/arrangement-view)).
- **Bitwig:** cue markers to jump between; no sections that move their content ([Bitwig](https://www.bitwig.com/userguide/latest/playing_back_the_arranger/)).
- **REAPER:** regions over all tracks. Ctrl+drag a region to copy everything in it, a region manager to name and colour them, and playlists of regions through an extension ([Tuts+](https://music.tutsplus.com/how-to-use-reaper-markers-and-regions--cms-107731t), [Sound On Sound](https://www.soundonsound.com/techniques/power-arranging-reaper)).

**What Segno takes:**
- **Sections that carry their music:** Studio One and Logic. Drag to move, Ctrl+drag to copy, the rest of the song making room.
- **An edge that moves alone:** Studio One and Logic's "disconnected" markers.
- **Song-wide things on the master's row:** Live, which draws tempo automation there.
- **The whole song at a glance:** Live's Overview, as thin lines in the row instead of a strip of its own.
- **Left for later:** Cubase's chains and Studio One's scratch pads (§16).

## 3. The idea: the master's row becomes the song row

The product owner's suggestion: use the master's row for the sections, with every track drawn in it as a thin line, since the master can't hold clips and its row is otherwise empty for good.

**Recommendation: yes, with three changes** (A1):
1. **The row moves to the top and stays there:** pinned under the ruler, above the tracks, so it never scrolls away.
   - Sections belong beside the ruler: they're about time, as the ruler, the loop and the playhead are. Studio One, Logic and Cubase all put theirs at the top.
   - Pinned, the miniature becomes useful: it shows the tracks scrolled out of view.
2. **Its header stays the master's.** Its name, volume and meter are where they are on any row. Clicking it selects the master, so the inspector shows its mix and the detail panel its modules. The row is the song's: how it sounds (the master's chain) and how it's built (its sections).
3. **The returns stay at the bottom,** and Ctrl+Alt+R shows and hides them alone. The mixer view doesn't change: the master stays pinned at its right.

This revisits open question B25 ([mixer.md](mixer.md) Q1, agreed 2026-10-09): the returns stay at the end of the track list, scrolling, but the master moves to the top.

**Why this beats the alternatives:**
- **A separate arranger row** (Studio One, Cubase, Logic) costs a row, and the master's row stays empty. That's two rows where one does both.
- **Sections in the ruler** costs no row, but the ruler is 24 px. Names, colours and dragging get cramped, and the ruler already does a job: click to move the playhead, drag to zoom.
- **The song row** puts the master's empty space to work and keeps one row for the whole song. Live puts song-wide things on the master too.

**What it costs:**
- Live's and Bitwig's users look for the master at the bottom.
- The master's row grows a second role, so a click on it has to say clearly which it means. The header and empty spots select the master; a section selects the section (§10).

## 4. Principles

1. **A section is a stretch of the song, with a name and a colour.** It owns nothing: the music in its time is what moves when it moves.
2. **Moving a section moves its music; moving an edge moves only the mark.** Every move says which it is before you let go.
3. **The song makes room.** A move, a copy, an insert or a delete shifts everything after it, so nothing ends up on top of anything else.
4. **What you hear moves exactly.** Clips crossing an edge are split there, and automation keeps its value at the joins. A moved section sounds the same in its new place.
5. **One undo step per command, click-free,** as every edit.
6. **Everything by mouse, nothing modal.** Gestures on the row, properties in the inspector, every command in the menu bar, the same for assistants.

## 5. The song row

```
┌ loop strip 12 ────────────────────────────────────────────────────────────────────┐
├ ruler 24 ─────────────────────────────────────────────────────────────────────────┤
│ Master     ▮│┌INTRO─────┐┌VERSE─────────────┐┌CHORUS────────────┐┌VERSE 2─────┐│ ← title band: brackets, names
│ ──── vol    │ ═══   ──── │ ═════════ ════════ │ ══════ ═══════════ │ ════ ──────── │ ← every track, one thin line
│             │ ─────────  │ ══════════════════ │ ══════════════════ │ ════════════  │   in its colour, where it plays
├─────────────┴────────────────────────────────────────────────────────────────────┤ ← pinned: the tracks scroll under it
│ Drums       │ ▆▆▆▆ ▆▆▆▆ ▆▆▆▆ ▆▆▆▆ ...                                             │
│ Bass        │ ...                                                                 │
```

- **Where:** pinned under the ruler, the arrangement's first row. The free automation lanes, the tracks and the returns scroll beneath it.
- **Height:** 40 px by default, shorter than a track since it's always there (the product owner, 2026-10-12): a 16 px title band for the sections' names, and 24 px of overview below it. Dragging its bottom edge resizes it (32 px at least, the title band and a thin overview), as any row. The height is view state, saved with the project.
- **The header:** the master's, from the track header kit: "Master", the meter, and the volume bar from 80 px tall, as now.
- **The lane:**
  - the overview (§6), across the whole lane;
  - the sections (§7) over it: a bracket each in its colour, a bar along the top with a tick down at each end, its name under the bar in caps. They are not blocks, so they never read as clips. A selected section's span is tinted in its colour;
  - the grid's bar lines, the loop's shading and the playhead, as on any row.
- **Empty:** with no sections yet, the title band reads "Drag here to mark a section", in the empty-state text style, until the first one is made.

## 6. The overview

Every track as a thin line, so the song's shape shows at a glance: where the drums play, where it thins out, what each section holds.

- **Lines:** one per track, in the arrangement's order, top to bottom, in the track's colour, drawn where it has clips. Muted clips are dimmer, as in their rows.
- **Fitting:** with more tracks than the overview has pixels, neighbouring tracks share a line, drawn wherever any of them plays. It always fits the row.
- **Left out:** automation lanes and returns (they hold no clips).
- **Drawn from the snapshot's clip ranges,** not from audio, so it costs a few rectangles per track even with hundreds of clips.
- **No click of its own yet.** A click in the lane selects (§10). Clicking a line to scroll to its track is later (§16).

## 7. What a section is

- **A name, a colour, a start and an end,** on the song's timeline.
- **Sections never overlap.** Gaps between them are fine: an unmarked stretch is just music no section names.
- **Edges snap** to the arrangement's grid (bars, at most zooms), Alt for free, as clips do. A section is at least one grid step long.
- **Names:** new sections are A, B, C… The rename field suggests the usual names: Intro, Verse, Pre-chorus, Chorus, Bridge, Breakdown, Build-up, Drop, Outro. A copy takes its source's name with a number ("Chorus 2"). Names can repeat (A4).
- **Colours:** each new section takes the next colour of the palette. A copy keeps its source's, so the same part looks the same wherever it repeats.

## 8. Making and editing sections

| Gesture | What it does |
|---|---|
| Drag across empty time in the row | A new section there (snapped), selected, its name ready to type |
| Song ▸ Add section | A new section from the loop if it's on, else from the playhead to the next section (or 8 bars) |
| Song ▸ Insert empty section | An empty section after the selected one (as long as it, or 8 bars), the rest of the song moving later to make room |
| Double-click the name, or F2 | Rename where it is |
| Drag an edge | Moves the mark only (the cursor shows a resize arrow). The neighbour's edge is a limit, not pushed |
| Drag the body | Moves the section **with its music** to another place in the song (§9) |
| Ctrl+drag the body | Copies it there, with its music |
| Ctrl+C, then Ctrl+V | Copies it with its music, then pastes it at the playhead (§9). Ctrl+X cuts it: copied, then deleted with its music. Edit ▸ Cut, Copy, Paste and its right-click menu have them too |
| Double-click the body | Loops it: the loop brace set to the section, loop on (§11) |
| Right-click | Rename…, Colour ▸, Loop section, Select its clips, Duplicate section, Delete section, Remove section mark, then the row's own items (Add track…) |

The inspector edits a selected section's name, colour, start and length, as marks: the music stays (§10).

## 9. Moving, copying and deleting with the music

### What moves

Everything between the section's start and end, on every track and lane:
- **clips** (with their takes, loops, fades and mute);
- **automation points** in every lane;
- **tempo and time-signature changes,** once the song has them: today it has one tempo and one time signature, so there's nothing to move yet (§16).

The loop brace and the playhead stay where they are.

### At the edges

- **A clip that crosses an edge** is split there first, as Split does: a looped clip keeps its loop's place, takes split with it, and MIDI notes crossing the edge end there. Edge fades take any click out of the new edges (fades.md).
- **Automation keeps its value at the edges:** a point is added on each edge, at the curve's value there, before the stretch is lifted. Nothing outside the section changes. Inside, the moved stretch keeps its shape exactly.
- **At the place it lands,** a clip crossing that point is split too, and the music after it moves later.

### The commands

| Command | The song before → after (A B C D are sections) |
|---|---|
| **Move** (drag the body) | A B C D → drop B after C → A C B D. The song keeps its length |
| **Copy** (Ctrl+drag, or Duplicate for "right after it") | A B C D → copy B after C → A B C B D. The song grows by B |
| **Paste** (Ctrl+C, then Ctrl+V with the playhead after C) | A B C D → A B C B D, as Copy. Ctrl+X, then Ctrl+V: a move |
| **Insert empty section** | A B C → after B → A B ▢ C. The song grows |
| **Delete section** (Delete) | A B C D → A C D: B's music goes and the gap closes. The song shrinks |
| **Remove section mark** | A B C D → A ▢ C D: the mark goes, the music stays |

- **Where a section can land:** the run follows the pointer, its start on the grid (Alt for free), pulled to a section edge near its start or end. It lands between sections, in empty time too. A place that would cut another section goes to that section's nearer edge. While you drag, a bracket in the active colour shows where it lands, with a line across every row at its start. Esc while dragging cancels.
- **While it plays:** edits apply at once and the song plays on. A note cut by an edit ends (daw-shell.md, "No stuck notes").
- **The clipboard** (the product owner, 2026-10-13): Copy keeps the selected sections' span with everything in it as it is then: its clips (a clip across an edge, the part inside), their takes, and its automation. A later edit doesn't change what pastes, a cut still pastes, and it pastes into another project open in Segno. Paste puts it at the playhead, or, with the playhead inside a section, at that section's nearer edge, as a drag lands; the music after it moves later. Each track's music goes onto the track in its place (by order); music with no track in its place is left out, with a toast. Pasted sections keep their names unless the song has them already ("Chorus 2"), and are selected. One undo step, "Paste section Chorus".
- **Delete is the whole section, music and all,** as Studio One's and Logic's are. Its undo step says so ("Undo Delete section Bridge"), and a toast offers Undo. "Remove section mark" keeps the music (A6).

## 10. Selection and the inspector

[selection.md](selection.md) §2 gains rows:

| Click | Channel | Clips |
|---|---|---|
| The song row's header, or an empty spot in its lane | The master | None |
| A section (Shift or Ctrl + click adds or removes) | The master | None, and the section is selected |
| Select its clips (a section's right-click menu, Song menu) | The track of the topmost clip | The clips within the section (edges included) |

- **A selected section** is a selection of its own, like points in a lane: Delete, Ctrl+D, F2 and the arrows act on it while it was used last.
  - ← and → move its music a section earlier or later.
  - Shift adds the next section to the selection, so a run of them moves as one.
- **The inspector:** SECTION (Name, Colour, Start, Length; edits move the marks, not the music), then the master's MIX, as for the master today. The detail panel shows the master's modules.

## 11. Playing and finding your way

- **Loop a section:** double-click it, or Song ▸ Loop section. The loop brace takes its edges and the loop turns on. With several selected, the loop runs from the first one's start to the last one's end.
- **Previous and next section** (Alt+← and Alt+→): the playhead jumps to the start of the section before or after it, playing or not.
- **Following the playhead,** the section it's in is lit in the title band.

## 12. Menus, keys and actions (ADR 0040)

A new **Song** menu, between Clip and Module. Song-wide things go here: sections now, and markers, tempo and time-signature changes when they come.

| Item | Shortcut | |
|---|---|---|
| Add section | – | `song.addSection` |
| Insert empty section | – | `song.insertSection` |
| — | | |
| Loop section | – | `song.loopSection` |
| Previous section | Alt+← | `song.previousSection` |
| Next section | Alt+→ | `song.nextSection` |
| — | | |
| Select clips in section | – | `song.selectClips` |
| Duplicate section | (Ctrl+D on a section) | `song.duplicateSection` |
| Delete section | (Delete on a section) | `song.deleteSection` |
| Remove section mark | – | `song.removeSectionMark` |
| Section colour ▸ | | the palette (`list("sectionColour")`) |

- Edit's Rename (F2), Duplicate (Ctrl+D), Delete, Cut, Copy and Paste act on a selected section as they act on clips, so the Song menu's items have no shortcuts of their own.
- View's "Show or hide returns and master" becomes **Show or hide returns** (`view.returns`, Ctrl+Alt+R; the ID stays).
- A test already fails if an action isn't in the menu bar.

## 13. Assistants (MCP)

Tools named as the others ([mcp.md](mcp.md)), each one named undo step, each with a test that drives it through MCP (ADR 0021):

| Tool | Does |
|---|---|
| `list_sections` | Every section: id, name, colour, start and end (bars and beats, and seconds) |
| `add_section` | Marks a section (name, start, end, colour optional); refuses one that would overlap |
| `set_section` | Renames, recolours or moves its marks (start, end), the music staying |
| `move_section` | Moves a section with its music to before or after another section, or `at` a point (inside a section, its nearer edge) |
| `copy_section` | Copies it with its music, to before or after another section, or `at` a point (the clipboard's paste at the playhead, with the place named) |
| `insert_section` | An empty section of so many bars, before or after another, the rest making room |
| `delete_section` | Deletes it with its music, the gap closing; `keep_music: true` removes only the mark |

`get_project` (the snapshot tool) lists the sections too, so an assistant sees the song's structure first.

## 14. Model and engine

- **Sections are the Edit's arranger track** (`te::ArrangerTrack`, its `ArrangerClip`s: name, colour, start, length). They're saved with the project and undone with it. The arranger track is hidden from the track list: the song row draws it.
- **A pure model** (`model::Sections`, `[fast]`-tested) plans each command on a plain list of sections, clips and lane points:
  - what splits where;
  - what moves by how much;
  - what the song looks like after.
- **`engine::Session` carries the plan out** in one transaction (one undo step), through the engine's split, move and insert operations and `setLanePoints`. The snapshot gains the sections, and each track's clip ranges for the overview.
- **The song row** is a row kind of its own in `model::ArrangementRows` (it was the master's row at the bottom), pinned by `model::TrackLayout` above the scrolling rows.
- **No tempo map change:** the song has one tempo. When tempo changes come, they're one more thing a plan moves.

## 15. Questions and recommendations

| # | Question | Recommendation |
|---|---|---|
| A1 | **The master's row as the song row** (the product owner's idea) | Yes: pinned under the ruler, its header the master's, the sections and the overview in its lane. The returns stay at the bottom (B25 stands for them), the mixer view as it is (§3). *Agreed, with the row shorter than drafted: 40 px by default* |
| A2 | **The overview** | Every track as a thin line in its colour where it has clips, muted ones dimmer; tracks share lines when there are more than pixels; no lanes or returns; no click of its own yet (§6) |
| A3 | **What a section is** | A name, colour, start and end; no overlaps, gaps allowed; edges on the grid (Alt free); at least a grid step long (§7) |
| A4 | **Names and colours** | A, B, C… with the usual names suggested when renaming; copies keep the name with a number and keep the colour, so repeats look alike (§7) |
| A5 | **Edge and body** | An edge moves the mark alone; the body moves the music; Ctrl copies. As Studio One and Logic (§8) |
| A6 | **Delete** | The whole section with its music, the gap closing, as Studio One's and Logic's; "Remove section mark" for the mark alone (§9) |
| A7 | **Clips across an edge** | Split at the edge when the music moves, as Split does: exact, and what you hear moves. The other way, a clip moving with the section it starts in, would carry music from outside it (§9) |
| A8 | **Automation** | Moves with its section; a point on each edge keeps the curve's value there, so nothing outside changes (§9) |
| A9 | **The song makes room** | Moves, copies, inserts and deletes shift everything after them; a move keeps the song's length (§9) |
| A10 | **Where a section lands** | At another section's start or end, with an insert line while dragging; in a gap, at its ends (§9) |
| A11 | **Selection** | A section is a selection of its own, its channel the master; the inspector shows SECTION, then MIX; Select clips in section bridges to clip commands until range selection comes (§10) |
| A12 | **Playing** | Double-click loops a section; Alt+← / Alt+→ jump between sections; the section under the playhead lit (§11) |
| A13 | **The Song menu** | New, between Clip and Module, for song-wide things: sections now, markers and tempo later (§12) |
| A14 | **Assistants** | The seven tools of §13, and the sections in `get_project` |
| A15 | **Left for later** | Chains (Cubase's play orders), scratch pads (Studio One), a tempo and time-signature track, markers, exporting or bouncing a section, clicking an overview line to reach its track (§16) |

## 16. What comes when

- **M3 (this step):** everything above.
- **With range selection (ADR 0028):** a section's time as a range to bounce or export; Export's range offers "a section".
- **With tempo and time-signature changes:** they live on the song row too (Live keeps tempo on the master), and move with their sections.
- **Later, with demand:**
  - **Chains:** play orders that leave the song as it is (Cubase).
  - **Scratch pads:** other versions of the arrangement to try (Studio One).
  - **Markers:** single points, for cues.
  - **The overview:** clicking a line scrolls to its track.
  - **Moving sections between projects.**

## 17. Tests (Done when)

- **Model (`[fast]`):** each command's plan on lists of sections, clips and lane points:
  - where clips split;
  - what moves and by how much;
  - the lane points added at the edges;
  - the song's length after;
  - names and colours of copies.
- **Session:**
  - each command is one undo step, and undo puts the project back exactly;
  - takes, loops and fades survive a split at an edge.
- **Sound:**
  - a section moved, then played, renders the same as it did in its old place, sample for sample (a dry instrument, so no tail crosses in);
  - a copy renders the same twice;
  - a move while playing leaves no stuck notes.
- **Assistants:** each tool of §13 driven through MCP.
- **UI:**
  - the song row's snapshots at standard and compact widths, zoomed, with sections, without, and with many tracks;
  - names cut short with an ellipsis rather than touching an edge;
  - the menu-bar test.
- **Performance:** the song row draws within the frame budget with 100 tracks and 2 000 clips.

## 18. Settled while building (2026-10-12)

- **The overview** is inset 3 px (`shell.songOverviewInset`) from the title band and the divider: a track's line right under a title band of the same colour ran into it. Its lines sit on whole pixels, as the sections over them do, so none pokes into the gap between two sections.
- **The rename field suggests nothing:** the shared in-place field has no list. A section's right-click menu has a **Name ▸** submenu with the usual names instead, beside Rename.
- **← and → don't move sections:** they already nudge notes and lane points. Dragging, Ctrl+D and Delete do it.
- **The section under the playhead isn't lit:** the playhead's line runs through the song row, which shows where it is. Lighting it can come with following the playhead (§16).
- **The Song menu's letter is N** (Alt+N): Alt+S solos the selected track.
- **Selecting a run:** Shift+click selects every section from the one picked last to the one clicked. Ctrl isn't a toggle here: Ctrl+drag copies.
- **A run with gaps** moves, copies and deletes as its span, from its first section's start to its last one's end, the gaps' music included.
- **Automation at an edge:** a straight or stepped segment cut at an edge keeps its exact shape; a bent one keeps its ends' values, its bend near enough.
- **The palette's size** (16) moved into the model (`model::kPaletteSize`), so the song row can show a section's colour before the engine makes it.
- **Assistants' tools take positions and lengths as the others do** (beats, or "bar.beat.sixteenth", "1 bar"); `list_sections` also gives each section's seconds. A test caught positions passed as beats where seconds were meant.
- **A new section is selectable at once,** before its snapshot arrives, as a new clip is.
- **Sections are brackets, not blocks** (the product owner's review, 2026-10-13): drawn as blocks with a title band, they looked too much like clips. Three looks were compared on a song-like project: brackets, flags (a tab at the start), and tinted zones. Brackets were picked: they read as a span of time, like the loop brace and the ruler, and leave the overview in full view. Bar and ticks are `shell.songBracket` (2 px), with `songBracketGap` (2 px) between two that touch, inset `clipGap` from the row's top. The name is in the caps style (`TextStyle::Section`), secondary text, primary when selected.
- **A dragged run follows the pointer** (the same review: "I can't seem to be able to drag to move a section"). At first a drag only picked the section boundary nearest the pointer, outside the run. Nothing moved with the pointer, and a lone section, or one aimed at empty time, had nowhere to go. Now its start follows the pointer and `model::landRun` finds where it lands (§9).
- **Copy and paste** (the product owner, 2026-10-13: "you should be able to also copy a selected section… and then paste it at playhead"): Ctrl+C, Ctrl+X and Ctrl+V, as §9 describes. The clipboard keeps the span as it was (`Session::SectionClipboard`: a `model::SongPiece` of its automation and sections, and its clips). A paste makes room for the span widened to the clips across its edges, puts those clips in whole, cuts them at the edges, then closes the margins, so nothing beside it is touched. Assistants got `at` on `move_section` and `copy_section`.
- **Tests:** the model's plans and landings (`SectionsTests`), the Session's commands with one-step undo and the sound of a moved section, sample for sample (`SectionTests`, including clips that touch end to end), every tool through MCP, and the song row by mouse with its screenshots (`AppSmokeTests`).
