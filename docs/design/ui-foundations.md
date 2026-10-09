# UI foundations

Everything a view draws comes from a named token or a shared component; views never mix colours, pick fonts or place things by literal numbers ([ADR 0023](../decisions/0023-ui-foundations.md)). Module panels have their own layout kit on top of this ([device-panels.md](device-panels.md), [ADR 0022](../decisions/0022-module-panels.md)). The base palette's values are in [design-language.md](design-language.md).

Code: `modules/fd_ui/theme/` (tokens, roles, styles) and `modules/fd_ui/shell/` (components). The app's time views live in `app/src/ui/time/`, and the track header layout in `app/src/model/TrackHeaderLayout`.

## Colours by role

Roles are derived from the base palette in `Theme.cpp` (`deriveRoles`), the only place colours are mixed. Views use `theme.roles.<name>`.

| Role | Derived from (Steel) | Use |
|---|---|---|
| `lane` | surface0 → surface1, 45 % | Arrangement lanes |
| `laneSelected` | surface1 → surface2, 40 % | The selected track's lane |
| `subLane` | surface0 → surface1, 35 % | Lanes under a main lane: velocity, automation (C4) |
| `strip` | surface0 → surface1, 50 % | The loop strip above a ruler |
| `keyRowLight` / `keyRowDark` | surface1 → surface2 70 % / surface0 → surface1 30 % | Piano roll rows for white and black keys |
| `shade` | surface0 at 55 % | Outside a range: before and after a clip in the piano roll |
| `gridBar` / `gridBeat` / `gridSub` | surface3 at 100 / 55 / 28 % | Grid lines in every time view |
| `gridOctave` / `gridMinor` | line / surface3 at 35 % | Piano roll: under every C, between E and F |
| `selectionFill` / `selectionStroke` | active at 10 / 60 % | Box selection |
| `dropTarget` | active at 12 % | Where a dragged file will land |
| `textHighlight` | active at 35 % | Selected text in fields |
| `playhead` | textPrimary | The playhead in every time view |
| `marker` | textPrimary at 80 % | Live playheads inside a display (Sampler) |
| `loopInactive` | textTertiary at 70 % | The loop brace while looping is off |
| `onClip` | surface0 | Text on a clip's title strip and on notes |
| `keyWhite` / `keyBlack` / `keySeam` | textTertiary → textSecondary 25 % / surface0 / surface1 at 60 % | The piano roll keyboard |
| `curveFill` | value at 14 % | The flat fill under any curve |
| `selectionCurve` | active at 45 % | The selected band's own curve (EQ) |
| `spectrumFill` / `spectrumLine` / `spectrumPre` | textTertiary at 22 / 50 / 35 % | Analyzers |
| `outside` | textTertiary at 60 % | Data outside an active range (waveform outside Start–End) |
| `loopTint` | value at 10 % | A loop region inside a display |
| `focusRing` | active at 70 % | Keyboard focus, 1 px |
| `hoverWash` | surface3 at 35 % | Hover over a large target (a collapsed section) |
| `dropWell` | surface1, lifted 8 % | A well a file is dragged over |
| `valueHot` | value, lifted 15 % | A value arc or fill while hovered or dragged |
| `valueText` | textPrimary at 85 % | Knob values at rest |
| `warning` | meterHot at 80 % | Inline warnings ("No instrument") |
| `overlay` | surface3 at 90 % | Diagnostic overlays (frame statistics) |
| `automation` | effectStart | An automation lane's line and points (C4) |
| `automationFillTop` / `automationFillBottom` | effectStart at 40 % / effectEnd at 4 % | The reserved gradient under an automation curve, from the curve to the lane's floor (ui-principles §4) |
| `automationOff` / `automationOffFill` | effectStart at 40 % (`fadedAlpha`) / 6 % | A lane that's off (the parameter keeps its own value); an empty lane's dashed line |
| `shadow` | black at 50 % | The one shadow, under popovers |

Role functions for colours that depend on content:

| Function | Rule | Use |
|---|---|---|
| `clipBody(colour, selected)` | the clip colour at 32 % brightness (42 % selected) | Clip bodies |
| `clipContent(colour)` | the clip colour, 25 % towards white | Waveforms and notes drawn on clips |
| `noteFill(colour, velocity, selected)` | control tone → clip colour by velocity (35–100 %); selected 30 % towards white | Piano roll notes and velocity stems |
| `faded(colour)` | 40 % alpha | Content that exists but doesn't play (notes outside a clip) |
| `fill(colour)` | `curveFill`'s alpha in that colour | Fills under coloured curves |
| `lift(colour, hovered, pressed)` | +6 % hovered, +12 % pressed | Every hover and press state |

**States:** hover lifts by 6 % and press by 12 %. Dimmed (ignored or switched off) is 35 % alpha, never compounded. Focus is a 1 px `focusRing`. Selection is the `active` colour, or a `textPrimary` outline on content.

## Text styles

`ui::font(TextStyle::…)`. Only `modules/fd_ui` calls `fonts::font` or `fonts::mono` directly.

| Style | Font | Use |
|---|---|---|
| `Title` | Inter 12 semibold | Module and plugin names, popover titles |
| `Name` | Inter 12 medium | Track, clip and action names in lists |
| `Body` | Inter 12 | Messages, property values, empty states |
| `Label` | Inter 11 medium | Control labels (knob and bar names) |
| `Caption` | Inter 11 | Units, categories, header captions, hints |
| `Control` | Inter 11 semibold | Toggles, segments, combos, tabs |
| `Field` | Inter 13 | Text fields and menus |
| `Section` | Mono 10 semibold, all caps | Section titles, panel titles, sub-labels |
| `Value` | Mono 11 medium | Readouts: ruler numbers, times, property numbers, bar values |
| `ValueSmall` | Mono 10 | Knob values, display axis labels |
| `ValueMedium` | Mono 13 medium | Number fields (tempo, signature) |
| `ValueLarge` | Mono 15 medium | The transport position |
| `Micro` | Mono 9 semibold | Labels inside small graphics (note names, EQ node numbers) |
| `Wordmark` | Mono 15 bold, all caps, tracked | The product's name beside its mark ([ADR 0024](../decisions/0024-product-name.md)) |

## Motion and input

| Token | Value | Use |
|---|---|---|
| `motion.panel` | 40 ms time constant (about 150 ms to settle) | Panels opening and resizing |
| `motion.follow` | 50 ms | The view following the playhead |
| `motion.zoom` | 80 ms (the `Timeline` model's own constant) | Animated zoom |
| `motion.settle` | 150 ms | Detail that waits for motion to stop (exact-zoom waveforms) |
| `input.dragThreshold` | 3 px | A press becomes a drag |
| `input.edgeGrab` | 6 px (a third of the target at most) | Grabbing a clip's, note's or loop brace's edge |
| `input.wheelScroll` | 400 px per wheel unit | Scrolling |
| `input.wheelZoom` | 2× per wheel unit (Ctrl) | Zooming at the mouse |
| `input.rulerZoom` | 1.2 % per pixel dragged vertically | Zooming by dragging the ruler |

## Shell metrics

| Token | Value |
|---|---|
| `shell.transportHeight` | 40 |
| `shell.rulerHeight` / `shell.loopStripHeight` | 24 / 12 |
| `shell.trackHeaderWidth` | 180 |
| `shell.panelHeaderHeight` | 24 (detail panel tabs, side panel titles) |
| `shell.padding` | 12 (inside side panels and popovers) |
| `shell.rowHeight` | 20 (property rows, compact list rows) |
| `shell.propertyLabelWidth` | 80 |
| `shell.listRowHeight` | 28 (palette and browser results) |
| `shell.popoverWidth` | 560 |
| `shell.toastHeight` / `shell.toastWidth` | 36 / 420 |
| `shell.keyboardWidth` / `shell.velocityHeight` | 52 / 56 (piano roll) |
| `shell.railWidth` / `shell.splitter` | 44 / 5 |
| `shell.barPadding` / `shell.barInset` | 8 / 6 (a bar's sides; top and bottom) |
| `shell.itemGap` / `shell.groupGap` | 4 / 12 (between items of a group; between groups) |
| `shell.buttonSize` / `shell.fieldHeight` | 28 (icon buttons in bars and rails) / 24 (text fields) |

## Shared components (`modules/fd_ui/shell`)

| Component | What it is | Used by |
|---|---|---|
| `PanelHeader` | A 24 px header: a section-style title or tabs on the left, then context; on the right, its view options (`ViewOption`) and a caption | Detail panel (Modules / Clip tabs; a plain Modules title in the mixer view), inspector, browser, the mixer view (Modules, Sends, Narrow) |
| `AutomationState` / `paintAutomationIndicator` | Whether a control's parameter is automated: a 4 px disc in `automation`, filled while its lane plays and a ring while it's off (left of a knob's name; a bar's, fader's, toggle's or choice's top-left corner). `Knob` and `Toggle` take the state and an `onPopupMenu` (right-click) | Every control of a parameter (C4) |
| `ViewOption` | A view option in a panel header: a 6 px indicator and a `Control` label, no button fill. Off: a 1 px ring and `textTertiary`; on: a filled `active` disc and `textPrimary` (the shape tells on from off, not only the colour); hover: the `hoverWash` behind it | The mixer view's Modules, Sends and Narrow; later the piano roll's and the browser's view options |
| `PropertyList` | Label and value rows on the row grid; values in `Body`, or `Value` for numbers | Inspector; export options (C8); preferences |
| `ListRow` | One row of a list: optional dot, name, caption, shortcut on the right; hover and selected states | Command palette; browser (M3) |
| `Popover` | Floating chrome: `surface2`, panel radius, the one shadow | Command palette; Assistants popover; future menus that float |
| `Toast` | A message in the arrangement's corner that times out | Warnings and confirmations |
| `EmptyState` | A centred hint where there is nothing to show | Chain without a track, clip view without a clip, empty project, empty inspector |
| `SectionStack` | Titled sections one under another: a `Section`-style caps title on the row grid, the content under it, and a 1 px `line` centred in the group gap between neighbours (a module panel's sections, stacked) | The inspector ([selection.md](selection.md) §5); later the browser and preferences |
| `BarLayout` | Places a bar's items left and right on the item and group gaps | The transport bar |
| `OutsideClick` | Closes a popover on any mouse press outside it (other top-level windows, such as its popup menus, excepted); popovers also close on Escape | The command palette, the lane picker, the Assistants popover |
| `Brand` | The mark (`paintMark`), the mark and name (`paintBrand`), the app icon (`appIcon`) | The transport bar's left end; the app and window icon |

**Toggle or view option.** A `Toggle` switches something on or off in the project or a device: mute, solo, Pre, power, a setting. It is a control, with a fill. A `ViewOption` only changes how a view looks (view state, never an undo step) and lives in that view's panel header, where it stays quiet until hovered.

## Time views (`app/src/ui/time`)

Every view that shows time (the arrangement, the piano roll, automation lanes from C4, and later an audio clip editor) is built from these, over the `Timeline` model:

| Part | Does |
|---|---|
| `TimeRuler` | Bar numbers and ticks; optionally the loop strip with its brace, and a range strip (a clip's extent). Click moves the playhead (snapped, Alt for free); drag zooms vertically and scrolls horizontally. |
| `GridPainter` | Bar, beat and subdivision lines in the grid roles, over any rectangle. |
| `Playhead` | Draws the playhead line, and invalidates just its old and new strip each frame. |
| `BoxSelection` | Draws the rubber band in the selection roles. |
| `TimeInput` | One mapping for every time view: Ctrl+wheel or pinch zooms at the mouse; Shift+wheel and horizontal wheel scroll time; the plain wheel scrolls the view's other axis (tracks, pitches). |

## Track headers (`model::TrackHeaderLayout`)

A pure layout model: given the header's size, it says where each slot goes. The mixer (C3) and recording (C5) fill slots instead of placing controls.

| Slot | Size | Shown when |
|---|---|---|
| Colour | 3 px strip on the left edge | Always |
| Name | 16 px row (`Name`) | Always |
| Buttons (mute, solo, arm) | 20 × 20 each, 4 apart, right-aligned in the name row | Height ≥ 32 |
| Subtitle (instrument, "No instrument") | 14 px row (`Caption`) | Height ≥ 48 |
| Controls (volume and pan bars) | 20 px row | Height ≥ 80 |
| Meter | 8 px strip on the right edge, full height | When the mixer provides one (C3) |

Padding: 12 px left (after the colour strip), 6 px top and bottom, 8 px right.

**Automation lanes** (C4, [automation.md](automation.md) §3, ADR 0029) are rows of their own among the tracks, with the same kit: the lane's colour strip, its name, its on switch in the buttons slot, what it drives in the subtitle ("Bass · Synth", "3 parameters") with the value now on the right, and with one assignment that parameter's control as a bar in the controls slot (height ≥ 80). Lane headers sit on `subLane`. Any row is resized by dragging its bottom edge in the header column (Alt: every row of its kind); heights are view state, by row (`model::ArrangementRows`).

## Channel strips (`model::ChannelStripLayout`)

The mixer view's strips (C3c, [mixer.md](mixer.md) §6) are placed by a pure layout model like the track headers: given a strip's height and the view's options (module names, sends, narrow, the number of returns), it places every part on the 4 px grid. Rows a strip doesn't use keep their space, so every fader and meter starts at the same height. Strip width: 96 px (narrow: 64, without the dB scale or Pre switches).

The controls are shared (`ui/ChannelControls`): volume and send bars on the volume taper, the pan bar, mute, solo and Pre toggles, the meter, the typed volume, and the fader, which is the shared `Knob` in its Fader style (a thin track filled to a flat cap, `faderCapHeight`; its travel follows the mouse one to one).

## The UI lint

`ui_tokens_only` (a CTest check, label `fast`) fails when a file in `app/src/ui`, `modules/fd_device_ui` or `devices/*/ui` does any of these:
- calls `fonts::font` or `fonts::mono`;
- mixes colours (`interpolatedWith`, `withAlpha`, `brighter`, `darker`, `withBrightness`);
- uses literal colours (`juce::Colour(0x…)`, `juce::Colours::…`, except `transparentBlack`).

`modules/fd_ui` is where all of that lives. Module panel geometry is checked separately by the layout lint (`fd_panel_tests`).
