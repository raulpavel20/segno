# 0002. Device suite and DAW built in parallel from one device library

- **Status:** Accepted
- **Date:** 2026-10-08

## Context
Most of Ableton Live Suite's value lies in its comprehensive, high-quality built-in instruments and effects. An arrangement DAW alone would add little: Tracktion Waveform Free is already a no-cost arrangement DAW on tracktion_engine, though it is proprietary and its built-in device set is small.

Options considered:
1. **Plugin suite first:** quickest to become useful in existing DAWs, but no integrated experience.
2. **DAW with native-only devices:** fully integrated, but a long time before anything is usable, and the devices are locked into one host.
3. **Both in parallel:** one device library that runs inside our DAW *and* is shipped as plugins.

## Decision
**Both in parallel (option 3).** Each device is written once against a JUCE-free device API ([ADR 0006](0006-juce-free-core-and-module-boundaries.md)) and runs through two thin adapters:
- a plugin adapter (CLAP/VST3/LV2/AU/standalone);
- a tracktion_engine adapter that makes the device a native plugin type in our DAW.

## Consequences
- The devices are useful early, in any DAW, including inside Ableton Live, where they can be A/B-tested by ear against their benchmarks.
- Our DAW gets native integration: a docked chain, no floating windows, and racks and macros later.
- Both adapters have to be kept equivalent. Null tests between the two hosts are part of continuous integration.
- The work is split across two products, so milestones pair device work with DAW work (see [roadmap.md](../roadmap.md)) to keep both moving.
