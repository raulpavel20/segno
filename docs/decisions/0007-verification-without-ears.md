# 0007. Automated, measurable verification + human sign-off

- **Status:** Accepted
- **Date:** 2026-10-08

## Context
Most implementation is done by Claude, an AI coding agent. It cannot listen to audio or feel how a UI responds. It can analyse audio numerically and look at rendered images. The product owner can listen and use the software, but has limited time. Device quality must hold up against Ableton Live Suite.

## Decision
Verification has four layers, and a feature is done only when all of them pass:

1. **Automated correctness**
   - Unit tests for DSP building blocks.
   - A **conformance suite** that runs on every registered device: block-size invariance, sample-rate sweep, silence, parameter fuzzing, click detection, state round-trip, deterministic reset, latency reporting, zero allocations in `process()`, voice handling.
   - Plugin validators: pluginval, clap-validator, auval.
   - Sanitizers: ASan, UBSan, RealtimeSanitizer.
   - Cross-adapter null tests.
2. **Measured quality**
   - Per-device **golden metrics** (frequency response, harmonics, aliasing, envelope timing) with tolerances, compared across platforms by metric, never by bit-exact audio.
   - Plots generated for review.
   - **A/B gap analysis** (once a machine with Live is available): the product owner renders standard test signals through the Live target device, and the tools report the numeric differences. Until then, manual testing is Linux-only and A/B against Live is deferred ([dev-environment.md](../dev-environment.md)).
3. **UI checks:** offscreen snapshots at 1× and 2× (reviewed, and compared with golden images on Linux), paint-time benchmarks, and interaction logic in pure models with unit tests.
4. **Human sign-off:** the product owner signs off sound and feel for every device and UI feature, by ear on the reference Linux machine, on headphones or monitors. Listening notes are recorded in the device's `SPEC.md` and turned into measurable checks where possible.

## Consequences
- Regressions are caught automatically, and "sounds different" becomes concrete numbers.
- Golden files only change with a stated reason in the commit.
- The product owner's listening and hands-on testing are on the critical path of every milestone. A nightly "audition pack" (renders, plots, report) keeps that effort small.
- A/B reference renders from Ableton are kept locally and never committed.
