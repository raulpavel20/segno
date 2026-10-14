# 0055. Factory content is files in a read-only Library

- **Status:** Accepted (product owner, 2026-10-14, with [factory-library.md](../design/factory-library.md))
- **Date:** 2026-10-14
- **Builds on:** [0017](0017-heavy-data-and-non-parameter-state.md) (heavy data and non-parameter state), [0044](0044-presets-are-files.md) (presets are files), [0046](0046-one-browser-modules-are-folders.md) (one browser, modules are folders)

## Context
- The Sampler's factory presets need sample instruments: hundreds of files, about 500 MB.
- The Wavetable ships 84 factory tables inside the module, which the browser shows only in the module's own folder, never as files.
- The browser's "Segno" place is the user's own folder, but it reads like a built-in library.
- Presets and projects point at samples by absolute paths, which don't travel between machines.
- Live, Bitwig and FL Studio all keep what ships as read-only files in a place of its own (Core Library, Packages, Packs), apart from the user's library.

## Decision
- **What ships besides the modules is files**, in a read-only, versioned **Library** installed with Segno: sample instruments (SFZ maps beside their samples) and the factory wavetables (written at build time by the same bake, each carrying its factory ID).
- **The browser shows it as a place of its own, Library**, before the user's Segno folder, which is shown as **User library**. A module's folder holds its presets only.
- **Presets and projects point into the Library by paths relative to it** (`library:…`), resolved on each machine; the user's files stay absolute or project-relative.
- **The DAW and the plugins find it in the same standard places**, overridable in Settings and by `SEGNO_LIBRARY`.
- **Released paths never change.** A preset or project that needs a newer Library says so; a missing Library is said, never silent.
- **A factory table loaded from the Library is set by its ID**, as the module's table menu sets it: the table stays the built-in one and projects stay small.

## Consequences
- Factory sounds can be found, previewed, dragged and opened in other programs like any file. Search covers them with everything else.
- The Library is built from a recipe in the repository, from sources fetched at pinned commits with their hashes. It is a build target of its own, not part of a plain build.
- Releases carry the Library as an installer component and an archive. The plugins work without it, but the presets that need it say so.
- The Wavetable's Tables folder in the browser (table-editor.md §11, T20) is replaced by the Library's Wavetables.
