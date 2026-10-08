# 0006. DSP and device core never depend on JUCE

- **Status:** Accepted
- **Date:** 2026-10-08

## Context
Every device has to run in two hosts: the plugin adapter and the tracktion_engine adapter ([ADR 0002](0002-device-suite-and-daw-in-parallel.md)). Most of the code will be written by an AI agent working across many sessions, so tests must be fast and deterministic and must not need a GUI or message thread. Module boundaries also have to be explicit enough that they don't wear away over time.

## Decision
- These modules **never include JUCE**: `libs/dsp`, `libs/device`, `libs/testkit`, `devices/*/dsp` and `tools/render`.
- JUCE-dependent internal libraries are packaged as JUCE modules (`fd_ui`, `fd_device_ui`, `fd_host_juce`), following tracktion_engine's convention, so JUCE configuration flags stay consistent.
- **Hard rules:**
  - `process()` never allocates, locks or does I/O.
  - The UI calls the same pure DSP functions as the audio path and never reimplements them.
  - Devices never write their own parameters; all edits go through the host binding.
- Boundaries are enforced through CMake target dependencies and an include check that runs in continuous integration.

## Consequences
- DSP and device tests build and run in seconds and produce bit-identical results on a given platform.
- Each adapter is small. Changing hosts, or adding one (for example a CLAP-first wrapper), means writing one adapter.
- Some conversion code is needed at the JUCE line: MIDI, state and sample loading. It lives in `fd_host_juce`.
