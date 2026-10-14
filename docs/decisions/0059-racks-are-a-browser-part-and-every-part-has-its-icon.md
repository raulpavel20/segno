# 0059. Racks are a part of the browser of their own, and every part shows its icon

- **Status:** Accepted (product owner, 2026-10-14)
- **Date:** 2026-10-14
- **Supersedes in part:** [0056](0056-racks-are-modules-of-modules.md): "the browser lists racks in a Racks folder at the top of Instruments and of Audio effects"
- **Amends:** [0046](0046-one-browser-modules-are-folders.md): the tree's top level gains Racks, between Audio effects and Plugins

## Context
- Racks were built (ADR 0056, step 1) with a Racks folder at the top of Instruments and another at the top of Audio effects, by the kind of rack.
- The product owner (2026-10-14): racks should be one thing, whether a rack holds an instrument or effects, "together in the same place", beside Favourites, Instruments and Audio effects.
- The rail (the browser at compact widths) showed an icon for each part (Search, Favourites, Instruments, Audio effects, Plugins, Places) at the size of the transport's buttons, while the open browser showed none: the two didn't correspond. The product owner asked for smaller rail icons, and for the same icons in the open browser: the magnifier in the search field, and each part's icon before its name, the star before Favourites.

## Decision
- **Racks are a part of the browser's tree of their own:** Favourites, Instruments, Audio effects, Racks, Plugins, then Places. All racks are there together, instrument racks and effect racks alike, the Library's first, then yours, each A to Z. The rail gains Racks, with an icon of its own (two modules stacked in a frame). Search still finds them as RACKS, and a rack still goes where a module would (an instrument rack in place of the track's instrument).
- **Every part of the browser shows one icon, the same in the rail and in the open browser,** at the list icon size (`shell.listIconSize`, as a tree row's disclosure and star are): before a part's name in the tree (the star before Favourites, the note before Instruments, the knob before Audio effects, the rack before Racks, the plug before Plugins, the folder before PLACES), and alone on its rail button, smaller than before.
- **Every search field shows the magnifier at its left,** the browser's and every other in Segno (the command palette, Settings, the Key popover, the lane picker, the preset list): one shared search field, so a search looks the same wherever it is.

## Consequences
- A rack is found in one place, whatever it holds; the kind of rack no longer decides where it's listed.
- The rail and the open browser read as the same thing at two sizes; a part's icon is learned once.
- The tree's indent becomes an icon and its gap (16 px), so a row's disclosure sits under its parent's icon or name, and children line up under the name of a part that has an icon.
