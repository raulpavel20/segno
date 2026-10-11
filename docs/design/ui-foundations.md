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
| `takeUnused` / `takeUnusedContent` | surface2 / textTertiary 35 % towards surface2 | The audio of a clip's takes that it doesn't play, on their strips: no clip colour (recording.md §6) |
| `keyWhite` / `keyBlack` / `keySeam` | textTertiary → textSecondary 25 % / surface0 / surface1 at 60 % | The piano roll keyboard |
| `curveFill` | value at 14 % | The flat fill under any curve |
| `selectionCurve` | active at 45 % | The selected band's own curve (EQ) |
| `spectrumFill` / `spectrumLine` / `spectrumPre` | textTertiary at 22 / 50 / 35 % | Analyzers |
| `spectrumCompare` / `spectrumReference` | Clip palette hues 9 (blue) and 13 (violet), the same in every theme | The Spectrum's compared track and reference curve |
| `spectrumSnapshot` / `spectrumSide` / `spectrumCollision` | textSecondary; textSecondary at 50 %; meterHot at `fillAlpha` | The Spectrum's snapshot (dashed), side (dotted) and collision wash |
| `outside` | textTertiary at 60 % | Data outside an active range (waveform outside Start–End) |
| `loopTint` | value at 10 % | A loop region inside a display |
| `focusRing` | active at 70 % | Keyboard focus, 1 px |
| `hoverWash` | surface3 at 35 % | Hover over a large target (a collapsed section) |
| `dropWell` | surface1, lifted 8 % | A well a file is dragged over |
| `valueHot` | value, lifted 15 % | A value arc or fill while hovered or dragged |
| `valueText` | textPrimary at 85 % | Knob values at rest |
| `warning` | meterHot at 80 % | Inline warnings ("No instrument") |
| `overlay` | surface3 at 90 % | Diagnostic overlays (frame statistics) |
| `automation` | effectStart | The automation indicator on controls (C4) |
| `shadow` | black at 50 % | The one shadow, under popovers |

Role functions for colours that depend on content:

| Function | Rule | Use |
|---|---|---|
| `clipBody(colour, selected)` | the clip colour at 32 % brightness (42 % selected) | Clip bodies |
| `clipContent(colour)` | the clip colour, 25 % towards white | Waveforms and notes drawn on clips |
| `clipDimmed(colour)` | the clip colour, 60 % towards `subLane` | Muted clips |
| `noteFill(colour, velocity, selected)` | control tone → clip colour by velocity (35–100 %); selected 30 % towards white | Piano roll notes and velocity stems |
| `faded(colour)` | 40 % alpha | Content that exists but doesn't play (notes outside a clip) |
| `fill(colour)` | `curveFill`'s alpha in that colour | Fills under coloured curves |
| `lift(colour, hovered, pressed)` | +6 % hovered, +12 % pressed | Every hover and press state |
| `automationLine(colour, on)` | the lane's colour; `faded` while off | An automation lane's line and points, and an empty lane's dashed line (C4) |
| `automationFillTop(colour, on)` / `automationFillBottom(colour, on)` | the lane's colour at 20 % / 0 %; a flat 5 % while off | The reserved gradient under a lane's curve, from the line to the lane's floor (ui-principles §4) |

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
| `motion.progressDelay` | 250 ms | Work done sooner shows no progress, so quick bounces don't flicker |
| `input.bendTravel` | 60 px | Dragged for a whole bend: automation segments and fades alike |
| `input.dragThreshold` | 3 px | A press becomes a drag |
| `input.edgeGrab` | 6 px (a third of the target at most) | Grabbing a clip's, note's or loop brace's edge |
| `input.wheelScroll` | 400 px per wheel unit | Scrolling |
| `input.wheelZoom` | 2× per wheel unit (Ctrl) | Zooming at the mouse |
| `input.rulerZoom` | 1.2 % per pixel dragged vertically | Zooming by dragging the ruler |

## Shell metrics

| Token | Value |
|---|---|
| `shell.transportHeight` | 40 |
| `shell.titleBarHeight` | 28 (the title bar on Linux and Windows: the brand, the menus, the name, the window buttons; ADR 0040) |
| `shell.windowButtonWidth` | 40 (a window button, as tall as the title bar) |
| `shell.resizeBorder` | 4 (Linux: the resize grips just inside the window's edges, outside its content) |
| `shell.rulerHeight` / `shell.loopStripHeight` | 24 / 12 |
| `shell.trackHeaderWidth` | 180 |
| `shell.panelHeaderHeight` | 24 (detail panel tabs, side panel titles) |
| `shell.padding` | 12 (inside side panels and popovers) |
| `shell.rowHeight` | 20 (property rows, compact list rows) |
| `shell.propertyLabelWidth` | 80 |
| `shell.listRowHeight` | 28 (palette and browser results) |
| `shell.listTextInset` | 10 (a list row's text from its edges; its headings line up with it) |
| `shell.listIconSize` | 12 (an icon button at the end of a list row: a preset's star) |
| `shell.presetListWidth` / `shell.presetListRows` | 320 / 10 (a module's preset list, [presets.md](presets.md) §7: rows shown before it scrolls) |
| `shell.settingsIndexWidth` / `shell.settingsColumnWidth` | 160 / 720 (Settings: the index of sections, and the most the page's column takes, [settings.md](settings.md) §4) |
| `shell.settingsLabelWidth` / `shell.settingsControlWidth` | 200 / 280 (a setting's name, then its controls) |
| `shell.settingsKeyWidth` | 160 (a shortcut in Settings > Keys, at the controls' right edge, so long action names get the rest) |
| `shell.browserIndent` | 12 (a browser row in from its parent, [browser.md](browser.md) §4) |
| `shell.browserWaveHeight` | 32 (the browser's preview strip: a sample's waveform) |
| `shell.popoverWidth` | 560 |
| `shell.formPopoverWidth` | 420 (a popover of property rows: the export panel, C8) |
| `shell.toastHeight` / `shell.toastWidth` | 36 / 420 |
| `shell.keyboardWidth` / `shell.velocityHeight` | 52 / 56 (piano roll) |
| `shell.railWidth` / `shell.splitter` | 44 / 5 |
| `shell.barPadding` / `shell.barInset` | 8 / 6 (a bar's sides; top and bottom) |
| `shell.itemGap` / `shell.groupGap` | 4 / 12 (between items of a group; between groups) |
| `shell.buttonSize` / `shell.fieldHeight` | 28 (icon buttons in bars and rails) / 24 (text fields) |
| `shell.fieldTextInset` | 6 (text from the left edge of a combo box, name field or input button) |
| `shell.buttonTextPadding` | 8 (a text button's padding each side of its label; `ui::Toggle::widthFor`) |
| `shell.propertyFieldInset` | 1 (a field in a property row, from the row's top and bottom) |
| `shell.lightSize` | 6 (an activity light: MIDI input, assistants) |
| `shell.clipGap` | 1 (a clip from its row's top, from the separator line below it, and from a clip touching its end: the seam) |
| `shell.clipOutline` | 1 (a selected clip's outline; a fade's square sits inside it) |
| `shell.clipTitleHeight` / `shell.clipTitleShare` | 15 / 35 % (a clip's title strip: at most this tall, and at most this share of the clip) |
| `shell.clipTitleMinText` | 10 (a title strip shorter than this shows no text) |
| `shell.clipTextInset` | 4 (a clip's name and badge, from its edges) |
| `shell.laneTextInset` | 8 (text in a lane, a hint or a take's name, from its visible left edge) |
| `shell.takeStripHeight` | 24 (a take's strip under its clip, C5b) |
| `shell.takeIconSize` | 12 (the audition button, `Icon::Audition`, at the start of a take strip's name chip; ADR 0038) |
| `shell.progressHeight` | 2 (the progress line along the bottom of what's being worked on, C7) |
| `shell.clipButtonSize` | 12 (a button on a clip, on a `surface2` chip: cancel its bounce; menu-bar.md §15) |
| `shell.dropLine` | 2 (where a dragged module, row or strip lands: a line in `active`) |
| `shell.fadeHandleSize` | 7 (a fade's or crossfade's handle on a clip, C6; a fade narrower than this draws only its handle) |

Components size themselves for their text: `ui::Toggle::widthFor(label)` and `ui::NumberField::widthFor(widest value)`; bars ask them rather than adding padding by hand. Every clip the arrangement draws (clips, recordings in progress, take strips) takes its box and title from one place (`ArrangementView::clipBox`, `titleOf`), with its edges on whole pixels.

## Shared components (`modules/fd_ui/shell`)

| Component | What it is | Used by |
|---|---|---|
| `PanelHeader` | A 24 px header: a section-style title or tabs on the left, then context; on the right, its view options (`ViewOption`, `ViewChoice`; hidden ones take no room), commands left of them a group apart (`addCommand`: a Toggle as a button, sized by its owner, centred), and a caption | Detail panel (Modules / Clip tabs; a plain Modules title in the mixer view), inspector, browser, the mixer view (Modules, Sends, Narrow) |
| `AutomationState` / `paintAutomationIndicator` | Whether a control's parameter is automated: a 4 px disc in `automation`, filled while its lane plays and a ring while it's off (left of a knob's name; a bar's, fader's, toggle's or choice's top-left corner). `Knob` and `Toggle` take the state and an `onPopupMenu` (right-click) | Every control of a parameter (C4) |
| `ViewChoice` | A view setting with a value ([ADR 0047](../decisions/0047-the-editing-grid-in-each-editor.md)): the value in `textSecondary` (`textPrimary` hovered), in the `ViewOption`'s label style and padding, then a small chevron, no fill; hover: the `hoverWash`. As wide as its value: its owner lays it out again when the value changes, so it sits as close to Snap as one view option to the next. A click opens its menu (the owner fills it) under it | The grid's step beside Snap, in the arrangement's corner and the clip view's header |
| `ViewOption` | A view option: a 6 px indicator and a `Control` label, no button fill. Off: a 1 px ring and `textTertiary`; on: a filled `active` disc and `textPrimary` (the shape tells on from off, not only the colour); hover: the `hoverWash` behind it. Its own padding is half the shell's: aligned on a text edge, it starts that much before it | The mixer view's Modules, Sends and Narrow; the arrangement's Snap, in the corner above the headers (C11 review); the piano roll's Snap, in the clip view's header (ADR 0047); later the browser's view options |
| `PropertyList` | Label and value rows on the row grid; values in `Body`, or `Value` for numbers | Inspector; export options (C8); preferences |
| `ListRow` | One row of a list: optional dot, name, caption, shortcut on the right; hover and selected states; room kept at the right (`trailing`) for a control the caller draws there, whose area it returns | Command palette; the preset list |
| `TreeRow` (`paintTreeRow`) | A ListRow in a tree: indented by `shell.browserIndent` per level, a disclosure (`ChevronRight`/`ChevronDown`) or a `+` for a command row, the name, a caption at the right (a count, a length), and a star column kept on every row so captions line up; `treeRowMark` and `treeRowStar` say where to click. A flat row (a search result) has no disclosure column | The browser ([browser.md](browser.md), ADR 0046) |
| `modifiedMark` / `paintNameWithMark` | What follows a name while what it names has changes not kept: " •" right after the name, never cut away (the name is cut instead) | The title bar's project name while unsaved; a module's preset while it differs from the preset (ADR 0044) |
| `Popover` | Floating chrome: `surface2`, panel radius, the one shadow | Command palette; Assistants popover; future menus that float |
| `Toast` | A message in the arrangement's corner that times out | Warnings and confirmations |
| `EmptyState` | A centred hint where there is nothing to show | Chain without a track, clip view without a clip, empty project, empty inspector |
| `paintValueTag` | What a gesture is changing, next to the pointer while it drags: `Value` text on a `surface3` tag a row high, kept in its area | Fade and crossfade drags (C6) |
| `paintProgress` | Work in progress on what it changes: a `shell.progressHeight` line along its bottom, a `surface3` track filled in `value` from the left; shown only after `motion.progressDelay` | Clips being bounced (C7); export (C8) |
| `SectionStack` | Titled sections one under another: a `Section`-style caps title on the row grid, the content under it, and a 1 px `line` centred in the group gap between neighbours (a module panel's sections, stacked). `titleArea(i)` is a section's title row, for a command at its right | The inspector ([selection.md](selection.md) §5); Settings, each section's Reset in its title row ([settings.md](settings.md) §6); later the browser |
| `BarLayout` | Places a bar's items left and right on the item and group gaps | Small bars |
| `model::layoutTopBar` (app) | The top bar's three sections: left and right items on the item and group gaps, the centre cluster on the bar's middle (moved in only when the sides need the room), and items dropped by priority when it's narrow; pure, `[fast]`-tested | The top bar |
| `OutsideClick` | Closes a popover on any mouse press outside it (other top-level windows, such as its popup menus, excepted); popovers also close on Escape | The command palette, the lane picker, the Assistants popover |
| `Brand` | The mark (`paintMark`), the mark and name (`paintBrand`), the app icon (`appIcon`) | The title bar's left end (the top bar's on macOS); the app and window icon |
| `NameEditor` | A name edited in place (C11): a field over the name's own area in its text style, on `surface2` with the focus ring, its text where the name was, selected, with the keyboard focus (`takeFocus`); Return or a click away keeps it, Escape or an empty name restores it, Tab and Shift+Tab go on. Done, it gives the focus to the nearest ancestor that takes keys (the shell, the mixer), never a control beside the name | Track and lane headers, clip titles, mixer strips |
| `takeFocus` | Gives a component the keyboard focus and keeps it until the next mouse press (or `focusKeptFor`): a window getting the system's focus back (after a menu closes, at launch) hands JUCE's focus to what had it before, silently, which would leave an open field that typing never reaches. Anything that opens to be typed into takes the focus this way, never with `grabKeyboardFocus` | The name editor, the inspector's names, the command palette, the lane picker, the export and About popovers, a knob's or number's typed value, the shell at launch (C11 review) |
| `EndEditingOnClickAway` | A press anywhere else in the window ends typing in a text field and keeps what was typed: the focus goes home (the shell), and the field's focus loss keeps it. Presses in other windows (menus) don't count, nor does the press that opened the field (`tookFocusOnThisPress`) | Installed once by the shell: every text field (C11 review) |
| `ColourGrid` / `addColourGrid` | The 16 palette colours as a 4 × 4 grid in a menu, a row high each, `itemGap` apart, padded like a text button, the current one ringed | Right-click and menu bar Colour ▸ (C11) |
| `ColourSwatch` | A colour as a property value: its swatch a row high, the hover wash; a click opens the grid | The inspector's Colour rows (C11) |
| `PresetBar` / `PresetField` / `PresetList` (`fd_device_ui`) | A module's presets in its header ([presets.md](presets.md), ADR 0044): previous and next (`deviceGrid.headerButton`), the preset's name in a combo's look with the modified mark, and the A/B toggle; the list is a `Popover` with a search field, tag toggles and a Favourites view option, `ListRow`s grouped under caps headings with a star each (`Icon::Star`, `StarFilled`), Save and Save as… below; the save form is property rows in place of the list | Every module's header, in the DAW and the plugins |
| `WindowButtons` | Minimise, maximise (restore while maximised) and close, each `windowButtonWidth` wide and as tall as their bar: a glyph in `textSecondary`, `textPrimary` on the hover wash when hovered, Close on `record` (red); where each one is answers the system's hit-testing | The title bar (C10) |
| The menu bar's look (`LookAndFeel`) | Titles in `Field`, padded like text buttons, `itemGap` apart: `textSecondary`; hovered, `textPrimary` on the hover wash; open, on its menu's `surface2` down to the bar's bottom so the two read as one. A model that is a `MenuMnemonics` gets its Alt letters underlined while the bar's `mnemonicsShown` is set | The title bar's menus (C10) |
| `TitleBar` (app) | The brand and the menus on the left, the project's name in the middle (`model::placeTitle`: centred, moved in, elided before the wordmark drops; a dot while unsaved), the window buttons on the right; `model::layoutTopBar` places it | The window's top, Linux and Windows (C10) |
| `ActionRegistry::shortcutText` (app) | A shortcut as people read it: "Ctrl+Shift+E", "Alt+↑", "Space", "Esc", "F11" (the system's symbols on macOS) | Menus, tooltips and the command palette |

**Toggle or view option.** A `Toggle` switches something on or off in the project or a device: mute, solo, Pre, power, a setting. It is a control, with a fill. A `ViewOption` only changes how a view looks or edits (view state, never an undo step: Modules, Narrow, Snap) and lives in that view's panel header, or in the arrangement's corner above the headers (Snap), where it stays quiet until hovered.

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
| Subtitle (instrument, "No instrument") | 14 px row (`Caption`), below the name and 4 px below the buttons (which stand taller than the name's row, and it runs under them) | Height ≥ 48 |
| Controls (volume and pan bars) | 20 px row | Height ≥ 80 |
| Meter | 8 px strip on the right edge, full height | When the mixer provides one (C3) |

Padding: 12 px left (after the colour strip), 6 px top and bottom, 8 px right.

**Automation lanes** (C4, [automation.md](automation.md) §3, ADR 0029) are rows of their own among the tracks, with the same kit: the lane's colour strip, its name, its on switch in the buttons slot, what it drives in the subtitle ("Bass · Synth", "3 parameters") with the value now on the right, and with one assignment that parameter's control as a bar in the controls slot (height ≥ 80). Lane headers sit on `subLane`. Any row is resized by dragging its bottom edge in the header column (Alt: every row of its kind); heights are view state, by row (`model::ArrangementRows`).

## Channel strips (`model::ChannelStripLayout`)

The mixer view's strips (C3c, [mixer.md](mixer.md) §6) are placed by a pure layout model like the track headers: given a strip's height and the view's options (module names, sends, narrow, the number of returns), it places every part on the 4 px grid. Rows a strip doesn't use keep their space, so every fader and meter starts at the same height. Strip width: 96 px (narrow: 64, without the dB scale or Pre switches).

The controls are shared (`ui/ChannelControls`): volume and send bars on the volume taper, the pan bar, mute, solo and Pre toggles, the meter, the typed volume, and the fader, which is the shared `Knob` in its Fader style (a thin track filled to a flat cap, `faderCapHeight`; its travel follows the mouse one to one).

## Layout quality (a review checklist)

The lints catch colours and fonts; they can't see a cramped or inconsistent layout. Nor can they see a command only a key or a right-click reaches: every action goes in the menu bar (ADR 0040), and the shell's test fails on one that isn't. Every UI change, however small, is checked against this list before it's called done. The product owner rejects sloppy UI outright (2026-10-10: text touching its neighbours, and margins drifting from the standard set at the start).

1. **Every size comes from a system.** Paddings, gaps, offsets and sizes come from `ShellMetrics`, `TrackHeaderMetrics`, the module grid, or a layout model (`model::layoutTrackHeader`, `model::layoutTopBar`, `model::ChannelStripLayout`, `ui::SectionStack`, `ui::BarLayout`). No literal paddings or offsets in views. A size the systems don't have becomes a named metric in `fd_ui`, listed in this document.
2. **Nothing touches.** Text and controls keep at least `itemGap` from their neighbours, and their container's padding from its edges. Text is elided or clipped to its own slot; it never runs into a button, a badge or another text.
3. **The tightest case is checked:** the shortest row height, the narrowest clip, the longest name, and the smallest window (1024 px wide).
4. **Text stays legible.** It never sits on busy content (waveforms, notes, a clip's colour) without a backing, and it keeps its text style's colour roles.
5. **One look per thing.**
   - A control reuses the shared component (`IconButton`, `Toggle`, `NumberField`, `PanelHeader`, the track header kit) and its interaction: the hover lift, a right-click menu, a tooltip with the shortcut.
   - The same affordance looks the same everywhere (a chevron for something that opens).
   - A new variant goes into `fd_ui`, not into one view.
6. **Aligned to the rhythm.** Items in a row share a centre line. Bars use `itemGap` within a group and `groupGap` between groups. Rows and sections start on the same edges as their neighbours.
7. **Nothing without a reason to be there.**
   - State shows only when it matters: a take badge from two takes, a light only while lit.
   - A feature that adds clutter without earning its place goes, as stars on takes did.
8. **Reviewed by eye, zoomed in.**
   - Dump screenshots (`FD_DUMP_DIR=… fd_integration_tests "<the test>"`) at standard and compact widths, and crop and enlarge the changed area.
   - Look for touching, overlap, misalignment, clipping and uneven gaps, and compare with the same element elsewhere.
   - Where it's cheap, a test checks it: the top bar's controls never overlap, and the header slots never overlap.

## The UI lint

`ui_tokens_only` (a CTest check, label `fast`) fails when a file in `app/src/ui`, `modules/fd_device_ui` or `devices/*/ui` does any of these:
- calls `fonts::font` or `fonts::mono`;
- mixes colours (`interpolatedWith`, `withAlpha`, `brighter`, `darker`, `withBrightness`);
- uses literal colours (`juce::Colour(0x…)`, `juce::Colours::…`, except `transparentBlack`).

`modules/fd_ui` is where all of that lives. Module panel geometry is checked separately by the layout lint (`fd_panel_tests`).
