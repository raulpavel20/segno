# 0054. Ableton Push 2 is supported, display included; other controllers through MIDI learn

- **Status:** Proposed (2026-10-14), with [push-2.md](../design/push-2.md) (Q1–Q16)
- **Date:** 2026-10-14
- **Revisits:** [vision.md](../vision.md), non-goal "Hardware controller ecosystems (Push-style integrations) beyond generic MIDI learn and mapping"
- **Builds on:** [0001](0001-arrangement-only-scope.md) (no Session view), [0021](0021-mcp-server.md) (only in the DAW; a client of the Session), [0027](0027-selection-and-context-panels.md) (one selection), [0040](0040-a-menu-bar-and-everything-by-mouse.md) (everything by mouse), [0045](0045-settings-are-one-page.md) (one settings page), [0052](0052-generic-names-are-free.md) (names)

## Context
- **The vision rules out controller integrations** beyond generic MIDI learn, and revisiting that needs an ADR.
- **The product owner has a Push 2** and wants to use it with Segno, with its display. They plan no Push 3 and no other controller of their own; other controllers go through generic MIDI learn (M3 C8). (2026-10-14)
- **Push 2 is documented for exactly this.** Ableton publishes its MIDI and display interface ([Ableton/push-interface](https://github.com/Ableton/push-interface)) for "people who want to create their own extensions or applications":
  - pads, encoders, buttons and the touch strip are class-compliant USB MIDI; LEDs and settings are set by MIDI and SysEx;
  - the display (960 × 160, 16-bit colour, 60 fps) is a USB bulk interface, reached through libusb.
- **Free software has done it for years:** Ardour (C++, GPL-2.0-or-later), DrivenByMoss for Bitwig and Reaper (Java, LGPL-3.0), and Ableton's own JUCE example (MIT).
- **The manual is Ableton's,** "all rights reserved": it may be used, not reproduced or redistributed. "Ableton" and "Push" are registered trademarks.
- **Push 2 was designed for Live's Session view:** clip launching on the 8 × 8 pads. Segno has no Session view (ADR 0001).
- **Options considered:**
  1. **Keep the non-goal:** Push 2 only as a plain MIDI controller through MIDI learn. Cheap, but no display, no lights that follow the song and none of what makes it worth having.
  2. **A controller scripting API**, so others write the Push support (Bitwig's way). A large interface to design and freeze, and nothing works out of the box.
  3. **A control-surface framework for many controllers,** on tracktion's `ControlSurface` classes (Mackie and others). Those edit tracktion's model directly, around the Session, its undo and its selection, and nobody has asked for other surfaces.
  4. **Push 2 built in,** as a client of the Session like the UI and the assistants. *Chosen.*

## Decision
1. **Segno supports Ableton Push 2, built into the DAW:** pads, encoders, buttons, lights, the touch strip, pedals and the display. Not Push 1 or Push 3. Every other controller works through MIDI learn, and so does Push 2 in its User mode.
2. **Only in the DAW.** The plugin builds never open the Push; inside another DAW, that host owns it. The code lives in `app/`, like the MCP server.
3. **A client of `engine::Session`, like the UI and the assistants.**
   - Its buttons run the `ActionRegistry`'s actions; its encoders edit through the host binding, one gesture from touch to release, so undo, automation and "devices never write their own parameters" hold unchanged.
   - **Nothing only the Push can do:** whatever it changes in the project is also an action or an edit the mouse can make (ADR 0040). A new one comes with its menu item.
4. **One selection** (ADR 0027): the Push shows and changes Segno's selection, and has none of its own.
5. **Its display is Segno's UI:** drawn with `fd_ui`'s colour roles, text styles, metrics and shared components, on a grid of 8 columns over the encoders, and checked like the screen (lint, layout tests, zoomed screenshots). Our own design, not a copy of Live's screens on the Push (ADR 0008). Curves come from the same functions the audio path uses.
6. **Arrangement first:** no clip launching (ADR 0001 holds). The pads play the selected track's instrument; the Session button shows the song, its tracks and sections, for finding your way around it.
7. **Ableton's manual is used, not reproduced:** our own table of the controls, written from it; the manual, its pictures and its JSON map stay out of the repository. libusb (LGPL-2.1-or-later) is a pinned dependency like any other.
8. **Names:** "Ableton Push 2" appears only to identify the hardware: in Settings, in the docs, in "works with". No Ableton logos, nothing implying endorsement, and "Push" in no name of ours: a feature, a mode, a product. ADR 0008 and 0052 hold for the names of our things.
9. **Tested without the hardware:** the same code runs against a virtual Push 2 in the tests; assistants and tests reach it through MCP (ADR 0021); its display frames are dumped as images for review. The product owner signs it off on the real one.

## Consequences
- **The vision's non-goal narrows** on acceptance: "hardware controller ecosystems beyond Ableton Push 2 and generic MIDI learn and mapping".
- **Easier:** playing, mixing and shaping sounds from the Push; every module is controllable on day one, its encoder banks taken from its panel's sections, with no Push code per module.
- **Harder, or must be maintained:**
  - a new dependency (libusb), and on Linux a udev rule before the display can be opened (MIDI works without it);
  - only one program can hold the Push: with Live running, Segno can't take it;
  - Ableton could change the firmware; the interface has been stable since 2016;
  - sign-off needs the hardware, so the product owner checks every step on theirs.
- **Next:** the design in [push-2.md](../design/push-2.md) is reviewed first; on acceptance, vision.md changes, Settings gains a Controllers section, and Linux packaging installs the udev rule (M7).
