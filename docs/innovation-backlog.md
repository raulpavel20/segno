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
| **Project-wide key and scale**, shared by the piano roll, MIDI effects, pitch devices and tuner | Live 12's scale awareness, extended to everything | 📌 M4 |
| **Take lanes and comping** for audio and MIDI | Standard in modern DAWs | 💡 |
| **Clip gain and clip effects:** gain envelopes and effects on individual clips, not only tracks | Fast fixes without automation lanes | 💡 |
| **Project versions:** named snapshots of a whole project with instant A/B (e.g. "mix v3 vs v4") | Safe experimentation, comparing mixes | 💡 |
| **Undo history panel**, including branches | Lets you see and revisit earlier states | 💡 |
| **Scratch lane:** an area beside the arrangement for parking ideas | Keeps the arrangement clean while you sketch | 💡 |

## Mixing

| Idea | Why | Status |
|---|---|---|
| **Cross-track masking overlay** in the EQ: see other tracks' spectra and their collisions | The DAW knows every track. Plugins need workarounds for this. | 📌 M3 (EQ stage 2) |
| **Reference track A/B** built into the master channel, with loudness matching | Every mix engineer does this with a third-party plugin today | 📌 M3 |
| **Sidechain by drag:** drag from any track onto a device's sidechain input | Removes routing dialogs | 💡 |
| **Gain-staging assistant:** see levels across the project and auto-level on import | Common beginner problem, and a time saver | 💡 |

## Sound design and modulation

| Idea | Why | Status |
|---|---|---|
| **Drag-to-modulate:** drop any modulator on any parameter of any device; modulation is shown on every control | Faster and more visible than Live's modulation workflow | 📌 M4 |
| **Macro snapshots with morphing** in racks | Live's macro variations, but blendable and automatable | 💡 (variations are M4; morphing later) |
| **Per-note expression (MPE) everywhere** it makes sense | Expressive playing across the whole suite | 📌 M5 |
| **Audio → wavetable resynthesis** in the wavetable synth | Turn any sound into an instrument | 💡 |
| **Wavetable editor** (draw, harmonics, morph) | Create tables without external tools | 💡 |
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
