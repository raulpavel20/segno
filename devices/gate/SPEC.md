# Gate: device spec

- **Status:** Draft (review requested; IDs can still change)
- **Type:** Audio effect
- **Tier / milestone:** Tier 1, M2
- **Live target:** Gate. References only ([ADR 0011](../../docs/decisions/0011-calibrated-ambition-and-consolidation.md)).

## Purpose

Removes what sits below a threshold (noise, bleed, room tails), or turns it down gently as an expander. Flip passes only the quiet part. With a sidechain it gates one track by another (trance gates, tightening bass to the kick).

## Beyond Live

None.

## Stages

| Stage | Scope | Milestone |
|---|---|---|
| Core | Everything below | M2 |

## Parameters

| ID (permanent) | Name | Range | Default | Unit | Curve / skew | Smoothing | Automatable |
|---|---|---|---|---|---|---|---|
| `threshold` | Threshold | −80 … 0 | −40 | dB | linear | 20 ms | Yes |
| `return` | Return | 0 … 24 | 3 | dB | linear | 20 ms | Yes |
| `mode` | Mode | Gate, Expander | Gate | choice | – | 10 ms crossfade | Yes |
| `ratio` | Ratio | 1 … 10 | 2 | :1 (Expander) | centre 2 | 20 ms | Yes |
| `range` | Range | −inf … 0 | −inf | dB | dB | 20 ms | Yes |
| `attack` | Attack | 0.02 … 150 | 0.1 | ms | centre 1 | 20 ms multiplicative | Yes |
| `hold` | Hold | 0 … 1 000 | 20 | ms | centre 50 | 20 ms | Yes |
| `release` | Release | 1 … 3 000 | 100 | ms | centre 100 | 20 ms multiplicative | Yes |
| `flip` | Flip | Off / On | Off | toggle | – | 10 ms crossfade | Yes |
| `lookahead` | Lookahead | Off, 1 ms, 10 ms | Off | choice | – | – | **No** (sets latency, as in the Compressor) |
| `sc.filter` | Sidechain Filter | Off, High Pass, Low Pass, Band Pass, Bell | Off | choice | – | 10 ms crossfade | Yes |
| `sc.freq` | Sidechain Freq | 20 … 20 000 | 100 | Hz | centre 632 Hz | 20 ms multiplicative | Yes |
| `sc.q` | Sidechain Q | 0.1 … 10 | 0.71 | – | centre 1 | 20 ms multiplicative | Yes |
| `sc.gain` | Sidechain Gain | −24 … +24 | 0 | dB (Bell only) | linear | 20 ms | Yes |
| `sc.listen` | Listen | Off / On | Off | toggle | – | 10 ms crossfade | Yes |

The sidechain source is host routing (M2 step A2). State version 1.

## DSP design

- **The shared dynamics core** (M2 step A1): detector (peak, stereo-linked) on the sidechain or the input, through the optional sidechain filter.
- **Gate:** opens when the level rises above Threshold and closes when it falls below Threshold − Return (hysteresis, so it doesn't chatter). Hold keeps it open after the level drops. The gain moves between 0 dB and Range with the attack and release times.
- **Expander:** downward expansion below the threshold at Ratio (the gain computer's expansion curve), limited by Range, with the same attack, hold and release.
- **Flip** inverts the gain: the output is the input minus the gated signal, so only what the gate removes is heard.
- **Lookahead:** opens before a transient arrives (no clicks on drums). Latency follows the setting, as in the Compressor (ADR 0020, to be written).
- **No zipper or click:** gain changes are smoothed per sample in dB; Range −inf closes to silence.
- **Analysis:**
  - slot 0 = detector level;
  - slot 1 = gain (dB);
  - slot 2 = open (1) or closed (0);
  - FIFO 0 = level history for the display.

## UI layout

```
┌─ ● Gate ──────────────────────────────────── [Gate|Expander] ┐
│ ┌──────────────────────────────┐ ◜◝Thresh ◜◝Return ◜◝Range    │
│ │ scrolling level history with │ ◜◝Attack ◜◝Hold ◜◝Release     │
│ │ threshold and return lines,  │ ◜◝Ratio [Flip] ▭Lookahead     │
│ │ open/closed shading          │ SC: [filter ▾] Freq Q [Listen]│
│ └──────────────────────────────┘                                │
└────────────────────────────────────────────────────────────────┘
```

- The history shows the detector level, the threshold and return lines (drag either to change it), and shading where the gate is open.
- Ratio is dimmed in Gate mode.

## Factory presets

"Drum bleed", "Vocal noise", "Tight kick", "Snare room cut", "Gentle expander", "Trance gate (sidechain)", "Flip: room only".

## Automated test targets

| Metric | Target |
|---|---|
| Opens and closes at Threshold and Threshold − Return (slow level ramps) | Within 0.1 dB |
| Attack, hold and release times | Within ±5 % |
| Range −inf, closed | Silence (< −120 dBFS) |
| Range −20 dB, closed | Output 20 dB down (±0.05 dB) |
| Flip: gate output + flipped output | Equals the input (< −120 dBFS residual) |
| Steady level just around the threshold (±1 dB noise) | No chatter: at most one open/close per hysteresis crossing |
| Reported latency vs measured, per Lookahead setting | Equal |
| Conformance suite | All pass |

## A/B cases (against the Live target)

Deferred until a Live machine is available.

## A/B log

–

## Sign-off

- [ ] Automated: unit, conformance and golden tests; validators
- [ ] Snapshots reviewed (100% / 200%)
- [ ] Sound signed off by the product owner (date):
- [ ] Feel signed off by the product owner (date):
