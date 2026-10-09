# 0028. Automation: every lane visible, as rows like tracks, on the control's scale

- **Status:** Accepted (product owner, 2026-10-09, with every recommendation of the design's review). Where lanes sit and what they belong to (lanes under their track, per-track folding, the lane's context, the per-track MCP listing) superseded by [0029](0029-independent-automation-lanes.md): lanes are rows of their own with assignments.
- **Date:** 2026-10-09
- **Builds on:** [0014](0014-flat-darker-ableton-adjacent.md) (the reserved gradient), [0016](0016-constant-device-latency.md) and [0020](0020-latency-follows-lookahead.md) (what can't be automated), [0021](0021-mcp-server.md) (MCP tools for every DAW feature), [0023](0023-ui-foundations.md) (the track header and time-view kits), [0026](0026-mixing-in-the-arrangement.md) (the mixer and its taper), [0027](0027-selection-and-context-panels.md) (one selection)

## Context
M2 needs automation (step C4): parameters that change over time, drawn, edited and recorded. Decisions:
- how lanes appear in the arrangement. The product owner's brief: lanes in the same visual language as tracks, and every automated parameter visible at once (in Live, a track shows one parameter's automation at a time and you pick which);
- what a straight line between two points means for parameters with skewed ranges (frequencies, times) and for volume;
- how the drawn curve stays the same as what plays;
- what happens when an automated control is moved;
- how recording works, and what is undoable.

tracktion_engine provides automation curves per parameter, playback in sub-blocks and a recording manager, with gaps: it draws and plays curve bends with different formulas, ignores one-point curves, interpolates our parameters in their plain units (hertz) and volume in its own fader units, and writes played values back into the project.

The design is in [design/automation.md](../design/automation.md).

## Decision
- **Lanes are rows like tracks.** Each automated parameter has its own lane under its track, with a header from the track header kit (the track's colour, the parameter's name, the module, the value, an on switch, the parameter's control when tall enough) and the curve on the arrangement's grid. All of a track's lanes show at once; they fold with the track. There is no per-track chooser.
- **A straight line is a straight move of the control.** Lanes span the control's travel. Our modules' parameters automate in normalised units (as plugin hosts already do); volume and sends automate on our taper (tracktion's fader mapping is patched to it). Projects saved earlier are converted when opened.
- **What you see is what plays.** One curve function in `model::Automation`, equal to the engine's; tracktion is patched so playback uses the drawn bend formula and a one-point lane holds its value.
- **Editing** uses the gestures of clips and notes; every gesture is one undo step.
- **Moving an automated control** while not recording turns its lane off (kept, faded) until turned back on, per lane or all at once from the transport bar. Lane on/off is a listening state, like solo: saved, never undoable.
- **Recording:** Automation arm and touch writing; one undo step per pass; lanes appear for what was written.
- **Automated controls show an indicator** (filled while the lane plays, a ring while it's off), and every control gets a right-click menu (show lane, clear, turn back on, reset). The host binding carries both, so module panels stay the same in plugin hosts, where they offer only reset.
- **Lanes and points join the one selection**; the context panels show a lane's module and a LANE or POINT section.
- **Assistants** get `get_automation`, `set_automation`, `clear_automation` and `set_automation_on`; lanes appear in `get_project`.
- **Not automatable:** latency and quality settings (ADRs 0016, 0020) and, in M2, mute.

## Consequences
- The arrangement's row model gains kinds (track, lane, return, master), and clip editing stops assuming that a row is a track.
- Three small tracktion patches join the pinned set (curve playback, one-point curves, the fader mapping), each with a test that fails without it.
- The project format changes for module parameters, volume and sends; the conversion of older projects is tested by rendering them before and after.
- Played automation never marks the project unsaved or rebuilds snapshots; the mixer's controls read live values while their lane plays.
- Gradients appear for the first time in the app, as roles (`automationFillTop`, `automationFillBottom`) under the UI lint.
- Range editing (a time range across clips and lanes), latch mode, moving automation with clips, mute automation and clip envelopes come later.
