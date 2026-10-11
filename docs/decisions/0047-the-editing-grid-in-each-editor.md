# 0047. The editing grid: adaptive or fixed, in each editor

- **Status:** Accepted (product owner, 2026-10-12)
- **Date:** 2026-10-12
- **Builds on:** [0023](0023-ui-foundations.md) §5 (one time-view kit), [0040](0040-a-menu-bar-and-everything-by-mouse.md) (the menu bar, everything by mouse), [0021](0021-mcp-server.md) (assistants); [daw-shell.md](../design/daw-shell.md) ("Timeline", "Piano roll"); [menu-bar.md](../design/menu-bar.md) §15 ("the grid size joins [Snap] later")

## Context
- The grid adapts to the zoom with a spacing fixed in code: lines at least 10 px apart in the arrangement, 56 px in the piano roll. Snapping uses the step you see, and a double-click in the piano roll draws a note one step long, so the note length you get depends on the zoom: finding a sixteenth means zooming in or out.
- The only control is Snap on or off, and the piano roll copies the arrangement's.
- In the field: Live's grid is zoom-adaptive (Narrowest, Narrow, Medium, Wide, Widest) or fixed (8 bars … 1/32), with triplets and snap, set from each editor's right-click menu, the Options menu and Ctrl+1 to Ctrl+5; each editor shows its current spacing. Logic's Snap menu offers Smart (zoom-adaptive) or a fixed division, and its piano roll can have its own.
- The product owner wants Live's settings, independent in the arrangement and the piano roll.

## Decision
- **Each editor has its own grid:** the arrangement and the piano roll each have a mode (adaptive or fixed), an adaptive width, a fixed value, triplets and snap. Changing one never changes the other. The piano roll has one grid for whichever clip it shows.
- **Adaptive widths** set how far apart the lines must be: Narrowest 7 px, Narrow 14, Medium 28, Wide 56, Widest 112; each is one step coarser than the last at the same zoom. The arrangement starts at Narrow, the piano roll at Wide.
- **Fixed values:** 8, 4, 2 and 1 bars, 1/2, 1/4, 1/8, 1/16, 1/32; the same at every zoom. Lines too dense to draw are thinned on screen; snapping keeps the value.
- **Triplets** turn every step shorter than a bar into its triplet (1/8 becomes 1/8T).
- **Lines below a bar restart at every bar,** so triplets and odd metres stay on the bar lines; snapping uses the lines drawn.
- **Where:** each editor shows its Snap and its step ("1/16", "1/8T", "1 bar") as view options on its surface; the step opens the grid's menu. The same items are in the Options menu with Live's keys (Ctrl+1 narrower, Ctrl+2 wider, Ctrl+3 triplets, Ctrl+4 snap, Ctrl+5 fixed), acting on the editor used last, and in each editor's right-click menu.
- **View state:** saved with the project, never an undo step.
- **Assistants** read and set each editor's grid (`get_grid`, `set_grid`).

## Consequences
- The piano roll no longer follows the arrangement's snap; a project saved before keeps its snap in both.
- New: `model::GridSettings`, a `Timeline` that takes it (grid arithmetic in 24 ticks per quarter note, so triplets are exact), a shared `ui::ViewChoice` (a view option that shows a value and opens a menu), four Options commands and two MCP tools.
- A drawn note's length, nudge, duplicate and quantise follow the piano roll's grid as before, now set rather than zoomed to.
