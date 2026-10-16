# 0064. A Drum Kit instrument whose pads hold any instrument, the Sampler by default

- **Status:** Proposed (2026-10-16), with [drum-kit.md](../design/drum-kit.md) (K1–K18)
- **Date:** 2026-10-16
- **Supersedes in part, on acceptance:** [0056](0056-racks-are-modules-of-modules.md): "Zones and the Drum Rack come in M4". The Drum Kit takes the Drum Rack's place, and key, velocity and chain-select zones are no longer planned.
- **Changes, on acceptance:** [device-suite.md](../devices/device-suite.md): the Drum Sampler leaves the device list, and the Drum Rack becomes the Drum Kit
- **Builds on:** [0056](0056-racks-are-modules-of-modules.md) (racks), [0044](0044-presets-are-files.md) (presets are files), [0046](0046-one-browser-modules-are-folders.md) (modules are folders of their presets), [0055](0055-factory-content-is-files-in-a-read-only-library.md) (the Library), [0034](0034-device-api-frozen.md) (the frozen device API), [0040](0040-a-menu-bar-and-everything-by-mouse.md) (the menu bar), [0021](0021-mcp-server.md) (assistants), [0052](0052-generic-names-are-free.md) (generic names)

## Context
- **The plan had two things for drums, as Live has.**
  - The **Drum Sampler** was a module: one sample an instance, with pitch, an envelope, a filter and simple effects, "designed to sit on drum pads".
  - The **Drum Rack** was a DAW feature: pads, each holding any chain of modules, with choke groups.
  - Both came from ADR 0011's "match Live by default". Neither has a design of its own.
- **The product owner (2026-10-16) found it roundabout:**
  - two things for one job, one of them called a rack although it's an instrument;
  - a Drum Sampler that repeats the Sampler, which has had a One-shot mode since M1. ADR 0011's own rule merges devices that do the same job.
- **Options considered:**
  1. **As planned:** the Drum Sampler and the Drum Rack. You have to learn racks to make a beat, and there are two samplers to build and keep up.
  2. **A kit of samples only,** one instrument as FL Studio's FPC, Studio One's Impact XT and Battery are. It's simple and could ship as a plugin, but:
     - pads play samples only: no synthesised drums or plugins on a pad;
     - each pad gets only the kit's own built-in effects;
     - every pad's settings need permanent parameter IDs: pads × parameters.
  3. **One Drum Kit instrument whose pads each hold any instrument and effects, the Sampler by default,** as Bitwig's Drum Machine and Logic's Drum Machine Designer are. *Chosen* by the product owner (2026-10-16).
- **Why option 3 costs little:** M3's racks already hold chains of any module or plugin. They come with mixer rows, macros, saving, undo and MCP tools, and the modules in them keep their ids. A pad is one more chain.

## Decision
1. **One instrument, the Drum Kit,** listed in Instruments like any instrument. It has a pad for each MIDI note (128), shown 16 at a time.
2. **A pad holds an instrument and the effects after it:** any of our modules or a plugin. A pad stays empty until it's given a sound:
   - a sample dropped on a pad makes a Sampler in One-shot mode with that sample, playing at its own pitch;
   - an instrument dropped on a pad replaces the pad's instrument and keeps its effects.
3. **There's no Drum Sampler.** The Sampler is the kit's sampler. A drum pad's needs that the Sampler lacks are added to the Sampler, starting with a fast stop for choke groups.
4. **Each pad has its own settings:**
   - the note it plays into its instrument (Plays);
   - volume, pan, on and solo;
   - a choke group: None or 1 to 16. A pad cuts the others in its group off when it's hit.
5. **It's never called a rack.** It's built on M3's racks, with a chain for each pad. The UI's words are kit, pad, choke group and Plays. The selected pad's modules show in the frame a rack's modules show in, so modules inside a module look the same everywhere.
6. **Kits are the Drum Kit's presets** (`.segnopreset`, device `drum-kit`), as the modulators' presets are presets of DAW-only modules:
   - in the browser under Instruments ▸ Drum Kit;
   - in its header's preset field, with Save preset, A/B and "Use as the default for a new Drum Kit".
7. **The Drum Kit belongs to the DAW,** as racks and modulators do. The plugins stay one module each. In other DAWs, Segno's drums are the Sampler and, from M5, the Drum Synth.
8. **Racks still don't nest:** a kit can't go in a rack, and a pad can't hold a rack or a kit.
9. **Choke groups use a new event, All sound off,** an addition to the device API under ADR 0034. Our instruments stop within the fade a stolen voice gets, and plugins receive MIDI CC 120.
10. **Before anything is built,** an engine spike runs and the UI is drawn in every state for approval, as was done for racks.

## Consequences
- **Easier:**
  - one instrument to learn, its pads' sounds edited where they are;
  - kits that mix samples, the Drum Synth (M5) and plugins, each pad with its own effects;
  - no pads × parameters table of permanent IDs. Each pad's modules keep their own, so automation, modulators, macros and MCP reach them as they reach any module;
  - one sampler to build and maintain. M4 has one device fewer.
- **Harder:**
  - The engine needs a note filter at the head of each pad's chain, choke groups and many chains in one rack. The spike measures the CPU with 16, 64 and 128 pads.
  - Every pad shows the Sampler's full panel. One-shot mode dims what doesn't apply, but a compact view stays open question B12.
  - No kit in other DAWs.
- **New permanent identifiers** (`docs/ids.md`, on approval): the module type `drum-kit`, and the pads' properties in a project and in a kit preset.
- **The device API gains `EventType::AllSoundOff`,** an addition that leaves devices and saved states unchanged (ADR 0034).
- **On acceptance these docs change:**
  - device-suite.md: no Drum Sampler; the Drum Rack row becomes the Drum Kit; the coverage notes;
  - the roadmap's M4;
  - ADR 0056's status line, and racks.md §3, §13 and R16;
  - push-2.md's step 3 and the piano roll's drum view (piano-roll.md §16), which both name the Drum Rack;
  - factory-library.md's Drum Rack kits, and presets.md P11's "core content with M4's Drum Sampler";
  - menu-bar.md and mcp.md, for the kit's items and tools.
- **The Drum Synth (M5) stays as planned:** one voice an instance, made for a pad. Its SPEC says so.
