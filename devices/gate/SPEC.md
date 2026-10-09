# Gate: device spec

- **Status:** Approved by the product owner (2026-10-09); IDs are permanent
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

| ID (permanent) | Name | Description | Range | Default | Unit | Curve / skew | Smoothing | Automatable |
|---|---|---|---|---|---|---|---|---|
| `threshold` | Threshold | The level above which the gate opens. | −80 … 0 | −40 | dB | linear | 20 ms | Yes |
| `return` | Return | How far below the threshold the level must fall before the gate closes, so it doesn't chatter. | 0 … 24 | 3 | dB | linear | 20 ms | Yes |
| `mode` | Mode | Gate cuts the sound below the threshold; Expander turns it down gradually. | Gate, Expander | Gate | choice | – | 10 ms crossfade | Yes |
| `ratio` | Ratio | How strongly the expander turns down what falls below the threshold. | 1 … 10 | 2 | :1 (Expander) | centre 2 | 20 ms | Yes |
| `range` | Range | How far the sound is turned down while the gate is closed; -inf is silence. | −inf … 0 | −inf | dB | dB | 20 ms | Yes |
| `attack` | Attack | How quickly the gate opens. | 0.02 … 150 | 0.1 | ms | centre 1 | 20 ms multiplicative | Yes |
| `hold` | Hold | How long the gate stays open after the level falls below the threshold. | 0 … 1 000 | 20 | ms | centre 50 | 20 ms | Yes |
| `release` | Release | How quickly the gate closes after the hold. | 1 … 3 000 | 100 | ms | centre 100 | 20 ms multiplicative | Yes |
| `flip` | Flip | Inverts the gate: it turns down what's above the threshold and lets the rest through. | Off / On | Off | toggle | – | 10 ms crossfade | Yes |
| `lookahead` | Lookahead | Delays the audio a little so the gate opens before a hit, keeping its attack. | Off, 1 ms, 10 ms | Off | choice | – | – | **No** (sets latency, as in the Compressor) |
| `sc.filter` | Sidechain Filter | Filters what the gate listens to, for example so it ignores the bass. | Off, High Pass, Low Pass, Band Pass, Bell | Off | choice | – | 10 ms crossfade | Yes |
| `sc.freq` | Sidechain Freq | The sidechain filter's frequency. | 20 … 20 000 | 100 | Hz | centre 632 Hz | 20 ms multiplicative | Yes |
| `sc.q` | Sidechain Q | How narrow the sidechain filter is. | 0.1 … 10 | 0.71 | – | centre 1 | 20 ms multiplicative | Yes |
| `sc.gain` | Sidechain Gain | How much the sidechain bell boosts or cuts. | −24 … +24 | 0 | dB (Bell only) | linear | 20 ms | Yes |
| `sc.listen` | Listen | Plays what the gate listens to, to set the sidechain filter by ear. | Off / On | Off | toggle | – | 10 ms crossfade | Yes |

The sidechain source is host routing (M2 step A2). State version 1.

## DSP design

- **The shared dynamics core** (M2 step A1): detector (peak, stereo-linked) on the sidechain or the input, through the optional sidechain filter (the Compressor's, shared: `dsp::SidechainFilter`).
- **Detector:** the loudest channel's peak over the last 10 ms, so low notes read steadily instead of dropping at every zero crossing. Opening is immediate; the window delays closing by 10 ms, which the hold absorbs: holds of 10 ms or more are exact, shorter ones act as 10 ms.
- **Gate:** opens when the level rises above Threshold and closes when it falls below Threshold − Return (hysteresis, so it doesn't chatter). Hold keeps it open after the level drops. The gain moves between 0 dB and Range with the attack (10–90 %) and release (to −60 dB of the change) times, which glide over 20 ms when changed. It starts closed, as after silence.
- **Expander:** downward expansion below the threshold at Ratio (the gain computer's expansion curve, hard knee), limited by Range, with the same attack, hold and release: when the gain would fall, it waits for the hold. Return doesn't apply. Switching Mode crossfades the two over 10 ms.
- **Flip** inverts the gain: the output is the input minus the gated signal, so only what the gate removes is heard. Switching crossfades over 10 ms.
- **Range:** the bottom of its travel reads "-inf dB" and is silence (gains under −99 dB are zero); above it, the closed gain in dB.
- **Listen** sends the filtered detector signal to the output, crossfading over 10 ms.
- **Lookahead:** opens before a transient arrives (no clicks on drums). Latency follows the setting, as in the Compressor ([ADR 0020](../../docs/decisions/0020-latency-follows-lookahead.md)).
- **No zipper or click:** gain changes are smoothed per sample in dB; Range −inf closes to silence.
- **Analysis:**
  - slots 0–1 = input peak, left and right (linear);
  - slots 2–3 = output peak, left and right (linear);
  - slot 4 = the detector's level (linear);
  - slot 5 = how far the gain is turned down (dB as a positive number, peak-held);
  - slot 6 = 1 if the gate was open at any time since last read;
  - FIFO 0 = history frames of (detector level dB, output level dB, open 0 or 1), one every 10 ms.

## UI layout

On the module grid ([device-panels.md](../../docs/design/device-panels.md)): sections left to right, each `id` TITLE [columns], with controls at (column, row) and spans; knobs are 1 × 3 cells. As built (B2).

| Section | Controls |
|---|---|
| `history` HISTORY [6] | Level history (0, 0, 6 × 6) with threshold and return lines (drag either), and shading where the gate is open |
| `gate` GATE [3] | Threshold (0, 0), Return (1, 0), Range (2, 0) · [Gate \| Expander] stacked (0, 3, 2 wide, 2 rows) · Ratio (2, 3) · Flip (0, 5, 2 wide) |
| `time` TIME [3] | Attack (0, 0), Hold (1, 0), Release (2, 0) · "Lookahead" sub-label (0, 3, 2 wide) and combo (0, 4, 2 wide) |
| `sidechain` SIDECHAIN [3] | "Filter" sub-label (0, 0) and combo (1, 0, 2 wide) · Listen (1, 1, 2 wide) · Freq (0, 3), Q (1, 3), Gain (2, 3) |
| meter strip | In and out, stereo |

- **History:** 4 s, newest at the right, −80 … 0 dB: the detector's level (dim fill from the bottom), the output level (a line), shading where the gate was open. The threshold is a solid line and, in Gate mode, the return a dashed one below it. Drag a line up or down (Shift: finely); double-click resets it. The value being changed reads out in the corner.
- The Mode segments are stacked because "Expander" needs two cells.
- **Dimming:** Ratio in Gate mode; Return in Expander mode; the sidechain Freq and Q while the filter is off; its Gain unless the filter is a bell.
- The sidechain source picker is in the header (step A2).

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

- [x] Automated: unit, conformance and golden tests; validators (2026-10-09)
- [ ] Snapshots reviewed (100% / 200%)
- [ ] Sound signed off by the product owner (date):
- [ ] Feel signed off by the product owner (date):
