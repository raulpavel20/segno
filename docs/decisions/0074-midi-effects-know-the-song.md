# 0074. MIDI effects know the song, and go beyond Live

- **Status:** Accepted (product owner, 2026-10-18, with [midi-effects.md](../design/midi-effects.md) and every recommendation of MX1–MX25)
- **Date:** 2026-10-18
- **Number:** taken on a branch beside main (`midi-effects`); renumbered at merge if main has used it
- **Amends:** [0011](0011-calibrated-ambition-and-consolidation.md) point 1 ("by default, match Live") for MIDI effects, as [0050](0050-the-wavetable-synth-goes-beyond-live.md) and [0065](0065-granular-goes-beyond-live.md) did for two instruments
- **Builds on:** [0057](0057-the-song-has-one-key.md) (the song's key), [0058](0058-the-piano-roll-writes-in-key-and-in-chords.md) (chords are notes), [0062](0062-random-rolls-again-each-time-it-plays.md) (random rolls again; Keep), [0034](0034-device-api-frozen.md) (additions to the frozen API), [0025](0025-sidechain-routing.md) (another track chosen in the header), [0035](0035-bounce-keeps-its-source.md) and [0032](0032-takes-belong-to-clips.md) (the source kept as a take), [0049](0049-the-master-row-is-the-song-row.md) (the song's sections)

## Context
- The product owner (2026-10-18) asked for MIDI effects as a new category: "a cohesive, comprehensive set of MIDI effects and perhaps even generators that would expand creativity and make coming up with ideas and flipping ideas into new things easy and fun", innovating "where these plugins can integrate deeply with the DAW since we are making both", and "smart and adaptive to your project".
- The plan until now copied Live's nine MIDI effects across M4 and M6 ([device-suite.md](../devices/device-suite.md)).
- The research ([midi-effects.md](../design/midi-effects.md) §2, eight DAWs, the leading plugins, hardware sequencers and the literature) found:
  - every DAW has the same basic shapers;
  - chords reach effects only through a chord track kept by hand (Logic, Cubase, Studio Pro) or from one track as it plays (Bitwig's Harmonize); no effect sees a change coming, and almost nothing reads the song's sections;
  - the most loved generative ideas (Elektron's conditions, the T-1, the NDLR, Marbles, Grids) live in hardware and plugins that can't see the song they play in;
  - users' complaints are getting a plugin's notes into the DAW, chord changes landing late, complexity, generic output, and results lost to the next roll.
- Segno already has what following needs: one key for the song, chords read from notes, named sections, other tracks a module can listen to, and randomness computed from a roll that can be kept. The device API reserved MIDI effects at M1, but no host passes their notes on.

## Decision
- **MIDI effects know the song and follow it by default:** the key, the harmony, the sections (with an Energy each), and, where chosen, another track's notes. Each works without them, with settings of its own, and says when it falls back. That's also how it works as a plugin in another host.
- **The harmony is read from the song's notes,** as the piano roll reads a clip's chords. There's no chord track.
  - Each track's Harmony is Auto, On or Off. A muted track, or one with no instrument, still counts, so a silent track of chords guides the song.
  - The chords read are shown along the song row.
  - Effects see the next chord coming, because the clips are in the arrangement.
- **Following never edits.** An effect changes what's heard, never a clip's notes, until its notes are kept.
- **Eleven modules** ([midi-effects.md](../design/midi-effects.md) §6):
  - shaping what you play: Arpeggiator, Chord, Pitch (Live's Pitch, Scale and Random in one), Velocity, Note Length, Note Echo, Groove, Controllers;
  - writing a part: Rhythm, Bassline, Melody, named for the piano roll's Generate tools whose algorithms they share.
- **They go beyond Live** where the integrated DAW or the research shows a clear gain: steps of the key everywhere, the song's chords, step lanes and conditions, fills and drop-outs before sections, another track's rhythm and feel, and the three generators.
- **Generators play where their track has clips,** and what they play is drawn there. Every section with the same name plays the same music.
- **Randomness follows ADR 0062:** it rolls again each pass, and Keep holds a roll. The last 16 rolls heard can be kept after the fact. Everything is computed from the roll and the place, so renders, jumps and kept rolls agree.
- **What they play can always become notes:**
  - Keep as notes keeps the notes before as a take and turns the effects off;
  - a stretch of what played drags out as a clip;
  - any MIDI effect runs as a tool on selected notes.
- **One algorithm in every place:** the effects, the clip view's tools and assistants run the same JUCE-free functions in `libs/music`.
- **No stuck notes:** a shared ledger ends what a module started when it stops, jumps, is turned off or is removed.
- **The device API grows by additions only** (ADR 0034 rule 3): MIDI out in both hosts, a read-only view of the song for the block, and another track's notes.
- **No machine learning in the modules.** They're explainable algorithms. Local models are in the backlog, as processes that drive Segno through MCP; nothing is bundled.
- **Built in steps,** each signed off on its own:
  - the spike and the UI drawn;
  - following the song (Pitch, Chord, Arpeggiator);
  - the generators and players;
  - the rest of Live's set.

## Consequences
- `libs/music` grows the harmony read from notes and the effects' functions; the clip view's tools' algorithms move into it from `app/src/model`.
- The engine needs MIDI between our modules in tracktion's chain, a track hearing another track's notes, generators playing in clips, and notes read ahead; a spike proves each first.
- The model gains a track's Harmony and a section's Energy.
- New UI:
  - the shared note display;
  - a Listen picker;
  - View ▸ Song chords and Played notes;
  - Track ▸ Harmony and Song ▸ Harmony…;
  - the browser's MIDI effects part.
- New MCP tools: `get_song_context`, `set_track_harmony`, `set_module_listen`, `get_played_notes`, `keep_as_notes`. `transform_notes` and `generate_notes` take a module, and `set_section` takes an energy.
- Factory content: designed presets for eleven modules, and about twenty players as racks in the Library.
- The plugin builds of MIDI effects stay best effort: hosts differ in what they accept, and Live hosts none.
- ADR 0011 holds for every other category.
