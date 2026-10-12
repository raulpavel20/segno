# Tracks and clips: design (M2 step C11)

How Segno lets you **rename, delete, reorder, duplicate and colour** tracks and clips, and **cut, copy and paste** clips, notes and automation points. These are the basics every DAW has; Segno doesn't have them yet.

The decision is [ADR 0041](../decisions/0041-tracks-and-clips-managed-in-place.md) (§14). It builds on:
- one kind of track, and clips that never overlap ([ADR 0019](../decisions/0019-one-kind-of-track.md));
- automation lanes as rows of their own ([ADR 0029](../decisions/0029-independent-automation-lanes.md));
- takes on their clips ([ADR 0032](../decisions/0032-takes-belong-to-clips.md));
- sidechain routing ([ADR 0025](../decisions/0025-sidechain-routing.md));
- one selection ([ADR 0027](../decisions/0027-selection-and-context-panels.md), [selection.md](selection.md));
- the menu bar, which is the complete list but never the catch-all ([ADR 0040](../decisions/0040-a-menu-bar-and-everything-by-mouse.md), [menu-bar.md](menu-bar.md) §15).

It settles open questions B41 (what deleting a track takes with it), B50 (cut, copy and paste) and B52 (renaming).

**Status:** approved by the product owner (2026-10-11), with every recommendation of §13 ([ADR 0041](../decisions/0041-tracks-and-clips-managed-in-place.md)). Built (2026-10-11). What building settled is noted in §17; what the feel check found, and how it's fixed, in §18. Signed off by the product owner (2026-10-11), with §18 and §19. The song test then had a clip's colour be its own (§20, [ADR 0043](../decisions/0043-a-clips-colour-is-its-own.md)).

## 1. Why

On 2026-10-11 the product owner found that tracks can't be renamed, deleted or rearranged. The gap was known but never planned:
- ADR 0029 notes that "the delete commands aren't built yet" and that lanes are reordered "with track reordering";
- B41 waits "when track deletion arrives";
- mcp.md lists "renaming, deleting and reordering tracks; clip names and colours" as not in the DAW yet.

Each deferred it, and no milestone step owned it. Today the Session can add a track or a return track and nothing else. So:
- tracks and return tracks can't be renamed, deleted, reordered, duplicated or recoloured;
- clips can't be renamed or recoloured;
- there's no clipboard.

Automation lanes can already be renamed and removed, and takes renamed.

## 2. Where the field is (2026)

From the manuals of Live 12, Bitwig 5, Studio One (now Fender Studio Pro), Logic 11, REAPER 7 and Cubase 14. Where a manual says nothing, that's noted.

| | Live | Bitwig | Studio One | Logic | REAPER | Cubase |
|---|---|---|---|---|---|---|
| **Rename** | Ctrl+R, in place; Tab goes to the next track's name | Header menu, inspector field | Double-click a console name; events from their menu | Double-click the header name; Shift+Return | Double-click the name | Double-click the name; Alt+F2 |
| **Delete a track** | Delete; no question (undo) | Delete; no question | Delete with no events selected; Shift+T | Cmd+Delete | Delete; **asks** (can be turned off) | Shift+Delete; **asks** if not empty |
| **Reorder** | Drag the title bar; Ctrl+arrows; returns too (12) | Drag the header | Drag in the track column | Hold an empty part of the header, drag; mixer strips too | Drag the panel (not its controls) | Drag the name or an empty area; a line shows where |
| **Duplicate a track** | Ctrl+D | Ctrl+D | Duplicate (Complete) for the events too | Opt+Shift+Cmd+D with content | Header menu | Project menu; copy below |
| **Clip colour** | Takes the track's; "Assign Track Color to Clips" | Inherits until set (the palette's ×) | Follows the track unless coloured by hand | As the track, or individual | Lowest level wins | Follows the track until set; "Set Event Color to Track" |
| **Paste lands** | At the insert marker | At the play-start marker | On the selected track at the playhead; multi-track from it | At the playhead, on the selected track | At the edit cursor, on the last selected track | On the selected track at the cursor |
| **Overlaps on paste** | Overwrites (forum) | Not documented | Layered (option: no overlap) | Later region heard | Layered (option) | Layered (option) |

- **Renaming is in place**, by double-click or a command. F2 isn't a DAW rename key: in Studio One it opens the editor, in REAPER and Cubase a dialog. It is the system's rename key in file managers on Linux and Windows.
- **Delete is the Delete key on a selected track**, with undo as the safety net. Only REAPER and Cubase ask first, and both let you turn the question off. No manual says how sends or sidechains to a deleted track are cleaned up.
- **Reordering is a header drag** with a line showing where it lands. The mixer follows the track order, and dragging strips reorders too (Logic, Cubase).
- **A duplicate goes right below** the original. Live, Bitwig, REAPER and Cubase copy the clips; Logic's and Studio One's plain Duplicate copy only the settings.
- **Clips follow their track's colour** until given their own (Studio One, Cubase, Bitwig), with a command to follow it again.
- **Paste lands at the playhead or edit cursor on the selected track**, never at the mouse by default. A multi-track paste starts there.
  - Several DAWs also have Paste at original position.
  - Notes paste at the playhead at their own pitches.
  - Nobody documents moving the playhead after a paste.
  - Live, Logic and Studio One keep clips from lying on top of each other, as ADR 0019 does; Studio One, REAPER and Cubase let them layer by default.
- **Across projects:** Bitwig, Logic, Studio One and REAPER paste between open projects, and REAPER even between running copies.


## 3. Principles

1. **In place first** (ADR 0040, menu-bar.md §15). You rename a name where you see it, move a track by dragging it, colour it from its own right-click, and delete what's selected with Delete. The menu bar lists every command too, and the inspector shows the properties.
2. **One undo step each**, named for what it did: "Rename track", "Delete track", "Move track", "Duplicate track", "Track colour", "Clip colour", "Cut", "Paste".
3. **Nothing modal, nothing asked.** Deleting doesn't ask first: the toast that says what went has Undo on it (`Shell::showToast`'s action). Undo is one key away anyway.
4. ~~**Clips follow their track's colour** until given their own, as they do now (a clip's colour defaults to its track's).~~ **A clip's colour is its own**: a new clip takes its track's colour once, and keeps it (§20, ADR 0043).
5. **Nothing left dangling.** Deleting something takes, in the same undo step, everything that only made sense with it: sends to a deleted return, sidechains fed from a deleted track, automation of its parameters.
6. **Assistants get the same** (ADR 0021): every command here has an MCP tool (§11).

## 4. Renaming

- **Where:**
  - **Tracks and return tracks:** double-click the name in the track header or on the mixer strip; or the inspector's Name row, which becomes a field (TRACK, RETURN).
  - **Clips:** double-click the clip's title strip (a double-click on its body still opens a MIDI clip in the clip view); or the inspector's CLIP Name, which becomes a field.
  - **Automation lanes:** a double-click on the lane header's name too; Rename… and the inspector's field stay.
  - **Takes:** unchanged, in the TAKE section's field and the take menu. A strip's first click already uses that take for its section (ADR 0038, menu-bar.md §15).
- **F2** renames the selection, whatever it is: a track, a return, a lane, a clip (`edit.rename`, Edit > Rename…).
  - It's the system's rename key in file managers on Linux and Windows, not a DAW's (§2): Live uses Ctrl+R, which is Record here; Logic uses Shift+Return.
  - As in Live, Tab while renaming a track keeps the name and goes on to the next track's (Shift+Tab: the one before), so a set of tracks can be named in one go.
- **The name editor:**
  - It edits in place, in the name's own text style, over its own area, with the text selected.
  - Return or a click elsewhere keeps it. Escape restores the name. An empty name restores it too: a track always has a name.
  - Several selected clips are renamed together (in the inspector, or with F2): they all take the name.
- **The master** isn't renamed: it's "Master".

## 5. Deleting tracks

- **How:**
  - Delete, with the track's header selected (clicked: the track selected, no clips). Delete still deletes clips, lanes, points or modules when those are what was used last (selection.md §6).
  - Track > Delete track, and the header's right-click menu.
  - Return tracks the same way. The master can't be deleted.
- **What goes with it, in the same undo step:**
  - its clips, with their takes;
  - its modules, and every sidechain fed from it (the module's sidechain goes back to Off, ADR 0025);
  - automation of its parameters:
    - a lane that drives only this track goes;
    - a lane that also drives other channels loses this track's assignments and stays;
  - for a return track: every send to it;
  - a lane sitting right after it moves to after the row above it (B41).
- **After:** the track above is selected (the one below, for the first). An audition on the track stops.
- **No question asked:** a toast says "Deleted Bass", with Undo.
- **Refused while recording**, with a toast saying why. Recording owns the track while it records.

## 6. Reordering

- **Drag a track's header** (its background, not its controls, past the drag threshold):
  - a 2 px line in `active` shows where it will land;
  - Escape cancels;
  - it's one undo step, "Move track".
- **Every track and lane moves on its own** ([ADR 0042](../decisions/0042-every-row-moves-on-its-own.md), §19):
  - dragging a header moves that row alone; every other row stays where it is, the lanes after a moved track included;
  - a track or a lane can be dropped between any two rows among the tracks and lanes, at the top or at the end;
  - this is ADR 0029's "reordering rows comes with track reordering", with a lane staying where it was put.
- **Return tracks** reorder among themselves, below the tracks and lanes. The master stays last.
- **The mixer view:** drag a strip by its name, along the strips; tracks among tracks, returns among returns.
- **One track at a time**, while the selection holds one channel (ADR 0027). Several at once comes with multi-track selection (§16).

## 7. Duplicating a track

- **How:** Ctrl+D with the track's header selected (Duplicate acts on what was used last, as now), Track > Duplicate track, or the header's right-click menu.
- **The copy** goes right after the original and the lanes after it. It's named with the next free number ("Bass" → "Bass 2", "Bass 2" → "Bass 3") and selected.
- **What's copied:**
  - its clips, with their takes and fades;
  - its modules and their settings, and its sidechains;
  - its mix: volume, pan, mute and sends;
  - its input, and its colour.
- **What isn't copied:** arm (two armed tracks would record the same input twice) and solo.
- **Automation:** a lane that drives only the original is copied, after the copy, driving the copy. A lane that drives several channels isn't copied.
- **Return tracks** duplicate the same way. Sends to the original stay; the copy gets none.

## 8. Colours

- **The palette:** the 16 clip colours (`Theme::clipColours`), in a swatch grid: 4 × 4 swatches, a row high each, with the current colour ringed.
- **Where:**
  - Tracks, returns and lanes: the header's right-click menu, Colour ▸ (the grid); the inspector's Colour row (a swatch that opens the grid).
  - Clips: the clip's right-click menu, Colour ▸, and the inspector's CLIP Colour row. Every selected clip takes it. ~~The grid's first item is "Track colour", which makes it follow its track again.~~ No "Track colour" item since §20.
- ~~**Following:** a clip without a colour of its own follows its track's, now and when the track's changes. That's how clips are stored already: a clip's colour defaults to its track's.~~ Since §20, **a clip's colour is its own**: its track's when it's made, independent after.

## 9. Cut, copy and paste

- **What:** whatever the area used last holds (selection.md §6):
  - clips in the arrangement;
  - notes in the clip view;
  - points on an automation lane.
- **Keys and places:** Ctrl+X, Ctrl+C, Ctrl+V; Edit > Cut, Copy, Paste; and the clips', notes' and points' right-click menus.
- **Pasting clips:**
  - They land at the playhead on the selected track.
  - Clips copied from several tracks keep their rows, from the selected track down; rows past the last track are left out, with a toast saying how many.
  - They cut what they land on, as moved clips do (ADR 0019). They keep their takes and fades. Audio clips refer to the same audio files.
- **Pasting notes:** at the playhead, inside the clip the clip view shows, keeping their timing and pitches. If the playhead isn't over the clip, they land at its start.
- **Pasting points:** at the playhead, on the selected lane, keeping their timing and values (mapped to the lane's range).
- **Pasting sections** (the song row, [arranger.md](arranger.md) §9): with their music as it was copied, at the playhead (inside a section, at its nearer edge), the rest of the song moving later.
- **After a paste:** what was pasted is selected, and the playhead stays where it was. No DAW surveyed moves it (§2). Duplicate (Ctrl+D) lays copies one after another.
- **Undo:** cut is one step ("Cut"), and so is each paste ("Paste").
- **The clipboard is the app's:** it lasts while Segno runs, across projects opened in it. The system clipboard and pasting between two running Segnos come later (§16).

## 10. Menus, keys and actions (ADR 0040)

| Item | Where | Key | Action |
|---|---|---|---|
| Cut, Copy, Paste | Edit (second group, first) | Ctrl+X, Ctrl+C, Ctrl+V | `edit.cut`, `edit.copy`, `edit.paste` |
| Rename… | Edit | F2 | `edit.rename` |
| Duplicate track | Track | (Ctrl+D with the header selected) | `track.duplicate` |
| Delete track | Track | (Delete with the header selected) | `track.delete` |
| Colour ▸ | Track; Clip | | lists: the grid for the selected track or clips |

- Delete and Duplicate keep their keys and route by what was used last, as now. The track commands have none of their own: their keys are those two.
- **Right-click menus:**
  - a track's header: Rename…, Duplicate track, Delete track, Colour ▸, then Add track…;
  - a clip's: Cut, Copy, Paste, Colour ▸, after the commands it has.

## 11. Assistants (MCP)

| Tool | What |
|---|---|
| `set_track` | A track's or return's name and/or colour |
| `delete_tracks` | Tracks or returns, as §5 (refused while recording) |
| `move_track` | A track right above another (the lanes stay), or a return among the returns |
| `move_automation_lane` | A lane right above a track or another lane, or at the end (§19) |
| `duplicate_track` | As §7; returns the copy |
| `set_clips` | Clips' name and/or colour (`null` colour: each its track's colour, once, §20) |
| `copy_clips` | Copies clips to a position and a track (the clipboard's counterpart: an assistant names where) |

`get_project` already reports names, colours and order.

## 12. Model and engine

- **`model::` (JUCE-free, `[fast]`-tested):**
  - `nextCopyName` ("Bass 2");
  - `rowMove`: where a dragged track or lane lands, and the lanes that move with a track;
  - `pastePlacement`: clips to rows and times, rows past the end;
  - `deletionPlan`: which lanes go and which only lose assignments, the lanes to re-anchor, the sends and sidechains to clear.
- **`Session`:**
  - `renameTrack`, `setTrackColour`, `deleteTrack`, `moveTrack`, `duplicateTrack`, `renameClips`, `setClipColour(clips, colour)` (§20: an `int`; it was `optional<int>`, nullopt to follow the track);
  - `copyClips` returns a `ClipClipboard` holding copies of the clips' state, so a cut still pastes; `pasteClips(clipboard, track, at)`.
  - Notes and points are copied as values (`model::Note`, `AutomationPoint`) by the shell.
  - Each is one undo step. tracktion's own `Edit::deleteTrack`, `moveTrack` and track copies with fresh ids do the work.
- **The shell:**
  - an in-place name editor (`ui::NameEditor`, shared: track headers, strips, clip titles, lane headers);
  - the header drag in `ArrangementView` and `MixerView`;
  - the swatch grid (`ui::ColourGrid`, in fd_ui);
  - the clipboard (one per app).

## 13. Decisions from the review (product owner, 2026-10-11: every recommendation agreed)

| # | Question | Recommendation |
|---|---|---|
| Q1 | **Renaming** | A double-click on the name where it is (track header, mixer strip, clip title strip, lane header); F2 for the selection; the inspector's Name rows become fields. Master: not renamed (§4) |
| Q2 | **Deleting a track** | Delete with its header selected, Track > Delete track, the header's menu; no question, a toast with Undo; refused while recording (§5) |
| Q3 | **What a deleted track takes** | Its clips and takes, modules, sidechains fed from it, lanes that drive only it, its assignments in shared lanes; sends to a deleted return; lanes after it re-anchor (§5, B41) |
| Q4 | **Reordering** | Drag the header (or a mixer strip's name); ~~the lanes after a track move with it~~ every track and lane moves alone (ADR 0042, §19); returns among returns; one track at a time (§6) |
| Q5 | **Duplicating a track** | Ctrl+D on its header, Track > Duplicate track; right after it, "Bass 2"; everything but arm and solo; lanes that drive only it are copied for the copy (§7) |
| Q6 | **Colours** | A 4 × 4 swatch grid from the right-click and the inspector; ~~clips follow their track's colour until given their own; "Track colour" brings that back~~ a new clip takes its track's colour once, and keeps it (§8, §20, ADR 0043) |
| Q7 | **Paste** | Clips at the playhead on the selected track, rows kept, cutting what they land on (as Live and Logic, ADR 0019); notes at the playhead in the shown clip; points at the playhead on the selected lane; the playhead stays; Duplicate lays copies one after another (§9) |
| Q8 | **Where the clipboard lives** | In the app, across projects, while it runs; the system clipboard later (§9) |
| Q9 | **Keys** | Ctrl+X/C/V; F2 rename (the system's rename key; Ctrl+R, Live's, is Record here), with Tab to the next track's name; Delete and Ctrl+D route to the track when its header was used last; no keys of their own for the track commands (§10) |
| Q10 | **Assistants** | `set_track`, `delete_tracks`, `move_track`, `duplicate_track`, `set_clips`, `copy_clips` (§11); `move_automation_lane` since §19 |

## 14. ADR 0041: Tracks and clips are managed in place

- Tracks, returns, lanes and clips are renamed where their names are, F2 included.
- They're deleted with Delete, moved by dragging their headers, duplicated with Ctrl+D and coloured from their own right-click and the inspector, each one undo step, with no question asked.
- Deleting takes along what only made sense with it.
- The clipboard holds clips, notes, points or sections from the area used last and pastes at the playhead.
- The menu bar lists every command (ADR 0040), and assistants have tools for each (ADR 0021).

This amended ADR 0029's "lanes stay where they were put": the lanes right after a track moved with it. [ADR 0042](../decisions/0042-every-row-moves-on-its-own.md) took that back the same day (§19).

## 15. Tests (Done when)

- **`[fast]`:** `nextCopyName`, `rowMove` (tracks with their lanes, lanes alone, returns), `pastePlacement` (rows, past the end, cutting), `deletionPlan` (lanes, assignments, sends, sidechains).
- **Integration:**
  - each Session command and its undo;
  - a deleted track's sidechain source back to Off, a deleted return's sends gone;
  - a duplicated track sounds like the original (rendered and compared);
  - paste after cut.
- **Shell:**
  - double-click renames a track, a clip, a lane;
  - F2;
  - Delete on a header;
  - dragging a header, with lanes following;
  - the colour grid;
  - Ctrl+X/C/V in each area;
  - screenshots, zoomed, at standard and compact widths: the name editor, the drag line, the colour grid.
- **MCP:** each tool through the server.
- **By hand:** manual-tests/M2.md, "Tracks and clips (C11)".

## 16. What comes when

- **C11 (M2):** everything above.
- **Later:**
  - selecting several tracks (Ctrl+click headers) to delete, move, duplicate or colour them together;
  - groups and folder tracks;
  - the system clipboard, and pasting between two running Segnos;
  - Paste at original position (Studio One, Logic, Cubase);
  - moving tracks with the keyboard;
  - colours beyond the palette.

## 17. Settled while building

- **Renaming:**
  - Tab goes on to the next row with a name, a lane's as well as a track's, bringing it into sight.
  - A clip's title is renamed with every part of a comp.
  - The lane header's Rename… now edits in place too.
- **"Its header was clicked last"** is the arrangement's to tell (`ArrangementView::headerActive`): a click anywhere else ends it, and a click on a clip in the track never deletes the track.
- **Colours:**
  - ~~The snapshot says whether a clip has a colour of its own (`ClipInfo::ownColour`), so the grid ticks "Track colour" and the inspector's swatch says so.~~ Gone with §20: every clip has its own.
  - Lanes take colours from the same grid (Track > Colour with a lane selected, the inspector's swatch).
  - Track > Colour and Clip > Colour are lists in the menu bar, as ADR 0040 allows for lists.
- **Pasting:**
  - Notes paste as one undo step, "Paste", and are selected; at the clip's start when the playhead isn't over the clip.
  - Points replace those over their span, keeping their values in the lane's 0..1, and are selected.
- **The drop line** is one metric, `shell.dropLine`, shared with the module chain's (it was a 2 hand-placed there).
- **Assistants:** clips report their colour in every result that lists them.
- **Engine:**
  - a duplicate is tracktion's state copied with new ids, its clips' take references re-pointed to the copied takes;
  - a return's copy takes the lowest free aux bus;
  - deleting a track clears live MIDI's target if it was that track.

## 18. After the feel check (product owner, 2026-10-11)

Six things came back from trying it. Each is fixed, with a test that would have caught it.

- **Renaming takes the keyboard focus, every time.**
  - What happened: renaming a track or a lane showed the field, but typing didn't reach it. A window that gets the system's focus back hands JUCE's focus to whatever had it before, without telling the field. That happens when the window manager answers a menu closing, or at launch.
  - The fix: `ui::takeFocus` gives a field the focus and keeps it until the next mouse press (or for two seconds). These use it:
    - the name editor;
    - the inspector's take and lane names;
    - the command palette and the parameter picker;
    - the export and About popovers;
    - a knob's or a number's typed value.
  - At launch it keeps the focus in the shell, so shortcuts work before the first click.
  - Finishing gives the focus to the shell (or the mixer), never to a control in the header, where Return would press it.
  - The shell tests now run on a window on the desktop, the only place keyboard focus exists, and replay the late hand-back.
- **A click elsewhere keeps what was typed.** §4 said so; the build didn't do it.
  - `ui::EndEditingOnClickAway` is installed once by the shell. On a press anywhere else in the window it gives the focus to the shell, and the field keeps its text as it loses the focus.
  - Presses in a menu don't count. Neither does the press that opened the field: the second press of a double-click on another name renames that one next.
  - It covers every text field: names, in place and in the inspector; the export panel's name; a number being typed.
- **A lane's header menu has Colour ▸**, as §8 promised, and reads like a track's:
  - Rename…, Assign parameter…, Turn lane on/off, **Delete automation lane**, Colour ▸.
  - "Delete automation lane" was "Remove automation lane", in the menu and the undo step.
  - "Remove from automation lane" on a control stays: it deletes nothing.
  - The MCP tool keeps its released name, `remove_automation_lane`.
- **The names the app gives never repeat** (`model::nextTrackName`, `nextReturnName`, `nextLaneName`, `uniqueName`, `laneCopyName`). Each kind takes the one after the highest in use, so a deleted one's name doesn't come back:
  - **Tracks:** "N Track", one past the highest number. With "1 Track" of four deleted, the next is "5 Track", not a second "4 Track".
  - **Return tracks:** the letter after the highest. With A and B, and A deleted, the next is "C Return".
  - **Automation lanes:** numbered when made, "Automation 1", "Automation 2"…, whatever they drive.
    - They were named after their parameters (B37, now superseded). The header's second line still says what a lane drives.
    - A project from before gets its lanes numbered as it opens, top to bottom, and stays saved.
  - **Other names:**
    - a bounce's track and a take moved to its own track take the next free number when their name is in use ("Bass bounce 2");
    - a duplicated track's lanes take the next lane number, or a copy's name ("Filter 2") when the user named them (`laneCopyName`).
  - Only names the user types can repeat.
  - A name is never empty. A lane's empty name is refused, as a track's is: the field restores it, and `set_automation_lane` says it can't be empty.
- **Snap is a view option** (`ui::ViewOption`), like the mixer's Modules, Sends and Narrow:
  - the dot and the label, no fill;
  - the ruler's row high, its dot where the header names start.
  - View options change how a view looks or edits (view state, never an undo step). They sit in a panel header, or here, in the arrangement's corner.
- **Found on the way:**
  - The window's title bar outlived the shell whose menus it showed, which tripped an assertion on quit.
  - Snap's place had been 3 px right of the names; it now comes from the header layout.

## 19. Every row moves on its own (product owner, 2026-10-11; ADR 0042)

- **Why:** the lanes after a track moved with it (§6 as built). That assumed the user keeps a track's lanes under it, which nothing says: a lane may drive another track, several, or none. Whether carrying them helps or gets in the way was a guess. Moving several rows is a selection's job, and comes with multi-track selection.
- **Now:**
  - a dragged track or lane moves alone; every other row keeps its place;
  - it can land between any two rows among the tracks and lanes: a track between two lanes too, no longer snapping past them;
  - returns still move among the returns.
- **How it's kept:** a lane is still stored as "after a track". A move rewrites the lanes around it, so each sits after the track now above it, in the same order (`model::moveRow`). Deleting a track already worked this way (B41).
- **Assistants:** `move_track` keeps its name and arguments: the track lands right above the given one, and the lanes stay. `move_automation_lane` moves a lane right above a track or another lane, or to the end.

## 20. A clip's colour is its own (product owner, 2026-10-11; ADR 0043)

- **Why:** the song test found clip and track colours tied together in ways that were hard to follow.
  - A clip moved to another track took the new track's colour, and recolouring a track recoloured its clips; a clip coloured by hand didn't follow, with nothing to tell which did.
  - Switching takes lost a clip's colour: its takes never had it.
  - The product owner wants "initialization, not entanglement".
- **Now:**
  - a new clip takes its track's colour once, when it's made: drawn, inserted, captured or recorded, its takes too;
  - after that, the clip's colour and the track's are independent: recolouring the track leaves its clips, moving a clip keeps its colour, recolouring a clip leaves its track;
  - recolouring a clip recolours its takes as well, so it keeps its colour whichever take it plays;
  - copies keep their source's colour: paste, duplicate, split, bounce, a duplicated track, takes moved to tracks;
  - the palette has no "Track colour" item, and the inspector's CLIP swatch is a plain swatch, like the TRACK one.
- **How it's kept:** every clip stores its colour, take clips too. After any edit that makes clips, `Session::Impl::colourNewClips` (from `settle`) gives a clip without one its track's colour, and a take clip its stack's playing clip's, in the same undo step. A recorded pass is coloured before its laps become takes.
- **Older projects:** clips that followed their track stored no colour. As the project opens, they take their track's colour, and take clips their clip's (or else the track's). The project stays saved, and nothing is added to undo.
- **Assistants:** `set_clips` keeps its arguments; a `null` colour gives each clip its track's colour now, once. `set_track` recolours only the track.
