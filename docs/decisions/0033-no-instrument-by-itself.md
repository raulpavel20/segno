# 0033. Segno never adds an instrument by itself

- **Status:** Accepted (product owner, 2026-10-10)
- **Date:** 2026-10-10
- **Supersedes:** [0019](0019-one-kind-of-track.md) item 4 ("A MIDI clip needs an instrument": dropping MIDI on a track without one adds the default instrument)
- **Builds on:** [0019](0019-one-kind-of-track.md) (one kind of track)

## Context
Since M2's piano roll, a MIDI clip made on a track without an instrument (a double-click, Ctrl+Shift+M, a dropped `.mid`, an assistant's `create_midi_clip`) brought the Subtractive Synth with it, so it would make a sound at once. Trying Capture MIDI, the product owner rejected this: the default instrument is rarely the one wanted, it clutters chains with modules nobody chose, and users load an instrument when they decide to.

There is no technical reason for it. tracktion plays a MIDI clip on a track without a synth as silence: the clip's MIDI reaches the chain, effects pass it through, and nothing turns it into audio.

## Decision
- **Segno never adds an instrument by itself.** Every way of making a MIDI clip (drawing, Ctrl+Shift+M, dropping a MIDI file, Capture MIDI, recording, assistants) puts a plain MIDI clip on the track. It is silent until the user adds an instrument.
- **The track says so:** its header shows "No instrument" while it has MIDI clips and nothing to play them (as before).
- **Live MIDI** still follows the selected track that has an instrument (0019 item 5): with no instrument there is nothing to hear.

## Consequences
- Fewer surprises in chains; one less undo side effect.
- A new MIDI clip on an empty track is silent: the header's "No instrument" and the module chain's + are how the user finds out and fixes it.
- MCP's `create_midi_clip` and `insert_midi_file` no longer report `addedInstrument`; tests that relied on the automatic synth add one explicitly.
