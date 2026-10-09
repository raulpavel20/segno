# Architecture overview

This is a summary of how the system is put together. Each choice is justified in an ADR in [decisions/](decisions/README.md). Detailed design for each module lives next to its code once that code exists.

## The core idea

**One device library, two hosts.** Every instrument and effect is written once against a small C++ device API that has no JUCE dependency. Two thin adapters run the same device:

```
                        ┌──────────────────────────────┐
                        │  devices/<name>  (DSP + UI)   │
                        │  written once, against        │
                        │  libs/device + libs/dsp       │
                        └──────────────┬───────────────┘
                 ┌─────────────────────┴──────────────────────┐
                 ▼                                            ▼
   ┌───────────────────────────────┐          ┌───────────────────────────────┐
   │ Plugin adapter (plugins/)     │          │ DAW adapter (app/)            │
   │ JUCE AudioProcessor           │          │ te::Plugin subclass inside    │
   │ → VST3 / AU / LV2 / Standalone│          │ tracktion_engine              │
   │ → CLAP via clap-juce-extensions│         │ → native, docked device chain │
   └───────────────────────────────┘          └───────────────────────────────┘
        any DAW (incl. Ableton for A/B)              our arrangement DAW
```

## Module map

Nothing below the line may include JUCE. This keeps the DSP and its tests fast, deterministic and independent of any host ([ADR 0006](decisions/0006-juce-free-core-and-module-boundaries.md)).

```
libs/dsp        DSP building blocks: filters, oscillators, oversampling, ADAA shapers,
                dynamics detectors, delay lines, smoothing (+ reused libraries, see ADR 0004)
libs/device     Device API: ParamSpec / ParamStore, EventList, State (JSON), Registry, AnalysisBus,
                VoiceManager, AssetHandoff (heavy data to the audio thread)
libs/audiofile  audio file decoding (WAV/AIFF/FLAC/MP3 via dr_libs) and WAV writing
libs/testkit    test signals, audio metrics, FFT, WAV I/O, golden comparison
devices/X/dsp   each device's DSP, SPEC.md, tests, presets
tools/render    fd-render: renders any device + state + signal/MIDI to WAV + metrics
──────────────────────────── JUCE line ────────────────────────────
modules/fd_ui          design tokens (colour roles, text styles, motion, metrics), LookAndFeel,
                       controls, shared shell components (ADR 0023)
modules/fd_device_ui   DevicePanel base and panel kit (sections on the module grid, ADR 0022),
                       parameter binding, shared displays (envelope, filter)
modules/fd_host_juce   state ↔ JUCE, MIDI conversion
devices/X/ui           each device's panel
plugins/               plugin adapter + per-device plugin targets
app/                   the DAW (tracktion_engine), DevicePlugin adapter, shell UI,
                       MCP server (app/src/mcp; DAW only, ADR 0021)
tools/snapshot, tools/gallery, tools/analyze
```

## Device contract (key points)

- **Parameters** are declared in a static table with fields: permanent ID, range, default, skew, unit, smoothing, flags. Both adapters build their host parameters from this table, so automation, undo and presets work identically in both.
- **`process()`** runs on the audio thread and is `noexcept`. It never allocates, locks or does I/O. This is enforced by an allocation hook in the tests and by RealtimeSanitizer.
- **Control-rate updates** happen on a grid aligned to absolute sample position. Output is therefore identical whatever the host's block size, and the tests check this bit for bit.
- **Large data** (samples, wavetables) is prepared off the audio thread and swapped in lock-free with `AssetHandoff`. UI edits to non-parameter state go through `ParamBinding::applyExtraState` ([ADR 0017](decisions/0017-heavy-data-and-non-parameter-state.md)).
- **State and presets** use one JSON format, `{device, stateVersion, params, extra}`. It stores plain (not normalised) values and has versioned migrations. The plugin and the DAW share it, so presets move between them.
- **The UI never computes its own DSP.** Curves and displays call the same pure functions the audio path uses, and live data reaches the UI through a lock-free analysis bus (meters, spectra, playheads).
- **Devices never write their own parameters.** All edits go through the host binding, so automation and undo always work.

## The DAW

- **Engine:** tracktion_engine provides the edit model, playback graph, plugin hosting, automation, rendering, undo and time-stretch. We pin a specific commit ([ADR 0004](decisions/0004-tech-stack.md)).
- **Our code** is the shell UI (transport, arrangement, browser, inspector, detail panel), the `DevicePlugin` adapter that registers every device as a native plugin type, and the interaction models (timeline, selection, snapping). These models are pure classes with unit tests.
- **Assistants (MCP):** a built-in MCP server lets any assistant the user connects drive the DAW and every device in it ([ADR 0021](decisions/0021-mcp-server.md)). It is a third client of `engine::Session`, next to the UI and the tests. It reaches devices only through the host binding, so the devices and their plugin builds know nothing about it. It is off by default and only reachable from the local machine. Layers ([mcp.md §7](design/mcp.md)):
  - `app/src/mcp/protocol` and `transport` (library `fd_mcp`): JSON-RPC for both protocol eras, Streamable HTTP on cpp-httplib, the stdio relay; JUCE-free, tested as `[fast]`.
  - `app/src/mcp/tools` and `McpServer` (`fd_app_mcp`): the tools over the Session, run on the message thread; tested against a real Session.
  - CTest `mcp_only_in_the_daw` fails if anything below `app/` includes or links it; `app_ui_does_not_include_tracktion` keeps tracktion out of `app/src/mcp` and JUCE out of its protocol and transports.
- **Third-party plugins:** VST3 on all platforms, AU on macOS, LV2 on Linux. CLAP hosting is added later through a custom plugin format, because neither JUCE nor tracktion_engine can host CLAP yet.

## The UI system

- **JUCE is the only UI toolkit** ([ADR 0012](decisions/0012-ui-toolkit-juce-everywhere.md)). Whether heavy views use JUCE's standard renderer or OpenGL is decided by a rendering spike in M0.
- **One repository** holds both products and the shared code ([ADR 0013](decisions/0013-single-repository.md)).
- **Design tokens** (`design/tokens.json` → generated `Tokens.h`) are the only source of colours, spacing, type and radii. This is what makes a light theme possible later.
- One bundled variable font; SVG icons; display-refresh-driven animation for meters, playheads and zoom.
- The rules themselves are in [design/ui-principles.md](design/ui-principles.md).

## Verification

Claude does most of the implementation but cannot hear audio or feel the UI. Verification is therefore automated and numeric, with human sign-off as the final gate ([ADR 0007](decisions/0007-verification-without-ears.md)):

- DSP unit tests, and a **conformance suite** that runs automatically on every device: block-size invariance, sample-rate sweep, silence, parameter fuzzing, click detection, state round-trip, latency, zero allocations, voice handling.
- **Golden metrics** per device: frequency response, harmonic content, aliasing, envelope timing. Tolerances are tuned per platform.
- **Plots**, and **A/B gap analysis against Live** once a Live machine is available, which turn "sounds different" into measured differences. Manual testing is currently Linux-only ([dev-environment.md](dev-environment.md)).
- **UI snapshots** at 1× and 2× scale, and paint-time benchmarks.
- **Plugin validators:** pluginval, clap-validator, auval. Sanitizers: ASan, UBSan, RTSan.
- **Cross-adapter null tests:** a device run natively in the DAW and the same device as our own VST3 must produce identical output.
- **CI** on Linux, Windows and macOS.
