# Chord: device spec

- **Status:** Approved by the product owner (2026-10-18) with the panels drawn (midi-effects.md §21); IDs are permanent (made lowercase and dotted, as docs/ids.md has them, before any build: midi-effects.md §22). Built in step 1a; the DAW's side in 1b, presets in 1c.
- **Type:** MIDI effect
- **Tier / milestone:** Tier 1, M3 P11 step 1 ([midi-effects.md](../../docs/design/midi-effects.md), [ADR 0074](../../docs/decisions/0074-midi-effects-know-the-song.md))
- **Live target:** Live 12's Chord: up to six shifts of ±36 semitones (or steps of the clip's scale), each with its own velocity, a strum with tension and crescendo, Learn from held keys. Checked against Logic's Chord Trigger (one chord transposed, or a chord a key), Cubase's Chorder and Chord Pads (adaptive voicing), Studio Pro's Chorder, and the plugins that play chords from one finger (Scaler, Captain Chords, InstaChord). References only; nothing is copied ([ADR 0011](../../docs/decisions/0011-calibrated-ambition-and-consolidation.md)).

## Purpose

Turns each note into a chord. **Add** puts notes over it at intervals you set (in semitones, or steps of the key, so a third is major or minor where it lands). **Song** plays the chord sounding in the song on any note, voiced around it and led smoothly from the last: a one-note rhythm becomes stabs that follow every chord change. **Keys** plays the key's chords from one finger, one a note of the key. A strum spreads each chord's notes.

## Beyond Live

| Addition | Reference | Why it's worth it | Cost |
|---|---|---|---|
| **Song:** the chord sounding in the song on any note, voice-led | Cubase's pattern player re-voices a loop to the chord track's chords | A rhythm part plays the harmony with no chords written into it; a chord change moves it | The song view; `libs/music`'s voicing and voice leading (built for the piano roll) |
| **Keys:** the key's chords from one finger (I, ii, iii…), triads, 7ths or 9ths, voice-led | Logic's Chord Trigger, Scaler's keys, Cubase's Chorder | Progressions with one finger, always in the key | `libs/music`'s key chords |
| **Richer** chords in Song: the 7th or 9th the key allows | Scaler's colours | Fuller harmony from a plain progression | Small |

## Stages

| Stage | Scope | Milestone |
|---|---|---|
| Core | Everything below | M3 P11 step 1 |
| Later | Substitutes in Song (relative, parallel, tritone), voice-led (midi-effects.md §16: the reharmonising walk) | With demand |

## Parameters

| ID (permanent) | Name | Description | Range | Default | Unit | Curve / skew | Smoothing | Automatable |
|---|---|---|---|---|---|---|---|---|
| `mode` | Mode | Adds notes at your intervals, plays the song's chord on each note, or the key's chord for each key. | Add, Song, Keys | Add | choice | – | – | Yes |
| `shift<n>` (1–6) | Shift *n* | A note added over each note played, this far from it; 0 adds none. | −36 … 36 | +4, +7, 0, 0, 0, 0 | st or steps | linear | – | Yes |
| `inkey` | In key | Shifts count steps of the key, so each added note stays in it. | Off, On | Off | toggle | – | – | Yes |
| `level` | Level | How hard the added notes play, as a share of the note played. | 0 … 100 | 100 | % | linear | – | Yes |
| `kind` | Kind | How many notes the chords have in Song and Keys: three, a 7th added, or a 9th. | Triad, 7th, 9th | Triad | choice | – | – | Yes |
| `voicing` | Voicing | How the chord's notes are spread: close, open, drop 2 or spread over two octaves. | Close, Open, Drop 2, Spread | Close | choice | – | – | Yes |
| `place` | Place | In Song, where the note you play goes in the chord: on top, at the bottom, or wherever leads smoothest from the last chord. | Top, Bottom, Smooth | Smooth | choice | – | – | Yes |
| `change` | At a change | In Song, what a held chord does when the song's chord changes: moves to the new one, or holds. | Move, Hold | Move | choice | – | – | Yes |
| `around` | Around | In Keys, the note the chords are voiced around. | C−2 … G8 | C3 | note | linear | – | Yes |
| `strum` | Strum (short: Time) | Spreads each chord's notes out in time, the first on the beat. | 0 … 400 | 0 | ms | skew centre 60 | – | Yes |
| `curve` | Curve | Whether the strum speeds up or slows down through the chord. | −100 … 100 | 0 | % | linear | – | Yes |
| `swell` | Swell | Each note of a strum louder (or softer) than the one before. | −100 … 100 | 0 | % | linear | – | Yes |
| `direction` | Direction | The order a strum plays the chord's notes in: up, down, or alternating. | Up, Down, Alt | Up | choice | – | – | Yes |

And HARMONY's, as the [Pitch's](../pitch/SPEC.md): Song mode follows Follow; Keys uses Key.

| ID (permanent) | Name | Description | Range | Default | Unit | Curve / skew | Smoothing | Automatable |
|---|---|---|---|---|---|---|---|---|
| `follow` | Follow | Whose chords it follows: the song's, the listened track's, chords played below the split, or its own. | Song, Track, Played, Own | Song | choice | – | – | Yes |
| `key` | Key | The song's key, or a key of its own (used too when the song has none). | Song, Own | Song | choice | – | – | Yes |
| `root` | Root | Its own key's and its own chord's root. | C … B | C | choice | – | – | Yes |
| `scale` | Scale | Its own key's scale. | the 36 scales of `libs/music`, by an index frozen with the parameter (new scales appended) | Major | choice | – | – | Yes |
| `chord` | Own chord | Its own chord, on the root. | Major, Minor, 7, Maj 7, Min 7, Sus 4, Dim, Aug | Major | choice | – | – | Yes |
| `split` | Split | With Follow on Played, notes below this note set the chord and aren't played. | C−2 … G8 | C2 | note | linear | – | Yes |

**Non-parameter state:** none. **Learn** (a button) writes Shift 1–6 from the chord held, one undo step; it isn't a parameter.

## Design

- **Add:** each note-on starts its note and up to six more (a shift of 0 adds none; one landing on a note already there adds none), at Level × its velocity. In key, a shift counts steps of the key from the note's place in it (a note outside it, from the step below).
- **Song:** each note-on starts the chord sounding at its start (the song view's, or Follow's), Kind deep (Richer: the key's 7th or 9th on that root), voiced by Voicing, the played note placed by Place: Smooth chooses the inversion nearest the last chord the module played (`libs/music` voice leading). The played note always sounds. **At a change** (Move), a chord held across the song's chord change ends and the new one starts at the change.
- **Keys:** each note-on of a note of the key starts the key's chord on that step (Kind, Voicing), around Around, voice-led from the last. Notes outside the key play as they are.
- **Strum:** the chord's notes in Direction's order, the first on the note's time, the rest spread over Strum by Curve, each Swell louder or softer. A strum ahead of the beat (centred on it) reads the clip's notes ahead (midi-effects.md §5) when the module is the first MIDI effect on its track.
- **Note-offs:** a chord ends with the note that started it. The module keeps what each note started; its ledger ends them all on stop, jump or off.
- **Controllers, bend and pressure** pass through; polyphonic pressure reaches every note of its chord.
- **No latency.** Real-time safe.

## UI layout

| Section | Controls |
|---|---|
| `notes` NOTES [4] | The note display (0, 0, 4 × 6): what came in, faint, the chords going out; the song's chord now and the next |
| `chord` CHORD [3] | Mode: Add \| Song \| Keys (0, 0, 3) · the sub-label STRUM (0, 1, 3) · Direction: Up \| Down \| Alt (0, 2, 3) · Time (0, 3; the Strum parameter) · Curve (1, 3) · Swell (2, 3) |
| `add` ADD [4], in Add | Shift 1, 2, 3 (0–2, 0) · Shift 4, 5, 6 (0–2, 3) · In key (3, 0) toggle · Learn (3, 1) button · Level (3, 3) |
| `voices` VOICES [3], in Song and Keys | Kind: Triad \| 7th \| 9th (0, 0, 3) · "Voicing  Close" (0, 1, 3) · "Place  Smooth" (0, 2, 3) · "Change  Move" (0, 3, 3) · Around (0, 4, 3) bar |
| `harmony` HARMONY [3] | As the Pitch's; collapsed by default |

- ADD shows in Add mode, VOICES in Song and Keys (sections that come and go with the mode, as a rack's CHAINS does). Place and At a change are dimmed in Keys, Around in Song; Direction, Curve and Swell while Strum is 0.
- **Drawn** (2026-10-18, midi-effects.md §21): about 656 px wide in Add, 604 in Song and Keys, HARMONY collapsed. The strum under its own sub-label, so its direction doesn't read as part of Mode.
- **The header** has Listen, for Follow ▸ Track, while Follow is on Track.

## Factory presets

- **Add:** Power fifth (+7, +12), Major triad, Minor triad (In key: +2, +4 steps), Stacked fourths, Octaves, Sus 2, Lush 9th in key.
- **Song:** Stabs on the chords (Smooth, Close), Pad voicing (Spread, 7th), Guitar strum (Strum 40 ms, Down), Piano comp (Drop 2).
- **Keys:** One-finger triads, One-finger sevenths, Neo-soul ninths (Spread).

## Automated test targets

| Metric | Target |
|---|---|
| Song mode | Every chord played is the song's chord at its start; voice leading moves at most the minimal total distance (`libs/music`) |
| Note-offs | Every note started ended once, chord changes, stops, jumps and loops included |
| Strum timing | Each note at its time, to the sample, for any block size |
| Allocation in `process()` | None |

## A/B cases (against the Live target)

| Case | Stimulus | Our settings | Benchmark settings | What to listen for |
|---|---|---|---|---|
| Add | A bassline | +7, +12 | Live's Chord, +7, +12 | The same chords |
| Strum | Block chords | Strum 60 ms, Curve +40, Swell −20 | Live's Chord, Strum 60, Tension, Crescendo | The same feel |

## A/B log

## Sign-off

- [ ] Automated: unit, conformance and golden tests; validators
- [ ] Layout lint passes (`fd_panel_tests`)
- [ ] Snapshots reviewed (100% / 200%; expanded, collapsed, folded)
- [ ] Feel signed off by the product owner (date):
