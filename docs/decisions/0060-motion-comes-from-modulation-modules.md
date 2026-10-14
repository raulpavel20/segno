# 0060. Motion comes from modulation modules, not from inside racks

- **Status:** Accepted (product owner, 2026-10-15); the modules themselves to be designed later
- **Date:** 2026-10-15
- **Supersedes in part:** [0056](0056-racks-are-modules-of-modules.md): "motion of their own (LFO, follower, sequencer, gate, random), from Transit, on the same modifiers"
- **Shapes:** M4's modulators with drag-to-modulate ([roadmap](../roadmap.md))

## Context
- Racks were planned in three steps (racks.md §14). Steps 1 and 2 (racks of chains with macros; snapshots, Morph, locks and Vary) are built and signed off. Step 3 would have given a rack's macros motion of their own: an LFO, a follower, a sequencer, a gate, a random source, set up inside the rack.
- M4 also plans modulators that drag onto any parameter. Built as racks.md planned, modulation would be designed twice: once inside racks, once for everything else.
- Some modules already carry modulation of their own (the Filter's LFO and envelope, the Wavetable's sources).

## Decision
- **Racks get no motion of their own:** their step 3 is dropped.
- **Motion comes from a collection of modulation modules** (an LFO, an envelope follower, and others), placed in a track like any module. Each can drive any parameter: a rack's Morph or macros, or a module's. An LFO tied to Morph moves a whole rack between its snapshots.
- **Modulation is built once, modularly in the track,** rather than written into each module. Modules keep what they already have.
- The collection, its modules, its names (generic, ADR 0052) and how a module is tied to a parameter are designed later, with M4's modulators.

## Consequences
- A rack's Morph and macros, being parameters, are moved by the same modules as everything else, and so are automation and macros today.
- One design for modulation instead of one per module, or one inside racks and another outside them.
- Until the collection exists, a rack's macros and Morph move by hand and by automation.
