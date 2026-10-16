# 0069. Factory presets are designed sound, and never clip

- **Status:** Accepted (the product owner's rule, 2026-10-16; the numbers below set by Claude, for review)
- **Date:** 2026-10-16
- **Amends:** [0044](0044-presets-are-files.md) and [presets.md](../design/presets.md) §13 (who makes factory presets, and how they're checked)

## Context
- **The product owner, on Granular's first set** (2026-10-16): "the presets are terrible. They sound bad, most of them are in the red and they seem to use a very shallow fraction of the synth. We need to establish a clear rule right now because this seems to be popping up on every instrument. Presets need to be quality. We cannot just generate a simple set of values and call it done … research and understand deeply how a synth like that works and what are the techniques to program it well … come up with a compelling, quality set of presets that show what the synth is best, and use the whole feature set, not randomly just so you could say you did … And in no way should they ever clip, some of them are in the red all the time in the upper octaves."
- **How sets were checked until now:** each preset loads, a single C3 renders below 0 dBFS, and it differs from the others. Levels were matched at that one note.
- **Measured against a keyboard check** (single notes in every octave and four-note chords at full velocity, the song playing): 43 of Granular's 44 presets, 36 of the Wavetable's 60, 46 of the Sampler's 48 and 21 of the Subtractive's 35 peak above −1 dBFS; chords at full velocity typically reach +2 to +9 dBFS. Granular's and the Sampler's notes are 10–40 dB apart across the keyboard.

## Decision
- **Factory presets are sound design, not values.**
  - Each set starts from research: how this kind of instrument is programmed well (its manuals, the best-regarded factory libraries, sound designers' techniques), written into the set's brief (`devices/<name>/presets/BRIEF.md`): its categories, each preset's idea and the technique it shows, its macros.
  - Each preset is a finished, compelling sound that shows what the instrument does best.
  - Across a set, the whole feature set is used where it makes musical sense, never to tick it off.
  - An instrument's preset plays well: named macros on what you'd want to move (where the instrument has macros), velocity, the mod wheel and pressure where they're natural.
  - A sound that lives in a space comes as a factory rack with a reverb or a delay, beside the dry preset, rather than dry alone.
- **An instrument's presets never clip, are level-matched and play evenly** (`fd::checkKeyboard`, `libs/device/PresetCheck.h`):
  - rendered at full velocity with the song playing at 120 bpm: a single note in every octave (MIDI 24–96) and a four-note chord (a major triad and its octave) on five roots (36–84), each held 4 seconds; and the chord on C3 again with every macro at 0, every macro at full, and the mod wheel and pressure at full;
  - **no take above −1 dBFS**;
  - **the level:** about −24 dB RMS for a single note at C3 at full velocity, so a four-note chord keeps its headroom;
  - **even:** the notes that sound within 12 dB of each other in the range the preset is played in (the middle five octaves unless its recipe says: a bass's lower, a high instrument's higher).
- **How it's held to that:** `fd-presets make` refuses to write a set any preset of which fails; `fd-presets check` reports a set note by note; the Release suite runs the check on every instrument set (`ctest -L presets`); the dev suite's lint adds a four-note chord at full velocity on C3 to its single note.
- **The product owner signs each set off by ear,** as before, and what they'd change, Claude changes.

## Consequences
- **Every instrument's existing set is re-levelled now** to pass (the Wavetable's, the Subtractive's and the Sampler's, by their output levels alone), so nothing in the red ships; about 6 dB quieter than before. Their sound design is reviewed against this bar in turn, the product owner choosing the order.
- **Granular's first set is replaced** by one built to this bar, its multisamples' zones each brought to full scale so grains play evenly across the keyboard.
- **The Sampler's evenness** comes from the Library's multisamples, which keep each recording's balance between registers: the sets that exceed 12 dB are listed (B97) and fixed in the Library's zone levels, not in the presets.
- **Checking takes time:** about a minute a set in Release, an hour for every set in Debug, so the full check runs where presets are made and in the Release suite, not on every dev build.
