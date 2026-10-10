# Permanent identifiers

Identifiers stored in users' projects, presets and plugin scans **can never change after a public release** ([ADR 0008](decisions/0008-naming-and-trade-dress.md)). Display names can change at any time.

> **Named, not yet released.** The product is Segno ([ADR 0024](decisions/0024-product-name.md)). The vendor-level values below freeze at the first public release; before it, the domain `segnoaudio.org` must be registered and a trademark search done. Device and parameter IDs are not affected by the name.

## Vendor level

| What | Value | Rules |
|---|---|---|
| Vendor display name | `Segno` | Shown in hosts' plugin lists; plugins are named "Segno <Module>" |
| Plugin manufacturer code (VST3/AU) | `Sgno` | 4 characters, at least one uppercase |
| Reverse-DNS prefix (CLAP IDs, bundle IDs) | `org.segnoaudio` | The DAW app is `org.segnoaudio.segno` |
| LV2 URI prefix | `https://segnoaudio.org/plugins/` | Must be a URI we control at release time |
| Project file extension | `.segno` | |
| MCP server name and resource scheme | `segno`, `segno://` | Part of the MCP interface (ADR 0021) |

## Device level (permanent once the SPEC is approved)

| Device | Device ID | Plugin code | CLAP ID | Status |
|---|---|---|---|---|
| Utility | `utility` | `Futl` | `org.segnoaudio.utility` | M0 |
| Saturator | `saturator` | `Fsat` | `org.segnoaudio.saturator` | M1 (approved 2026-10-10) |
| EQ | `eq` | `Feq1` | `org.segnoaudio.eq` | M1 (approved 2026-10-10) |
| Subtractive Synth | `subtractive-synth` | `Fsub` | `org.segnoaudio.subtractive-synth` | M1 (approved 2026-10-10) |
| Sampler | `sampler` | `Fsmp` | `org.segnoaudio.sampler` | M1 (approved 2026-10-10) |
| Compressor | `compressor` | `Fcmp` | `org.segnoaudio.compressor` | M2 (approved 2026-10-09) |
| Gate | `gate` | `Fgat` | `org.segnoaudio.gate` | M2 (approved 2026-10-09) |
| Limiter | `limiter` | `Flim` | `org.segnoaudio.limiter` | M2 (approved 2026-10-09) |
| Spectrum | `spectrum` | `Fspc` | `org.segnoaudio.spectrum` | M2 (spec draft) |

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
