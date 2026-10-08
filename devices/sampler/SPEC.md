# Sampler: device spec

- **Status:** Draft (review requested; IDs can still change)
- **Type:** Instrument
- **Tier / milestone:** Tier 1, M1 (classic/one-shot) → M4 (slice) → M5 (multisample)
- **Live target:** Simpler (M1, M4), Sampler (M5). References only ([ADR 0011](../../docs/decisions/0011-calibrated-ambition-and-consolidation.md)).

## Purpose

Turn any sound into an instrument: drop a file on it and play it. M1 covers Classic mode (sustain and loop) and One-shot mode (drums and hits). Later stages add slicing and multisample zones on the same engine.

## Beyond Live

None in M1.

## Stages

| Stage | Scope | Milestone |
|---|---|---|
| Core | Classic and One-shot modes, below | M1 |
| Slice | Transient-detected slices, slice to pads | M4 |
| Multisample | Key and velocity zones, round-robin, SFZ import | M5 |

## Parameters

| ID | Name | Range | Default | Notes |
|---|---|---|---|---|
| `mode` | Mode | Classic, One-shot | Classic | |
| `start` | Start | 0 … 100 % | 0 | Of the sample length |
| `end` | End | 0 … 100 % | 100 | |
| `loop` | Loop | Off / On | Off | Classic only |
| `loop.start` | Loop Start | 0 … 100 % | 0 | Relative to the start–end region |
| `loop.xfade` | Loop Crossfade | 0 … 50 % | 2 | Of the loop length |
| `root` | Root Key | 0 … 127 | 60 | Read from file metadata when present |
| `transpose` | Transpose | −48 … +48 st | 0 | |
| `detune` | Detune | −50 … +50 ct | 0 | |
| `gain` | Gain | −inf … +12 dB | 0 | |
| `vel.amp` | Velocity → Volume | 0 … 100 % | 50 | |
| `amp.a` / `amp.d` / `amp.s` / `amp.r` | Amp A/D/S/R | as Subtractive Synth | 0.5 ms, 300 ms, 100 %, 50 ms | One-shot ignores sustain and release unless `oneshot.gate` is on |
| `oneshot.gate` | Gate | Off / On | Off | One-shot: note-off fades out |
| `fade.in` / `fade.out` | Fade In / Out | 0 … 100 ms | 0.5 / 2 ms | Click-free start and end |
| `filter.on` | Filter | Off / On | Off | Same filter engine as Subtractive Synth |
| `filter.type` / `filter.cutoff` / `filter.res` / `filter.env` / `filter.key` | Filter … | as Subtractive Synth | – | |
| `fenv.a` / `fenv.d` / `fenv.s` / `fenv.r` | Filter Env A/D/S/R | as above | 2 ms, 400 ms, 0 %, 300 ms | |
| `voice.mode` | Voice Mode | Poly, Mono, Legato | Poly | |
| `voice.count` | Voices | 1 … 32 | 16 | |
| `voice.glide` | Glide | 0 … 2 s | 0 | |
| `voice.bend` | Bend Range | 0 … 24 st | 2 | |

**Non-parameter state** (`extra`):
- `sample.path`: absolute path;
- `sample.rate` and `sample.frames`: the original sample rate and length, for checking.

Project-relative paths come with collect-and-save (M3). Embedding the audio in the project or preset comes later. State version 1.

## DSP design

- **Loading ([ADR 0017](../../docs/decisions/0017-heavy-data-and-non-parameter-state.md)):**
  - Files are decoded with JUCE-free decoders (`libs/audiofile`): WAV/W64/RF64, AIFF, FLAC, MP3. Decoding is synchronous in `readExtraState`, never on the audio thread.
  - The result is an immutable `SampleData`. It holds the channels (padded for the interpolator) and copies decimated to ½, ¼ … of the rate, plus a min/max overview for the display. It reaches the audio thread through `AssetHandoff`.
  - Swapping samples while playing fades the old voices out (1.5 ms). The old data is freed off the audio thread.
  - A missing file keeps its path in the state and is shown on the panel; the device plays silence.
  - When the panel loads a file with a root note in its metadata (`smpl`/`inst`), it sets Root Key through the host. That is a separate, undoable step.
- **Playback:**
  - Windowed-sinc interpolation from `dsp::SincKernels`: Kaiser, 48 taps, cutoff at 0.45 fs, polyphase tables.
  - The voice reads the smallest decimated copy that is at most 2× its rate. The kernel's cutoff is lowered for the remaining speed-up (up to 96 taps).
  - Playback pitch is limited to ±60 st from the sample's own pitch, which bounds one-shot tails.
- **Loop:** the loop's last *crossfade* frames are blended (equal power) with the frames just before the loop start. The crossfade is limited by the material before the loop start and by half the loop length.
- **Voices:**
  - The shared voice manager.
  - A stolen or retriggered voice fades out over 1.5 ms before the new note starts (click-free).
  - Per voice: amp envelope, stereo filter (the instruments' shared `dsp::SynthFilter`) and filter envelope.
  - Glide in all voice modes; legato keeps the playback position.
  - One-shots play to the end, ignoring note-off unless Gate is on. All-notes-off releases them too.
- **Analysis:**
  - slot 0 = output peak;
  - slot 1 = active voices;
  - slots 2–9 = playheads of up to eight voices (0..1 of the sample, −1 = unused).

  Start, end and loop markers come from parameters.

## UI layout

Waveform on the left, then two-row groups (about 1350 px wide):

| Group | Top row | Bottom row |
|---|---|---|
| Waveform | File name and length; waveform (dimmed outside Start–End); loop tint; draggable S, E and L markers; live playheads | |
| Pitch and level | Transpose, Detune | Gain, Velocity → Volume |
| Region | [Classic \| One-shot]; Loop and Gate (the one the mode ignores is dimmed); Start and End bars | Loop Start, X-Fade, Fade In, Fade Out |
| Envelopes | Amp display, A, D, S, R | Filter-envelope display, A, D, S, R |
| Filter | On, type buttons, response display (dimmed when off) | Cutoff, Res, Env, Key |
| Voice | Mode, then Root, Voices, Glide and Bend bars, stacked across both rows | |

- Drop a file anywhere on the panel to load it, or click the empty waveform (or double-click a loaded one) to browse. An empty Sampler shows "Drop a sample, or click to browse".
- Dropping onto a track in the DAW comes with the DAW slice (C4/C7).

## Factory presets

These ship with the CC0 core content (M7). M1 ships defaults only.

## Automated test targets

| Metric | Target |
|---|---|
| Pitch accuracy (sine sample, ±24 st) | ±0.5 cent |
| Aliasing transposed up 24 st (sweep sample) | < −70 dB |
| Loop seam (crossfade on) | Click score < 8 |
| One-shot plays to the end, ignoring note-off | Exact length |
| Sample swap while playing | No allocation or locks in `process()`; no clicks |
| State round trip | Restores the sample path and parameters; same render |
| Conformance suite (instrument checks) | All pass |

## A/B cases (against the Live target)

Deferred until a Live machine is available.

## A/B log

–

## Sign-off

- [ ] Automated: unit, conformance and golden tests; validators
- [ ] Snapshots reviewed (100% / 200%)
- [ ] Sound signed off by the product owner (date):
- [ ] Feel signed off by the product owner (date):
