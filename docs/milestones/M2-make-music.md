# M2: Make music

**Goal:** finish a track in the DAW with only our devices. That means:
- MIDI parts written in a piano roll;
- audio and MIDI recorded in;
- a mixer with sends and returns;
- automation;
- dynamics on tracks and the master;
- an exported file.

See [roadmap.md](../roadmap.md) and the device specs in `devices/*/SPEC.md`.

**Status:** ◐ in progress (started 2026-10-09). M1's product-owner sign-offs and its framework freeze ADR run alongside, since they only need listening and feel checks.

## Steps

### A. Framework

| # | Step | Done when | Status |
|---|---|---|---|
| A1 | **Dynamics core** (`libs/dsp`), shared by the Compressor, Gate and Limiter, and later by Multiband Dynamics and Drum Bus. It covers:<ul><li>level detection (peak and RMS, stereo link);</li><li>a gain computer (threshold, ratio and soft knee, for both compression and expansion/gating);</li><li>attack and release smoothing, including program-dependent release;</li><li>a lookahead delay;</li><li>true-peak detection (4× oversampled, ITU-R BS.1770);</li><li>K-weighted loudness (LUFS momentary, short-term, integrated).</li></ul> | Unit tests:<ul><li>static curves within 0.01 dB of the formula;</li><li>attack and release times within ±5 %;</li><li>inter-sample peaks found within 0.2 dB;</li><li>LUFS within ±0.1 LU on generated reference tones.</li></ul> | ☐ |
| A2 | **Sidechain input** in both hosts. A device declares a sidechain bus:<ul><li>the plugin adapter exposes it as an auxiliary input (VST3, CLAP, AU);</li><li>the DAW adapter feeds it from a chosen track (tracktion appends sidechain channels after the main ones);</li><li>the device header picks the source.</li></ul> | <ul><li>Conformance: with no sidechain connected, a device behaves exactly as without one.</li><li>Integration: a compressor on one track ducks to another track.</li></ul> | ☐ |

### B. Devices (each: SPEC → DSP + tests + goldens → panel → plugin → Definition of Done)

| # | Device | Status |
|---|---|---|
| B1 | **Compressor**, with a Glue character ([SPEC](../../devices/compressor/SPEC.md)) | ☐ spec draft |
| B2 | **Gate** ([SPEC](../../devices/gate/SPEC.md)) | ☐ spec draft |
| B3 | **Limiter** ([SPEC](../../devices/limiter/SPEC.md)) | ☐ spec draft |
| B4 | **Spectrum** ([SPEC](../../devices/spectrum/SPEC.md)) | ☐ spec draft |

### C. DAW

| # | Step | Done when | Status |
|---|---|---|---|
| C1 | **Command palette** (Ctrl+K): fuzzy search over every action, showing each action's shortcut, recently used first (ui-principles §1). | Ranking tests; every registered action is reachable | ☐ |
| C2 | **MIDI clips and piano roll** (clip view in the detail panel):<ul><li>create a clip (double-click an empty lane);</li><li>draw, select, move, resize and delete notes;</li><li>a velocity lane;</li><li>grid and snap, quantise;</li><li>a keyboard that previews notes;</li><li>edits undoable, one step per gesture.</li></ul> | Model tests (note edits, quantise); Session tests; manual feel check | ☐ |
| C3 | **Mixer**:<ul><li>volume, pan, mute and solo per track (track header and inspector);</li><li>a master track;</li><li>return tracks with pre- or post-fader sends;</li><li>level meters in the track headers.</li></ul> | Gain and pan laws exact; a neutral mix nulls against its sources | ☐ |
| C4 | **Automation**:<ul><li>lanes under a track for any parameter;</li><li>breakpoints with curves, drawn and edited;</li><li>recording parameter moves while playing;</li><li>automated controls show an indicator;</li><li>lane fills use the reserved gradient (ui-principles §4).</li></ul> | Model tests; round trip in `.fdproj`; manual check | ☐ |
| C5 | **Recording**:<ul><li>audio and MIDI into clips on the armed track (live MIDI already follows the selection);</li><li>an input choice per track (audio and MIDI at once, ADR 0019);</li><li>count-in;</li><li>monitoring.</li></ul> | Integration tests record from a virtual MIDI input and from a test audio input | ☐ |
| C6 | **Clip fades and crossfades** (ADR 0019): fade handles on audio clips, short automatic fades at clip edges, crossfades where audio clips meet. | Edits stay click-free (click score); manual check | ☐ |
| C7 | **Bounce in place:** render a clip, or a time range of a track, to audio on the same track; the MIDI is kept, muted (ADR 0019). | Bounce nulls against the live render | ☐ |
| C8 | **Export:**<ul><li>the mix or the loop range to WAV or FLAC;</li><li>bit depth, sample rate, dither and normalise options;</li><li>progress shown in the window, never modal.</li></ul> | The exported file nulls against an offline render | ☐ |

## Order of work

1. **C1, the command palette.** It is small, makes every action reachable, and is used daily from then on.
2. **C2, the piano roll.** Without it you can't write music in the DAW.
3. **A1, then B1 and B2 (Compressor, Gate), then A2 (sidechain), then B3 (Limiter).** One detector core, three devices.
4. **C3 the mixer and C4 automation.** The mixer gives the master track a place for the Limiter.
5. **C5 recording, then C7 bounce in place.**
6. **C6 fades, C8 export, B4 Spectrum.**

## Carried over from M1

- Product-owner sign-offs (sound and feel) for the four M1 devices and the DAW slice ([manual checks](../manual-tests/M1.md)); then the framework-freeze ADR.
- Subtractive Synth: drag envelopes on their displays; factory presets.

## Exit criteria

**Automated:**
- Every M2 device meets the Device Definition of Done except the product owner's sign-off.
- Validators pass; frame budgets hold; the DAW tests pass.

**Product owner:**
- Device specs approved; each device signed off by ear and by feel.
- A complete short track made with only M2 features and our devices, exported to a file.
