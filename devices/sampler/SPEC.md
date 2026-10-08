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
- `sample.relative`: path relative to the project, if available;
- the original sample rate and length, for checking.

Embedding the audio in the project or preset comes later. State version 1.

## DSP design

- **Loading:**
  - Files are decoded off the audio thread: WAV, AIFF, FLAC and MP3 (JUCE-free decoders).
  - The result is an immutable `SampleData` (channels, rate, plus pre-decimated copies at ½, ¼ … rate for clean upward transposition), handed to the audio thread lock-free (step A5).
  - Swapping samples while playing fades the old voices out.
- **Playback:** windowed-sinc interpolation, choosing the decimated copy that keeps the playback ratio ≤ 2 for anti-aliasing. Loop crossfade uses equal-power curves.
- **Voices:** the shared voice manager; per-voice amp envelope, filter and filter envelope; glide in mono and legato.
- **Analysis:** voice playheads go through a FIFO tap (start, end and loop markers come from parameters); 0 = output peak.

## UI layout

```
┌─ ● Sampler ─────────────────────────────────────── kick.wav ── [Classic|One-shot] ┐
│ ┌─────────────────────────────────────────────────────┐  ◜◝Trans ◜◝Det  ◜◝Gain    │
│ │ waveform (flat, clip-colour tint) with start/end/loop │  A D S R env view         │
│ │ markers you can drag, and live voice playheads        │  [Filter] ◜◝Cut ◜◝Res     │
│ └─────────────────────────────────────────────────────┘  [Loop] ◜◝LStart ◜◝XFade  │
└───────────────────────────────────────────────────────────────────────────────────┘
```

Drop a file onto the panel (or onto a track in the DAW) to load it. An empty Sampler shows "Drop a sample".

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
