# 0013. One repository for the suite and the DAW

- **Status:** Accepted
- **Date:** 2026-10-08

## Context
We build two products from one device library ([ADR 0002](0002-device-suite-and-daw-in-parallel.md)): the device suite (plugins) and the DAW. The first idea was two repositories, one for the DAW and one for the suite. Several things cut across both:
- the device API, the DSP library and the test kit;
- the JUCE UI modules: design tokens and components, which the DAW window uses too;
- the cross-host null tests, which need both adapters;
- the docs and ADRs.

Before the device API is frozen (end of M1), it will change often. Most code is written by an AI agent working across sessions, and that works best when one checkout contains everything and a change can land in one commit.

## Decision
**One repository (a monorepo)**, with the two products kept clearly separate inside it:

```
foss-daw/
  docs/                  product docs + ADRs (shared)
  libs/                  JUCE-free core: dsp, device, testkit (shared)
  modules/               JUCE modules: fd_ui, fd_device_ui, fd_host_juce (shared)
  devices/               the suite's devices (DSP + UI + SPEC + presets)
  plugins/               suite packaging: plugin targets and installers
  app/                   the DAW (tracktion_engine)
  tools/  tests/  cmake/
```

- **The products can be built independently.**
  - `-DFD_BUILD_APP=OFF` builds only the suite, without downloading tracktion_engine.
  - `-DFD_BUILD_PLUGINS=OFF` builds only the DAW.
- **Releases are versioned separately**, with tags like `suite/v0.3.0` and `daw/v0.1.0` and separate packaging workflows.
- **CI only builds what a change affects**, using path filters.

## Consequences
- Atomic changes across the device API, devices, UI and both hosts; one CI setup; one set of docs.
- Contributors who only care about the plugins can ignore `app/` entirely.
- If a separate repository is ever needed (for example, a different team or release process for the suite), `git filter-repo` can split out `libs/`, `modules/`, `devices/` and `plugins/` with full history.
- The repository is larger than either part alone, which is acceptable at this scale.
