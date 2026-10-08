# Spectrum: device spec

- **Status:** Draft (review requested; IDs can still change)
- **Type:** Audio effect (analysis only; audio passes unchanged)
- **Tier / milestone:** Tier 1, M2
- **Live target:** Spectrum. References only ([ADR 0011](../../docs/decisions/0011-calibrated-ambition-and-consolidation.md)).

## Purpose

Shows a track's frequency content while you mix: a smooth, readable spectrum with peak hold, for stereo, either channel, or mid and side. It leaves the audio untouched and uses the same analyser as the EQ, so the two look and read alike.

## Beyond Live

None.

## Stages

| Stage | Scope | Milestone |
|---|---|---|
| Core | Everything below | M2 |

## Parameters

All are display settings: they never affect audio, and they are not automatable.

| ID (permanent) | Name | Range | Default | Unit | Automatable |
|---|---|---|---|---|---|
| `channel` | Channel | L+R, Left, Right, Mid, Side | L+R | choice | No |
| `resolution` | Resolution | 2 048, 4 096, 8 192, 16 384 | 8 192 | FFT points | No |
| `average` | Averaging | 0 … 5 000 | 300 | ms | No |
| `hold` | Peak Hold | Off, 1 s, 3 s, Infinite | 3 s | choice | No |
| `slope` | Slope | 0 … 6 | 3 | dB/oct (tilt around 1 kHz) | No |
| `range` | Range | 60, 90, 120 | 90 | dB shown | No |

State version 1.

## DSP design

- **Audio:** passes unchanged (a null test against the input, zero latency).
- **Analysis tap:** the audio thread pushes the chosen channel combination into a FIFO (`SampleFifo`, M1 step A4); nothing else happens on the audio thread.
- **In the UI:**
  - a windowed FFT (Hann, 75 % overlap) at the chosen resolution;
  - power smoothed over time with the averaging constant;
  - display smoothing across frequency (1/12 octave);
  - peak hold.
- **Shared analyser:** the EQ's analyser moves into `fd_device_ui` as a shared component, so the EQ and the Spectrum look the same.

## UI layout

```
┌─ ● Spectrum ───────────────────────────────────────────────────┐
│ ┌──────────────────────────────────────────────────────────────┐│
│ │ spectrum (flat fill + line), peak-hold line, frequency and dB││
│ │ grid; hover shows the frequency, note name and level          ││
│ └──────────────────────────────────────────────────────────────┘│
│ [L+R|L|R|M|S]  Res ▾  Avg ◜◝  Hold ▾  Slope ◜◝  Range ▾          │
└────────────────────────────────────────────────────────────────┘
```

The display is the device. The panel is wide, and controls sit in one row under it.

## Factory presets

"Mixing (3 dB/oct)", "Flat", "Mid/side check", "Fast", "Slow and smooth".

## Automated test targets

| Metric | Target |
|---|---|
| Audio through the device | Null against the input (< −120 dBFS), zero latency |
| A 1 kHz sine at −12 dBFS, slope 0 | Peak at 1 kHz (±1 bin), −12 dB (±0.5 dB) |
| Pink noise with slope 3 dB/oct | Flat display (±1.5 dB, 50 Hz–15 kHz, after averaging) |
| Mid and Side on L = R input | Side shows nothing (below the range floor) |
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
