# 0065. Granular goes beyond Live, CPU first

- **Status:** Accepted (product owner, 2026-10-16, with Granular's SPEC and every recommendation of G1–G24)
- **Date:** 2026-10-16
- **Amends:** [0011](0011-calibrated-ambition-and-consolidation.md), point 2 ("instruments stay close to Live's stock instruments"), for Granular only, as [0050](0050-the-wavetable-synth-goes-beyond-live.md) did for the Wavetable
- **Device:** [devices/granular/SPEC.md](../../devices/granular/SPEC.md)

## Context
- The product owner chose Granular as the next instrument ([next-synth.md](../devices/next-synth.md)): nothing in Segno turns recordings into textures, it's free software's weakest area, and every major DAW now has one.
- They asked for it to be "extensive and featureful, such that for granular workflows most will not feel the need for other plugins": Granulator III plus extras, with room for experimental workflows. And, since granular synthesis is CPU-heavy, for optimisation to be "even more of a focus than usual … optimise everything where possible".
- Live's Granulator III has one sample, three modes, a fixed short list of modulation sources per knob, and up to 20 grains per channel in its Cloud mode, where Shape is fixed to save CPU. Its captures are lost unless saved.
- The leading granular instruments go further: several sources, live input, per-grain filters, pitch quantised to a scale, pitched and spectral modes. Users name CPU, clicks, single sources and hard-to-read UIs as their complaints (the research in the SPEC).

## Decision
- **Granular aims at the depth of the leading granular instruments:**
  - two sources per instrument, each a file, a Library instrument, a clip or the live input;
  - six modes (Classic, Loop and Cloud as Granulator's; Pitched; Rhythm; Spectral), and Fire in any mode;
  - a filter and colour on every grain;
  - pitch that lands on the song's key or a chord;
  - a full modulation system as the Wavetable's;
  - live input from any track, with captures kept as files in the project.
- **Its innovation goes where the integrated DAW helps:** the song's key, tempo and position (a playhead that follows the song), clips and tracks as sources, captures kept with the project, every grain visible as it plays.
- **Experiments are in when they're cheap:** Rhythm with Chance, Repeats and Swing; random values per grain; glides within a grain; feedback into the live buffer; both sources on every grain.
- **CPU is a design constraint, not a later pass.**
  - Grains are rendered one at a time over the block in two passes built for SIMD.
  - Everything that can be per grain is computed once per grain.
  - Pitched grains read a pyramid of the source, so no grain needs oversampling.
  - The grain budget is fixed (Max Grains per note, 1 024 per instance), never measured, so renders repeat.
  - The CPU and the grains playing show in the header.
  - Release benchmarks hold it to budgets: 8 notes × 64 grains under 4 % of a core.
- It's built in three steps (the instrument, live and the source editor, spectral), each signed off on its own.
- ADR 0011 holds for every other instrument. Raising another needs its own decision.

## Consequences
- The second-largest device after the Wavetable: about 340 parameters, a source editor in the detail panel, its own source pyramid and analysis, and the first SIMD kernel in the codebase, compiled twice and chosen at run time.
- The kernel's benchmarks and their budgets become the model for B74's CPU budget per module.
- Factory content grows: about 56 presets from the Library's instruments and a few sources of Segno's own, and factory racks pairing Granular with a space.
- Granular's matrix is its own for now; sharing one with the Wavetable is a later refactor.
