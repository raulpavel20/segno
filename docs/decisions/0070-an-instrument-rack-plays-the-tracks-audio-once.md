# 0070. An instrument rack plays the track's audio once, through its first chain heard

- **Status:** Accepted (product owner, 2026-10-16: "find a proper solution for B92 and implement it such that the issue is fixed")
- **Date:** 2026-10-16
- **Builds on:** [0019](0019-one-kind-of-track.md) (one kind of track: an instrument adds its sound to the audio that reaches it), [0056](0056-racks-are-modules-of-modules.md) (racks: chains side by side, summed), [0064](0064-a-drum-kit-whose-pads-hold-any-instrument.md) (the Drum Kit, built on a rack)
- **Answers:** open question B92
- **Number:** 0065 and 0069 are taken on the `synth` branch; renumbered at merge if main has used 0070

## Context
- **Every chain of a rack gets the rack's input,** and their outputs are added (ADR 0056). For an effect rack that's the point: a dry chain beside a crushed one is parallel processing, and two empty chains are the input twice, as in other DAWs.
- **An instrument adds its sound to the audio that reaches it** (ADR 0019). Any track holds audio clips and MIDI clips, and a track can listen to an audio input, so a track's audio reaches its instrument.
- **Together they double the track's audio.** In an instrument rack with two chains that start with an instrument, each chain passes the track's audio on with its instrument's sound, and the rack adds them. The track's audio clips play twice as loud, +6.0 dB (measured 2026-10-16 while building the Drum Kit's spike). Four chains make it +12 dB.
- **The Drum Kit doesn't have the problem.** Its pads start from silence, and the track's audio goes from the kit's input to its output once, by one connection of its own (drum-kit.md §20).
- **Other DAWs don't meet it,** because they don't let a track mix audio clips with an instrument's sound. In Live, a MIDI track's Instrument Rack gets no audio, so a chain without an instrument there is silent. Bitwig's hybrid tracks do, and its devices pass on audio that reaches them, but its documentation doesn't say what an Instrument Layer does with it.
- **Two things must not change:**
  - **grouping modules into a rack sounds as they did** (racks.md §15, tested). A track with an audio clip and a synth followed by a reverb plays the clip through the reverb, so it must still do that once the two are a rack;
  - **effect racks:** parallel processing is what they're for.

## Decision
- **An instrument rack plays the track's audio once, through its first chain heard:** the first chain that's on, or, while any chain is soloed, the first soloed one.
  - That chain gets the rack's input as any chain did. Its instrument adds its sound to the track's audio, and its effects process both, as on a track (ADR 0019).
  - **Every other chain starts from silence** and gets only the notes: its sound is its instrument's, through its own effects.
- **One chain stays as it was:** grouping a track's modules into a rack, and taking them out, doesn't change how the track's audio sounds.
- **With no chain heard,** nothing of the rack is heard, as in an effect rack. A rack turned off passes the track's audio, as any module turned off does.
- **Effect racks are unchanged:** every chain processes the rack's input, and the outputs are added.
- **The Drum Kit keeps its own way:** the track's audio goes round the pads, untouched. A kick's compressor shouldn't process the track's audio, and a kit is never grouped from a chain.
- **The connections follow from the chains** (`wireRack`) after every edit, as before. Turning chains on or off, soloing, moving an instrument to or from a chain's start, or adding or removing a chain, can move the track's audio to another chain. That move is part of the same undo step.

## Alternatives considered
- **Round the rack, as the Drum Kit does.** It's the simplest rule. But a one-chain rack's effects would no longer process the track's audio, so grouping a synth and its reverb would take the reverb off the track's audio clips.
- **Every chain gets the track's audio at 1/N of its level.** The sum is the audio once, and the chains are equal. But it needs a hidden gain at each chain's start, and it feeds compressors and distortion a quieter signal than the track's.
- **Always through chain 1.** It's simpler to say, but turning chain 1 off would silence the track's audio while chain 2 still plays.
- **Leave it.** Then any track with audio clips and a layered instrument rack plays its clips 6 to 12 dB too loud.

## Consequences
- **No change for most racks:**
  - layered instruments sound as before;
  - the track's audio is heard once, through the first chain heard;
  - effect racks and kits are unchanged.
- **In an instrument rack, a chain without an instrument is silent unless it's the first heard.** In Live that's what such a chain does on a MIDI track. An effect chain for the track's audio belongs after the rack, or in an effect rack.
- **An instrument that takes audio as its input** (a vocoder, ADR 0019; none yet) gets the track's audio only in the first chain heard.
- **Tests:**
  - an instrument rack of one to four chains plays the track's audio at its own level;
  - a one-chain rack's effects still process it;
  - turning chains off, or soloing one, keeps the audio heard once;
  - layered instruments still sum;
  - an effect rack still doubles its input with two empty chains.
