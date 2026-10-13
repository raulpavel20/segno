# 0050. The Wavetable Synth goes beyond Live

- **Status:** Proposed (with the Wavetable Synth's SPEC, 2026-10-13)
- **Date:** 2026-10-13
- **Amends:** [0011](0011-calibrated-ambition-and-consolidation.md), point 2 ("instruments stay close to Live's stock instruments"), for the Wavetable Synth only
- **Device:** [devices/wavetable/SPEC.md](../../devices/wavetable/SPEC.md)

## Context
- ADR 0011 set instruments to match Live's and named "targeting Serum and Vital for the wavetable synth" as unrealistic. That kept the synths' scope small while the mixing core and the DAW came first.
- On 2026-10-13 the product owner moved the Wavetable Synth up from M5 to the front of M3, asking for "more options to make music with" and a wavetable synth that is "full-featured, modern and innovative".
- Live's Wavetable is seen as basic next to the leading wavetable synths: 2 oscillators with one mode each, no noise source, no drawable LFOs, no table editor. Users also dislike that a DAW's own synths are shallower than the plugins they buy (the research in the SPEC).

## Decision
- The Wavetable Synth aims at the depth of the leading wavetable synths: 3 oscillators with a sub and noise, two warps each, 16-voice unison, 2 filters, a full modulation system, and a table editor with audio import.
- Its innovation goes where the integrated DAW helps, rather than into more of everything:
  - any sound in the project turned into a wavetable;
  - non-destructive table editing in the bottom panel;
  - the project's key;
  - variation instead of randomise;
  - CPU you can see;
  - warps that never alias.
- It is built in three steps (the synth, the tables, the song), each signed off on its own, as the EQ's stage 2 was.
- ADR 0011 holds for every other instrument. Raising another needs its own decision.

## Consequences
- The largest device so far: about 380 parameters, a table editor, new filters, and a modulation system whose drag-and-drop and rings are built in the shared panel library for M4's DAW-wide modulators too.
- The innovation backlog's audio-to-wavetable resynthesis and wavetable editor are scheduled here.
- Factory content grows: about 80 tables made in-house or from CC0 sources, and about 40 presets.
