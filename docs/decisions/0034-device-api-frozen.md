# 0034. The device API and the device state format are frozen

- **Status:** Accepted (product owner, 2026-10-10, with the M1 sign-off)
- **Date:** 2026-10-10
- **Builds on:** [0006](0006-juce-free-core-and-module-boundaries.md) (one JUCE-free device API, two adapters), [0016](0016-constant-device-latency.md) (latency), [0017](0017-heavy-data-and-non-parameter-state.md) (heavy data and extra state), [0020](0020-latency-follows-lookahead.md) (latency follows lookahead), [0025](0025-sidechain-routing.md) (sidechains)

## Context
The roadmap freezes the framework at the end of M1: from then on, changing the device API or the state format needs an ADR and a migration path. That is what lets devices be written in parallel and lets saved projects and presets keep working.

M1 was signed off on 2026-10-10, together with everything M2 had built by then. By that date the API had already grown, additively, to cover M2's dynamics devices: `ParamFlags::ChangesLatency` (ADR 0020), `InfinityAtMax`, `ParamSpec::description` (open question B22), named meters (`DeviceInfo::meters`), the sidechain bus in both hosts (ADR 0025) and display commands on the analysis bus. Each was added so that existing devices and saved states were untouched. The freeze takes the API as it is on that date.

Two later milestones are expected to extend it: M4's modulators and racks, and M5's MPE (per-note expression). They should fit as additions; if one can't, it brings its own ADR.

## Decision
1. **Frozen:** the public headers of `libs/device` and the contracts they document:
   - `Device`: `prepare`, `reset` (deterministic), `process` (real-time safe), `latencySamples`, `tailSeconds`, `writeExtraState` / `readExtraState`, `migrateState`;
   - `DeviceInfo`, `DeviceKind`, `BusConfig` (main, sidechain and MIDI buses), `MeterSpec` and `MeterKind`;
   - `ProcessContext`, `ProcessSpec`, `TransportInfo` and `AudioBusView`, including in-place processing (input and output may alias);
   - `Event`, `EventType` and `EventList`;
   - `ParamSpec` and what its fields mean: plain values, the skew mapping to and from normalised values, units, smoothing, `ParamFlags`, `sinceVersion`, `description`;
   - `ParamStore`, `SmoothedParam`, `AnalysisBus` and `SampleFifo`, `AssetHandoff`, `VoiceManager`, the registry;
   - the host binding contract (`ui::ParamBinding`): devices never write their own parameters, and extra-state edits go through `applyExtraState`.
2. **Frozen: the device state format,** shared by plugin state, DAW projects and presets: `{device, stateVersion, params, extra}` with plain values. A missing parameter takes its default, an unknown one is ignored, older states are migrated by `migrateState`, and a state from a newer build is refused.
3. **Allowed without an ADR: additions** that keep every existing device compiling unchanged and every saved state loading to the same sound. Examples: a new virtual whose default does nothing, a new flag, unit, meter kind or event type, a new field with a default. Devices ignore event types they don't know. Each addition is listed in `docs/architecture.md` ("Device contract") in the same change.
4. **Needs an ADR and a migration path:** removing anything above, changing what it means, or any change that makes an existing device stop compiling or a saved state sound different.
5. **Per device,** the rules already in place stay: IDs are permanent once the SPEC is approved (`docs/ids.md`). A new parameter raises the device's `stateVersion` and carries `sinceVersion`. A range or meaning change comes with a `migrateState` step and a test that loads the old state.
6. **Not frozen:** the hosts' internals (the plugin adapter, the DAW adapter, the Session), the panel kit and UI foundations (ADRs 0022 and 0023, under their own lints), the testkit and the DSP library. They change freely, as long as devices and saved states keep working.

## Consequences
- Devices can be written against a stable API; M3's eight devices don't wait on framework changes.
- Projects and presets saved from now on keep loading: the conformance suite's state round trips and each device's migration tests guard this.
- M4 (modulation, racks) and M5 (MPE) design their API additions to fit rule 3, or propose an ADR.
- `docs/architecture.md` lists the frozen contract and is updated with each addition.
