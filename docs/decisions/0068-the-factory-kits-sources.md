# 0068. The factory kits' sounds: CC0 sources, two accepted cases, and Segno's own

- **Status:** Accepted (product owner, 2026-10-16)
- **Date:** 2026-10-16
- **Builds on:** [0055](0055-factory-content-is-files-in-a-read-only-library.md) (factory content is files in a read-only Library), [0064](0064-a-drum-kit-whose-pads-hold-any-instrument.md) (the Drum Kit), [0067](0067-drum-machine-numbers-are-terms-of-the-trade.md) (drum machine numbers)
- **Number:** 0065 is taken on the `synth` branch; renumbered at merge if main has used 0068

## Context
- The product owner asked for factory kits from **CC0 sources exclusively**, curated into cohesive kits by style, across a very wide range of styles (2026-10-16).
- Research of three families (drum machines, acoustic kits, percussion and world; [drum-kit.md](../design/drum-kit.md) §27) found plenty of CC0:
  - Versilian's VCSL, VSCO 2 CE and Virtuosity Drums;
  - Karoryfer's libraries;
  - FreePats' CC0 sets;
  - recordings of drum machines by their owners;
  - Sonic Pi's and LMMS's CC0 samples.
- It also found four questions a strict reading can't answer alone:
  - The best-known 808 recording, Michael Fischer's (1994), carries a CC0 dedication added by its repackager, TidalCycles (2021). Fischer himself wrote only "these samples are ABSOLUTELY FREE!".
  - Some machines play back their maker's own stored samples (a 707's, a 909's hats and cymbals, a Yamaha RX5's, a Boss DR-5's). A recording of one is the recordist's to dedicate, but the maker's recordings sit underneath it.
  - Many Freesound packs are CC0 sound by sound, but their original files need a logged-in account, so the Library build can't fetch them.
  - No author-CC0 recordings exist for several electronic styles: chiptune, tuned 808 basses, more analog voices.
- Sources dedicated to the public domain only by an author's statement without CC0 (Salamander Drumkit, The Open Source Drum Kit), and the Unlicense (uzu-drumkit), aren't CC0.

## Decision
- **The factory kits use CC0 sources only,** each verified from its rights holder's own statement or license file, and fetched at a pinned commit with every file's SHA-256.
- **Two cases are accepted** (the product owner, 2026-10-16):
  - **Fischer's 808 recordings,** under the CC0 its repackager added: the author's intent is plain, it's the real machine, and it has been used freely for thirty years.
  - **CC0 recordings of sample-playback machines,** the maker's stored samples underneath them included.
- **Not used:**
  - public-domain statements without CC0, and the Unlicense;
  - anything whose provenance a source itself doubts (a sample "of unknown origin", one from a record).
- **Freesound waits:** its CC0 packs can fill gaps later, through a mirror of curated files that the Library can fetch.
- **Segno makes its own sounds** where no CC0 recording exists: drum voices rendered by the Library build from recipes in the repository (analog-style kicks, snares, hats, claps and toms; chip noise and pulse drums; tuned 808s). They're Segno's, dedicated CC0 with the rest.
- **Names follow ADR 0067:** no makers' or model names in kits or in the Library's folders. Each source is credited in the Library's notices.

## Consequences
- The Library grows by the kits' samples: trimmed, 16-bit FLAC, a selection of each source rather than all of it.
- The Library build gains three abilities:
  - mixing a hit's microphones into one sample;
  - rendering Segno's own drum voices;
  - fetching from more repositories.
- The residual risks of the two accepted cases are the product owner's call, recorded here. If a rights holder objects, the kit is rebuilt without those sounds; the Library's versions make that a normal update.
