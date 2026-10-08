# 0016. Constant latency per device instance

- **Status:** Accepted
- **Date:** 2026-10-09

## Context
Some devices need latency: oversampling FIR filters, lookahead, and linear-phase modes later. If a device's latency changes when a parameter changes (for example the Saturator's Quality setting), hosts have to re-negotiate it:
- VST3 and CLAP hosts handle that unevenly, and some restart processing or click;
- in our DAW, tracktion_engine has to rebuild the playback graph.

It also makes automation of those parameters unsafe.

## Decision
- **A device reports one latency that never changes while the instance is prepared.** It may depend on the sample rate (set in `prepare()`), but never on parameter values.
- **Settings that need less latency are padded** up to the maximum (e.g. the oversampler pads 2× and 4× to the 8× latency; `fd::dsp::Oversampler`).
- **Latency is a whole number of samples**, so dry paths and the host's delay compensation align exactly.
- **Exception:** a mode that would need *much* more latency (e.g. linear-phase EQ in M3) gets its own ADR. It is then either a non-automatable setting that re-prepares the device, or a separate device.
- The conformance suite checks that measured latency equals reported latency for randomised settings.

## Consequences
- Small, constant latency costs (the Saturator reports 73 samples at every Quality setting, about 1.5 ms at 48 kHz).
- Quality and oversampling parameters are not automatable (the `ParamFlags::Automatable` flag is off).
- No host-side latency renegotiation code is needed in M1.
