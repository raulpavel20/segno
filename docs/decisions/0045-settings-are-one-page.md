# 0045. Settings are one searchable page, applied as you change them

- **Status:** Accepted (product owner, 2026-10-11, with every recommendation of [design/settings.md](../design/settings.md) §13; no reset button per setting)
- **Date:** 2026-10-11
- **Builds on:** [ui-principles.md §1](../design/ui-principles.md) ("Preferences are one searchable page, not a set of tabs"), [0040](0040-a-menu-bar-and-everything-by-mouse.md) (the menu bar; Settings… in Options), [0021](0021-mcp-server.md) (assistants), [0023](0023-ui-foundations.md) (the shared components), [0008](0008-naming-and-trade-dress.md) (no other vendors' names in the UI)

## Context
- What you set once has no home or a poor one: Audio settings is JUCE's stock selector in a callout; recording offsets and the recording file format have no control; export can't remember defaults; the Segno folder and plugin folders are fixed; shortcuts can't be changed; the interface's size follows the system.
- In the field (settings.md §2), DAWs use a window of tabs, mostly with OK and Cancel; only REAPER searches all its settings, and only Bitwig shows them inside the main window. Modern apps lead with search and apply changes as you make them.
- Segno's build has JACK off, so on Linux it reaches PipeWire only through ALSA.

## Decision
- **Settings are one page in the centre of the window,** beside the arrangement and the mixer: searchable, in sections with an index, never a modal dialog. The top bar keeps playing.
- **Every change applies and is saved at once;** there is no OK or Cancel. Each choice marks its default; a section shows one Reset only while something in it differs from its defaults.
- **Settings are per user,** in one readable `settings.json` in the system's place for app settings.
- **A setting with a natural place elsewhere keeps it there,** and that place opens the page at its row.
- **Every action's shortcut can be changed** on the page; conflicts are resolved in place.
- **Assistants can read and change settings,** never their own permissions or your keys.
- **JACK is on** in the DAW and the standalone plugins on Linux.

## Consequences
- Options > Audio settings… becomes Settings… (Ctrl+,); `app.json` and `assistants.json` move into `settings.json`.
- The `ActionRegistry` takes its keys from the key map; the menus, tooltips and palette show what you set.
- New shared pieces: the settings catalogue (names, descriptions, search words) and a themed audio section in place of JUCE's selector.
