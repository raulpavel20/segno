# Utility: device spec

- **Status:** Approved (M0 pipeline device)
- **Type:** Audio effect
- **Tier / milestone:** Tier 1, M0
- **Live target:** Utility (gain, balance, width, mono, phase invert, DC filter, mute). References only; nothing is copied ([ADR 0011](../../docs/decisions/0011-calibrated-ambition-and-consolidation.md)).

## Purpose

The everyday gain/stereo/polarity tool. In M0 it is also the "hello world" that proves the whole pipeline: DSP, tests, offline rendering, plugin builds and the native DAW device.

## Beyond Live

None. Bass Mono and channel modes (left/right/swap) are possible later additions.

## Stages

| Stage | Scope | Milestone |
|---|---|---|
| Core | Everything below | M0 |

## Parameters

| ID (permanent) | Name | Description | Range | Default | Unit | Curve / skew | Smoothing | Automatable |
|---|---|---|---|---|---|---|---|---|
| `gain` | Gain | Raises or lowers the level; all the way down is silence. | −70 (= −inf) … +35 | 0 | dB | centre 0 dB | 20 ms, linear in amplitude | Yes |
| `balance` | Balance | Moves the sound towards the left or the right. | −1 … +1 | 0 | Pan (50L…C…50R) | linear | 20 ms linear | Yes |
| `width` | Width | Narrows the stereo image towards mono (0 %) or widens it (above 100 %). | 0 … 400 | 100 | % | centre 100 % | 20 ms linear | Yes |
| `mono` | Mono | Sums both channels to mono. | Off / On | Off | toggle | – | 20 ms (via width) | Yes |
| `invert.l` | Invert L | Flips the polarity of the left channel. | Off / On | Off | toggle | – | 10 ms crossfade through 0 | Yes |
| `invert.r` | Invert R | Flips the polarity of the right channel. | Off / On | Off | toggle | – | 10 ms crossfade through 0 | Yes |
| `dc` | DC Filter | Removes DC offset with a gentle high-pass filter. | Off / On | Off | toggle | – | 10 ms crossfade | Yes |
| `mute` | Mute | Silences the output, with a short fade. | Off / On | Off | toggle | – | 20 ms (via gain) | Yes |

No non-parameter state. State version 1.

## DSP design

```
in L/R → DC filter (5 Hz one-pole HP, crossfaded) → polarity (per channel)
       → width (mid/side; side × width/100, or 0 when Mono)
       → balance (attenuate the opposite side: L × (1 − b) for b > 0, R × (1 + b) for b < 0)
       → gain (dB → linear, × 0 when muted) → out L/R
```

- Parameters are read on control ticks (every 32 samples from the last reset) and every derived factor is smoothed per sample. Output is therefore bit-identical regardless of host block size.
- Every toggle is a smoothed crossfade, so no switch clicks.
- Mono input is treated as L = R. With mono output, the result is (L + R) / 2.
- Latency 0, tail 0.
- Analysis slots: 0 = output peak L, 1 = output peak R.

## UI layout

On the module grid ([device-panels.md](../../docs/design/device-panels.md)): sections left to right, each `id` TITLE [columns], with controls at (column, row) and spans; knobs are 1 × 3 cells. About 240 px wide.

| Section | Controls |
|---|---|
| `level` LEVEL [1] | Gain (0, 0) · Balance (0, 3) |
| `stereo` STEREO [1] | Width (0, 0) · Mono toggle (0, 3) |
| `channels` CHANNELS [1] | Ø L (0, 0), Ø R (0, 1), DC (0, 2) toggles · Mute toggle (0, 3) |
| meter strip | Output (stereo) |

## Factory presets

None in M0. Possible later: "Mono check", "Invert polarity", "−6 dB pad", "Wide".

## Automated test targets

| Metric | Target |
|---|---|
| Gain −6.0206 dB on a sine | Output level is 0.5× (±1e-5) |
| Width 0 % or Mono | L == R exactly |
| Invert L | L output == −L input (after the crossfade) |
| Balance 50R | L silent, R untouched |
| DC filter on a 0.5 DC input | Residual < 1e-3 after 1 s |
| Mute | Silence after 20 ms |
| Conformance suite | All checks pass |

## A/B cases (against the Live target)

Deferred until a machine with Live is available ([dev-environment.md](../../docs/dev-environment.md)).

## A/B log

–

## Sign-off

- [ ] Automated: unit, conformance and golden tests; validators
- [ ] Snapshots reviewed (100% / 200%)
- [ ] Sound signed off by the product owner (date):
- [ ] Feel signed off by the product owner (date):
