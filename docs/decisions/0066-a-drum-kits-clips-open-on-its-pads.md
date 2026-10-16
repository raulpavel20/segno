# 0066. A Drum Kit's clips open on its pads, without the tools drums don't need

- **Status:** Proposed (2026-10-16, with [drum-kit.md](../design/drum-kit.md) §23)
- **Date:** 2026-10-16
- **Builds on:** [0064](0064-a-drum-kit-whose-pads-hold-any-instrument.md) (the Drum Kit), [0058](0058-the-piano-roll-writes-in-key-and-in-chords.md) (the piano roll writes in key and in chords), [0040](0040-a-menu-bar-and-everything-by-mouse.md) (the menu bar, everything by mouse), [0021](0021-mcp-server.md) (assistants)
- **Amends:** [0058](0058-the-piano-roll-writes-in-key-and-in-chords.md): its in-key default, for a kit's clips. It also moves the drum view, which 0058 left for M4, to M3.
- **Number:** 0065 is taken on the `synth` branch; renumbered at merge if main has used 0066

## Context
- At the Drum Kit's step 1 sign-off (2026-10-16), the product owner asked for a drum view. A clip on a kit's track should show only the rows of the pads with sounds, so a beat can be written without hunting through the roll. Tools that make no sense for drums, such as making chords, shouldn't show there.
- With a song key, ADR 0058 opens every clip In key: only the key's rows, with a chord lane naming what sounds together. On a kit's clip that's wrong twice over:
  - the key's rows aren't the pads;
  - a kick, a snare and a hat struck together get named as a chord.
- The field (drum-kit.md §23.1):
  - Live, Bitwig, Studio One with Impact, and Cubase show the pads with sounds by default.
  - Bitwig and Studio One leave the key's tools to the piano.
  - On note lengths it splits: Cubase and Studio One draw hits; Live, Logic and Bitwig keep lengths.

## Decision
- **A clip on a Drum Kit's track opens on its pads.**
  - Each row is a pad with a sound, named, lowest at the bottom.
  - A note on a pad without a sound keeps a hatched row, so nothing in the clip is hidden.
  - Notes keep their lengths.
- **One toggle per clip: the pads, or the whole roll.** It takes In key's place, with In key's memory and menu item, so a clip has one switch whichever view it's for.
- **On the pads, the surface shows what works on drums.** The chord lane, Draw's chords, In key, Others and the melodic tools leave the header, the Tools pane and the right-click menu. The menu bar keeps them, dimmed with the reason.
- **Assistants see the pads:**
  - `get_clip` names them;
  - `edit_notes` and `generate_notes` take a pad's name for a pitch.

## Consequences
- The piano roll gets a third way to make its rows, as a pure model function. Each tool in the catalogue says whether it works on drums.
- The Drum Kit's step 2 grows from macros alone to macros and the drum view. It's drawn and approved before it's built.
- Per-row mute, solo and length, and an order of your own, are left for a step sequencer.
