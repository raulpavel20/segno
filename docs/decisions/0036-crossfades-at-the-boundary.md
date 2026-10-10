# 0036. Crossfades sit at the boundary; every clip edge is click-free

- **Status:** Proposed
- **Date:** 2026-10-10
- **Builds on:** [0019](0019-one-kind-of-track.md) (one kind of track; clips never overlap; "blending between neighbouring audio clips is a crossfade at the boundary, not an overlap"), [0028](0028-automation-lanes.md) (bends), [0032](0032-takes-belong-to-clips.md) (takes on clips)

## Context
M2 step C6 adds fades and crossfades to audio clips; comping (C5c) needs crossfaded joins. Two things have to be settled.

1. **How a crossfade plays when clips never overlap.** Many DAWs crossfade by overlapping clips (REAPER, Cubase, Studio One, Logic's X-Fade mode): both play across the overlap, each fading. tracktion_engine works that way too. Segno's clips never overlap (ADR 0019), which keeps editing simple: the placed clip wins, and a clip's edges are where it starts and stops. The alternative keeps the clips edge to edge and lets each one play a little of its source past the join (its *handle*) while the crossfade lasts, as Live and Pro Tools do.
2. **Clicks.** An audio clip that starts or stops mid-waveform clicks. Live fades every clip edge by 4 ms, visibly; Bitwig fades only edges that were cut; Cubase and Pro Tools can play fades that aren't shown.

The design is [design/fades.md](../design/fades.md).

## Decision
1. **A crossfade belongs to the join between two audio clips that meet.** The clips stay edge to edge in the arrangement. While the crossfade lasts, each clip plays its handle past the join, fading. A crossfade is centred on the join, can be moved before or after it within the handles, slides to the side that has spare audio, and can't be made where neither clip has any.
2. **Clips still never overlap.** Moving either clip away from the other removes their crossfade.
3. **Every cut edge of an audio clip fades 4 ms automatically, visibly:** not a file's own start or end, not where the clip runs on seamlessly into the next part of the same file, and not where a longer fade or crossfade is. A project setting turns it off.
4. **Fades belong to clips** and bend like automation segments, with the same function for the sound and the drawing.
5. **The engine plays it through a small tracktion patch:** the engine behaviour may extend where an audio clip plays (into its handles) and supply its fade node. Segno's model decides both.

## Consequences
- Editing keeps one rule (no overlaps); crossfades follow joins.
- Recordings, splits, bounces and comps have handles; files placed edge to edge without spare audio can't crossfade until they're trimmed.
- One more tracktion patch to maintain (`cmake/patches/`), checked when tracktion is updated.
- Offline renders (the render tool, bounce, export) play crossfades and fades exactly as playback does.
- C5c's comping gets crossfaded joins from the same mechanism.
