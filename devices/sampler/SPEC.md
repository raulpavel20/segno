# Sampler: device spec

- **Status:** Approved by the product owner (2026-10-10, with the M1 sign-off); IDs are permanent
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

| ID | Name | Description | Range | Default | Notes |
|---|---|---|---|---|---|
| `mode` | Mode | Classic plays while the key is held, with optional looping; One-shot plays to the end, as for drums. | Classic, One-shot | Classic | |
| `start` | Start | Where in the sample playback starts. | 0 … 100 % | 0 | Of the sample length |
| `end` | End | Where in the sample playback ends. | 0 … 100 % | 100 | |
| `loop` | Loop | Loops between Loop Start and End while the key is held (Classic only). | Off / On | Off | Classic only |
| `loop.start` | Loop Start | Where the loop starts, within the start-to-end region. | 0 … 100 % | 0 | Relative to the start–end region |
| `loop.xfade` | Loop Crossfade | Crossfades the end of the loop into its start, to hide the seam. | 0 … 50 % | 2 | Of the loop length |
| `root` | Root Key | The note at which the sample plays at its original pitch. | 0 … 127 | 60 | Read from file metadata when present |
| `transpose` | Transpose | Shifts the pitch in semitones. | −48 … +48 st | 0 | |
| `detune` | Detune | Fine-tunes the pitch in cents. | −50 … +50 ct | 0 | |
| `gain` | Gain | The sample's level. | −inf … +12 dB | 0 | |
| `vel.amp` | Velocity → Volume | How much velocity changes the volume: at 0 % every note plays equally loud. | 0 … 100 % | 50 | |
| `amp.a` | Amp Attack | How long a note takes to reach full volume. | 0.5 ms … 20 s | 0.5 ms | |
| `amp.d` | Amp Decay | How long the volume takes to fall to the sustain level. | 0.5 ms … 20 s | 300 ms | |
| `amp.s` | Amp Sustain | The volume held while the key stays down. | 0 … 100 % | 100 % | One-shot ignores it unless `oneshot.gate` is on |
| `amp.r` | Amp Release | How long a note takes to fade out after the key is released. | 0.5 ms … 20 s | 50 ms | One-shot ignores it unless `oneshot.gate` is on |
| `oneshot.gate` | Gate | In One-shot mode, releasing the key fades the sample out instead of letting it play to the end. | Off / On | Off | One-shot: note-off fades out |
| `fade.in` | Fade In | A short fade at the start of each note, against clicks. | 0 … 100 ms | 0.5 ms | Click-free start |
| `fade.out` | Fade Out | A short fade at the end of each note, against clicks. | 0 … 100 ms | 2 ms | Click-free end |
| `filter.on` | Filter | Turns the filter on. | Off / On | Off | Same filter engine as Subtractive Synth |
| `filter.type` | Filter Type | The filter: low pass at 12 or 24 dB per octave, high pass or band pass. | LP 12, LP 24, HP 12, BP 12 | LP 24 | Same filter engine as Subtractive Synth |
| `filter.cutoff` | Cutoff | Where the filter starts to cut: lower is darker. | 20 … 20 000 Hz | 2 000 | log |
| `filter.res` | Resonance | Emphasis at the cutoff; near the top the 24 dB low pass starts to whistle on its own. | 0 … 100 % | 20 | |
| `filter.env` | Filter Env Amount | How far the filter envelope moves the cutoff, up or down. | −100 … +100 % | 0 | Filter envelope to cutoff |
| `filter.key` | Filter Key Track | How much the cutoff follows the note played: at 100 % it rises an octave per octave. | 0 … 100 % | 50 | |
| `fenv.a` | Filter Env Attack | How long the filter envelope takes to rise. | 0.5 ms … 20 s | 2 ms | |
| `fenv.d` | Filter Env Decay | How long the filter envelope takes to fall to its sustain level. | 0.5 ms … 20 s | 400 ms | |
| `fenv.s` | Filter Env Sustain | The filter envelope's level while the key stays down. | 0 … 100 % | 0 % | |
| `fenv.r` | Filter Env Release | How long the filter envelope takes to fall after the key is released. | 0.5 ms … 20 s | 300 ms | |
| `voice.mode` | Voice Mode | Poly plays chords; Mono plays one note at a time; Legato is mono without restarting the envelopes when notes overlap. | Poly, Mono, Legato | Poly | |
| `voice.count` | Voices | How many notes can sound at once. | 1 … 32 | 16 | |
| `voice.glide` | Glide | How long the pitch takes to slide from one note to the next. | 0 … 2 s | 0 | |
| `voice.bend` | Bend Range | How far the pitch bend wheel bends, in semitones. | 0 … 24 st | 2 | |

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

On the module grid ([device-panels.md](../../docs/design/device-panels.md)): sections left to right, each `id` TITLE [columns], with controls at (column, row) and spans; knobs are 1 × 3 cells. About 1480 px wide; sections collapse.

| Section | Controls |
|---|---|
| `sample` SAMPLE [6] | Waveform (0, 0, 6 × 6): file name and length; dimmed outside Start–End; loop tint; draggable S, E and L markers; live playheads |
| `pitch` PITCH [1] | Transpose (0, 0) · Detune (0, 3) |
| `level` LEVEL [1] | Gain (0, 0) · Velocity → Volume (0, 3) |
| `region` REGION [4] | [Classic \| One-shot] (0, 0, 2 cells per button) · Loop (0, 1, 2 wide), Gate (2, 1, 2 wide): the one the mode ignores is dimmed · Start bar (0, 2, 2 wide), End bar (2, 2, 2 wide) · Loop Start (0, 3), X-Fade (1, 3) |
| `fades` FADES [1] | In (0, 0) · Out (0, 3) |
| `env` ENVELOPES [6] | Amp display (0, 0, 2 × 3) · A D S R (2–5, 0) · Filter-envelope display (0, 3, 2 × 3) · A D S R (2–5, 3) |
| `filter` FILTER [4], power = Filter | Type buttons (0, 0, 4 × 1) · Response display (0, 1, 4 × 2) · Cutoff (0, 3), Res (1, 3), Env (2, 3), Key (3, 3) |
| `voice` VOICE [2] | Mode (0, 0) · Root bar (0, 1) · Voices bar (0, 2) · Glide bar (0, 3) · Bend bar (0, 4), all 2 wide |
| meter strip | Output |

- Drop a file anywhere on the panel to load it, or click the empty waveform (or double-click a loaded one) to browse. An empty Sampler shows "Drop a sample, or click to browse".
- Dropping onto a track in the DAW comes with the DAW slice (C4/C7).

## Factory presets

Waiting for sounds: the product owner brings the set they're built on (2026-10-11; [presets.md](../../docs/design/presets.md) §15, P11). Until then the Sampler has only Default settings.

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
