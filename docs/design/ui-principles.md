# UI principles: what "modern" means here

"Modern UI" is the main thing that sets this project apart, so it cannot stay a matter of taste. This document turns it into principles, concrete rules and a checklist. The chosen values (theme "Steel") are recorded in [design-language.md](design-language.md). This document defines the constraints those values must satisfy.

Decision records: [ADR 0009](../decisions/0009-dark-first-design-language.md), superseded by [ADR 0014](../decisions/0014-flat-darker-ableton-adjacent.md); menus and the mouse: [ADR 0040](../decisions/0040-a-menu-bar-and-everything-by-mouse.md).

## Summary

- **Deep dark, clean and minimal, close to Ableton's visual grammar.** Dense and neutral; content (waveforms, curves, meters) is the brightest thing on screen; chrome recedes.
- **Flat everything.** Solid colours on surfaces and controls; no bevels, gloss, glows or 3D. Gradients are a reserved accent effect (automation, modulation).
- **Context, not dialogs.** Whatever you select, its controls appear in place, in a panel or popover, never behind a modal.
- **Nothing hidden.** A menu bar holds every command with its shortcut, as in any desktop app, so everything can be done with the mouse. Frequent actions are also one click away where you work, and have a key. Every action can be found by search.
- **One window that adapts.** Panels collapse, resize and reflow to fit a laptop screen or an ultrawide monitor.
- **Devices are designed together** and behave like one product.

---

## 1. Nothing hidden

People should not have to remember where things are hidden, and nothing should be reachable only by those who know a key or a right-click. Product owner, 2026-10-11: "Everything has to be accessible with the mouse in some way", and the menus should be where people expect them, "otherwise all users will be lost" ([ADR 0040](../decisions/0040-a-menu-bar-and-everything-by-mouse.md)).

**Rules**
- **Everything can be done with the mouse.** Every action is in the **menu bar** (File, Edit, Track, Clip, Transport, View, Options, Help: [menu-bar.md](menu-bar.md)), with its shortcut beside it, so the menus also teach the keys. A shortcut, the command palette or a right-click menu is never the only way to do something; a property is also in the inspector.
- Every **frequent action** is **visible where you work** (a button, or the right-click menu of the thing it acts on) **and has a shortcut**. "Frequent" means used in most sessions: add a track, add a device, split, duplicate, quantise, loop, record, undo. Export is in File, where every app keeps it.
- **Every action** is also in the **command palette** (`Ctrl/Cmd+K`): fuzzy search, showing the action's shortcut, recently used items first. The palette has a button in the top bar.
- **Menus are traditional and shallow.** File comes first and Help last; names and order follow common use, as in other DAWs. At most one level of submenus, in the menu bar and in right-click menus.
- **No modal dialogs for editing.** Modals are allowed only for:
  - confirming destructive actions;
  - unsaved-changes prompts;
  - first-run audio device setup.

  Everything else is edited in place, in a panel, or in a popover.
- **Context menus are shortcuts, never the only way** to do something: what they offer is also in the menu bar or the inspector. One menu builder serves both, so the items read the same.
- **Drag and drop works wherever it makes sense:**
  - files → arrangement;
  - device → chain;
  - preset → device;
  - plugin → track;
  - sample → sampler;
  - clip → another track.
- **Inline editing:** double-click to rename, type a value into any focused control, drag numbers to change them.
- **Preferences** are one searchable page, not a set of tabs. Common settings (audio device, buffer size, theme, UI scale) are also reachable from the command palette.

## 2. An adaptive UI built on context panels

The UI shows the controls for what you are working on, where you are working.

**Layout (one window):**

```
┌──────────────────────────────────────────────────────────────────────────────┐
│ ◆ SEGNO  File Edit Track Clip Transport View Options Help   My song    – ▢ ✕ │  Title bar: menus, title, window buttons
├──────────────────────────────────────────────────────────────────────────────┤
│ ▶ ■ ●   120.00 BPM  4/4   1 . 1 . 1   ⟲ loop   metronome   CPU ▁▃   ⌕ Ctrl+K   │  Transport bar
├──────────┬───────────────────────────────────────────────────────┬───────────┤
│ Browser  │  Arrangement                                          │ Inspector │
│          │  ┌ ruler / loop brace / markers ──────────────────┐   │           │
│ devices  │  │ track ▸ [ clip ][ clip      ]   [ clip ]        │   │ context-  │
│ presets  │  │ track ▸      [ midi clip        ]               │   │ sensitive │
│ samples  │  │ track ▸ ~~~~ automation lane ~~~~               │   │ properties│
│ plugins  │  └─────────────────────────────────────────────────┘   │ of the    │
│ files    │                                                       │ selection │
├──────────┴───────────────────────────────────────────────────────┴───────────┤
│ Detail panel: device chain of the selected track  ⇄  clip editor (piano roll)│
│ [ Device ▾ ][ Device ▾ ][ Device ▾ ]  +                                      │
└──────────────────────────────────────────────────────────────────────────────┘
```

- **Detail panel (bottom)** is the main context surface.
  - Select a track and it shows that track's device chain.
  - Select a clip and it shows the clip editor (piano roll or audio clip properties).
  - It switches automatically, and `Tab` toggles between the two views.
- **The centre** shows the arrangement, or the **mixer view**: every channel as a strip, side by side ([mixer.md](mixer.md), ADR 0026). Arrange ⇄ Mix switches between them; the selection is shared, and each context panel shows only what that view needs (the mixer view: modules below, no inspector) ([selection.md](selection.md), ADR 0027).
- **Inspector (right, collapsible)** shows the properties of whatever is selected: clip, track, device or note range, in titled sections. It replaces dozens of property dialogs. It never repeats what the centre view already shows.
- **One selection, one subject:** the panels always show the thing selected last; a selected track and a selected clip never disagree.
- **Popovers** handle quick edits next to the thing being edited, such as a clip's colour and name, fades, or a parameter's automation and modulation. They close when you click away and never block anything.
- **Browser (left)** offers one search box across devices, presets, samples, plugins and files. Sample preview is built in.

**Adaptive behaviour**
- **Compact width (below ~1440 logical px):** the browser collapses to an icon rail, and the inspector becomes an overlay instead of a docked panel.
- **Standard width:** everything is docked; panels are resizable; their sizes are remembered per project.
- **Wide (2560 px and up) or ultrawide:** the inspector stays docked open; the detail panel can show the device chain and clip editor side by side.
- **Focus mode** (one shortcut) hides everything except the arrangement.
- **UI scale** goes from 75% to 200% and is independent of the OS. The UI is fully HiDPI and renders crisply at fractional scales.
- Third-party plugin editors are the only floating windows; they cannot be avoided. Detaching our own panels to a second monitor is a later feature.

## 3. Clean and minimal

**Rules**
- **Separate with space and elevation first.** Use spacing and surface tone to group things. Borders are rare, thin (1 px) and low-contrast. The exception: sections inside a module panel are separated by a 1 px line, so related controls read as a group ([ADR 0022](../decisions/0022-module-panels.md)).
- **Progressive disclosure.**
  - A device shows its essential controls by default; depth lives in an *expanded* view.
  - Secondary controls appear on hover or selection, but anything essential is always visible.
- **Few, consistent icons.** One line-icon set (1.5 px stroke on a 16 px grid). Every icon has a tooltip that includes its shortcut. When the meaning isn't obvious, use a text label rather than an icon.
- **Quiet chrome, loud content.** Waveforms, curves, meters and clip colours are the most vivid things on screen. Panels, headers and toolbars recede.
- **One way to do each thing.** Avoid duplicate controls for the same action in different places. The menu bar, right-click menus, the command palette and shortcuts are routes to an action, not copies of it, so they don't count.

## 4. Flat, minimal, close to Ableton's visual grammar

Decision record: [ADR 0014](../decisions/0014-flat-darker-ableton-adjacent.md).

**Everything is flat:**
- Surfaces, panels, headers, controls and clips are **solid colours**.
- Separation comes from close tone steps and spacing; a 1 px line only where that reads better.
- No bevels, embossing, glossy highlights, glows or fake 3D, and no shadows on docked UI. Floating menus and popovers may use one subtle shadow so they read as floating.
- **Knobs:** a thin value arc (about 2–2.5 px) in a solid colour, a dim track, and a needle. No centre cap and no 3D. Compact: about 32–40 px arc diameter at 100 %.
- **Buttons and toggles:** rectangles with a very small radius (2–3 px). On = solid fill with dark text; off = a dim surface with secondary text.
- **Data displays** (EQ curves, spectra, envelopes, waveforms): a solid line plus a flat translucent fill of the same colour.
- **Meters:** flat level zones (normal, hot, clip), no gradient.
- **Density close to Ableton:** small type, compact controls, information-dense panels, colour only where it means something (active states, clip/track colours, meters). Modern and minimal: one spacing grid, fewer lines, Inter for text and a mono for values.

**Gradients are a reserved accent effect.** They're allowed only where listed here:
1. **Automation:** the fill under an automation lane's curve, in the lane's own colour, fading out towards the lane's floor.
2. **Modulation:** the modulation range shown on a modulated control.

Additions to this list are made here, deliberately. Gradients are never used on surfaces, panels, headers, controls, clips or text.

**Motion**
- Panel open and close, view switches and popovers take 120–180 ms with ease-out curves.
- Zoom and scroll are continuous and smooth, never jumpy.
- Nothing animates in a way that delays the user's next action.
- The UI respects a "reduce motion" setting.

## 5. Deep dark theme first

The dark theme is the primary design target. A light theme comes later; everything is built from **design tokens** from day one so it can be added without touching components.

**Surface ladder:** deeper and neutral, with close steps. The final values (theme "Steel") are in [design-language.md](design-language.md); the table shows the shared starting ladder.

| Token | Use | Starting point |
|---|---|---|
| `surface0` | App background, arrangement canvas | `#060708` |
| `surface1` | Panels (browser, detail, inspector) | `#0B0C0E` |
| `surface2` | Device bodies, cards | `#111215` |
| `surface3` | Controls (knob tracks, off buttons), hover | `#1B1D21` |
| `line` | The rare divider | `#202226` |
| `textPrimary` | Values, names | `#E4E6E9` |
| `textSecondary` | Labels | `#9A9EA6` |
| `textTertiary` | Hints, disabled | `#5D6168` |

**Colour roles** (tokens):
- `value`: knob arcs, slider and fader fills, curve lines.
- `active`: on states, device power, selection.
- `effectStart` → `effectEnd`: the modulation gradient, and the automation indicator on controls. An automation lane's gradient is in the lane's own colour (its row colour, like a track's).
- Role accents: instrument, MIDI and modulation tags; record/alert.
- Meter zones.

Rules:
- **Deep, but not pure black.** `#000` causes harsh contrast and smearing on OLED screens.
- **Clip and track palette:** about 16–24 curated hues tuned for dark backgrounds, flat. Waveforms and notes are drawn in a lighter tint of the clip colour.
- **Contrast:** text meets WCAG AA (4.5:1 for body, 3:1 for large text and essential graphics). Values never depend on colour alone. The default accents are colour-blind-safe.

## 6. Typography

- Two bundled variable fonts under the OFL licence, so rendering is identical on every OS: a modern sans-serif (Inter) for names and text, and a mono (JetBrains Mono) for values and readouts ([design-language.md](design-language.md), "Type").
- Proposed type scale: 11, 12, 13, 15 and 18 px. Hierarchy comes from size and weight, not from boxes and borders.
- **Every value is set in the mono**, so numbers don't jitter while they change, and readouts look like an instrument's.
- Sentence case for labels. All caps only for tiny section labels, set in the mono.

## 7. Modules look and behave as one product

Every built-in instrument and effect (a **module** in the UI; a *device* in code) follows the same anatomy and interaction rules, so learning one teaches all of them. The layout spec is [device-panels.md](device-panels.md) ([ADR 0022](../decisions/0022-module-panels.md)).

**Panel anatomy**

```
┌─ ‹ ● Module name ──────── preset ▾ ── settings ▾ ┐   header 28: fold, power, name, preset, settings
│  SECTION A            │ SECTION B ⏻ │ OUTPUT    │   titled sections, 1 px lines between them
│  ┌─────────────────┐  │  ◜◝   ◜◝    │  ◜◝       │
│  │ live visualiser │  │  Freq Res   │  Output   │   controls on one grid: 48 px columns,
│  │ (curve / env /  │  │  ◜◝   ◜◝    │  ◜◝       │   20 px rows; every knob 48 × 76
│  │  waveform)      │  │  Drive Mix  │  Mix      │
│  └─────────────────┘  │             │           │
└──────────────────────────────────────────────────┘   fixed height 232 px; sections collapse, the module folds
```

- **Fixed height and a horizontal chain**, so a whole chain reads left to right. The **expanded view** adds width, never height.
- **One grid, no hand-placed geometry.** Panels declare sections and place controls on cells; the kit computes every position, and a lint test rejects anything off the grid.
- **Sections** group what belongs together, under a short title, with a line between neighbours. Clicking a title collapses the section to a narrow strip. In the DAW, a whole module folds to a strip. Both are view state, saved with the project but never undoable.
- **Visualisers are controls.** Wherever a device draws something (EQ curve, envelope, transfer curve, waveform markers, filter response, delay taps), you can grab it and edit it directly. Knobs are the secondary path, not the only one.
- **Compact first, deep on demand.** Some devices are deep, but their default view stays simple: for example, the EQ's compact 3-band view or the Sampler's classic mode. Depth sits one click away in the expanded view, never in a menu.
- **Every device shows what it is doing:**
  - EQs and filters: curve plus spectrum;
  - dynamics: transfer curve plus gain reduction;
  - distortion: transfer shape plus harmonics;
  - delays and reverbs: decay or time display;
  - synths: oscillator, filter and envelope shapes;
  - samplers: waveform with playheads.
- **Interaction rules** (identical everywhere):
  - drag vertically to change a value; `Shift` for fine steps; double-click resets to default;
  - type a number while the control is focused; the mouse wheel adjusts;
  - right-click gives automation, modulation, MIDI learn and reset;
  - modulated parameters show a modulation ring; automated ones show an indicator.
- **One preset browser** per module header, with search, tags, favourites and A/B compare.
- **Third-party plugins** appear in the chain as compact cards: power, name, a few pinned parameters, and a button to open their own editor.

## 8. Keyboard and mouse, both complete

- **Everything with the mouse:** every action is in the menu bar (§1). Gestures with a modifier (Shift for fine, Alt to bypass snap) refine a mouse gesture; they aren't commands of their own.
- **Everything with the keyboard:** complete shortcut coverage for editing, navigation and transport, and the command palette for the rest. Every shortcut can be remapped. The menu bar opens from the keyboard too (F10).
- Menus and tooltips show shortcuts, so users learn them gradually.
- Focus is always visible and the arrow keys move it predictably.

## 9. Performance is part of the UI

- 60 fps or more while zooming, scrolling and dragging on large projects. Meters and playheads stay in sync with the display refresh.
- No spinners in the main workflow. Feedback is instant, and slow work (scanning, analysis) runs in the background with an unobtrusive progress indicator.
- Paint-time budgets are enforced by benchmarks (see [ADR 0007](../decisions/0007-verification-without-ears.md)).

---

## What we avoid (the "dated UI" list)

| Dated pattern | What we do instead |
|---|---|
| Bevels, embossing, glossy or photoreal 3D knobs | Flat controls; thin value arcs; gradients only as a listed accent effect |
| Grey OS-default widgets mixed with custom ones | One custom component library, used everywhere |
| Chains of modal dialogs and tabbed property dialogs | Context panels, inspector, popovers |
| Menus three levels deep; features only reachable from menus | A traditional menu bar with everything, one submenu level at most; frequent actions one click away too; a shortcut for each; command palette |
| Features only a key or a right-click away, that only those who know find | Every action in the menu bar with its shortcut beside it; right-click menus as shortcuts to it |
| Floating windows everywhere | Single docked window; only third-party plugin editors float |
| Toolbars full of cryptic icons | Few icons, always with tooltips and shortcuts; text labels where clearer |
| Tiny fixed-size bitmap text, blurry on HiDPI | Bundled variable font; vector rendering; 75–200% UI scale |
| Every plugin looking different | One design language across all devices |
| Visual noise: borders around everything | Space and elevation; quiet chrome, vivid content |
| Preferences required for basic tasks | Sensible defaults; settings in context and searchable |

## UI definition of done (applies to every UI feature)

- [ ] Uses design tokens only: colour roles, text styles, metrics and shared components ([ui-foundations.md](ui-foundations.md)). No hard-coded colours, sizes or fonts; the UI lint passes.
- [ ] Passes the layout quality checklist ([ui-foundations.md](ui-foundations.md), "Layout quality"): spacing from the systems, nothing touching or overlapping at the tightest sizes, one look per thing, nothing without its place; screenshots of the changed area reviewed zoomed in.
- [ ] Works in compact, standard and wide layouts, and at 100% and 200% scale. Snapshots reviewed.
- [ ] Has a mouse path and a keyboard path: an action is in the menu bar (with its shortcut) and the command palette; nothing is only a key or only in a right-click menu ([ADR 0040](../decisions/0040-a-menu-bar-and-everything-by-mouse.md)). Its design doc says where it is in the menu bar.
- [ ] No new modal dialog (or a recorded exception from §1).
- [ ] Follows the device interaction rules (§7) where they apply.
- [ ] Meets the paint-time budget.
- [ ] **Signed off by hand** by the product owner: it feels right to use.

## References (study, don't copy)

Live is a reference, not a template ([ADR 0011](../decisions/0011-calibrated-ambition-and-consolidation.md)). Take the best interaction ideas from anywhere, then improve on them where the gain is clear.


- **Ableton Live:** the closest reference for visual grammar: flat neutral greys, density, compact controls, single-window flow, the docked device chain. Nothing is copied ([ADR 0008](../decisions/0008-naming-and-trade-dress.md)).
- **Bitwig Studio:** the inspector panel and how modulation is shown.
- **FabFilter plugins:** interactive visualisers and direct manipulation.
- **Vital:** how modulation is visualised.
- **Linear, Figma, VS Code, Zed:** command palettes, care in dark UI, panel systems. VS Code: a menu bar in a title bar of its own, with the system's title bar as an option.
