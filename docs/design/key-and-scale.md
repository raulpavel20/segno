# The song's key: design (M3 step P3)

A key for the project: a root note and a scale, shown in the top bar beside the tempo and the time signature. The piano roll shows it, the Push plays in it, modules hear it, and assistants read and set it. It builds on:
- the top bar and its song group ([daw-shell.md](daw-shell.md), "Transport"), and the Song menu ([arranger.md](arranger.md) §12);
- the piano roll ([daw-shell.md](daw-shell.md), "Piano roll"), which P4 makes key-aware for editing;
- the frozen device API, which grows by additions only ([ADR 0034](../decisions/0034-device-api-frozen.md));
- the song row ([ADR 0049](../decisions/0049-the-master-row-is-the-song-row.md)), where changes of key will live when they come;
- the menu bar as the complete list ([ADR 0040](../decisions/0040-a-menu-bar-and-everything-by-mouse.md)) and assistants' tools ([ADR 0021](../decisions/0021-mcp-server.md)).

**Status:** approved by the product owner (2026-10-14), with every recommendation of §17; the decision is [ADR 0056](../decisions/0056-the-song-has-one-key.md). Built 2026-10-14: what building settled is in §20.

## 1. Why

- The product owner (2026-10-13): "A key and scale for the project: hard to work without." Moved up from M4 to M3, before the piano roll (P4).
- Nothing in Segno knows the song's key today:
  - the piano roll draws every row as a piano does, so the notes that fit aren't shown;
  - the Push design plays "in key", and waits on this for the key ([push-2.md](push-2.md) §7.1, Q5);
  - the Wavetable's Key Chord and Snap to Key wait on it ([SPEC](../../devices/wavetable/SPEC.md), W11);
  - M4's Scale, Chord and Arpeggiator modules need it ([device-suite.md](../devices/device-suite.md)).
- A key shared by the whole DAW is something plugins on their own can't do. The [innovation backlog](../innovation-backlog.md) lists it: "shared by the piano roll, MIDI effects, pitch devices and tuner".

## 2. Where the field is (2026)

Checked against the vendors' manuals and release notes in October 2026: Live 12.4, Logic Pro 12, Cubase Pro 15, Fender Studio Pro 8 (Studio One until January 2026), Bitwig 6.1, FL Studio 2026, REAPER 7.

| | Whose key | Where it's set | A new project | Changes along the song |
|---|---|---|---|---|
| **Live 12** | Each clip's | The control bar's Scale Mode, root and scale (the selected clip's), and the clip view | No documented default; new clips take the selected clip's scale | Only through clips in different scales |
| **Logic Pro 12** | The project's, plus a Chord track | The LCD, the Signature track | C major | Key changes on the Signature track |
| **Cubase Pro 15** | A project root key (for loops), and scale events on the Chord track | The toolbar; the Chord track; the Key Editor's own scale | Not documented | Scale events, made from chords or by hand |
| **Studio Pro 8** | The session's key signature; the piano view's scale is set per track | The New Session dialog, the transport's Key button, the Signature track | **None** | Key changes on the Signature track, at bar starts |
| **Bitwig 6** | The project's, new in 6.0 (March 2026) | Root Note and Scale, two transport parameters | Not documented | Automating those parameters |
| **FL Studio 2026** | The piano roll's scale, saved with the project | The piano roll | – | Key markers in the piano roll |

- **Live's per-clip scales confuse people.** Ableton's FAQ exists mainly to say that the control bar's scale "is not a global scale control for all Clips in the Set": it shows the selected or playing clip's ([Ableton](https://help.ableton.com/hc/en-us/articles/11425083250972-Keys-and-Scales-in-Live-12-FAQ)). Underneath, Live's own API keeps one current scale on the song (`Song.root_note`, `scale_name`), which Push reads and writes. Logic, Studio Pro and now Bitwig keep one key for the project, with changes on a timeline ([Apple](https://support.apple.com/guide/logicpro/set-the-project-key-signature-lgcp42b42603/mac), [Fender](https://fenderstudiopromanual.fender.com/en/Content/Arranging_Topics/Signature_Track.htm), [Bitwig](https://downloads.bitwig.com/6.0/Release-Notes-6.0.html)).
- **Scales:**
  - **Live** has a fixed list of 35, the same on Push (from Major and Minor to Pelog Tembung and Messiaen 3–7). Its MIDI modules see "Chromatic" when no scale is on.
  - **Bitwig 6** has 23, from 3 to 8 notes.
  - **Your own scales:** Cubase (rooted on C, for the whole app), Studio Pro (presets), FL Studio and REAPER (scale files). Live and Bitwig have none, except inside their Scale-type modules.
  - Every one of them treats 5-, 6-, 8- and 10-note scales the same way, in steps of the scale.
- **Changing the key moves no notes** in Live, Studio Pro or Bitwig; Live's Fit to Scale is a separate command. Studio Pro's key signature "does not affect which notes can be selected" in the piano view. Logic is the exception: a key change transposes its chords by default ([Apple](https://support.apple.com/guide/logicpro/create-key-signature-changes-lgcpe6f54f88/mac)).
- **What follows the key:**
  - **The piano roll:** highlighting and folding (Live), snapping and moving in key (Bitwig, Logic 12, Cubase, FL Studio), steps of the scale shown (Studio Pro).
  - **MIDI modules:** Live's Arpeggiator, Chord, Pitch, Random, Scale and Meld have "Use Current Scale", and fall back to their own root and scale when no clip has one. Bitwig's note modules have "Use Global Key" ([Ableton](https://www.ableton.com/en/release-notes/live-12/), [Bitwig](https://downloads.bitwig.com/6.1/Release-Notes-6.1.html)).
  - **Loops:** Logic's Apple Loops and Cubase's tagged loops follow the project's key. Logic takes the key and tempo from the first Apple Loop placed in a new project ([Apple](https://support.apple.com/guide/logicpro/lgcp516b16d1/mac)).
- **Note names:** Live's piano ruler offers flats, sharps or both, and "Auto" picks by the root's place in the circle of fifths. Logic spells by the key signature, and can leave out double sharps and flats.
- **Finding the key:** Logic analyses chords (Chord ID, Logic 12), Studio Pro detects key signatures from a part, FL Studio's highlighting can detect the scale, and Cubase's Scale Assistant lists the scales that fit the selected notes. Live and Bitwig have none. None of them publishes how often it's right; every one offers its result as a suggestion to edit ([Apple](https://support.apple.com/guide/logicpro/analyze-key-signature-a-range-chords-lgcp48aad421/mac), [Fender](https://fenderstudiopromanual.fender.com/en/Content/Arranging_Topics/Signature_Track.htm)).
- **Plugins:**
  - **VST3's `ProcessContext::chord`** is a chord, not a key, and no host could be found that fills it in.
  - **CLAP, AU and LV2** carry no key.
  - **ARA 2** has a real key model: key signatures with a root on the circle of fifths and a 12-note set, and tracktion_engine already passes its pitch sequence to ARA plugins through it ([ARA](https://github.com/Celemony/ARA_API/blob/main/ARAInterface.h)).
  - In practice, a key reaches only each DAW's own modules and ARA plugins such as Melodyne.
- **Tuning is a separate idea** in Live and Bitwig: Live hides its scale controls while a tuning system is loaded.

**What this means for Segno:** one key for the song, as Logic, Studio Pro and Bitwig have it, without Live's per-clip confusion. It's shown in the top bar, as every one of them shows it on its transport or LCD. It starts with no key, as Studio Pro does. Our own modules hear it, as Live's and Bitwig's do. Keys found from the notes are offered as suggestions, as everywhere.

## 3. Principles

1. **One key, the song's.** Not one per clip or per track: one answer to "what key is this in?" for every editor, module and controller.
2. **A key describes, it never changes anything.** Setting or changing it moves no note and changes no sound by itself. What follows it says so: the piano roll's rows, a module switched to follow it. Fitting notes to the key is a command of its own (P4).
3. **No key is a real state.** A new project has none, and everything works as it does today until one is chosen.
4. **Shown where the song is:** in the top bar beside the tempo and the time signature, one click to change, one undo step.
5. **The same scales everywhere:** one catalogue with the same names in the top bar, the piano roll, the Push, modules and assistants.
6. **Everything by mouse,** and the same for assistants.

## 4. What a key is

- **A root and a scale:** one of the 12 notes, and a scale from the catalogue (§5). Or no key.
- **Its notes:** the scale's intervals counted up from the root, a set of 12 pitch classes at most. Modules see this set (§9).
- **A minor and C major are different keys.** They hold the same notes, but the root decides what's lit as the root, and which chords P4 builds.
- **Note names follow the key,** as written music does: F major has B♭, not A♯, and E major has D♯, not E♭.
  - A root on a black key takes the name whose key signature has fewer sharps or flats: D♭, E♭, F♯, A♭ and B♭ major; C♯, E♭, F♯, G♯ and B♭ minor. A mode takes its parent major's signature (C♯ Dorian, from B major's five sharps). At a tie, F♯ major and E♭ minor, as most music writes them.
  - Scales other than the major and minor modes take the signature of the major or minor they're closest to: harmonic and melodic minor, the minor pentatonic and the blues scale take the minor's.
  - In a 7-note scale each letter is used once (F♯ major has E♯), but never a double sharp or double flat: the plain note is used instead, as Logic can do.
  - Notes outside the key take the key's direction: flats in a flat key, sharps otherwise.
  - Without a key, sharps, as today.
  - **Choosing the other name** (D♯ minor instead of E♭ minor) comes later, if asked. The project already has room for it (§15).

## 5. The scales

Thirty-six scales, in five groups. They're the 35 that Live and Push users already know, plus the major blues scale. Each has a **permanent ID** (stored in projects, listed in [ids.md](../ids.md)), a name, and a short name for tight places.

| Group | Scales (intervals from the root, in semitones) |
|---|---|
| **Common** | Major (0 2 4 5 7 9 11), Minor (0 2 3 5 7 8 10), Harmonic minor (0 2 3 5 7 8 11), Melodic minor (0 2 3 5 7 9 11), Major pentatonic (0 2 4 7 9), Minor pentatonic (0 3 5 7 10), Blues (0 3 5 6 7 10), Major blues (0 2 3 4 7 9) |
| **Modes** | Dorian (0 2 3 5 7 9 10), Phrygian (0 1 3 5 7 8 10), Lydian (0 2 4 6 7 9 11), Mixolydian (0 2 4 5 7 9 10), Locrian (0 1 3 5 6 8 10) |
| **More colours** | Harmonic major (0 2 4 5 7 8 11), Dorian ♯4 (0 2 3 6 7 9 10), Phrygian dominant (0 1 4 5 7 8 10), Lydian augmented (0 2 4 6 8 9 11), Lydian dominant (0 2 4 6 7 9 10), Super Locrian (0 1 3 4 6 8 10), Hungarian minor (0 2 3 6 7 8 11), Double harmonic (0 1 4 5 7 8 11), Spanish 8-tone (0 1 3 4 5 6 8 10) |
| **Symmetric** | Whole tone (0 2 4 6 8 10), Diminished half-whole (0 1 3 4 6 7 9 10), Diminished whole-half (0 2 3 5 6 8 9 11), Messiaen 3 (0 2 3 4 6 7 8 10 11), Messiaen 4 (0 1 2 5 6 7 8 11), Messiaen 5 (0 1 5 6 7 11), Messiaen 6 (0 2 4 5 6 8 10 11), Messiaen 7 (0 1 2 3 5 6 7 8 9 11) |
| **Japanese and Indonesian** | Hirajoshi (0 2 3 7 8), In-sen (0 1 5 7 10), Iwato (0 1 5 6 10), Kumoi (0 2 3 7 9), Pelog selisir (0 1 3 7 8), Pelog tembung (0 1 5 7 8) |

- **The names are music theory's,** not any vendor's ([ADR 0052](../decisions/0052-generic-names-are-free.md)). Double harmonic is also called Bhairav; the list's search finds a scale by either name.
- **Short names** ("maj", "min", "harm min", "dor", "maj pent", "blues") are used where the full name doesn't fit (§6).
- **Your own scales** (a set of notes you choose) come later, if asked (K5). Live and Bitwig have none either, outside their Scale-type modules.
- **Bitwig's 23 are covered** too, but for its two triads (major and minor), which suit a module better than a song's key.

## 6. Setting the key

### In the top bar

```
◆  120.00 BPM  4/4  A minor  ♪ ··|   ↶ ↷      ▶ ■ ●  …
   └ the song: tempo · time signature · key ┘
```

- **The key field** sits after the time signature, in the song group. It shows the key's name ("A minor", "F♯ Phrygian dominant") in the tempo's text style, with the hover wash. A click opens the Key popover under it.
- **No key** shows "No key" in `textTertiary`, still a click away from the popover.
- **The tooltip** names the key's notes: "A minor: A B C D E F G".
- **When the window is narrow,** the key uses its short name ("A min") before anything drops. It never drops itself, like the tempo.
- **Right-click** offers Key… and No key, as the Song menu does (§13).
- **No wheel or drag:** a key is chosen, never scrolled through by accident.

### The Key popover

```
┌ KEY ───────────────────────────────────── No key ┐
│      ┌─┐ ┌─┐     ┌─┐ ┌─┐ ┌─┐                     │  one octave of keys: a click
│      └─┘ └─┘     └─┘ └─┘ └─┘                     │  sets the root; the root
│   ┌───┬───┬───┬───┬───┬───┬───┐                  │  filled (●), the key's other
│   │ • │ • │ • │ • │ • │ ● │ • │                  │  notes dotted (•)
│   └───┴───┴───┴───┴───┴───┴───┘                  │
│   A minor:  A  B  C  D  E  F  G                  │  ← its notes, spelled
├──────────────────────────────────────────────────┤
│ [⌕ Search scales                              ]  │
│ COMMON                                           │
│   Major                                          │
│ ✓ Minor                                          │
│   Harmonic minor                                 │
│   …                                   (scrolls)  │
├──────────────────────────────────────────────────┤
│ FROM YOUR NOTES    A minor   C major   E minor   │
└──────────────────────────────────────────────────┘
```

- **The chrome is the shared `Popover`;** Escape or a click elsewhere closes it (`OutsideClick`).
- **The root:** one octave of keys. A click on a key makes it the root. The root is filled in `active`, and the key's other notes carry a dot. The line under the keys spells the key's notes (§4).
- **The scale:** a list of `ListRow`s under the groups' caps headings, as the preset list has, the current scale ticked, with a search field above it. Hovering a scale previews its notes on the keys and in the spelled line, and changes nothing until a click.
- **From a project with no key,** picking a root gives its major, and picking a scale gives it on C.
- **No key** is a text button in the title row.
- **From your notes** (§11): up to three keys that fit the project's notes, best first. A click sets one. The row isn't shown until there are enough notes to judge.
- **Undo:** changes made while the popover is open are one undo step, named after the key it closed on ("Set key to A minor", "Remove the key"). Nothing changed, no step.
- **Sizes** come from the shell metrics: the popover's width (`shell.formPopoverWidth`), list rows (`shell.listRowHeight`, `shell.presetListRows` before it scrolls). The keys' size is a new metric, with ui-foundations.md and its tests.

### Not in the inspector

The tempo and the time signature have no inspector section; the key joins them in the top bar. Selecting the song row still shows the master's sections (arranger.md §10).

## 7. The piano roll (in this step)

With a key, the piano roll shows it. Without one, nothing changes.

- **Rows:** the key's notes are the light rows (`keyRowLight`) and the others dark (`keyRowDark`), in place of the piano's pattern of white and black keys. The root's rows are a tone lighter (a new role, `keyRowRoot`). The line under every C stays, so octaves keep their place. The line between E and F goes, since it only marked the piano's pattern.
- **The keyboard** stays a piano. The root's keys get their labels too ("A2"), beside the C's.
- **Note names** on notes and keys are spelled by the key (§4).
- **Editing doesn't change:** drawing, moving and transposing stay chromatic. Editing in key is P4 (§12).

## 8. The Push

As [push-2.md](push-2.md) §7.1 has it: the pads play in the project's key, and the Scale page changes it, one undo step, as the top bar does. With no key, the pads play C major and the display says "No key: playing C major". Choosing a key on the Push sets the project's.

## 9. Modules

- **The device API gains the key** at each block's start: `TransportInfo::key`, a `KeyInfo` with the root (0–11, or −1 for no key) and the key's notes (a 12-bit set). It's an addition under ADR 0034's rule 3: every module compiles and sounds as before, and `docs/architecture.md` lists it.
- **In the DAW,** the adapter fills it from the song's key.
- **In the plugins,** there's no key. No plugin format carries one in practice (§2):
  - VST3's `ProcessContext::chord` is a chord, no host is known to fill it in, and JUCE neither reads nor sends it (checked in the pinned JUCE, 2026-10-14);
  - CLAP, AU and LV2 have nothing for it.
  
  If that changes, the plugin adapter fills the key in, and modules are already ready for it.
- **A module that follows the key works without one,** and its SPEC says what it does then. The Wavetable's Key Chord plays a triad by the note's place in C major (W11, already approved). M4's Scale, Chord and Arpeggiator follow the project's key by default, with a key of their own to choose instead. That also gives them a key in other DAWs.
- **Names and spelling** come from the shared catalogue (§15), never from a module's own list.

## 10. Changes of key along the song (later)

- **This step has one key for the whole song.**
- **The model, the engine and the device API already ask for "the key at this time",** so changes can come later without changing any of them.
- **When they come,** with tempo and time-signature changes (arranger.md §16), they're marks on the song row that move with their sections. The top bar shows the key at the playhead, and the piano roll shows each stretch's key.
- **Considered, and not recommended:**
  - **A key per clip,** as Live 12 has it (§2). Two clips on one track in different keys leave a module with no single key to follow, and the song's key is nowhere. Live's own control bar and Push fall back to one current scale, which follows whichever clip was selected last.
  - **A key per section:** neat, but sections are optional, and a key can change where no section starts.

## 11. Finding the key from the notes

- **Where:** the Key popover's last row, "From your notes": up to three keys, best first, from the selected MIDI clips, or every MIDI clip when none is selected. A click sets one. Segno never sets a key by itself.
- **How:** the notes' pitch classes, weighted by length, are matched against major and minor key profiles (the Krumhansl–Schmuckler method). Only major and minor are offered: the notes alone can't tell a mode from its relative major.
- **Not enough to go on** (fewer than 8 notes, or fewer than 5 different ones): the row isn't shown.
- **MIDI files:** a file's key signature isn't used. Many files carry C major whatever they hold, so their notes go through detection like any others.
- **From audio:** later, with loops that follow the song (M4's warping): §16.

## 12. What P4 builds on this

P4 is designed on its own. It will likely add:
- folding to the key's rows;
- drawing and moving notes in key;
- transposing by steps of the scale;
- notes outside the key marked;
- fitting notes to the key;
- chords from the key on a root.

This step stops at showing the key.

## 13. Menus, keys and actions (ADR 0040)

The **Song** menu gains a first group, for the song's own settings:

| Item | Shortcut | |
|---|---|---|
| Key… | – | `song.key`: opens the Key popover under the top bar's key field |
| No key | – | `song.noKey`: dimmed while there's no key |
| — | | |
| *(the section items, as now)* | | |

- Neither has a default shortcut; both can be assigned in Settings > Keys.
- The command palette finds both.
- The menu-bar test already fails if an action isn't in the menu bar.

## 14. Assistants (MCP)

Each tool is one named undo step, with a test that drives it through MCP (ADR 0021):

| Tool | Does |
|---|---|
| `get_project` | Gains `key`: `{root: "A", scale: "minor", name: "A minor", notes: ["A", "B", "C", "D", "E", "F", "G"]}`, or `null` |
| `set_key` | `root` (a name: "F#", "Gb" and "G♭" all accepted) and `scale` (an ID or a name), or `none: true`. Returns the key as set; an unknown scale is refused, with the IDs |
| `list_scales` | Every scale: ID, name, short name, group, intervals |
| `detect_key` | `clips?` (default: every MIDI clip). Up to three keys, best first, each with how well it fits (0–1); none when there are too few notes |

## 15. Model and engine

- **`libs/music`** (new, JUCE-free, below the JUCE line, `[fast]` tests). One place for the theory every part shares:
  - the scale catalogue (ID, name, short name, group, intervals);
  - `music::Key` (a root and a scale) with its notes, whether a pitch is in it, and each note's name spelled by the key;
  - key detection from notes.
  - P4 adds chords in a key, and M4's MIDI modules use the same library. The app, the Push support and modules all use it, so no two places can disagree about a scale.
- **`libs/device`:** `TransportInfo::key` (§9).
- **The song's key is stored in the engine's pitch sequence** (`te::PitchSequence`). It's tracktion's own record of the song's key, already saved with the project and already moved by the engine's insert-space and delete-region edits.
  - Its first setting holds the root.
  - Our scale ID goes in a property of ours on the same setting, since tracktion's own list has only 11 scales. Without it, the song has no key (§20).
  - tracktion's own scale and sharps fields are set to the nearest match and to the key's spelling (§4), so its features agree.
  - **A bonus:** tracktion already hands this pitch sequence to ARA plugins (Melodyne) as key signatures. If Segno hosts ARA one day, they get the song's key with nothing more to do.
- **`engine::Session::setKey`** takes a key, or none, as one undo step. The snapshot carries the song's keys as a `model::KeyMap` (one entry today) with `keyAt(beat)`.
- **The DAW adapter** (`DevicePlugin`) reads the key on the audio thread from a lock-free copy, published when it changes, as the tempo is.
- **Projects saved before this step** open with no key.

## 16. With the rest of Segno

- **Saved with the project;** every change is one undo step.
- **Bounce and export** don't change: the key changes no sound by itself. Writing the key into exported files' tags comes with export's metadata (M7, [export.md](export.md) §16).
- **The browser and the Library,** later, with M4's warping: samples and loops that carry a key, shown and filtered by "in the song's key", and played in it.
- **The arranger:** nothing moves yet, with one key. Changes of key will move with their sections (§10).
- **The computer keyboard** stays chromatic.
- **The Tuner** (B10) can show whether the note it hears is in the key.
- **Third-party plugins** aren't told the key: JUCE's hosting doesn't send VST3's `chord` field, and CLAP has none (§9).

## 17. Questions and recommendations

| # | Question | Recommendation |
|---|---|---|
| K1 | **Whose key** | The song's: one key, not one per clip or per track (§3, §10) |
| K2 | **Changes along the song** | Later, as marks on the song row that move with their sections, together with tempo and time-signature changes. The model, the engine and the device API ask for "the key at a time" from now on (§10) |
| K3 | **A new project's key** | None. Everything works as today until a key is chosen (§3) |
| K4 | **Where it's set** | The top bar's song group, after the time signature, opening the Key popover: the root on one octave of keys, the scales in a searchable list, keys found from your notes. Song ▸ Key… and No key in the menu bar (§6, §13) |
| K5 | **Which scales** | 36 in five groups: the 35 Live and Push users know, plus the major blues scale. Each has a permanent ID. Your own scales later, if asked (§5) |
| K6 | **Note names** | Spelled by the key, as written music does; sharps without a key, as today. The other name for a root (D♯ minor) later, if asked (§4) |
| K7 | **What follows the key in P3** | The piano roll's rows and the root's labels, note names, the Push, modules (through the device API) and assistants. Editing in key is P4 (§7, §12) |
| K8 | **Changing the key** | Moves no note and changes no sound. Fitting notes to the key is P4's command (§3) |
| K9 | **Finding the key** | From MIDI notes, major and minor, offered in the popover, never set by itself. From audio later (§11) |
| K10 | **Modules** | `TransportInfo::key`, an addition to the frozen API. A module works without a key, and its SPEC says how. M4's MIDI modules follow the project by default, with a key of their own as the alternative (§9) |
| K11 | **Where it's stored** | The engine's pitch sequence, with our scale ID beside it (§15) |
| K12 | **Shared theory** | A new JUCE-free `libs/music`: scales, keys, spelling and detection, used by the app, the Push support and modules (§15) |
| K13 | **Assistants** | `set_key`, `list_scales`, `detect_key`, and the key in `get_project` (§14) |

## 18. Tests (Done when)

- **`libs/music` (`[fast]`):**
  - each scale's notes from every root;
  - the IDs are unique and never change (a list in the test);
  - spelling: every major and minor key uses each letter once; D♭ major but C♯ minor; F♯ major has E♯; no double sharps or flats;
  - detection: a C major melody with a cadence finds C major first; an A minor phrase with G♯ finds A minor first; too few notes finds nothing.
- **Session:**
  - setting and removing the key are one undo step each, and undo restores it exactly;
  - the key survives saving and reopening;
  - a project from before this step opens with no key.
- **The device API:**
  - a test module in the DAW adapter sees the project's key, and no key without one;
  - every module renders as before: conformance and golden files unchanged.
- **Assistants:** each tool of §14 driven through MCP.
- **UI:**
  - the top bar with no key, a short key and the longest key ("F♯ Phrygian dominant"), at standard and compact widths, zoomed: nothing touches;
  - the popover: no key, a key set, a scale hovered, the found keys shown;
  - the piano roll in A minor and in F♯ major, zoomed;
  - the menu-bar test.

## 19. What comes when

- **M3 P3 (this step):** everything above.
- **M3 P4:** editing in key and chords (§12).
- **Wavetable step 3:** Key Chord and Snap to Key (W11) on `TransportInfo::key`.
- **M4:** the Push's Scale page; the Scale, Chord and Arpeggiator modules; loops in the song's key, with warping.
- **With tempo and time-signature changes:** changes of key on the song row (§10).
- **M7:** the key in exported files' tags.
- **Later, if asked:** your own scales; finding the key from audio.

## 20. Settled while building (2026-10-14)

- **Where it's stored:** our scale ID is the property `segnoScale` on the pitch sequence's first setting; without it there's no key, so projects from before open with none. A scale ID this build doesn't know reads as no key, and the project keeps it.
- **Modules** each keep a copy of the key, published from the pitch sequence whenever it changes (undo and redo too) and read on the audio thread without a lock. Offline renders (bounce, export) build their modules on the project's copy, so they hear the key too.
- **The spelling** (`libs/music`): a scale's key signature is written as a mode of the major scale (`signatureMode`); scales with a minor third and no major third take the minor's. At a tie on the circle of fifths a root without an accidental wins, then flats for minor-like scales and sharps for the rest: F♯ major, E♭ minor, F Locrian.
- **♯ and ♭ on screen:** Inter has neither, so the UI font falls back to the bundled JetBrains Mono for them (never a system font). The top bar's key and the piano roll's names are in the mono already.
- **The top bar** lays the key out as a group of its own after the time signature. `model::layoutTopBar` gained a narrower width per item (`BarItem::narrowWidth`), taken before anything drops: the key's short name.
- **New shared components** (ui-foundations.md): `ui::ChoiceField`, the key field, which looks like the `NumberField` beside it; `ui::OctaveKeys`, the popover's keys, at new `shell.octaveKey…` metrics. A new colour role, `keyRowRoot`, for the root's rows.
- **The popover** opens under the top bar (as the Assistants popover does), at the field's left. The spelled line shows the key's name and its notes; while a scale is hovered, the previewed key in `textSecondary`. **No key** hides while there's no key: there's nothing to remove, and the shared toggle has no dimmed look. Each key found from the notes is a text button whose tooltip says how well it fits.
- **Undo:** `Session::KeyChoice` keeps the popover's step open against tracktion's own timer, renames it after each change, and takes it back whole when the key ends where it began.
- **Assistants** get accidentals as `#` and `b` (`"F# minor"`), and give them any way (`"F#"`, `"Gb"`, `"G♭"`, `"e flat"`, or 0–11).

