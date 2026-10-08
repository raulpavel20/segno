# M1: Vertical slice

**Goal:** the first version you can actually make music with. It has:
- four real devices, one per category: EQ (core), Saturator, Subtractive Synth (core), Sampler (classic/one-shot);
- the first real DAW shell: an arrangement with cached waveforms, transport, a docked device chain with native and third-party plugins, live MIDI input, and save/open.

At the end of M1 the **device API is frozen** (any later change needs an ADR and a migration).

See [roadmap.md](../roadmap.md) and the device specs in `devices/*/SPEC.md`.

**Status:** ◐ in progress (started 2026-10-09)

## Steps

### A. Framework completion (the API that gets frozen)

| # | Step | Done when | Status |
|---|---|---|---|
| A1 | **DSP building blocks:** FIR half-band oversampler (2/4/8×, integer latency), anti-aliased (ADAA) waveshapers, TPT state-variable and ladder filters, analog-matched second-order sections and Butterworth cascades, ADSR, LFO, band-limited oscillators, sample interpolation | Unit tests incl. measured aliasing and response targets | ☑ incl. windowed-sinc sample interpolation and decimation (`dsp/Resampling.h`) |
| A2 | **Test kit additions:** THD, aliasing ratio and harmonic profile metrics; MIDI test scripts | Metrics detect what they should (naive saw fails, BLEP saw passes) | ◐ metrics done (exact, leakage-free); MIDI scripts with A3 |
| A3 | **Instrument support:** voice manager (poly/mono/legato, stealing, sustain pedal, pitch bend, note ids); MIDI in the tracktion adapter; instrument conformance checks (notes always release, all-notes-off, stealing without clicks, block-size invariance with events) | Conformance passes for a test instrument | ☑ `VoiceManager`; MIDI in both adapters; instrument conformance checks; instrument null test in tracktion |
| A4 | **Analysis taps:** lock-free sample FIFO from DSP to UI (spectra, scopes, playheads) | Tested SPSC; no allocation on the audio thread | ☑ `SampleFifo` on `AnalysisBus` |
| A5 | **Heavy and non-parameter state:** real-time-safe handoff for large data (samples); `ParamBinding::applyExtraState` so UI edits to non-parameter state go through the host (undoable in the DAW) | Sampler can load files while playing, without glitches or allocation on the audio thread | ☑ [ADR 0017](../decisions/0017-heavy-data-and-non-parameter-state.md): `AssetHandoff`, `applyExtraState` in all hosts (DAW: undoable), `libs/audiofile` |
| A6 | **Latency policy:** constant latency per device instance (e.g. oversampling factors are padded to one latency) | ADR; conformance checks reported vs measured latency for every setting | ☑ [ADR 0016](../decisions/0016-constant-device-latency.md) |

### B. Devices (each: SPEC → DSP + tests + goldens → panel → plugin → Definition of Done)

| # | Device | Status |
|---|---|---|
| B1 | **Saturator** ([SPEC](../../devices/saturator/SPEC.md)) | ◐ automated DoD met; awaiting spec approval and sound/feel sign-off |
| B2 | **EQ** (core) ([SPEC](../../devices/eq/SPEC.md)) | ◐ engine + display done, validators pass; compact view deferred (open question B12); awaiting spec approval and sign-off |
| B3 | **Subtractive Synth** (core) ([SPEC](../../devices/subtractive-synth/SPEC.md)) | ◐ engine, panel, plugin, goldens; validators and null test pass; still to do: drag envelopes on their displays, factory presets; awaiting spec approval and sound/feel sign-off |
| B4 | **Sampler** (classic/one-shot) ([SPEC](../../devices/sampler/SPEC.md)) | ◐ engine, panel (waveform with markers, drop/browse), plugin, goldens; spec tests, conformance, validators and DAW null and undo tests pass; awaiting spec approval and sound/feel sign-off |

### C. DAW slice

| # | Step | Done when | Status |
|---|---|---|---|
| C1 | **Shell:** OpenGL top level ([ADR 0015](../decisions/0015-rendering-opengl-for-the-daw.md)); app `UIBehaviour` (progress, warnings); layout: transport bar, browser stub, arrangement, detail panel; action registry + shortcuts | Window matches the UI principles layout; actions have shortcuts | ◐ shell, adaptive panels, `ActionRegistry` with a shortcut for every action, popover audio settings, in-window warnings ([design](../design/daw-shell.md), [ADR 0018](../decisions/0018-daw-app-structure.md)); awaiting feel check |
| C2 | **Timeline model and arrangement view:** pure, tested `TimelineModel` (time↔pixel, zoom anchored at the mouse, animated zoom, adaptive grid, snapping); tracks, ruler, playhead; trackpad pinch and scroll | Model tests; smooth on both displays | ◐ `model::Timeline`/`TrackLayout` tested; arrangement with ruler zoom/scroll, wheel, pinch, snapping, file drops; awaiting smoothness check (Release) |
| C3 | **Waveform cache:** background peak building per file plus a per-zoom tile cache | Paint benchmark: 32 tracks zooming under 8 ms on the reference machine | ☑ peaks per file (background), GL tiles per file and zoom; 1.9 ms avg / 6.3 ms p95 at 4K ([numbers](../design/daw-shell.md#performance)) |
| C4 | **Clip editing:** drag files in, move with snap, trim, split, duplicate, delete, box select; undo/redo | Edit tests on the TE model; manual feel check | ◐ select (click, Shift/Ctrl, box), move with snap (Alt free) across tracks, trim either edge within the source, split at playhead (Ctrl+E), duplicate (Ctrl+D), delete; one undo step per gesture; model and Session tests plus a mouse-driven test; awaiting feel check |
| C5 | **Transport:** play/stop, position, tempo, loop brace, metronome | Manual check | ◐ play/stop, return to start, click to locate, loop brace (draw/move/resize/toggle), metronome, tempo and signature fields (undoable), follow playhead; Session and mouse-driven tests; awaiting manual check |
| C6 | **Device chain:** native panels plus third-party plugin cards; add, reorder, bypass, delete; VST3/LV2 scanning (crash-safe) | Native and third-party devices on one track | ◐ chain in the detail panel: native panels + plugin cards (own editor window), add menu, header-drag reorder, bypass, delete, undo; in-process scan with dead man's pedal (out-of-process scanner is M3); Session and UI tests incl. our VST3 as a stand-in plugin; awaiting manual check with real third-party plugins |
| C7 | **Instrument tracks:** live MIDI input monitoring through the synth or sampler; `.mid` import as a read-only preview | Play the synth from a MIDI keyboard | ◐ instruments on any track (ADR 0019), live MIDI routed to the selected instrument track (follows device changes), computer keyboard as MIDI, `.mid` drop as a read-only note clip; Session tests; awaiting playing the synth from a MIDI keyboard |
| C8 | **Project save/open** (`.fdproj`) | Round-trip test | ☑ `.fdproj` (tracktion edit + our view state), new/open/save/save as, unsaved-changes prompt, title; round-trip test (tracks, clips, devices incl. parameters and extra state, tempo, loop, view) |

## Order of work

1. A1–A2 and A6, then **Saturator** (B1). It exercises oversampling, ADAA, latency and a transfer-curve display.
2. A4, then **EQ** (B2). It exercises analysis taps, a large display you edit directly, and many parameters.
3. A3, then **Subtractive Synth** (B3). It exercises the voice manager, MIDI in both hosts, oscillators, filters and envelopes.
4. A5, then **Sampler** (B4). It exercises heavy-data handoff and non-parameter state.
5. C1–C8, interleaved with the devices once the shell exists, so new devices can be tried inside the DAW.

## Exit criteria

**Automated:**
- Every M1 device meets the Device Definition of Done (`docs/devices/device-suite.md`) except the product owner's sign-off.
- Plugin validators pass; frame budgets are met; the DAW tests pass.

**Product owner:**
- Device specs approved.
- Each device signed off by ear and by feel.
- The DAW slice used to make a short loop: audio clips plus a synth part played live, with the devices in the chain.
- Framework freeze recorded as an ADR.
