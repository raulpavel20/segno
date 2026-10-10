# 0037. A pass makes its own clip; a take plays alone

- **Status:** Accepted (product owner, 2026-10-10, after trying C5b and C6)
- **Date:** 2026-10-10
- **Supersedes in part:** [0032](0032-takes-belong-to-clips.md): "a later pass over that clip adds its takes to it" for a pass over *part* of a clip; a clip playing parts of several takes after such a pass; and "another clip placed over part of the clip leaves the range … whole". The rest of 0032 stands: takes belong to clips, one take shows nothing, takes open under their clip, a clip and its takes are edited as one, the storage.
- **Builds on:** [0019](0019-one-kind-of-track.md) (clips never overlap; the placed clip wins), [0031](0031-recording-keeps-every-take.md) (every pass is kept; what a pass replaces is kept)

## Context
Trying C5b and C6, the product owner recorded a take, then a pass entirely inside it, then a longer pass over everything. The inner pass didn't make a clip. It joined the old clip's takes and made a comp: the new take inside, the old one around it. The long pass joined the same takes again.

Then choosing takes from the menu gave results that depended on the order of clicks. Choosing a take laid it only where it had audio and left the rest as it was. Choosing take 2 changed only the middle. Choosing take 1 left take 3's ends playing.

The product owner expected a new recording to make a new clip, as most DAWs do. A survey (Live 12, Logic, Pro Tools, Cubase, Studio One, REAPER, Bitwig) found the following.

- **Recording over part of a clip:**
  - Live's main lane and Pro Tools put a new clip there and split the old one around it.
  - REAPER's default splits the old item at the punch and keeps the covered audio as a take of the middle piece.
  - Logic builds a comp in the old take folder, which is what Segno did.
- **Recording again over the same range:** every one of them adds the pass as another take of the same clip, folder or lanes.
- **Choosing a take from a list:** Logic's take folder, Studio One's take list and Pro Tools' alternate takes play that take on its own.

## Decision
- **A pass makes its own clip**, over the range it recorded.
  - **A clip it covers in part is cut**, as any placed clip cuts one (ADR 0019): trimmed at an edge, split around a punch inside it.
  - **A clip it covers entirely gives its takes to the new clip.** A second pass over the same range, such as another loop pass, adds takes to the same clip.
  - **What played over the pass's range before it is kept as "Before take N"**, a take of the new clip, so choosing it brings back exactly what was there. It is left out when it would only repeat one of the clip's takes, such as a second loop pass over a clip playing take 3.
- **Choosing a take plays that take alone** across the clip's range, wherever the choice comes from: the menu, a strip, Alt+↑/↓, "Keep only this take", or an assistant.
  - Where the take has no audio, nothing plays, and the clip shows only what plays.
  - The clip keeps its range, so choosing a longer take fills it again.
  - The result never depends on which take played before.
  - A clip plays parts of several takes only when comping (C5c) makes it so.
- **A clip's range never reaches over another clip.** When another clip or a pass cuts a clip, its range shrinks to the part that stays. Choosing a take therefore changes only the clip you see, never a neighbour. Trimming the clip back out still brings its takes back.

The design is [design/recording.md](../design/recording.md) §6 and §20.

## Consequences
- Recording is predictable. A punch is a clip of its own, with what it replaced one click away, and loop passes still accumulate on one clip.
- The engine's pass import decides, for each clip a pass overlaps, whether to join its takes (covered entirely) or cut it (covered in part). It does this through a pure, tested plan (`model::passCover`). Settling a track clamps every clip's range between its neighbours.
- Choosing a take replaces the clip's parts. The per-part replacement after deleting a take stays as it is.
- Existing projects keep their clips and comps; nothing is converted. A range reaching over another clip is clamped the next time its track settles.
- The tests that encoded a partial pass joining takes are rewritten. The MCP `use_take` description says the take plays alone.
