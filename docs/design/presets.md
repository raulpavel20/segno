# Presets: design (M3 step A1)

How Segno keeps a module's settings to use again: **presets**, found and saved in the module's own header, the same in the plugins and the DAW, with **A/B compare** and a **default for new modules**. The decision is [ADR 0044](../decisions/0044-presets-are-files.md) (§16). It builds on:
- the device state format, shared by plugin state, projects and presets, frozen by [ADR 0034](../decisions/0034-device-api-frozen.md): `{device, stateVersion, params, extra}` with plain values;
- non-parameter state ([ADR 0017](../decisions/0017-heavy-data-and-non-parameter-state.md)) and the host binding: modules never write their own parameters;
- the module panel kit ([ADR 0022](../decisions/0022-module-panels.md), [device-panels.md](device-panels.md)) and [ui-principles.md §7](ui-principles.md): "one preset browser per module header, with search, tags, favourites and A/B compare";
- the menu bar as the complete list ([ADR 0040](../decisions/0040-a-menu-bar-and-everything-by-mouse.md)), and assistants' tools ([ADR 0021](../decisions/0021-mcp-server.md));
- the M3 plan's answers: user presets in one folder shared by the plugins and the DAW (Q3); factory presets drafted by Claude and curated by the product owner (Q2, B4).

**Status:** approved by the product owner (2026-10-11), with every recommendation of §15. Claude builds and names the factory presets; the product owner signs them off by ear (§13). Built and signed off (2026-10-11), with the factory sets for every module but the Sampler (§21).

## 1. Why

- No module has presets yet. The quality bar says a module without good presets isn't done (device-suite.md), so all nine M1 and M2 modules are waiting, and every M3 module needs them from its first day.
- The browser (C2) lists presets with everything else, so presets come first.

## 2. Where the field is (2026)

**In the module's header.** Every DAW and plugin maker puts the preset in the header, with previous and next:
- **Plugin makers:** FabFilter (a preset button with the name, ◀ ▶, a browser with search, tags and favourites), Surge XT (prev/next, a heart, search) and Native Instruments (factory and user submenus, ◀ ▶).
- **DAWs:** Logic's Settings menu, Studio One's (now Fender Studio Pro) and Cubase's preset fields. Live has a Save Preset button and Hot-Swap.
- **Browsing:**
  - Bitwig opens a pop-up browser filtered to the device. Selecting a preset there loads it at once, so you hear it as you go.
  - Live and Bitwig also list presets in the main browser.

**What a preset carries:** the converging set is a name, a creator, free-form tags, favourites and a description.
- CLAP's preset discovery has hosts map a plugin's free tags into their own.
- Categories exist too (Bitwig, Vital's nine styles, Cubase's MediaBay attributes), as do colour labels and smart folders (u-he).
- Hardly anyone has ratings.

**Factory and user:** everyone keeps factory presets read-only, and restorable, apart from the user's.
- User presets are files in a visible folder, mostly under Documents:
  - FabFilter: `Documents/FabFilter/Presets/<Product>`;
  - Surge XT: `Documents/Surge XT`, from XDG's documents folder on Linux;
  - Live's User Library is in Documents on Windows;
  - Bitwig's library is in Documents.
- The formats' own folders (VST3's `~/.vst3/presets`, `~/Library/Audio/Presets`) suit host-saved presets more than a suite's own.

**"Modified":** few show it.
- Surge XT adds an asterisk.
- FabFilter dims the name.
- Logic lights its Compare button.

**A/B compare:** common in plugins, rare in DAWs.
- **Plugins:** FabFilter (A/B and Copy) and NI (the first switch to B copies A).
- **DAWs:** Live 12 (A/B on its own devices) and Cubase (A and B saved with the project).
- Live disables automation when you switch, until you turn it back on: a known annoyance.
- Logic and Studio One only compare with the saved state.

**Default for new instances:** universal.
- Live, Bitwig, Studio One and Cubase have "Save as Default Preset".
- Logic has "Save Setting As Default".
- REAPER has "Save preset as default".
- FabFilter has "Save As Default".

**Undo:** FabFilter makes a preset load one undo step. No DAW documents whether a load is one step, or what it does to automation.

Sources: the [Live 12 manual](https://www.ableton.com/en/live-manual/12/working-with-instruments-and-effects/), [Bitwig's user guide](https://bitwig.com/userguide/latest/browsers), [Logic's plug-in settings](https://support.apple.com/guide/logicpro/work-with-plug-in-settings-lgcp4dcb0092/mac), the [Fender Studio Pro manual](https://fenderstudiopromanual.fender.com/en/Content/Built-In_Effects_Topics/Chapter-Built_In_Effects.htm), [Cubase's control panel](https://archive.steinberg.help/cubase_pro/v12/en/cubase_nuendo/topics/audio_effects/audio_effects_effect_control_panel_r.html), the [REAPER 7 guide](https://www.reaper.fm/userguide/ReaperUserGuide781c.pdf), [FabFilter's help](https://www.fabfilter.com/help/pro-q/presets/loadingpresets), the [Surge XT manual](https://surge-synthesizer.github.io/manual-xt/), [CLAP's preset discovery](https://github.com/free-audio/clap/blob/main/include/clap/factory/preset-discovery.h), [VST3 preset locations](https://steinbergmedia.github.io/vst3_dev_portal/pages/Technical+Documentation/Locations+Format/Preset+Locations.html).

**Segno** takes the common ground and goes further in three cheap ways:
- one preset library for the plugins and the DAW;
- A/B on every module in both, without Live's automation trap;
- browsing that auditions as you go but takes one undo step.

## 3. Principles

1. **In the module's header, the same on every module** (ui-principles §7). The preset's name, previous and next, and the list are where you're looking. The menu bar lists the commands too (ADR 0040).
2. **A preset is a file you can see.** Plain JSON in a folder of your own, shared by the plugins and the DAW. You can copy, back up, send and version presets without Segno.
3. **Factory presets are always there.** They're built in and read-only, and come back with every update. Saving over one saves a copy of your own.
4. **Browsing is listening.** Picking a preset loads it at once, so you can audition a list with the arrow keys. Undo takes you back to where you were before you started browsing.
5. **Nothing modal.** Saving and renaming happen in place, in the preset list, not in a dialog.
6. **A preset is the sound, not the session.** It holds what shapes the sound, never where a module sits, what feeds its sidechain or how its panel is folded.
7. **Assistants get the same** (ADR 0021): listing, loading and saving presets.

## 4. What a preset holds

- **The module's state,** as projects and plugin state store it:
  - every parameter, as plain values;
  - its extra state (ADR 0017), the Sampler's sample, for one, except what belongs to the session.
- **Not in a preset:**
  - **session data a module declares:** today only the Spectrum's snapshot, a picture of this mix at one moment. Its reference curve is kept, so a preset can hold a target curve;
  - the panel's view state (folded, collapsed sections), already kept outside the device state;
  - the sidechain's source (host routing, ADR 0025);
  - automation, MIDI mappings, and whether the module is on.
- **About the preset:** tags, a one-line description and an author ("Segno" for factory presets).
- **Its name is the file's name.** Renaming the file renames the preset, and the two can never disagree.

**The file** (`.segnopreset`, a new permanent identifier for `docs/ids.md`) is JSON:

```json
{
  "segnoPreset": 1,
  "tags": ["Bus", "Subtle"],
  "description": "Gentle glue for a drum or mix bus.",
  "author": "Segno",
  "state": { "device": "compressor", "stateVersion": 1, "params": { "threshold": -18.0 }, "extra": {} }
}
```

- `state` is exactly the frozen device state, so loading a preset is loading a state: missing parameters take their defaults, older versions migrate, and a newer version is refused.
- `segnoPreset` is the file format's version.

## 5. Where they live

- **Factory presets:**
  - kept in the repository as `devices/<name>/presets/*.segnopreset` and built into the plugins and the DAW;
  - read-only, and listed first.
- **Yours:** `Documents/Segno/Presets/`, beside the `Documents/Segno/Unsaved` folder Segno already uses.
  - New presets are saved in a folder named after the module, `Presets/Compressor/`.
  - Any subfolders you make are yours to organise with.
  - **A preset is found by what's in it, not where it is:** every `.segnopreset` under `Presets/` belongs to the module its `state.device` names. Moving files between folders, or a module's display name changing, never loses one.
  - The plugins and the DAW read the same folder, so a preset saved in another DAW shows in Segno and the other way round.
  - The list is read again each time it opens.
- **Favourites:** `Presets/Favourites.json`, one list for factory presets and yours, also shared.
- **Defaults for new modules:** `Presets/Defaults/<Module>.segnopreset` (§9).
- **Moving the folder** comes with Settings (C1), as one of the browser's places (C2).

## 6. In the module's header

```
┌─ ‹ ● Compressor ──────── ‹  Glue •      ▾  ›  [A] ── Sidechain None ▾ ┐
```

- **Where it sits:** the **preset group**, right-aligned before the header settings, as ui-principles §7 draws it:
  - previous and next (`IconButton`s);
  - between them, the **preset field**: the preset's name, then a **dot while the module differs from it**, the same dot the title bar shows while a project is unsaved;
  - after them, the **A/B button** (§8).
- **The field's width:** it takes the room between the name and the header settings, between a minimum and a maximum (new `deviceGrid` metrics). A long name is cut with an ellipsis, and the full name shows in a tooltip.
  - Where a module is narrow, its header's minimum width grows, and the panel with it, as the layout kit already does (Utility: about 312 px instead of 240).
  - The layout lint checks every panel with the preset group, at its narrowest.
- **Before any preset is loaded,** the field reads "Default settings" (or your default's name, §9).
- **If the preset's file is gone,** the field keeps its name and shows no dot.
- **"Modified" is computed, not tracked:** the dot shows while the module's state differs from the preset's (parameters within their display precision, and extra state). Undo back to the preset's state, and the dot goes.
- **Right-click on the preset field:** the preset commands (§10).

## 7. The preset list

A click on the field opens a popover under it (the shared `Popover` and `ListRow`, as the command palette uses):

```
┌──────────────────────────────────────────┐
│ ⌕ Search presets                          │
│ [Bus] [Drums] [Vocals] [Subtle] [Heavy] … │   tags present in the list: click to filter
│ ★ Favourites only                         │
├──────────────────────────────────────────┤
│ FACTORY                                   │
│   Default settings                        │
│   Glue                       Bus, Subtle ☆│
│ ▸ Punchy drums             Drums, Heavy ★│   the loaded one is marked
│ YOURS                                     │
│   My vocal                         Vocals ☆│
├──────────────────────────────────────────┤
│ Save    Save as…                    A ⇄ B │
└──────────────────────────────────────────┘
```

- **Search** is focused when the popover opens. It matches the name, tags, description and author, ranked like the command palette.
- **Tags** filter the list; several tags narrow it further. **Favourites only** shows the starred ones.
- **The list:** the factory presets, then yours, grouped by your subfolders.
  - Each row has the name, its tags as the caption, and a star to make it a favourite.
  - A preset saved by a newer Segno shows dimmed, with "Needs a newer Segno" as its caption.
- **Browsing:**
  - a click, or moving with the arrow keys, loads the highlighted preset at once;
  - Enter or a click elsewhere closes the list, keeping what's loaded;
  - Escape closes it and goes back to what was there before it opened.
  - Everything loaded while the list is open is **one undo step**, "Load preset Punchy drums".
- **Previous and next** in the header step through the same list as it's filtered now, wrapping at the ends.
- **Saving in place:**
  - **Save as…** turns the list's top into a small form: the name (focused, with the current name selected), tags (with suggestions from the tags in use), the description and the author. Enter saves.
  - A name already among yours turns the button into **Replace**.
  - **Save** writes over the loaded preset when it's yours; on a factory preset it's Save as….
  - Rename (yours only) edits the name in its row.
  - Delete (yours only) moves the file to the system's trash, so it can be got back.

## 8. A/B compare

- **Every module has two settings, A and B,** in the plugins and the DAW alike.
- **They start the same.** Edits change the one shown.
- **Switching:**
  - the A/B button in the header shows "A" or "B"; a click switches;
  - switching is one undo step in the DAW, "Compare B";
  - "Copy A to B" (or B to A, whichever is shown) in the preset commands makes them equal again.
- **Loading a preset** loads it into the one shown. Saving saves the one shown.
- **Kept** with the project, and with the plugin's state in other DAWs (outside the device state, like the view), so tomorrow you can still compare. Never in a preset.
- **Automation isn't touched:** a lane keeps playing in A and B alike (§11). That's unlike Live, where switching turns automation off until you turn it back on.

## 9. Defaults for new modules

- **"Use as default for new Compressors"** saves the module's state as the default: `Presets/Defaults/Compressor.segnopreset`, named after the device inside, as every preset is found. A new Compressor starts from it, in the DAW and in the plugins. The field reads "Default".
- **"Clear the default"** goes back to the module's own defaults.
- **"Default settings"** heads the factory list: the module's own parameter defaults, so you can always get back to a plain module.

## 10. Commands, menus and keys (ADR 0040)

The commands act on the **selected module**, or the one whose header was used last, as the module view does now.

- **Module** is a new menu in the menu bar, between Clip and Transport (Alt+D, Mo*d*ule):

| Item | Shortcut | |
|---|---|---|
| Previous preset | | |
| Next preset | | |
| — | | |
| Save preset | | Writes over yours; on a factory preset, Save preset as… |
| Save preset as… | | Opens the list with its form |
| Revert to preset | | While modified; one undo step |
| — | | |
| Compare A/B | | Ticked on B |
| Copy A to B | | "Copy B to A" while B shows |
| — | | |
| Use as default for new *Compressors* | | |
| Clear the default | | While there is one |
| — | | |
| Show presets folder | | Opens `Documents/Segno/Presets/` in the file manager |

- No shortcuts at first: the buttons are on screen, and shortcuts become remappable with Settings (C1).
- The menu grows with M4's racks and modulators.
- Fold module and Expand module sections stay in View: they're view settings.
- **Right-click on the preset field:** the same items.
- **On screen:** the preset group (§6) and the list's Save and Save as… (§7).

## 11. Undo, automation and MIDI learn

- **In the DAW:**
  - loading a preset, reverting and switching A/B are each one undo step;
  - saving, renaming and deleting are file operations, outside the project's undo. Delete goes to the trash.
- **Automation wins.** A parameter with a lane that plays follows its lane after a preset loads, as it did before. Nothing turns a lane off: the open-question B29 rule (a lane turns off when you move its control by hand) is about your hand on the control, and a preset isn't that.
  - The automated controls already show their indicator.
- **MIDI mappings** (C8) belong to the project, not the preset, and survive a load.
- **In the plugins:**
  - a load sets each parameter as a gesture, so hosts record it like a hand on the controls;
  - the extra state goes through the host as now;
  - plugins have no undo of their own, as today.

## 12. In the plugins

- **The same header,** list, A/B and defaults, with the same folder.
- **Factory presets** are also the host's program list (`getNumPrograms`), so hosts' own program menus (REAPER, Ardour, Cubase) list them.
- **The plugin state** keeps which preset was loaded and the other A/B setting (outside the device state, like the view), so the header reads the same after the host reopens its project.
- **Later** (§17):
  - CLAP's preset discovery, which lets CLAP hosts list Segno presets without loading the plugin (`FILE` locations for yours, `PLUGIN` for the factory set);
  - LV2 preset bundles.

## 13. Factory presets (B11)

- **How many** (Q2, agreed):
  - about 10–20 for an effect;
  - 30 or more for the Subtractive Synth and the Sampler;
  - 3–5 for Utility, the Spectrum, the Tuner and the Loudness Meter.
- **Who:** Claude builds and names each set from its SPEC (product owner, 2026-10-11).
  - Each set is checked by rendering: every preset loads, makes sound within level, and is distinct from the others.
  - The product owner signs each set off by ear, as a module is signed off. What they'd change, Claude changes.
  - A changed factory preset is a reviewed change, committed with its reason.
- **Names** say what it's for or how it sounds ("Glue", "Vocal presence", "Wide pad"). Per ADR 0008, no other vendor's names, products or famous hardware.
- **Tags** come from a short vocabulary per kind of module:
  - **use:** Drums, Bass, Vocals, Keys, Guitar, Bus, Master;
  - **character:** Subtle, Heavy, Warm, Bright, Wide, Clean, Dirty;
  - **instruments' sound:** Bass, Lead, Pad, Keys, Pluck, Brass, FX, Drums.
  - Your own presets take any tag.
- **The Sampler's presets need sounds:** the product owner brings the set they're built on (2026-10-11, revising P11). Until then the Sampler has only Default settings.
- **Recipes:** each set is `devices/<name>/presets/recipes.json`, naming only what differs from the module's defaults (numbers in the parameter's unit, or text it reads: "4:1", "LP 24"). `fd-presets make` (`tools/presets`) writes each preset's whole state beside it; the conformance suite checks the files are what the recipes make. `fd-presets describe <module>` lists a module's parameters for writing recipes.
- **Lint:** a test loads every factory preset and checks:
  - its device and version;
  - no unknown parameters;
  - a render without NaNs and below 0 dBFS (instruments: a C3 note);
  - its name against a list of banned words.

## 14. Assistants (MCP)

| Tool | What |
|---|---|
| `list_presets` | A module's (or a device type's) presets: name, factory or yours, tags, description, author, favourite; which is loaded, and whether the module differs from it |
| `load_preset` | Loads a preset into a module, by name (factory first unless `source` says otherwise); one undo step |
| `save_preset` | Saves a module's state as a preset of yours: name, tags, description; refuses an existing name unless `replace` |
| `compare` | Switches a module between A and B, or copies one to the other |

- `add_module` gains an optional `preset`.
- Assistants can't delete presets: deleting files is yours.

## 15. Questions and recommendations

| # | Question | Recommendation |
|---|---|---|
| P1 | **What a preset holds** | The sound: parameters and extra state (the Sampler's sample, the Spectrum's reference curve), minus session data a module declares (the Spectrum's snapshot). Never the view, the sidechain's source, automation or MIDI mappings (§4) |
| P2 | **The file** | `.segnopreset`: JSON wrapping the frozen device state with tags, a description and an author; the file's name is the preset's name (§4) |
| P3 | **Where yours live** | `Documents/Segno/Presets/`, one folder per module for new ones, any subfolders you like; found by content, shared by the plugins and the DAW (§5) |
| P4 | **The header** | Right-aligned before the header settings: previous and next around the name, a dot while modified, an A/B button; Utility's panel grows to about 312 px to fit (§6) |
| P5 | **Browsing** | A click or an arrow key loads at once (audition as you go); Escape goes back; one undo step for the whole browse (§7) |
| P6 | **"Modified"** | A dot after the name, the project's unsaved dot; computed by comparing, so undoing back clears it (§6) |
| P7 | **A/B** | On every module, in both hosts; kept with the project and plugin state; switching is one undo step; automation keeps playing (§8) |
| P8 | **Automation on load** | Lanes keep playing and win; nothing is turned off (§11) |
| P9 | **Defaults** | "Use as default for new …" and "Clear the default"; "Default settings" heads the factory list (§9) |
| P10 | **Where in the menu bar** | A new Module menu between Clip and Transport (Alt+D). The alternative, Track > Presets ▸, puts module commands under the track (§10) |
| P11 | **The Sampler's factory presets need sounds,** and there's no core content yet | *Revised 2026-10-11: the product owner brings the sounds; the Sampler's presets wait for them.* Was: a small set of in-house CC0 sounds (about 30: drums, keys, textures, rendered from Segno's own instruments or recorded for it) in `content/`, installed with the app and plugins; Sampler presets name them by a content path that doesn't depend on where Segno is installed. The rest of the core content stays with M4's Drum Sampler |
| P12 | **Deleting** | Moves the file to the trash; assistants can't delete (§7, §14) |
| P13 | **Tags and categories** | Free tags only, from a short vocabulary for factory presets; no categories, ratings or colour labels (§13) |
| P14 | **Shortcuts** | None at first; remappable with Settings (§10) |

## 16. ADR 0044: Presets are files of the module's state, shared by the plugins and the DAW

- A preset is a `.segnopreset` file holding the module's state in the frozen device state format, with tags, a description and an author. Its name is the file's name.
- Factory presets are built in and read-only. Yours live in `Documents/Segno/Presets/`, shared by the plugins and the DAW, and are found by what's in them.
- Presets are found, loaded, saved and compared in the module's header, the same on every module in both hosts, with the commands in a Module menu.
- A preset is the sound, not the session.
- Loading is one undo step, browsing included. Automation keeps playing over a preset.

## 17. Model and engine

- **`libs/device`** (JUCE-free, `[fast]` tests):
  - `PresetFile`: reads and writes the file;
  - `PresetLibrary`: the factory set and a user folder given by the host, scanned by content, with search, tags, favourites, defaults and name rules;
  - `presetState(device)`: the state without session data;
  - `sameSound(a, b)`: whether two states sound the same, for the dot.
- **The device contract,** as additions (ADR 0034, rule 3), listed in architecture.md:
  - `DeviceInfo::sessionExtraKeys`: the extra keys a preset leaves out (default: none);
  - `ui::ParamBinding::applyState(state)`: the host applies a whole state as one edit. The default sets each parameter and the extra state in turn.
- **Factory presets:** a CMake step turns `devices/*/presets/` into a JUCE-free library linked into the plugins and the DAW.
- **`fd_device_ui`:**
  - `PresetBar` in `DevicePanel`'s header;
  - `PresetList` in a popover, from `fd_ui`'s `Popover` and `ListRow`;
  - new `deviceGrid` metrics for the field.
- **The DAW:**
  - `DevicePlugin` applies a state as one undo transaction;
  - it keeps the loaded preset and the other A/B setting in its project state (`fdPreset`, `fdCompare`);
  - the Module menu's actions;
  - the MCP tools.
- **The plugins:** `DeviceAudioProcessor` gets the program list, keeps the preset and the A/B setting in its plugin state, and loads with gestures.

## 18. Tests (Done when)

- **`[fast]`:**
  - a preset file round-trips byte for byte;
  - older state versions migrate; a newer one is refused and listed dimmed;
  - session keys are left out;
  - scanning finds presets by content in any subfolder;
  - search and tag filters;
  - favourites and defaults;
  - name rules (no characters a file name can't take; no clash on Save as… without Replace).
- **Conformance:** every factory preset of every module loads, renders without NaNs and below 0 dBFS, and passes the name lint.
- **Integration (DAW):**
  - loading is one undo step, browsing included, and Escape restores;
  - A/B switches without clicks, is one undo step, and is kept with the project;
  - a preset saved in the DAW loads in the plugin build, and the other way round;
  - a new module starts from your default;
  - an automated parameter keeps following its lane after a load.
- **Plugins:** validators pass with the program list; hosts see the loaded preset's name after reopening.
- **Panels:** the layout lint passes for every module with the preset group, at its narrowest.
- **Screenshots:** the header with a long and a modified name, the list with tags and the form, at standard and compact widths, zoomed.
- **MCP:** each tool.
- **Manual (manual-tests/M3.md):** browsing a list by ear, saving, A/B, a default, a preset moving between a plugin in another DAW and Segno.

## 19. What comes when

- **A1 (M3):** everything above; built (§20).
- **With the browser (C2):** presets in the browser's search; dragging one onto a track adds its module with it; the browser filtered to the selected module's presets.
- **Later:**
  - CLAP preset discovery and LV2 preset bundles;
  - third-party plugins' states saved as presets;
  - a preset's sound previewed before loading;
  - sharing and packs;
  - morphing between A and B.

## 20. Settled while building (2026-10-11)

- **The host interface:** each host implements `PresetHost` (`fd/device/Presets.h`): its state, its memo (which preset, A/B), one edit per load, browsing. That replaced §17's `ParamBinding::applyState`: the memo and browsing needed a host's side anyway.
- **The DAW's state is the stored values,** what the project keeps. A load writes them in its undo step: tracktion's own parameter setter writes them later, after the step has closed, so undo took back only the name. A parameter whose lane plays follows the lane over a preset, as §11 says; tracktion keeps the stored value up to date with the lane.
- **The memo** is `fdPreset` on the module in the project (undone with the state), and `preset` beside the device state in a plugin's state.
- **The plugins' program list** is Default settings and the factory presets. One of yours isn't a program, so the host sees the first; a host setting the program it reads again changes nothing, which keeps a host that resets the program after loading a state from undoing it.
- **Escape in a plugin** puts back the state from when the list opened (plugins have no undo of their own).
- **A plugin's window grows** while its list is open, when the list doesn't fit under the header (`DeviceUiContext::popoverRoom`), and shrinks back as it closes.
- **Headers:** Utility's panel is 280 px in the DAW and 256 in a plugin. Where the header makes a panel wider than its sections, the meter strip sits at the right edge, the room after the last section (device-panels.md).
- **Words:** "Use as the default for a new Compressor" (no plurals to get wrong); the save form's Description is "About".
- **The modified mark** is the title bar's, shared (`paintNameWithMark`): right after the name, which is cut instead of the mark.
- **Tests never touch your presets:** CTest gives every test an empty folder of its own (`SEGNO_PRESETS_FOLDER`), and tests that write presets set their own.

## 21. The factory presets (B11, 2026-10-11)

118 presets for eight modules: Utility 5, Spectrum 6, EQ 18, Compressor 15, Gate 12, Limiter 12, Saturator 15, Subtractive Synth 35. Each SPEC lists its set. The Sampler's wait for its sounds.

- **Checked by rendering** (the conformance suite), effects on pink noise at −18 dBFS RMS, the synth on a held C3:
  - every preset loads, holds every parameter at the module's current version, plays without NaNs and peaks at or under 0 dBFS;
  - it sounds (a flipped gate excepted: on steady noise above its threshold it passes nothing, by design);
  - it differs from the module's defaults and from every other preset;
  - its name and description use no other vendor's names (ADR 0008), and its tags come from the vocabulary.
- **Levels:**
  - the Saturator's are matched to their input within 1 dB, so they're compared by sound, not loudness;
  - EQ, dynamics and Utility presets change the level by what they do, mostly within 3 dB (limiters and compressors with makeup are louder on purpose; a de-esser and a ducker turn steady noise down);
  - the synth's single-note peaks sit between −7 and −12 dBFS (the Riser swells over seconds).
- **Names** keep to what a file can hold, so the SPECs' "Mid/side check", "Flip: room only" and "M/S master" became "Mid and side check", "Flip, room only" and "Mid-side master".
- **Tags:** the vocabulary grew with the sets: Lead, Pad, Pluck, Brass and FX for instruments and effects; Check, Fix, Stereo, Level, Mix and Ducking for utilities and dynamics.
- **The list** scrolls so it never cuts a row at its top: its view starts on a row, and a part-row only shows at the bottom.
