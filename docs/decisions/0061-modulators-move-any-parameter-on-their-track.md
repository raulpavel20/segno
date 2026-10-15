# 0061. Modulators move any parameter on their track, each target with its own copy

- **Status:** Proposed (reviewed by the product owner, 2026-10-15: approved, with modulators anywhere in the chain instead of a zone of their own; the revision for a last look, with [modulators.md](../design/modulators.md))
- **Date:** 2026-10-15
- **Builds on:** [0060](0060-motion-comes-from-modulation-modules.md) (motion comes from modulation modules), [0022](0022-module-panels.md) (module panels), [0044](0044-presets-are-files.md) (presets), [0046](0046-one-browser-modules-are-folders.md) and [0059](0059-racks-are-a-browser-part-and-every-part-has-its-icon.md) (the browser), [0025](0025-sidechain-routing.md) (sidechains), [0040](0040-a-menu-bar-and-everything-by-mouse.md) (the menu bar), [0021](0021-mcp-server.md) (assistants), [0052](0052-generic-names-are-free.md) (generic names)
- **Moves:** M4's modulators to M3 (P5), at the product owner's request (2026-10-15)

## Context
- ADR 0060 decided that motion comes from a collection of modulation modules, to be designed later. The product owner asked to start them on 2026-10-15, with GRM Tools Atelier's modulation as the inspiration for something new.
- **The field** (modulators.md §2):
  - Live puts its modulators in the chain as Max for Live devices.
  - Bitwig has dozens, in slots on any device.
  - Cubase 14 gives each track up to eight, in a tab of their own.
  - Atelier has three deep ones, sharing one vocabulary. Each target gets its own engine ("polyadic"), and every slider shows its moving value.
- **What Segno already has:**
  - The panel kit shows modulation: rings, a source's handle dragged onto a knob, Modulate with ▸. Its words are **route** and **target**.
  - The Wavetable's modulation has **Per Note**, Smooth, Mode, and the ADSR words.
  - tracktion's modifiers live on every track and in every rack. Every assignment reads one shared value, once per audio block.
- **A conflict in the docs:** vision.md lists modulators among what ships as plugins. But a plugin in another host can't move other plugins' parameters, and device-suite.md already puts modulators in the DAW.

## Decision
- **They're called Modulators, and each one is a modulator:** a module that makes no sound and moves other parameters. Not "CV tools" (jargon, an Ableton pack's name, and a hardware non-goal), and not "Modulation" (already a section and page name).
- **The first set is LFO, Random, Envelope, Follower and Steps.** Expression and Gesture come later.
- **They go anywhere in the chain, like any module,** each panel on the module grid. A modulator looks like one (its icon, no level meters). Where it sits doesn't change what it moves, except for the Follower, which listens there. Group into rack takes modulators along; inside a rack, a modulator moves only what's in it.
- **They move any automatable parameter on their own track:**
  - modules, ours and plugins;
  - a rack's macros, Morph and chain rows;
  - the track's volume, pan and sends;
  - other modulators.

  Not another track's parameters; the Follower listens to another track's sound instead.
- **Modulation moves around where the parameter is set.** Each route has an amount from −200 to +200 %. Routes add up, and the sum is clamped to the parameter's range. Switches and choices move as knobs with steps. Loops are refused. A modulated knob can still be set by hand, and saved values leave modulation out.
- **Per Target** is on by default for Random, Envelope and Steps: each target gets its own copy, with its own phase, random path and chances. It's off by default for the LFO, whose copies spread their phases when it's on. The Follower has no such switch. While Per Target is on, the display draws one trace per target, laid over each other, each a step fainter.
- **One vocabulary:** the Wavetable's words where it has them (Rate, Sync, Mode, Phase, Smooth, the ADSR words, and Per Target beside Per Note), and Atelier's where it doesn't (Slant, Jitter, Chance, Cyclic).
- **Every moved control shows its moving value,** and hovering a modulator draws lines to its targets.
- **Randomness follows [ADR 0062](0062-random-rolls-again-each-time-it-plays.md).**
- **Modulators belong to the DAW.** The plugins keep the modulation inside each module.
- **The UI is drawn in every state and approved before it's built,** and the product owner tries it hands-on before sign-off.

## Consequences
- **New:**
  - modulator signals as pure functions in `libs/dsp`, and our own tracktion modifiers;
  - a moving-value dot and hover lines in the panel kit;
  - the browser's Modulators part and its icon;
  - the Module and Song menu items;
  - the MCP tools `get_modulators`, `route_modulator`, `set_route`, `remove_route` and `keep_roll`;
  - the type IDs `mod-lfo`, `mod-random`, `mod-envelope`, `mod-follower` and `mod-steps`, listed in `docs/ids.md` when approved.
- **Engine work, decided by a spike:**
  - per-route state for Per Target, either one engine modifier per route or a small tracktion patch;
  - a modulator's place in the chain, either as a pass-through plugin that owns its modifier or kept in our chain model;
  - the Follower listening mid-chain;
  - the block-rate steps on plugins, measured.
- **When this is accepted:**
  - vision.md is corrected: the modules ship as plugins, and the modulators are the DAW's;
  - the roadmap's M4 keeps only the modulators that come later.
- The MIDI effect planned for M6 as "Random" gets another name when its SPEC is written.
