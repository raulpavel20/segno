# 0046. One browser, one search; modules are folders of their presets

- **Status:** Proposed (for review with [design/browser.md](../design/browser.md) §16)
- **Date:** 2026-10-11
- **Builds on:** [ui-principles.md §2](../design/ui-principles.md) ("one search box across devices, presets, samples, plugins and files. Sample preview is built in"), [0044](0044-presets-are-files.md) (presets), [0045](0045-settings-are-one-page.md) (settings: the Segno folder, the browser's places), [0041](0041-tracks-and-clips-managed-in-place.md) (tracks and clips), [0030](0030-projects-are-folders.md) (projects are folders), [0040](0040-a-menu-bar-and-everything-by-mouse.md) (the menu bar), [0021](0021-mcp-server.md) (assistants)

## Context
- The browser is a placeholder. Modules are added only from the chain's + menu, presets only from a module's header, files only by dragging them in from the desktop; samples can't be heard first, and a project's files can't be gathered to move it.
- In the field (browser.md §2), Live and Studio Pro show each device as a folder of its presets, and Bitwig filters and drills into a device's presets; all preview on selection with their own volume, Bitwig and REAPER in the project's tempo; Live, Cubase, Logic and REAPER gather or clean a project's files.
- The product owner likes Live's devices that open like folders, holding their presets.

## Decision
- **The browser is one panel on the left:** a tree of Favourites, Instruments, Audio effects, Plugins and Places, and one search over everything it knows, without a scope to choose first.
- **A module is a folder of its presets:** its own row loads its default; inside are your presets and the factory ones, from the same library as the module's header.
- **Files are browsed where they are,** in places you choose, indexed in the background; nothing is copied until you collect the project's files, and nothing is deleted but by Remove unused files, to the system's trash.
- **Previews play on selection,** after the master's modules at their own volume, in time with the song while it plays; a loop dropped in the song follows its tempo too.
- **A drag puts it where it's dropped; double-click puts it on the selection;** one undo step each. An instrument replaces the track's instrument.
- **Assistants can search, browse, preview and collect,** never delete.

## Consequences
- The browser placeholder is replaced; the module chain's + menu stays as a quick way in.
- New: a JUCE-free browser index and search, a background indexer, a preview player mixed after the master, an offline preset render for instrument presets, clips that follow the song's tempo (the start of M4's warping), Collect files and save, Remove unused files.
- Settings > Library gains Browser places; View gains Search the browser (Ctrl+F) and Preview in the browser; File gains Collect files and save and Remove unused files….
