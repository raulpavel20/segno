# 0004. Tech stack: C++20, JUCE 9, tracktion_engine, CPM-pinned dependencies

- **Status:** Accepted (JUCE confirmed by the product owner; UI toolkit detail in [0012](0012-ui-toolkit-juce-everywhere.md))
- **Date:** 2026-10-08

## Context
Building a DAW engine from scratch (real-time graph, plugin hosting, automation, rendering, undo, time-stretch) would take years and is not where this project adds value ([ADR 0002](0002-device-suite-and-daw-in-parallel.md)). Our value is the devices and the UI.

Facts as of 2026-10:
- **JUCE 9.0.3** is the latest release, licensed AGPLv3 or commercial. It covers plugin formats, cross-platform UI, and audio and MIDI I/O.
- **tracktion_engine** (GPLv3+ or commercial) is a complete DAW engine built on JUCE.
  - `master` is stale; active development is on `develop`.
  - It pins a JUCE 8 commit but is tested daily against JUCE's development branch.
  - Custom native plugins are added by subclassing `te::Plugin` and registering it with `PluginManager`.
  - It includes headless rendering and Signalsmith Stretch time-stretching.
- Reusable DSP libraries compatible with our license:
  - GPLv3: sst-basic-blocks, sst-filters, sst-waveshapers, sst-effects.
  - MIT: sst-voicemanager, Signalsmith DSP, Airwindows.
  - Reference only: chowdsp_utils.
- Rust was considered and rejected: its GUI and plugin-hosting libraries are not mature enough for an application of this size.

## Decision
- **Language and build:** C++20, CMake (3.22+), Ninja.
- **Frameworks:** JUCE 9.0.3, plus tracktion_engine pinned to a specific `develop` commit (fetched as an archive, which avoids its SSH submodule).
- **CLAP export:** clap-juce-extensions.
- **Dependencies:** every dependency is pinned by tag or commit with **CPM.cmake** in a single file, `cmake/Dependencies.cmake`, which also records each dependency's license.
- **DSP reuse:** the sst-* libraries, Signalsmith DSP and sst-voicemanager, with simde for SIMD on ARM.
- **Tests:** Catch2 v3.

## Consequences
- We get a proven engine and plugin layer, and our effort goes into devices and UI.
- **Version coupling risk:** we use JUCE 9 while tracktion_engine pins JUCE 8.
  - A weekly canary build tracks both upstream development branches.
  - The fallback is tracktion_engine's own pinned JUCE commit.
  - All tracktion_engine API use is kept inside `app/src/engine/`.
- tracktion_engine's `develop` branch changes quickly. Updating the pinned commit is a deliberate monthly task with a checklist.
- JUCE plugin targets compile slowly. We use ccache and the CPM source cache, and pull requests rebuild only the devices that changed.
