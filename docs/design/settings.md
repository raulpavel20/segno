# Settings: design (M3 step C1)

How Segno keeps what you choose once, for every project: your audio interface, MIDI inputs, recording, where your files live, plugin folders, export defaults, keyboard shortcuts, the interface's size and the assistants. It all goes on **one searchable page**, as [ui-principles.md §1](ui-principles.md) already says, and the "Audio settings" popover goes away into it. The decision is [ADR 0045](../decisions/0045-settings-are-one-page.md) (§15). It builds on:
- ui-principles §1: "Preferences are one searchable page, not a set of tabs. Common settings (audio device, buffer size, theme, UI scale) are also reachable from the command palette";
- the menu bar ([ADR 0040](../decisions/0040-a-menu-bar-and-everything-by-mouse.md), [menu-bar.md](menu-bar.md) §5): "Preferences arrive as Settings… (Ctrl+,), and Audio settings becomes their first section";
- the shared components: `SectionStack`, `PropertyList`, the shell metrics ([ui-foundations.md](ui-foundations.md), ADR 0023);
- what earlier designs left for Settings: export defaults ([export.md](export.md) §9), the recording file format and offsets ([recording.md](recording.md) §5, Q15), the presets folder ([presets.md](presets.md) §5), remapping shortcuts ([daw-shell.md](daw-shell.md)), the assistants' switch ([ADR 0021](../decisions/0021-mcp-server.md)).

**Status:** approved by the product owner (2026-10-11), with every recommendation of §13 but one: no reset button per setting (S3, §6). Built the same day, for review; what was settled while building is in §17.

## 1. Why

What you set once has no home yet, or a poor one:
- **Audio settings** is JUCE's stock device selector in a callout: none of Segno's look, no buffer latency in milliseconds, nothing about MIDI or recording.
- **Recording offsets** (audio and MIDI, ±500 ms) exist in the engine with no control at all; the recording **file format and bit depth** were promised "in Audio settings" and never got one.
- **Export** can't remember defaults for new projects: there was nowhere to keep them.
- **Where your files live** (`Documents/Segno`, its Presets and Unsaved projects) is fixed.
- **Plugin folders** are the system's defaults, with no way to add one.
- **Shortcuts** can't be changed, so actions without one (the preset commands) stay without.
- **The assistants' switch** lives only in the Assistants popover.
- **The interface's size** follows the system, with no way to make it larger or smaller.

## 2. Where the field is (2026)

**DAWs put preferences in a window of tabs, mostly with OK and Cancel:**
- **Live 12:** a Settings window of ten tabs (Display & Input, Theme & Colors, Audio, Link, Tempo & MIDI, File & Folder, Library, Plug-Ins, Record Warp & Launch, Licenses & Updates). No search, no reset button.
- **Logic:** a Settings window of panes with sub-tabs; saved automatically; no search.
- **Fender Studio Pro** (formerly Studio One): an Options dialog of tabs, plus an Advanced tab of more tabs; Apply, then OK.
- **Cubase:** two dialogs, Studio Setup for the hardware and Preferences for behaviour; a page list, OK / Apply / Cancel, a Defaults button per page, and preference presets. Users have asked for search since 2025.
- **REAPER:** a tree of pages with a **find box** that also searches each option's help text, which shows at the window's foot on hover; export of chosen parts as a zip; plain-text `reaper.ini`.
- **Bitwig:** the only one in the main window, as an overlay of the Dashboard; pages Behavior, Audio, Controllers, Synchronization, Shortcuts, User Interface, Recording, Locations, Plug-ins. Bitwig 6 also puts each editor's own options in that editor.

**Outside DAWs, search leads:**
- **VS Code:** a searchable page over `settings.json`, applied as you change it; a contents tree on the left; a mark on every changed setting with Reset beside it; an `@modified` filter; a notice with a Restart button right after a setting that needs one.
- **macOS System Settings:** a sidebar and a search field whose matches list as you type.
- **JetBrains:** a tree with search that narrows to the matches; settings that need a restart say so inline.

**Shortcuts:**
- Logic, Cubase, REAPER, Studio Pro, Bitwig, VS Code and JetBrains all search by name, and most by pressing the keys (Logic's "Pressed", Cubase's "Press Key", REAPER's "Find shortcut…", VS Code's Record Keys).
- A key already in use: Logic and Cubase ask to replace (Logic can also keep both), Studio Pro shows its current use with a Show link, VS Code counts the commands on it.
- Logic opens its key editor at a command from a Control-click on the menu item.
- Live 12 has no command-shortcut editor at all, only Key Map Mode for controls.
- Key maps from other DAWs: Studio Pro ships some; Segno won't (ADR 0008).

**Audio:** everyone shows the driver, devices, rate and buffer; Live and Logic show the latency it makes; Live has a test section; Bitwig keeps the engine running while it switches interfaces and offers to switch to one just plugged in. On Linux, Bitwig supports ALSA, JACK and PipeWire; REAPER, ALSA and JACK (PipeWire through its JACK). **Segno's build has JACK off** (JUCE's default), so on Linux it reaches PipeWire only through ALSA (§13, S11).

**Storage:** per user, in the system's place: REAPER `~/.config/REAPER/reaper.ini` (plain text); VS Code and JetBrains under `~/.config` on Linux, `Application Support` on macOS, `%APPDATA%` on Windows.

Sources: the [Live 12 manual](https://www.ableton.com/en/live-manual/12/first-steps/), [Bitwig's Dashboard](https://www.bitwig.com/userguide/latest/the_dashboard/), [Logic's settings](https://support.apple.com/guide/logicpro/logic-pro-settings-overview-lgcp53689108/mac) and [key commands](https://support.apple.com/guide/logicpro/assign-key-commands-lgcp41ff6979/mac), the [Fender Studio Pro manual](https://fenderstudiopromanual.fender.com/en/Content/Setup_Topics/General_Options.htm), [Cubase's preferences](https://archive.steinberg.help/nuendo/v11/en/cubase_nuendo/topics/preferences/preferences_the_preferences_dialog_r.html), the [REAPER 7 guide](https://www.reaper.fm/userguide/ReaperUserGuide781c.pdf), [VS Code's settings](https://code.visualstudio.com/docs/configure/settings) and [key bindings](https://code.visualstudio.com/docs/configure/keybindings), [macOS search](https://support.apple.com/guide/mac-help/find-system-settings-mchl8d10839d/mac), [JetBrains' settings](https://www.jetbrains.com/help/idea/settings-preferences-dialog.html).

**Segno** takes the modern pattern most DAWs haven't: one searchable page in the main window, applied as you go, help text under every setting, and a filter for what you've changed.

## 3. Principles

1. **One page you can search** (ui-principles §1). Every setting on one scrolling page, in sections, with a search field at its top that narrows the page to what matches. An index of the sections down its left side scrolls to them; it's not tabs.
2. **A change takes effect as you make it.** No OK, Apply or Cancel. Closing keeps everything as it is.
3. **Not modal.** Settings take the centre of the window, where the arrangement and the mixer are; the top bar and the transport keep working, so you can play while you set the buffer. Closing them brings back the view you came from.
4. **Also where you'd look for it.** A setting with a natural place elsewhere stays there too, and its place there opens this page at that row: the top bar's settings button, the Record button's menu, the Assistants popover, the command palette.
5. **Yours, not the project's.** Settings are per user and the same for every project. What belongs to a song (tempo, its export folder) stays in the project.
6. **A file you can read.** One JSON file per user, in the system's place for it.
7. **Nothing hides that you need on day one.** The defaults work without visiting Settings; the first launch still asks for nothing but the audio device when there's no working one.

## 4. Where it opens

```
┌ title bar ──────────────────────────────────────────────────────────────────────────┐
├ top bar (transport, as always) ─────────────────────────────────────────────────────┤
├ Browser ┬ Settings ───────────────────────────────────────────────────────┬ Inspector ┤
│         │  ⌕ Search settings                                         ✕   │           │
│         │  ┌──────────┬─────────────────────────────────────────────┐   │           │
│         │  │ Audio    │ AUDIO                                       │   │           │
│         │  │ MIDI     │  Output        Scarlett 2i2         ▾      │   │           │
│         │  │ Recording│  Input         Scarlett 2i2         ▾      │   │           │
│         │  │ Library  │  Sample rate   48 kHz               ▾      │   │           │
│         │  │ Plugins  │  Buffer        256 samples · 5.3 ms ▾      │   │           │
│         │  │ Export   │  ───────────────────────────────────────    │   │           │
│         │  │ Keys     │ MIDI                                        │   │           │
│         │  │ Display  │  …                                          │   │           │
│         │  │ Assistants                                             │   │           │
│         │  └──────────┴─────────────────────────────────────────────┘   │           │
└─────────┴────────────────────────────────────────────────────────────────┴───────────┘
```

- **In the centre,** in place of the arrangement or the mixer, as a third view beside them. The browser and inspector stay as they were; the detail panel hides while Settings show (nothing there acts on them).
- **Open:** Options > Settings… (Ctrl+,), the top bar's settings button, the command palette. On macOS, the app menu's Settings… (Cmd+,).
- **Close:** the ✕, Escape (when no field is being typed in), Settings… again, or switching to Arrange or Mix.
- **The page:** a search field across the top; the index of sections at the left (`shell.settingsIndexWidth`); the sections in one column to its right, at most `shell.settingsColumnWidth` wide, so rows stay readable on a wide screen.
- **Each setting** is a row on the shell's row grid (`PropertyList`): its name, its control, and under them one line saying what it does (`Caption`, `textSecondary`), so nothing needs a tooltip to be understood.

## 5. Search

- **Typing narrows the page** to the settings whose name, description or section match every word, and the sections holding them. The index dims sections with nothing left.
- **Matches are marked** in the names (`textHighlight` behind the matched letters).
- **Keys** search too: typing "ctrl+b" or pressing keys while the Keys section's search is focused finds the action on them (§7, Keys).
- **Changed only:** a view option beside the search shows only the settings you've changed from their defaults.
- **Nothing found:** an empty state, "No setting matches", and the search stays.
- **From the command palette:** each setting is listed as "Settings: Buffer size" and opens the page at its row, marked for a moment.

## 6. Applying, resetting, and what restarts

- **At once:** every change is applied and saved as you make it.
- **Audio:** changing the device, rate or buffer restarts the audio for a moment; playing goes on from where it was.
- **Reset** (revised with the product owner: no button per setting, which would be a great many):
  - each choice marks its default, "48 kHz (default)", and a number's description gives it, so going back is choosing it;
  - a section's title row shows one **Reset** only while something in the section differs from its defaults;
  - Keys has one **Reset all shortcuts**, shown the same way.
- **Nothing needs a restart.** The one setting that would (the interface's size, on some systems) applies to new windows at once and to the main window after a relaunch, and says so under its row.
- **Not in the project's undo:** settings are yours, not edits to the song.

## 7. Sections and their settings

| Section | Setting | Control | Default |
|---|---|---|---|
| **Audio** | Driver | The system's: ALSA and JACK on Linux (PipeWire through either; S11), Core Audio, WASAPI and ASIO | JACK where it runs, else ALSA (Linux) |
| | Output device / Input device | Choices | The system's default |
| | Output channels / Input channels | Which pairs are used (toggles per pair) | The first pair |
| | Sample rate | Choices the device supports | 48 kHz |
| | Buffer size | Choices, each with its latency ("256 samples · 5.3 ms") | 256 |
| | Test tone | A button: plays a short tone on the output | – |
| | Load | Readout: CPU load and dropouts since opening | – |
| **MIDI** | Inputs | Each MIDI input: on or off (an off one is ignored everywhere) | All on |
| | Computer keyboard | The keyboard as MIDI on at launch | Off |
| **Recording** | File format | WAV, AIFF, FLAC | WAV |
| | Bit depth | 16, 24, 32-bit float | 24 |
| | Count-in | Off, 1 beat, 2 beats, 1 bar, 2 bars (also on the Record button) | 1 bar |
| | Audio offset / MIDI offset | ms, −500 … 500: for interfaces that report their latency wrongly | 0 |
| **Library** | Segno folder | Where presets, unsaved projects and, later, samples live; "Move…" moves its contents | `Documents/Segno` |
| | New projects | Where Save as… starts | The Segno folder |
| | Browser places | Folders shown in the browser (C2) | – |
| **Plugins** | Folders | The folders scanned for VST3 and LV2, beyond the system's; Add…, Remove | The system's |
| | Scan | "Scan for plugins" (also Options > Scan for plugins), with the count found | – |
| | Skipped | Plugins that crashed a scan, with "Try again" | – |
| **Export** | Defaults for new projects | The export panel's options: format, bit depth, sample rate, dither, normalising | The last ones used (export.md §9) |
| **Keys** | Every action's shortcut | The action list (§8) | The defaults |
| **Display** | Interface size | 75 %, 100 %, 125 %, 150 %, 175 %, 200 %, or Automatic | Automatic (the system's) |
| | System title bar | Linux and Windows (also Options > Use the system title bar) | Off |
| **Assistants** | Assistants | On or off (also the Assistants popover) | Off |
| | Read only | Assistants can look but not change | Off |
| | Files assistants may read | Any you can, or chosen folders (open question B18) | Any you can |

- **Theme:** one (Steel) until the light theme (M7); no setting until there's a choice.
- **Recording file format** applies to new recordings; existing files stay as they are.

## 8. Keys

```
│ KEYS                                                                    │
│  ⌕ Search actions or press keys                                        │
│  Edit                                                                   │
│    Undo                                    Ctrl+Z                       │
│    Duplicate                               Ctrl+D                       │
│  Module                                                                 │
│    Save preset                             (none)                       │
│    Previous preset                         Alt+,      (default: none)   │
```

- **Every action,** grouped by its menu, with its shortcut. Search by name, or press the keys to find what's on them.
- **Changing one:** click its shortcut, press the new keys. Escape cancels; Backspace clears it.
- **A conflict** shows in place: "Ctrl+D is Duplicate's: Replace?". Replacing takes it from the other action; the other is left without.
- **The menus, tooltips and the command palette** show the shortcuts as you set them.
- **Reset all shortcuts** in the section's title row, shown only while some differ from the defaults. A single shortcut goes back by setting its default key again; a changed one shows its default as its caption.
- **No other DAW's key map** (ADR 0008 keeps other vendors' names out of the UI).

## 9. Storage

- **One file:** `settings.json` in the system's place for app settings:
  - Linux: `$XDG_CONFIG_HOME/Segno/` (usually `~/.config/Segno/`);
  - macOS: `~/Library/Application Support/Segno/`;
  - Windows: `%APPDATA%\Segno\`.
- **What moves in:** `app.json` (Open recent, the title bar option) and `assistants.json` (on, read only) become sections of it, read once and removed. The engine's own `Settings.xml` (the audio device it opened, the plugin list) stays where tracktion keeps it: an implementation detail Settings writes through.
- **Readable, tolerant:** what's missing or malformed takes its default; an unknown key is kept, so an older Segno doesn't drop what a newer one wrote.
- **Tests** use a folder of their own (as with presets).

## 10. Menus, keys and actions (ADR 0040)

- **Options > Settings…** (Ctrl+,) replaces Audio settings…. On macOS it's the app menu's Settings… (Cmd+,).
- **The Record button's menu:** "Recording settings…" opens the page at Recording.
- **The top bar's settings button** opens the page (it opened Audio settings).
- **The Assistants popover** keeps its switches and gains "All assistant settings…".
- **Options > Use the system title bar** and **Scan for plugins** stay in the menu, and are on the page too.

## 11. Assistants (MCP)

| Tool | What |
|---|---|
| `get_settings` | The settings, by section |
| `set_settings` | Changes some: recording format and offsets, count-in, export defaults, the interface's size, MIDI inputs, the audio buffer and rate |

- **Never** the assistants' own settings (on, read only, which files they may read), and never shortcuts: an assistant can't widen its own reach or change your keys.

## 12. Model and engine

- **`model::Settings`** (JUCE-free, `[fast]`): the whole file, read and written as JSON, with defaults, tolerance and the move from `app.json` and `assistants.json`; `AppSettings` grows into it.
- **`model::settingsCatalogue`**: every setting's section, name, description and search words, for the page, the search and the command palette; `model::searchSettings`.
- **`model::KeyMap`**: the shortcuts as set, conflicts, resets; the `ActionRegistry` reads its keys from it.
- **The page:** `app::SettingsView` in the centre, built from `SectionStack` and `PropertyList`, a themed audio section in place of JUCE's selector (`juce::AudioDeviceManager` underneath).
- **The Session:** recording format and offsets, count-in and MIDI inputs it already has; export defaults.

## 13. Questions and recommendations

| # | Question | Recommendation |
|---|---|---|
| S1 | **Where Settings open** | In the centre, in place of the arrangement or mixer, the top bar still playing (§4). Not a separate window, not a modal dialog |
| S2 | **One page or tabs** | One scrolling, searchable page with a section index (ui-principles §1) |
| S3 | **Applying** | At once, saved as you go; no OK, Apply or Cancel. *Revised by the product owner: no reset per setting; defaults marked in the choices, one Reset per section shown only when something differs* (§6) |
| S4 | **The sections** | Audio, MIDI, Recording, Library, Plugins, Export, Keys, Display, Assistants (§7) |
| S5 | **Interface size** | 75 to 200 % in steps, or Automatic (the system's) (§7) |
| S6 | **The Segno folder** | Movable, with its presets and unsaved projects moved along (§7, presets.md §5) |
| S7 | **Export defaults** | New projects start from the export settings used last, shown and editable in Settings (export.md §9) |
| S8 | **Keys** | Every action remappable, conflicts resolved in place, no other DAW's key map (§8) |
| S9 | **Storage** | One `settings.json` per user in the system's place; `app.json` and `assistants.json` move into it (§9) |
| S10 | **Assistants** | `get_settings` and `set_settings`, never their own permissions or your keys (§11) |
| S11 | **JACK on Linux.** Segno's build has it off (JUCE's default), so PipeWire is reached only through ALSA, without its low-latency graph or routing to other apps | Turn JACK on in the DAW and the standalone plugins (`JUCE_JACK=1`; CI already installs its headers), and prefer it where a JACK server (or PipeWire's) runs, ALSA otherwise |

## 14. Tests (Done when)

- **`[fast]`:**
  - the file round-trips; missing and malformed values take their defaults; unknown keys survive; the move from `app.json` and `assistants.json`;
  - search matches names, descriptions and sections; every setting in the catalogue has a name, a description and a section;
  - the key map: a new key, a conflict and its replacement, a reset, the defaults.
- **Integration:**
  - each setting applies at once and survives a relaunch (a new Session and shell reading the same file);
  - audio: a buffer change restarts the audio and playing goes on;
  - recording: a FLAC 32-bit setting records a FLAC 32-bit file; the offsets move a recording;
  - a remapped key runs its action, and the menus show it;
  - opened from each of its places, at the right row; Escape and switching views close it.
- **Screenshots:** the page, a search, the Keys section and a conflict, at standard and compact widths, zoomed.
- **MCP:** `get_settings`, `set_settings`, and the refusals.
- **Manual (manual-tests/M3.md):** setting up an interface from scratch, a buffer change while playing, remapping a key.

## 15. ADR 0045: Settings are one searchable page, applied as you change them

- Settings are one page in the centre of the window, beside the arrangement and the mixer: searchable, in sections with an index, never a modal dialog.
- Every change applies and is saved at once; there is no OK or Cancel. Defaults are marked in the choices; a section's Reset shows only while something in it is changed.
- Settings are per user, in one readable `settings.json` in the system's place for app settings.
- A setting with a natural place elsewhere keeps it there, and that place opens the page at its row.
- Every action's shortcut can be changed there.
- Assistants can read and change settings, never their own permissions or your keys.

## 16. What comes when

- **C1 (M3):** everything above.
- **With their steps:** browser places (C2), the plugin scanner in its own process (C5), lower-latency monitoring and the round-trip latency test (C7), MIDI learn's mappings (C8).
- **Later:** the light theme and a Theme setting (M7); importing and exporting settings; per-device MIDI controller setups.

## 17. Settled while building

- **Moving the Segno folder** moves your presets (its Presets folder). An unsaved project's recordings stay where that project uses them; new unsaved projects start in the new place. Reset moves the folder back to `Documents/Segno`.
- **Defaults marked in the choices:** 48 kHz and 256 samples where the device offers them (Audio's Reset sets those), WAV and 24-bit, 1 bar, "0 ms (default)", "Automatic (default)", "The project's (default)". On or off settings read On or Off, as the Assistants popover's switch does.
- **The interface's size applies at once** to every window (the toolkit's global scale), so there's no relaunch and no note under the row.
- **Keys:**
  - the shortcut sits at the controls' right edge, `shell.settingsKeyWidth` wide, so long action names have room; the list's rows are an item gap apart, grouped under the menu each action is in;
  - a changed shortcut shows its default under it ("Default: Ctrl+Alt+F");
  - the page sets one key per action (Record keeps Ctrl+R and F9 until you change it);
  - the search finds a shortcut by its keys typed as text ("ctrl+z"); pressing the keys into the search isn't built (open question B59).
- **While Settings show,** the shortcuts that act on the hidden arrangement or mixer (Edit, Clip, Notes, Track, Module, Automation, Project) don't run, so Delete never deletes clips you can't see; playing, the views, files and the computer keyboard still do. The menu bar's items still act.
- **Export defaults:** an export from the panel makes its settings a new project's (its folder and name left out); Settings > Export shows and changes them, dither included.
- **JACK:** on in the DAW and the standalone plugins (Linux). The first launch, with no audio device chosen yet, takes JACK when a JACK server (or PipeWire's) answers, else the system's default (ALSA); after that the engine keeps what you choose.
- **The command palette** lists every setting as "Settings: <name>", opening the page at its row, marked for a moment.
- **Assistants:** `set_settings` checks everything before changing anything, so a refused call changes nothing; read-only mode refuses it.
- **Not on the page yet:** browser places (C2); which files assistants may read (open question B18).
