# Melody: device spec

- **Status:** Early draft (midi-effects.md §18, step 0): drawn now as the generators' example, so the product owner sees what a generator looks like before step 1. Its full review, and its IDs, come with step 2.
- **Type:** MIDI effect (a generator)
- **Tier / milestone:** M3 P11 step 2 ([midi-effects.md](../../docs/design/midi-effects.md) §6.11, [ADR 0074](../../docs/decisions/0074-midi-effects-know-the-song.md))
- **Live target:** the Sequencers pack's Melodic Steps and SQ Sequencer (lanes, lengths and directions, conditions); the piano roll's Generate ▸ Melody, which it shares its algorithm with. Beyond them, the ideas of Marbles (random that loops, or shuffles), the Turing Machine (slow change), the Metropolix's accumulator, the NDLR's chord-tone motifs and the research on lines that sound meant (midi-effects.md §2.3).

## Purpose

A step sequencer that also writes. Draw the steps, or let it write a line from the song (chord tones on strong beats, steps of the key between, a motif and its varied repeat), and let it change as it goes. Drop a clip on it and it varies your own idea.

## Parameters (draft)

| ID | Name | Description | Range | Default |
|---|---|---|---|---|
| `sync`, `rate`, `hz` | Sync, Rate | As the Arpeggiator's | | 1/16 |
| `<lane>.length` | *Lane* length | Pitch, Velocity, Gate, Chance, Ratchet, Slide: steps before the lane starts again | 1 … 64 | 16 |
| `density` | Density | How many steps it writes notes on | 0 … 100 % | 60 % |
| `leaps` | Leaps | Steps of the key, or leaps | 0 … 100 % | 25 % |
| `contour` | Contour | The line's shape over its length | Rise, Fall, Arch, Wave, Any | Arch |
| `phrase` | Phrase | A motif and its repeats | Free, AA′, AABA, ABAB | AA′ |
| `lowest`, `highest` | Lowest, Highest | The range it writes in | notes | C3, C5 |
| `change` | Change | How much each lap differs from the last; 0 loops | 0 … 100 % | 0 |
| `change.mode` | Change mode | New notes, or the same notes in new orders | New, Shuffle | New |
| `climb` | Climb | Steps of the key the line moves each lap, home at each section | −7 … 7 | 0 |
| `plays` | Plays | Where its track has clips, or whenever the song plays | In clips, Always | In clips |
| `sections` | Sections | Each section's name its own line, each section its own, or one line | By name, Each, One | By name |
| `notes` | Notes played | Played notes, and a clip's, transpose it, restart it, or are ignored | Transpose, Restart, Ignore | Transpose |
| `answer` | Answer | With Listen on another part: plays in its rests, against it, or neither | Off, Answer, Counter | Off |
| `fill` | Before a section | A fill, a drop-out, or nothing | None, Fill, Drop out | None |

And HARMONY's, as the [Pitch's](../pitch/SPEC.md). **Non-parameter state:** the steps (a chord tone, a step of the key or a note; velocity, gate, chance, ratchets, slide, a condition and a lock each), the kept roll.

## UI layout (drawn, provisional)

| Section | Controls |
|---|---|
| `steps` STEPS [8] | The lane chooser (0, 0, 6): Pitch \| Velocity \| Gate \| Chance \| Ratchet \| Slide; the display (0, 1, 8 × 5): the steps (the pitch lane on the chord's and key's rows, locks on steps), the playhead, the chord now and the next |
| `write` WRITE [4] | Density (0, 0) · Leaps (1, 0) · Lowest (2, 0) · Highest (3, 0) · "Contour  Arch" (0, 3, 4) · "Phrase  AA′" (0, 4, 4) · Vary (0, 5, 2) button · Learn… (2, 5, 2) button |
| `change` CHANGE [2] | Change (0, 0) · Climb (1, 0) · New \| Shuffle (0, 3, 2) · Keep (0, 4, 2) |
| `play` PLAY [3] | Rate (0, 0), following Sync · Sync (1, 1) toggle, beside the dial · "Plays  In clips" (0, 3, 3) · "Sections  By name" (0, 4, 3) · "Notes  Transpose" (0, 5, 3) |
| `harmony` HARMONY [3] | As the Pitch's; collapsed by default |

Answer and Before a section wait for step 2's review (the header's Listen and the form's conditions).
