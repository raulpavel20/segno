# 0019. One kind of track: audio and MIDI clips together, never overlapping

- **Status:** Accepted (product owner, 2026-10-09). Item 4 (a MIDI clip brings the default instrument) superseded by [0033](0033-no-instrument-by-itself.md): Segno never adds an instrument by itself.
- **Date:** 2026-10-09
- **Builds on:** [0018](0018-daw-app-structure.md) (Session, snapshots)

## Context
Most DAWs, Ableton included, make you choose up front between an audio track and a MIDI track. A part often needs both: a sampled loop and a played line for the same sound, or a MIDI part next to its rendered audio. Reaper, Bitwig ("hybrid tracks") and Waveform already allow both on one track. tracktion_engine has a single track type that holds both clip kinds, and the app already uses only that type; "Audio" or "Instrument" was only a label derived from the chain.

Two questions need answers.
1. **Overlaps:** may an audio clip and a MIDI clip play at the same time on one track?
2. **Signal flow:** what happens to a track's audio when its chain has an instrument? Before this decision our instruments overwrote the track's audio, so an audio clip on a track with a synth was silent.

## Decision
1. **One kind of track.** Any track holds audio clips and MIDI clips. There is no track type to choose. A track's header shows its instrument, if it has one.
2. **No overlaps on a track, of any kind.** A track plays one clip at a time; layering means another track.
   - The clip you place wins. Moving, dropping, duplicating or extending a clip over others cuts what it covers: covered clips are removed, partly covered ones trimmed, and a clip covered in its middle is split around it.
   - The cut is part of the same undo step as the placement.
   - Blending between neighbouring audio clips is a crossfade at the boundary (with clip fades, M2), not an overlap.
3. **Signal flow** follows the chain, left to right.
   - Audio clips enter at the start of the chain; MIDI clips feed the first instrument.
   - **An instrument adds its sound to the audio that reaches it and never mutes it.** So an instrument's release tail rings on into a following audio clip, and audio passes through a track whose instrument is silent.
   - Effects before the instrument process only the audio; effects after it process both.
   - Third-party instruments with no audio input replace their buffer in practice. The engine adds the incoming audio back to their output (a small tracktion patch), so the rule holds for every instrument.
   - Instruments that take audio as an input (a vocoder, say) process it instead.
4. **A MIDI clip needs an instrument.** Dropping MIDI on a track without one adds the default instrument, in the same undo step, and says so; it can be swapped in the chain. A track that has MIDI clips but no instrument says so in its header.
5. **Live MIDI** keeps following the selected track that has an instrument.

## Consequences
- **Easier:**
  - there is no track-type choice;
  - a part's audio and MIDI share one lane, one chain and one mixer channel;
  - bounce in place (render MIDI to audio on the same track) becomes natural when recording lands (M2).
- **Must maintain:** the tracktion patch for third-party instruments (`cmake/patches/`), checked when tracktion is updated.
- **Clip editing:** every placement resolves overlaps. The pure part (`model::clearRange`) is unit-tested; the Session applies it.
- **Recording (M2):** a track can listen to an audio input and a MIDI input at once; recording creates clips of the matching kind.
