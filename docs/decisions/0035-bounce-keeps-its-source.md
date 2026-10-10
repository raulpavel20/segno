# 0035. Bounce keeps its source: in place as a take, to a new track muted

- **Status:** Accepted (product owner, 2026-10-10, with every recommendation of design/bounce.md §14)
- **Date:** 2026-10-10
- **Builds on:** [0019](0019-one-kind-of-track.md) (one kind of track, no overlaps, the signal flow), [0029](0029-independent-automation-lanes.md) (lanes drive parameters on any channel), [0030](0030-projects-are-folders.md) (project folders; files never deleted implicitly), [0032](0032-takes-belong-to-clips.md) (takes on clips), [0033](0033-no-instrument-by-itself.md) (no instrument by itself)

## Context
M2 step C7 renders clips to audio ("bounce in place"). The plan said the MIDI is kept, muted. Three things have to be settled.

1. **What is rendered.** A Segno track holds audio and MIDI clips. Audio clips enter at the start of the chain, MIDI clips feed the first instrument, and the modules after the instrument process both (ADR 0019). If a bounce in place rendered the whole chain, it would then be processed by those modules a second time. Bitwig, whose tracks are hybrid too, renders only the instrument when it bounces in place, and its effects stay live.
2. **Where the source goes.** Clips on a track never overlap (ADR 0019), so a muted MIDI clip can't stay under its bounce. Takes (ADR 0032) are already where a clip keeps its other versions.
3. **Printing effects.** Every current DAW can also render a part through the whole channel, usually onto a new track with the source muted or deactivated (Live 12.2, Logic, Cubase, Studio One). Segno has no clip mute yet.

The design is [design/bounce.md](../design/bounce.md).

## Decision
1. **Bounce in place renders up to where the clip enters the chain:** a MIDI clip through the track's modules up to and including its instrument, an audio clip as it plays. The modules after that point, the mixer and the sends keep running on the result, so the track sounds the same (it nulls) and stays editable.
2. **The source of a bounce in place becomes a take** of the bounced clip ("Before bounce", with the bounce as "Bounce"). Going back to the MIDI is choosing a take, at any time.
3. **Bounce in place isn't offered** where it couldn't sound the same: a MIDI clip on a track without an instrument, or with audio modules before its instrument. The reason is shown.
4. **Bounce to new track** renders the clips through the whole chain before the fader, onto a new track right after the source. The new track copies the source's volume, pan and sends, and joins the lanes driving them. The source clips are muted, so the mix sounds the same.
5. **Clips can be muted:** a muted clip keeps its place and plays nothing.
6. **Bounces render offline on a copy of the project** and never stop playback. Each lands as one undo step into `Bounces/`, as 32-bit float WAV. Its files are never deleted implicitly.

## Consequences
- A clip's takes can be of both kinds (the MIDI it came from, and the audio bounce). The take code copies clip states whatever their kind; tests cover the mixed stacks.
- Clip mute is new state: in the snapshot, the project (tracktion's own clip mute), drawing, the inspector, a shortcut and MCP.
- The clip gets a right-click menu, built from the action registry.
- A new shared `Progress` component shows work in progress on what it changes; export (C8) uses it too.
- Freeze isn't built in M2; it's revisited with racks (M4).
- `Session::render` gains clip filtering, tails and a cut chain, which export (C8) can use as well.
