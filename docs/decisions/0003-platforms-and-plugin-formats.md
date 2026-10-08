# 0003. Platforms and plugin formats

- **Status:** Accepted
- **Date:** 2026-10-08

## Context
The product owner develops on Linux, and Linux is underserved by modern music software. Ableton Live, the quality benchmark, runs only on Windows and macOS and loads VST3 and AU plugins, not CLAP. To A/B test devices inside Live, Windows and macOS builds are required.

Facts as of 2026-10:
- JUCE 9 exports VST3, AU, LV2 and standalone, but **not CLAP**. CLAP export was promised for JUCE 9 and has not shipped.
- **clap-juce-extensions** (MIT) adds CLAP export to JUCE plugins and supports JUCE 9.
- The VST3 SDK has been MIT-licensed since October 2025.
- Neither JUCE nor tracktion_engine can **host** CLAP plugins.

## Decision
- **Platforms:** Linux, Windows and macOS (universal binaries) are all first-class and built in continuous integration from the first milestone.
- **Plugin formats we ship:** CLAP, VST3, LV2 (Linux), AU (macOS) and standalone.
- **Formats our DAW hosts:** VST3 on all platforms, AU on macOS and LV2 on Linux from M1. **CLAP hosting** comes in M3 through a custom plugin format built on the CLAP SDK.

## Consequences
- Three-platform continuous integration from day one; ARM/universal builds catch SIMD portability problems early.
- CLAP export depends on an unofficial wrapper. It is isolated in one CMake function so we can switch to JUCE's native CLAP support when it arrives.
- A "CLAP-first" design (writing to CLAP and wrapping it for other formats) was considered and rejected for now: it drops LV2 and adds more custom code to maintain.
