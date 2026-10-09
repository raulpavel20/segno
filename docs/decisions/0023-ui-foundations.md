# 0023. UI foundations: colours by role, text styles, shared components, and a UI lint

- **Status:** Accepted (product owner, 2026-10-09)
- **Date:** 2026-10-09
- **Builds on:** [0014](0014-flat-darker-ableton-adjacent.md) (flat, Ableton-adjacent grammar), [0018](0018-daw-app-structure.md) (app structure), [0022](0022-module-panels.md) (module panel kit)

## Context
The module panel kit (ADR 0022) fixed one surface: module panels. The rest of the UI still hand-tunes its look, piece by piece:
- **Colours:** about 50 colours are computed on the spot (`surface0.interpolatedWith(surface1, 0.45f)`, `active.withAlpha(0.10f)` …), mostly in the arrangement and the piano roll.
- **Fonts:** 54 direct font calls use 27 different size and weight combinations.
- **Layout:** about 60 numbers in the shell are placed by hand.
- **Duplicated views:** the piano roll re-implements the arrangement's ruler, grid and playhead, and the two have already drifted apart (different ruler heights and tick sizes).

The next steps build exactly where this matters:
- **mixer (C3):** controls and meters in the track headers;
- **automation (C4):** lanes under tracks, a third time view;
- **recording (C5):** arm buttons;
- **export (C8):** an options form;
- **browser and preferences (M3).**

Ad hoc colour mixes also block the light theme planned for M7: each mix of dark surfaces would have to be found and fixed by hand.

## Decision
1. **Colours by role.** Every colour a view draws is a named role in `fd_ui::Theme`: lane, selected lane, grid lines, selection, drop target, playhead, clip body and content, note fill, shade, overlay, spectrum, and so on.
   - Roles are derived from the base palette in one place (`Theme.cpp`), so a theme can define them differently.
   - Interaction states are tokens too: hover and pressed lift, the dimmed alpha, the focus ring.
2. **Text styles.** About a dozen named styles (title, name, body, label, caption, control, field, section, value, value small/medium/large, micro) replace direct font calls. Only `fd_ui` picks fonts and sizes.
3. **Motion and input constants.** Animation time constants, drag thresholds, edge grab widths, and wheel and pinch factors are tokens. The arrangement and the piano roll then feel the same.
4. **Shell metrics and shared components.** The shell's sizes are tokens (bar and ruler heights, header widths, paddings, row heights). Repeated pieces become shared components:
   - a panel header with tabs;
   - a property list (label and value rows);
   - a list row;
   - a popover;
   - a toast;
   - an empty-state hint.
5. **One time-view kit.** The ruler (bar numbers, ticks, optional loop brace and range strip), the grid painter, the playhead and the box selection are shared components. So is one input mapping (wheel, pinch, ruler drag) over the existing `Timeline` model. The arrangement and the piano roll use them now; automation lanes and any later time view must too.
6. **A track header kit.** The header column has fixed slots: colour, name, subtitle, a button row (mute, solo, arm), a control row (volume, pan) and a meter. Which slots show depends on the track's height. This is a pure layout model, so the mixer (C3) and recording (C5) fill slots instead of placing controls.
7. **A UI lint.** A CTest check fails on the following, outside `modules/fd_ui`:
   - a direct font call;
   - colour arithmetic (`interpolatedWith`, `withAlpha`, `brighter`, `darker`);
   - literal colours.

   It covers `app/src/ui`, `modules/fd_device_ui` and `devices/*/ui`. The module layout lint (ADR 0022) already covers panel geometry.

## Consequences
- **Easier:**
  - new views are assembled from roles, styles and components, and look like the rest by construction;
  - a light theme becomes a matter of defining roles;
  - the arrangement, the piano roll and the coming automation lanes behave identically.
- **Harder:**
  - a new visual need starts with a new role, style or component in `fd_ui` (and its doc entry), not a local tweak. That is the point.
- **Docs:** `docs/design/ui-foundations.md` lists every role, style, token and component. `design-language.md` keeps the values of the base palette.
- **Scheduled:** M2 step A4, before the mixer (C3).
