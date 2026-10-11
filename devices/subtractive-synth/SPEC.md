# Subtractive Synth: device spec

- **Status:** Approved by the product owner (2026-10-10, with the M1 sign-off); IDs are permanent
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

| ID | Name | Description | Range | Default | Notes |
|---|---|---|---|---|---|
| `osc1.shape` | Osc 1 Shape | Morphs the first oscillator from saw through square and triangle to sine. | 0 … 100 % | 0 | Morphs saw → square → triangle → sine |
| `osc1.oct` | Osc 1 Octave | Shifts the first oscillator by whole octaves. | −3 … +3 | 0 | |
| `osc1.pw` | Osc 1 Width | The pulse width of the first oscillator's square: 50 % sounds hollow, narrower sounds thinner. | 5 … 95 % | 50 | Affects the square region |
| `osc2.wave` | Osc 2 Wave | The second oscillator's waveform. | Saw, Square, Triangle, Sine | Saw | |
| `osc2.oct` | Osc 2 Octave | Shifts the second oscillator by whole octaves. | −3 … +3 | 0 | |
| `osc2.semi` | Osc 2 Semi | Shifts the second oscillator by semitones, for intervals. | −12 … +12 st | 0 | |
| `osc2.detune` | Osc 2 Detune | Detunes the second oscillator slightly, for a thicker, beating sound. | −50 … +50 ct | 7 | |
| `mix.osc1` | Osc 1 Level | The first oscillator's level. | −inf … 0 dB | 0 | |
| `mix.osc2` | Osc 2 Level | The second oscillator's level. | −inf … 0 dB | −6 | |
| `mix.noise` | Noise | The level of noise mixed in. | −inf … 0 dB | −inf | |
| `filter.type` | Filter Type | The filter: low pass at 12 or 24 dB per octave, high pass or band pass. | LP 12, LP 24, HP 12, BP 12 | LP 24 | LP 12/HP/BP: TPT state-variable; LP 24: ZDF ladder |
| `filter.cutoff` | Cutoff | Where the filter starts to cut: lower is darker. | 20 … 20 000 Hz | 2 000 | log |
| `filter.res` | Resonance | Emphasis at the cutoff; near the top the 24 dB low pass starts to whistle on its own. | 0 … 100 % | 20 | Self-oscillates near 100 % (LP 24) |
| `filter.key` | Key Track | How much the cutoff follows the note played: at 100 % it rises an octave per octave. | 0 … 100 % | 50 | |
| `filter.env` | Env Amount | How far the modulation envelope moves the cutoff, up or down. | −100 … +100 % | 30 | Mod envelope to cutoff (±5 octaves at full) |
| `amp.a` | Amp Attack | How long a note takes to reach full volume. | 0.5 ms … 20 s | 2 ms | Exponential segments |
| `amp.d` | Amp Decay | How long the volume takes to fall to the sustain level. | 0.5 ms … 20 s | 300 ms | |
| `amp.s` | Amp Sustain | The volume held while the key stays down. | 0 … 100 % | 80 % | |
| `amp.r` | Amp Release | How long a note takes to fade out after the key is released. | 0.5 ms … 20 s | 200 ms | |
| `mod.a` | Mod Attack | How long the modulation envelope takes to rise. | 0.5 ms … 20 s | 2 ms | Exponential segments |
| `mod.d` | Mod Decay | How long the modulation envelope takes to fall to its sustain level. | 0.5 ms … 20 s | 400 ms | |
| `mod.s` | Mod Sustain | The modulation envelope's level while the key stays down. | 0 … 100 % | 0 % | |
| `mod.r` | Mod Release | How long the modulation envelope takes to fall after the key is released. | 0.5 ms … 20 s | 300 ms | |
| `mod.cycle` | Mod Cycle | Loops the modulation envelope's attack and decay, so it works as a second LFO. | Off / On | Off | Loops A→D as an extra LFO |
| `lfo.wave` | LFO Wave | The LFO's shape. | Sine, Triangle, Saw, Square, S&H | Sine | |
| `lfo.rate` | LFO Rate | How fast the LFO moves when it isn't synced to the tempo. | 0.01 … 40 Hz | 2 | |
| `lfo.sync` | LFO Sync | Locks the LFO to the song's tempo, in note values. | Off, 1/32 … 4 bars | Off | Tempo sync when not Off |
| `m<n>.src` (n = 1–3) | Mod n Source | What drives this modulation slot: an envelope, the LFO, or how the note is played. | Mod Env, LFO, Velocity, Mod Wheel, Pressure, Key, Slide | – | |
| `m<n>.dst` | Mod n Target | What this modulation slot changes. | Pitch, Cutoff, Resonance, Osc 1 Shape, Osc 1 Width, Osc 2 Level, Amp, Pan | – | At 100 %: pitch ±12 st, cutoff ±5 oct, shape/res ±full range, width ±45 %, levels/amp ×0–2, pan ±full. (LFO Rate was dropped: the LFO is global, so per-voice sources can't drive it.) |
| `m<n>.amt` | Mod n Amount | How much, and in which direction, this slot modulates its target. | −100 … +100 % | 0 | |
| `voice.mode` | Voice Mode | Poly plays chords; Mono plays one note at a time; Legato is mono without restarting the envelopes when notes overlap. | Poly, Mono, Legato | Poly | |
| `voice.count` | Voices | How many notes can sound at once. | 1 … 16 | 8 | |
| `voice.glide` | Glide | How long the pitch takes to slide from one note to the next. | 0 … 2 s | 0 | |
| `voice.drift` | Drift | Small random wandering of each voice's pitch and filter, like an analog synth. | 0 … 100 % | 15 | Per-voice pitch and cutoff wander |
| `voice.bend` | Bend Range | How far the pitch bend wheel bends, in semitones. | 0 … 24 st | 2 | Per-note bend for MPE |
| `out.volume` | Volume | The synth's output level. | −inf … +6 dB | −6 | |

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

On the module grid ([device-panels.md](../../docs/design/device-panels.md)): sections left to right, each `id` TITLE [columns], with controls at (column, row) and spans; knobs are 1 × 3 cells. About 1550 px wide; sections collapse.

| Section | Controls |
|---|---|
| `osc1` OSC 1 [4] | Wave display (0, 0, 2 × 3) · Shape (2, 0) · Width (3, 0) · Oct (0, 3) · Level (1, 3) · Noise (2, 3) |
| `osc2` OSC 2 [4] | Wave display (0, 0, 2 × 2) · Wave (0, 2, 2 wide) · Oct (2, 0) · Semi (3, 0) · Detune (0, 3) · Level (1, 3) |
| `filter` FILTER [4] | Type buttons (0, 0, 4 × 1) · Response display (0, 1, 4 × 2) · Cutoff (0, 3), Res (1, 3), Key (2, 3), Env (3, 3) |
| `env` ENVELOPES [6] | Amp display (0, 0, 2 × 3) · A D S R (2–5, 0) · Mod display with Cycle (0, 3, 2 × 3) · A D S R (2–5, 3) |
| `mod` MODULATION [6] | Slot *n* (rows 2*n*−2 and 2*n*−1): Source (0, 2 wide) and Target (2, 2 wide), then Amount bar (0, 4 wide) · LFO sub-label (4, 0, 2 wide) · Wave (4, 1, 2 wide) · Sync (4, 2, 2 wide) · Rate (4, 3) |
| `voice` VOICE [3] | Mode (0, 0, 2 wide) · Voices bar (0, 1, 2 wide) · Bend bar (0, 2, 2 wide) · Volume (2, 0) · Glide (0, 3) · Drift (1, 3) |
| meter strip | Output |

- Mini displays (oscillator waves, filter response, envelope shapes) are drawn by the same functions the audio path uses (`osc1Mix` + `Oscillator::naiveAt`, `dsp::SynthFilter::magnitude`, the `Adsr` itself).
- Envelope segment widths follow the log of their times and fill the display.
- Mod amounts, voice count and bend range are value bars.
- The Cycle toggle is the Mod envelope display's accessory.
- To do: drag envelope segments directly on the displays.

## Factory presets

Built and named by Claude ([presets.md](../../docs/design/presets.md) §13; `devices/subtractive-synth/presets/`, made with `fd-presets` from recipes); signed off by the product owner by ear (2026-10-11). 35 presets, each checked by the conformance suite: it loads, plays cleanly under 0 dBFS and differs from the rest.

- **Sub bass** (Bass, Clean): A pure sine sub, one note at a time; play it low.
- **Warm bass** (Bass, Warm): Saw and a square an octave down, a soft filter bite on each note.
- **Acid line** (Bass, Lead): A resonant saw line; overlapping notes slide, and harder notes open the filter more.
- **Pluck bass** (Bass, Pluck): A short, punchy bass that decays to nothing.
- **Reese bass** (Bass, Dirty): Two detuned saws beating against each other; the mod wheel opens it.
- **Square bass** (Bass): A hollow square with a sine an octave below.
- **Wobble bass** (Bass, FX, Heavy): The LFO sweeps the filter in eighth notes, in time with the song.
- **Rubber bass** (Bass): A round, bouncy triangle with a short filter blip and a little slide.
- **Warm saw lead** (Lead, Warm): Two detuned saws with a gentle vibrato; overlapping notes glide.
- **Lead with glide** (Lead): A narrow square with a saw an octave up, gliding between overlapping notes.
- **Bright lead** (Lead, Bright): Saws a fifth apart, open and cutting.
- **Soft sine lead** (Lead, Clean): A soft sine with a hint of the octave, slow to start, with vibrato.
- **Square lead** (Lead): Two squares; the mod wheel narrows the first one's pulse.
- **Screamer** (Lead, Heavy): Resonant and loud, with a little noise; harder notes scream more.
- **Soft pad** (Pad, Warm): A triangle and a quiet saw, slow to swell, drifting gently across the stereo field.
- **PWM strings** (Pad): A square whose pulse width moves slowly, like a string section.
- **Dark pad** (Pad, Warm): Low, slow and dark, with the filter breathing.
- **Glass pad** (Pad, Bright): Sine and triangle a twelfth apart, airy, with a shimmer.
- **Evolving pad** (Pad, FX): The looping mod envelope and a slow LFO keep the tone moving.
- **Noise sweep** (Pad, FX): Filtered noise rising slowly through a band pass.
- **Electric keys** (Keys, Warm): A soft, bell-like electric piano; harder notes are brighter.
- **Organ** (Keys): Two sines an octave apart, on and off like an organ's keys, with a little vibrato.
- **Funky clav** (Keys, Pluck): A thin pulse through a band pass, quick and percussive.
- **Soft keys** (Keys, Warm): A mellow triangle with a sine above, decaying slowly.
- **Pluck** (Pluck): A bright attack that closes fast, for arpeggios and chords.
- **Soft pluck** (Pluck, Warm): A gentle triangle pluck with a short filter blip.
- **Bell** (Pluck, Bright): Two sines at an inharmonic ratio (3.5), ringing out like a bell.
- **Chord stab** (Pluck): Saws a fifth apart with a quick filter snap: a stab from a single key.
- **Synth brass** (Brass): The filter opens a little after each note starts, like a horn; harder notes are brighter.
- **Soft horns** (Brass, Warm): Slower and darker brass for held chords.
- **Synth kick** (Drums): A sine falling two octaves in a few milliseconds; play it around C1.
- **Synth snare** (Drums): A pitched body and a burst of noise through a band pass.
- **Noise hat** (Drums, Bright): High-passed noise, very short.
- **Laser zap** (FX): A saw falling two octaves fast, through a resonant filter.
- **Riser** (FX): Noise and a saw rising over eight seconds while held: build-ups.

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
