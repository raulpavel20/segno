# 0058. The piano roll writes in key and in chords: one surface, chords read from the notes

- **Status:** Accepted (product owner, 2026-10-14, with [piano-roll.md](../design/piano-roll.md))
- **Date:** 2026-10-14
- **Builds on:** [0057](0057-the-song-has-one-key.md) (the song's key), [0047](0047-the-editing-grid-in-each-editor.md) (each editor's grid), [0048](0048-clips-loop.md) (clip loops), [0027](0027-selection-and-context-panels.md) (one selection, context panels), [0028](0028-automation-lanes.md) (automation's gestures), [0040](0040-a-menu-bar-and-everything-by-mouse.md) (the menu bar, everything by mouse), [0021](0021-mcp-server.md) (assistants)
- **Number:** taken on a branch beside main; renumbered at merge if main has used it

## Context
- The product owner asked for a piano roll that's key-aware, writes chords, turns root notes into chords that fit the key, and has "a more modern set of features to make the piano roll actually good and up to standard" (2026-10-13, 2026-10-14).
- The current DAWs differ (research in [piano-roll.md](../design/piano-roll.md) §2, October 2026):
  - Bitwig 6 writes in one project key: snap to key, arrows by steps of the scale.
  - FL Studio stamps voice-led chords and shows other tracks' notes; it's the piano roll users call the best.
  - Cubase and Studio Pro suggest the next chord.
  - Live 12, Bitwig and Cubase 14 have per-note chance.
  - Live 12's MIDI Tools transform and generate notes.
  - None names the chords over the notes, writes them on a click in the song's key, and suggests the next one, all in the piano roll.
- Segno's piano roll has none of this; Segno has the song's key and its theory in `libs/music`.

## Decision
- **One surface, no tool palette.** Gestures and modifiers do the common things as now. A visible Draw choice (Note, Chord, Chord under) sets what a double-click draws.
- **In key by default:** with a song key, drawing and moving land on its notes (Alt bypasses), and the arrows move by steps of the scale.
- **Chords are read from the notes,** never kept as separate data. A chord lane under the ruler names them in the key. Clicking a name selects its notes; changing a chord rewrites only its notes, voice-led. A Chord popover offers the key's chords, suggestions for the next one, and borrowed chords, from a small explainable model.
- **Other tracks' notes** can be shown faint, never selectable.
- **Per-note chance and velocity spread** are decided from a seed, the note and the lap, so playback, bounce and export always agree.
- **Controllers** (pitch bend, mod wheel, sustain, any CC) are lanes in the clip view, edited as automation is.
- **Tools that transform and generate** are popovers that preview live: Apply is one undo step, Escape undoes nothing.
- **A Notes menu** joins the menu bar between Clip and Song.
- **Built in three stages:** in key, chords and the basics; then chance and controllers; then the tools. Each is signed off on its own.

## Consequences
- `libs/music` grows chords, voicings, voice leading, chord names and the suggestion model. `model/NoteEditing` grows every transform as a pure function. `model::Note` gains release velocity, mute, chance and velocity spread.
- The engine needs a small hook where tracktion plays a clip's notes, for chance (stage 2).
- New MCP tools: `get_chords`, `set_chord`, `make_chords`, `suggest_chords`, `transform_notes`, `generate_notes`, `get_controllers`, `set_controllers`; `edit_notes` and `get_clip` grow.
- The menu bar gains Notes; Clip ▸ Notes ▸ moves into it. The View menu gains the clip view's options.
- **Open until later:** grooves (M4), a drum view (M4), per-note MPE (M5), editing several clips at once, step input, chord names over arpeggios, chance groups and conditions.
