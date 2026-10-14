# The piano roll: design (M3 step P4)

A piano roll that's good to write music in, not only to fix notes: it writes in the song's key, writes and re-voices chords, shows the other tracks' notes, edits each note's properties and expression, and has a modern set of tools to transform and generate notes. It builds on:
- today's piano roll ([daw-shell.md](daw-shell.md), "Piano roll"), the editing grid ([ADR 0047](../decisions/0047-the-editing-grid-in-each-editor.md)) and clip loops ([ADR 0048](../decisions/0048-clips-loop.md));
- the song's key ([ADR 0057](../decisions/0057-the-song-has-one-key.md), [key-and-scale.md](key-and-scale.md)) and the theory in `libs/music`;
- one selection and the context panels ([ADR 0027](../decisions/0027-selection-and-context-panels.md), [selection.md](selection.md)), and automation's gestures ([automation.md](automation.md));
- the menu bar as the complete list ([ADR 0040](../decisions/0040-a-menu-bar-and-everything-by-mouse.md)) and assistants' tools ([ADR 0021](../decisions/0021-mcp-server.md));
- the UI foundations ([ADR 0023](../decisions/0023-ui-foundations.md)): every panel sized from its content, nothing touching.

**Status:** approved by the product owner (2026-10-14), with every recommendation of §17 (N1–N19) and the stages of §16; the decision is [ADR 0058](../decisions/0058-the-piano-roll-writes-in-key-and-in-chords.md). A visual mockup of the clip view came first and was approved (2026-10-14). Each stage is then built and signed off on its own. **P4a built (2026-10-14), for review:** what building settled is §19. **Revised after the product owner's first use** (§20: the key's rows, chords chosen where they're written, Make chords… chord by chord), approved and built the same day.

## 1. Why

- The product owner (2026-10-13): "Feature-poor today. Key and scale aware; chords; turning root notes into chords of various kinds that fit the key; research the market, combine and innovate."
- And (2026-10-14): "a more modern set of features to make the piano roll actually good and up to standard."
- **Today** it draws, selects, moves and resizes notes, quantises to the grid, duplicates, transposes and nudges by keys, and has a velocity lane. Since P3, its rows show the song's key. It has no chords, no tools for the key, nothing about other tracks, no per-note properties beyond velocity, no MIDI controllers (recorded pitch bend and sustain are kept but can't be seen), and no tools that transform or generate notes.
- The piano roll is where most of a song's notes are written. Users judge a DAW by it: FL Studio's is the one named "the best" in forums, for being quick with the mouse, its ghost notes, chord stamps and strum.

## 2. Where the field is (2026)

Checked against the vendors' manuals and release notes in October 2026: Live 12.4, Bitwig 6.0, Logic 12.4, Cubase 15, Fender Studio Pro 8, FL Studio 2026, REAPER 7.82.

| | Live 12 | Bitwig 6 | Logic 12 | Cubase 15 | Studio Pro 8 | FL Studio 2026 | REAPER 7 |
|---|---|---|---|---|---|---|---|
| A song-wide key | per clip | ✓ project key | chord track | chord track | chord track | per editor | per editor |
| Fold to the key / to used notes | ✓ / ✓ | – / ✓ | – | ✓ / ✓ | ✓ / ✓ | – | – / ✓ |
| Snap pitch to the key while drawing | – | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ |
| Move by steps of the scale | ✓ (a tool) | ✓ arrows | – | – | ✓ arrows | – | ✓ (a dialog) |
| Fit notes to the key | ✓ | ✓ (smart) | ✓ | ✓ | ✓ | – | ✓ |
| Chords stamped in the editor | Stacks | – | – | ✓ | – | ✓ voice-led | – |
| Next-chord suggestions | – | – | – | ✓ | ✓ | ✓ | – |
| Chord names shown | – | – | ✓ Chord ID | ✓ | ✓ | ✓ | ✓ (notation) |
| Inversions, drop voicings | inversion | – | – | ✓ | – | ✓ | inversion |
| Strum | ✓ | – | – | – | ✓ | ✓ | – |
| Per-note chance | ✓ | ✓ + conditions | – | ✓ | – | – | – |
| Velocity spread per note | ✓ | ✓ | – | ✓ | – | – | – |
| Release velocity | ✓ | ✓ | ✓ | – | – | ✓ | ✓ |
| Other tracks' notes shown | multi-clip | ✓ layers | – | – | ✓ | ✓ ghosts | ✓ |
| Tools that transform and generate | ✓ MIDI Tools | operators, Note FX | Session Players | Logical Editor | Action menu | ✓ tools, scripts | scripts |
| Stretching a selection in time | ✓ | – | ✓ time handles | – | – | ✓ | – |

(– : not found in the documentation read; not proof it's missing.)

- **Writing in key:** Bitwig 6 has the newest and best-reviewed model, and it's ours: one key for the project. Under Snap to Key its pencil only draws the key's notes, the arrows move by steps of the scale, and Quantize to Key moves notes in, choosing up or down by their context ([Bitwig](https://downloads.bitwig.com/6.0/Release-Notes-6.0.pdf)). Studio Pro 7.1 makes its arrows follow the scale too. Live folds to the scale (G) or to the notes used (F), and has Fit to Scale ([Ableton](https://www.ableton.com/en/live-manual/12/editing-midi/)).
- **Chords:**
  - **FL Studio** stamps chords on a click. In 2026 it builds the chord either under the note clicked (the melody stays on top) or over it, from the scale and the chords around it, voice-led ([Image-Line](https://www.image-line.com/fl-studio-learning/fl-studio-online-manual/html/basics_new.htm)). Its Chord Progression tool writes whole progressions, with voicings and performance patterns.
  - **Cubase**'s Key Editor turns notes into triads and four-note chords, inverts them, and makes drop-2, drop-3 and drop-2+4 voicings. Its Chord Assistant suggests chords "closer or further" from the one before ([Steinberg](https://steinberg.help/nuendo/v12/en/cubase_nuendo/topics/chord_pads/chord_pads_chord_assistant_proximity_c.html)).
  - **Logic 12** names chords in audio and MIDI (Chord ID). **Studio Pro 8** fills in the next chord from a model of thousands of progressions.
  - **Nobody does all of it in the piano roll with one song key:** chord names over the notes, chords written on a click in the key, and chord-to-chord suggestions, voice-led. Plugins (Scaler 3, Captain Chords) do it outside the DAW, against a key you set again in each.
- **Tools:** Live 12's MIDI Tools are the template: transformations (Arpeggiate, Chop, Connect, Ornament, Quantize, Recombine, Span, Strum, Time Warp, Velocity Shaper) and generators (Rhythm, Seed, Shape, Stacks, Euclidean), set with parameters and re-editable while the notes stay selected ([Ableton](https://www.ableton.com/en/live-manual/12/midi-tools/)). Its users ask for a cleaner undo and for tools that don't just bake in. FL's tools menu (Riff machine, Arpeggiate, Strum, Flam, Chop, Randomize in scale) and Cubase's Logical Editor are the others users praise.
- **Per-note properties:** chance is now expected: Live (with Play All / Play One groups), Bitwig (with conditions: first time, every Nth loop, fills) and Cubase 14 (Play Probability, Velocity Variance) ([Bitwig](https://www.bitwig.com/userguide/latest/operators)). Release velocity is common.
- **Seeing other parts:** FL's ghost notes, Bitwig 6's layered editing and Studio Pro's multi-part editing show other clips' notes while you write; Live users ask for it.
- **Ergonomics:** Bitwig 6 added a tool palette (pencil, spray can, knife, eraser, audition, step input). Logic and Live stretch a selection in time with handles; Cubase draws velocity in lines, curves and waves.

**What this means for Segno:** the song's key, already shared by everything, lets the piano roll do what the best of them do (write in key, fit to the key, fold) and what none of them does in one place: name the chords over the notes in the key, and write and re-voice them there, with suggestions for the next one. Add what users now expect (ghost notes, chance, release velocity, controller lanes, time handles, transform and generate tools) the way Segno does things: no modal tools, one undo step per gesture or tool, every command in the menu bar, and assistants able to do all of it.

## 3. Principles

1. **One surface, no modes you can get lost in.** Gestures do the common things, as now (double-click draws, drag moves, edges resize). There's no tool palette to switch; a modifier changes a gesture while it's held. What a click draws (a note or a chord) is a view option you can see in the header.
2. **In key by default, the whole roll one click away.** With a song key, the clip view shows only the key's rows, so every click lands on one of its notes and the arrows move by steps of the scale; one toggle shows the whole roll, chromatic. Without a key, everything is chromatic, as today. A click always lands on the row under the pointer (§21).
3. **Chords are notes.** There's no chord track to keep in step: chord names are read from the notes, and editing a chord rewrites its notes. The notes are always the truth.
4. **A tool is one undo step, previewed before it's kept.** Tools with settings open a popover that changes the notes live as you set it; Apply keeps it as one step, Escape puts everything back.
5. **The same gestures as everywhere else:** the velocity lane and controller lanes are edited like automation lanes; Ctrl+drag copies, as it does clips and sections.
6. **What it plays is what it renders.** A note's chance is decided the same way in playback, bounce and export, so a mix never changes between them.
7. **Theory in one place.** Chords, voicings, voice leading, chord names and suggestions live in `libs/music`, shared with the Push, the Chord and Arpeggiator modules (M4) and assistants.
8. **Every panel sized to its content,** nothing touching, at standard and compact widths; screenshots reviewed zoomed in before sign-off.

## 4. The clip view

```
┌ Modules · Clip ── Notes ⌄  Quantise   Draw chord ⌄ [Triad 7th 9th sus2 sus4 add9] Smooth ⌄  ● In key  ○ Others  ● Snap  1/16 ⌄ ┐
├──────────┬──────────────────────────────────────────────────────────────────────────────────────────┤
│          │ ⟦ loop brace ⟧                                                                            │ loop strip
│          │ 1          2          3           4          5                                           │ ruler (time handles, §9)
│  CHORDS  │ Am         │ F          │ C          │ G7         │                                       │ the chord lane (§7)
├──────────┼──────────────────────────────────────────────────────────────────────────────────────────┤
│   C4     │ ▬▬▬▬▬▬▬▬▬▬ ▬▬▬▬▬▬▬▬▬▬ ▬▬▬▬▬▬▬▬▬▬                                                         │ the notes, rows by key
│  B3      │            ░░░░                     ← another track's notes, faint (§8)                  │
│ A3 ●     │ ▬▬▬▬▬▬▬▬▬▬            ▬▬▬▬▬▬▬▬▬▬ ▬▬▬▬▬▬▬▬▬▬                                              │
│   …      │                                                                                            │
├──────────┼──────────────────────────────────────────────────────────────────────────────────────────┤
│Velocity ⌄│ ▮      ▮    ▮      ▮        ▮                                                             │ the lane (§11): Velocity, Chance,
└──────────┴──────────────────────────────────────────────────────────────────────────────────────────┘ Release, Pitch bend, Mod wheel…
```

- **The header** keeps its tabs on the left and, on the right, its commands, then the view's options, in today's look (`ViewOption`, `ViewChoice`, a command as a `Toggle`):
  - **Notes ⌄** (the product owner's review of the mockup, 2026-10-14: every tool in the working area): a menu button, a command's look with a chevron, that opens the whole Notes menu (§13) under it. It's built by the same function as the menu bar's Notes, so the two never differ. A new shared component, `ui::MenuButton`, with ui-foundations.md;
  - **Draw** (§7.2): what a double-click draws, a note or the key's chord on it; while it draws chords, their kind side by side and their voicing beside it (§20.2);
  - **In key** (§6, §21): only the key's rows, or the whole roll; shown while the song has a key;
  - **Others** (§8): the other tracks' notes, faint;
  - **Snap** and the grid's step, as today; **Quantise**, as today, beside Notes.
  - Hidden options take no room. At compact widths the header drops options in a set order (Others, then the voicing, then Draw), each still in the View menu; In key and the kinds while drawing chords stay.
- **The chord lane** (§7) sits under the ruler: the chord names over the notes, and where chords are written.
- **The bottom lane** shows velocity by default; its chooser (`Velocity ⌄`, in the keys' column) switches to chance, release velocity or a controller (§10, §11).
- **The inspector** gets a NOTES section for the selected notes (§9.6).

## 5. Selection, as before and more

- Click, Shift/Ctrl+click and box selection, as now. **Clicking a chord in the chord lane selects its notes**, so every command applies to a chord at once.
- **Select ▸** in the Notes menu: same pitch, outside the key, in the loop, invert selection. Edit ▸ Select all (Ctrl+A) selects every note, as now.
- Commands act on the selected notes, or on all of them when none are selected, as Quantise does today.

## 6. Writing in key

With a song key ([ADR 0057](../decisions/0057-the-song-has-one-key.md)), revised in §21:
- **In key** (a view option, on whenever the song has a key; turned off for a clip, it stays off for that clip): the rows are only the key's notes, each labelled with its note and step ("A3 · 1"), white or black as on the piano. A row is a step of the key: a double-click draws on the row clicked, and a drag moves notes through the key's notes. **Off:** the whole roll, the piano's rows and nothing more, chromatic. Either way a click lands on the row under the pointer; nothing snaps to the key.
- **The arrows** move the selection by steps of the scale while In key is on (a C in C major goes to D, then E); **Ctrl+↑/↓** by a semitone, which is how a note leaves the key without leaving the view; **Shift+↑/↓** by an octave, as today. In the whole roll, ↑/↓ move by a semitone, as today. (Alt+↑/↓ stay with takes.)
- **Fit to key** (Notes menu): every selected note outside the key moves to the nearest note of the key. At an equal distance it goes the way its melody was moving (up after a rising step, down after a falling one), else down.
- **Outside the key:** In key, a pitch with notes outside the key keeps its row among the key's, hatched and named without a step, so nothing that sounds is hidden (§21.2). "Select ▸ Outside the key" finds them; Fit to key moves them.
- **Hearing:** clicking keys, drawing and moving play the notes, as today.

## 7. Chords

### 7.1 The chord lane

```
│  CHORDS  │ Am · i      │ F · VI      │ C · III     │ G7 · VII7   │            │
```

- **Names, read from the notes:** where three or more notes of different pitch classes sound together, the lane names the chord they make, from `libs/music`: its root and kind ("Am", "Cmaj7", "Dsus4"), a slash for another bass ("C/E"), and its step in the song's key ("vi", "IV", a borrowed chord marked "♭VII"). Spans with the same chord join. Two notes alone, or a melody, get no name.
- **Display:** Names, Steps or both, from the lane's right-click menu (view state). Without a key, names alone.
- **Its look:** a row high (`shell.rowHeight`), shown while the clip has chords or Draw is set to a chord. Each name sits at its span's start in the `Name` style, its step after it in `textSecondary`, cut short with an ellipsis rather than reaching the next span. A thin tick in `line` marks where each span starts.
- **A click on a name** selects that chord's notes (§5). **A double-click** opens the Chord popover to change it (§7.3).
- **Writing chords there:** a double-click in an empty part of the lane opens the Chord popover for that place; the chord chosen is written for the grid's step from the click (drag across the lane first to set a longer span).
- **Changing a chord rewrites only its notes,** keeping their timing and velocities, re-voiced to move as little as possible from the chord before (§7.4). One undo step.
- **Arpeggiated chords** (one note at a time) aren't named in this stage; naming over a window of time comes later (§16).

### 7.2 Drawing chords

- **Draw** (a view choice in the header): **Note** (as now), **Chord**, or **Chord under** (the clicked note on top).
- **Chord:** a double-click draws the key's chord built on the clicked note: a triad by default, or the kind set in the Chord popover (sevenths, ninths, sus). Without a key, a major or minor triad by the note's place in C major, the rule the Wavetable already uses.
- **Chord under:** the clicked note becomes the top voice, and the key's chord with that note on top is built under it. For writing chords under a melody.
- Each drawn chord is voice-led from the chord just before it, unless Voicing says otherwise (§7.4). Its notes are selected, so a drag moves the whole chord.

### 7.3 The Chord popover

```
┌ CHORD AT 3.1 ────────────────────────────── ┐
│ IN THE KEY                                   │
│ [ Am ][ B° ][ C ][ Dm ][ Em ][ F ][ G ]      │  the key's chords on each step (i … VII)
│ NEXT, AFTER F                                │
│ [ C ][ G ][ Dm ][ E ]                        │  suggestions, most usual first
│ BORROWED                                     │
│ [ D ][ E ][ B7 ]                             │  chords from the parallel key, and dominants
├──────────────────────────────────────────────┤
│ Kind      Triad  7th  9th  sus2  sus4  add9  │
│ Voicing   Smooth  Close  Open  Drop 2  Spread│
│ Inversion Root  1st  2nd  3rd                │
└──────────────────────────────────────────────┘
```

- Opened from the chord lane (§7.1), or Notes ▸ Chords ▸ Change chord… for the selected chord (§20.4).
- **In the key:** the chord on each step of the scale, named, in the kind chosen below (for scales other than major and minor, the chords their notes make).
- **Next:** suggestions after the chord before this place, most usual first. They come from a model of common progressions in `libs/music`, kept small and explainable: the usual moves between steps (V to I, ii to V, IV to V…), plus deceptive and plagal moves. It's not a learned model, so every suggestion can be explained.
- **Borrowed:** chords from the parallel key (♭VII, iv, ♭VI in major; IV and V in minor) and the dominant of the next step (V/V), for colour.
- **Hovering a chord plays it** through the track's instrument, voiced as it would be written; a click writes it (one undo step).
- **Kind, Voicing, Inversion** are the popover's own, for the chord there (§20.4): they start as that chord is, and set nothing else. **Smooth** (the default) picks the inversion and octave that move the least from the chord before.
- **Sized to its content**, as the Key popover is.

### 7.4 Making and re-voicing chords

On selected notes (Notes menu; right-click):
- **Make chords…:** each selected note becomes a chord, chosen card by card (§20.3): a kind on the note, a chord of the key with it on top, or the note left alone, each with its voicing. A one-finger line becomes a progression.
- **Invert up / down** (Alt+Shift+↑/↓): the lowest note goes up an octave, or the highest down.
- **Voicing ▸:** Close, Open, Drop 2, Drop 3, Spread; **Smooth voicing** re-voices a run of chords to move as little as possible, keeping the first.
- **Add bass:** the root an octave or two under each chord, for a quick bassline.
- **Strum…** (a tool, §12): spreads each chord's notes in time.

## 8. Other tracks' notes

- **Others** (a view option): the notes of other tracks' MIDI clips at the same time, drawn faint in their tracks' colours, behind the clip's own notes. They can't be selected or moved, so a click never picks the wrong note.
- **Which tracks:** all with notes there by default; its menu picks tracks one by one (view state).
- **Chord names** read the clip's own notes; with Others on, a second line names the chords of everything shown, so a bassline can be written against the pads.
- Later: editing several clips at once (§16).

## 9. Editing

### 9.1 Copy, split, join

- **Ctrl+drag** copies the selected notes (as clips and sections do); **Alt+drag** ignores the grid and the key.
- **Split at playhead** (Edit, Ctrl+E) splits the selected notes crossing the playhead when the clip view was used last, as Delete acts on notes then.
- **Join** (Ctrl+J): selected notes of the same pitch that touch or overlap become one.
- **Legato:** each selected note lasts until the next one starts. **Fixed length:** every selected note one grid step long. **Double** and **Half** speed. **Reverse** (the order in time) and **Mirror pitches** (upside down around the middle, in the key with a key).

### 9.2 Time handles

- With two or more notes selected, the ruler shows their span as a bracket with a handle at each end (as section brackets look in the song row).
- **Dragging a handle stretches** the selection's timing from the other end (snapped); dragging it past the other end reverses the order. **Alt** stretches lengths too.
- One undo step, the stretch shown as a value tag ("× 1.5").

### 9.3 Velocity

- The velocity lane as today, plus **Shift+drag draws a straight line** across the notes it passes: a crescendo in one gesture.
- **Velocity…** (a tool, §12): shape the selected notes' velocities as a ramp, a curve or a wave, compress or expand them, or randomise them a little.

### 9.4 Muting notes

- **Mute** (0): Clip ▸ Mute clips acts on the selected notes when the clip view was used last, and its item then reads "Mute notes". Muted notes stay, drawn hollow, and don't play. tracktion keeps the mute with the note.

### 9.5 Quantise

- **Quantise** (Ctrl+U) as now, with the last settings.
- **Quantise…** (Ctrl+Shift+U): a popover with Strength, Swing, Starts and/or Ends, previewed live. Grooves come with M4's groove work.

### 9.6 The inspector's NOTES section

For the selected notes: **Pitch**, **Start**, **Length**, **Velocity**, **Release velocity**, **Chance** (§10), **Mute**.
- One note: its values, typed or dragged.
- Several: a range ("60 – 72"); dragging moves them all by the same amount, and typing a value sets them all.

## 10. Chance

- **Chance** (0–100 %, per note): how likely the note is to play each time it comes round. A note under 100 % shows it as a thin bar along its top edge, as long as its chance (the whole width at 100 %), and the percentage when the note is wide enough.
- **Velocity spread** (± velocity, per note): its velocity varies by up to that much each time.
- **The same every time it's rendered:** each note's fate is decided from the clip's seed, the note and the lap it's in, never from a running random state, so playback, bounce and export agree. **Re-roll** (Notes menu) gives the clip a new seed.
- **Edited** in the bottom lane's Chance view (as velocity is) and in the inspector. Groups where only one note of several plays (Live's Play One) and conditions (first time, every Nth lap) come later (§16).

## 11. Controller lanes

- **The bottom lane's chooser:** Velocity, Chance, Release velocity, then the controllers the clip has (Pitch bend, Mod wheel, Sustain, Aftertouch, CC n), then "Add controller ▸".
- **Edited as automation lanes are** ([automation.md](automation.md)): double-click adds a point, drag moves, Shift for fine, Alt for free, a line drawn with Shift+drag. Sustain is drawn as steps.
- **Recorded controllers appear** (a sustain pedal held while playing, the pitch wheel): they're already kept in the clip, now seen and edited.
- **They play with the clip,** to our modules and to plugins alike. Automating a module's parameter stays with automation lanes; controllers are what a player's hand does.
- Per-note expression (MPE: pitch, slide, pressure per note) comes with M5.

## 12. Tools that transform and generate

- **Where:** Notes ▸ Transform ▸ and Notes ▸ Generate ▸ in the menu bar; the right-click menu; the command palette.
- **How:** each tool opens a popover by the clip view with its settings, sized to its content. The notes change live as you set it; **Apply** (Return) keeps the result as one undo step, **Escape** puts everything back. Its settings are remembered for next time.

**Transform** (on the selected notes, or all):
- **Humanize:** small random shifts of timing, velocity and length (amounts; a seed).
- **Strum:** spreads each chord's notes (up, down, alternating; time; curve).
- **Arpeggiate:** turns chords into arpeggios (up, down, up-down, played order, random; rate; gate; octaves).
- **Chop:** splits notes into repeats (count or rate; velocity falling or rising).
- **Ornament:** flams and grace notes (one or two before; how far; how loud).
- **Velocity:** ramp, curve, wave, compress or expand, randomise (§9.3).
- **Span:** legato, staccato or a length as a share of the gap to the next note.
- **Randomise pitch:** within a range, in the key with a key.

**Generate** (into the clip's loop, or the selected time; never over notes you keep, unless asked):
- **Rhythm:** a Euclidean pattern (steps, pulses, rotation) on a pitch, or on the chords already there.
- **Melody:** notes in the key over a range, with a density and a seed; follows the chord lane when there are chords.
- **Chords:** a progression in the key (a common one, or from the suggestion model), with a rhythm and voicing.
- **Bassline:** from the chord lane: roots, roots and fifths, or walking, in a rhythm.

## 13. Menus, keys and actions (ADR 0040)

A new **Notes** menu, between Clip and Song: the piano roll's commands have outgrown Clip ▸ Notes ▸. **The same menu opens from the clip view's Notes ⌄ button** (§4), so every tool is in reach where you work. Every item acts on the selected notes (or all), and is dimmed with the reason when the clip view isn't showing a MIDI clip. Related commands sit one level down, so the menu stays short enough to open over the piano roll.

| Item | Shortcut |
|---|---|
| Quantise | Ctrl+U |
| Quantise… | Ctrl+Shift+U |
| — | |
| Transpose ▸ (Up a step ↑, Down a step ↓; Up a semitone Ctrl+↑, Down a semitone Ctrl+↓; Up an octave Shift+↑, Down an octave Shift+↓) | as listed |
| Fit to key | – |
| — | |
| Make chords… (§20.3) | – |
| Chords ▸ (Change chord… (§20.4); Invert up Alt+Shift+↑, Invert down Alt+Shift+↓; Smooth voicing, Close, Open, Drop 2, Drop 3, Spread; Add bass) | as listed |
| — | |
| Join | Ctrl+J |
| Length ▸ (Legato, Fixed length, Double speed, Half speed) | – |
| Reverse | – |
| Mirror pitches | – |
| — | |
| Re-roll chance (P4b) | – |
| — | |
| Transform ▸ (Humanize…, Strum…, Arpeggiate…, Chop…, Ornament…, Velocity…, Span…, Randomise pitch…) | – |
| Generate ▸ (Rhythm…, Melody…, Chords…, Bassline…) | – |
| — | |
| Select ▸ (Same pitch, Outside the key, In the loop, Invert selection) | – |
| Nudge later / earlier | → / ← |

- Clip ▸ Notes ▸ moves here. Edit's Delete, Duplicate, Cut, Copy, Paste, Select all and Split at playhead act on notes when the clip view was used last, and Clip ▸ Mute clips (0) mutes notes then (§9.4): one action each, so no shortcut is doubled.
- **The View menu** gains the clip view's options (Show only the key's notes, Others, Draw ▸), so each is reachable without the header.
- **The right-click menu** on notes offers the most used: Quantise, Fit to key, Make chords…, Chords ▸ (Change chord…, Invert, voicings), Mute, Transform ▸, Select ▸.
- Shortcuts that would clash are checked by the existing test.

## 14. Assistants (MCP)

Each tool is one named undo step, with a test that drives it through MCP (ADR 0021):

| Tool | Does |
|---|---|
| `edit_notes` | Gains `transpose_steps` (steps of the scale), `fit_to_key`, `stretch` (a factor from a point), `legato`, `fixed_length`, `mute`, `chance`, `velocity_spread`, `release_velocity` |
| `get_chords` | The clip's chords as the chord lane names them: span, name, step in the key, notes |
| `set_chord` | Writes or replaces the chord over a span (a name like "Dm7" or a step like "ii7"), voice-led unless a voicing is given |
| `make_chords` | Turns notes into chords: kind, voicing, inversion |
| `suggest_chords` | What the Chord popover offers at a place: in the key, next, borrowed |
| `transform_notes` | A transform of §12 by name, with its settings |
| `generate_notes` | A generator of §12 by name, with its settings |
| `get_controllers` / `set_controllers` | A clip's controller lanes: points by controller |
| `reroll_chance` | A clip's new chance seed, given or random (P4b) |

`get_clip` gains each note's chance, velocity spread, release velocity and mute, and the clip's controllers.

## 15. Model and engine

- **`libs/music`** grows the theory:
  - chord kinds (intervals, names, symbols), the key's chord on each step, Roman numerals;
  - voicings (inversions, drop 2 and 3, open, spread) and voice leading (the inversion and octave that move least);
  - naming a set of notes (root, kind, bass), and the next-chord model (the usual moves, borrowed chords);
  - all `[fast]`-tested, and shared with the Push, the M4 Chord and Arpeggiator modules, and assistants.
- **`model/NoteEditing`** grows the pure functions: in-key moves, fit to key, chords from roots, every transform and generator, time handles, joins and splits. Each takes notes and settings and returns notes, tested without the engine. The popovers preview by calling them.
- **`model::Note`** gains `releaseVelocity`, `muted`, `chance` and `velocitySpread`. tracktion's notes already keep the first two; the others are properties of ours on the note.
- **Chance in the engine:** where tracktion plays a clip's notes, a small hook decides each note from a hash of the clip's seed, the note and the lap. It's real-time safe and needs no stored random state. This is settled while building stage 2, with a test that a bounce matches playback.
- **Controllers:** tracktion's MIDI clips keep controller events (`MidiList`). The Session reads and writes them, and the snapshot carries them per clip.
- **The chord lane** reads names from the snapshot's notes, cached per clip version, so drawing it costs nothing per frame.

## 16. Stages

| Stage | What | Why first |
|---|---|---|
| **P4a: in key, chords, and the basics** | §5–§9: Key and Fold, arrows in the scale, Fit to key; the chord lane, Draw chords, the Chord popover with suggestions, make chords, inversions and voicings; other tracks' notes; copy, split, join, legato, time handles, velocity lines, mute, Quantise…, the NOTES inspector; the Notes menu; MCP | What the product owner asked for first, and what makes writing fast |
| **P4b: chance and controllers** | §10–§11: chance and velocity spread (played the same in every render), release velocity, the controller lanes | Expected now; recorded controllers become visible |
| **P4c: transform and generate** | §12: the transform and generate tools | Builds on the chords and chance of the stages before |
| **Later, with their features** | Grooves (M4, with quantise); a drum view (M4, with the Drum Rack); per-note MPE (M5); editing several clips at once; step input; chord names over arpeggios; chance groups and conditions | Each needs its own milestone's work |

Each stage is built, reviewed in screenshots at standard and compact widths, and signed off before the next.

## 17. Questions and recommendations

| # | Question | Recommendation |
|---|---|---|
| N1 | **Scope and stages** | The three stages of §16, in that order, each signed off on its own |
| N2 | **Tools or no tools** | No tool palette: gestures and modifiers as now, plus a visible Draw choice (Note, Chord, Chord under). Fewer modes to get lost in than Bitwig's or FL's palettes (§3) |
| N3 | **Key snap's default** | ~~On whenever the song has a key, kept per project; Alt bypasses it for a gesture~~ *Superseded by §21: no snapping; In key shows only the key's rows* |
| N4 | **The arrows with a key** | ↑/↓ a step of the scale, as Bitwig and Studio Pro do; Ctrl a semitone; Shift an octave, as today (§6) |
| N5 | **Fold** | ~~All rows / Key (rows labelled with their step) / Notes~~ *Superseded by §21: In key or the whole roll* |
| N6 | **Out-of-key notes** | Shown by their rows, found by Select ▸ Outside the key, moved by Fit to key; no extra mark on the notes (§6) |
| N7 | **The chord lane** | Chord names read from the notes, with steps in the key; a click selects a chord's notes, a double-click changes it, the empty lane writes one. No separate chord track (§7.1) |
| N8 | **Drawing chords** | A Draw choice: Note, Chord (the clicked note is the root), Chord under (the clicked note on top), voice-led (§7.2) |
| N9 | **Suggestions** | In the key, next after the chord before, borrowed; from a small explainable model in `libs/music`, not a learned one (§7.3) |
| N10 | **Voicing** | Smooth by default (the least movement from the chord before); Close, Open, Drop 2, Drop 3, Spread, and inversions by hand (§7.4) |
| N11 | **Other tracks' notes** | A view option, all tracks by default, faint and never selectable; their chords named on a second line (§8) |
| N12 | **Time handles** | A bracket in the ruler over two or more selected notes; dragging an end stretches, past the other reverses (§9.2) |
| N13 | **The NOTES inspector** | Pitch, start, length, velocity, release velocity, chance, mute; ranges for several notes, moved together (§9.6) |
| N14 | **Chance** | Per note, with velocity spread; decided from a seed, the note and the lap, so every render matches playback; Re-roll for a new seed (§10) |
| N15 | **Controller lanes** | In the bottom lane's chooser, edited as automation is; recorded controllers shown (§11) |
| N16 | **Tools** | Popovers that preview live; Apply is one undo step, Escape undoes nothing; settings remembered (§12) |
| N17 | **A Notes menu** | New, between Clip and Song; Clip ▸ Notes ▸ moves there; the same menu opens from a Notes ⌄ button in the clip view's header, so every tool is where you work (the product owner, 2026-10-14) (§4, §13) |
| N18 | **Assistants** | The tools of §14 |
| N19 | **Later** | Grooves, the drum view, MPE, multi-clip editing, step input, arpeggio chords, chance conditions (§16) |

## 18. Tests (Done when)

- **`libs/music` (`[fast]`):** the key's chords on every step of every scale; voicings and inversions; voice leading (the least total movement over a set of progressions); naming (every chord kind, inversions as slash chords, steps in the key, borrowed chords); suggestions (V goes to I first; the borrowed list per key).
- **`model/NoteEditing` (`[fast]`):** in-key moves and draws; Fit to key, ties included; Make chords; time handles (stretch and reverse); join and split; every transform and generator, deterministic for a seed.
- **The engine:** chance and velocity spread render the same in playback, bounce and export; a re-roll changes them; controllers play and record.
- **Assistants:** each tool of §14 driven through MCP.
- **UI:**
  - every popover and the chord lane sized to their content;
  - the header at standard and compact widths, options dropping in order and nothing touching;
  - screenshots zoomed in, reviewed for each stage;
  - the menu-bar test and the shortcut test.
- **Performance:** the piano roll draws within the frame budget with 2 000 notes, the chord lane and other tracks' notes shown.

## 19. Settled while building (P4a, 2026-10-14)

**Theory (`libs/music`, `Chords.h`):**
- **28 chord kinds** read from notes and written by name (triads, sus, sixths, sevenths, add9, ninths, 6/9, 7♭9, 7♯9, 9sus4, m11, 13). Notes no kind explains exactly are read as a kind with tensions in parentheses ("Em7(♭9)", "C7" without its fifth). Where notes read two ways, the reading with the root in the bass wins, then the kinds' order (Am7 over C6 with A under).
- **Slash chords:** an inversion is "C/E". A bass that's only an added tone reads as a chord over it: D under C E G is "C/D", not "Cadd9/D".
- **Spelling:** a chord's root is spelled by its degree in the key, so a borrowed ♭VII in C major is B♭, not A♯; its bass by its place in the chord (E7/G♯ in F major). A diminished chord on a note outside the key is spelled as leading up (♯iv°, C♯°7). Without a key, as in C major.
- **The key's chords:** a seven-note scale's thirds; other scales (pentatonics, symmetric) the first of major, minor, sus4, sus2, diminished, augmented whose notes are all the key's. **sus2 and sus4 are true sus chords** (a major second or a perfect fourth for the third, with a perfect fifth) even where a note leaves the key, as Fsus4 does in C major; **add9 adds a major ninth.** A note outside the key gets the kind most of whose notes are in it (B♭ in C major is B♭, its seventh B♭maj7).
- **Without a key** (§7.2): a major or minor triad by the note's place in C major, B minor and the black keys by most notes in C major (E♭, F♯m); the Wavetable's Key Chord can use the same function.
- **Suggestions:** the usual moves from each step (one table for major-like keys, one for minor-like), each with why ("Down a fifth: the strongest move", "Deceptive: the dominant not going home"); a dominant outside the key resolves down a fifth, ♭VII and iv go home, ♭VI goes on to ♭VII. Borrowed: ♭VII, iv, ♭VI, ♭III in a major key, IV and V in a minor one, and V/V, leaving out what the key already has. **Chord under** picks the key's chord with the note that follows the chord before best, else the most usual (I, IV, V, vi …).
- **Voicing:** every inversion of the close voicing, in every octave, scored: Smooth by the least movement from the chord before (voices matched in order), the others by register. A voicing asked to keep the clicked note (Chord) or to have it on top (Chord under) keeps it. More notes than the chord's double it from the root up; fewer drop the fifth first.

**The clip view:**
- **The chord lane** joins spans of the same chord across a gap of up to a beat, so a chord played as stabs is one chord to select and rewrite. Muted notes don't sound, so they make no chord. With Others on, a second line (**ALL**) names the chords of everything shown when they say more than the clip's own; it shows even when the clip has no chords, for writing a bassline against the pads. Its labels are made when the chords change, not every frame.
- **Display** (Names and steps, Names, Steps) is the lane's right-click menu; Steps needs a key.
- *(Fold and Key became In key in §21.)* **Fold to key** keeps the row of any note outside the key, so no note is ever hidden; folded, every row is a key of its own labelled "A3 · 1", with a line under each row as the rows no longer alternate. Fold to notes, as named. Folded, a drag moves by the rows shown; Key snapping is then the rows'.
- **Ctrl+click** still adds a note to the selection or takes it out; **Ctrl+drag** copies (the note pressed joins the selection first).
- **Time handles** snap to the grid; **Alt** stretches the lengths too; a drag past the other end reverses. The tag reads "× 1.5".
- **The Chord popover** sits beside the place it writes and the chord before it, so both stay in view (left of them when there's no room right). It's as wide as its fixed content (its titles and the three choices), and the chords wrap within it, so it keeps its width as the kind changes. A chord written stays lit; each write is one undo step. Changing the kind with a chord there writes the key's chord of that kind on the same root; changing the voicing or inversion re-voices it. Without a song key it offers "As in C major" and nothing borrowed. (Its choices set what Draw used, and the Draw menu opened it for them, until §20.)
- **Quantise…:** Strength and Swing (0–100 %; full swing puts every other line a third of a step later, a triplet feel) and Moves (Starts, Ends, Both). Its settings are kept with the project's view state and used by Quantise (Ctrl+U).
- **Others** draws the other tracks' MIDI clips (muted clips and notes left out), lap by lap, in their clip colours at `ghostAlpha` (28 %). Choosing the tracks one by one (§8) comes with P4b.
- **The header** gives way at narrow widths: its hint first, then Others, Fold and Draw (`PanelHeader::setDropOrder`), the context cut short; nothing touches down to 600 px.
- **The NOTES inspector** sits under CLIP while the clip view shows that clip with notes selected; its title names the selection and its chord ("Notes · 4 selected · G7"). Release velocity and Chance join it with P4b, and the bottom lane's chooser (Velocity ⌄) with the controller lanes.
- **The options** (Key, Fold, Others, Draw, the chord lane's Display, the Chord popover's choices, Quantise…'s settings) are view state, kept with the project.

**Menus and keys:** the Notes menu takes **N** (Alt+N); **Song moves to G** (Alt+G). The View menu named Key "Snap to key", as Snap is "Snap to grid" (until §21). The Notes tools have no default keys beyond §13's (assignable in Settings > Keys).

**Assistants:** `edit_notes` applies its edits in a fixed order (velocity, transpose, shift, steps, fit to key, quantise, stretch, legato, fixed length, join, split, reverse, mirror, invert, voicing, mute, add bass); names are written with # and b, as the key tools write them.

**Performance:** 2 000 notes with the chord lane and other tracks' notes draw in 44 ms in a Debug build (measuring the lane's text every frame made it 386 ms); the test checks a frame (16 ms) in a Release build.

## 20. Revised after the product owner's first use (approved and built, 2026-10-14)

The product owner's review of P4a:
- "There is a button called 'Make chord…' and one called 'Make chords'. One transforms notes into chords and the other… I can't tell exactly what it does."
- "When you draw a chord, it draws whatever chord is selected in that submenu that opens with 'Chord kind and voicing', but that menu is hard to reach. Switching what chords I want to draw is something I do very often, maybe multiple times within one phrase."
- "When transforming root notes into a chord progression, we again use that setting. Many things seem to be tied to that section buried under menus, and it's confusing. I very rarely want all those root notes turning into the same kind of chord with the same voicing. I would want that configurable per note."
- "Without a key, two shades of grey show black and white keys. Pick a key, and the same two shades show something completely different: whether a note is in the key. I felt like the piano got scrambled in front of me, and I suddenly could not click on certain notes."

**What went wrong:** one hidden setting (the Chord popover's Kind, Voicing and Inversion) drove three different things (drawing chords, making chords, the next chord written), so the choice that's made most often was the hardest to reach; two commands had names a letter apart; and the key reused the piano's two shades for another meaning, while Key snapping moved clicks to other rows without showing it.

**Where the field is:** Bitwig 6 lets the piano roll keep the piano's pattern, or replace it with coloured lanes for the key's notes and dark ones for the rest ("Adapt to Key"); FL Studio darkens the rows outside the scale; Live 12 highlights the scale and can fold to it. FL's chord stamps are chosen from a menu in the piano roll's toolbar before clicking, which its users ask to make quicker ([Bitwig release notes](https://downloads.bitwig.com/6.0/Release-Notes-6.0.html), [FL Studio manual](https://www.image-line.com/fl-studio-learning/fl-studio-online-manual/html/pianoroll.htm)). Nobody makes a run of notes into chords chosen note by note; Scaler and Captain Chords pick chord by chord on pads.

### 20.1 The key never scrambles the piano

*Revised in §21: the lavender rows, the keyboard's dots and the ghost went, with Key snapping and Fold. The piano's rows stay.*

- **The rows always show the piano:** white keys' rows lighter, black keys' darker, a line under each C and between E and F, with or without a key. Nothing about the piano's geography changes when a key is set.
- **The key is a colour of its own:** its notes' rows are the piano's shades turned towards `accentMidi` (lavender: the colour of MIDI, never the selection's blue), the root's more. In C minor, E♭'s row is dark and lavender, E's light and grey: the shade says which key of the piano, the hue says in the key or not. A black key's row in the key stays darker than a white key's out of it.
- **The keyboard** stays a piano; the key's notes get a small lavender dot at their left end, clear of the C's names (as the Key popover's keys show a key's notes), the root its name. Folded, each row's key is white or black as on the piano.
- **Where a click will draw:** while the pointer is over the grid, a ghost of what a double-click draws sits where it will land: the note, or with Draw set to a chord, the whole chord, voiced as it will be. With Key on, over a row outside the key, the ghost shows on the key's nearest row, so a click never lands somewhere unexpected; Alt shows it on the row itself.
- Fold to key, Fold to notes, Fit to key and the arrows are as before.

### 20.2 Choosing the chord where you draw it

- **While Draw is set to a chord,** the header shows the kinds side by side right after it: `Draw chord ⌄ [Triad 7th 9th sus2 sus4 add9] Smooth ⌄`. One click switches the kind, for the next chord drawn; the voicing (Smooth, Close, Open, Drop 2, Drop 3, Spread, and the inversion under it) is a choice beside it. They take no room while drawing notes.
- **The View menu has them too:** View ▸ Draw ▸ Note, Chord, Chord under, then the kinds and voicings, ticked.
- **Narrow:** the voicing gives way first (after Others and Fold); the kinds stay while drawing chords.
- The Draw menu's "Chord kind, voicing…" goes.

### 20.3 Making chords, chord by chord

- **One command, Make chords…** (Notes menu, the notes' right-click menu): a popover with a card for each selected note, in time order, each card the note and the chord it becomes. The notes in the clip change live as the cards are set; **Apply** (Return) keeps them as one undo step, Escape puts the notes back.
- **Each card** has the chord (the key's chord on the note, as a triad, to start with) and its voicing. The chord's menu has every kind on that note (Am, Am7, Am9, Asus2, Asus4, Am(add9)), then the key's chords with the note on top ("Under A: F, Dm"), then "Leave the note"; the voicing's has Smooth (the least movement from the card before) and the others. Hovering a card plays its chord.
- **For every card at once:** a row of kinds and a voicing at the foot of the popover, then each card changed alone.
- **Sized to its content:** cards wrap onto rows; the popover is as wide as eight cards at most.

### 20.4 Changing one chord

- **Make chord… becomes Change chord…** (Notes ▸ Chords ▸, and the notes' right-click menu): the Chord popover for the selected chord, dimmed unless the selected notes make one chord. A chord where there's none is drawn (Draw chord), or written from the chord lane (a double-click or a drag in it), as now.
- **The Chord popover's Kind, Voicing and Inversion act on that chord alone.** They start as the chord is, and no longer set what Draw or Make chords use: each place that writes chords has its own choices, where it writes them.

### 20.5 Questions and recommendations

| # | Question | Recommendation |
|---|---|---|
| R1 | ~~**The rows with a key**~~ *(§21)* | The piano's pattern always; the key as a lavender tint on its rows (the root's stronger) and dots on the keyboard (§20.1). Not Bitwig's choice of two backgrounds: an option to fix a confusing default isn't a fix. Not FL's darker out-of-key rows: that's the piano's channel again |
| R2 | ~~**Where a click will draw**~~ *(§21)* | A ghost of the note or chord under the pointer, on the row it will land on (§20.1) |
| R3 | ~~**Key on by default**~~ *(§21)* | Still on with a song key (N3), now that the rows and the ghost show what it does. The other way: off by default, the key only shown |
| R4 | **The kind to draw** | Side by side in the header while drawing chords, the voicing beside it; and in View ▸ Draw ▸ (§20.2) |
| R5 | **Make chords** | One command, Make chords…: a card per note, each its own chord and voicing, all at once from the foot; live, one undo step (§20.3) |
| R6 | **Make chord…** | Becomes Change chord…, for the selected chord (§20.4) |
| R7 | **The Chord popover's choices** | For that chord alone (§20.4) |
| R8 | **Assistants** | `set_chord` already takes a kind and voicing per chord; `make_chords` gains `chords`, a kind and voicing per note, as the cards have |

### 20.6 Settled while building (2026-10-14)

- *(Gone with §21.)* **The key's rows** were four colour roles (`keyRowLightInKey`, `keyRowDarkInKey`, `keyRowLightRoot`, `keyRowDarkRoot`): each piano shade mixed towards lavender, then brought back to the shade lightened a little (by 20 %, the root's by 45 %), so the hue never overtakes the shade. A plain tint over the rows made a black key's row in the key lighter than a white key's out of it: the same confusion again. `keyRowRoot` goes.
- *(Gone with §21.)* **The ghost** was drawn by a shared painter (`ui::paintGhost`: a dashed `textSecondary` outline over the clip's colour at `ghostAlpha`), and worked out again only when the row, the step or Alt changes under the pointer.
- **The header's kinds** are a `Segments` (now a `PanelHeader` option, a row high in the taller header); at narrow widths Others, Fold, the voicing, Draw and last the kinds give way. The View menu names them "Draw triads", "Draw 7th chords" … and "Voiced smooth", "Voiced close" …
- **Make chords…** starts every card on the key's triad, smoothly voiced; the foot's kinds and voicings are lit only while every card shares one. A card's chord menu lists the kinds on its note, then "Under" the key's other chords with it in them, then "Leave the note"; its voicing menu, the voicings then the inversions. Cards wrap at eight a row and scroll past three rows. The popover, like the Chord popover, sits beside the notes, under the lane, or under the ruler while the clip has no chords yet.
- **Assistants:** `make_chords` takes `chords`, one per matching note in time order: `"leave"`, or `kind`, `chord` (a chord with the note in it; on top when it isn't the root), `voicing`, `inversion`.

## 21. Revised again: In key or the whole roll (approved and built, 2026-10-14)

The product owner, after using §20:
- "The lavender looks bad. And the ghost notes feel bad to work with."
- Their proposal: "Get rid of the ghost notes and the key lavender highlighting. Then we combine the Fold and Key toggle functionality into a single [toggle] that shows either the whole roll, unhighlighted, [or] just the notes in the key. We get rid of the 'just rows with notes' option because it is not that useful anyway. If we have a key selected the piano roll opens automatically in the 'just notes in the key' mode, otherwise the full roll. But you can always switch." And the one question it leaves: "How do you show the user that there are notes in the MIDI data if we are in the key-only mode but the user placed notes outside the key previously? They might be invisible to them but produce sound, which is confusing."

**What went wrong:** §20 kept Key snapping, which puts a click on another row than the one under the pointer, then added two things to explain it: lavender rows to show where the key is, and a ghost to show where a click would land. Both patched a view that showed one thing (every row) and did another (only the key's rows count). A view that shows only the rows you can write on needs neither.

**Where the field is:** Live folds to the scale and shows notes outside it on dark lanes ([Soundfly](https://flypaper.soundfly.com/produce/how-to-use-scale-mode-in-ableton-live-11-video/), [Ableton](https://help.ableton.com/hc/articles/11425083250972)). Push's step sequencer hides them instead, and Ableton keeps a help article for people looking for their missing notes ([Ableton](https://help.ableton.com/hc/en-us/articles/209775085-Push-1-certain-notes-in-the-Melodic-Step-Sequencer-not-visible)): hiding notes that sound confuses.

### 21.1 One toggle: the key's notes or the whole roll

- **In key** (the header's toggle, shown while the song has a key; View ▸ Show only the key's notes, Ctrl+Alt+N):
  - **On:** the rows are the key's notes, each labelled with its note and step ("A3 · 1"), white or black as on the piano, a line under each. A row is a step of the key: a double-click draws on the row clicked, a drag and ↑/↓ move by the key's steps.
  - **Off:** the whole roll: the piano's rows and nothing more, the C's labelled. The key colours nothing. A row is a semitone; ↑/↓ and drags move by semitones.
- **A click lands where it's made,** in either. Key snapping goes, and with it Alt's bypass of the key (Alt still frees the grid). To leave the key in key mode, Ctrl+↑/↓ moves the selection by a semitone, and the note's row appears; or switch to the whole roll.
- **Fold goes:** the key's rows are In key; "Rows with notes" goes.
- **Default:** on whenever the song has a key. Turned off for a clip, it stays off for that clip (a drum part stays whole); turned on, the clip follows the key again. Without a key, the whole roll; the toggle is hidden and the menu item dimmed with why.

### 21.2 Notes outside the key stay in view

- **In key, a pitch with notes outside the key keeps its row,** in its place among the key's, so nothing that sounds is hidden. The row is **hatched** (fine diagonal lines over its shade), and its key on the keyboard is named without a step ("F♯3"). A darker lane, as Live has, doesn't show here: the palette's darkest row shades are already a step apart.
- **The row goes when its last note leaves it,** once the gesture ends, so nothing moves under the pointer. A preview that writes outside the key (the Chord popover, Make chords…, a chord drawn) shows its rows while it's shown.
- Select ▸ Outside the key and Fit to key find and move them, as before.

### 21.3 What goes, what stays

- **Goes:** the ghost of what a double-click draws (and `ui::paintGhost`); the four lavender row roles; the keyboard's dots and the root's label; Key snapping (and `NoteDrag::key`); Fold and Fold to notes; View ▸ Snap to key and Fold ▸.
- **Stays:** other tracks' notes (Others): they're the other parts, not a preview; In key, on the rows shown. Note names are still spelled by the key (B♭ in F major), in both.

### 21.4 Questions and recommendations

| # | Question | Recommendation |
|---|---|---|
| S1 | **Rows and writing** | One toggle: In key (the key's rows, a click on the row clicked) or the whole roll (chromatic), §21.1 |
| S2 | **Notes outside the key, In key** | Their rows stay, hatched, named without a step (§21.2). Not hidden (Push), not drawn between two rows (they'd overlap and be hard to click), not only counted (they couldn't be edited) |
| S3 | **The default** | In key with a song key; a clip turned off stays off |
| S4 | **Rows with notes** | Dropped |
| S5 | **The ghost and the lavender** | Dropped: a click lands where it's made, and the rows are the key |
| S6 | **Assistants** | Nothing changes: the view's options were never theirs; `edit_notes`' `transpose_steps` and `fit_to_key` stay |

### 21.5 Settled while building (2026-10-14)

- **The hatch** is a shared painter (`ui::paintHatch`: 1 px lines rising to the right, `shell.hatchSpacing` 6 px apart, placed by x + y so neighbouring rows' lines meet) in a new role, `keyRowHatch` (the `line` colour: a diagonal 1 px line reads quieter than a straight one), a pixel inside the row so a note's edge stays clean. A quieter colour (`surface3` at 60 %) vanished at 1×.
- **The keyboard, In key:** every row a key of its own, white or black as on the piano, labelled "A3 · 1"; a row outside the key just "A♯3". **The whole roll:** the C's labelled, nothing else; the root's label went with the dots.
- **In the header,** In key sits where Key and Fold were, a `ViewOption` like Others, shown while the song has a key; at narrow widths Others, the voicing, Draw and the kinds give way, never In key. Its tooltip is the View menu's item, "Show only the key's notes" (Ctrl+Alt+N).
- **Remembered per clip** as view state with the project (`wholeRoll`: the clips turned to the whole roll); a clip turned back on is taken off the list, so it follows the key again.
- **Rows during a gesture:** a drag lands only on rows, so the rows hold still until it ends; a drawn chord or a preview with notes outside the key adds their rows as it starts, and a preview given up (Escape, a click elsewhere) takes them away.
- **Mirror pitches** mirrors in the key's steps while In key, chromatically in the whole roll, as ↑/↓ do.

## 22. P4b built: chance, release velocity, controller lanes (2026-10-15)

Built as §10 and §11 say. What building settled:

**Chance in the engine:**
- **A note's fate** (`model::noteFate`) comes from a hash (SplitMix64) of the clip's seed, the clip, the note's pitch and its place in the clip's content to a tick, and the lap. No random state runs anywhere. **The clip is in it**, so each copy of a clip rolls its own; Re-roll gives one clip a new seed.
- **Where:** tracktion builds every MIDI clip's playback sequence through `EngineBehaviour::createPlaybackMidiSequence`, so playback, bounces and exports all pass through the same decision. A looped clip arrives with its laps laid end to end, so each note's lap is known. Render copies keep each clip's ID and seed, and a test shows a render matching the same notes written out, sample for sample.
- **Kept** on tracktion's notes (`fdChance`, `fdSpread`; release velocity is tracktion's own `lift`) and the clip (`fdChanceSeed`). Only what isn't the plain note's is written. A seed's change rebuilds playback, undo too.
- **Velocity spread** moves a note's velocity up or down by up to its spread each lap, in MIDI steps, never silent.

**Controllers:**
- **A lane is points,** as an automation lane's, in beats from the clip's content start. It plays as MIDI events: each point's own, and between two points that aren't a step, a ramp with an event wherever the MIDI value changes, at most every 64th of a beat. The lane's points are marked on their events (`fdPoint`, `fdCurve`) and the ramps too (`fdRamp`). Reading back drops the ramps, so a lane reads as it was drawn. **Recorded controllers** (unmarked) are points that hold.
- **Ranges:** CC and aftertouch 0–127; pitch bend in 16 384ths, so its rest (8 192) is exactly the middle and its top exactly the top. The pedals (CC 64–69) are on or off: steps of two values.
- **The gestures are the automation lanes' own,** one shared piece (`LaneGesture`) used by the arrangement and the clip view: double-click adds or deletes, drag moves (Shift fine, Alt free), Alt bends, Ctrl+drag draws, an empty spot box-selects. Delete deletes the selected points when the lane was used last. An edit kept builds on the last one until the snapshot has it.
- **Drawn** by the automation lanes' painter in the clip's colour. An empty lane shows its resting value dashed, and the hint gives way where the two would cross (pitch bend rests in the middle).

**The bottom lane:**
- **Its head** (`ui::LaneChoice`, new) sits in the keys' column: the lane's short name in caps ("VELOCITY", "CHANCE", "RELEASE", "BEND", "MOD", "CC 74") over a chevron. Its menu lists Velocity, Chance and Release velocity; then the controllers (the clip's, then pitch bend, mod wheel, sustain, expression, aftertouch, breath); then Other controller ▸ (CC 0–127 in sixteens). The keys' column grew from 52 to 56 px so "VELOCITY" fits with its margins.
- **Chance and release velocity** are stems drawn and edited as velocity's: a click or drag sets the notes under it, Shift+drag draws a line, a drag on a selected stem moves the selection's. The readout says "Chance 50 %", "Release 64". **Velocity spread** shows behind each velocity stem as the range it plays in.
- **Which lane** is view state, kept with the project.

**Notes:**
- **Chance on a note:** a 2 px bar along its top, as long as its chance, where the note's name leaves room for it. That means a narrow note (hi-hats: the usual case) or rows tall enough for both. A note wide enough for its name writes its chance after it ("50%"); at the default row height, a bar and a name would touch.
- **The NOTES inspector** gains Spread ("± 12"), Release and Chance ("75 %"), each typed or dragged as the others.
- **Notes ▸ Re-roll chance** sits after Mirror pitches; it's dimmed, saying why, while no note is left to chance.

**Others, track by track:** Others' right-click (`ViewOption::onPopupMenu`, new) and View ▸ Other tracks' notes ▸ list each other track with notes, ticked while shown. The tracks left out are view state.

**Assistants:** notes in and out carry `chance` (percent), `velocity_spread` and `release_velocity`, only when they aren't the plain note's. `edit_notes` sets them. `reroll_chance`, `get_controllers` and `set_controllers` are new, and `get_clip` lists a clip's controller lanes and its seed.

**Not yet:** copying and pasting controller points; controllers in the NOTES inspector; drawing controllers while recording (recorded ones show).

