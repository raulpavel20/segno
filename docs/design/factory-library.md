# The Library: what comes with Segno, and the Sampler's sounds

What Segno ships besides its modules: sample instruments for the Sampler's presets, the Wavetable's tables, later kits and clips. Where they live, how the browser shows them beside your own files, how presets and projects point at them, and how they get onto a machine. Then the sounds themselves: about 48 Sampler presets from two CC0 libraries.

**Status:** approved by the product owner (2026-10-14, every recommendation of L1–L13), with [ADR 0055](../decisions/0055-factory-content-is-files-in-a-read-only-library.md). The product owner asked for the Sampler's missing presets (about 40–50, CC0 only, VSCO 2 Community Edition suggested) and for one structure that ties samples and wavetables together.

## 1. What's there now, and the loose end

- **The browser's "Segno" place** is the Segno folder (Documents/Segno): *yours*. Its Samples and Wavetables folders are made empty for your own files ([browser.md](browser.md) §9). It reads like a built-in library, but nothing that ships is in it.
- **The Wavetable's 84 factory tables** live inside the module, as code (recipes) and embedded AKWF cycles. The browser shows them only in the Wavetable's own folder, under Tables (table-editor.md §11), never as files in a place.
- **The Sampler plays one sample**, referenced by an absolute path (`sample.path`). It has no factory presets: they waited for sounds ([presets.md](presets.md) §15).
- **So nothing that ships is a file you can find, preview, drag, or open in another program.** And there's no answer yet for a preset that needs 30 samples, or a project made on one machine that uses them on another.

## 2. How others do it

| | Factory content | In the browser | Your own |
|---|---|---|---|
| **Live** | The **Core Library**, installed with Live, read-only; **Packs** installed into a folder of their own | Places: Packs, User Library, the project, folders you add; Categories (Sounds, Drums, Samples…) list factory and your content together | The **User Library** (Documents/Ableton/User Library): your presets, samples, defaults |
| **Bitwig** | **Packages**, installed by its package manager | A source for each package; content types (Presets, Samples, Multisamples…) across all of them | My Library, locations you add |
| **FL Studio** | The **Packs** folder, installed with it | Packs in the browser | Your folders |

They all draw the same line:
- **What ships is files, read-only, in a place of its own.** A preset points at its samples inside that place by a path relative to it, so it works on every machine where the place is installed.
- **Your content is somewhere else, and yours.**
- **Search and the kind views go across both.**

## 3. The sounds: two CC0 libraries by Versilian Studios

- **[VSCO 2 Community Edition](https://versilian-studios.com/vsco-community/)** ([GitHub](https://github.com/sgossner/VSCO-2-CE)): **CC0**; 3.2 GB of WAV (44.1 kHz, 16/24-bit), about 3 000 samples, with "vanilla" SFZ mappings (75 instruments).
  - Strings: violin, viola and cello sections; solo violin and double bass; harp. Each section has sustain, pizzicato, spiccato and tremolo.
  - Woodwinds: flute, piccolo, oboe, clarinet, bassoon, each with sustain and staccato.
  - Brass: horn, trumpet (open and muted), trombone, tuba.
  - Keys: two upright pianos, a pipe organ (loud and quiet).
  - Percussion: timpani, glockenspiel, marimba, xylophone, a drum and cymbal set.
- **[VCSL](https://github.com/sgossner/VCSL)** (Versilian Community Sample Library): **CC0**; 6.2 GB, "a broader expansion to the VSCO 2 CE sample set". It adds:
  - grand pianos and harpsichords;
  - recorders, ocarinas, harmonicas, a tenor saxophone;
  - vibraphone, tubular bells, kalimbas and mbiras, wine glasses;
  - a full percussion set.

  Its file names carry the note, the velocity layer and the round robin ("Vibes_hard_A2_v2_rr1"), so mappings can be made from them.
- **Why these:** CC0 (no attribution, no conditions), recorded consistently by one studio, acoustic: the niche the product owner points at, since everything Segno makes now is synthetic. Pitch-mapped already, made for a sampler.
- **What they use of SFZ** (counted in VSCO's files): `sample`, `lokey`/`hikey`/`pitch_keycenter`, `lovel`/`hivel`, `volume`, `ampeg_attack`/`ampeg_release`, `default_path`, round robin (`seq_length`/`seq_position`) and random (`lorand`/`hirand`). That is a small, well-defined part of SFZ.

## 4. A problem first: one sample can't make an orchestra

An instrument recorded every two or three notes, at two or three loudnesses, needs **zones**: which sample plays for which key and velocity. One sample stretched across the keyboard sounds wrong a few notes from where it was recorded (a cello an octave up is a chipmunk). The Sampler's SPEC has zones as its **Multisample** stage, planned for M5: key and velocity zones, round robin, SFZ import.

**Recommendation:** bring that stage forward, as the Sampler's stage 2, before the presets.
- **Zones** by key and velocity, each with its root, tuning, gain and loop.
- **Round robin** and random choice.
- **SFZ import** of the opcodes in §3.

The Sampler's controls (envelopes, filter, voices) shape the whole instrument as now. One sample stays what it is: a single zone across the keyboard. Its own SPEC update goes for review first (CLAUDE.md), with the zone display in the panel.

## 5. The structure (ADR 0055)

**One rule: what ships is files in the Library; what's yours is in your Segno folder; a module's folder holds its presets.**

- **The Library: Segno's own content, read-only, installed with the app.** In the browser's Places, **Library** first, then **User library**: the Segno folder, renamed in the browser only, so it's clearly yours.
  ```
  Library/
    Samples/                          a folder a family, an instrument, its articulations
      Strings/Violin section/Sustain/Sustain.sfz + its samples
      Keys/Upright piano/Upright piano.sfz + …
      …
    Wavetables/                       the factory tables as files, by category
      Basic/Saw.wav  Acoustic/Cello.wav  …
    Racks/                            the factory racks (racks.md §18.8), by kind
      Transitions/Riser.segnorack  Parallel/Wide chorus.segnorack  Instruments/Bass stack.segnorack  …
  ```
- **Samples:** the instruments as SFZ files (the zone maps) beside their samples, so they're files like any other.
  - Selecting one in the browser plays it: a phrase for an instrument, as presets do; the sample itself for a sample.
  - Dropping an SFZ or a sample on a Sampler loads it, and on a track makes a Sampler with it.
- **Wavetables:** the 84 factory tables are written as files at build time, by the same bake.
  - Each is a WAV with its frame size (`clm `) and its factory ID in a chunk of ours.
  - Loading one, from the Library or a drop, puts that factory table on the oscillator **by its ID**, as the table menu does: projects stay small, the table stays the built-in one, and the file is there to find, preview, open elsewhere, or start a recipe from.
  - The Wavetable's **Tables** folder in the browser (built for T20) goes: the Library's Wavetables replaces it, and the module's table menu keeps its categories.
- **References:** presets and projects point into the Library by a path relative to it (`library:Samples/Strings/Violin section/Sustain/Sustain.sfz`), never by where it's installed. Files outside the Library stay absolute (or relative to the project, after Collect files). Collect files leaves Library files where they are: every Segno has them.
- **Where it's installed, and how it's found:**
  - Linux: `~/.local/share/Segno/Library` for you, or `/usr/share/segno/library` for everyone;
  - macOS: `/Library/Application Support/Segno/Library`;
  - Windows: `%ProgramData%\Segno\Library`.

  The DAW and the plugins look in the same places, in that order. Settings > Library shows where it is and can point elsewhere (an external drive); `SEGNO_LIBRARY` overrides it (tests, portable setups).
- **Versioned:** the Library has a version. A preset or project that needs a newer one says so ("This preset needs Library 1.1"). Paths in a released Library never change: new content adds, nothing moves.
- **Missing:** a preset whose sound isn't there loads its settings and says what's missing, with where to get the Library. The Sampler's display says the same. Never silent.

## 5a. How it looks, factory and yours side by side

**On disk: two folders, never mixed.**
```
Library  (installed with Segno, read-only: ~/.local/share/Segno/Library on Linux)
  library.json                       its version
  Samples/
    Strings/Violin section/          an instrument: an SFZ a playing style, its samples beside them
      Sustain.sfz  Pizzicato.sfz  Spiccato.sfz  Tremolo.sfz  samples/…
    Keys/Grand piano/Grand piano.sfz  samples/…
    Drums/Acoustic kit/              (later) a kit: one hit a file
      Kick 1.flac  Snare 1.flac  Hat closed.flac …
  Wavetables/
    Basic/Saw.wav … Acoustic/Cello.wav …   the 84 factory tables, by category

Segno folder  (Documents/Segno: yours, written by you and by Segno for you)
  Presets/<Module>/…                 presets you save
  Samples/                           your own sounds
  Wavetables/                        tables you export
  Unsaved/                           projects not saved yet
```

**In the browser: modules hold presets, places hold files.**
```
★ Favourites
▸ Instruments
   ▸ Sampler          Yours, then the factory presets by tag: Strings ▸ Violins, Violins pizzicato…
   ▸ Wavetable        Yours, Pad, Lead, Bass…
   ▸ Drum Kit         (later) Yours, Acoustic kit, Electronic kit…
▸ Audio effects
▸ Plugins
PLACES
▸ Project             the open project's folder
▸ Library             what ships: Samples ▸ Strings ▸ Violin section ▸ Sustain…   Wavetables ▸ Acoustic ▸ Cello…
▸ User library        your Segno folder: Samples, Wavetables
▸ Drum samples        folders you add
```

**How they connect:**
- **A factory preset is built into its module, as now (ADR 0044); its sounds are Library files.** It points at them by `library:` paths:
  - the Sampler's "Violins" at `library:Samples/Strings/Violin section/Sustain.sfz`;
  - a Drum Kit's kit, pad by pad, at `library:Samples/Drums/Acoustic kit/Kick 1.flac`…;
  - a Wavetable preset at a factory table by its ID: the same table as `Library/Wavetables/Acoustic/Cello.wav`, built into the synth too. So Wavetable presets play even without the Library.
- **The files are the raw material; the presets are instruments made from them.** Selecting `Sustain.sfz` in the Library plays a phrase; dragging it onto a track makes a Sampler with it at its defaults. Dragging `Kick 1.flac` onto a Drum Kit's pad loads that pad ([drum-kit.md](drum-kit.md) §6).
- **What you make is yours, in your Segno folder:**
  - a preset you save goes to `Presets/Sampler/…` (Yours in the module's folder), and can point at Library files and your own alike;
  - your recordings and sounds go to `Samples/` or a folder you add;
  - a table made in the editor travels in the project or preset (recipe and frames), and Export wavetable writes it to `Wavetables/`.
- **Nothing ever writes into the Library.** Changing something that ships makes a copy of yours: an edited factory table becomes "Cello (edited)" in the project, a changed factory preset saved becomes one of yours.
- **Search goes across all of it,** grouped by kind (PRESETS, SAMPLES, WAVETABLES…), each result saying where it's from ("Library · Strings", "User library").

## 6. Getting it onto a machine

- **What it is:** the chosen samples, trimmed (the tail cut where it falls below −60 dB, sustains to about 8 s, pianos 12 s), converted to 16-bit FLAC (our decoder reads FLAC), with their SFZ maps; the 84 tables; a version file.
  - **Size:** the 48 presets in §7 use about 2.7 GB of source WAV. Trimmed, at 16 bits and in FLAC, about **500 MB**.
  - The pianos and the percussion get a selection (alternate notes, two velocity layers), not everything recorded.
- **How it's made:** `tools/library`, a JUCE-free tool, builds it from a recipe kept in the repository:
  - `library/recipe.json` lists each instrument's source files, each with its SHA-256, and the trims and mappings;
  - the sources are fetched by file from the two repositories at pinned commits, as the AKWF cycles are;
  - encoding needs libFLAC (BSD), added for this tool only.

  The same recipe gives the same Library on every machine.
- **In development:** `cmake --build --preset dev --target segno-library` builds it once into the CPM cache (about 2.7 GB downloaded the first time), and the dev build points at it.
  - A release build looks only where an installer puts the Library. `tools/install-desktop-entry.sh` (Segno in the app list) links `~/.local/share/Segno/Library` to the built one, never over an installed Library, and `--remove` takes the link away.
  - It isn't part of a plain build: a fresh checkout shouldn't need 3 GB.
  - Tests that need it (the factory presets' lint for the Sampler) depend on that target.
- **In a release:** an installer component of its own, "Segno Library", and an archive beside the app for those who install by hand. A plugin-only install can take it too. Hosting the archive is for when releases start.

## 7. The presets: 48, one an instrument or articulation

| Family | Presets | From |
|---|---|---|
| **Strings (13)** | Violins, Violins pizzicato, Violins spiccato, Violins tremolo, Violas, Cellos, Cellos pizzicato, Cellos spiccato, Solo violin, Solo violin pizzicato, Double bass, Double bass pizzicato, Harp | VSCO 2 CE |
| **Woodwinds (8)** | Flute, Flute staccato, Piccolo, Oboe, Clarinet, Clarinet staccato, Bassoon, Bassoon staccato | VSCO 2 CE |
| **Brass (8)** | French horn, Muted horn, Trumpet, Muted trumpet, Trumpet staccato, Trombone, Trombone staccato, Tuba | VSCO 2 CE |
| **Keys (5)** | Upright piano, Grand piano, Harpsichord, Church organ, Soft organ | VSCO 2 CE, VCSL |
| **Mallets and bells (6)** | Glockenspiel, Marimba, Xylophone, Vibraphone, Tubular bells, Kalimba | VSCO 2 CE, VCSL |
| **Percussion (3)** | Timpani, Timpani rolls, Orchestral percussion (bass drum, snare, cymbals, triangle, gong across the keys) | VSCO 2 CE |
| **More (5)** | Recorder, Harmonica, Tenor sax, Ocarina, Wine glasses | VCSL |

- **Names:** what they are, in the presets' own style ("Violins pizzicato"). No maker's names: the grand piano's maker, the harmonica's, a synth's model stay out (ADR 0008, 0052).
- **Tags:** from the vocabulary (Keys, Pluck, Lead, Pad, Brass, Bass, Drums; Warm, Bright, Clean…). presets.md §15 would get **Strings**, **Woodwinds** and **Orchestral** added for this set.
- **Each preset:** the instrument's SFZ, envelopes suited to it (a sustain's attack and release, a pluck's decay), velocity to volume as the instrument plays, the filter off, and levels matched as the other modules' presets are.
- **Every preset is heard and its level checked** by the lint as now, the Library present.

## 8. Order of work

1. **The Library and the browser** (ADR 0055):
   - the place and its location and references;
   - the factory tables as files, loading by ID;
   - the User library's name;
   - the Tables folder removed;
   - with its MCP tools (`browse` and `search` cover it; `list_wavetables` gives Library paths).
2. **The Sampler's stage 2:** its SPEC updated for review first. Zones, round robin, SFZ import, the zone display, its MCP (`load_sample` takes an SFZ; `get_module` lists zones).
3. **The content:**
   - the `tools/library` recipe and build, libFLAC, the 48 instruments mapped and trimmed;
   - the presets, tags added, levels matched;
   - the snapshots and sound reviewed.

## 9. Questions for review

| # | Question | Recommendation |
|---|---|---|
| L1 | **Where factory content lives** | Files in a read-only **Library** installed with Segno, a place of its own in the browser, never in your Segno folder (ADR 0055) |
| L2 | **Your Segno folder in the browser** | Shown as **User library** (the folder keeps its name on disk), after Library, so what's yours and what ships are never confused |
| L3 | **The Wavetable's factory tables** | Written into Library/Wavetables at build time by the same bake, each carrying its ID: loading one sets the factory table by ID. The module's Tables folder in the browser goes; its table menu keeps the categories |
| L4 | **How presets and projects point at shipped files** | `library:` paths relative to the Library, resolved per machine; files of yours stay absolute or project-relative; Collect files leaves Library files alone |
| L5 | **Finding the Library** | Standard places per system, the same for the DAW and the plugins; Settings > Library can point elsewhere; `SEGNO_LIBRARY` overrides |
| L6 | **When it's missing** | The preset loads its settings and says what's missing and where to get it, on the module too. Never silent |
| L7 | **The sounds** | VSCO 2 CE for the orchestra (the product owner's suggestion), and VCSL (same studio, also CC0) for pianos, harpsichord, mallets, bells and a few solo instruments |
| L8 | **The Sampler for these** | Its Multisample stage brought forward from M5 as stage 2: key and velocity zones, round robin, SFZ import of the opcodes these libraries use. Its SPEC updated for review first |
| L9 | **The presets** | The 48 in §7, one an instrument or articulation, with three new tags: Strings, Woodwinds, Orchestral |
| L10 | **Size and format** | About 500 MB: trimmed, 16-bit FLAC, a selection of the pianos' and percussion's samples |
| L11 | **Making it** | `tools/library` builds it from a recipe in the repository, sources fetched by file at pinned commits with their hashes, libFLAC for the encoding |
| L12 | **In development and releases** | A build target of its own (not part of a plain build), cached; tests that need it depend on it. Releases ship it as an installer component and an archive |
| L13 | **Versions** | The Library has a version. Released paths never change; a preset or project needing a newer Library says so |

## 10. Settled while building

**Phase 1, the Library and the browser (2026-10-14):**
- **Where it's looked for:** Settings > Library first, then `SEGNO_LIBRARY`, then the standard places (§5), and a development build last in the source tree's `.cache/library`. L5 had the variable override Settings; reversed, so a folder chosen in Settings always takes effect. The variable is for where there are no Settings: the plugins in other hosts, portable setups, tests. The plugins don't see the DAW's choice ([B83](../open-questions.md)).
- **What makes a Library:** a `library.json` with its version (`{"segnoLibrary": 1, "version": "1.0.0"}`). A folder chosen in Settings counts without one, so a Library copied by hand still works.
- **Building it:** `fd-library build <folder>` (`tools/library`) writes the version and the factory tables, 31 MB for now. The `segno-library` target writes it into `.cache/library`, which a Debug build finds; a plain build doesn't run it. Each table's WAV carries its factory ID in a `sgno` chunk (`{"table": "acoustic/cello"}`).
- **A factory table's file loads by its ID** wherever a table file is taken: a browser drop or double-click, Import wavetable…, a layer in the table editor, a preview, and `set_wavetable` with a path. The project stores the ID, as the table menu's choice does.
- **In the browser:**
  - Places are Project, Library, User library, then yours.
  - With no Library the place says "Not installed", greyed, with Locate… on a right-click, which sets Settings > Library.
  - Search results from it say "Library · Acoustic".
  - The Wavetable's module folder holds its presets only; the `table:` items and `DevicePanel::takeFactoryItem` are gone. They were never released.
- **The shared tree row puts the name first:** a caption shows only when the name and the caption both fit whole, and the tooltip carries it otherwise ([ui-foundations.md](ui-foundations.md)). The Library's tables made this matter: "Cello.wav" was cut short for its "64 frames".
- **Settings > Library:** a Library row first: where it was found, "Not installed", or the chosen folder with "(not found)"; Choose…; Reset goes back to where it's installed. `get_settings` gives `library.location`.
- **The Sampler** stores a sound in the Library as `library:Samples/…`, resolved each time it loads. A missing one keeps its reference, plays silence and says on the panel where it would be and how to get the Library. The DAW's loading, the panel and `load_sample` all store it so.
- **MCP:** `browse` lists the Library (missing when there's none) and User library. `search` gives a Library table's path, and `set_wavetable` takes it. `list_wavetables` gives each factory table's file in the Library.
- **Tests** find no Library: CTest points `SEGNO_LIBRARY` at a folder that isn't there. A test that needs one writes the factory tables into a folder of its own and chooses it.
- **Left for phase 3, with the samples:**
  - audio clips made from Library samples stored as `library:` paths (a file path resolver on the project);
  - Collect files leaving them alone (L4).

  No Library file can become a clip until the samples ship.

**Phase 2, the Sampler's stage 2 (2026-10-14):** approved with S1–S13 and built; what building settled is in [devices/sampler/SPEC.md](../../devices/sampler/SPEC.md), "Settled while building (stage 2)". For phase 3 it leaves:
- VSCO 2 CE's maps read as they are, nothing ignored;
- a 50-zone instrument of 8-second stereo samples loading in 143 ms (WAV; FLAC to be measured);
- about 2.1 MB a zone of 8 s stereo at 44.1 kHz.

**Phase 3, the content (2026-10-14):**
- **The sources:** VSCO 2 CE at its 1.1.0 release (`28092772`, with its SFZ maps) and VCSL at `c1ea7bcc`.
  - `tools/library/make_recipe.py` lists the instruments and makes `tools/library/recipe.json` from them: every zone, every source file with its SHA-256.
  - It fetches each file once into the CPM cache and checks it against its git blob id.
  - The `segno-library` target checks every file again by its SHA-256 (`cmake/FdLibrarySources.cmake`), then `fd-library build` makes the Library.
  - libFLAC is 1.5.0, for the tool only.
- **What it made:**
  - 48 instruments: 1,532 zones over 1,496 samples, from 1.9 GB of sources fetched once.
  - **318 MB of samples, 348 MB with the tables,** under the 500 MB expected: tails cut at −60 dB, sustains at 8 s, pianos and bells at 12 s, 16-bit FLAC.
- **Folders:** Strings, Woodwinds, Brass, Keys, Tuned percussion, Percussion.
  - §7's "Mallets and bells" is Tuned percussion, with the kalimba and the wine glasses.
  - §7's "More": the recorder, ocarina, tenor sax and harmonica joined Woodwinds.
  - The pipe organ's two are Keys/Pipe organ/Loud and Soft. The kit is Percussion/Orchestral kit.
- **The maps:**
  - **VSCO's own.**
    - The upright piano keeps two of its three loudnesses, the softest and the loudest.
    - VSCO maps the piccolo, xylophone and glockenspiel at written pitch, an octave below what they sound. They move up an octave: every instrument plays at the pitch it sounds.
  - **VCSL's from its file names:**
    - each note up to halfway to the next, the ends six semitones beyond;
    - loudnesses share 0–127;
    - repeated notes take turns.
    - Kept: the grand piano's 2nd and 4th of four loudnesses (all 19 notes), the harpsichord's 8′ register, the vibraphone's and the tenor sax's two, the tubular bells' softer and louder of each note.
  - **Roots checked by pitch:** the tool detects each sample's pitch and warns where it isn't its root. That's how the octaves were found, and how VCSL's names were shown to count C3 = 60.
    - The rest of its warnings are plucks, bells and timpani it can't hear, and single files an octave out.
    - The loud organ sounds an octave low by its 16′ stop, as an organ should.
- **Names:** VCSL's samples get names of ours, "Harmonica_C3.flac", in Segno's note names; the harmonica's maker's name stays out (ADR 0008).
- **Levels are matched in the Library:**
  - every zone of an instrument gets a gain in the recipe, so its C3 comes out near −22 dB RMS, peaking under −3 dBFS. That's where the other instruments' presets sit (median −23 dB).
  - A kit is measured on its C1 kick.
  - The presets' Gain stays at 0, and an SFZ opened from the browser sounds as its preset does.
  - SFZ's `<global> volume` couldn't do it: a region's own volume replaces it.
- **Presets:**
  - `devices/sampler/presets/recipes.json`, 48, their folders by first tag.
  - The tags Strings, Woodwinds and Orchestral joined the vocabulary.
  - The Sampler's `sample.rate`, `sample.frames` and `sample.zones` are session keys now, so a preset holds only its sound's path, the same whether the Library is there or not.
- **Hearing them:**
  - previews and the lint play an instrument in its register: `Sampler::playableKey` moves a C3 by octaves into its zones, then to the nearest key a zone plays;
  - a kit, like a bass, plays a C1.
  - The fast preset lint checks everything but the sound where the Library isn't there. "The Sampler's presets play their instruments from the Library" (`[library]`) renders all 48 from the source tree's Library, skipped where it isn't built.
- **The waveform** is drawn scaled to the sample's own peak: the Library's quiet samples took their level from their zones and showed as flat lines.
- **The Library's README** thanks Versilian Studios, though CC0 asks nothing.
- **Signed off by the product owner (2026-10-14),** with the Sampler's stage 2 and its 48 presets: the Library is done but for shipping it, which comes with releases (§6).

**The drums (2026-10-16):** the Drum Kit's factory kits ([drum-kit.md](drum-kit.md) §27–§28, [ADR 0068](../decisions/0068-the-factory-kits-sources.md)) add `Samples/Drums/<pool>/`.
- **Fourteen more sources,** each a git repository fetched file by file at a pinned commit, as VSCO's and VCSL's are, plus pieces Segno renders itself (`tools/library/DrumSynth`). `tools/library/drums.py` lists them and their pieces; `make_recipe.py` adds them to `recipe.json`.
- **New in the build:**
  - a zone can mix several microphones into one sample;
  - a piece can be levelled as one (every zone the same gain);
  - drums are trimmed to their onset;
  - a zone can be a voice rendered from its parameters.
- **What it made:** 565 pieces, 2,810 samples, 258 MB. The Library is 604 MB; its sources fetched once are 3.2 GB.
- **Size isn't a limit** (the product owner, 2026-10-16, B94): "604 MB is fine, even more would also be totally fine, libraries are often GBs in size". L10's 500 MB was an estimate. Sound decides what goes in, not megabytes.
- **The README** credits every source, CC0 or not asked.
- `fd-library files` lists a recipe's source files for the build's hash check, and `fd-library measure` gives a sample's levels for balancing the kits.
