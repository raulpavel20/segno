# Limiter: device spec

- **Status:** Draft (review requested; IDs can still change)
- **Type:** Audio effect
- **Tier / milestone:** Tier 1, M2
- **Live target:** Limiter. References only ([ADR 0011](../../docs/decisions/0011-calibrated-ambition-and-consolidation.md)).

## Purpose

Guarantees a ceiling and makes a mix loud without audible pumping or distortion. It sits last on the master or on any track that must never clip. True-peak mode keeps inter-sample peaks under the ceiling for streaming delivery.

## Beyond Live

Small: a built-in loudness readout (short-term and integrated LUFS) and true-peak meter, so you can master to a target without another plugin. The full Loudness Meter device follows in M3.

## Stages

| Stage | Scope | Milestone |
|---|---|---|
| Core | Everything below | M2 |

## Parameters

| ID (permanent) | Name | Range | Default | Unit | Curve / skew | Smoothing | Automatable |
|---|---|---|---|---|---|---|---|
| `gain` | Gain | 0 … +24 | 0 | dB | linear | 20 ms | Yes |
| `ceiling` | Ceiling | −24 … 0 | −0.3 | dB (dBTP with True Peak on) | linear | 20 ms | Yes |
| `release` | Release | 1 … 1 000 | 80 | ms | centre 80 | 20 ms multiplicative | Yes |
| `release.auto` | Auto Release | Off / On | On | toggle | – | – | Yes |
| `mode` | Mode | Standard, Maximise | Standard | choice | – | 10 ms crossfade | Yes |
| `truepeak` | True Peak | Off / On | On | toggle | – | – | **No** (changes latency slightly; set before use) |
| `link` | Channels | Stereo, L/R, M/S | Stereo | choice | – | 10 ms crossfade | Yes |
| `lookahead` | Lookahead | 1.5 ms, 3 ms, 6 ms | 3 ms | choice | – | – | **No** (sets latency) |

- **Maximise** links Gain and Ceiling, as mastering limiters do: raising Gain lowers nothing else, and the ceiling stays where it is. Output level follows Gain, limited at the ceiling.
- **Standard:** Gain drives into the limiter; the ceiling is fixed.

State version 1.

## DSP design

- **Lookahead peak limiter:**
  - the gain needed for each sample is held as a running minimum over the lookahead window;
  - it is smoothed by an attack ramp spanning the window, so the gain reaches its value exactly when the peak arrives;
  - then it releases exponentially (auto release blends fast and slow by recent activity).
  - The output never exceeds the ceiling.
- **True peak:** detection runs on a 4× oversampled copy (ITU-R BS.1770 interpolation), so inter-sample peaks count too.
- **Channels:**
  - Stereo: one gain for both channels;
  - L/R: independent;
  - M/S: limit mid and side separately, then decode.
- **Latency:** the lookahead (plus the true-peak interpolator), constant for a given setting. As in the Compressor, the setting is not automatable and hosts see a change as a configuration change (ADR 0020).
- **Loudness:** K-weighted momentary (400 ms), short-term (3 s) and integrated (gated) LUFS of the output, from the shared dynamics core. A reset button clears the integrated value.
- **Analysis:**
  - slots 0–1 = output true peak;
  - slot 2 = gain reduction;
  - slot 3 = short-term LUFS;
  - slot 4 = integrated LUFS;
  - FIFO 0 = gain-reduction history.

## UI layout

```
┌─ ● Limiter ──────────────────────────── [Standard|Maximise] ┐
│ ┌──────────────────────────────┐ ◜◝Gain  ◜◝Ceiling ◜◝Release  │
│ │ gain-reduction history       │ [Auto] [True Peak] [St|LR|MS]│
│ │ (scrolling) with output level│ ▭Lookahead                   │
│ └──────────────────────────────┘ −9.8 LUFS-S  −10.4 LUFS-I  ⟲ │
│                                   −0.3 dBTP                  ║│
└──────────────────────────────────────────────────────────────┘
```

## Factory presets

"Safety (−1 dBTP)", "Streaming master (−1 dBTP)", "Loud master", "Transparent", "Drum bus peaks", "M/S master".

## Automated test targets

| Metric | Target |
|---|---|
| Sample peaks of the output, any input up to +24 dB over the ceiling (noise, sines, transients) | Never above the ceiling |
| True peaks with True Peak on (4× oversampled measurement) | ≤ ceiling + 0.1 dB |
| Input below the ceiling, Gain 0 dB | Null against the latency-aligned input (< −120 dBFS) |
| Distortion limiting a 100 Hz sine by 6 dB, release 80 ms | THD < 1 % |
| Reported latency vs measured, per setting | Equal |
| LUFS readout on a 997 Hz sine at −23 dBFS (stereo) | −23.0 ± 0.1 LUFS |
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
