# 0056. Racks are modules of modules, saved as their own files

- **Status:** Accepted (the product owner, 2026-10-14: the design, then its UI drawn in every state); where the browser lists racks superseded by [0059](0059-racks-are-a-browser-part-and-every-part-has-its-icon.md) the same day: a Racks part of its own, racks of both kinds together
- **Date:** 2026-10-14
- **Builds on:** [0002](0002-device-suite-and-daw-in-parallel.md) (the DAW's native chain: "racks and macros later"), [0022](0022-module-panels.md) (module panels), [0044](0044-presets-are-files.md) (presets are files), [0046](0046-one-browser-modules-are-folders.md) (one browser), [0053](0053-a-modules-editor-in-the-detail-panel.md) (a module's editor in the detail panel), [0055](0055-factory-content-is-files-in-a-read-only-library.md) (factory content in the Library)

## Context
- A sound is often a chain of modules: an instrument and its effects, or a few effects that work together. Nothing keeps one whole: it can't be saved, found in the browser or put on another track except by copying the track.
- One knob can't move several parameters across modules. Every DAW the product owner compares with has a container with macros: Live's racks, Bitwig's containers, Studio One's FX Chains, Logic's Smart Controls, Reason's Combinator.
- The product owner asked for racks kept apart from module presets, which never gain a rack variant, and for ideas from Transit's one-knob transitions.
- tracktion_engine has racks (a graph of plugins), macro parameters with ranged, curved assignments, and modifiers (LFOs, followers, step sequencers), but not racks inside racks.

## Decision
- **A rack is a module that holds modules:** chains side by side, each serial, summed, each with a mixer row. It's an instrument rack when an instrument starts one of its chains, else an effect rack. It moves, folds, turns off and automates as any module.
- **Racks don't nest.** Zones and the Drum Rack come in M4.
- **A rack has eight macros.** Each maps parameters across the rack, every mapping with its own range (From, To) and Curve. A mapped parameter follows its macro and isn't dragged by hand.
- **A rack is saved as a `.segnorack` file**, apart from module presets: its chains, their modules' states and mixer rows, its macros and mappings. Not automation or routing. Yours go in the User library's Racks folder; factory racks are files in the Library.
- ~~**The browser lists racks in a Racks folder** at the top of Instruments and of Audio effects.~~ Superseded by [0059](0059-racks-are-a-browser-part-and-every-part-has-its-icon.md): racks are a part of the browser of their own, both kinds together.
- **Racks are a DAW feature,** built on tracktion's racks and macro modifiers. The plugins stay one module each.
- **Later, macros gain snapshots** blended by an automatable Morph, Vary with locks, and motion of their own (LFO, follower, sequencer, gate, random), from Transit, on the same modifiers.

## Consequences
- A patch made of many modules is kept, found and reused whole; one knob, or one automation lane, moves a whole sound.
- The snapshot model and the chain view gain nesting one level deep; the panel kit's knobs learn that a host can set them (a macro's ring, no drag).
- `.segnorack` is a new permanent identifier (`docs/ids.md`).
- Live's macro variations become blendable here (the innovation backlog), and M4's DAW-wide modulators arrive early, for macros first.
