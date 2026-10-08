# Design language: "Steel"

The product owner chose this theme at the M0 design gate (2026-10-09). The rules live in [ui-principles.md](ui-principles.md) and [ADR 0014](../decisions/0014-flat-darker-ableton-adjacent.md); this document records the **values**. The code source of truth is `modules/fd_ui/theme/Theme.cpp`, and the two must match.

**Character:** flat, very dark, slightly cool neutral greys. Blue is used for values and for active states. Dense and compact, close to Ableton's visual grammar.

**Gate history**
- Round 1 rejected: gradients on panels and knob arcs read as 3D, and the result wasn't dark enough.
- Round 2 offered three flat candidates: Carbon (amber), Steel (blue) and Signal (lime). **Steel was chosen.**
- The alternatives remain in `tools/gallery` for comparison only.

## Colour tokens

### Surfaces and text

| Token | Value | Use |
|---|---|---|
| `surface0` | `#060709` | App background, arrangement canvas |
| `surface1` | `#0A0C0F` | Panels: browser, detail, inspector, top bar |
| `surface2` | `#101317` | Device bodies, cards |
| `surface3` | `#1A1E24` | Controls (knob tracks, off buttons), hover |
| `line` | `#1F242B` | The rare 1 px divider |
| `textPrimary` | `#E4E6E9` | Names, values |
| `textSecondary` | `#9A9EA6` | Labels |
| `textTertiary` | `#5D6168` | Hints, disabled, ruler numbers |

### Roles

| Token | Value | Use |
|---|---|---|
| `value` | `#7DB0FF` | Knob arcs, slider and fader fills, curve lines |
| `active` | `#4E95F5` | On states, device power, selection |
| `onActive` | `#050D1A` | Text and icons on an `active` fill |
| `effectStart` → `effectEnd` | `#4E95F5` → `#9A78F0` | **The only gradient pair:** automation and modulation only |
| `accentInstrument` | `#E9B44C` | Instrument tag |
| `accentMidi` | `#A88BEB` | MIDI tag |
| `accentModulation` | `#4FD1A5` | Modulation tag |
| `record` | `#F0505A` | Record, alerts |

### Meter zones (flat)

| Token | Value | Range |
|---|---|---|
| `meterNormal` | `#45C97A` | below −6 dBFS |
| `meterHot` | `#E8C547` | −6 to 0 dBFS |
| `meterClip` | `#F0505A` | above 0 dBFS |

### Clip and track colours (flat; first 8 of the palette)

`#D9824A` · `#D4B04A` · `#7DBA5A` · `#46B3A3` · `#4F8FD6` · `#8E74D6` · `#CC6699` · `#C95454`

A clip is drawn with:
- a solid title strip in the clip colour, with dark text;
- a body in the same hue at about 32 % brightness;
- the waveform or notes in a lighter tint of the clip colour.

## Geometry and type

| Token | Value |
|---|---|
| Spacing grid | 4 px |
| Control radius | 2.5 px |
| Panel radius | 4 px |
| Device panel height | 168 px (24 px header) |
| Knob | ≤ 38 px arc diameter, 2.5 px arc, 1.5 px needle |
| Value bar | Knob in bar style for dense rows (mod amounts, voice counts): `surface3` bar, `value` fill at 45 % from the origin, name left and value right inside; same interaction as a knob, also follows horizontal drags |
| Font | Inter (variable), bundled |
| Type scale | 11 · 12 · 13 · 15 · 18 px; tabular numerals for values |

## Still open

- **Light theme:** a later milestone (M7). It is built from the same token names.
- **Full clip palette:** 16–24 hues, defined when the arrangement view is built (M1).
- **Icon set:** chosen with the first icons (M1).
