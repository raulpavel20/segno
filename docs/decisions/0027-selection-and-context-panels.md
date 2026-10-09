# 0027. One selection; the context panels follow it, per view

- **Status:** Accepted (product owner, 2026-10-09)
- **Date:** 2026-10-09
- **Builds on:** [0018](0018-daw-app-structure.md) (views draw from snapshots and view state), [0023](0023-ui-foundations.md) (shared components), [0026](0026-mixing-in-the-arrangement.md) (the mixer view, which this refines)

## Context
The feel check of the mixer (M2 step C3) found the context panels confusing:
- In the arrangement, clicking a track's header highlighted the track, but the detail panel and the inspector kept showing a clip selected earlier, and its track.
- The inspector listed the clip's and the track's properties without saying which was which.
- In the mixer view, the detail panel still offered the clip view and its piano roll, and after a module-name click it stopped following the selected strip. The inspector repeated the strip's controls and didn't follow either.

The cause was structural: the selected track and the selected clips were two separate things, and the panels let a selected clip win over a newer track selection. Nothing written said what the panels should show in each view; ADR 0026 only said they "stay".

## Decision
- **One selection, shared by both views:** a channel and any clips. The selected channel is always the channel of what was selected last: selecting a track, a return, the master or a strip clears the clips; selecting a clip selects its track. Each action changes the selection once.
- **The context panels follow the subject** (one clip, several clips, a channel, or nothing), decided by one pure, tested function of the selection, the project and the current view.
- **Arrangement:** the detail panel shows a single MIDI clip in the clip view and anything else as the selected channel's modules; Tab switches until the subject changes. The inspector shows the subject in **titled sections separated by a line** (Clip or Clips, Track or Return or Master, Mix, Sends), built from a shared `fd_ui` part.
- **Mixer view:** the detail panel shows only the selected channel's **modules** (no clip view, no tabs). The **inspector is not shown**: the strips already carry every channel control, and get its width. Actions on arrangement content you can't see (clip edits, zoom, Tab) are off.
- Each view keeps its own panel state: returning to the arrangement restores its detail mode and its inspector.

The rules are in [design/selection.md](../design/selection.md).

## Consequences
- ADR 0026's "the browser, inspector and detail panel stay" now reads: the browser and the detail panel stay; the inspector belongs to the arrangement.
- The selected track and a selected clip can no longer disagree, so track actions (Alt+M, Alt+S, the computer keyboard, live MIDI) always act on what the panels show.
- New panel content starts as a row in the selection doc's tables and a case in the rule function's tests; future selectable things (automation lanes in C4, a module's or a note range's properties) extend the same model rather than adding their own.
- The inspector's sections give later features (input and output, recording, automation) an obvious place.
- Multi-track selection (for mix edits on several tracks) still waits; when it comes, it extends the channel to a set with the same invariant.
