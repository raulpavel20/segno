# Pitch: device spec

- **Status:** Approved by the product owner (2026-10-18) with the panels drawn (midi-effects.md §21); IDs are permanent (made lowercase and dotted, as docs/ids.md has them, before any build: midi-effects.md §22). Built in step 1a; the DAW's side in 1b, presets in 1c.
- **Type:** MIDI effect
- **Tier / milestone:** Tier 1, M3 P11 step 1 ([midi-effects.md](../../docs/design/midi-effects.md), [ADR 0074](../../docs/decisions/0074-midi-effects-know-the-song.md))
- **Live target:** Live 12's Pitch (transpose by semitones or steps of the scale, a pitch window that blocks, folds or limits), Scale (notes mapped to a scale, folded) and Random (a chance, choices by an interval, up, down or both, in the scale), in one module (MX3). References only; nothing is copied ([ADR 0011](../../docs/decisions/0011-calibrated-ambition-and-consolidation.md)).

## Purpose

Decides which note plays. It moves notes by semitones or by steps of the key, fits them to the key or to the chord sounding in the song, turns a melody upside down, varies notes by chance, and keeps them in a range. Fitted to the song's chords, any part follows them: a riff copied from the verse into the chorus bends to the chorus's chords, live, and nothing in the clip changes.

## Beyond Live

| Addition | Reference | Why it's worth it | Cost |
|---|---|---|---|
| **Fit to the song's chords** (and to the chord's notes on strong beats only, the key between) | Studio Pro's Narrow, Cubase's Follow Chord Track: both from a chord track kept by hand | The part follows the song's harmony, read from its notes; moved parts and loops fit wherever they're put | The song view's chords (§5.1 of the design); a pure fit in `libs/music` |
| **At a change:** a note held across a chord change moves to the new chord, or holds | Cubase users' first complaint about following chords | A held note never clashes with the chord that comes after it | Small: the module keeps what it played |
| **Mirror in the key** | Live's Invert tool (offline, chromatic); Fugue Machine's invert | The classic inversion, live, staying in the key | A pure function |
| **Steps of the key everywhere** (Steps, Random's spread, Mirror) | Live's "Use Current Scale" | One key for the song (ADR 0057) | None |

## Stages

| Stage | Scope | Milestone |
|---|---|---|
| Core | Everything below | M3 P11 step 1 |
| Later | Parallel fit: a part keeps its shape over new chords, from the chords it was written against (midi-effects.md §16) | With demand |

## Parameters

| ID (permanent) | Name | Description | Range | Default | Unit | Curve / skew | Smoothing | Automatable |
|---|---|---|---|---|---|---|---|---|
| `semitones` | Semitones | Moves every note up or down by semitones. | −48 … 48 | 0 | st | linear | – | Yes |
| `steps` | Steps | Moves every note by steps of the key, so it stays in the key; semitones without one. | −14 … 14 | 0 | steps | linear | – | Yes |
| `mirror` | Mirror | Turns the melody upside down around the Around note, by steps of the key. | Off, On | Off | toggle | – | – | Yes |
| `axis` | Around | The note Mirror turns the melody around. | C−2 … G8 | C3 | note | linear | – | Yes |
| `fit` | Fit | Moves notes onto the key, onto the chord sounding, or leaves them as they are. | Off, Key, Chord | Off | choice | – | – | Yes |
| `toward` | Toward | Which way a note moves to fit: to the nearest note, up or down. | Near, Up, Down | Near | choice | – | – | Yes |
| `passing` | Passing notes | Fitting to the chord, notes off the beat fit to the key instead, so passing notes stay. | Off, On | On | toggle | – | – | Yes |
| `change` | At a change | What a note held across a chord change does: moves to fit the new chord, or holds. | Move, Hold | Move | choice | – | – | Yes |
| `chance` | Chance | How likely each note is to move by chance. | 0 … 100 | 0 | % | linear | – | Yes |
| `spread` | Spread | How far a note moves by chance, in steps of the key (semitones without one). | 1 … 12 | 2 | steps | linear | – | Yes |
| `way` | Way | Which way a note moves by chance. | Up, Down, Both | Both | choice | – | – | Yes |
| `lowest` | Lowest | The lowest note let through. | C−2 … G8 | C−2 | note | linear | – | Yes |
| `highest` | Highest | The highest note let through. | C−2 … G8 | G8 | note | linear | – | Yes |
| `outside` | Outside | What a note outside the range does: folds in by octaves, is left out, or plays the nearest edge. | Fold, Drop, Limit | Fold | choice | – | – | Yes |

And the HARMONY section's parameters, the same in every module that follows harmony (midi-effects.md §5.1; the Chord, Arpeggiator, Melody and Bassline have them too):

| ID (permanent) | Name | Description | Range | Default | Unit | Curve / skew | Smoothing | Automatable |
|---|---|---|---|---|---|---|---|---|
| `follow` | Follow | Whose chords it follows: the song's, the listened track's, chords played below the split, or its own. | Song, Track, Played, Own | Song | choice | – | – | Yes |
| `key` | Key | The song's key, or a key of its own (used too when the song has none). | Song, Own | Song | choice | – | – | Yes |
| `root` | Root | Its own key's and its own chord's root. | C … B | C | choice | – | – | Yes |
| `scale` | Scale | Its own key's scale. | the 36 scales of `libs/music`, by an index frozen with the parameter (new scales appended) | Major | choice | – | – | Yes |
| `chord` | Own chord | Its own chord, on the root. | Major, Minor, 7, Maj 7, Min 7, Sus 4, Dim, Aug | Major | choice | – | – | Yes |
| `split` | Split | With Follow on Played, notes below this note set the chord and aren't played. | C−2 … G8 | C2 | note | linear | – | Yes |

**Non-parameter state:** none.

## Design

```
notes in ─> Mirror ─> Semitones, Steps ─> Random ─> Fit ─> Range ─> notes out
                                                   ^
                          the key and the chord now (the song view, or its own) ───┘
```

- **One pure function a note** (`libs/music`, shared with the clip view's tools and assistants, principle 9): from the note, the key, the chord, whether the beat is strong, and the note's roll, the note it becomes (or none). The module keeps, for each note it started, the note it played, so its note-off ends the right one (and its ledger ends them all on stop, jump or off).
- **Mirror:** by steps of the key around Around (chromatic without a key): a note two steps above becomes two steps below.
- **Steps:** a note outside the key moves by the steps of the key nearest it, keeping its offset.
- **Random:** each note-on rolls (a hash of the pass's roll, the module and the note's place, ADR 0062): with Chance, it moves by 1 to Spread steps, Up, Down or Both. Keep holds a roll.
- **Fit:**
  - **Key:** to the nearest note of the key (Toward), as Fit to key does in the clip view.
  - **Chord:** to the nearest note of the chord sounding at the note's start (the song view's, or Follow's). With Passing notes on, a note off the beat fits to the key instead (midi-effects.md §22).
  - **At a change** (Move): when the chord changes under a held note that isn't in the new chord, it ends and its fitted note starts at the change. A held note can't change pitch in MIDI without a bend, so moving plays it again.
  - **No chord** at a place: the key's home chord; no key either: its own chord. The display says which.
- **Range:** a note outside Lowest and Highest folds in by octaves (Fold), is left out (Drop), or plays the edge, the nearest note of the key inside it (Limit).
- **Controllers, bend and pressure** pass through. Polyphonic pressure follows its note to the note it became.
- **No latency.** Real-time safe: tables a note, no allocation.

## UI layout

On the module grid ([device-panels.md](../../docs/design/device-panels.md)).

| Section | Controls |
|---|---|
| `notes` NOTES [4] | The note display (0, 0, 4 × 6): what came in, faint, and what goes out; the chord now and the next along the top, or what it fell back to |
| `move` MOVE [2] | Semitones (0, 0) · Steps (1, 0) · Mirror (0, 3) toggle · Around (1, 3) |
| `fit` FIT [3] | Fit: Off \| Key \| Chord (0, 0, 3) · Toward: Near \| Up \| Down (0, 1, 3) · Passing notes (0, 2, 3) toggle · "Change  Move" (0, 3, 3) |
| `random` RANDOM [2] | Chance (0, 0) · Spread (1, 0) · "Way  Both" (0, 3, 2) |
| `range` RANGE [2] | Lowest (0, 0) · Highest (1, 0) · "Out  Fold" (0, 3, 2) |
| `harmony` HARMONY [3] | "Follow  Song" (0, 0, 3) · "Key  Song" (0, 1, 3) · Root (0, 2, 3) bar · the scale (0, 3, 3), its name alone (the longest don't fit beside a name) · "Chord  Major" (0, 4, 3) · Split (0, 5, 3) bar. Collapsed by default |

- **Dimmed where they don't apply:** Around while Mirror is off; Toward, Passing notes and At a change while Fit is Off (and Passing and At a change while it's Key); Spread and Way at no Chance; in HARMONY, Root and Scale unless Key is Own, Chord unless Follow is Own (Root then too), Split unless Follow is Played.
- **The header** has Listen (the header's picker, captioned Listen, as the Follower's): the track whose notes it hears, for Follow ▸ Track. It shows while Follow is on Track.
- **HARMONY collapsed by default:** the song is what it follows, and the display says so; the section is there for the fallbacks.
- **Combos carry their names inside** ("Change  Move"), short where the cells are few ("Out  Fold").
- **Drawn** (2026-10-18, midi-effects.md §21): about 800 px wide with HARMONY collapsed, 976 open.

## Factory presets

Designed to [ADR 0069](../../docs/decisions/0069-factory-presets-are-designed-and-never-clip.md)'s bar applied to notes: each a use, right in any key.

- **Follow the chords** (Fit: Chord, Passing notes on), **Chord tones only** (Passing off), **In key** (Fit: Key).
- **Octave up**, **Octave down**, **Up a third in key** (Steps 2), **Down a sixth in key**.
- **Mirror** (Around the key's root), **Mirror and follow**.
- **Humanised melody** (Chance 15 %, Spread 1, Fit: Key), **Wild runs** (Chance 60 %, Spread 4).
- **Bass range** (C0 to C2, Fold), **Lead range** (C3 to C5, Fold).

## Automated test targets

| Metric | Target |
|---|---|
| Each note's output against the pure function, all settings | Identical |
| Note-offs | Every note-on ended once, under random streams, chord changes, stops, jumps and loops |
| Block-size invariance | The same notes, to the sample, for any block size |
| Allocation in `process()` | None |
| Fit to the song's chords | Fixture songs: every fitted note in the chord sounding (or the key, with Passing on, off the beat) |

## A/B cases (against the Live target)

| Case | Stimulus | Our settings | Benchmark settings | What to listen for |
|---|---|---|---|---|
| Transpose in key | A melody in A minor | Steps +2 | Live's Pitch, scale on, +2 | The same notes |
| Fit to key | Chromatic runs | Fit: Key, Near | Live's Scale, A minor | Our ties go the way the melody moves |
| Random | A held arpeggio | Chance 50 %, Spread 2, Both | Live's Random, Chance 50 %, Choices 2, scale on | The same range of movement; ours rolls again each pass |

## A/B log

## Sign-off

- [ ] Automated: unit, conformance and golden tests; validators
- [ ] Layout lint passes (`fd_panel_tests`)
- [ ] Snapshots reviewed (100% / 200%; expanded, collapsed, folded)
- [ ] Feel signed off by the product owner (date):
