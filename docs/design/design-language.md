# Design language: "Steel"

The product owner chose this theme at the M0 design gate (2026-10-09). The rules live in [ui-principles.md](ui-principles.md) and [ADR 0014](../decisions/0014-flat-darker-ableton-adjacent.md); this document records the **values**. The code source of truth is `modules/fd_ui/theme/Theme.cpp`, and the two must match.

**Character:** flat, very dark, neutral greys (no tint). Blue is used for values and for active states. A mono typeface for every number and readout gives the instrument-panel feel. Dense and compact, close to Ableton's visual grammar.

**Gate history**
- Round 1 rejected: gradients on panels and knob arcs read as 3D, and the result wasn't dark enough.
- Round 2 offered three flat candidates: Carbon (amber), Steel (blue) and Signal (lime). **Steel was chosen.**
- The alternatives remain in `tools/gallery` for comparison only.
- 2026-10-09, product owner: greys made neutral (they had a blue tint), and a bundled mono font added for values and readouts.

## Colour tokens

### Surfaces and text

| Token | Value | Use |
|---|---|---|
| `surface0` | `#070707` | App background, arrangement canvas |
| `surface1` | `#0C0C0C` | Panels: browser, detail, inspector, top bar |
| `surface2` | `#131313` | Device bodies, cards |
| `surface3` | `#1E1E1E` | Controls (knob tracks, off buttons), hover |
| `line` | `#232323` | The rare 1 px divider |
| `textPrimary` | `#E6E6E6` | Names, values |
| `textSecondary` | `#9E9E9E` | Labels |
| `textTertiary` | `#616161` | Hints, disabled, ruler numbers |

All greys are neutral (equal red, green and blue); colour comes only from the roles below and from clip colours.

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

### Clip and track colours (flat, 16 hues)

Equal perceptual lightness and chroma (OKLCH L 0.70, C 0.115), hues every ~22°, so no clip is louder than another. New tracks step through the palette three hues at a time, so neighbours differ.

`#DD8273` · `#D78958` · `#C89344` · `#B19F42` · `#92AA54` · `#6FB171` · `#4CB58C` · `#29B5A8` · `#1DB2C0` · `#3AADD4` · `#5CA5E1` · `#809BE6` · `#9D92E1` · `#B689D2` · `#CA82BB` · `#D67FA2`

A clip is drawn with:
- a solid title strip in the clip colour, with dark text;
- a body in the same hue at about 32 % brightness;
- the waveform or notes in a lighter tint of the clip colour.

Colour roles (lanes, grid, selection, states …), text styles, motion and shell metrics derived from these values are in [ui-foundations.md](ui-foundations.md).

## Geometry and type

| Token | Value |
|---|---|
| Spacing grid | 4 px |
| Control radius | 2.5 px |
| Panel radius | 4 px |
| Module panel height | 232 px (28 px header); the layout grid is in [device-panels.md](device-panels.md) |
| Module grid | Columns 48 px, 4 px apart; rows 20 px, 8 px apart; sections padded 12 px, separated by a 1 px `line` |
| Knob | Always 48×76: name 16, arc area 44 (38 px arc, 2.5 px track, 1.5 px needle), value 16 |
| Line controls | Toggles, combos, bars, segmented rows: 20 px tall |
| Value bar | Knob in bar style for dense rows (mod amounts, voice counts): `surface3` bar, `value` fill at 45 % from the origin, name left and value right inside; same interaction as a knob, also follows horizontal drags |
| Fonts | Inter (variable) for names and text; JetBrains Mono (variable) for values, numbers and readouts. Both bundled, both OFL-1.1 |
| Type scale | 11 · 12 · 13 · 15 · 18 px |

### Type

- **Mono (`fonts::mono`)** for every value, number and readout: knob and field values, the transport (position, time, tempo, signature, CPU), ruler bar numbers, the inspector's numbers, note names, the velocity read-out, shortcut hints in the command palette. Fixed width, so changing numbers never jitter.
- **Mono, all caps, 10 px semibold** for tiny section labels (INSTRUMENTS, INSPECTOR, AMP, OSC 2), with no extra letter spacing (the mono is spaced already).
- **Sans (`fonts::font`)** for everything people name or read: track, clip, device and preset names, parameter names, buttons, menus, messages.
- **Knob values are one size: 10 px mono.** A knob whose widest possible value doesn't fit its 48 px column drops the space before the unit (`2.00kHz`); only if that still doesn't fit does it shrink, to no less than 9 px. Decided once per knob, so the size never changes while the value does.

### Icons

Line icons drawn in code (`ui::Icon`, `modules/fd_ui/controls/Icons.cpp`): 1.5 px stroke on a 16 px grid. Transport glyphs (play, stop, record) are filled. Every icon has a tooltip with its shortcut.
- **One icon, one thing, one size where it lists things:** the browser's parts (Favourites' star, Instruments' note, Audio effects' knob, Racks' two stacked modules, Plugins' plug, Places' folder) show the same icon before their names in the tree and on the rail's buttons, both at `shell.listIconSize`, as a list's disclosures and stars are; a search field shows the magnifier at its left (ADR 0059). An icon button may draw its icon at a size of its own (`IconButton::setIconSize`) where it stands for something listed beside it.

### Brand

The product is Segno ([ADR 0024](../decisions/0024-product-name.md)). Code: `modules/fd_ui/shell/Brand`.
- **Mark:** the segno sign (𝄋), drawn in code like the icons: a monoline S of two bowls joined by a diagonal spine, a slash across the spine, a dot on either side. 1.6 px stroke on the 16 px grid, round caps, in `active`.
- **Wordmark:** the mark, then "SEGNO" in the mono, bold, all caps, with extra letter spacing (text style `Wordmark`, 15 px). Its only place in the DAW is the top left of the transport bar, as tall as the bar's buttons.
- **App icon:** the mark in `active` on a `surface2` rounded square with a `line` edge (`fd-snapshot --app-icon`).
- The mark is never redrawn by hand, recoloured per view or used as a button.

## Still open

- **Light theme:** a later milestone (M7). It is built from the same token names.
- **Font licences:** both fonts are OFL-1.1 and are compiled into the app and the plugins; their licence texts must ship with every build (M7 packaging, third-party notices).
