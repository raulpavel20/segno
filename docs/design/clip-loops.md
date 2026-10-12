# Clip loops: design (M3 step C10)

How a clip repeats: a **Loop** switch on every clip, a **loop** inside its content (a start and a length), and edges in the arrangement that **go as far as you drag them**, repeating the loop with every repeat marked. The product owner asked for it before the effects go on, "just like Ableton". The decision is [ADR 0048](../decisions/0048-clips-loop.md) (§15). It builds on:
- one kind of track and clips that never overlap ([ADR 0019](../decisions/0019-one-kind-of-track.md)): extending a clip cuts what it covers;
- takes on clips ([ADR 0032](../decisions/0032-takes-belong-to-clips.md)) and comping ([ADR 0038](../decisions/0038-comping-you-can-listen-through.md));
- fades and crossfades at the boundary ([ADR 0036](../decisions/0036-crossfades-at-the-boundary.md), [fades.md](fades.md)): every edit is click-free;
- each editor's grid ([ADR 0047](../decisions/0047-the-editing-grid-in-each-editor.md)) and the time-view kit ([ADR 0023](../decisions/0023-ui-foundations.md), [ui-foundations.md](ui-foundations.md));
- the menu bar as the complete list ([ADR 0040](../decisions/0040-a-menu-bar-and-everything-by-mouse.md), [menu-bar.md](menu-bar.md) §15) and assistants' tools ([ADR 0021](../decisions/0021-mcp-server.md), [mcp.md](mcp.md));
- the clip editing of [daw-shell.md](daw-shell.md) ("Clip editing (C4)", "Piano roll").

**Status:** approved by the product owner (2026-10-12), with every recommendation of §14; the product owner chose the audio clip view (L16). Built 2026-10-12, for review; what building settled is in §17.

## 1. Why

- A loop is how most electronic music is built: a one-bar beat, a four-bar chord pattern, a sampled phrase, repeated for as long as the section needs. Today the only way is Duplicate (Ctrl+D), which makes copies that drift apart when one is edited.
- An audio clip stops at its file's end (daw-shell.md: "trims stay within the source file"). A MIDI clip extends into empty space.
- Every DAW in the field has looping clips. Without it, Segno is missing something users expect from day one.

## 2. Where the field is (2026)

- **Live:**
  - Each clip has a Loop switch, with Loop Position and Loop Length. The loop length starts as the clip's length.
  - The clip view shows the loop as a **loop brace** whose markers you drag. The keys nudge it, and Ctrl+↑/↓ double or halve it.
  - A clip plays from its start marker, so it can run into the loop (a lead-in).
  - "Unwarped audio clips cannot be looped", and fades "cannot cross a clip's loop boundaries" ([clip view](https://www.ableton.com/en/manual/clip-view/), [arrangement view](https://www.ableton.com/en/manual/arrangement-view/)).
  - In the arrangement, dragging a looped clip's edge extends it with repeats.
- **Logic:**
  - A region has a Loop parameter.
  - Dragging the **upper part of the region's right edge** turns the pointer into a Loop pointer and repeats the region as far as you drag, while the lower part resizes.
  - "Notches appear at the top and bottom of the region, marking the beginning and end of each complete repetition."
  - Unchecking Loop removes the repeats ([Apple](https://help.apple.com/logicpro/mac/10.4/en.lproj/lgcpf7c0e0db.html)).
- **Bitwig:**
  - Arranger clips have Loop, Start (where each repetition starts) and Length in the Inspector. With Loop off the clip plays once and anything past its content is empty.
  - An I-beam cursor with a loop mark sets the loop length on the timeline. "All of those mouse movements are really just triggering parameter changes in the Inspector" ([Bitwig](https://www.bitwig.com/userguide/latest/the_inspector_panel_on_arranger_clips/)).
- **REAPER:**
  - Loop source is on by default. Extending an item past its source repeats it, with small marks where each repeat begins.
  - "Loop section of item" loops just a part ([Cockos](https://www.cockos.com/wiki/index.php/Loop_Source)).
- **Cubase** repeats events as copies (Repeat, or dragging a handle), not as a loop inside one event ([Steinberg](https://archive.steinberg.help/nuendo/v12/en/cubase_nuendo/topics/parts_events/parts_and_events_repeating_t.html)).

**What Segno takes from each:**
- **One clip that loops, not copies:** Live, Logic, Bitwig and REAPER.
- **Edges that extend with repeats:** all of them.
- **Notches at every repeat:** Logic and REAPER.
- **A brace in the clip view for the loop's start and end:** Live.
- **A field for each value in the inspector:** Bitwig.
- **A one-gesture loop on the right edge's upper part:** Logic. Segno's fade handles sit at the top corners, so the gesture is placed to avoid them.

## 3. Principles

1. **A loop belongs to the clip.** It moves, splits, copies and bounces with the clip, like its fades. It isn't a copy of anything.
2. **Extend where you arrange, shape where you edit.** The arrangement makes a looped clip longer or shorter and changes its loop length. The clip view sets exactly where the loop starts and ends.
3. **What you see plays.** Every repeat is drawn, and every place the loop starts again is marked.
4. **Switching Loop on moves nothing.** The loop is what the clip shows at that moment.
5. **Click-free, as every edit** (fades.md §1).
6. **Everything by mouse, nothing modal.** The loop is a property, so it's in the inspector. Its gestures are on the clip, and the menu bar lists it. Assistants get the same (ADR 0021, ADR 0040).

## 4. What a loop is

A clip's **content** is what it plays from: its audio file, or its MIDI notes. A **loop** is a stretch of that content, a start and a length. A looped clip plays the loop again and again for as long as the clip is. Each pass is a **lap**, and where one lap ends and the next begins is a **seam**.

```
content (a 2-bar file)  |a b c d e f g h|
loop (start c, 1 bar)        [c d e f]
clip on the timeline       |e f|c d e f|c d e f|c d|     ← starts mid-loop (its phase), 4 laps
                                ^       ^       ^          seams
```

- **The phase.** A looped clip can start partway into its loop: that's its *phase*, the content it plays first. Dragging its left edge changes the phase, and the content stays where it is on the timeline.
- **No lead-in.** A looped clip plays from inside the loop. Content before the loop start is never reached, unlike Live's start marker before the loop (L7).
- **Tempo.**
  - A MIDI clip's loop is in beats, so it follows the tempo.
  - An audio clip's loop is in its file's seconds, like audio clips today, until M4's warping makes audio follow the tempo.
  - Audio is placed at the file's own speed. A file's loop metadata no longer makes the engine stretch it on its own (L8).

## 5. In the arrangement

```
 ┌ Drums ⟲ ────▾──────────▾──────────▾──────┐   ▾ a notch at each seam
 │ ▁▃▇▅▂▁▃▇▆▃ ┊ ▁▃▇▅▂▁▃▇▆▃ ┊ ▁▃▇▅▂▁▃▇▆▃ ┊ ▁▃▇ │   ┊ a thin line through the body
 └──────────────────────────────────────────┘   ⟲ the loop mark (Loop on)
```

- **Every lap is drawn the same:** waveform, notes and fades, with the name once at the start.
- **The loop points are clear:**
  - A **notch** is cut into the title strip at each seam, and a **thin line** runs through the body.
  - While Loop is on, a small **loop mark** (`Icon::Loop`) sits at the end of the title strip, before the takes badge if there is one. It shows that the clip loops even before it has a second lap.
  - Nothing else is added: the name, colour and outline are as for any clip.
- **Edges.** A looped clip's edges go as far as you drag them, either way.
  - **The right edge** adds or removes laps.
  - **The left edge** reveals or hides earlier laps. The content stays put on the timeline; only where the clip begins changes.
  - Both snap to the arrangement's grid; Alt drags freely, as everywhere.
  - What the clip covers is cut when you let go, as for any clip (ADR 0019).
  - A **value tag** while dragging says what you'll get, e.g. "4 bars · 4 loops".
- **Changing the loop length.** Drag any notch.
  - The loop's length follows, snapped to whole grid steps, and every lap reflows. The first lap stays where it is.
  - The tag reads "Loop 1 bar".
  - The cursor over a notch is the left-right arrow, with the tooltip "Drag to change the loop's length".
- **Looping in one gesture.** On a clip that doesn't loop yet, the **upper part of its right edge** shows the loop cursor (an arrow with the loop mark). The upper part is the title strip or the edge's upper half, whichever is taller.
  - Dragging it right switches Loop on and repeats the clip as far as you drag. The loop is what the clip showed (§8).
  - It never shortens a clip. Letting go where you started changes nothing.
  - The lower part of the edge trims, as now. Where the fade-out handle shows, the handle wins in its corner.
  - It acts on the selected clips, as a trim does. Clips that can't loop (§8) are trimmed instead.
- **The loop cursor** is a new shared cursor in `fd_ui` (`ui::cursorFor(Icon::Loop)`), for anything that drags a loop.

## 6. In the clip view

```
 ┌ Clip ─────────── ● Snap  1/16 ⌄ ───────────────────────────────────────┐
 │ ░░░░░░░░░[■■■■■■■■■■■■■■■■■■■■ loop ■■■■■■■■■■■■■■■■]░░░░░░░░░░░░░░░ │ ← loop strip (12 px)
 │ 1           2           3           4           5           6          │ ← the song's bars
 ├────────────────────────────────────────────────────────────────────────┤
 │ shaded      notes or the waveform, as they play       shaded (faded)    │
 └────────────────────────────────────────────────────────────────────────┘
```

- **The loop strip and its brace**, above the clip view's bar numbers, work exactly like the transport's brace in the arrangement (daw-shell.md, "Loop brace"):
  - **Drag on an empty part of the strip:** draws a loop, snapped to the editor's grid, and switches Loop on.
  - **Drag the brace's edges:** sets the loop's start or end.
  - **Drag the body:** moves the loop and keeps its length.
  - **Click the brace:** switches Loop on or off. While Loop is off, the brace shows in `loopInactive`, where the clip would loop, and a drag inside it draws a loop (§17).
  - Alt drags freely.
- **One brace, three places.** The brace's arithmetic becomes one pure `model::BraceDrag` (the zones, the drag, what letting go does), with `[fast]` tests. The arrangement's transport brace and both clip views use it.
- **Outside the loop** the grid is shaded and the content faded, as outside the clip today (daw-shell.md, "Piano roll"). Notes there are kept, but don't play.
- **The frame.** The clip view shows the lap the clip starts in, at its place in the song, so the song's bar numbers line up.
  - If that lap would begin before the song's start, the view moves to the next lap that doesn't.
  - In the usual case (a loop on bar lines, starting at the clip's start) the view looks as it does today.
  - Opening a looped clip fits the view to the loop.
- **The playhead** wraps within the loop while the song plays the clip's later laps, so it always shows what's playing.
- **MIDI clips:** the piano roll, as now, with the strip added above its ruler.
- **Audio clips: a new audio clip view** (the product owner's choice, L16) in the Clip tab when one audio clip is selected. A double-click on an audio clip opens it, as for MIDI.
  - It shows the clip's file as a waveform, at its place in the song, with the same ruler, loop strip, brace, shading and playhead.
  - It zooms and scrolls with the time-view kit, like the piano roll.
  - It edits nothing else yet. It's the home of M4's warping.
  - Its header has Snap and the step, as the piano roll's has. It uses the clip view's grid, the piano roll's in ADR 0047, which already serves whichever clip the view shows, so there's no third grid.

## 7. In the inspector

The CLIP section gains three rows, after Offset:

| Row | Control |
|---|---|
| **Loop** | A toggle (`ui::Toggle`), on or off. Dimmed with the reason when the clip can't loop (§8) |
| **Loop start** | A field in bars and beats of the content ("1.3.1"), dragged in grid steps, or typed: "2.1.1", "3 beats", and for audio "1.5 s" |
| **Loop length** | A field: "1 bar", "2.0.0", "6 beats", and for audio "1.5 s" |

- The fields show while Loop is on. With Loop off, only the toggle shows (nothing without a reason, ui-foundations "Layout quality").
- **Offset** shows where in the content the clip starts. For a looped clip that's inside the loop.
- **Parsing is shared.** Bars and beats are read and written by one JUCE-free `model/MusicalTime`, shared with the assistants' tools (it moves out of `mcp/Conventions.h`).
- **Rows are placed by name.** The CLIP rows place their controls by a key in `ui::Property` instead of by their row number, so rows can come and go.

## 8. Switching Loop on and off

- **On:** the loop is what the clip shows. It starts where the clip's content starts and is as long as the clip. For audio it never runs past the file's end. Nothing moves or sounds different until you extend the clip or change the loop.
- **Off:** the repeats go. The clip ends where its first lap ends and keeps playing from the same point (Logic's rule; L3). Undo brings the repeats back.
- **Drawing a loop in the clip view** on a clip that doesn't loop switches Loop on with that loop. If the content at the clip's start is inside the new loop, the clip keeps starting there; otherwise it starts at the loop's start. The clip's length doesn't change.
- **Clips that can't loop**, with the reason on the dimmed toggle, the menu item and the assistants' error:
  - **A clip with two takes or more.** A loop belongs to one piece of content, and a comp switches content along the clip. "Keep only this take" first. This covers a recording's "Before take" and a bounce's source too.
  - **An audio clip the engine already stretches to the tempo**, from a project made before this change. Looping it comes with M4's warping.
  - **An audio clip whose file is missing.**
- **A clip part of a take stack with one take** leaves the stack when it starts to loop, in the same undo step.

## 9. With the rest of Segno

**Fades and crossfades (fades.md):**
- **The clip's fade-in and fade-out** stay at its outer edges, and may span laps.
- **Seams are click-free.** Each seam counts as a pair of edges for the 4 ms edge-fade rule (fades.md §2):
  - the lap's end is a cut edge if the loop ends before the file does;
  - the next lap's start is a cut edge if the loop starts after the file's start.
  - A loop of the whole file wraps without a fade, as a file's own start and end aren't faded.
  - Edge fades off turns these off too.
- **Outer edges** follow the same rule, by what the clip plays there: a clip that starts at its file's start, or ends at its end, has no edge fade there.
- **Crossfades.** A looped clip's spare audio past its edges is its own neighbouring lap. A crossfade out of a looped clip plays its next lap under the fade.

**Takes (ADR 0032, 0038).** A take keeps the loop it was made with. Choosing a bounce's source take plays it looped, as it was.

**Other edits:**
- **Split** gives both parts the same loop; the right part starts where the split fell in the loop, and the join plays on seamlessly.
- **Duplicate, copy, paste and cut-by-another-clip** keep the loop.
- **Bounce in place and export** play it as it sounds.
- **Mute and colour** are unaffected.

**Notes (daw-shell.md, B15).** In a looped clip, notes drawn or moved past the loop's end lengthen the loop to the end of that bar, in the same undo step. The clip's length in the arrangement stays.

**Recording:**
- A pass over a looped clip cuts it as it cuts any clip (ADR 0037).
- MIDI overdub doesn't merge into a looped clip. The pass makes its own clip, as it does without overdub. Folding what's recorded into the loop, as Live does, waits until asked (L12).

**The arranger track (C3, to come)** splits looped clips at section edges like any clip.

## 10. Menus, keys and actions (ADR 0040)

| Where | What |
|---|---|
| **Clip menu** | **Loop clips**, after Mute clips: a toggle, ticked when every selected clip loops, dimmed with the reason when none can |
| **Clip right-click menu** | **Loop**, after Mute |
| **Inspector** | Loop, Loop start, Loop length (§7) |
| **On the clip** | Edges, notches and the loop-in-one-gesture (§5) |
| **Clip view** | The brace (§6) |

- **No default shortcut.** Ctrl+L is the transport's Loop (and Live's Loop Selection). Every L combination is taken, and plain L is a note on the computer keyboard. Loop clips is assignable in Settings > Keys (L14).
- The command palette lists it, as it lists every action.

## 11. Assistants (MCP)

| Tool | Arguments | Does |
|---|---|---|
| `loop_clips` | `clips`, `loop?` (true), `start?`, `length?` | Switches Loop on or off. `start` is a content position (beats from the content's start, or "2.1.1"). `length` is in beats, "1 bar", "4 beats", or for audio "1.5 s". It refuses with the reason for clips that can't loop |
| `trim_clip` | as now | A looped clip's start and end go anywhere. "An audio clip can't extend past its file" applies only to clips that don't loop |

- Clip results and `segno://clip/{id}` gain `loop`: `null`, or `{start, length}` in beats of the content, with `startSeconds` and `lengthSeconds` for audio.
- In a looped clip, notes count from the loop's start. Writing notes past the loop's end returns `loopLengthened`.
- One call is one undo step: "Assistant: loop clips".

## 12. Model and engine

- **The model.** `model::ClipInfo` gains `loop {start, length}` (length 0: no loop), in seconds of the content for both kinds, like the clip's own times. MIDI's loop, kept in beats by the engine, is converted at the Session boundary.
- **One pure module**, `app/src/model/ClipLoops.{h,cpp}`, with `[fast]` tests:
  - `contentAt`: what a time in the clip plays;
  - `laps` and `seams`: each lap's place on the timeline and in the content;
  - `seamAt`: hit-testing;
  - `loopOn`, `loopOff` and `withLoop` (§8);
  - `whyCantLoop`;
  - `lapNotes`: notes cut at the loop's edges, as the engine plays them;
  - `lengthenedLoop` (§9);
  - `clipViewOrigin` (§6).

  Drawing, fades, takes, trims and the tools all go through it.
- **The engine is tracktion's own loop.** Audio loops in seconds (`setLoopRange`, the tempo sync off) and MIDI in beats (`setLoopRangeBeats`). Projects save it with no format change, and older projects have no loops.
- **Where the old meaning of offset is assumed.** While a clip loops, the engine's offset is the phase. About twenty places read offset as "seconds into the file": trims, edge fades, seamless joins, takes, waveforms, notes and the tools. They move to `contentAt`.
  - One Session helper keeps the phase within the loop at every write.
  - Segno switches a loop off itself, so the phase is kept (the engine's own way drops it).
- **Click-free seams.** The seam fades are periodic data in the clip's playback (`ClipPlayback`), applied by the same fade node as edge fades, with no allocation.
- **Spare audio.** A looped clip's spare audio for crossfades is the engine's own next lap. The playback hook already allows it; only Segno's rule changes.
- **The audio clip view** is a new view in the detail panel, drawn with the waveform tiles and the time-view kit.
- **New in `fd_ui`:** the loop cursor and a notch size metric (`shell.loopNotch`), each listed in ui-foundations.md.

## 13. Tests (Done when)

- **`[fast]`:**
  - `ClipLoops`:
    - laps and seams at the edges;
    - an unnormalised phase;
    - on and off round trips that move nothing;
    - notes cut at the loop's edges as the engine cuts them;
    - lengthening a loop to the bar;
    - the clip-view frame;
  - `BraceDrag`;
  - `MusicalTime` parsing;
  - drags that extend a looped clip, change its loop length or loop it in one gesture;
  - edge fades at seams;
  - seamless joins of a split looped clip.
- **Engine** (rendered samples, not just state):
  - a looped audio clip plays `file[loopStart + ((phase + t) mod length)]` sample for sample, at phase 0 and mid-loop, over 3.5 laps;
  - a looped MIDI clip's notes land on every lap;
  - a split looped clip nulls against the unsplit one;
  - a bounce of a looped clip matches its playback;
  - a seam whose loop ends inside the file is smooth, and a whole-file loop is unfaded;
  - a crossfade out of a looped clip plays its next lap.
- **Session:** on, off and undo; save and reopen; clips that can't loop are refused; takes keep their loops; notes past the loop's end lengthen it; overdub doesn't merge into a loop.
- **The shell:**
  - mouse gestures: the edges, a notch, the loop in one gesture, the brace in both clip views;
  - the menu item and the right-click item;
  - the inspector rows;
  - screenshots, zoomed in, at standard and compact widths: notches on short and long laps, the narrowest looped clip, the loop mark beside the takes badge, both clip views, and the inspector at 1024 px.
- **MCP:** `loop_clips`, `trim_clip` past the file, notes counted from the loop's start, and undo names.
- **Speed:** a 64-lap clip in the arrangement benchmark.
- **By hand** (manual-tests/M3.md, C10):
  - drag a drum loop to 16 bars, and loop a clip in one gesture;
  - drag a notch;
  - set the loop in the piano roll and in the audio clip view;
  - split, duplicate and bounce a looped clip;
  - crossfade out of one;
  - change the tempo;
  - save, reopen and undo.

## 14. Questions and recommendations

| # | Question | Recommendation |
|---|---|---|
| L1 | **Where the loop is set and extended** | Extended in the arrangement (edges, notches). Its start and end are set in the clip view's brace. All three values are in the inspector (§5–§7) |
| L2 | **What switching Loop on does** | Loops what the clip shows now: nothing moves or sounds different (Live's default length, Logic's region loop) |
| L3 | **What switching Loop off does** | The repeats go, and the clip ends at its first lap's end (Logic). Live keeps the clip's length and plays past the loop, which leaves silence after a short MIDI loop and a file's end after audio. Undo brings the repeats back |
| L4 | **How the loop points show** | A notch in the title strip and a thin line through the body at every seam (Logic's notches, REAPER's marks), plus a loop mark on the title strip while Loop is on. Final look settled in the screenshot review |
| L5 | **Changing the loop's length in the arrangement** | Drag any notch, snapped to grid steps; the repeats reflow (Bitwig's loop cursor, on the marks you already see) |
| L6 | **Looping without opening anything** | The upper part of a clip's right edge loops it as you drag (Logic). It's placed to stay clear of the fade handles; the lower part still trims |
| L7 | **A lead-in before the loop (Live's start marker)** | No. A looped clip starts inside its loop (Bitwig, Logic). A lead-in is a short clip before it. The engine can't play one, and it would add a third marker to learn |
| L8 | **Audio loops and the tempo** | MIDI loops follow the tempo (beats). Audio loops are file seconds until M4's warping, and audio is placed at the file's own speed as browser.md R8 already promised. Live requires warping to loop audio. Segno loops it at its own speed now, and warping makes it follow the tempo later |
| L9 | **Clicks at seams** | Seams follow the 4 ms edge-fade rule: faded where the loop cuts into the file, not where the file itself starts and ends (§9) |
| L10 | **Crossfading a looped clip** | Its spare audio is its own next lap, so a crossfade out of a loop works as for any clip |
| L11 | **Takes** | A clip with two takes or more can't loop. Keep only this take first. A loop belongs to one piece of content |
| L12 | **MIDI overdub into a looped clip** | The pass makes its own clip, as without overdub. Folding recorded notes into the loop (Live) when asked |
| L13 | **Notes past a looped clip's loop end** | Lengthen the loop to that bar (the B15 rule, applied to the loop); the clip's length stays |
| L14 | **A shortcut for Loop clips** | None by default. Ctrl+L and every L combination are taken, and plain L is a computer-keyboard note. It can be assigned in Settings > Keys. If one is wanted: Ctrl+J, which is free |
| L15 | **Loop on by default for some clips** | Off for every clip, so nothing changes until asked. Possibly later: files the browser knows are loops at the song's tempo start looped |
| L16 | **An audio clip view** | **Chosen by the product owner (2026-10-12):** yes. The file's waveform with the same ruler, brace and playhead as the piano roll, editing nothing else yet (§6) |
| L17 | **The tools** | `loop_clips(clips, loop?, start?, length?)` beside `mute_clips`. `loop` in clip results. `trim_clip` past the file for looped clips (§11) |

## 15. ADR 0048: Clips loop

[ADR 0048](../decisions/0048-clips-loop.md):
- Any clip can loop a part of its content: a start and a length, with Loop on or off.
- A looped clip's edges go as far as dragged. Every repeat is drawn and marked with a notch.
- Notches change the loop's length in the arrangement. The clip view's brace sets its start and end. The inspector has all three.
- Seams follow the edge-fade rule.
- A clip with takes, or an audio clip already stretched to the tempo, can't loop.
- It amends daw-shell.md's "trims stay within the source file" (not for a looped clip) and fades.md §2 (seams are edges).

## 16. What comes when

- **C10 (M3):** everything above.
- **When asked:**
  - folding MIDI overdub into a loop;
  - a lead-in before the loop;
  - files known to be loops starting looped;
  - Live's keys for the brace (nudge, double, halve);
  - Crop to the loop;
  - a shortcut for Loop clips.
- **M4 (warping):** audio loops in beats that follow the tempo, and the audio clip view's warp markers. When a clip gets a time-stretch mode, its loop must survive: the engine drops loops then, so this is an open question.

## 17. Settled while building (2026-10-12)

- **Audio loops are whole samples of their file.** The engine wraps a loop in whole samples, so a loop that isn't a whole number of them drifted a fraction of a sample each lap from where its seams are drawn and faded (a 0.3013 s loop: 0.4 samples a lap). Loops are written in whole samples, so what's drawn is exactly where it wraps.
- **A seam's fades reach where they reach.** Every seam whose fades reach into what a clip plays applies, so a looped clip split a sample beside a seam (on the grid, where the seam is a fraction off it) fades as the whole did, instead of fading out and starting again at full level. A seam right on a clip's edge is that edge's fade. A split looped clip nulls against the whole, inside a lap, beside a seam and on one.
- **While Loop is off, a drag inside the brace draws a loop.** The brace shows what the clip would loop (itself), so every press over the clip landed on it; there's no loop to move, so a drag there draws one. Its edges still set a loop from the clip, and a click switches Loop on.
- **The B15 rule for loops looks at the notes an edit writes**, not every note: a loop over part of a longer phrase isn't stretched by editing a note inside it.
- **Overdub:** a pass doesn't merge when any armed track has a looped MIDI clip (the engine merges into whatever clip a pass plays over, so the pass is told before it starts). The pass makes its own clip and cuts the looped one.
- **The inspector's Loop toggle reads "On"**, as Settings' switches do. The fields drag in sixteenths, and typed values land on a sixteenth, "1.5 s" too.
- **Bars and beats as text** (`model/MusicalTime`) are shared by the inspector, the value tags and the assistants' tools (moved from the tools' conventions). Beats are quarter notes everywhere, as the tools' already were.
- **Notches keep clear of the name:** a notch that would cut into it isn't drawn; the line through the body still marks the seam.
- **The loop mark is drawn with its clip**, so a drag's preview shows it too.
- **The audio clip view keeps its own waveform cache** (one file at a time): the detail panel is made before the arrangement whose cache it would share.
- **Clips with loop metadata** (an ACID chunk) were stretched to the tempo by the engine when placed; they're placed at their file's speed now, as browser.md R8 said.
- **Speed:** 32 tracks of 64-lap loops paint as fast as 32 tracks of 8 clips over the same span (the arrangement benchmark).
