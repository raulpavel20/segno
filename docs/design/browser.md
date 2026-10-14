# The browser: design (M3 step C2)

How you find what goes into a song: modules and their presets, plugins, samples, MIDI files and projects, in the panel on the left. **One search box** finds all of them, and **modules open like folders** that hold their presets, as the product owner asked. The decision is [ADR 0046](../decisions/0046-one-browser-modules-are-folders.md) (§17). It builds on:
- ui-principles §2: "Browser (left) offers one search box across devices, presets, samples, plugins and files. Sample preview is built in"; at compact widths it "collapses to an icon rail";
- presets ([ADR 0044](../decisions/0044-presets-are-files.md), [presets.md](presets.md) §19): "presets in the browser's search; dragging one onto a track adds its module with it; the browser filtered to the selected module's presets";
- settings ([ADR 0045](../decisions/0045-settings-are-one-page.md), [settings.md](settings.md) §7): the Segno folder, and the browser's places as a row in Library;
- tracks and clips ([ADR 0041](../decisions/0041-tracks-and-clips-managed-in-place.md)): files dropped on the arrangement already become clips; no instrument is added by itself ([ADR 0033](../decisions/0033-no-instrument-by-itself.md)); recordings move into the project's folder on its first save ([ADR 0030](../decisions/0030-projects-are-folders.md));
- the menu bar as the complete list ([ADR 0040](../decisions/0040-a-menu-bar-and-everything-by-mouse.md)) and assistants' tools ([ADR 0021](../decisions/0021-mcp-server.md));
- the shared components: `ListRow`, `PanelHeader`, `ViewOption`, the waveform cache and the shell metrics ([ui-foundations.md](ui-foundations.md), ADR 0023).

**Status:** approved by the product owner (2026-10-11), with every recommendation of §16. Built and signed off (2026-10-12); what was settled while building is in §21.

## 1. Why

- The browser is a placeholder: a disabled search field over a fixed list of module names. Modules are added only from the module chain's + menu, presets only from a module's header, and files only by dragging them in from the system's file manager.
- 118 factory presets now exist, and more come with every M3 module. They're easiest to find where you'd look first: the browser, next to their modules.
- Samples can't be heard before they're in the song, and there's no way to keep a project's files together to move it to another machine.

## 2. Where the field is (2026)

**Live 12:**
- A sidebar of **Collections** (seven colour labels, keys 1 to 7), **Library** labels (Sounds, Drums, Instruments, Audio Effects, MIDI Effects, Modulators, Plug-Ins, Clips, Samples, Grooves, Templates, Tunings) and **Places** (Packs, User Library, Current Project, and folders you add with Add Folder…, which Live scans; a missing folder shows greyed with Locate Folder).
- **Instruments are organised by device:** each device is a folder. Loading the device gives its defaults, and opening it shows its presets, in folders by kind of sound. Audio effects work the same way.
- **Search** matches every word typed, across all of the library or the selected label; `#` searches tags. A **filter view** shows tag groups; Quick Tags edit an item's tags; Auto Tags tag your samples by analysing their sound.
- **Preview:** with the Preview toggle on, selecting an item plays it. While the song plays, previews start at the next bar and loop; Raw plays files at their own tempo, once. Previews have their own volume (Preview/Cue). Instrument presets can be previewed too.
- **Loading:** drag onto a track, or into the empty space to make a new track; double-click or Enter loads onto the selected track. Back and forward buttons go through the browser's history.
- **Files:** File > Collect All and Save copies a Set's files into its Project folder. Live 12 also finds similar sounds from a sample ("Sound Similarity").

**Bitwig Studio:**
- A **Browser panel** on the right and a **Pop-up Browser** from every + button, filtered to where it was opened.
- Content comes in **sources**: Everything, packages, collections (Favorites, your own, smart collections that save a search), by kind (instruments, effects, presets, samples, clips) and locations (your library, added folders, the project).
- **Filters** beside the results: location, file kind, category, creator, **device** (a device's presets only) and tags (the most common for the search). A device with presets shows ">"; the right arrow opens **Show Presets for** it.
- **Preview** plays on selection (Auto-preview When Selected), with its own volume; samples either **Play at Original Speed** or **Stretch to Project Tempo**, both in preview and when inserted.

**Fender Studio Pro** (formerly Studio One):
- Browser tabs for instruments, effects, loops, files and the cloud; instruments sort flat, by folder, vendor or type, and **presets sit under their plug-in**.
- **What a drag makes:** an instrument dropped in blank space makes an instrument track; dropped on a track with another instrument it asks **Replace, Combine or Keep**; an effect on a track is an insert, in blank mixer space a new effects channel.

**Cubase:** the right zone's Media rack has tiles for instruments, effects, loops and samples, presets, favourites and a file browser, over its MediaBay database; loops preview in the project's tempo. The Pool's **Remove Unused Media** clears files a project doesn't use.

**Logic:** a Library of patches and a Browser with project audio, all files and Apple Loops. Loops are found by tags, and preview in the project's tempo and key. Unused audio can be selected in the project's audio browser and deleted.

**REAPER:** the Media Explorer's **tempo match** plays and inserts at the file's speed, or at the project's tempo (also at half or double); beat-synced previews start at once by default, or at the next measure. File > Clean current project directory removes unused files.

**What they share:** one place to browse, a search over everything, devices with their presets beside them (Live and Studio Pro as folders, Bitwig as a filter and a drill-in), previews on selection with their own volume and an in-time mode, drag to where it goes, and a command to gather a project's files.

Sources: the [Live 12 browser](https://www.ableton.com/en/live-manual/12/working-with-the-browser/) and [files and Sets](https://www.ableton.com/en/live-manual/12/managing-files-and-sets/) chapters, [Sound On Sound on Live 12](https://www.soundonsound.com/node/4931445), [Bitwig's browsers](https://bitwig.com/userguide/latest/browsers) and [their common elements](https://www.bitwig.com/userguide/latest/common_browser_elements), [Studio Pro's instruments and effects tabs](https://fenderstudiopromanual.fender.com/en/Content/The_Browser_Topics/Instruments_and_Effects_Tabs.htm), [Cubase's Media rack](https://archive.steinberg.help/nuendo/v12/en/cubase_nuendo/topics/project_window/project_window_right_zone_mediabay_r.html), [Logic's Loop Browser](https://help.apple.com/logicpro/mac/9.1.6/en/logicpro/usermanual/chapter_11_section_3.html), [REAPER's Media Explorer actions](https://www.cockos.com/wiki/index.php/ALR_MediaExplorer_40023).

**Segno** takes Live's folders (the product owner's ask), Bitwig's previews in time, one search that needs no filters to start with, and drags that land where they're dropped.

## 3. Principles

1. **Type to find anything.** One search box over modules, presets, plugins, samples, MIDI files and projects; no scope to set first.
2. **Browse like folders.** A module is a folder of its presets; your folders are folders. Nothing is hidden behind tabs.
3. **Hear it before it's in the song.** A sample plays when selected, in time with the song while it plays; an instrument preset plays a short phrase.
4. **Drag it where it goes, or double-click to put it on what's selected.** Each drop is one undo step.
5. **Files stay where they are** until you gather them into the project. The browser never moves or deletes a file you didn't ask it to.
6. **Never in the way.** No dialogs while browsing; it keeps up with 10 000 files; everything is reachable by mouse (ADR 0040).

## 4. The panel

```
┌ Browser ──────────────────────────┐
│ ⌕ Search                     ●Preview │   search; the preview switch (a view option)
├───────────────────────────────────┤
│ ★ Favourites                      │
│ ▾ Instruments                     │
│   ▾ Subtractive Synth             │   a module: loads its default; opens to its presets
│     ▸ Yours                       │
│     ▸ Bass                     8  │   instruments' presets in folders by kind of sound
│     ▸ Lead                     6  │
│   ▸ Sampler                       │
│ ▸ Audio effects                   │
│ ▸ Plugins                         │
│ PLACES                            │
│ ▸ Project                         │   the open project's folder
│ ▸ Library                         │   what ships with Segno (ADR 0055), read-only
│ ▸ User library                    │   Documents/Segno: your presets, samples, projects
│ ▸ Drum samples                    │   a folder you added
│ + Add folder…                     │
├───────────────────────────────────┤
│ Kick 808.wav          ▶  ━━○━━    │   the preview strip: name, play, volume
│ ▁▃█▅▃▂▁▁▂▅█▅▂▁   0.6 s · 44.1 kHz │   waveform and what it is
└───────────────────────────────────┘
```

- **One column,** as the panel is narrow (220 px by default, 160 to 480): a tree, each level `shell.browserIndent` (a new metric, 12 px) in from its parent, rows on the shell's compact row height (`shell.rowHeight`). Long names end in an ellipsis, with the full name in a tooltip.
- **What's at the top level:**
  - **Favourites:** anything you've starred (§8).
  - **Instruments** and **Audio effects:** our modules, A to Z. MIDI effects join when there are some (M4).
  - **Plugins:** the plugins found by the scan, in folders by maker.
  - **Places:** the project's folder, the Library, the Segno folder (as User library), and the folders you add (§9).
- **Opening and closing:** a click on the triangle, or on a folder's name, opens it; the right and left arrows do too. Folders stay as you left them, so the tree is the same each time the browser shows.
- **Each row shows** its name, and on the right a caption where it helps: a folder's count, a preset's tags, a sample's length, a plugin's format. The name comes first: a caption shows only when both fit whole, and the tooltip carries it otherwise.
- **Right-click** on any row: what can be done with it (Load on the selected track, Add to favourites, Show in folder, Remove from places…), each also reachable another way (ADR 0040).

## 5. Modules are folders of their presets

As in Live, at the product owner's suggestion:
- **The module's own row is the module.** Double-click, Enter or a drag loads it with its default: yours, if you set one (presets.md §9), else Default settings.
- **Opening it shows its presets,** folders first, then presets:
  - **Yours:** your presets for it, in your own subfolders (presets.md §5);
  - **an instrument's factory presets** in folders by kind of sound, named by each preset's first tag (Subtractive Synth: Bass, Lead, Pad, Keys, Pluck, Brass, Drums, FX);
  - **an effect's factory presets** as one list, A to Z: they're 5 to 18 each, and their first tags are about where they're used, too varied to make useful folders.
- **A preset's row** loads the module with that preset. Dropped on a module of its kind, it loads into that module instead (one undo step, "Load preset Glue", as from the header).
- **Your default** shows inside as "Your default" while you have one, so it can be seen and starred.
- **The same presets as the header:** the browser and each module's preset list read the same library (`PresetLibrary`), so a preset saved, renamed or deleted in either shows in the other.
- **Plugins** are rows without presets in C2; their own presets come later (presets.md §19).

## 6. Search

- **Typing searches everything** the browser knows, from the first letter: modules, presets, plugins, and the files in your places (§9). Every word must match the name, tags, description, module, maker or folder.
- **Results replace the tree, grouped by kind,** the best group first: MODULES, PRESETS, PLUGINS, SAMPLES, MIDI FILES, PROJECTS. A group shows its best 8; a last row "All 240 samples" shows the rest.
  - A preset's caption is its module ("Glue · Compressor"); a file's is its folder.
  - Ranked like the command palette: whole words and starts of names first, then favourites, then what you used last.
- **Tags:** the tags the results carry show as buttons under the search (as in the preset list, presets.md §7); a click narrows the results to that tag.
- **Escape** clears the search and gives back the tree as it was; a second Escape gives the keyboard back to the arrangement.
- **Search the browser** (Ctrl+F, View menu) shows the browser if it's hidden and puts the keyboard in the search box.
- **Fast:** results show within 50 ms of a key on 10 000 files (§13).

## 7. Previews

**Samples:**
- **With Preview on** (the switch in the browser's header, on by default; also View > Preview in the browser), selecting a sample plays it. A click on ▶ in the preview strip, or Shift+Enter, plays it while Preview is off.
- **In time with the song while it plays:** a loop with a known tempo starts at once, from the point that lines up with the song's bar, stretched to the song's tempo, and loops while it's selected. A one-shot, or a file of unknown tempo, plays as it is, once. With the song stopped, everything plays at its own speed.
  - **A file's tempo** comes from its loop metadata (ACID chunks, Apple Loops), else a tempo in its name ("Drums 124 bpm"), else the engine's tempo detector, run once when the file is indexed.
- **Where it plays:** mixed into the output after the master's modules, so a limiter on the master never colours it, at its own volume (the strip's slider). It's never recorded, bounced or exported.
- **The strip** shows the name, a waveform with a playhead, the length, tempo and sample rate, play and stop, and the volume.

**Instrument presets:** selecting one, with Preview on, plays a short phrase rendered with that preset offline: a held note in its register (Bass a C1, Pad a chord, the rest a C3), about two seconds, through the strip. Our modules render in milliseconds on their own (`tools/render` does the same), so it never touches the project. Effects' presets, and plugins', don't preview in C2.

**Wavetables** (table-editor.md §7, §11): a held C3 through a Wavetable of its own with the table on its first oscillator, rendered offline as an instrument preset's phrase is. The Wavetable's module folder opens to **Tables** (its factory tables, a folder a category), then its presets; the factory tables are found by search too, and put in as your own are. Dropped on a track, a Wavetable with the table goes in place of its instrument; on a Wavetable in the chain, the table goes on the oscillator its OSC section shows.

**MIDI files:** play on the selected track's instrument while Preview is on, the song's tempo stretching them as it would after a drop; with no instrument there, the strip says so.

## 8. Favourites

- **One Favourites for everything:** a star on a row's hover (as in the preset list) marks a module, preset, plugin, file or folder. Favourites lists them by kind.
- **Presets' stars are the same** as the header's (`Presets/Favourites.json`); everything else's are kept in `settings.json`.
- Coloured collections (Live's, Bitwig's) wait until people ask for more than one list.

## 9. Places

- **Project:** the open project's folder: Recordings, Bounces, Exports, Samples (collected files). An unsaved project's folder is its place under `Segno/Unsaved`.
- **Library** ([factory-library.md](factory-library.md), ADR 0055): what ships besides the modules, read-only: the factory wavetables in Wavetables, a folder a category (and, with the Sampler's stage 2, sample instruments in Samples). Found where it's installed or where Settings > Library points; when there's none it shows "Not installed", greyed, with Locate… on a right-click. Search results from it say so ("Library · Acoustic").
- **User library:** the Segno folder (Settings > Library), named so in the browser only: Presets, Samples (a new folder for your own sounds, shipped empty), Wavetables (your tables, [table-editor.md](table-editor.md) §7; made with the place, as Samples is), and your projects if you keep them there.
- **Your folders:** **+ Add folder…** at the end of Places, also Settings > Library > Browser places (Add…, Remove). Right-click a place to remove it; a folder that's gone shows greyed, with Locate… and Remove.
- **Indexing:**
  - the files under every place are indexed in the background, at launch and when a folder is opened: name, kind, length, sample rate, channels and tempo;
  - the index is kept in `~/.config/Segno/browser-index.json`, so search works from the first second;
  - files the browser can use: WAV, AIFF, FLAC, Ogg, MP3, MIDI, Segno projects, wavetables (`.wt`, and sound files in a folder named Wavetables: a kind of their own, WAVETABLES in the results, their frames as the caption), and multisamples (SFZ files: MULTISAMPLES in the results, their zones as the caption; selected, they play the presets' phrase through a Sampler of their own, [devices/sampler/SPEC.md](../../devices/sampler/SPEC.md) S11);
  - adding a whole drive is allowed but slow, and the strip says so while it indexes, as Live warns.

## 10. Putting things in the song

| What | Dropped on a track (its header, lanes or mixer strip) | Dropped in the module chain | Dropped below the tracks (or in the mixer's empty space) | Double-click or Enter |
|---|---|---|---|---|
| **A module** | At the end of the track's chain; an instrument replaces the track's instrument (§11) | Where it's dropped (the chain's drop line) | A new track with it | Onto the selected track, as on the left |
| **A preset** | Its module with the preset, as above; on a module of its kind, into that module | Its module with the preset, where dropped; on a module of its kind, into it | A new track with its module | Into the selected module if it's of its kind, else onto the selected track |
| **A plugin** | As a module | As a module | As a module | As a module |
| **A sample** | An audio clip where it's dropped (as files from the desktop today) | On the Sampler: its sample. Elsewhere: a Sampler with it | A new track with the clip at the drop point | At the playhead on the selected track |
| **A multisample** (an SFZ file) | A Sampler with it, in place of the track's instrument | On the Sampler: its zones. Elsewhere: a Sampler with it | A new track with a Sampler with it | Into the selected Sampler, else onto the selected track as on the left |
| **A MIDI file** | A MIDI clip where it's dropped | – | A new track with the clip | At the playhead on the selected track |
| **A project** | – | – | – | Opens it (asking about unsaved changes) |

- **One undo step per drop,** named for what it did ("Add Compressor", "Insert Kick 808").
- **While dragging,** the target shows as it does for files from the desktop today: the drop line in `active`, the track or chain highlighted.
- **A loop with a known tempo** dropped in the song **follows the song's tempo,** as its preview did (§7, Q8): the clip is set to stretch with the song (the engine's clip time-stretch, already built in), the first piece of M4's warping.
- **Clips keep their files where they are** (principle 5) until you collect them.

## 11. An instrument on a track that has one

- **It replaces it,** in one undo step, with a toast: "Replaced Sampler with Subtractive Synth", and Undo. The track keeps its effects and clips; lanes that drove the old instrument stay, their assignments to it removed, as when a module is deleted ([automation.md](automation.md)).
- Studio Pro asks Replace, Combine or Keep; Live replaces. A question at every drop would slow browsing presets down, and Undo is one click away.

## 12. Keeping a project's files together

- **File > Collect files and save:** copies every file the project uses from outside its folder into its `Samples` folder (keeping names, numbering clashes), points the clips at the copies, and saves. An unsaved project asks for a name first, as Save as… does. Nothing outside the project is touched.
- **File > Remove unused files…:** lists the files in the project's Recordings, Bounces and Samples that no clip or take uses (nor anything undo could bring back), with their sizes; **Move to trash** sends them to the system's trash, after this one confirmation (ui-principles allows a dialog for destruction).
- Both say what they did in a toast, with Show in folder.

## 13. Fast enough

- **Search:** within 50 ms of a key on 10 000 indexed files, measured by a `[fast]` test; the search runs on the message thread over the index in memory, which holds names and words lower-cased once.
- **Scrolling:** the tree and the results draw only the rows in view, within the frame budget (the arrangement's benchmark, with the browser open).
- **Indexing** runs on a background thread, never blocking the message thread; a place being indexed shows a spinner on its row.
- **Previews** start within 30 ms of the selection (the file's start is read ahead while the selection moves).

## 14. Menus, keys and actions (ADR 0040)

| Item | Menu | Shortcut | |
|---|---|---|---|
| Show browser | View | Ctrl+Alt+B | as now |
| Search the browser | View | Ctrl+F | *new* (`browser.search`): shows the browser, keyboard in its search |
| Preview in the browser | View | – | *new* (`browser.preview`), a toggle; also the switch in the browser's header |
| Collect files and save | File | – | *new* (`file.collect`) |
| Remove unused files… | File | – | *new* (`file.removeUnused`) |

- **In the browser:** arrows move and open; Enter loads onto the selection; Shift+Enter previews; Escape clears the search; typing anywhere in the tree goes to the search.
- **Ctrl+F is free** today; Ctrl+Alt+F is Focus mode and Ctrl+Shift+F Follow playhead.

## 15. Compact widths and assistants

**Below 1440 px** the browser is its rail (ui-principles §2). The rail holds Search, Favourites, Instruments, Audio effects, Plugins and Places; a click opens the browser over the arrangement at that part, at its normal width, and it closes when something is dragged out of it or on a click elsewhere.

**Assistants (MCP):**

| Tool | What |
|---|---|
| `search` | `query`, `kinds?`, `limit?`: the browser's results, each with an id (a module type, a preset id, a file's path), its kind, name and caption |
| `browse` | `place?`, `folder?`: what's in a place or folder (the places when neither is given) |
| `preview` | `item`, `stop?`: plays a sample, MIDI file or instrument preset for the person, as the browser would; reports its length and tempo |
| `collect_files` | Collect files and save; refused in read-only mode |

- Loading uses the tools there are: `add_module` (with `preset`), `insert_audio_file`, `insert_midi_file`.
- **Assistants never delete files:** there's no tool for Remove unused files.

## 16. Questions and recommendations

| # | Question | Recommendation |
|---|---|---|
| R1 | **The panel's shape:** Live's sidebar and content side by side, Bitwig's sources and filters, Studio Pro's tabs, or one tree | One column: Favourites, Instruments, Audio effects, Plugins, then Places, as a tree that opens in place; search turns it into results (§4). The panel is narrow, and a tree needs no tabs |
| R2 | **Modules as folders of their presets** (the product owner's suggestion; Live, Studio Pro; Bitwig as a drill-in) | Yes: the module's row loads its default; opening it shows Yours, then the factory presets, in folders by kind of sound for instruments and A to Z for effects (§5) |
| R3 | **Search:** where it looks, and how results show | Always everything, no scope to pick; results grouped by kind, best group first, 8 per group with "All…"; the results' tags as buttons to narrow (§6) |
| R4 | **Favourites and collections** | One Favourites for everything, sharing the presets' stars; coloured collections later (§8) |
| R5 | **Places** | The project's folder, the Segno folder (with a new, empty Samples folder) and folders you add, indexed in the background; also listed in Settings > Library (§9) |
| R6 | **Preview:** on selection, and where it plays | On selection with Preview on (on by default); after the master's modules, at its own volume; never recorded or exported (§7) |
| R7 | **Previews in time** | While the song plays, a loop of known tempo starts at once, lined up with the bar and stretched to the song's tempo; one-shots and unknown tempos play as they are. The tempo from metadata, the name, or the engine's detector (§7) |
| R8 | **A loop dropped in the song** | Follows the song's tempo, as its preview did, using the engine's clip time-stretch: the first piece of M4's warping. If that proves larger than C2 can hold, the preview plays at the file's own speed instead, so it never promises what a drop won't do, and both move to M4 together |
| R9 | **Previewing presets** | Instrument presets by a short phrase rendered offline (§7); effects' presets later |
| R10 | **What a drop makes** | The table in §10: a track gets it at the end of its chain, the chain where dropped, empty space a new track; a sample on the Sampler loads into it; double-click puts it on the selection |
| R11 | **An instrument on a track that has one** | Replace it, one undo step, with Undo in the toast; no question at each drop (§11) |
| R12 | **Keeping files together** | File > Collect files and save, into the project's `Samples`; File > Remove unused files…, to the system's trash after one confirmation; assistants can collect, never delete (§12) |
| R13 | **Compact widths** | The rail's icons open the browser over the arrangement at that part (§15) |
| R14 | **Keys and menus** | Ctrl+F Search the browser; Preview in the browser in View; Collect and Remove unused in File (§14) |
| R15 | **Later, not C2** | Similar sounds, tagging your samples by sound, coloured collections, plugins' own presets, effects' preset previews, importing tracks from another project, packs. Hot-swapping a Sampler's sound from the browser comes with the Sampler's presets |

## 17. ADR 0046: One browser, one search; modules are folders of their presets

- **The browser is one panel on the left:** a tree of Favourites, Instruments, Audio effects, Plugins and Places, and one search over everything it knows, without a scope to choose first.
- **A module is a folder of its presets:** its own row loads its default; inside are your presets and the factory ones, the same library as the module's header.
- **Files are browsed where they are,** in places you choose, indexed in the background; nothing is copied until you collect the project's files, and nothing is deleted but by Remove unused files, to the system's trash.
- **Previews play on selection,** after the master's modules at their own volume, in time with the song while it plays; a loop dropped in the song follows its tempo too.
- **A drag puts it where it's dropped; double-click puts it on the selection;** one undo step each. An instrument replaces the track's instrument.
- **Assistants can search, browse, preview and collect,** never delete.

## 18. Model and engine

- **`model::BrowserIndex`** (JUCE-free, `[fast]`): items (kind, name, words, tags, module, folder, length, tempo), the tree of categories and places, search and ranking (on the palette's `rankPalette`), grouping, tags of a result set, the index file read and written, and a tempo from a file's name.
- **The indexer** (JUCE, a background thread): walks the places, reads each file's header (length, rate, channels, loop metadata through the engine's `LoopInfo`), detects tempos (the engine's `TempoDetect`), and hands batches to the message thread.
- **`engine::Session`:** a **preview player** mixed after the master (play a file synced or not, play a rendered buffer, stop, volume); a loop clip that follows the song's tempo; `collectFiles()` and `unusedFiles()` / `removeUnusedFiles()`; `replaceInstrument`.
- **`app::BrowserPanel`** rewritten on `ListRow`, with drag sources the arrangement, the mixer and the module chain accept (a drag carries a browser item; files from the desktop keep working).
- **Presets:** `PresetLibrary` gives the browser each module's presets and folders; the offline preset render uses the device's DSP as `tools/render` does.

## 19. Tests (Done when)

- **`[fast]`:**
  - search over 10 000 generated items answers within 50 ms, and ranks names before tags and words before parts of words;
  - the tree: modules with their presets (folders by first tag for instruments, A to Z for effects, Yours first), places, favourites;
  - tempos read from names ("124bpm", "124 BPM", "Loop_90"); the index file round-trips and tolerates what it doesn't know.
- **Integration:**
  - every drop of §10 does what the table says, one undo step each; double-click onto the selection; an instrument replaces the track's, and Undo brings it back;
  - a preset dropped on a module of its kind loads into it; a sample dropped on the Sampler loads into it;
  - a loop previewed while the song plays starts on its bar line and at the song's tempo; the preview is in the output and absent from an export and a bounce;
  - a dropped loop of 120 BPM in a 100 BPM song plays its bars in the song's bars;
  - Collect files and save leaves a project that opens from another folder with every clip; Remove unused files lists exactly the files nothing uses;
  - places: added, indexed, removed, and missing folders greyed.
- **Screenshots** of the tree, a module opened, results, the preview strip, the rail's overlay, at standard and compact widths, zoomed.
- **MCP:** `search`, `browse`, `preview`, `collect_files`, and read-only's refusal.
- **Manual (manual-tests/M3.md):** finding and auditioning a sound by ear, dragging to every target, collecting a project and opening it on another machine.

## 20. What comes when

- **C2 (M3):** everything above.
- **With other steps:** the Sampler's factory presets and hot-swapping its sound from the browser (B11, when the sounds arrive); the out-of-process plugin scan feeding the Plugins folder (C5); MIDI effects as a category (M4).
- **Later:** similar sounds, tagging your samples by their sound, coloured collections, plugins' own presets, effects' preset previews, importing tracks from another project, packs to download.

## 21. Settled while building (2026-10-12)

- **R8, the fallback:** previews don't stretch loops to the song's tempo. About 56 places in 20 files (fades, takes, splits, crossfades, waveforms, bounce) take an audio clip to play at its file's speed, so clips that follow the tempo are M4's warping, and previews wait for it too, so a preview never promises what a drop won't do. **In time** now means: while the song plays, a loop that is a whole number of beats (two or more) at the song's tempo starts at once from the point that lines up with the bar, and loops; anything else plays once, from its start.
- **A file's tempo** comes from its loop metadata (a WAV's acid chunk, an AIFF's beat count) or a tempo in its name; the engine's tempo detector isn't run (a one-shot would get a wrong tempo, and tempos only label files until M4).
- **The preview** is an audio callback of its own on the device, added after the engine's output, at −6 dB to start (the strip's number); a MIDI file plays on the selected track's instrument as live MIDI, up to 16 bars.
- **An instrument preset's phrase** is about two seconds rendered offline through a module of its own: Bass a C1, Pad a C3 major chord, anything else a C3.
- **Modules' folders:** Yours first (your default shows there as "Your default"), then an instrument's factory presets in folders by first tag, A to Z (Subtractive Synth: Bass, Brass, Drums, FX, Keys, Lead, Pad, Pluck); an effect's A to Z. The Sampler has no presets yet, so it has no folder to open.
- **An effect dropped ahead of the instrument** goes right after it, so the instrument stays first.
- **The panel:** the shared header (Browser, and the Preview view option), the search, the results' tags as toggles on one line, the tree drawn with a new shared `TreeRow`, and the preview strip at the foot: the name and volume, a sample's waveform with the playhead, then play and what it is (length, tempo, sample rate, in that order so a narrow panel cuts the least).
- **The rail's icons** are new: `Note` (instruments), `Knob` (audio effects), `Plug` (plugins), `Folder` (places), beside Search and the star. Over the arrangement, the panel has a hairline at its edge.
- **Remove unused files** counts a file as used if anything used it since the project opened, so undo can never bring back a clip whose file is gone.
- **Collect files and save** needs the project saved once; before that it asks for a name first, as Save as… does.
- **Tests** never touch your Segno folder: CTest gives them their own (the parent of their presets folder). Nor do they find a Library installed on the machine: CTest points `SEGNO_LIBRARY` at a folder that isn't there, and a test that needs one writes its own.

