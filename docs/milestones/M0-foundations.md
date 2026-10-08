# M0: Foundations

**Goal:** prove the whole pipeline end to end with one simple device, **Utility**:

```
DSP code ─┬─ automated tests
          ├─ offline render tool
          ├─ plugin builds
          └─ native device in the DAW
```

Everything later is built on this. See [roadmap.md](../roadmap.md) and [architecture.md](../architecture.md).

**Status:** ☑ done (2026-10-08 → 2026-10-09)

## Steps

| # | Step | Done when | Status |
|---|---|---|---|
| 1 | **Repository:** `git init`, `LICENSE` (AGPL-3.0), REUSE, `CLAUDE.md`, `.gitignore`, `.clang-format`, `docs/ids.md` | Files exist; the ID scheme is documented | ☑ |
| 2 | **Build system:** CMake + presets (dev, rel, asan, rtsan), CPM with pinned dependencies, build options (`FD_BUILD_PLUGINS`, `FD_BUILD_APP`, `FD_BUILD_TESTS`) | `cmake --preset dev && cmake --build --preset dev` works | ☑ |
| 3 | **`libs/dsp` seed:** dB helpers, parameter smoothers, control grid, one-pole, biquad, DC blocker, denormal guard | Unit tests pass | ☑ |
| 4 | **`libs/testkit`:** signals, metrics (peak/RMS/DC, null residual, clicks, latency, FFT magnitude), WAV I/O, golden compare | Unit tests pass, including "the metric detects what it should" | ☑ |
| 5 | **`libs/device` + Utility DSP + conformance suite** | The conformance suite passes for Utility; zero allocations in `process()` | ☑ |
| 6 | **`tools/render` (`fd-render`)** | Renders Utility over test signals to WAV + metrics JSON | ☑ |
| 7 | **JUCE modules seed** (`fd_ui`, `fd_device_ui`, `fd_host_juce`): tokens, LookAndFeel, Knob, Toggle, Meter, DevicePanel, Utility panel; `tools/snapshot`, `tools/gallery` | Snapshot PNGs render | ☑ (gallery moves to step 11) |
| 8 | **Plugins:** Utility as VST3, CLAP, LV2 and Standalone | pluginval (strictness 10) and clap-validator pass locally | ☑ |
| 9 | **DAW skeleton:** tracktion_engine boots; Utility registered as a native device; a WAV plays through it; cross-host null tests | Null tests pass (< −120 dBFS) | ☑ |
| 10 | **Rendering spike** ([ADR 0012](../decisions/0012-ui-toolkit-juce-everywhere.md)): device panel + 32-track arrangement, standard renderer vs OpenGL | Frame times measured on the reference machine; result recorded as an ADR | ☑ [ADR 0015](../decisions/0015-rendering-opengl-for-the-daw.md): OpenGL for the DAW, software for plugin editors |
| 11 | **Design gate** ([ADR 0014](../decisions/0014-flat-darker-ableton-adjacent.md)): `tools/gallery`, an interactive gallery switching live between flat candidate themes (device panel, controls in every state, meter, clips on the canvas, an automation lane) | Product owner picks one → `design/design-language.md` | ☑ Steel chosen (round 2) → [design-language.md](../design/design-language.md) |
| 12 | **CI workflows** for Linux, Windows and macOS | Workflows written; they run once the GitHub repo exists | ☑ (written; first run when pushed) |

Notes from implementation:
- tracktion_engine needed a one-line fix to compile with Clang + libstdc++. It's applied as a pinned patch (`cmake/patches/`) and should be upstreamed.
- Rendering with `te::Renderer::Parameters` needs a `UIBehaviour` that runs tasks (the default does nothing). The app will need one too, for progress and warnings.
- clap-validator requires a parameter rescan after a state load. The plugin adapter requests one through `updateHostDisplay`.
- JUCE's CLAP wrapper reports success when loading invalid state. The adapter ignores such state safely, but clap-validator flags it as a warning.

Changes from the original M0 sketch:
- The oversampler and anti-aliased waveshapers move to M1, where the Saturator first needs them.
- Manual testing is Linux-only ([dev-environment.md](../dev-environment.md)).

## Exit criteria

**Automated:**
- All unit and conformance tests are green on Linux.
- Utility passes pluginval and clap-validator.
- Both null tests pass.
- CI workflows are written.

**Product owner, on the reference machine:**
- [x] The Utility plugin works as a standalone app (third-party Linux host check deferred to M1 with more devices).
- [x] The DAW skeleton plays a WAV through Utility over PipeWire/JACK without dropouts.
- [x] Rendering spike reviewed: OpenGL feels smoother; text good either way.
- [x] Design direction chosen at the design gate (Steel, 2026-10-09).
