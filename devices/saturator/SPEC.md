# Saturator: device spec

- **Status:** Approved by the product owner (2026-10-10, with the M1 sign-off); IDs are permanent
- **Type:** Audio effect
- **Tier / milestone:** Tier 1, M1
- **Live target:** Saturator. References only; nothing is copied ([ADR 0011](../../docs/decisions/0011-calibrated-ambition-and-consolidation.md)).

## Purpose

Quick, light saturation for warmth, density and grit. Pick a curve and push the drive; Color shapes which frequencies saturate most. Clean at low drive, with no audible aliasing at high drive.

## Beyond Live

None. The aim is to match Live's Saturator in feel and quality. Anti-aliasing (ADAA plus oversampling) is a quality requirement, not a feature.

## Stages

| Stage | Scope | Milestone |
|---|---|---|
| Core | Everything below | M1 |

## Parameters

| ID (permanent) | Name | Description | Range | Default | Unit | Curve / skew | Smoothing | Automatable |
|---|---|---|---|---|---|---|---|---|
| `drive` | Drive | How hard the signal is pushed into the curve: more drive, more distortion. | 0 … +36 | 0 | dB | linear | 20 ms, linear in amplitude | Yes |
| `curve` | Curve | The shape of the distortion, from gentle (Soft, Warm, Tape) to harsh (Hard, Fold, Rectify). | Soft, Warm, Tape, Hard, Fold, Rectify | Soft | choice | – | crossfade 10 ms on change | Yes |
| `bias` | Bias | Shifts the signal before the curve, adding even harmonics for an asymmetric, warmer tone. | −1 … +1 | 0 | – | linear | 20 ms | Yes |
| `color` | Color | Shapes which frequencies saturate most: a bell boosts them before the curve and cuts them the same amount after. | Off / On | Off | toggle | – | 10 ms crossfade | Yes |
| `color.freq` | Color Freq | The frequency Color makes saturate more, or less. | 100 … 10 000 | 1 000 | Hz | centre 1 kHz | 20 ms multiplicative | Yes |
| `color.gain` | Color Gain | How much Color boosts before the curve; negative values make that range saturate less. | −12 … +12 | 0 | dB | linear | 20 ms | Yes |
| `output` | Output | The level after saturation, to match the loudness of the dry signal. | −36 … +12 | 0 | dB | linear | 20 ms, linear in amplitude | Yes |
| `mix` | Dry/Wet | Blends the original signal with the saturated one. | 0 … 100 | 100 | % | linear | 20 ms | Yes |
| `quality` | Quality | How much the curve is oversampled: higher stays cleaner on bright sounds and uses more CPU. | 2×, 4×, 8× | 4× | choice (oversampling) | – | – (latency stays constant) | No |

No non-parameter state. State version 1.

## DSP design

```
in ──┬─> drive gain ─> [Color pre: bell +g] ─> upsample ×N ─> shaper(x + bias) ─> downsample ─> DC blocker ─> [Color post: bell −g] ─> output gain ─┐
     └───────────────────────────── dry path (delayed by the device latency) ──────────────────────────────────────────── mix ──────────────────┘
```

- **Shapers** (each with a closed-form antiderivative for first-order ADAA):
  - **Soft:** `tanh x`.
  - **Warm:** asymmetric tanh, with a steeper negative side, for even harmonics.
  - **Tape:** `x / (1 + |x|)`.
  - **Hard:** clip to ±1.
  - **Fold:** `sin(πx/2)`, which folds beyond ±1.
  - **Rectify:** a blend of `|x|` and x, giving an octave-up character.
- **Oversampling:** linear-phase FIR half-band stages (2/4/8×). The **latency is the same for every Quality setting** (lower factors are padded) and is reported to the host; the dry path is delayed by the same amount.
- **Curve changes** crossfade between the old and new shaper over 10 ms.
- Parameters are read on control ticks and smoothed per sample, as in Utility.
- **Analysis slots:** 0/1 = input peak L/R; 2/3 = output peak L/R.

**Measured envelope** (ADAA + FIR oversampling; aliasing in the audible band at 48 kHz). Each doubling of the oversampling factor buys about 20 dB:
- Rectify is the hardest curve, then Hard.
- At 4×, +12 dB drive: 1 kHz −87 to −153 dB; 2.5 kHz −76 to −152 dB; 5 kHz −67 to −152 dB.
- Half-band filters let near-Nyquist content fold into 21.6–24 kHz. It is inaudible and not counted.

## UI layout

On the module grid ([device-panels.md](../../docs/design/device-panels.md)): sections left to right, each `id` TITLE [columns], with controls at (column, row) and spans; knobs are 1 × 3 cells. About 416 px wide.

| Section | Controls |
|---|---|
| `shape` SHAPE [4] | Transfer curve (0, 0, 3 × 4) · Curve buttons (0, 4, 3 × 2: three per row) · Drive (3, 0) · Bias (3, 3) |
| `color` COLOR [1], power = Color | Freq (0, 0) · Gain (0, 3) |
| `output` OUTPUT [1] | Output (0, 0) · Mix (0, 3) |
| meter strip | In and out |
| header | Quality |

The transfer-curve display draws the active shaper at the current drive and bias, computed by the same pure function the audio path uses. You can drag on it to change drive (vertical) and bias (horizontal), which is why Drive and Bias sit beside it in SHAPE.

COLOR's content is dimmed while Color is off: Freq and Gain only act when it is on. Color works through the saturation (boost before it, the same cut after), so it is heard as drive pushes the signal into the curve, not on a clean signal.

## Factory presets

Built and named by Claude ([presets.md](../../docs/design/presets.md) §13; `devices/saturator/presets/`, made with `fd-presets` from recipes); signed off by the product owner by ear (2026-10-11). 15 presets, each checked by the conformance suite: it loads, plays cleanly under 0 dBFS and differs from the rest.

- **Gentle warmth** (Warm, Subtle): A little even-harmonic warmth, hardly louder.
- **Asymmetric tube** (Warm): An uneven curve, like an overdriven valve: rich even harmonics.
- **Tape glue** (Bus, Warm, Subtle): Rounds the peaks of a group like tape does.
- **Warm bus** (Bus, Warm, Subtle): A touch of drive and asymmetry for a group or the mix.
- **Keys warmth** (Keys, Warm): Tape-style drive, a little more in the low mids.
- **Mid push (Color)** (Vocals, Guitar): Saturates the mids around 1.5 kHz more than the rest, so the part comes forward.
- **Vocal edge** (Vocals, Bright): Edge in the upper mids, blended under the clean voice.
- **Bass grit** (Bass, Dirty): Grit in the mids while the low end stays round.
- **Sub harmonics** (Bass): Adds harmonics to the lows, so the bass reads on small speakers.
- **Drum crunch** (Drums, Dirty): Tape crunch, half blended in.
- **Hard clip** (Drums, Heavy): Shaves the peaks off for loudness; push Drive for more.
- **Parallel grit** (Drums, Bus, Dirty): Hard clipping blended low under the dry sound.
- **Lo-fi fuzz** (FX, Dirty, Heavy): Fuzz with a honky mid push.
- **Fold FX** (FX, Dirty, Heavy): Wavefolding: the louder it plays, the more the tone shifts.
- **Octave rectify** (FX): Rectification adds an octave up: for leads, guitars and synths.

## Automated test targets

| Metric | Target |
|---|---|
| Drive 0 dB, mix 100 %, Soft | Effectively transparent at −20 dBFS: THD < 0.1 % |
| Aliasing in the audible band (≤ 20 kHz), every curve: 4×, +12 dB drive, 2.5 kHz | < −75 dB |
| Aliasing in the audible band, every curve, worst case: 8×, +24 dB drive, 5 kHz | < −65 dB |
| Harmonic profile per curve (1 kHz, +12 dB drive) | Golden metrics (h2…h9 levels) ±0.5 dB |
| Bias ≠ 0 | Even harmonics present; output DC < −80 dBFS after 100 ms |
| Mix 0 % | Null against input delayed by the reported latency (< −120 dBFS) |
| Latency | Measured equals reported, for every Quality setting |
| Conformance suite | All checks pass |

## A/B cases (against the Live target)

Deferred until a Live machine is available.

## A/B log

–

## Sign-off

- [ ] Automated: unit, conformance and golden tests; validators
- [ ] Snapshots reviewed (100% / 200%)
- [ ] Sound signed off by the product owner (date):
- [ ] Feel signed off by the product owner (date):
