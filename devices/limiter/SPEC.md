# Limiter: device spec

- **Status:** Approved by the product owner (2026-10-09); IDs are permanent
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

| ID (permanent) | Name | Description | Range | Default | Unit | Curve / skew | Smoothing | Automatable |
|---|---|---|---|---|---|---|---|---|
| `gain` | Gain | Drives the signal into the limiter, making it louder. | 0 … +24 | 0 | dB | linear | 20 ms | Yes |
| `ceiling` | Ceiling | The highest level the output reaches. | −24 … 0 | −0.3 | dB (dBTP with True Peak on) | linear | 20 ms | Yes |
| `release` | Release | How quickly the limiter lets go after a peak. | 1 … 1 000 | 80 | ms | centre 80 | 20 ms multiplicative | Yes |
| `release.auto` | Auto Release | Adapts the release to the material, for loudness without pumping. | Off / On | On | toggle | – | – | Yes |
| `mode` | Mode | Standard: lowering the Ceiling limits harder; Maximise: the Ceiling moves the whole output, the limiting stays the same. | Standard, Maximise | Standard | choice | – | 10 ms crossfade | Yes |
| `truepeak` | True Peak | Also catches peaks between samples, so the output stays under the ceiling after conversion. | Off / On | On | toggle | – | – | **No** (changes latency slightly; set before use) |
| `link` | Channels | Whether both channels are limited together, separately, or as mid and side. | Stereo, L/R, M/S | Stereo | choice | – | 10 ms crossfade | Yes |
| `lookahead` | Lookahead | How far ahead the limiter looks: longer is smoother, shorter adds less delay. | 1.5 ms, 3 ms, 6 ms | 3 ms | choice | – | – | **No** (sets latency) |

- **Standard:** Gain drives into the limiter and the Ceiling caps the output; lowering the Ceiling limits harder.
- **Maximise:** Gain and Ceiling are linked: moving the Ceiling moves the whole output, and the amount of limiting stays the same. Gain alone sets how hard it's driven. For mastering to a target peak. (Product owner, 2026-10-09.)

State version 1.

## DSP design

- **Lookahead peak limiter.** Five gain computers run side by side (linked, left, right, mid, side), so a change of Channels can crossfade. Each one:
  - takes the gain each sample needs to stay under the threshold;
  - holds the smallest over the lookahead plus 10 ms (the hold, so the gain doesn't ripple on low notes);
  - ramps it over the lookahead (a moving average), so the gain reaches its value exactly when the peak arrives;
  - then releases exponentially in dB (to −60 dB of the change in the Release time). Auto release blends from a quarter of Release after short peaks to four times it once limiting has lasted about 400 ms.
- **The ceiling:**
  - Standard limits at the ceiling.
  - Maximise limits at full scale and then moves the whole output down to the ceiling, so the limiting doesn't change with it.
  - Switching crossfades over 10 ms. The makeup and the ceiling are delayed with the audio they apply to.
  - A last clamp at the ceiling catches what smoothing can't foresee (the ceiling moving quickly).
  - The output never exceeds the ceiling.
- **True peak:** left, right, mid and side each run through a 4× oversampling interpolator (ITU-R BS.1770, `dsp::TruePeakDetector`), and the larger of the sample and the values between samples counts. It adds the interpolator's delay (8 samples) to the latency.
- **Channels:**
  - Stereo: one gain for both channels;
  - L/R: independent;
  - M/S: the mid is limited to the threshold and the side takes the room left, then both are decoded; the decoded channels stay under the ceiling and the centre keeps its level.
  - Switching crossfades over 10 ms.
- **Latency:** the lookahead (72, 144 or 288 samples at 48 kHz) plus, with True Peak on, the interpolator. Both settings are configuration, not automatable; hosts see a change as a configuration change ([ADR 0020](../../docs/decisions/0020-latency-follows-lookahead.md)).
- **Loudness:** K-weighted short-term (3 s) and integrated (gated) LUFS of the output, from the shared dynamics core; the integrated value is published every 100 ms. The readout's Reset clears it and the true-peak maximum: a display command (`AnalysisBus::request`), not a parameter.
- **Analysis:**
  - slots 0–1 = the output's true peak, left and right (linear, peak-held);
  - slot 2 = gain reduction (dB as a positive number, peak-held);
  - slot 3 = short-term LUFS;
  - slot 4 = integrated LUFS;
  - slot 5 = the loudest true peak since the last reset (dBTP);
  - request 0 = reset the loudness readout;
  - FIFO 0 = history frames of (input dB after Gain, output dB, gain reduction dB), one every 10 ms.

## UI layout

On the module grid ([device-panels.md](../../docs/design/device-panels.md)): sections left to right, each `id` TITLE [columns], with controls at (column, row) and spans; knobs are 1 × 3 cells. As built (B3).

| Section | Controls |
|---|---|
| `history` HISTORY [6] | Level and gain-reduction history (0, 0, 6 × 6), −36 … 0 dB, with the ceiling as a dashed line |
| `limit` LIMIT [3] | Gain (0, 0), Ceiling (1, 0), Release (2, 0) · Auto Release (0, 3, 2 wide) · True Peak (0, 4, 2 wide) |
| `mode` MODE [3] | [Standard \| Maximise] stacked (0, 0, 3 wide, 2 rows) · Channels [Stereo \| L/R \| M/S] (0, 2, 3 wide) · "Lookahead" sub-label (0, 3, 2 wide) and combo (0, 4, 2 wide) |
| `loudness` LOUDNESS [3] | Readouts (0, 0, 3 × 5): short-term and integrated LUFS, the true-peak maximum · Reset (0, 5, 3 wide) |
| meter strip | Output (true peak), stereo |

- The history is the Compressor's (`ui::DynamicsHistory`): input after Gain (dim fill), output (a line), gain reduction from the top.
- The Mode segments are stacked because "Maximise" needs two cells.

## Factory presets

Built and named by Claude ([presets.md](../../docs/design/presets.md) §13; `devices/limiter/presets/`, made with `fd-presets` from recipes); the product owner signs them off by ear. 12 presets, each checked by the conformance suite: it loads, plays cleanly under 0 dBFS and differs from the rest.

- **Safety -1 dBTP** (Master, Clean): Catches overs and nothing else; nothing goes past -1 dB true peak.
- **Transparent** (Master, Subtle): A little louder, with the longest look ahead and a smooth release.
- **Streaming master** (Master): Moderately louder, under -1 dB true peak, as streaming services ask.
- **Loud master** (Master, Heavy): Pushed hard for loudness; listen for pumping and distortion.
- **Maximise -1 dBTP** (Master): Maximise mode: Gain sets the limiting, the Ceiling only the output level.
- **Mid-side master** (Master, Wide): Limits the mid and the side apart, keeping the centre's level.
- **Dual mono** (Wide): Each channel on its own: wider-sounding, but the image can shift on peaks.
- **Gentle bus** (Bus, Subtle): Rounds off a group's peaks with a slow, fixed release.
- **Drum bus peaks** (Drums, Bus): Shaves the tallest drum peaks fast, for headroom.
- **Clip control** (Drums, Heavy): Very fast, almost clipping: hard transients turned into loudness.
- **Vocal peaks** (Vocals): Catches a vocal's loudest moments after the compressor.
- **Bass peaks** (Bass): Holds a bass's peaks with a release long enough not to distort the lows.

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

- [x] Automated: unit, conformance and golden tests; validators (2026-10-09)
- [ ] Snapshots reviewed (100% / 200%)
- [ ] Sound signed off by the product owner (date):
- [ ] Feel signed off by the product owner (date):
