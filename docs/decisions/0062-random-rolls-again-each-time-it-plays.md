# 0062. Random rolls again each time it plays; Keep holds a roll

- **Status:** Accepted (product owner, 2026-10-15, with [modulators.md](../design/modulators.md) §13 and [piano-roll.md](../design/piano-roll.md) §26)
- **Date:** 2026-10-15
- **Supersedes in part:** [0058](0058-the-piano-roll-writes-in-key-and-in-chords.md): "per-note chance the same in every render"
- **Applies to:** the modulators ([0061](0061-modulators-move-any-parameter-on-their-track.md)) and the piano roll's chance

## Context
- **What happens today.** P4b computes a note's chance from a hash of the clip's seed, the note and the clip's lap. Each lap of a looped clip and each copy of a clip rolls differently, but the same place in the song always gives the same result. Playback, bounce and export agree, and Re-roll gives the clip a new seed.
- **The product owner (2026-10-15):** "me setting a 20% chance on a note and then finding out that means simply that that note won't ever play there unless you move it, it's kinda lame, I would just not put the note there if that's what I wanted. Kinda defeats the purpose of using randomness." They asked for the honest pros and cons.
- **For fixed rolls:**
  - the export sounds like what you heard and mixed around;
  - bounce in place and freezing are exact;
  - the song sounds the same tomorrow, and for a collaborator;
  - chance on a looped bar writes varied laps in one gesture, and they hold still while you mix;
  - the loudness measured while playing matches the export.
- **For fresh rolls:**
  - It's what the word means everywhere else. Live's note probability, Logic's Step Sequencer and Elektron's trig conditions roll each time they play.
  - Fixed rolls on a clip that plays once are a hidden edit: the note might as well be deleted.
  - Variation over time is the point: a loop that never quite repeats, a generative piece left running, a live set.
  - A random modulator that repeats from the top is an automation lane nobody drew.
  - You explore by listening, with no Re-roll to press.
- **Tools that write notes are a different case:** their results are seen and edited.

## Decision
- **Randomness rolls again each time it plays.** This covers a note's chance and velocity spread, the Random modulator, the Envelope's and Steps' Chance, and Jitter. A new roll is drawn each time playback starts. The transport loop's lap and a looped clip's lap go into it.
- **Keep holds a roll.**
  - Segno remembers the roll that's playing, or the last one played, including the loop's lap.
  - Keep stores that roll on the clip or the modulator, which then plays it every time, as clips do today. Let it roll goes back to fresh rolls. Each is one undo step.
  - Song ▸ Keep all as played (and Let all roll) acts on everything in the song.
  - Re-roll gives a kept clip another kept roll.
- **Randomness is computed, never run.** A random value is a hash of the roll, the clip or modulator, and its place, so a kept roll renders exactly as it played.
- **Export and bounce:** kept things play as kept, and the rest rolls fresh. When the song leaves anything to chance, the export panel says so. Bounce in place keeps one roll as audio.
- **Tools that write keep their seeds:** the piano roll's Transform and Generate tools, and a rack's Vary.
- **Chance rolls by default;** keeping a roll is a choice.
- **Not changed now:** the modules' own random sources (A2's sample-and-hold and wander, and the Wavetable's random sources and start phases). Whether they follow is an open question (B86).

## Consequences
- **Engine work:** the chance decision moves from where tracktion builds a clip's playback ahead of time (`createPlaybackMidiSequence`) to where the notes play, and it stays real-time safe. A spike finds the hook, with a small tracktion patch if needed.
- **Tests:** P4b's test that a render matches playback runs with a kept roll. New tests check that two passes differ and that Keep reproduces a pass.
- **Exports of a song with unkept chance differ each time,** by design, and the export panel says so.
- **Assistants:** `keep_roll` is new, and `reroll_chance` applies to kept clips.
- **Docs:** piano-roll.md §26 revises §10 and N14; modulators.md §13 applies the rule to the modulators.
