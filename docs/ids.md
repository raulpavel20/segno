# Permanent identifiers

Identifiers stored in users' projects, presets and plugin scans **can never change after a public release** ([ADR 0008](decisions/0008-naming-and-trade-dress.md)). Display names can change at any time.

> **Named, not yet released.** The product is Segno ([ADR 0024](decisions/0024-product-name.md)). The vendor-level values below freeze at the first public release; before it, the domain `segnoaudio.org` must be registered and a trademark search done. Device and parameter IDs are not affected by the name.

## Vendor level

| What | Value | Rules |
|---|---|---|
| Vendor display name | `Segno` | Shown in hosts' plugin lists; plugins are named "Segno <Module>". Their files are named the same, "&" written "and" ("Segno Phaser and Flanger.vst3") |
| Plugin manufacturer code (VST3/AU) | `Sgno` | 4 characters, at least one uppercase |
| Reverse-DNS prefix (CLAP IDs, bundle IDs) | `org.segnoaudio` | The DAW app is `org.segnoaudio.segno` |
| LV2 URI prefix | `https://segnoaudio.org/plugins/` | Must be a URI we control at release time |
| Project file extension | `.segno` | |
| Preset file extension | `.segnopreset` | [ADR 0044](decisions/0044-presets-are-files.md); the file's format version is its `segnoPreset` key |
| Rack file extension | `.segnorack` | [ADR 0056](decisions/0056-racks-are-modules-of-modules.md); JSON, its format version its `segnoRack` key; modules referred to by chain and place, never by a project's ids |
| User folder | `Documents/Segno` (`Presets/`, `Racks/`, `Unsaved/`) | Presets are found by the device named inside them, not by their subfolder |
| MCP server name and resource scheme | `segno`, `segno://` | Part of the MCP interface (ADR 0021) |

## Device level (permanent once the SPEC is approved)

| Device | Device ID | Plugin code | CLAP ID | Status |
|---|---|---|---|---|
| Utility | `utility` | `Futl` | `org.segnoaudio.utility` | M0 |
| Saturator | `saturator` | `Fsat` | `org.segnoaudio.saturator` | M1 (approved 2026-10-10) |
| EQ | `eq` | `Feq1` | `org.segnoaudio.eq` | M1 (approved 2026-10-10) |
| Subtractive | `subtractive-synth` | `Fsub` | `org.segnoaudio.subtractive-synth` | M1 (approved 2026-10-10) |
| Sampler | `sampler` | `Fsmp` | `org.segnoaudio.sampler` | M1 (approved 2026-10-10) |
| Compressor | `compressor` | `Fcmp` | `org.segnoaudio.compressor` | M2 (approved 2026-10-09) |
| Gate | `gate` | `Fgat` | `org.segnoaudio.gate` | M2 (approved 2026-10-09) |
| Limiter | `limiter` | `Flim` | `org.segnoaudio.limiter` | M2 (approved 2026-10-09) |
| Spectrum | `spectrum` | `Fspc` | `org.segnoaudio.spectrum` | M2 (approved 2026-10-11) |
| Delay | `delay` | `Fdly` | `org.segnoaudio.delay` | M3 (approved 2026-10-12) |
| Chorus | `chorus` | `Fchr` | `org.segnoaudio.chorus` | M3 (approved 2026-10-12) |
| Phaser & Flanger | `phaser-flanger` | `Fphf` | `org.segnoaudio.phaser-flanger` | M3 (approved 2026-10-12) |
| Auto Pan & Tremolo | `auto-pan-tremolo` | `Fapt` | `org.segnoaudio.auto-pan-tremolo` | M3 (approved 2026-10-12) |
| Reverb | `reverb` | `Frvb` | `org.segnoaudio.reverb` | M3 (approved 2026-10-12) |
| Filter | `filter` | `Fflt` | `org.segnoaudio.filter` | M3 (approved 2026-10-12) |
| Loudness Meter | `loudness-meter` | `Flud` | `org.segnoaudio.loudness-meter` | M3 (approved 2026-10-13) |
| Tuner | `tuner` | `Ftun` | `org.segnoaudio.tuner` | M3 (approved 2026-10-15) |
| Wavetable | `wavetable` | `Fwav` | `org.segnoaudio.wavetable` | M3 P1 (approved 2026-10-13) |

**Rules:**
- The device ID is kebab-case and describes the function. It is used in state JSON, the registry, CLAP and LV2 IDs, and file names.
- A plugin code is 4 characters with **exactly one uppercase letter**. Our scheme is `F` plus 3 lowercase letters or digits. Never reuse a code, even for a removed device.
- New devices are added to this table when their SPEC is approved.

## Panel section level

- A module panel's section ID (`shape`, `color`, `band`) is stored in projects and plugin state as view state ([ADR 0022](decisions/0022-module-panels.md)): lowercase, unique within the panel, listed in the device's `SPEC.md`.
- Don't rename a section ID; unknown IDs are ignored, so a renamed section just forgets whether it was collapsed. Titles are display names and can change.

## Racks ([racks.md](design/racks.md), ADR 0056; approved 2026-10-14, permanent)

- A rack is a module of type `rack` (`DeviceSlot::deviceId`, MCP's `type`). Its macros are its parameters `macro1` … `macro8`, 0 to 100 %, automated as `m<rack id>/macro<n>`; then `morph`, 0 to 100 % (step 2, racks.md §18.3, approved 2026-10-14), automated as `m<rack id>/morph`.
- A rack panel's sections (view state): `macros`, `snapshots`, `morph`, `chains`.
- A module's parameter is kept in a project as a property of its plugin's state named as its id; one whose id tracktion uses for a plugin's own property (`type`, `id`, `enabled`, `process`, `frozen`, `customName`, `quickParamName`, `windowX`, `windowY`, `windowLocked`, `sidechainSourceID`, `masterPluginID`) as `fdParam.<id>` (the Filter's `type`: `fdParam.type`).
- In a project, on the rack's type (step 2): its snapshots (`fdSnapshots`, JSON: each a name and eight positions), the current one (`fdSnapshot`), Morph's on, A and B (`fdMorphOn`, `fdMorphA`, `fdMorphB`); Morph is the macro parameter marked `fdMorph`; a macro's lock, `fdLocked` on its state. In a `.segnorack` file: `snapshots`, `morph` (`on`, `a`, `b`, `position`) and `locked` on a macro, each left out while it has nothing to say.
- In a project, a rack's chains are kept on its modules' places in tracktion's rack (`fdChain`, `fdChainMixer`, `fdChainName`, `fdChainOn`, `fdChainSolo`) and its shown chain on the rack (`fdShownChain`).

## Modulators ([modulators.md](design/modulators.md), ADR 0061; approved 2026-10-15, permanent)

- A modulator is a module of one of the types `mod-lfo`, `mod-random`, `mod-envelope`, `mod-follower`, `mod-steps` (`DeviceSlot::deviceId`, MCP's `type`). The `mod-` prefix keeps them apart from device IDs.
- Its parameter IDs follow the parameter rules below and are listed here as they're built.

## Parameter level

- A parameter ID is unique within its device: lowercase, dot-separated (`gain`, `band3.freq`).
- It is defined once in the device's `ParamSpec` table and listed in its `SPEC.md`.
- Never rename or reuse a parameter ID. To change a parameter's meaning or range in an incompatible way, add a new ID and migrate old state in the device's `stateVersion` migration.
- `sinceVersion` in the `ParamSpec` records the device state version in which the parameter was added. It feeds JUCE's parameter version hint.

## Scale level ([key-and-scale.md](design/key-and-scale.md) §5; approved 2026-10-14, permanent)

- A scale ID is stored in projects (the song's key) and read by assistants (`set_key`, `list_scales`): lowercase kebab-case, the scale's usual name. Names and short names are display text and can change.
- Never rename or reuse a scale ID. A scale added later gets a new ID; a project with an ID this build doesn't know opens with no key.

| Group | IDs |
|---|---|
| Common | `major`, `minor`, `harmonic-minor`, `melodic-minor`, `major-pentatonic`, `minor-pentatonic`, `blues`, `major-blues` |
| Modes | `dorian`, `phrygian`, `lydian`, `mixolydian`, `locrian` |
| More colours | `harmonic-major`, `dorian-sharp-4`, `phrygian-dominant`, `lydian-augmented`, `lydian-dominant`, `super-locrian`, `hungarian-minor`, `double-harmonic`, `spanish-8-tone` |
| Symmetric | `whole-tone`, `diminished-half-whole`, `diminished-whole-half`, `messiaen-3`, `messiaen-4`, `messiaen-5`, `messiaen-6`, `messiaen-7` |
| Japanese and Indonesian | `hirajoshi`, `in-sen`, `iwato`, `kumoi`, `pelog-selisir`, `pelog-tembung` |
