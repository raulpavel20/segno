# Permanent identifiers

Identifiers stored in users' projects, presets and plugin scans **can never change after a public release** ([ADR 0008](decisions/0008-naming-and-trade-dress.md)). Display names can change at any time.

> **Placeholder phase.** The product name and organisation domain have not been chosen. The vendor-level values below (`fossdaw`, `Fdaw`) are placeholders and will be replaced **once**, before the first public build. Device and parameter IDs are not affected by that replacement.

## Vendor level (placeholders)

| What | Value | Rules |
|---|---|---|
| Vendor display name | `FossDAW` | Shown in hosts' plugin lists |
| Plugin manufacturer code (VST3/AU) | `Fdaw` | 4 characters, at least one uppercase |
| Reverse-DNS prefix (CLAP IDs, bundle IDs) | `org.fossdaw` | |
| LV2 URI prefix | `https://fossdaw.org/plugins/` | Must be a URI we control at release time |

## Device level (permanent once the SPEC is approved)

| Device | Device ID | Plugin code | CLAP ID | Status |
|---|---|---|---|---|
| Utility | `utility` | `Futl` | `org.fossdaw.utility` | M0 |
| Saturator | `saturator` | `Fsat` | `org.fossdaw.saturator` | M1 (spec draft) |
| EQ | `eq` | `Feq1` | `org.fossdaw.eq` | M1 (spec draft) |
| Subtractive Synth | `subtractive-synth` | `Fsub` | `org.fossdaw.subtractive-synth` | M1 (spec draft) |
| Sampler | `sampler` | `Fsmp` | `org.fossdaw.sampler` | M1 (spec draft) |
| Compressor | `compressor` | `Fcmp` | `org.fossdaw.compressor` | M2 (spec draft) |
| Gate | `gate` | `Fgat` | `org.fossdaw.gate` | M2 (spec draft) |
| Limiter | `limiter` | `Flim` | `org.fossdaw.limiter` | M2 (spec draft) |
| Spectrum | `spectrum` | `Fspc` | `org.fossdaw.spectrum` | M2 (spec draft) |

**Rules:**
- The device ID is kebab-case and describes the function. It is used in state JSON, the registry, CLAP and LV2 IDs, and file names.
- A plugin code is 4 characters with **exactly one uppercase letter**. Our scheme is `F` plus 3 lowercase letters or digits. Never reuse a code, even for a removed device.
- New devices are added to this table when their SPEC is approved.

## Panel section level

- A module panel's section ID (`shape`, `color`, `band`) is stored in projects and plugin state as view state ([ADR 0022](decisions/0022-module-panels.md)): lowercase, unique within the panel, listed in the device's `SPEC.md`.
- Don't rename a section ID; unknown IDs are ignored, so a renamed section just forgets whether it was collapsed. Titles are display names and can change.

## Parameter level

- A parameter ID is unique within its device: lowercase, dot-separated (`gain`, `band3.freq`).
- It is defined once in the device's `ParamSpec` table and listed in its `SPEC.md`.
- Never rename or reuse a parameter ID. To change a parameter's meaning or range in an incompatible way, add a new ID and migrate old state in the device's `stateVersion` migration.
- `sinceVersion` in the `ParamSpec` records the device state version in which the parameter was added. It feeds JUCE's parameter version hint.
