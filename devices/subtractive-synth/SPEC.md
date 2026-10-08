# Subtractive Synth: device spec

- **Status:** Draft (review requested; IDs can still change)
- **Type:** Instrument
- **Tier / milestone:** Tier 1, M1 (core) → M5 (stage 2)
- **Live target:** Drift (core), Analog (stage 2). References only ([ADR 0011](../../docs/decisions/0011-calibrated-ambition-and-consolidation.md)).

## Purpose

The everyday synth: basses, leads, pads, plucks. It sounds good immediately, is light on the CPU, and has a slightly alive character (per-voice drift). Everything important is on one compact panel.

## Beyond Live

None in the core. MPE is part of the core, as in Drift.

## Stages

| Stage | Scope | Milestone |
|---|---|---|
| Core | Below (Drift-level) | M1 |
| Stage 2 | Dual oscillator→filter→amp paths with routing, second filter, unison (Analog-level), in the expanded view | M5 |

## Parameters

| ID | Name | Range | Default | Notes |
|---|---|---|---|---|
| `osc1.shape` | Osc 1 Shape | 0 … 100 % | 0 | Morphs saw → square → triangle → sine |
| `osc1.oct` | Osc 1 Octave | −3 … +3 | 0 | |
| `osc1.pw` | Osc 1 Width | 5 … 95 % | 50 | Affects the square region |
| `osc2.wave` | Osc 2 Wave | Saw, Square, Triangle, Sine | Saw | |
| `osc2.oct` | Osc 2 Octave | −3 … +3 | 0 | |
| `osc2.semi` | Osc 2 Semi | −12 … +12 st | 0 | |
| `osc2.detune` | Osc 2 Detune | −50 … +50 ct | 7 | |
| `mix.osc1` | Osc 1 Level | −inf … 0 dB | 0 | |
| `mix.osc2` | Osc 2 Level | −inf … 0 dB | −6 | |
| `mix.noise` | Noise | −inf … 0 dB | −inf | |
| `filter.type` | Filter Type | LP 12, LP 24, HP 12, BP 12 | LP 24 | LP 12/HP/BP: TPT state-variable; LP 24: ZDF ladder |
| `filter.cutoff` | Cutoff | 20 … 20 000 Hz | 2 000 | log |
| `filter.res` | Resonance | 0 … 100 % | 20 | Self-oscillates near 100 % (LP 24) |
| `filter.key` | Key Track | 0 … 100 % | 50 | |
| `filter.env` | Env Amount | −100 … +100 % | 30 | Mod envelope to cutoff (±5 octaves at full) |
| `amp.a` / `amp.d` / `amp.s` / `amp.r` | Amp A/D/S/R | 0.5 ms … 20 s / level 0–100 % | 2 ms, 300 ms, 80 %, 200 ms | Exponential segments |
| `mod.a` / `mod.d` / `mod.s` / `mod.r` | Mod A/D/S/R | same | 2 ms, 400 ms, 0 %, 300 ms | |
| `mod.cycle` | Mod Cycle | Off / On | Off | Loops A→D as an extra LFO |
| `lfo.wave` | LFO Wave | Sine, Triangle, Saw, Square, S&H | Sine | |
| `lfo.rate` | LFO Rate | 0.01 … 40 Hz | 2 | |
| `lfo.sync` | LFO Sync | Off, 1/32 … 4 bars | Off | Tempo sync when not Off |
| `m<n>.src` (n = 1–3) | Mod n Source | Mod Env, LFO, Velocity, Mod Wheel, Pressure, Key, Slide | – | |
| `m<n>.dst` | Mod n Target | Pitch, Cutoff, Resonance, Osc 1 Shape, Osc 1 Width, Osc 2 Level, Amp, Pan, LFO Rate | – | |
| `m<n>.amt` | Mod n Amount | −100 … +100 % | 0 | |
| `voice.mode` | Voice Mode | Poly, Mono, Legato | Poly | |
| `voice.count` | Voices | 1 … 16 | 8 | |
| `voice.glide` | Glide | 0 … 2 s | 0 | |
| `voice.drift` | Drift | 0 … 100 % | 15 | Per-voice pitch and cutoff wander |
| `voice.bend` | Bend Range | 0 … 24 st | 2 | Per-note bend for MPE |
| `out.volume` | Volume | −inf … +6 dB | −6 | |

State version 1. The defaults should sound good immediately, as a warm detuned saw with a gentle filter envelope.

## DSP design

- **Oscillators:** band-limited by high-order BLEP/BLAMP corrections (elliptic BLEP or equivalent). The shape morph crossfades band-limited waveforms. Target: sawtooth aliasing below −70 dB at C7 (48 kHz).
- **Filters:**
  - TPT state-variable filter (12 dB modes);
  - ZDF 4-pole ladder with gentle saturation (LP 24).

  Cutoff is modulated per sample from smoothed sources.
- **Envelopes:** exponential ADSR, retriggered or legato by voice mode; cycle mode for the mod envelope.
- **Voices:** the voice manager (M1 step A3): stealing (oldest released first, then oldest), sustain pedal, per-voice MPE (pitch bend, pressure, slide), glide.
- **Control-rate updates** run on the 32-sample grid; audio-rate paths are per sample.
- **Analysis slot:** 0 = output peak (the UI also shows active-voice count, via slot 1).

## UI layout

```
┌─ ● Subtractive Synth ─────────────────────────────────────────────────────────────────┐
│ OSC 1 [shape view]  OSC 2 [wave]   MIX        FILTER [curve]     AMP ENV [view]  MOD  │
│ ◜◝Shape ◜◝Oct ◜◝PW  ◜◝Oct ◜◝Semi ◜◝Det  ◜◝1 ◜◝2 ◜◝N  ◜◝Cut ◜◝Res ◜◝Env  A D S R  1..3 │
│                                                    [LP12|LP24|HP|BP]   MOD ENV  LFO    │
└───────────────────────────────────────────────────────────────────────────────────────┘
```

Mini displays (oscillator shape, filter response, envelope shapes) come from the same pure functions as the DSP. Envelope displays can be dragged directly.

## Factory presets

"Warm saw", "Sub bass", "Acid line", "Soft pad", "Pluck", "PWM strings", "Noise sweep", "Lead with glide".

## Automated test targets

| Metric | Target |
|---|---|
| Saw aliasing at C7 (2 093 Hz), 48 kHz | < −70 dB |
| Tuning (A4 = 440 Hz, drift 0) | ±0.1 cent |
| Envelope timing (attack 10 ms, release 100 ms) | ±5 % (10–90 %, −60 dB) |
| Filter self-oscillation (LP 24, res 100 %, key track 100 %) | Tracks the note pitch within ±5 cents over 3 octaves |
| Voice stealing at maximum polyphony | Click score < 8 |
| All notes released | Silence (< −120 dBFS) after release + 50 ms |
| CPU | Measured per voice (recorded, trend tracked) |
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
