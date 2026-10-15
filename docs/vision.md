# Vision

## One line

**Segno** is a free and open-source music production suite: a **comprehensive set of modern, professional instruments and effects**, and an **arrangement-focused DAW** built around them, with a UI that looks and feels current.

## The problem

- **FOSS DAW engines are good; their UIs are not.** Mature FOSS DAWs have solid audio engines. Their interfaces feel 15–20 years old: menus several levels deep, modal dialogs, inconsistent widgets, and no visual craft. Rebuilding a UI is rarely a priority for these projects, so the problem persists.
- **FOSS instruments and effects exist, but not as a suite.** There are excellent individual plugins (Surge XT, Vital, LSP, Airwindows, Dexed, ChowDSP and others). Each looks and behaves differently, few are designed to work together, and many have dated UIs. Nothing in FOSS matches the experience of opening Ableton Live Suite and having a complete, consistent, high-quality toolbox in front of you.
- **Free-as-in-price is not the same as free software.** Tracktion Waveform Free is a capable no-cost DAW. It is proprietary, and its built-in device set is small.

## What we are building

1. **A device suite.** Instruments, audio effects and MIDI effects with the breadth of Ableton Live Suite, and modulators in the DAW ([ADR 0061](decisions/0061-modulators-move-any-parameter-on-their-track.md): a plugin can't move other plugins in another host). They share one design language, one interaction model, one preset format and one quality bar. They ship as plugins (CLAP, VST3, LV2, AU, standalone) and work in any DAW.
2. **A DAW.** It is arrangement-focused and built on tracktion_engine. The same devices run natively inside it, in a docked device chain with no floating windows. It hosts third-party plugins too. Any AI assistant the user chooses can drive it, and every device in it, through a built-in MCP server ([ADR 0021](decisions/0021-mcp-server.md)). This is opt-in and local, and no assistant is bundled.

The two are developed in parallel from one shared device library (see [ADR 0002](decisions/0002-device-suite-and-daw-in-parallel.md)). The plugins make the suite useful early, inside DAWs people already use. The DAW provides the fully integrated experience.

## Who it is for

- Producers who work on a timeline (arrangement view) and want an Ableton-calibre toolbox without proprietary lock-in.
- Linux musicians who want a first-class, modern-looking production environment.
- Users of any DAW who want a cohesive, free, high-quality set of devices.

## What defines it (the pillars)

1. **Comprehensive devices: Live-level, and beyond where it counts.** Every role Live Suite's devices cover, we cover too, with Live-level features and sound. We go further selectively, mainly in the mixing core: one unlimited-band EQ instead of three simple ones. Instruments stay close to Live's stock instruments. Devices are merged only when they do the same job. Every device sounds professional, shows what it is doing, and works the same way as every other device. See [devices/device-suite.md](devices/device-suite.md).
2. **A modern UI.** Deep dark theme first, flat and minimal, close to Ableton's visual grammar but cleaner, adaptive single-window layout, context panels instead of dialogs, shallow menus where people expect them, with every command reachable by the mouse. See [design/ui-principles.md](design/ui-principles.md).
3. **Arrangement first.** The timeline is the centre of the DAW. Everything is designed around composing, arranging and mixing on it. See [ADR 0001](decisions/0001-arrangement-only-scope.md).
4. **Runs on Linux, Windows and macOS, as plugins and as a DAW.** No platform is second-class; Linux especially. See [ADR 0003](decisions/0003-platforms-and-plugin-formats.md).
5. **Free and open.** AGPL-3.0. Presets and state are stored in open, documented formats. Projects can be exchanged with other DAWs via DAWproject (later).
6. **Measurable quality.** Every device is checked by automated measurements and by human listening tests against established references before it ships. See [ADR 0007](decisions/0007-verification-without-ears.md).
7. **Beyond Live, not a copy.** Live is our reference, not a template, in both UI and functionality. We improve on it where the gain is clear and the cost is realistic, especially where the integrated DAW makes something possible that a standalone plugin can't do: cross-track awareness, drag-to-modulate, an arranger track, a project-wide key, control by any assistant ([ADR 0021](decisions/0021-mcp-server.md)). See [ADR 0011](decisions/0011-calibrated-ambition-and-consolidation.md) and [innovation-backlog.md](innovation-backlog.md).

## Non-goals (for now)

The aim is to stay focused, not to rule these out forever. Revisiting any of them requires an ADR.

- **Session view / clip launching** and live-performance workflows.
- **A Max for Live equivalent** (a visual patching environment inside the DAW).
- **Matching Live's 70+ GB sound library.** We ship curated factory presets and a modest set of CC0 sounds; community packs come later.
- **Hardware controller ecosystems** (Push-style integrations) beyond generic MIDI learn and mapping.
- **Video, notation and surround/immersive mixing.**
- **A built-in AI assistant or a bundled language model.** Assistants the user chooses can drive the DAW through MCP ([ADR 0021](decisions/0021-mcp-server.md)), but the DAW itself ships none.
- **Mobile and tablet versions.**
- **Cloning Ableton.** We do not copy its names, presets, graphics, layouts or limitations. Live is the reference for coverage and quality; we go past it selectively (see [ADR 0008](decisions/0008-naming-and-trade-dress.md) and [ADR 0011](decisions/0011-calibrated-ambition-and-consolidation.md)).

## How this compares to what exists

| | Device suite | UI | Free software | Linux |
|---|---|---|---|---|
| Ableton Live Suite | Comprehensive, excellent | Modern | No | No |
| Bitwig Studio | Comprehensive | Modern | No | Yes |
| Tracktion Waveform Free | Small | Mixed | No (freeware) | Yes |
| Ardour | Minimal built-in; relies on plugins | Dated | Yes | Yes |
| Individual FOSS plugins | Excellent pieces, no cohesion | Inconsistent | Yes | Mostly |
| **This project** | Comprehensive, cohesive; Live-level, beyond where it counts | Modern, dark-first | Yes (AGPL) | Yes |

## What success looks like

- A full track can be produced, from sound design through mixing to master, **using only our devices**.
- In blind A/B tests, each core device holds up against its Live Suite counterpart. The EQ can match the features of the best commercial EQs.
- A new user can create a track, add instruments and effects, record, arrange and export **without a modal dialog, and with the mouse alone**. Every command is in a menu bar where they'd look for it in any app, with its shortcut beside it; the frequent ones are one click away where they work ([ADR 0040](decisions/0040-a-menu-bar-and-everything-by-mouse.md)).
- The UI stays smooth (60 fps or more) with a large project open, on a mid-range laptop running Linux.
