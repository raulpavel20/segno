# 0014. Flat, darker and closer to Ableton; gradients only as an accent effect

- **Status:** Accepted
- **Date:** 2026-10-09
- **Supersedes:** [0009](0009-dark-first-design-language.md)

## Context
The product owner reviewed the first rendered UI: the Utility panel in three candidate themes, and the standalone plugin and DAW skeleton running live. Their verdict:
- **Too dimensional.** The subtle vertical gradients on panels and the gradient value arcs gave surfaces a 3D look. They want things flat.
- **Gradients should be a visual effect in a few accent places** (for example automation), not a general design tool.
- **Closer to Ableton**, but modern and minimalist.
- **Not dark enough.**

Everything else in ADR 0009 stands: dark first, minimal, context panels instead of dialogs, almost no menu diving, devices designed as one family, design tokens throughout.

## Decision
The design language follows [design/ui-principles.md](../design/ui-principles.md), updated as follows:

1. **Everything is flat.**
   - Surfaces, panels, headers, controls and clips use solid colours only.
   - Separation comes from tone steps and spacing, plus a 1 px line where that's clearer.
   - No glows or bevels, and no shadows on docked UI.
2. **Gradients are a reserved accent effect.** They are allowed only on an explicit list (automation lanes and curves, and modulation ranges for now), and anything new goes on the list through the UI principles doc. They are never used on surfaces, panels, controls or text.
3. **Darker.** A deeper, neutral surface ladder with close tone steps. The final values are picked at the design gate.
4. **Close to Ableton's visual grammar:**
   - Neutral greys, dense layout, small type, compact controls.
   - Colour only where it means something: active states, clip and track colours, meters.
   - Thin value arcs with a needle on knobs.
   - Rectangular buttons with a very small radius.

   It stays modern and minimalist: one consistent spacing grid, fewer lines, a single modern sans (Inter). Nothing is copied: no Ableton colours, icons, assets or layouts ([ADR 0008](0008-naming-and-trade-dress.md)).
5. **Choosing the theme:** the design gate (M0 step 11) is an interactive gallery that switches live between flat candidate themes. It shows a real device panel, controls in every state, a meter, clips on the arrangement canvas, and the one place a gradient is allowed (an automation lane).

## Consequences
- `fd_ui` controls and `DevicePanel` drop their gradients. Knobs get smaller and thinner, toggles become flat rectangles, and the meter switches to flat level zones.
- The token set separates **value colour** (knob arcs, slider fills), **active colour** (on states, power, selection) and **effect gradient** colours (automation and modulation only).
- Flat data displays (EQ curves, spectra, envelopes) use a solid line plus a flat translucent fill, not a gradient.
