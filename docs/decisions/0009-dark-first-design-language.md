# 0009. Dark-first, flat, minimal, contextual design language

- **Status:** Superseded by [0014](0014-flat-darker-ableton-adjacent.md)
- **Date:** 2026-10-08

## Context
The main complaint about existing FOSS DAWs is that their interfaces feel 15–20 years old. The product owner defined "modern" as:
- little to no menu diving;
- a modern, adaptable UI with context windows;
- a clean, minimal UI;
- gradients;
- a modern flat look and feel for the elements, with no dated styling;
- a deep dark theme first.

## Decision
The design language follows [design/ui-principles.md](../design/ui-principles.md). In summary:
- **Single adaptive window.** A bottom detail panel (device chain or clip editor), a right inspector, a left browser, and popovers for quick edits. No modal dialogs for editing.
- **Every frequent action** is one click or one shortcut away; **every action** can be found in a command palette.
- **Flat elements** (value-arc knobs, flat buttons, small consistent corner radii) with **purposeful gradients**: subtle ones for surface depth, vivid ones for data such as curves, meters and arcs, and accents for active states.
- **Deep dark theme first.** Near-black layered surfaces; content is the brightest thing on screen; colours, spacing and type come from design tokens, so a light theme can follow.
- **Devices designed as one family:** fixed-height panels, a live visualiser in each, and identical interaction rules everywhere.

Exact colours, type scale and spacing are chosen at the **M0 design gate**: candidate themes are rendered, the product owner picks one, and it is recorded in `design/design-language.md`.

## Consequences
- A custom component library is required. No OS-default widgets.
- Every UI feature has to meet the UI definition of done in the principles document, including product-owner sign-off on feel.
- Third-party plugin editors are the only accepted floating windows.
- The token system has to exist before any real UI work (M0).
