# 0056. The song has one key, which everything in Segno follows

- **Status:** Accepted (product owner, 2026-10-14, with [key-and-scale.md](../design/key-and-scale.md))
- **Date:** 2026-10-14
- **Builds on:** [0034](0034-device-api-frozen.md) (the frozen device API grows by additions), [0040](0040-a-menu-bar-and-everything-by-mouse.md) (the menu bar, everything by mouse), [0049](0049-the-master-row-is-the-song-row.md) (the song row), [0021](0021-mcp-server.md) (assistants), [0052](0052-generic-names-are-free.md) (generic names)

## Context
- The product owner asked for a key and scale for the project (2026-10-13): "hard to work without". It comes before the key-aware piano roll (P4).
- The Push design, the Wavetable's Key Chord and Snap to Key, and M4's Scale, Chord and Arpeggiator all wait on a key that Segno doesn't have.
- DAWs differ (research in [key-and-scale.md](../design/key-and-scale.md) §2, October 2026). Live 12 gives each clip its own scale, and its FAQ exists mainly to explain that the control bar's scale isn't the Set's. Logic, Studio Pro and Bitwig 6 (new in March 2026) keep one key for the project, with changes on a timeline. Studio Pro starts with no key, Logic with C major. Live's and Bitwig's own modules follow the key; no plugin format carries one in practice.
- tracktion_engine already keeps a pitch sequence in every project (a root and one of 11 scales per change), saved with it and moved by its insert and delete edits.

## Decision
- **The song has one key:** a root and a scale from a shared catalogue, or no key. It belongs to the song, not to clips or tracks. A new project has no key.
- **A key describes; it never changes anything by itself.** Setting it moves no note and changes no sound. Fitting notes to it is an explicit command (P4).
- **It's set in the top bar,** after the time signature, through a Key popover: the root on one octave of keys, the scales in a searchable list, and keys found from the notes, offered and never set by themselves. Song ▸ Key… and No key are in the menu bar. Each change is one undo step.
- **36 scales,** each with a permanent ID. Note names are spelled by the key.
- **Modules see it** through `TransportInfo::key` (the root and the key's notes), an addition to the frozen API. A module that follows the key works without one.
- **Shared theory lives in a new JUCE-free `libs/music`:** scales, keys, spelling and detection, used by the app, the Push support and modules.
- **Stored in the engine's pitch sequence,** with Segno's scale ID beside it.
- **Changes of key along the song come later,** as marks on the song row that move with their sections. Until then the model, the engine and the device API already ask for "the key at a time".

## Consequences
- New: `libs/music`, `TransportInfo::key`, `Session::setKey`, `model::KeyMap` in the snapshot, the top bar's key field and the Key popover, the piano roll's rows by key (a new colour role, `keyRowRoot`), the Song menu's Key… and No key, and the MCP tools `set_key`, `list_scales` and `detect_key`, with the key in `get_project`.
- `docs/ids.md` lists the scale IDs, and `docs/architecture.md` lists `TransportInfo::key` with the frozen contract's additions.
- The Wavetable's step 3 (W11), the Push's Scale page and M4's MIDI modules build on it. P4 makes editing follow it.
- **Open until asked:** your own scales, and finding the key from audio.
