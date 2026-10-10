# Clip fades and crossfades: design (M2 step C6)

How audio clips start and stop without clicks: **edge fades** that need no thought, **fades** you draw on a clip, and **crossfades** where two audio clips meet. The decision is [ADR 0036](../decisions/0036-crossfades-at-the-boundary.md). It builds on one kind of track and its rule that clips never overlap ([ADR 0019](../decisions/0019-one-kind-of-track.md): "blending between neighbouring audio clips is a crossfade at the boundary, not an overlap"), takes on clips ([ADR 0032](../decisions/0032-takes-belong-to-clips.md)), automation's bends ([automation.md](automation.md) §5), the time-view kit ([ui-foundations.md](ui-foundations.md)) and bounce ([bounce.md](bounce.md), whose cut tails already fade).

**Status:** proposed (2026-10-10). The recommendations in §12 await the product owner's review. Nothing is built yet.

**Where the field is (2026):** fades are non-destructive clip properties everywhere: a length and a curve, set with handles at a clip's top corners and in an inspector, linear by default (Studio One, Pro Tools). Crossfades come in two models:
- **Real overlap on the lane:** REAPER, Cubase, Studio One and Logic's X-Fade mode crossfade where clips overlap.
- **Clips edge to edge, playing hidden audio past the join:** Live (drag a fade handle over the neighbour's edge, or Create Crossfade), Pro Tools (butted clips, a crossfade centred on the splice, before it or after it) and Bitwig's pre-fade.

Clicks at edges are handled three ways:
- **Visible floor fades:** Live's "Create Fades on Clip Edges" puts a 4 ms fade on every clip edge, visible and editable; deleting a fade resets it to 4 ms, and comp segments get 4 ms crossfades.
- **Cut edges only:** Bitwig fades automatically at split points only, never at a file's own start or end.
- **Hidden fades:** Cubase and Pro Tools play auto fades that aren't shown (off by default in Pro Tools; Steinberg warns of their CPU cost).

Crossfade curves: Pro Tools and Cubase offer equal power (for unrelated material) and equal gain (for phase-coherent material), Logic an equal-power crossfade pivoting on the junction; curvature is adjustable everywhere.

Segno follows the edge-to-edge model, which ADR 0019 already chose, and combines the best of the rest: Live's visible floor fades, but only on cut edges as in Bitwig; Pro Tools' placement and its clear refusal when there's no hidden audio; one bend control shared with automation; equal power by default, with equal gain a click away and the guidance for each.

## 1. Principles

1. **Every edit is click-free.** An audio clip never starts or stops abruptly, whether or not anyone thought about it. Edge fades do it unseen; where a clip runs straight on into the next part of the same recording, nothing needs fading.
2. **Fades belong to the clip.** They move, trim, split, copy and bounce with it. They aren't automation.
3. **Crossfades sit at the boundary.** Clips still never overlap (ADR 0019). A crossfade plays each clip's spare audio past the join (its *handle*), so the arrangement keeps its plain shape.
4. **One curve language.** A fade bends like an automation segment, with the same gesture, drawn by the same function the audio uses (the UI never reimplements DSP).
5. **On the clip, no dialog.** Handles on the clip make and shape fades; a menu and the inspector refine them.

## 2. Edge fades (automatic)

- **Every cut edge of an audio clip fades over 4 ms:** a start trimmed in from its file's beginning, an end trimmed back from its file's end. A file's own start and end aren't faded: they're where the recording or the sample begins and ends, often a transient worth keeping (Bitwig's rule).
- **They're visible, as fades are** (§3), and they're a floor: a fade made shorter than 4 ms on a cut edge, or removed, is 4 ms again (Live's rule). Nothing is played that isn't shown.
- **Seamless joins get none.** Where a clip meets the next part of the same file at the same place in it (a split, a bounce run, comp parts from one take), the audio already runs on without a break; a fade there would dip.
- **A longer fade or a crossfade** on an edge replaces its edge fade.
- **On by default, per project:** an "Edge fades" toggle (the command palette, and saved with the project) turns them off for sample-accurate editing, such as cutting a drum hit right at its attack.
- **MIDI clips** have no audio to fade: the instrument starts and ends its notes.

## 3. Fades

**Making them.** Each audio clip has a fade-in and a fade-out handle: small squares at the top corners of its body, under the title strip. They show when the pointer is over the clip, and always on the selected clips.

| Gesture | Does |
|---|---|
| Drag a fade handle inward | Sets the fade's length. Free, not snapped (Shift: fine). At most the clip's length less the other fade; while dragging, a faint mark shows how far it can go |
| Drag the dot in the middle of a fade's line up or down | Bends it (−100 % … +100 %). Alt + drag on the line does the same, as on an automation segment; Alt + double-click straightens it |
| Double-click a fade handle | Removes the fade (a cut edge keeps its 4 ms edge fade) |
| Drag a fade-out handle past the clip's end into the clip after it (or a fade-in handle back into the one before) | Makes a crossfade there (§4) |

**Drawn:** the faded part of the clip is shaded (the curve's area above it in `surface0` at the faded alpha), with the curve as a line in `textPrimary`. The waveform stays the source's; the shading shows the fade.

**The curve:** a fade is a segment from silence to full level (fade-in) or back (fade-out) with a bend, the same function as an automation segment (`model::automationValueAt`). Its gain is that curve as a level, so a straight fade is a straight ramp of gain. New fades start straight (linear), the common default. Lengths are times, not beats: a fade keeps its duration when the tempo changes.

**In the inspector:** CLIP gains Fade in and Fade out (lengths you can type, in ms) and their bends.

**With edits:**
- moving and duplicating keep a clip's fades;
- trimming an edge keeps its fade's length (shortened if the clip gets shorter than its fades);
- splitting gives the left part the fade-in and the right part the fade-out; the new inner edges are seamless, so they need none;
- a clip's takes keep their own fades: choosing a take uses its clip as stored;
- bouncing in place bakes the fades into the new audio (bounce.md §3).

## 4. Crossfades

**Where:** between two audio clips that meet (the first ends where the second starts) on a track.

**Making one:**
- drag a fade handle across the join (§3): the crossfade's length follows the drag;
- or select clips and press Ctrl+Shift+X (**Crossfade**): every join between selected clips that meet gets a 10 ms crossfade (enough to hide a join without smearing it), and a join that has one keeps it. With no joins in the selection, it says so.

**Where it sits:** centred on the join, playing each clip's handle: the first clip's audio after its end, the second's before its start. Drag its middle sideways to move it before or after the join (Pro Tools' pre and post crossfades), within the handles. If one side has no spare audio (a file starts or ends exactly there), it slides to the side that has it. If neither has enough, it's as long as the handles allow, and with none at all it can't be made: "No audio beyond these clips' edges to crossfade: trim them shorter first." Recordings, splits, bounces and comps always have handles; separate files placed edge to edge may not.

**Shapes:** Equal power (the default: constant loudness across different material, such as two takes), Equal gain (straight ramps, for the same material, which would bulge with equal power: a split of one recording, a loop to itself), and S-curve. The crossfade's right-click menu and the inspector choose, each with that one line of guidance.

**Drawn:** both curves across the crossfade's span over the join, the span shaded as fades are, a handle at each end to resize it, and its middle to drag it sideways.

**With edits:**
- a crossfade belongs to the join. Moving either clip away from the other removes it (undo brings it back); moving them together keeps it;
- trimming at the join (the join moves) keeps the crossfade, limited to the handles at its new place;
- deleting either clip removes it;
- selecting the crossfade (a click on its span) and pressing Delete removes it.

## 5. Comping joins (C5c)

Comping (recording.md §7) makes clips that meet at joins and play parts of different takes. Each join gets a 10 ms equal-power crossfade automatically (Live gives comp segments 4 ms; 10 ms is the usual punch-in crossfade and hides a join between two performances better). Take files always cover the whole clip, so the handles are there.

## 6. Menus, keys and the inspector

- **A fade's or crossfade's right-click menu:** its shapes (ticked), Straighten (fades), Remove.
- **The clip's menu** (bounce.md §10) gains Crossfade.

| Action | ID | Shortcut |
|---|---|---|
| Crossfade the selected clips' joins | `clip.crossfade` | Ctrl+Shift+X |
| Edge fades on or off (the project) | `project.edgeFades` | (palette) |

## 7. Assistants (MCP)

| Tool | Arguments | Result |
|---|---|---|
| `set_fades` | `clips`, `in?` / `out?` (a length in beats, ms or text such as "10 ms"; 0 removes), `inBend?` / `outBend?` (−1 … 1) | The clips' fades as set. One undo step |
| `crossfade` | `clips` (their joins), `length?`, `shape?` ("equal power", "equal gain", "s-curve"), or `remove: true` | Each join: its crossfade's span and shape, or why it couldn't be made |

Clip results gain `fadeIn`, `fadeOut` (length and bend) and `crossfadeIn` / `crossfadeOut` where they have one. `get_project` reports the edge-fade setting; `set_project` (or the palette action) changes it.

## 8. Model and engine

- **`model::Fades`** (JUCE-free, `[fast]`): the gain of a fade at a time (the automation segment function) and of each crossfade shape; where a crossfade sits for given handles; which edges are seamless; each clip's *playback*: how far it plays past its start and end (crossfade handles) and the fades it plays with (its own, edge fades, crossfades), from its neighbours on the track. The arrangement draws the same functions.
- **Storage** on the clip's state, undoable: the fade lengths in tracktion's own `fadeIn` and `fadeOut` (so its copies and splits carry them), and Segno's bends, crossfade lengths, offsets and shapes as `fd…` properties. A crossfade is stored on both clips (each its half); the model drops it when they no longer meet.
- **Engine:** a small tracktion patch (`cmake/patches/tracktion_engine-clip-playback-hook.patch`) lets the engine behaviour adjust where an audio clip's node plays (its range reaching into the handles, its offset moved to match) and supply its fade node. Segno's behaviour asks `model::Fades` for the clip's playback and builds a fade node that applies the model's gain. Offline renders (the render tool, bounce, export) build the same graph, so they hear the same thing.
- **The bounce's cut fade** (bounce.md §7) becomes a plain fade-out the user can see and change.

## 9. Tests (Done when)

- **Click-free:** clips of a sine cut at random points, played back to back and apart, score as smooth (`testkit::clickScore`) with edge fades on, and click with them off (the test can tell); a seamless split doesn't dip (it nulls against the unsplit clip).
- **Fades:** a fade-in's rendered gain follows the model's curve for straight and bent fades (within 0.1 dB); fades move, trim and split as §3 says; one undo step per gesture.
- **Crossfades:** equal power keeps the level of two uncorrelated noises across the join within 0.5 dB; equal gain sums a split sine back to the original (it nulls); a crossfade slides to the side with handles and refuses without any; moving a clip away removes it.
- **Model:** curves, placement, seamless detection, playback per clip (`[fast]`).
- **UI:** handles, the shading, the crossfade's drawing and menus (screenshots at standard and compact widths, zoomed); the shell drags a fade and a crossfade end to end.
- **MCP:** `set_fades`, `crossfade`.
- **Manual:** `manual-tests/M2.md`, "Fades (C6)".

## 10. Building it

1. **Spike:** the patch and a fade node that plays a clip's handle, through an offline render, with a crossfade that nulls.
2. Fades and edge fades, then crossfades, each with their MCP tools and tests. Then C5c's comping builds on the crossfades.

## 11. What comes when

| Part | When |
|---|---|
| Edge fades, fades, crossfades, MCP | M2 (C6) |
| Comping's crossfaded joins | M2 (C5c, right after) |
| Fades on a time range across clips (one gesture fading several) | With range editing |
| Clip gain and its envelope | Later ([innovation-backlog.md](../innovation-backlog.md)) |
| Pre-fades: a clip's start fading in from earlier hidden audio, keeping its first note at full level (Bitwig) | When asked |
| Fades in MIDI clips (REAPER scales velocities) | Not planned: MIDI has its own tools |

## 12. Questions and recommendations (for review)

| # | Question | Recommendation |
|---|---|---|
| Q1 | **Edge fades** | 4 ms on cut edges only (not a file's own start or end, not seamless joins), visible and a floor, on by default with a project toggle (§2) |
| Q2 | **Fade handles** | Corners of the clip's body on hover and on selected clips; free drag with a mark showing how far it can go; a dot to bend, Alt+drag as in automation (§3) |
| Q3 | **Fade curve** | One bendable curve shared with automation; new fades start linear; lengths in time, not beats (§3) |
| Q4 | **Crossfades and the no-overlap rule** | At the boundary, playing each clip's handle; clips never overlap (ADR 0036, §4) |
| Q5 | **Making a crossfade** | Drag a fade handle across the join, or Ctrl+Shift+X on selected clips with the default length (§4) |
| Q6 | **Placement** | Centred, draggable before or after the join within the handles, sliding to the side with spare audio; refused without any (§4) |
| Q7 | **Crossfade shapes** | Equal power (default), equal gain, S-curve (§4) |
| Q8 | **Default crossfade length** | 10 ms, for Ctrl+Shift+X and comp joins (§4, §5) |
| Q9 | **Comp joins** | Automatic 10 ms equal-power crossfades (§5) |
| Q10 | **MIDI clips** | No fades (§2) |
