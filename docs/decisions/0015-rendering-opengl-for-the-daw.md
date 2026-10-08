# 0015. Rendering: OpenGL for the DAW window, software renderer for plugin editors

- **Status:** Accepted (2026-10-09: the product owner found OpenGL smoother, with text equally good in both modes)
- **Date:** 2026-10-09
- **Follows:** [0012](0012-ui-toolkit-juce-everywhere.md) (rendering path to be chosen by a spike)

## Context
M0 step 10 built `tools/spike` (`fd-spike`), which draws two stress scenes in the real theme with either JUCE's standard (software) renderer or JUCE's OpenGL renderer:
1. **Device chain:** 6 device panels, each with an animated 256-point response curve, a 96-bar spectrum and 6 moving knobs.
2. **Arrangement:** 32 tracks of waveform clips with continuous zoom (2–96 bars) and scroll. Waveforms come from min/max peak mipmaps, computed for every pixel column on every frame.

Measured on the reference machine (Ryzen AI 9 HX 370, Radeon 890M, Ubuntu 26.04, GNOME Wayland via XWayland, 2× UI scale), Release build, every frame fully invalidated:

| Scene | Display | Software: paint / fps | OpenGL: paint / fps |
|---|---|---|---|
| Device chain | 4K external (3840×2160) | 8.1–8.9 ms / 41–50 fps | 4.1 ms / 116–119 fps |
| Arrangement | 4K external | 18.2–25.0 ms / 26–33 fps | 17.1 ms / 51 fps |
| Device chain | Laptop panel (2880×1920, 120 Hz) | 5.7 ms / 65 fps | 2.6 ms / 118 fps |

Findings:
- **OpenGL roughly halves drawing time** and paints on its own thread. The device chain reaches the display refresh rate (~120 Hz); software tops out around 40–65 fps at this scale.
- **OpenGL needs the GL thread to drive frames** (continuous repainting, with the message thread only invalidating). Scheduling repaints from the message thread while OpenGL is attached caused bad frame pacing (32 fps despite 3.6 ms paints).
- **The arrangement is CPU-bound in our test code** (per-pixel peak lookup every frame), not in drawing. Real waveform rendering must cache its drawing per zoom level, whichever renderer is used.
- A Debug build is 3–5× slower. Performance must always be judged in Release.

## Decision
1. **The DAW window uses JUCE's OpenGL renderer**, attached to the top-level content, with continuous repainting driven by the GL thread. A "Software rendering" preference is the fallback for machines with poor GL drivers.
2. **Plugin editors use the software renderer by default.** They are small (one device panel), and GL inside third-party hosts on Linux is a compatibility risk. An opt-in OpenGL setting can be added later if needed.
3. **Heavy views cache their drawing:**
   - waveforms: per clip, per zoom level, invalidated on edit;
   - spectra and meters: draw only what changed.
4. **Performance budgets** (Release, reference machine, 4K at 2×), checked by paint benchmarks:
   - device chain under 6 ms per frame;
   - 32-track arrangement while zooming under 8 ms per frame.

## Consequences
- `fd_ui` components must be thread-safe to paint on the GL thread. Component state is changed on the message thread, and painting must not take locks that the audio thread uses. Anything read during paint must be safe to read from another thread (the spike's stats needed a mutex; the animation clock needed an atomic).
- M1's arrangement view must implement a waveform cache to meet the 8 ms budget.
- The GL path has to be checked on Windows and macOS when those builds are tested (JUCE uses OpenGL on macOS and Windows too).
- If the product owner finds text or rendering quality worse under OpenGL, revisit before M1.
