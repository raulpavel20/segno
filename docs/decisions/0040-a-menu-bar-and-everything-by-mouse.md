# 0040. A menu bar in a title bar of our own; everything reachable with the mouse

- **Status:** Accepted (product owner, 2026-10-11, with every recommendation of [design/menu-bar.md](../design/menu-bar.md) §11)
- **Date:** 2026-10-11
- **Amends:**
  - [0014](0014-flat-darker-ableton-adjacent.md): "almost no menu diving", and through it [0009](0009-dark-first-design-language.md)'s "every frequent action is one click or one shortcut away; every action can be found in a command palette";
  - the [vision](../vision.md)'s "without opening a menu bar";
  - [0024](0024-product-name.md): where the wordmark sits.

  The rest of those stands.
- **Builds on:** [0018](0018-daw-app-structure.md) (the action registry), [0012](0012-ui-toolkit-juce-everywhere.md) (JUCE everywhere)

## Context
- The rule "one click **or** one shortcut; everything in the command palette", and the vision's "without opening a menu bar", let each step ship an action, a shortcut and a palette entry without a click route.
- Trying export (C8) on 2026-10-11, the product owner found it reachable only by Ctrl+Shift+E or the palette. So were New, Open, Save, Save as, Undo, Redo and the palette itself, and some commands were only in right-click menus.
- The product owner: "We need to align much more with the traditional way in this specific area, otherwise all users will be lost", and "Everything has to be accessible with the mouse in some way". They asked for a menu bar, small in height, and whether the system's title bar could be replaced by our own with the menus on the same line.

Every DAW surveyed has a menu bar headed by File and ending with Help (design §3):
- Most (Live, Cubase, REAPER, and very likely Studio One) keep the system's title bar with the menu row under it.
- Bitwig draws its own title bar but keeps the menus on the row below.
- FL Studio puts the menus, the title and the window buttons on one row, as VS Code and JetBrains IDEs do. Both keep the system's title bar as an option, for tiling window managers, X forwarding and Linux desktops where a self-drawn bar falls short.

JUCE 9.0.3 supports this:
- windows without the native title bar;
- `Component::findControlAtPoint`, so Windows moves, snaps and resizes the window and shows its snap layouts;
- `ComponentPeer::startHostManagedResize`, so on Linux the window manager moves and resizes the window (`_NET_WM_MOVERESIZE`), with its own snapping.

## Decision
- **Everything with the mouse.**
  - Every action is in the menu bar, with its shortcut beside it.
  - A shortcut, the command palette or a right-click menu is never the only way to do something, and a property is also in the inspector.
  - A test fails on an action that isn't in the menu bar.
- **A traditional menu bar:** File, Edit, Track, Clip, Transport, View, Options, Help.
  - Its items are the registry's actions (name, shortcut, dimmed, ticked), each exactly once, with at most one level of submenus.
  - File holds the project (New, Open, Open recent, Save, Save as, Export…, Quit). Edit starts with Undo and Redo.
- **A title bar of our own on Linux and Windows:** one 28 px row with the brand, the menus, the project's name and the window buttons.
  - Dragging, snapping, maximising and resizing go through the system, so the window behaves as with the system's title bar.
  - "Use the system title bar" (Options) brings the system's back, and the menus keep their row.
- **macOS:** the system menu bar, with the app menu (About, Audio settings, Quit), and the native title bar.
- **Frequent actions stay one click away** where you work. The top bar gains Undo, Redo and a search button for the command palette. Export stays in File, as in every DAW.
- **The menu bar is the complete list, never the catch-all** (review, 2026-10-11, design §15). A property lives in the inspector, a frequent command on the surface it acts on, cancelling where the progress shows. The menu bar lists them too. A control earns a place on screen only if enough people use it often; otherwise the menu is its place, as with Turn automation back on and the computer keyboard's octave and velocity.

The design is [design/menu-bar.md](../design/menu-bar.md).

## Consequences
- **Docs and rules:**
  - ui-principles §1 and §8 and the UI definition of done now require a mouse path and a keyboard path.
  - CLAUDE.md gains the rule.
  - Every design doc says where its feature sits in the menu bar.
- **New code:**
  - a JUCE-free `model::MenuBar` and `model::RecentProjects`;
  - a `juce::MenuBarModel` over the registry;
  - a `TitleBar` and `ui::WindowButtons`;
  - a main window with `findControlAtPoint`, and the host-managed move on Linux;
  - tokens and icons for the title bar.
- **New actions:** Quit, About, Full screen, the system title bar option, cancelling bounces, the computer keyboard's octave and velocity, and the take menu's items.
- **Commands that only a key or a right-click reached** get a menu bar or inspector route (design §8).
- **Moving the brand:** it moves from the top bar to the title bar on Linux and Windows, which leaves room for Undo, Redo and search.
- **Window behaviour** must be checked by hand on GNOME, KDE and Windows 11. A custom title bar is where apps most often feel "off", so the system title bar option stays as the fallback.
- **C10 starts by checking on GNOME and KDE** that the window manager runs the move (JUCE's own title bar moves the window itself, losing snapping). If it doesn't, Linux starts with the system's title bar and the menus on their own row.
- **No new MCP tools:** the menu bar is a route to existing actions.
