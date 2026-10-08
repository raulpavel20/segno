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

**Measured envelope** (ADAA + FIR oversampling; aliasing in the audible band at 48 kHz). Each doubling of the oversampling factor buys about 20 dB:
- Rectify is the hardest curve, then Hard.
- At 4×, +12 dB drive: 1 kHz −87 to −153 dB; 2.5 kHz −76 to −152 dB; 5 kHz −67 to −152 dB.
- Half-band filters let near-Nyquist content fold into 21.6–24 kHz. It is inaudible and not counted.

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

Freq and Gain are dimmed while Color is off: they only act when it is on. Color works through the saturation (boost before it, the same cut after), so it is heard as drive pushes the signal into the curve, not on a clean signal.

## Factory presets

"Gentle warmth", "Tape glue", "Bass grit", "Hard clip", "Fold FX", "Octave rectify", "Mid push (Color)".

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
