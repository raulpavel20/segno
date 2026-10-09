# 0020. Latency that follows a lookahead setting (dynamics devices)

- **Status:** Proposed (for the product owner, with the Compressor, Gate and Limiter SPECs)
- **Date:** 2026-10-09
- **Builds on:** [0016](0016-constant-device-latency.md) (constant latency per device instance), whose exception clause this uses

## Context
The Compressor and Gate offer lookahead (Off, 1 ms, 10 ms); the Limiter always looks ahead (1.5, 3 or 6 ms, plus its true-peak interpolator). Lookahead lets a detector act on a transient before it arrives, at the cost of delaying the audio by the same time.

ADR 0016 says a device's latency never changes with its parameters: settings that need less are padded to the maximum. For lookahead that would mean:
- every Compressor and Gate delays its track by 10 ms even with lookahead off, the common case;
- monitoring through them while recording (C5) is 10 ms late;
- a Limiter set to 1.5 ms pays for 6 ms.

ADR 0016 foresaw this: a setting that needs much more latency gets its own ADR, and becomes either a non-automatable setting that re-prepares the device or a separate device.

## Decision
- **Lookahead is a non-automatable setting whose latency follows it** (the Limiter's True Peak too, since it adds the interpolator's delay). With lookahead off, latency is zero.
- **Within one setting, ADR 0016 holds:** the latency is constant and a whole number of samples, and the dry path is delayed to match (the Mix control never comb-filters).
- **Parameters that change latency are marked** (`ParamFlags::ChangesLatency`) and are never automatable. Only the user changes them, as they would a configuration setting.
- **Devices prepare for the longest setting:** buffers are allocated in `prepare()` for the maximum lookahead. A change of setting only moves the delay, with no allocation on the audio thread. The device resets its delay lines, so a change while audio plays may click once.
- **Hosts learn of a change on the message thread:**
  - **Plugins:** the adapter reports the new latency (`setLatencySamples`): VST3 sends a latency change to the host, CLAP calls its latency-changed callback, AU posts the latency property. Hosts may restart processing briefly.
  - **The DAW:** the device plugin reports its new latency, and the playback graph is rebuilt, re-aligning delay compensation.
- **The conformance suite** measures latency for every value of a latency-changing parameter, not only random settings; parameter fuzzing leaves those parameters alone.

## Consequences
- Compressors and gates add no delay unless lookahead is on, so live monitoring through them stays tight.
- Changing lookahead can briefly interrupt playback (a graph rebuild in the DAW, a processing restart in some plugin hosts). It's a set-and-forget setting, so that's accepted, and it never happens during automation.
- The device API gains `ParamFlags::ChangesLatency`; both adapters watch those parameters and re-query `latencySamples()` when one changes.
- Linear-phase EQ (M3) can use the same mechanism, or a separate device, as ADR 0016 says.
