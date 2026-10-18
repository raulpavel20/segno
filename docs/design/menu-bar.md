# Menu bar, title bar and the mouse: design (M2 step C10)

How Segno makes every command reachable with the mouse: **a traditional menu bar** (File, Edit, Track, Clip, Transport, View, Options, Help) that holds every action with its shortcut, shown **in a title bar of our own** on Linux and Windows, and in the system menu bar on macOS.

The decision is [ADR 0040](../decisions/0040-a-menu-bar-and-everything-by-mouse.md) (§12). It changes:
- [ui-principles.md](ui-principles.md) §1 and §8: "one click *or* one shortcut" becomes "everything with the mouse, frequent things one click away, and a shortcut for each";
- the [vision](../vision.md)'s "without opening a menu bar";
- where the wordmark sits ([ADR 0024](../decisions/0024-product-name.md)).

It builds on the action registry ([daw-shell.md](daw-shell.md), [ADR 0018](../decisions/0018-daw-app-structure.md)), the command palette (C1), the top bar's layout (`model::layoutTopBar`) and the shared components ([ui-foundations.md](ui-foundations.md)).

**Status:** §15's review changes built (2026-10-11), with the rest. Approved by the product owner (2026-10-11), with every recommendation of §11 ([ADR 0040](../decisions/0040-a-menu-bar-and-everything-by-mouse.md)). Built (2026-10-11); signed off by the product owner (2026-10-11). What building settled is noted where it applies:
- the title bar's place in the window, and the drag (§4);
- how shortcuts read, the actions without one, Clip > Fades, and the take items (§5);
- the fade row of the audit (§8);
- the settings file (§9).

## 1. Why

Trying C8 export (2026-10-11), the product owner found the export panel opened only with Ctrl+Shift+E or the command palette. That wasn't the only case:
- **Keyboard or palette only (no click route at all):**
  - the project: New, Open, Save, Save as, Export;
  - Undo and Redo, Select all;
  - the command palette itself;
  - focus mode, zoom to fit, edge fades;
  - the computer keyboard's octave and velocity;
  - cancelling a bounce;
  - and more (§8).
- **Right-click only:** the count-in's length and MIDI loop recording (the Record button's menu), and the take menu's Copy/Move to new track, Delete and Keep only this take.

**How it happened:**
- The rule was "every frequent action is one click **or** one shortcut away; every action is in the command palette" (ui-principles §1, ADR 0009/0014).
- The vision ruled out a menu bar, and nothing took its place.
- So each step shipped an action, a shortcut and a palette entry, and the click route was the part left out.

The product owner, 2026-10-11:
- "We need to align much more with the traditional way in this specific area, otherwise all users will be lost."
- "We definitely need one of those 'File Edit Tools View...' project menus that all the apps have. It can be pretty small in height."
- "Everything has to be accessible with the mouse in some way."
- On the system's title bar: is it possible to replace it with our own, with close, minimise and so on, "perhaps on the same line?"

## 2. Principles

1. **Everything with the mouse.** Every action is in the menu bar. A shortcut, the command palette or a right-click menu is never the only way. A property is also in the inspector.
2. **Traditional where people look.**
   - File comes first and Help last. File holds the project (New, Open, Open recent, Save, Save as, Export, Quit), and Edit starts with Undo and Redo.
   - Menu names and their order follow common use (§3).
   - The window's buttons are where the system puts them.
3. **One list of commands.** A menu item *is* an action from the registry: the same name, shortcut, dimming and tick as in the palette, the tooltips and the right-click menus. Where a right-click menu and a menu bar menu share items (the take menu, the Record button's menu), one builder makes both.
4. **Menus teach the keys.** Every item shows its shortcut, so a mouse user learns them gradually (ui-principles §8).
5. **Frequent things stay one click away.** The menu bar is the complete list, not the fast route. The buttons and right-click menus where you work stay, and the top bar gains Undo, Redo and the palette (§7).
6. **The menu bar is never the catch-all** (product owner, 2026-10-11, §15). Putting something in the menu bar doesn't settle where it lives:
   - a property lives in the inspector;
   - a frequent command lives on the surface it acts on;
   - cancelling lives where the progress shows.

   The menu bar then lists it too. A control earns a place on screen only if enough people use it often; otherwise the menu is its place.
7. **Shallow.** At most one level of submenus.
8. **The window's top is ours, and behaves like the system's.** On Linux and Windows, one compact row holds the menus, the project's name and the window buttons. It moves, snaps, maximises and resizes the window as the system's title bar does. The system's title bar remains an option.
9. **Quiet chrome.** The row recedes: the darkest surface, secondary text, and a lift only on hover and while a menu is open.

## 3. Where the field is (2026)

**DAWs (Windows and Linux):**

| DAW | Window's top | Menus on the window buttons' row? | Top-level menus |
|---|---|---|---|
| Live 12 | The system's title bar, with a system menu bar under it | No | File, Edit, Create, View, Playback, Navigate, Options, Help (Playback and Navigate are new in 12; their place in the order is unconfirmed) |
| Bitwig Studio 5 | Its own: project tabs, the Dashboard button, window buttons in the system's layout | No: the menus are on the next row, with the transport | A File button, Play, Add, Edit, and one menu for the selection (Time, Track, Device…); settings and help in the Dashboard |
| Studio One 6/7 | Most likely the system's, with a menu bar under it (users have asked for a dark one) | Probably not | From memory: Studio One, File, Edit, Song, Track, Event, Audio, Transport, View, Help |
| FL Studio 2024/25 | Its own: the toolbar *is* the title bar ("click on an empty area of the Toolbar to move FL Studio") | **Yes**: the menus, the title and the window buttons are panels of one strip | File, Edit, Add, Patterns, View, Options, Tools, Help |
| Cubase 13/14 | The system's (14 follows the accent colour), with a menu bar under it that can be hidden | No (asked for by users in 2024) | From memory: File, Edit, Project, Audio, MIDI, Scores, Media, Transport, Studio, Workspaces, Window, Help |
| REAPER 7 | The system's, with a menu row under it | No | File, Edit, View, Insert, Item, Track, Options, Actions, Help (the last three from older guides) |

- **On macOS**, Live, Studio One, Cubase and Logic use the system menu bar.
- Bitwig and FL Studio keep their menus in the window on macOS too. Bitwig's system menu bar is nearly empty, and users complain about it.

**Other apps:**
- **VS Code:** `window.titleBarStyle` is `custom` by default on every system, and on Linux since 1.98 (February 2025). The menus, the title and the window buttons are on one row.
  - `native` remains the fallback, for these reasons:
    - X forwarding, where the custom bar can't move the window;
    - GNOME on Wayland, where it has no shadow;
    - the missing system window menu on Linux;
    - window managers that show no buttons.
  - Switching is in the title bar's own right-click menu.
- **JetBrains IDEs:** "Merge main menu with window title" (Windows and Linux; macOS uses the system menu bar); turning it off brings back the system's title bar.
- **Chrome:** "Use system title bar and borders".

**What follows for Segno:**
- **The menu bar itself is universal.** File comes first and Help last, with Edit and View, and an Options or Tools menu for settings. Live's Options, Studio One's Transport and Track, and REAPER's Track are the closest to ours.
- **Menus on the window buttons' row is the modern apps' choice (VS Code, JetBrains, FL Studio), not most DAWs'.**
  - Most DAWs keep the system's title bar with a menu row under it, which costs a row of height and looks like the system rather than the app.
  - The product owner asked for one row, about 30 px less than a title bar plus a menu row. The risks are known, and the usual answer to them is an option for the system's title bar.
- **On Linux the risk is real.** If the app moves its own window, the window manager's snapping and tiling are lost. A borderless X11 window has no shadow, and its resize grips must sit inside it. Tiling window managers get buttons that do nothing useful, and two title bars.
  - Hence the option (§4), and a first check on GNOME and KDE before anything else is built (§13).

## 4. The window's top

**Linux and Windows (by default): a title bar of our own.**

```
┌ Title bar 28 ───────────────────────────────────────────────────────────────────────────────────────────────────┐
│ ◆ SEGNO  File  Edit  Track  Clip  Transport  View  Options  Help              My song •               –   ▢   ✕ │
├ Top bar 40 ─────────────────────────────────────────────────────────────────────────────────────────────────────┤
│ 120.00 BPM 4/4 ♪ ··|  ↶ ↷      ▶ ■ ● ⊙ ⟿ ▕ 3. 1. 1  0:04.000 ▏ ⟲ ⇉      CPU 3 % ⌨•  Arrange|Mix  ◧ ▭ ◨  ⌕ ✦• ⚙  │
└─────────────────────────────────────────────────────────────────────────────────────────────────────────────────┘
```

- **Left:**
  - the brand (the mark and the wordmark, moved here from the top bar, ADR 0024);
  - then the menu titles, `itemGap` apart, each padded by `buttonTextPadding`.
- **Centre:** the project's name, centred on the window like the top bar's centre cluster (moved in only when the sides need the room). A dot follows it while there are unsaved changes.
- **Right:** Minimise, Maximise (Restore while maximised) and Close, the full height of the row.
- **Laid out by `model::layoutTopBar`**, the top bar's pure three-section layout, `[fast]`-tested. When the window is narrow:
  - the name elides first, then the wordmark drops (the mark stays);
  - the menus and the window buttons never drop. At 1024 px all eight menus fit, with the name elided.
- **Look:**
  - the row is `surface0`, under the top bar's `surface1`;
  - menu titles are in `Field` (Inter 13), `textSecondary`;
  - hovered: `textPrimary` on `hoverWash`; open: `textPrimary` on the open menu's `surface2`, so the title and its menu read as one;
  - the name is `textSecondary`;
  - window buttons are `IconButton`-like, `shell.windowButtonWidth` wide, with the hover lift; Close lifts to `record` (red) with a `textPrimary` glyph, as on Windows.
- **Behaviour, like the system's:**
  - **Drag** the row anywhere but a menu title or a button to move the window, with the system's snapping and tiling:
    - Windows: the row answers `findControlAtPoint` with `caption`, and the system does the rest (Aero Snap, shake).
    - Linux: a press on the caption hands the move to the window manager (`ComponentPeer::startHostManagedResize` with no edge, which sends `_NET_WM_MOVERESIZE_MOVE`), so GNOME's and KDE's edge snapping and tiling work.
      - JUCE's own title bar moves the window itself, which loses all of that, and it can't drag a maximised window. So the row doesn't use it.
      - The host-managed move is a public JUCE call but untested here; the first task of C10 checks it (§13).
  - **Double-click** the caption: maximise or restore. Windows does this from the caption; on Linux the row does it.
  - **Maximise and Restore** (the button, a double-click, the right-click menu) are the window manager's on Linux (`_NET_WM_STATE_MAXIMIZED_*`), as for any app:
    - Restore returns the size and place the window had, and dragging a maximised window out of the top restores it;
    - Super+Up or a drag to the top maximises, and the button shows Restore.
    - JUCE only asks the window manager while the system's title bar is on, so a small patch asks for ours too (`cmake/patches/juce-borderless-maximise.patch`). Without it the window forgot it was maximised as soon as the window manager answered: Restore did nothing, and the outline came back at the next layout (fixed 2026-10-12).
  - **The window buttons:**
    - On Windows they answer `findControlAtPoint` as `minimise`, `maximise` and `close`, so hovering Maximise shows Windows 11's snap layouts.
    - Close is the app's quit, which asks about unsaved changes as now.
  - **Edges and corners** resize: JUCE's resizable border, handed to the window manager on Linux (`_NET_WM_MOVERESIZE`), and `size…` answers to `findControlAtPoint` on Windows.
    - On Linux the grips sit just inside the window, since a borderless X11 window has no frame outside it. They're `shell.resizeBorder` wide, with the resize cursor, and the views' edges keep clear of them.
    - A 1 px `line` outlines the window while it isn't maximised. On Linux it has no shadow (a borderless X11 window gets none), and the outline separates it from the windows behind.
  - **Windows' own behaviour:**
    - The window keeps the system's drop shadow, and isn't semi-transparent or temporary, so Win+arrows, clicking the taskbar to minimise, and maximising without covering the taskbar all work (JUCE's advice).
    - Windows 11 only offers the snap layouts the window fits: Microsoft suggests a minimum width of 500 or less. With our 1024 px minimum, halves fit on a wide screen, but thirds and quarters may not (§14).
  - **Right-click** the caption:
    - Windows shows the system's window menu (from the caption).
    - On Linux the row shows a small menu of its own: Minimise, Maximise or Restore, Close, and Use the system title bar. The system's own window menu (Move to monitor…) comes later (§14).
  - **Full screen** (View > Full screen, F11): the row stays, with its menus and name; the window buttons hide. From a maximised window it leaves maximised first (the window manager holds a maximised window to the work area) and comes back maximised.
- **Built:**
  - The row is part of the window's content, above the shell (`WindowContent`), not inside the shell, so popovers, toasts and the shell's tests keep their coordinates.
  - On Linux the window manager's move starts once a drag passes the drag threshold (`input.dragThreshold`), not on the press, so a double-click still reaches the row.
- **What JUCE gives us** (9.0.3, as pinned):
  - non-native title bars (`setUsingNativeTitleBar(false)`);
  - `Component::findControlAtPoint` (Windows hit-testing: caption, buttons, edges);
  - `ComponentPeer::startHostManagedResize` (Linux, X11's `_NET_WM_MOVERESIZE`).
  - Maximise through the window manager only with the native title bar; patched for ours (above). The JUCE sources have CRLF line endings, so the patch's hunks do too (`.gitattributes`).
  - JUCE has no native Wayland backend. Under Wayland, Segno runs through XWayland, and the window manager moves and snaps it the same way.
  - On Linux, a window without the native title bar also gets `_KDE_NET_WM_WINDOW_TYPE_OVERRIDE` from JUCE, so KDE's taskbar and tiling are checked by hand.

**The system's title bar (an option).**
- Options > **Use the system title bar** puts the system's title bar back, with its buttons and the window title.
- Our row then holds only the brand and the menus (still 28 px), and no window buttons.
- It's for tiling window managers (i3, sway), where window buttons mean little and you'd get two title bars; for X forwarding; and for anyone who prefers the system's look.
- On Linux it's also in the title bar's right-click menu, as in VS Code, so it can be found where the problem is.
- It takes effect at once (the window is recreated) and is remembered in the app's data folder.

**macOS: the system menu bar.**
- The same menus go to the menu bar at the top of the screen (`MenuBarModel::setMacMainMenu`), headed by the app menu: About Segno, Settings… (Cmd+,), Services, Hide, Quit Segno (Cmd+Q).
- The window keeps its native title bar and traffic lights. There's no in-window menu row, and the brand stays in the top bar.
- A unified title bar (content under the traffic lights) comes later (§14). JUCE can't do it without a small piece of Objective-C++, which must reapply its settings after full screen.

**The system's window title** (taskbar, window switcher) puts the document first, the convention on Windows and GNOME: "My song – Segno", with "*" while unsaved. It's "Segno – My song *" today.

## 5. The menus

**Rules:**
- **Items are actions.** Each item has its action's name and shortcut. It's dimmed while the action can't run, and ticked while a toggle is on (`ActionRegistry::addMenuItem`, as the right-click menus already do).
- **Each action is in the menu bar exactly once.** Right-click menus repeat items; the menu bar doesn't.
- **Shortcuts read the usual way** on Linux and Windows: "Ctrl+Shift+E", "Alt+↑", "Space", "Esc", "Del", "F11" (`ActionRegistry::shortcutText`). macOS uses its own symbols. Menus, tooltips and the command palette all show this form. *Built:* JUCE's own text ("ctrl + shift + E", "cursor up") was what showed before.
- **Rare actions have no shortcut** and live in the menus alone: the take items, About and the title bar option. The shell test lists them.
- **"…"** ends the name of an item that asks for more before it acts: Open project…, Save as…, Export…, Assign parameter to the selected lane…, Settings…, Rename take…, Command palette…, Assistants…
  - Action names change to match. IDs don't change.
- **Lists filled when a menu opens** (not actions): Open recent, the count-in's length, MIDI loop recording. They're built by the same builders as the right-click menus that show them.
- **At most one level of submenus.** Separators group items; a menu never starts or ends with one, and never has two in a row.
- **New actions** for C10 are marked *new*.

**File**

| Item | Shortcut | |
|---|---|---|
| New project | Ctrl+N | |
| Open project… | Ctrl+O | |
| Open recent ▸ | | *new*: the last 10 projects, newest first; ones no longer on disk are left out; "Clear the list" at the end |
| — | | |
| Save | Ctrl+S | |
| Save as… | Ctrl+Shift+S | |
| — | | |
| Collect files and save | | `file.collect` (M3 C2, [browser.md](browser.md) §12): copies outside files into the project's Samples |
| Remove unused files… | | `file.removeUnused` (M3 C2): files nothing uses, to the trash after a confirmation |
| — | | |
| Export… | Ctrl+Shift+E | |
| — | | |
| Quit | Ctrl+Q | *new* (`app.quit`); asks about unsaved changes, as closing the window does. In the app menu on macOS |

**Edit**

| Item | Shortcut | |
|---|---|---|
| Undo *step* | Ctrl+Z | Names the step ("Undo Add track", `Session::undoName`) |
| Redo *step* | Ctrl+Shift+Z | |
| — | | |
| Delete | Delete | |
| Duplicate | Ctrl+D | |
| Split at playhead | Ctrl+E | |
| — | | |
| Select all | Ctrl+A | |

Cut, Copy and Paste have their place at the top of the second group once there's a clipboard (open question B50).

**Track**

| Item | Shortcut |
|---|---|
| Add track | Ctrl+T |
| Add return track | Ctrl+Alt+T |
| — | |
| Arm selected track | Alt+R |
| Mute selected track | Alt+M |
| Solo selected track | Alt+S |
| — | |
| Add automation lane | Ctrl+Shift+L |
| Assign parameter to the selected lane… | Ctrl+Alt+Shift+L |
| Automate last touched parameter | Alt+L |

**Clip**

| Item | Shortcut | |
|---|---|---|
| New MIDI clip | Ctrl+Shift+M | |
| — | | |
| Bounce in place | Ctrl+B | |
| Bounce to new track | Ctrl+Shift+B | |
| Cancel bounces | Escape | *new* (`clip.cancelBounces`); enabled while a bounce runs. Also a × on the clip being bounced (§15) |
| Make wavetable | | *new* (`clip.makeWavetable`, [table-editor.md](table-editor.md) §5): the selected audio clip into the selected Wavetable's table editor, or a new MIDI track's Wavetable; dimmed unless one audio clip is selected |
| — | | |
| Mute clips | 0 | |
| Loop clips | | *new* (`clip.loop`, C10, [clip-loops.md](clip-loops.md) §10): a toggle, ticked while every selected clip loops, dimmed with the reason while none can. No default key (Ctrl+L and every L combination are taken); assignable in Settings > Keys. Also in the clip's right-click menu, after Mute clips |
| Crossfade | Ctrl+Shift+X | |
| — | | |
| Takes ▸ | | Show takes of the selected clip (Ctrl+Alt+K); Previous take (Alt+↑); Next take (Alt+↓); — ; Use this take for the whole clip, Rename take…, Copy take to new track, Move take to new track, Spread the selected clip's takes to new tracks (Ctrl+Alt+Shift+K); — ; Keep only this take. The take items act on the selected takes only (§15); they're *new* as actions, and Copy, Move and Keep only are also in the inspector's TAKE section. Delete and Edit > Delete delete selected takes, so there's no Delete take. *Built:* the take menu (right-click on the badge or a strip) keeps its own items, which act on the strip clicked and name it ("Keep only Take 2"), so it isn't the same builder |
Clip ▸ Notes ▸ moved to the Notes menu (M3 P4).

**Notes** (M3 P4, [piano-roll.md](piano-roll.md) §13, [ADR 0058](../decisions/0058-the-piano-roll-writes-in-key-and-in-chords.md); Alt+N): the piano roll's commands, on the selected notes or on all of them while the clip view shows a MIDI clip, dimmed with the reason otherwise. The clip view's **Notes ⌄** button opens this same menu (one builder).

| Item | Shortcut | |
|---|---|---|
| Quantise | Ctrl+U | With Quantise…'s settings |
| Quantise… | Ctrl+Shift+U | The Tools pane at Quantise ([piano-roll.md](piano-roll.md) §25): strength, swing, starts and/or ends, previewed live; Apply keeps it as one step |
| — | | |
| Transpose ▸ | | Up a step (↑), Down a step (↓): steps of the scale with Key on and a song key, else semitones; they raise and lower automation points when a lane was used last, as before. — ; Up a semitone (Ctrl+↑), Down a semitone (Ctrl+↓); — ; Up an octave (Shift+↑), Down an octave (Shift+↓) |
| Fit to key | | Dimmed, saying why, without a song key |
| — | | |
| Make chords… | | The Tools pane at Make chords: a row per note, each its own chord and voicing ([piano-roll.md](piano-roll.md) §20.3, §25) |
| Chords ▸ | | Change chord… (the Chord popover for the selected chord; dimmed unless the selection is one chord); — ; Invert up (Alt+Shift+↑), Invert down (Alt+Shift+↓); — ; Smooth, Close, Open, Drop 2, Drop 3 and Spread voicing; — ; Add bass |
| — | | |
| Join | Ctrl+J | |
| Length ▸ | | Legato, Fixed length; — ; Double speed, Half speed |
| Reverse | | |
| Mirror pitches | | |
| — | | |
| Transform ▸ | | Humanize…, Strum…, Arpeggiate…, Chop…, Ornament…, Velocity…, Span…, Randomise pitch…: each opens the Tools pane at it, previewed as it's set, Apply keeps it as one step ([piano-roll.md](piano-roll.md) §12, §23, §25; M3 P4c); dimmed without notes |
| Generate ▸ | | Rhythm…, Melody…, Chords…, Bassline… (the Tools pane at it) into the selected notes' span, else the clip's loop; the bassline dimmed, saying why, until the clip has chords |
| Re-roll chance | | The clip's notes left to chance decided again (a new seed, [piano-roll.md](piano-roll.md) §10); dimmed, saying why, while no note is |
| — | | |
| Select ▸ | | Select same pitch, Select outside the key, Select in the loop, Invert selection |
| Nudge later / earlier | → / ← | They nudge lane points too, as before |

The notes' right-click menu has Transform ▸ too, Quantise… first, as the Tools pane's tiles. Clip ▸ Mute clips (0) reads "Mute notes" and mutes them while the clip view was used last; Edit ▸ Split at playhead (Ctrl+E) splits the notes crossing it then.

**Audio** (M3 P6a, [audio-editor.md](audio-editor.md) §13, [ADR 0063](../decisions/0063-audio-is-edited-as-properties-warped-in-real-time.md); Alt+A; between Notes and Song): what acts on audio clips: the clip the audio editor shows when it was used last, else the selected audio clips; what needs the editor's range is dimmed with the reason elsewhere. The audio editor's **Audio ⌄** button opens this same menu (one builder), and the waveform's right-click menu has it as a submenu after the most used commands.

| Item | Shortcut | |
|---|---|---|
| Warp | | *P6b:* ticked while the clip follows the tempo ([audio-editor.md](audio-editor.md) §7.1) |
| Warp mode ▸ | | Beats, Tones, Repitch: the clip's ticked; dimmed, saying why, while it doesn't warp |
| Tempo ▸ | | Double, Half (the clip's tempo); — ; Detect… (the Tools pane at Tempo), Song tempo from this clip |
| — | | |
| Quantise | Ctrl+U | The transients to the grid through warp markers, with the Quantise tool's settings (§7.7) |
| Quantise… | Ctrl+Shift+U | The Tools pane at Quantise |
| Warp markers ▸ | | Markers at transients, Warp from here, Set the downbeat here, Remove markers (the editor's) |
| — | | |
| Transpose ▸ | Ctrl+↑ / Ctrl+↓; Shift+↑ / Shift+↓ | Up a semitone, Down a semitone; — ; Up an octave, Down an octave (§8.1) |
| Follow key | | *P6c:* ticked while the clip plays in the song's key (§8.3); a clip with no key found says so |
| — | | |
| Gain ▸ | | Up 1 dB, Down 1 dB: the gain line over the range while there's one, else the clips' gain; — ; Silence (the range) |
| Normalise… | | The Tools pane at Normalise (peak or loudness, each clip or together), previewed as it's set, Apply keeps it as one step |
| — | | |
| Reverse | | Ticked while the clip plays backwards |
| Invert polarity | | Ticked while inverted |
| Channels ▸ | | Stereo, Left, Right, Mono, Swapped: for stereo files, the one playing ticked; dimmed, saying why, for mono ones |
| — | | |
| Crop to range | | |
| Fade to range | | A range from the clip's start becomes its fade-in, one to its end its fade-out; dimmed, saying why, in the middle |
| Loop range | | |
| Slice… | | *P6c:* the Tools pane at Slice (§9); also on the waveform's right-click, after Reverse |

**Shared keys** (P6b): Quantise, Quantise… and Transpose's keys are the Notes menu's. Each key goes to whichever kind of clip the clip view shows: the Notes menu's action routes to the audio when it shows an audio clip. The Audio menu's items are actions of their own that show the same key (`Action::keysFrom`), so every action is still in the menu bar once. ← and → nudge the audio editor's selected markers by a grid step, as they nudge notes. Follow key and Slice… (P6c) have no keys: Follow sits beside the clip's key in the inspector, and Slice has its button on the bar. Edit ▸ Split (Ctrl+E) reads **Split at range** and splits at the range's edges while the audio editor was used last with a range; Delete takes the range out, and Select all makes the whole clip the range.

**Song** (added with the song's sections, M3 C3; the song's key first, M3 P3, [ADR 0057](../decisions/0057-the-song-has-one-key.md); [ADR 0049](../decisions/0049-the-master-row-is-the-song-row.md), [arranger.md](arranger.md) §12; Alt+G since M3 P4 gave Notes the N; Alt+S solos)

| Item | Shortcut | |
|---|---|---|
| Key… | | *new* (`song.key`, M3 P3, [key-and-scale.md](key-and-scale.md) §13): the Key popover under the top bar's key field |
| No key | | *new* (`song.noKey`): the song has no key; dimmed while it has none |
| — | | |
| Add section | | The loop if it's on, else from the playhead to the next section (or 8 bars) |
| Insert empty section | | After the selected ones, as long as the last of them |
| — | | |
| Loop section | | Also a double-click on a section's body |
| Previous section | Alt+← | |
| Next section | Alt+→ | |
| — | | |
| Select clips in section | | Everything in its time, those across its edges too |
| Duplicate section | (Ctrl+D on a section) | |
| Delete section | (Delete on a section) | With its music |
| Remove section mark | | The music stays |
| — | | |
| Section colour ▸ | | The palette |

**Module** (added with presets, [ADR 0044](../decisions/0044-presets-are-files.md), [presets.md](presets.md) §10; Alt+D)

| Item | Shortcut | |
|---|---|---|
| Previous preset / Next preset | | The selected module's |
| — | | |
| Save preset | | Writes over yours; on a factory preset, opens Save as |
| Save preset as… | | |
| Revert to preset | | While changed |
| — | | |
| Compare A/B | | Ticked on B |
| Copy A to B | | "Copy B to A" while B shows |
| — | | |
| Use as the default for a new *module* | | Names the module ("…a new Compressor") |
| Clear the default | | |
| — | | |
| Show presets folder | | |
| — | | |
| Edit wavetable | | *new* (`module.editWavetable`, [table-editor.md](table-editor.md) §8, §11): opens the table editor below the module, ticked while it's open; chosen again, closes it (as the × on its tab does). Also a double-click on the table display. Dimmed unless a Wavetable is selected, as the two below |
| Import wavetable… | | *new* (`module.importWavetable`): a table loads on the oscillator OSC shows; audio opens the way chooser |
| Export wavetable… | | *new* (`module.exportWavetable`): WAV or `.wt`, into Segno/Wavetables. Ctrl+Shift+E stays File > Export |
| — | | |
| Load sample… | | *new* (`module.loadSample`, [devices/sampler/SPEC.md](../../devices/sampler/SPEC.md) S10): a sample or an SFZ file into the selected Sampler, with what the file sets. Also a click on the Sampler's empty display, or a double-click on a loaded one. Dimmed unless a Sampler is selected |
| Group into rack | Ctrl+G | *new* (`module.groupIntoRack`, [racks.md](racks.md) §9): the selected modules (Shift-click adds along the chain) into a rack, where the first was |
| Ungroup rack | Ctrl+Shift+G | *new* (`module.ungroupRack`): the selected rack's shown chain back in the track's chain |
| Edit macros | | *new* (`module.editMacros`): the selected rack's macro editor, ticked while it's open; also a macro's right-click |
| Add chain | | *new* (`module.addChain`): to the selected rack; also its "+ Chain" and its end cap's "+" |
| Rename rack… | | *new* (`module.renameRack`): in place, in its header; also its header's right-click |
| Save rack / Save rack as… | | *new* (`module.saveRack`, `module.saveRackAs`): the selected rack into your racks, named as the rack; Save rack as… renames it first |
| Show racks folder | | *new* (`module.racksFolder`) |
| Store snapshot | | *new* (`module.storeSnapshot`, [racks.md](racks.md) §18.2): where the selected rack's macros are; also "+ Store", the rack's and a macro's right-click |
| Update snapshot | | *new* (`module.updateSnapshot`): the macros over the current snapshot; dimmed unless they've moved from it, and while Morph is on |
| Rename snapshot… | | *new* (`module.renameSnapshot`): the current one, in place; also a double-click on its name |
| Remove snapshot | | *new* (`module.removeSnapshot`): the current one; also its right-click |
| Morph | | *new* (`module.morph`, §18.3): ticked while on; dimmed until there are two snapshots; also MORPH's power dot |
| Vary / Vary more | Ctrl+Alt+V / Ctrl+Alt+Shift+V | *new* (`module.vary`, `module.varyMore`, §18.5): the selected rack's free mapped macros; dimmed while Morph is on; also the rack's right-click |
| Rename pad… | | *new* (`module.renamePad`, [drum-kit.md](drum-kit.md) §12): the selected Drum Kit's selected pad, in place on the pad; also a double-click on its name and its right-click. Dimmed for an empty pad |
| Copy pad / Paste pad | | *new* (`module.copyPad`, `module.pastePad`): the pad's sound and settings, pasted over the selected pad (any kit); also the pad's right-click; Ctrl+drag between pads copies too |
| Clear pad | | *new* (`module.clearPad`): its modules and settings go; also its right-click |
| Add MIDI effect ▸ | | *new* (`module.addMidiEffect.<id>`, [midi-effects.md](midi-effects.md) §13): Arpeggiator, Chord, Pitch, A to Z; after the selected module or at the chain's end, and a MIDI effect always lands before the track's instrument (§11). Also the browser's MIDI effects part and the chain's + |

Load sample… also loads onto a Drum Kit's selected pad when the kit is selected (drum-kit.md §12). A Drum Kit has a rack's macros (Edit macros), and none of a rack's other items: they're dimmed for it.

**Transport**

| Item | Shortcut | |
|---|---|---|
| Play / stop | Space | |
| Stop and return to start | Home | |
| — | | |
| Record | Ctrl+R | |
| Capture MIDI | Ctrl+Shift+R | |
| Automation arm | Ctrl+Shift+A | |
| Turn automation back on | Ctrl+Alt+A | Moved here from Track (§15): beside Automation arm, the pair that sets automation as a whole |
| — | | |
| Count-in | Ctrl+Alt+C | |
| Count-in length ▸ | | Off, 1 beat, 2 beats, 1 bar, 2 bars (the Record button's list, one builder) |
| MIDI loop recording ▸ | | Each lap: a new take / add to the clip (shared) |
| — | | |
| Loop | Ctrl+L | |
| Metronome | Ctrl+M | |

**View**

| Item | Shortcut | |
|---|---|---|
| Mixer view | Ctrl+Alt+X | Ticked in the mixer view |
| — | | |
| Show browser | Ctrl+Alt+B | |
| Search the browser | Ctrl+F | `browser.search` (M3 C2, [browser.md](browser.md) §14): the keyboard in its search; folded to its rail, it opens first, docked |
| Preview in the browser | | `browser.preview`, a toggle; also the Preview switch in the browser's header |
| Show inspector | Ctrl+Alt+I | |
| Show detail panel | Ctrl+Alt+D | |
| Switch modules / clip view / editor | Tab | Goes round Modules, Clip and a module's open editor ([ADR 0053](../decisions/0053-a-modules-editor-in-the-detail-panel.md)) |
| — | | |
| Show automation lanes | Ctrl+Alt+L | |
| Show returns and master | Ctrl+Alt+R | |
| Fold module | Ctrl+Alt+M | |
| Expand module sections | Ctrl+Alt+E | |
| Next module page | Ctrl+Alt+Page Down | For a module with pages ([ADR 0051](../decisions/0051-pages-for-very-wide-modules.md)); dimmed for others. With the Wavetable Synth (M3 P1) |
| Previous module page | Ctrl+Alt+Page Up | As above |
| — | | |
| Other tracks' notes ▸ | | Show other tracks' notes (the clip view's Others, [piano-roll.md](piano-roll.md) §8); — ; each other track with notes, ticked while it's shown (M3 P4b; also the Others option's right-click) |
| Clip view lane ▸ | | Velocity, Chance, Release velocity; the controllers (the clip's and the usual ones); Other controller ▸ (CC 0–127): what the clip view's bottom lane shows (§11, M3 P4b; also its head's menu). Over an audio clip: Gain, the only lane until pitch (M3 P6a) |
| Waveform height ▸ | | Taller, Shorter, Reset: the audio editor's rows, as its level column dragged and double-clicked does (M3 P6a, [audio-editor.md](audio-editor.md) §4); dimmed, saying why, while it shows no audio clip |
| Tools pane ▸ | | Transform tools, Generate tools: the clip view's Tools pane on that tab, ticked while it shows it; chosen again, it closes (the header's Transform and Generate, [piano-roll.md](piano-roll.md) §25) |
| Show only the key's notes | Ctrl+Alt+N | The clip view's In key (M3 P4, [piano-roll.md](piano-roll.md) §6, §21): only the key's rows, else the whole roll; ticked while it shows the key's rows; dimmed without a song key. On a Drum Kit's clip it reads **Show only the kit's pads**, the header's Pads ([drum-kit.md](drum-kit.md) §23.3); there, what drums don't need is dimmed, saying why: the key's and the chords' items, Draw ▸, Other tracks' notes, and the tools that don't work on drums (§23.4) |
| Show other tracks' notes | | The clip view's Others (§8) |
| Draw ▸ | | Draw note, Draw chord, Draw chord under: the clip view's Draw (§7.2); — ; Draw triads, Draw 7th chords, Draw 9th chords, Draw sus2, sus4, add9 chords; — ; Voiced smooth, close, open, drop 2, drop 3, spread: the kind and voicing chords are drawn with (§20.2) |
| — | | |
| Zoom in | = | |
| Zoom out | − | |
| Zoom to fit | Ctrl+0 | |
| Follow playhead | Ctrl+Shift+F | |
| — | | |
| Focus mode | Ctrl+Alt+F | |
| Full screen | F11 | *new* (`view.fullScreen`); Ctrl+Cmd+F on macOS |

**Options**

| Item | Shortcut | |
|---|---|---|
| Narrower grid | Ctrl+1 | *new* ([ADR 0047](../decisions/0047-the-editing-grid-in-each-editor.md)): the editor used last; the next width, or half the fixed value |
| Wider grid | Ctrl+2 | *new*: the opposite |
| Triplet grid | Ctrl+3 | *new* |
| Snap to grid | Ctrl+4 | Now the editor used last's |
| Fixed grid | Ctrl+5 | *new*: fixed at the step showing, or back to adaptive |
| Edge fades | Ctrl+Alt+Shift+E | |
| Reduced latency when monitoring | | *new* (`project.reducedLatency`, M3 C7, [recording.md](recording.md) §22.1): a tick, kept with the project; also on Settings > Recording |
| — | | |
| Computer keyboard ▸ | | Computer keyboard as MIDI (Ctrl+Shift+K); — ; Octave down (Z), Octave up (X), Velocity down (C), Velocity up (V): *new* as actions, enabled while it plays (today the keys only) |
| MIDI map mode | | *new* (`midi.mapMode`, M3 C8, [midi-learn.md](midi-learn.md) §3): a tick; no default shortcut (Ctrl+M is the metronome's) |
| — | | |
| Settings… | Ctrl+, | `app.settings` (M3 C1, [settings.md](settings.md), ADR 0045; it was Audio settings…, `app.audioSettings`). In the app menu on macOS. Recording settings… in the Record button's menu opens the page at Recording |
| Scan for plugins | Ctrl+Alt+P | |
| Assistants… | | |
| — | | |
| Use the system title bar | | *new* (`app.systemTitleBar`); Linux and Windows |

*Built (M3 C1):* Settings… replaced Audio settings…; audio is the page's first section.

**Help**

| Item | Shortcut | |
|---|---|---|
| Command palette… | Ctrl+K | Search every action by name |
| — | | |
| About Segno | | *new* (`app.about`): a popover with the version; the licence notice (AGPL-3.0-or-later, no warranty) and where the source is, which the licence asks interactive programs to show; the third-party credits. In the app menu on macOS |

**Keyboard access:**
- F10 opens File; the arrows move between menus and items, Enter runs, and Escape closes.
- Alt+letter opens a menu (Windows and Linux): **F**ile, **E**dit, **T**rack, **C**lip, **N**otes, Son**g**, Mo**d**ule, Trans**p**ort, **V**iew, **O**ptions, **H**elp. These letters avoid the Alt shortcuts already taken (Alt+R, Alt+M, Alt+S, Alt+L).
- While Alt is held, the letters are underlined.
- The computer keyboard ignores Alt, as it does now.

## 6. Menus and the rest of the shell

- **Popovers:** opening a menu closes an open popover (the palette, export, assistants), as any click outside it does. A popover opened from a menu (Export…) opens as it does from its key.
- **Text fields:** while one has focus, the menu bar still works by mouse; its shortcuts go to the field (as now).
- **Recording and playback:** menus never stop the transport. Choosing an item is the same as pressing its key.
- **Toasts:** an item that can't run is dimmed, so it can't be chosen. Its reason (`whyNot`) shows when its shortcut is pressed, as now.
- **Assistants:** the menu bar changes nothing for MCP. Every action it reaches already has its tool or is window state (§10).

## 7. The top bar

- **The brand** moves to the title bar on Linux and Windows. That leaves room in the top bar for:
  - **Undo and Redo** (`Icon::Undo` and `Icon::Redo`, drawn already but never placed): `IconButton`s after the song group in the left section. Their tooltips name the step ("Undo Add track (Ctrl+Z)"), and they're dimmed when there's nothing to undo.
  - **A search button** (`Icon::Search`, *new*) that opens the command palette, first in the app group (search, assistants, settings). Its tooltip is "Command palette (Ctrl+K)".
- **No Export button:** File > Export…, where every DAW surveyed keeps it, and Ctrl+Shift+E. Exporting is a once-a-session action, and the top bar has no room to spare at 1024 px.
- **Dropping when narrow:** the time, the CPU load, the panel toggles, then Undo and Redo. The search button never drops.
- **On macOS** the brand stays in the top bar. Undo, Redo and search are the same as on Linux and Windows.

## 8. Everything with the mouse: the audit

**What a key or a right-click alone reached** (found 2026-10-11), and its mouse route after C10:

| What | Today | After C10 |
|---|---|---|
| New, Open, Save, Save as | Keys, palette | File |
| Export | Ctrl+Shift+E, palette | File > Export… |
| Undo, Redo | Keys, palette | Edit, and top bar buttons |
| Select all | Ctrl+A | Edit |
| The command palette | Ctrl+K | Help, and the top bar's search button |
| Focus mode, zoom in/out/to fit, follow, snap, the grid, edge fades, fold and expand modules | Keys, palette (some have buttons) | View, Options |
| Show takes, previous/next take, spread takes | Keys, the badge, the take menu | Clip > Takes |
| Notes: quantise, transpose, nudge | Keys only (the piano roll has no right-click menu) | Clip > Notes; and a right-click menu on notes and points: Quantise, Duplicate, Delete |
| Assign parameter, automate last touched, turn automation back on | Keys, palette, some right-click | Track |
| The computer keyboard's octave and velocity | Z/X, C/V only | Options > Computer keyboard; the keyboard button's right-click menu |
| Cancel bounces | Escape only | Clip > Cancel bounces; a bouncing clip's right-click menu |
| Count-in length, MIDI loop recording | The Record button's right-click menu | Transport |
| Take: rename, copy/move to a new track, delete, keep only this take | The take menu (right-click) | The inspector's TAKE (name; Copy, Move, Keep only); Delete; Clip > Takes (§15) |
| Crossfade shape, Before/Centred/After, remove a crossfade; straighten or remove a fade | Right-click on the fade (the inspector has only the fades' lengths) | The inspector's CLIP: each fade's length and curve, each crossfade's shape (None removes it) and position (§15) |
| Reset a control to its default | Right-click, double-click | Unchanged: double-click is the mouse route |
| Mixer: select the next strip | ←/→ | Unchanged: click the strip |

**The audit is finished when C10 is built:**
- Every right-click menu item and every key handler outside the registry (`keyPressed` in the shell, the mixer and the popovers) is checked against this table.
- What's missing goes in the menu bar or the inspector.

**From now on:**
- Every design doc says where its feature sits in the menu bar.
- Its shell test opens that menu.
- The registry test fails on an action that isn't in the menu bar (§13).

## 9. Code

- **`model::MenuBar`** (`app/src/model/MenuBar.{h,cpp}`, JUCE-free):
  - The menus as data: titles with their mnemonic letter. Each entry is an action id, a separator, a submenu (a title and entries), or a named list filled when it opens ("recent", "countIn", "midiLoop").
  - A macOS variant with the app menu.
  - Pure checks used by the tests: one submenu level, no empty menus, no stray separators, no id twice.
- **`model::RecentProjects`** and **`model::AppSettings`** (`app/src/model/AppSettings.{h,cpp}`): add (moves to the front), cap at 10, drop missing, clear. They're kept in the app's data folder (`Segno/app.json`, beside the assistants' settings), as JSON, with the title bar option. Without a settings file (tests) nothing is kept.
- **`ShellMenus`** (`app/src/ui/ShellMenus.{h,cpp}`, owned by the shell): a `juce::MenuBarModel` over `model::MenuBar`.
  - Each `juce::PopupMenu` is built with `ActionRegistry::addMenuItem` (name, shortcut, dimmed, ticked).
  - The named lists: Open recent (the shell's), and the builders the Record button's menu uses (`TransportBar::countInMenu`, `midiLoopMenu`); Clip > Fades (the shell's, from the selection).
  - On macOS it's set with `setMacMainMenu`.
- **`WindowContent`** (`app/src/ui`): the title bar over the shell (the shell alone on macOS), and the hit-testing the window asks for.
- **`TitleBar`** (`app/src/ui`): the brand, a `juce::MenuBarComponent`, the name and `ui::WindowButtons`, laid out by `model::layoutTopBar`. It reports where its caption and buttons are for `findControlAtPoint`.
- **`ui::WindowButtons`** (`modules/fd_ui/shell`): minimise, maximise/restore and close, with the shared look.
- **`MainWindow`** (`Main.cpp`):
  - Uses no native title bar, and JUCE's own title bar is 0 px high, so the content is `TitleBar` over the shell.
  - `findControlAtPoint` answers from the title bar and the window's edges.
  - On Linux, a press on the caption calls `startHostManagedResize`, and a double-click toggles maximise.
  - "Use the system title bar" switches `setUsingNativeTitleBar` and hides the window buttons and the name.
- **The LookAndFeel** draws the menu bar from tokens (`drawMenuBarBackground`, `drawMenuBarItem`, `getMenuBarFont`). Popup menus already use it.
- **New tokens** ([ui-foundations.md](ui-foundations.md), added when built):
  - `shell.titleBarHeight` = 28;
  - `shell.windowButtonWidth` = 40;
  - `shell.resizeBorder` = 4 (the resize grips inside the window's edges on Linux);
  - icons `Minimise`, `Maximise`, `Restore` and `Search` (`Close` exists).
- **New actions:** `app.quit`, `app.about`, `app.systemTitleBar`, `view.fullScreen`, `clip.cancelBounces`, `midi.octaveDown`/`Up`, `midi.velocityDown`/`Up`, and the take actions (`take.useWhole`, `take.rename`, `take.copyToTrack`, `take.moveToTrack`, `take.delete`, `take.keepOnly`). The take menu and the computer keyboard's keys call these actions.

## 10. Assistants (MCP)

No new tools. The menu bar is a new route to existing actions, whose features already have their tools:
- `new_project`, `open_project`, `save_project`, `export_audio`;
- undo and redo;
- the take tools, and so on.

Quit, Full screen, About and the title bar option are matters of the window, not of the project. ADR 0021's rule (every feature with its tools) is unchanged.

## 11. Decisions from the review (product owner, 2026-10-11: every recommendation agreed)

| # | Question | Recommendation |
|---|---|---|
| Q1 | **A menu bar** | Decided by the product owner (2026-10-11): yes, always shown. In the window on Linux and Windows; the system menu bar on macOS (§4) |
| Q2 | **Everything with the mouse** | Decided by the product owner (2026-10-11). Every action is in the menu bar, and a test enforces it. Right-click menus, keys and the palette are never the only route; properties are also in the inspector (§2, §8) |
| Q3 | **The menus and their names** | File, Edit, Track, Clip, Transport, View, Options, Help (§5). "Options" rather than "Tools", because what it holds are settings and modes (Live's name for it); "Tools" works as well if you prefer it |
| Q4 | **Our own title bar** (Linux, Windows) | Yes, with the menus on the same row as the name and the window buttons, as VS Code, JetBrains IDEs and FL Studio do. Most DAWs keep the system's title bar with a menu row under it (§3). It moves, snaps, maximises and resizes the window through the system (§4). If the first check (§13) shows the window manager can't run the move on GNOME or KDE, Linux starts with the system's title bar instead |
| Q5 | **The system's title bar** | An option (Options > Use the system title bar, and the title bar's right-click menu), for tiling window managers, X forwarding and anyone who prefers it; the menus keep their own row |
| Q6 | **macOS** | The system menu bar and the native title bar; a unified title bar later |
| Q7 | **Height** | 28 px (`shell.titleBarHeight`). The window's top is about 9 px shorter than GNOME's title bar plus our top bar today, and gains the menus |
| Q8 | **The brand** | Moves to the title bar's left end on Linux and Windows; stays in the top bar on macOS |
| Q9 | **Top bar buttons** | Undo, Redo and a search button for the palette. No Export button: File > Export… and Ctrl+Shift+E |
| Q10 | **Open recent** | Yes: the last 10 projects, kept in the app's data folder |
| Q11 | **About Segno** | Yes, in Help: the version, the licence notice, where the source is, and the third-party credits |
| Q12 | **Keyboard access** | F10, and Alt+letter mnemonics (F, E, T, C, P, V, O, H) that avoid the Alt shortcuts in use |
| Q13 | **Full screen** | View > Full screen (F11). The menus stay; the window buttons hide |
| Q14 | **The window buttons and title** | Minimise, maximise and close on the right; the project's name in the middle, with a dot while unsaved. The system's window title puts the document first ("My song – Segno"). Reading the desktop's own button layout (GNOME's close-only, buttons on the left) comes later |

## 12. ADR 0040: A menu bar in a title bar of our own; everything reachable with the mouse

**Decision:**
- Every action is in a traditional menu bar, with its shortcut, so everything can be done with the mouse. Shortcuts, the palette and right-click menus are additional routes.
- On Linux and Windows the menu bar sits in a title bar of our own, on one row with the project's name and the window buttons, and behaves like the system's. The system's title bar is an option.
- On macOS the menus go to the system menu bar.
- Frequent actions remain one click away where you work, and the top bar gains Undo, Redo and the command palette.

**It amends:**
- ADR 0014's "almost no menu diving", and 0009's "one click or one shortcut" (through 0014);
- the vision's "without opening a menu bar";
- where ADR 0024's wordmark sits.

The full record is [ADR 0040](../decisions/0040-a-menu-bar-and-everything-by-mouse.md).

## 13. Tests (Done when)

- **First, before anything else** (a throwaway check): a window without the native title bar whose caption calls `startHostManagedResize` with no edge, on GNOME (Wayland, through XWayland) and KDE. It must:
  - move the window with the window manager's snapping;
  - drag it out of maximised;
  - show it in KDE's taskbar.

  If it fails, Linux defaults to the system's title bar (Q4), and the rest of C10 is unchanged.

- **`[fast]`:**
  - `model::MenuBar`: one submenu level, no empty menus, no stray separators, no id twice; the macOS variant moves About, Audio settings and Quit to the app menu.
  - `model::RecentProjects`: order, cap, dropping missing files, clearing.
  - The title bar's layout (`model::layoutTopBar` with its items) never overlaps from 1024 to 2560 px: the name elides, then the wordmark drops.
- **The shell (integration):**
  - Every action in the registry is in the menu bar exactly once, and every id in the menu bar exists.
  - Each menu builds, with items dimmed and ticked from their actions.
  - Choosing File > Export… opens the export panel, and Edit > Undo undoes.
  - The take items act on the selected take.
  - The Record button's menu and Transport share their lists.
  - Screenshots at standard and compact widths, zoomed in: the title bar, an open menu, the system title bar mode, the top bar with Undo, Redo and search.
  - Only the title bar option reaches the window: other settings (a browser category opened, a favourite) leave it alone.
- **A showing window, with a window manager** (integration; skipped without a desktop session, as on CI's bare X server): Maximise stays maximised once the window manager has answered, and Restore returns the window to its earlier bounds.
- **By hand** (manual-tests/M2.md, "Menu bar and title bar (C10)"), on GNOME (Wayland, through XWayland), KDE and Windows 11:
  - drag to move, snapping to halves and corners, double-click to maximise;
  - minimise, maximise and restore;
  - closing with unsaved changes;
  - resizing from every edge and corner;
  - Windows 11's snap layouts over Maximise;
  - full screen, and switching to the system title bar and back;
  - two monitors, 200 % scale;
  - every menu by mouse and by keyboard (F10, Alt+letter).

## 15. Each thing where you'd reach for it (review, 2026-10-11)

Asked whether every action was in its right place, or the menu bar had become a catch-all, the product owner agreed to these changes, except for two on-screen controls that don't earn their place.

**What moves to where you work:**
- **Snap** (Ctrl+4): a Snap toggle in the corner above the track headers, on the editing surface; the grid size joins it later. ~~The piano roll follows it as now.~~ The grid joined it (ADR 0047): each editor has its own Snap and step, `[● Snap] [1/16 ⌄]`, the arrangement's here and the piano roll's in the clip view's header. *Built:* in the ruler's row, where the headers' names start, its tooltip the action's. Since C11's review a view option (`ui::ViewOption`, like the mixer's Modules, Sends and Narrow), not a filled button (tracks-and-clips.md §18).
- **Quantise** (Ctrl+U): a Quantise button in the clip view's header, while it shows a clip (`PanelHeader` gains commands on the right, before its view options). *Built:* right of the "Tab switches" caption, dimmed while there's nothing to quantise.
- **Cancelling a bounce:**
  - a small × on a clip being bounced, at its progress line; it cancels the bounces running, as Escape does. *Built:* on a `surface2` chip so it reads over the notes or waveform, at the right end of the part of the clip in view, so a long clip's is never off screen (`shell.clipButtonSize`);
  - Clip > Cancel bounces stays in the menu.
  - An export running with its panel closed is cancelled from the panel: File > Export… (or Ctrl+Shift+E) shows it with Cancel. The 2 px progress line along the top bar is too thin to click.
- **Fades and crossfades** are properties, so they go in the inspector's CLIP section. Clip > Fades is removed from the menu bar, and the right-click on a fade stays as the shortcut.
  - Each fade's row shows its length and its curve (−100 … 100 %, 0 straight), dragged or typed. *Built:* an unbent fade reads "Straight", which says what the field is where a bare "0 %" wouldn't; a fade of no length has no curve to set.
  - While a crossfade meets the clip, rows for it at either end show its shape (None removes it) and where it sits against the join (Before, Centred, After), each a choice. *Built:* labelled "Xfade in" and "Xfade out", the usual short form, because "Crossfade out" ran into the value column; a position set by dragging reads "As dragged".
- **Takes:** the inspector's TAKE section gains Copy to new track, Move to new track and Keep only this take, beside "Use this take".
  - "Delete take" is removed: Delete and Edit > Delete already delete the selected takes.
  - The take commands act only on the selected takes, never quietly on the take the selected clip plays.
  - *Built:* renaming stays in the TAKE section's name field and the take menu's Rename. A double-click on a strip can't rename it: its first click already uses that take for its section (ADR 0038), so the comp would change first.
- **Turn automation back on** moves within the menus, from Track to Transport beside Automation arm, the pair that sets automation as a whole. It gets no button: the product owner judged too few people would use it often enough to earn one. The lanes' own switches stay the way to turn lanes back on.

**No on-screen controls for the computer keyboard's octave and velocity:** they stay on their keys (Z/X, C/V), in Options > Computer keyboard and in the keyboard button's right-click menu, as C10 built them. The product owner: fields beside the button "don't earn their place", and the keys are "intuitive enough".

**Found on the way:** tracks and clips can't be renamed at all; there is nothing for it in the Session yet. That's open question B52 (renaming in place, with a double-click, as ui-principles §1 promises).

## 16. What comes when

- **C10 (M2):** everything above.
- **Later:**
  - macOS's unified title bar (the content under the traffic lights), through a small Objective-C++ shim;
  - a smaller minimum window width, so Windows 11's narrower snap layouts fit;
  - reading the desktop's button layout on Linux (GNOME's close-only, buttons on the left);
  - the window menu on right-clicking the title bar on Linux;
  - a Window menu on macOS (Cmd+M is Metronome there today);
  - Help > the manual and Report a problem, once there's a website;
  - Settings… with preferences (M3), and remapping shortcuts (the menus show the remapped keys).
