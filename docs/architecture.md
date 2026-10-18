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
libs/dsp        DSP building blocks: filters, oscillators, oversampling (linear phase, and minimum
                phase for a few samples' latency; M3 B6), ADAA shapers,
                smoothing, the dynamics core (gain curves, detection, attack/release, lookahead,
                true peak, BS.1770 loudness; M2 A1), the modulation core (a delay line read
                between samples while it moves, a feedback path, LFOs locked to the song; M3 A2),
                the spectral engine (a response that changes frame by frame with no phase change,
                and per-frequency dynamics; the EQ's linear phase and spectral bands, M3 B8)
                (+ reused libraries, see ADR 0004)
libs/music      music theory shared by the DAW, the Push support and devices (ADR 0057): the
                scale catalogue, keys and their spelling, finding a key from notes
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
packaging/linux        the launcher entry and the icon at every size (made with
                       fd-snapshot --app-icon); installed with the app by
                       cmake --install <build> --component Segno, or for a build of yours by
                       tools/install-desktop-entry.sh
```

## Device contract (key points)

The contract is frozen since M1's sign-off ([ADR 0034](decisions/0034-device-api-frozen.md)): additions that leave existing devices and saved states unchanged are listed here as they land; anything else needs an ADR and a migration.

- **Parameters** are declared in a static table with fields: permanent ID, range, default, skew, unit, smoothing, flags. Both adapters build their host parameters from this table, so automation, undo and presets work identically in both.
- **`process()`** runs on the audio thread and is `noexcept`. It never allocates, locks or does I/O. This is enforced by an allocation hook in the tests and by RealtimeSanitizer.
- **Control-rate updates** happen on a grid aligned to absolute sample position. Output is therefore identical whatever the host's block size, and the tests check this bit for bit.
- **Large data** (samples, wavetables) is prepared off the audio thread and swapped in lock-free with `AssetHandoff`. UI edits to non-parameter state go through `ParamBinding::applyExtraState` ([ADR 0017](decisions/0017-heavy-data-and-non-parameter-state.md)).
- **State and presets** use one JSON format, `{device, stateVersion, params, extra}`. It stores plain (not normalised) values and has versioned migrations. The plugin and the DAW share it, so presets move between them.
- **The UI never computes its own DSP.** Curves and displays call the same pure functions the audio path uses, and live data reaches the UI through a lock-free analysis bus (meters, spectra, playheads). The bus also carries display commands back (resetting a loudness meter): never state, which only changes through the host binding. A device names the slots its meters use (`DeviceInfo::meters`: id, name, kind), so hosts can read them by name (the MCP `measure_module` tool); peak slots have a second reader for that, so a measurement never takes the peaks a panel shows.
- **Devices never write their own parameters.** All edits go through the host binding, so automation and undo always work.
- **Sidechains** are host routing ([ADR 0025](decisions/0025-sidechain-routing.md)). A device declares a sidechain bus and reads `ProcessContext::sidechain`, which is empty when nothing is connected; it then listens to its input. The plugin adapter exposes the bus as an inactive auxiliary input and feeds it only when the header's picker says External; the DAW adapter lists the sidechain channels after the main ones, which is how tracktion routes a source track to it.
- **Presets** ([ADR 0044](decisions/0044-presets-are-files.md)) are files of the state format (`fd/device/Presets.h`: the file, `PresetLibrary`, `PresetController`, JUCE-free). A device names the extra-state keys that belong to the session, not the sound (`DeviceInfo::sessionExtraKeys`, added 2026-10-11): presets leave them out and loading one leaves them alone. Each host implements `PresetHost` (its state, its memo of which preset and A/B, one edit per load); factory presets are generated from `devices/<name>/presets/` into each device's library.
- **Voices** come from `VoiceManager`. Its `setMode` and `setVoiceCount` with a listener (added 2026-10-11) release the voices a change of mode or a lower count leaves out, so no held note sticks; instruments call those from their parameter tick.
- **Knobs set by a rack's macro** (added 2026-10-14, [racks.md](design/racks.md) §4): `ui::ParamBinding::macroMapping(index)` says a parameter is set by a macro (its name, its range From and To, and whether the pointer is showing another macro's), and `addMacroItems` adds Map to macro and Unmap to a control's menu. In the DAW only; elsewhere they say nothing. A mapped knob shows its range as a ring and follows (`ui::Knob::setFollowing`): it isn't dragged, wheeled, typed into or reset. While a macro is dragged by its name, `macroDrag` says which knobs could take it and which is under the pointer.
- **A panel's action as one undo step** (added 2026-10-13): `ui::ParamBinding::beginEdit(name)` names the undo step for the edits that follow, when a panel changes several parameters at once (the EQ's Make room). The DAW makes it one step; plugin hosts group their own undo, so it does nothing there.
- **Transient settings** (added 2026-10-13, for the EQ's Listen and Delta): `ParamFlags::Transient` marks how you listen rather than how a device sounds. Hosts never save it, presets leave it out, applying a state puts it back to its default, and the DAW makes no undo step for it. Never automatable (the conformance suite checks).
- **Loudness** (added 2026-10-13, for the Loudness Meter): units `Unit::Lufs` and `Unit::Dbtp` for parameters, and meter kinds `MeterKind::LoudnessUnits` (a range or difference in LU) and `MeterKind::Correlation` (−1 to 1), each NaN when there's no reading.
- **Pitch** (added 2026-10-15, for the Tuner): meter kinds `MeterKind::Hertz` (a frequency), `MeterKind::Pitch` (a MIDI note number; `measure_module` names it, C3 = 60), `MeterKind::Cents` and `MeterKind::Ratio` (0 to 1), each NaN when there's no reading.
- **The song's key** (added 2026-10-14, [ADR 0057](decisions/0057-the-song-has-one-key.md)): `TransportInfo::key`, a `KeyInfo` with the root (0–11, or −1 for no key) and the key's notes as a 12-bit set, at the block's start. The DAW fills it from a lock-free copy each module keeps of the project's key; the plugin adapter leaves it unknown (no plugin format carries one). A device that follows the key works without one. Names, spelling and scales come from `libs/music`.
- **MIDI effects** (added 2026-10-18, [ADR 0074](decisions/0074-midi-effects-know-the-song.md), [midi-effects.md](design/midi-effects.md) §20): `DeviceKind::MidiEffect` and `ProcessContext::outEvents`, reserved at M1, are hosted. A MIDI effect is given notes and no audio channels; its `outEvents` replace the notes it was given, and the track's audio passes it untouched. It writes its notes through a `NoteLedger` (`fd/device/NoteLedger.h`), which ends them on an all-notes-off: the DAW's adapter hands one over when the song stops, as tracktion does on a jump. Two additions to `ProcessContext`, null where the host has none (any plugin host):
  - `song`: a read-only `SongView` (`fd/device/SongView.h`): the harmony read from the song's notes, the sections, and this track's clips and their notes ahead, handed over whole off the audio thread;
  - `listen`: the notes of the track the module listens to, this block (the note-sources tracktion patch).

  `host::toMidiMessage` turns events back into MIDI for both adapters.

  The suite's MIDI effects share `devices/midi-shared/dsp` (JUCE-free; §22 of the design): `NoteEffect`, the base (a clock, its own beats while the song is stopped, its roll, notes shared by owners and written through the ledger), the `Follower` (the key and the chord now, from the song, a listened track or the notes below the split) and `Moments` (a block's chord changes and notes in time order). Their theory is `libs/music`'s (`Following.h`, `Arpeggio.h`), pure, for the clip view's tools and assistants too. As plugins they have no audio bus (`KIND midi`).
- **Latency** is constant for a device instance ([ADR 0016](decisions/0016-constant-device-latency.md)), except for settings marked `ParamFlags::ChangesLatency` (lookahead, [ADR 0020](decisions/0020-latency-follows-lookahead.md)), which hosts learn of on the message thread.

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
- **One place for each piece of view state** (added 2026-10-14, after the browser's rail opened a second, unseen "open" of the panel): whether a panel, popover or view shows is kept once, by its owner, and changed through one function (the shell's side panels: `Shell::setPanel`). The menu's tick, the title bar's buttons, keyboard shortcuts, the panel's own controls and the layout all read that state; none keeps a copy, and no second flag says the same thing a different way. A new way of showing something (a rail, a drag, an assistant) calls the owner's function rather than setting a flag of its own.

## Verification

Claude does most of the implementation but cannot hear audio or feel the UI. Verification is therefore automated and numeric, with human sign-off as the final gate ([ADR 0007](decisions/0007-verification-without-ears.md)):

- DSP unit tests, and a **conformance suite** that runs automatically on every device: block-size invariance, sample-rate sweep, silence, parameter fuzzing, click detection, state round-trip, latency, zero allocations, voice handling.
- **Golden metrics** per device: frequency response, harmonic content, aliasing, envelope timing. Tolerances are tuned per platform.
- **Plots**, and **A/B gap analysis against Live** once a Live machine is available, which turn "sounds different" into measured differences. Manual testing is currently Linux-only ([dev-environment.md](dev-environment.md)).
- **UI snapshots** at 1× and 2× scale, and paint-time benchmarks.
- **Plugin validators:** pluginval, clap-validator, auval. Sanitizers: ASan, UBSan, RTSan.
- **Cross-adapter null tests:** a device run natively in the DAW and the same device as our own VST3 must produce identical output.
- **CI** on Linux, Windows and macOS.
