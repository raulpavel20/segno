# 0012. UI toolkit: JUCE everywhere; rendering path chosen by a spike

- **Status:** Accepted
- **Date:** 2026-10-08

## Context
The UI is the main thing that sets the product apart ([ADR 0009](0009-dark-first-design-language.md)). Qt Quick was considered for its animation and GPU rendering. However, the devices ship as plugins inside other DAWs ([ADR 0002](0002-device-suite-and-daw-in-parallel.md)), and Qt is fragile inside plugins:
- it expects to own a single application object and the event loop, both of which the host owns;
- there are documented problems with host crashes, broken menus, lost input and teardown hangs.

Using Qt for the DAW window only would mean two toolkits, two event loops and two text renderers, which breaks the single design language. Visage (MIT, GPU-rendered) is promising but young, and the JUCE integration we found is community-made and macOS-focused.

## Decision
- **JUCE is the only UI toolkit**, for device panels (in plugins and in the DAW) and for the DAW window.
- **The rendering path is chosen by a spike in M0.** Two test screens are built twice, once with JUCE's standard renderer and once with JUCE plus OpenGL for heavy views:
  1. a device panel with an animated, gradient-filled EQ curve and glow;
  2. a 32-track arrangement with waveforms and continuous zoom.

  Both are measured on the reference machine ([dev-environment.md](../dev-environment.md)) on the 4K monitor and the 2880×1920 laptop panel under fractional scaling. The product owner judges the look and feel. The result is recorded as a follow-up ADR.
- Visage is re-evaluated only if the spike shows JUCE can't meet the UI principles.

## Consequences
- One component library and design-token system serves both products.
- The animation and visual-effects tooling is less convenient than Qt Quick's. We build what we need in `fd_ui` (JUCE's animation module, display-synced redraws, cached gradients and shadows, and GPU shaders if the spike justifies them).
- On Linux, JUCE uses X11 and runs through XWayland under Wayland. Crisp rendering at fractional scaling is part of what the spike checks.
