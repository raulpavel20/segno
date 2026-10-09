# Compressor: device spec

- **Status:** Approved by the product owner (2026-10-09); IDs are permanent
- **Type:** Audio effect
- **Tier / milestone:** Tier 1, M2
- **Live target:** Compressor and Glue Compressor (one device, two characters). References only ([ADR 0011](../../docs/decisions/0011-calibrated-ambition-and-consolidation.md)).

## Purpose

Controls dynamics on single tracks (Clean) and holds busses and mixes together (Glue). One device covers both because the job is the same and only the character differs. You see what it does: the transfer curve you can grab, and a scrolling history of level and gain reduction.

## Beyond Live

Small: the gain-reduction history scrolls continuously beside the transfer curve (Live shows one or the other).

## Stages

| Stage | Scope | Milestone |
|---|---|---|
| Core | Everything below | M2 |

## Parameters

| ID (permanent) | Name | Description | Range | Default | Unit | Curve / skew | Smoothing | Automatable |
|---|---|---|---|---|---|---|---|---|
| `threshold` | Threshold | The level above which the compressor starts turning the sound down. | −60 … 0 | −18 | dB | linear | 20 ms | Yes |
| `ratio` | Ratio | How strongly it turns down what goes over the threshold: at 4:1, 4 dB over comes out 1 dB over. | 1 … 100 (∞ at max) | 4 | :1 | centre 4 | 20 ms | Yes |
| `knee` | Knee | How gradually compression starts around the threshold: wider is gentler. | 0 … 24 | 6 | dB | linear | 20 ms | Yes |
| `attack` | Attack | How quickly it reacts when the level goes over the threshold. | 0.01 … 300 | 10 | ms | centre 10 | 20 ms multiplicative | Yes |
| `release` | Release | How quickly it lets go once the level falls back. | 1 … 3 000 | 150 | ms | centre 150 | 20 ms multiplicative | Yes |
| `release.auto` | Auto Release | Adapts the release to the material: fast after short peaks, slower under sustained loudness. | Off / On | Off | toggle | – | – | Yes |
| `makeup` | Makeup | Gain after compression, to bring the level back up. | −24 … +36 | 0 | dB | linear | 20 ms | Yes |
| `makeup.auto` | Auto Makeup | Sets the makeup gain from the threshold and ratio, so the level stays about the same. | Off / On | Off | toggle | – | – | Yes |
| `character` | Character | Clean is transparent; Glue reacts more gently, for holding busses and mixes together. | Clean, Glue | Clean | choice | – | 10 ms crossfade | Yes |
| `detector` | Detector | Whether it reacts to peaks or to the average level (RMS). | Peak, RMS | Peak | choice | – | 10 ms crossfade | Yes |
| `link` | Stereo Link | How much the two channels share their gain reduction: 100 % keeps the stereo image steady. | 0 … 100 | 100 | % | linear | 20 ms | Yes |
| `lookahead` | Lookahead | Delays the audio a little so the compressor catches fast peaks in time. | Off, 1 ms, 10 ms | Off | choice | – | – | **No** (sets latency, see below) |
| `sc.filter` | Sidechain Filter | Filters what the compressor listens to, for example so it ignores the bass. | Off, High Pass, Low Pass, Band Pass, Bell | Off | choice | – | 10 ms crossfade | Yes |
| `sc.freq` | Sidechain Freq | The sidechain filter's frequency. | 20 … 20 000 | 100 | Hz | centre 632 Hz | 20 ms multiplicative | Yes |
| `sc.q` | Sidechain Q | How narrow the sidechain filter is. | 0.1 … 10 | 0.71 | – | centre 1 | 20 ms multiplicative | Yes |
| `sc.gain` | Sidechain Gain | How much the sidechain bell boosts or cuts. | −24 … +24 | 0 | dB (Bell only) | linear | 20 ms | Yes |
| `sc.listen` | Listen | Plays what the compressor listens to, to set the sidechain filter by ear. | Off / On | Off | toggle | – | 10 ms crossfade | Yes |
| `mix` | Mix | Blends the uncompressed signal with the compressed one (parallel compression). | 0 … 100 | 100 | % | linear | 20 ms | Yes |
| `output` | Output | The level after the compressor. | −24 … +24 | 0 | dB | linear | 20 ms | Yes |

**Sidechain source** (external sidechain on or off, and which track) is host routing, not a parameter (M2 step A2). With no external sidechain the detector listens to the input.

State version 1.

## DSP design

```
in ──┬─────────────────────────── lookahead delay ──> × gain ──> makeup ──> output ──┬─> out
     │                                                   ▲                            │ mix with
sidechain (or in) ─> SC filter ─> level (peak/RMS, link) ┤                            │ latency-
     │                                    Glue: from out ─┘                            │ aligned dry
     └──── dry ─────────────────────────────────────────────────────────────────────┘
level ─> dB ─> gain computer (threshold, ratio, knee) ─> attack/release (dB domain) ─> gain
```

- **Gain computer:** the standard soft-knee curve (quadratic across the knee), from a pure function the display also draws (`dsp::compressorCurve`). Ratio ∞ is a limiter curve.
- **Smoothing:** a one-pole filter on the gain reduction in dB, with branching attack and release.
  - Auto release blends a fast and a slow release according to how long the reduction has lasted (program-dependent): from half the Release setting after short peaks to five times it once reduction has lasted about 500 ms.
  - Times are 10–90 % for attack and to −60 dB of the reduction for release, like the other envelopes in the suite.
  - Attack and Release glide to a new setting over 20 ms, so a jump to a fast attack never snaps the gain. Switching Auto Release hands the current gain from one smoother to the other.
- **Detector:**
  - Peak: rectified signal through the attack/release;
  - RMS: a 10 ms mean square before the gain computer. It runs in either mode, so switching crossfades the two levels over 10 ms.
  - Stereo link: linked uses the louder channel's level; unlinked works per channel; in between blends the two.
- **Glue character:** feedback detection (the detector reads the output) and a gentle output soft-clip (tanh above −1.9 dBFS, never past full scale). That's the bus-compressor behaviour: slower, smoother, "glued". Switching character crossfades over 10 ms. The Glue presets turn Auto Release on; the character itself doesn't, because a device never sets its own parameters.
- **Auto makeup** compensates the static gain at −18 dBFS input: `−18 − curve(−18)`. Turning it on or off glides between it and the Makeup setting over 20 ms.
- **Sidechain filter:** a biquad (the EQ's cookbook designs), on the detector path only. A change of type crossfades from the outgoing filter over 10 ms; frequency, Q and gain glide over 20 ms. Listen sends the filtered detector signal to the output, crossfading over 10 ms.
- **Lookahead and latency.** The latency follows the Lookahead setting: 0, 1 or 10 ms. That makes it an exception to [ADR 0016](../../docs/decisions/0016-constant-device-latency.md), recorded in [ADR 0020](../../docs/decisions/0020-latency-follows-lookahead.md) (accepted).
  - The setting is not automatable.
  - Hosts see a change as a configuration change: the plugin reports new latency, and the DAW rebuilds the graph.
  - Off means zero latency, which keeps live monitoring through a compressor tight.
  - The delay lines are sized for 10 ms in `prepare()`; a new setting takes effect at the next block, with no allocation.
- **Dry path:** delayed by the same lookahead, so Mix never comb-filters.
- **Analysis:**
  - slots 0–1 = input peak, left and right (linear);
  - slots 2–3 = output peak, left and right (linear);
  - slot 4 = gain reduction (dB as a positive number, peak-held);
  - slot 5 = the level the gain computer sees (linear): the curve's moving dot;
  - FIFO 0 = history frames of (input dB, output dB, gain reduction dB), one every 10 ms; a frame is written whole or not at all.

## UI layout

On the module grid ([device-panels.md](../../docs/design/device-panels.md)): sections left to right, each `id` TITLE [columns], with controls at (column, row) and spans; knobs are 1 × 3 cells. As built (B1).

| Section | Controls |
|---|---|
| `curve` CURVE [3] | Transfer curve (0, 0, 3 × 6), with the detector's level as a moving dot |
| `history` HISTORY [4] | Scrolling history (0, 0, 4 × 6) |
| `comp` COMPRESSION [3] | Threshold (0, 0), Ratio (1, 0), Knee (2, 0) · Makeup (0, 3) · Auto Makeup toggle (1, 3, 2 wide) · Character [Clean \| Glue] (1, 4, 2 wide) |
| `time` TIME [2] | Attack (0, 0), Release (1, 0) · Auto Release toggle (0, 3, 2 wide) · "Lookahead" sub-label (0, 4, 2 wide) and combo (0, 5, 2 wide) |
| `detector` DETECTOR [2] | [Peak \| RMS] (0, 0, 2 wide) · Link bar (0, 1, 2 wide) |
| `sidechain` SIDECHAIN [3] | "Filter" sub-label (0, 0) and combo (1, 0, 2 wide) · Listen (1, 1, 2 wide) · Freq (0, 3), Q (1, 3), Gain (2, 3) |
| `output` OUTPUT [1] | Output (0, 0) · Mix (0, 3) |
| meter strip | In and out, stereo; gain reduction is in the history |

- **Transfer curve:** −60 … 0 dB on both axes, drawn from the same function the audio path uses, with a flat fill below it. Drag the threshold point, or anywhere left of it, to move the threshold (along the diagonal: right or up raises it). Drag right of it, down or up, to raise or lower the ratio. Shift drags finely; double-click resets the one under the mouse. The value being changed reads out in the corner.
- **History:** 4 s, newest at the right, on the same −60 … 0 dB scale: input level (dim fill from the bottom), output level (a line), gain reduction (from the top, in the value colour), the threshold as a dashed line, and the current reduction in the corner. Nothing is drawn in silence.
- **Dimming:** Makeup while Auto Makeup is on; the sidechain Freq and Q while the filter is off; its Gain unless the filter is a bell.
- The sidechain source picker is in the header (step A2).

## Factory presets

"Gentle vocal", "Drum punch", "Bass control", "Bus glue", "Mix glue", "Parallel smash", "Sidechain duck", "De-ess (sidechain bell)".

## Automated test targets

| Metric | Target |
|---|---|
| Static curve: steady sines across −60…0 dBFS, every knee and ratio | Output level within 0.05 dB of the curve |
| Attack and release times (steps of +20 dB) | Within ±5 % of the setting |
| Ratio ∞ with the slowest attack | Output never above threshold + attack overshoot (bounded, reported) |
| Mix at 0 % | Null against the latency-aligned input (< −120 dBFS) |
| Below threshold, makeup 0 dB | Null against the input (< −120 dBFS) |
| Reported latency vs measured, per Lookahead setting | Equal |
| Sidechain ducking (external key, input steady) | Gain reduction follows the key within the attack time |
| Conformance suite | All pass |

## A/B cases (against the Live target)

Deferred until a Live machine is available.

## A/B log

–

## Sign-off

- [x] Automated: unit, conformance and golden tests; validators (2026-10-09)
- [ ] Snapshots reviewed (100% / 200%)
- [ ] Sound signed off by the product owner (date):
- [ ] Feel signed off by the product owner (date):
