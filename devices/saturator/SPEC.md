# Saturator: device spec

- **Status:** Draft (review requested; IDs can still change)
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

| ID (permanent) | Name | Range | Default | Unit | Curve / skew | Smoothing | Automatable |
|---|---|---|---|---|---|---|---|
| `drive` | Drive | 0 … +36 | 0 | dB | linear | 20 ms, linear in amplitude | Yes |
| `curve` | Curve | Soft, Warm, Tape, Hard, Fold, Rectify | Soft | choice | – | crossfade 10 ms on change | Yes |
| `bias` | Bias | −1 … +1 | 0 | – | linear | 20 ms | Yes |
| `color` | Color | Off / On | Off | toggle | – | 10 ms crossfade | Yes |
| `color.freq` | Color Freq | 100 … 10 000 | 1 000 | Hz | centre 1 kHz | 20 ms multiplicative | Yes |
| `color.gain` | Color Gain | −12 … +12 | 0 | dB | linear | 20 ms | Yes |
| `output` | Output | −36 … +12 | 0 | dB | linear | 20 ms, linear in amplitude | Yes |
| `mix` | Dry/Wet | 0 … 100 | 100 | % | linear | 20 ms | Yes |
| `quality` | Quality | 2×, 4×, 8× | 4× | choice (oversampling) | – | – (latency stays constant) | No |

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

## UI layout

```
┌─ ● Saturator ──────────────────────────────────────────┐
│ ┌──────────────┐  ◜◝ Drive   ◜◝ Bias   [Soft|Warm|Tape]  ▮▮ in │
│ │ transfer     │  ◟◞ 12 dB   ◟◞ 0.0    [Hard|Fold|Rect]  ▮▮ out│
│ │ curve        │  ◜◝ Output  ◜◝ Mix    [Color] ◜◝Freq ◜◝Gain  │
│ └──────────────┘  ◟◞ 0 dB    ◟◞ 100%           Quality 4× ▾  │
└────────────────────────────────────────────────────────┘
```

The transfer-curve display draws the active shaper at the current drive and bias, computed by the same pure function the audio path uses. You can drag on it to change drive (vertical) and bias (horizontal).

## Factory presets

"Gentle warmth", "Tape glue", "Bass grit", "Hard clip", "Fold FX", "Octave rectify", "Mid push (Color)".

## Automated test targets

| Metric | Target |
|---|---|
| Drive 0 dB, mix 100 %, Soft | Effectively transparent at −20 dBFS: THD < 0.1 % |
| Aliasing ratio: 5 kHz sine at +24 dB drive, Hard, 4× | < −80 dB (non-harmonic energy relative to the total) |
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
