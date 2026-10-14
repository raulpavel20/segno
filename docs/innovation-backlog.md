# Innovation backlog

Ideas for going beyond Live ([ADR 0011](decisions/0011-calibrated-ambition-and-consolidation.md)). Devices match Live by default. Ideas that would expand a device past its Live target, or that cut across devices, or that only the integrated DAW makes possible, are collected here and scheduled only by an explicit roadmap decision.

**Status:** 💡 idea · 📌 scheduled (milestone given) · ✅ shipped · ✖ rejected (keep it, with the reason)

To schedule an idea, it needs:
- the user problem it solves;
- a check that it respects the [UI principles](design/ui-principles.md);
- an entry in [roadmap.md](roadmap.md).

## Arrangement and workflow

| Idea | Why | Status |
|---|---|---|
| **Arranger track:** song sections (intro, verse, drop…) that you can move, duplicate and reorder with all their content | Arrangement is our core workflow, and Live only offers locators | 📌 M3 |
| **Project-wide key and scale**, shared by the piano roll, MIDI effects, pitch devices and tuner | Live 12's scale awareness, extended to everything | 📌 M3 (P3, [key-and-scale.md](design/key-and-scale.md)) |
| **Take lanes and comping** for audio and MIDI | Standard in modern DAWs | 📌 M2 (C5b, C5c; [recording.md](design/recording.md)) |
| **Capture audio:** armed audio inputs are always buffered, so a moment played before pressing Record can be kept, as Capture MIDI does for notes | Nothing played is lost, not only MIDI | 💡 ([recording.md](design/recording.md) §15) |
| **Stack mode** for loop recording: each lap goes straight onto a new track instead of a take lane | Doubles and layered parts while you play | 💡 |
| **Tempo from captured MIDI:** capturing into an empty, stopped project sets the tempo from what was played | Start from a played idea without a click | 💡 |
| **Take ranking by assistants:** an assistant renders and measures each take (level, timing against the grid, pitch) and suggests the best | Faster choices among many takes; the user decides | 💡 (the tools exist with C5b) |
| **Clip gain and clip effects:** gain envelopes and effects on individual clips, not only tracks | Fast fixes without automation lanes | 💡 |
| **Project versions:** named snapshots of a whole project with instant A/B (e.g. "mix v3 vs v4") | Safe experimentation, comparing mixes | 💡 |
| **Undo history panel**, including branches | Lets you see and revisit earlier states | 💡 |
| **Scratch lane:** an area beside the arrangement for parking ideas | Keeps the arrangement clean while you sketch | 💡 |

## Assistants

| Idea | Why | Status |
|---|---|---|
| **Built-in MCP server:** any assistant the user chooses can drive the whole DAW and every device in it: editing, devices, transport, render and analysis ([ADR 0021](decisions/0021-mcp-server.md)) | Existing DAW integrations work from outside and see only part of the program; this DAW was designed for it. It also helps accessibility | 📌 M2 (C9) |
| **Built-in assistant** (the DAW as an MCP client, with a chat panel) | Help without leaving the DAW | 💡 (needs its own ADR) |

## Mixing

| Idea | Why | Status |
|---|---|---|
| **Cross-track masking overlay** in the EQ: see other tracks' spectra and their collisions | The DAW knows every track. Plugins need workarounds for this. | 📌 M3 (EQ stage 2); the Spectrum's Compare (M2) brings the shared analysis and the collision rule |
| **Reference track A/B** built into the master channel, with loudness matching | Every mix engineer does this with a third-party plugin today | 📌 M3; the Spectrum shows a reference's curve from M2 |
| **Sidechain by drag:** drag from any track onto a device's sidechain input | Removes routing dialogs | 💡 |
| **Gain-staging assistant:** see levels across the project and auto-level on import | Common beginner problem, and a time saver | 💡 |
| **Mixer strips in the detail panel:** a compact row of channel strips under the arrangement, so both show at once (the mixer view replaces the arrangement) | Mixing while watching the arrangement, on large screens | 💡 after C3 (the mixer view is in M2, [mixer.md](design/mixer.md) §6) |

## Sound design and modulation

| Idea | Why | Status |
|---|---|---|
| **Drag-to-modulate:** drop any modulator on any parameter of any device; modulation is shown on every control | Faster and more visible than Live's modulation workflow | 📌 M4 |
| **Macro snapshots with morphing** in racks | Live's macro variations, but blendable and automatable | 💡 (variations are M4; morphing later) |
| **Per-note expression (MPE) everywhere** it makes sense | Expressive playing across the whole suite | 📌 M5 |
| **Audio → wavetable resynthesis** in the wavetable synth | Turn any sound into an instrument | Scheduled: the Wavetable Synth, step 2 (M3 P1, ADR 0050) |
| **Wavetable editor** (draw, harmonics, morph) | Create tables without external tools | Scheduled: the Wavetable Synth, step 2 (M3 P1, ADR 0050) |
| **Larger FM synth:** more operators, any-to-any modulation matrix | Deeper FM sound design | 💡 |
| **Modular hybrid synth** (combine VA, wavetable, FM, sample and granular engines) | One instrument for complex sound design | 💡 |
| **Large distortion model library** (dozens of models per stage) | Saturn-style depth | 💡 |

## Browser and content

| Idea | Why | Status |
|---|---|---|
| **Loudness-normalised previews** in the browser | Fair comparison between sounds | 💡 |
| **Similarity search and auto-tagging** for samples (local ML, offline) | Finding sounds is slow | 💡 |
| **Stem separation and audio→MIDI on import** (local ML models) | Remixing, sampling, transcription | 💡 |
| **Unified search across devices, presets, samples and plugins** | One place to look | 📌 M3 |
