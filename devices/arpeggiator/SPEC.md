# Arpeggiator: device spec

- **Status:** Draft, for the product owner's review with the panels drawn (midi-effects.md §18, step 0). IDs become permanent when it's approved.
- **Type:** MIDI effect
- **Tier / milestone:** Tier 1, M3 P11 step 1 ([midi-effects.md](../../docs/design/midi-effects.md), [ADR 0074](../../docs/decisions/0074-midi-effects-know-the-song.md))
- **Live target:** Live 12's Arpeggiator (styles, rate synced or free, gate past 100 % for legato, Steps and Distance in semitones or steps of the scale, retrigger by note or beat, repeats, groove, hold, velocity decay) and the Sequencers pack's Step Arp (values per step). Checked against Bitwig's (17 patterns, per-step lanes), Logic's (patterns, capture), Studio Pro's (32-step velocity and gate), Kirnu Cream, BlueARP and Thesys (lanes of their own lengths), and the NDLR's motifs (a pattern and a rhythm of different lengths). References only ([ADR 0011](../../docs/decisions/0011-calibrated-ambition-and-consolidation.md)).

## Purpose

Plays the notes held one at a time, in an order, at a rate, over octaves or steps of the key. Lanes of up to 16 steps set each step's velocity, gate, octave, ratchet and chance; each lane has its own length, so a 5-step lane drifts against a 16-step one and the line keeps changing without being random. With no notes held it can play the chord sounding in the song, so a pluck arpeggiates the song's chords with no clip of chords.

## Beyond Live

| Addition | Reference | Why it's worth it | Cost |
|---|---|---|---|
| **Step lanes of their own lengths** | Step Arp (one length), Cream, Thesys, the NDLR's 5-against-8 | A line that evolves over bars from a few steps, composed rather than random | Five lanes of 16 values, a display to draw them |
| **The song's chords with no notes held** | The NDLR's motifs, Logic's players | A part that follows the song without a clip | The song view |
| **Retrigger each section**, or every 1–8 bars | Live retriggers by note or beat | Patterns that line up with the form | The song view's sections |
| **Shuffled:** a random order kept until the notes change | Random styles that reshuffle each time | Random that repeats, so it's a figure, not noise | Small |

## Stages

| Stage | Scope | Milestone |
|---|---|---|
| Core | Everything below | M3 P11 step 1 |
| Later | Conditions on steps (midi-effects.md §8.3), with step 2's generators | M3 P11 step 2 |

## Parameters

| ID (permanent) | Name | Description | Range | Default | Unit | Curve / skew | Smoothing | Automatable |
|---|---|---|---|---|---|---|---|---|
| `style` | Style | The order the notes held are played in. | Up, Down, Up-down, Down-up, Converge, Diverge, Played, Chord, Random, Shuffled | Up | choice | – | – | Yes |
| `sync` | Sync | Plays at a note value of the song's tempo, or at a rate of its own. | Off, On | On | toggle | – | – | Yes |
| `rate` | Rate | How often a step plays, as a note value. | 1/1, 1/2, 1/2T, 1/4D, 1/4, 1/4T, 1/8D, 1/8, 1/8T, 1/16D, 1/16, 1/16T, 1/32, 1/32T, 1/64 | 1/16 | choice | – | – | Yes |
| `hz` | Rate (free) | How many steps a second, without Sync. | 0.5 … 50 | 8 | Hz | skew centre 6 | – | Yes |
| `gate` | Gate | How long each step plays, as a share of the step; past 100 % steps overlap. | 5 … 200 | 80 | % | linear | – | Yes |
| `swing` | Swing | Delays every second step. | 0 … 75 | 0 | % | linear | – | Yes |
| `steps` | Steps | How many times the pattern plays, each time moved by Distance. | 1 … 8 | 1 | | linear | – | Yes |
| `distance` | Distance | How far each repeat of the pattern moves: semitones, or steps of the key with In key. | −24 … 24 | 12 | st or steps | linear | – | Yes |
| `inKey` | In key | Distance and the Octave lane count steps of the key. | Off, On | Off | toggle | – | – | Yes |
| `retrigger` | Retrigger | When the pattern starts again from its first step. | Off, Note, 1 bar, 2 bars, 4 bars, 8 bars, Section | Note | choice | – | – | Yes |
| `hold` | Hold | Keeps playing the notes after they're let go, until new ones are played. | Off, On | Off | toggle | – | – | Yes |
| `songChords` | Song chords | With no notes held, plays the chord sounding in the song. | Off, On | Off | toggle | – | – | Yes |
| `from` | From | The note the song's chords are played from. | C−2 … G8 | C3 | note | linear | – | Yes |
| `<lane>Length` (`velocityLength`, `gateLength`, `octaveLength`, `ratchetLength`, `chanceLength`) | *Lane* length | How many steps the lane has before it starts again. | 1 … 16 | 16 | steps | linear | – | Yes |

And HARMONY's (`follow`, `key`, `root`, `scale`, `chord`, `split`), as the [Pitch's](../pitch/SPEC.md): Song chords follow Follow; In key uses Key.

**Non-parameter state** (extra state, kept in presets): the lanes' values, 16 each:
- Velocity: 0–200 % of the note's (100);
- Gate: 0–200 % of Gate (100);
- Octave: −2 … +2 (0), steps of Distance;
- Ratchet: 1–4 hits in the step (1);
- Chance: 0–100 % (100).

Each lane's values are drawn on the display; the defaults change nothing, so a lane does nothing until drawn.

## Design

- **The clock:** steps on the song's grid when synced (Rate, Swing), counted from the song's start, so a render, a jump and playing through agree; free at Hz otherwise.
- **The notes held** (and with Hold, the last ones let go), sorted for the Style; Played in the order pressed; Shuffled shuffles them once (a hash of the pass's roll and the set of notes) and keeps the order until the notes change. Steps × Distance extends the list.
- **Each step:** the next note of the list; the lanes' values at their own positions (each lane counts its own steps, so their lengths drift); Chance rolls (ADR 0062); Ratchet splits the step into hits, Gate × the Gate lane sets the length; Velocity × the lane's.
- **Song chords:** with no notes held (and none let go under Hold), the list is the chord sounding in the song, voiced up from From (Follow's chord, or the key's home chord where there's none).
- **Retrigger:** the pattern and the lanes start again on a new note (Note), at a bar line every N bars, or at each section.
- **Note-offs:** each step's note ends at its gate; its ledger ends every note on stop, jump, a new pass or off.
- **Controllers, bend and pressure** pass through.
- **No latency.** Real-time safe.

## UI layout

| Section | Controls |
|---|---|
| `steps` STEPS [6] | The lane chooser (0, 0, 5): Velocity \| Gate \| Octave \| Ratchet \| Chance (view state, a `ui::Selector`); the display (0, 1, 6 × 5): the lane's 16 steps drawn as bars, its length a handle at its end, the step playing lit; under them the notes going out, faint, and along the top the chord now and the next |
| `play` PLAY [3] | "Style  Up" (0, 0, 3) · Sync (0, 1) toggle · Hold (1, 1) toggle · "Retrig  Note" (0, 2, 3) · Rate (0, 3), following Sync (Rate or Rate (free)) · Gate (1, 3) · Swing (2, 3) |
| `range` RANGE [2] | Steps (0, 0) · Distance (1, 0) · In key (0, 3, 2) toggle · Song chords (0, 4, 2) toggle · From (0, 5, 2) bar |
| `harmony` HARMONY [3] | As the Pitch's; collapsed by default |

- Distance is dimmed at one Step; From while Song chords is off.
- **The lanes on the display:** bars from the floor (Octave from the middle), a step left at its neutral value grey (it changes nothing), the step playing the brightest, the steps past the lane's length shaded, its length a line after its last step, dragged. The chooser names them short: Vel, Gate, Oct, Ratch, Chance.
- **Drawn** (2026-10-18, midi-effects.md §21): about 656 px wide, HARMONY collapsed.
- **The header** has Listen, for Follow ▸ Track, while Follow is on Track.

## Factory presets

Twenty or so, designed to [ADR 0069](../../docs/decisions/0069-factory-presets-are-designed-and-never-clip.md)'s bar applied to notes: classic up and up-down at 1/16; trance gates (Gate lane); a 5-against-16 accent lane; ratchet rolls; a two-octave pluck in key; a chance-thinned figure; and **Song chords** presets (a pluck, a bell, a bass) that play along with no clip.

## Automated test targets

| Metric | Target |
|---|---|
| Steps on the grid | Every step's note at its sample for any block size, after a jump as when played through |
| Lanes of different lengths | Each lane's value at its own position (fixture: lengths 5 and 16 realign every 80 steps) |
| Note-offs | Every note started ended once |
| Allocation in `process()` | None |

## A/B cases (against the Live target)

| Case | Stimulus | Our settings | Benchmark settings | What to listen for |
|---|---|---|---|---|
| Up-down | A held Cmaj7 | Up-down, 1/16, Gate 80 %, Steps 2, Distance 12 | The same in Live | The same notes and timing |
| Swing | A held triad | 1/16, Swing 30 % | Live's Groove at a matching swing | A similar feel |

## A/B log

## Sign-off

- [ ] Automated: unit, conformance and golden tests; validators
- [ ] Layout lint passes (`fd_panel_tests`)
- [ ] Snapshots reviewed (100% / 200%; expanded, collapsed, folded)
- [ ] Feel signed off by the product owner (date):
