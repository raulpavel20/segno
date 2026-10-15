# 0063. Audio clips are edited as their own settings: warped in real time, untouched at their own tempo, in the song's key when asked

- **Status:** Accepted (product owner, 2026-10-15, with [audio-editor.md](../design/audio-editor.md) and its rule for every editor's tools, §20)
- **Date:** 2026-10-15
- **Builds on:** [0048](0048-clips-loop.md) (clip loops and the audio clip view), [0036](0036-crossfades-at-the-boundary.md) (fades and crossfades), [0035](0035-bounce-keeps-its-source.md) (bounce), [0030](0030-projects-are-folders.md) (files are never changed or deleted implicitly), [0057](0057-the-song-has-one-key.md) (the song's key), [0058](0058-the-piano-roll-writes-in-key-and-in-chords.md) (the piano roll's surface), [0027](0027-selection-and-context-panels.md) (one selection, context panels), [0040](0040-a-menu-bar-and-everything-by-mouse.md) (the menu bar), [0021](0021-mcp-server.md) (assistants)
- **Moves:** M4's warping and time-stretch to M3 (P6), at the product owner's request (2026-10-15)
- **Number:** taken on a branch beside main's P5; renumbered at merge if main has used it

## Context
- The product owner (2026-10-15) asked to finish the audio side of the clip view: "both MIDI and audio fully editable. Right now it's just a visualiser", researched against modern DAWs.
- Today an audio clip can be cut, moved, faded, looped and comped. It has no gain, pitch, reverse or tempo, and its clip view only shows its waveform.
- **The field** (audio-editor.md §2, October 2026):
  - Every DAW sets a clip's gain, transpose and fine tune in an inspector, without touching its file.
  - Every DAW but Logic has a gain line inside the clip.
  - Every one warps audio to the song's tempo, with markers on found transients. Live's model is the reference.
  - Each offers 4–10 stretch algorithms, which do three jobs: slicing for drums, stretching anything, varispeed.
  - None plays any audio in the song's key from a key it finds; Logic does it for Apple Loops, which carry theirs.
- tracktion_engine already warps in real time: markers, a tempo and a key to follow, pitch shifting. Signalsmith Stretch is built in.

## Decision
- **The file is never changed.** Gain, a gain line, transpose and fine tune, reverse, polarity, channels, warping and its markers are settings of the clip, kept in the project.
- **Nothing is stretched that needn't be:** a clip at its own tempo and pitch plays its file sample for sample, warped or not, and a test holds it to that.
- **Warping runs in real time** (tracktion's real-time path, no proxy renders); bounces and exports render through the same path.
- **Three modes, named for their job:** Beats (Segno's own player: each piece plays from its transient), Tones (Signalsmith Stretch, formants as an option) and Repitch (speed and pitch together). When Warp goes on, the audio chooses Beats or Tones, and the choice is shown.
- **What follows the tempo by default:** loops, recordings, bounces and comps do. One-shots and long files from elsewhere don't.
- **Warp markers** take Live's gestures in Segno's grammar: drag a transient to pin it, double-click to add or remove, Shift+drag to slide the audio, Alt to leave the grid.
- **The gain line belongs to the audio:** its points are pinned to moments of the file, so it loops, stretches, splits and flips with the audio.
- **The clip's key** is read or found, shown, and correctable. **Follow key** plays a clip in the song's key by the smallest step; it's off by default, because a found key is a guess.
- **The piano roll's places:**
  - the editor opens when one audio clip is selected;
  - properties sit in an AUDIO inspector section;
  - what's used while working is on the editor's bar, one click each: Gain, Transpose, Reverse, Warp with its mode, tempo, ×2 and ÷2, Quantise, and Tempo, Normalise and Slice opening the Tools pane at their tool; a range's commands while one is selected (the product owner's rule at approval: tools on screen, never two clicks deep);
  - a new Audio menu sits between Notes and Song.
- **Built in three stages:**
  - the editor and the clip's sound;
  - warping, behind an engine spike;
  - key and slices.

  Each is signed off on its own.

## Consequences
- `model::ClipInfo` gains an audio clip's settings, its warp markers and its gain line. `model/AudioEditing` and `model/AudioToolkit` hold the pure functions.
- `libs/dsp` gains onsets, tempo, percussiveness and pitch-class profiles. `libs/music` gains a key from a profile.
- The engine:
  - patches Signalsmith's formants into tracktion's wrapper;
  - adds a Beats player behind the clip-playback hook;
  - plays the gain line, polarity and channels in Segno's clip node;
  - sets each clip's `LoopInfo` before its stretch mode, so loops survive warping (B65).
- Some edits reach outside the editor:
  - the arrangement gains a gain handle on audio clips, and draws them warped and through their gain;
  - selection.md's table opens the editor for one audio clip.
- New MCP tools: `set_audio`, `set_warp_markers`, `get_transients`, `get_gain_line`, `set_gain_line`, `edit_audio_range`, `list_audio_tools`, `transform_audio`, `detect_tempo`. `get_clip` and `detect_key` grow.
- **Open until later:**
  - stem separation and audio to MIDI (proposed as the next step, its models in the Library);
  - a pitch line;
  - slices into a Sampler or a Drum Rack (M4);
  - Melodyne-style pitch editing through ARA;
  - several clips in the editor;
  - the song's tempo following a free-time recording.
