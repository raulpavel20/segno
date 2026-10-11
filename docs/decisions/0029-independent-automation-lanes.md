# 0029. Automation lanes are rows of their own, with assignments

- **Status:** Accepted (product owner, 2026-10-09); a lane that drives only a deleted track goes with it: amended by [0041](0041-tracks-and-clips-managed-in-place.md). 0041 also had the lanes right after a track move with it; [0042](0042-every-row-moves-on-its-own.md) took that back, so a lane stays where it was put again
- **Date:** 2026-10-09
- **Supersedes in part:** [0028](0028-automation-lanes.md) (where lanes sit and what they belong to)
- **Builds on:** [0027](0027-selection-and-context-panels.md) (one selection), [0021](0021-mcp-server.md) (MCP tools for every DAW feature)

## Context
ADR 0028 put each automated parameter on its own lane under its track. After testing it, the product owner found that too restrictive: a lane was welded to one parameter of one track, so a curve that should move several things (a filter on the synth and a send on the drums) had to be drawn several times, and a lane couldn't exist before deciding what it moves.

The engine of 0028 makes a different arrangement cheap. A lane's values are a control's travel (0 … 1), and tracktion's curve is linear in values, so any curve can be copied onto any parameter through a range without changing its shape.

Separately, the lane picker and the command palette closed only when their text field lost keyboard focus, so clicking the arrangement (which takes no focus) left them floating.

## Decision
- **An automation lane is a row of its own** in the arrangement, among the tracks: it sits after a track (or at the top), and stays where it was put whatever it drives. It holds a curve on its own. The UI calls it "Automation lane".
- **A lane drives its assignments:** any number of parameters, on any channels, each through **its own range** (from and to in the parameter's units; inverting allowed; the full range by default).
- **One lane per parameter.** Assigning a parameter already on a lane moves it.
- **The lane is the source of truth.** Each assigned parameter's tracktion curve is a copy of the lane through its range, written in the same undo step as every edit, so undo restores both together. Lanes are Segno's own state in the project (format 3); older projects convert.
- **Recording** writes a control's moves into its lane, mapped back through its range; a control without a lane gets one. A pass stays one undo step.
- **Moving an assigned control** turns that assignment off (0028's override, per assignment).
- **A lane's context** is its first assignment's channel: the detail panel shows that module; the inspector shows the lane, its points and its assignments (with their ranges), not a track's sections.
- **Popovers close on any click outside them and on Escape**, through one shared helper (`ui::OutsideClick`).

The design is in [design/automation.md](../design/automation.md) (§1–§3, §6–§13).

## Consequences
- Per-track lane folding goes; one shortcut shows or hides every lane.
- One curve can move several parameters, inverted or over part of their range, without copying points.
- The MCP tools gain lanes as things of their own (`add_automation_lane`, `assign_automation`, `unassign_automation`, `set_automation_lane`, `remove_automation_lane`); the parameter-addressed tools act through the parameter's lane.
- Deleting a track or module must take its assignments with it in the same undo step (the delete commands aren't built yet; until then, opening a project prunes dangling ones).
- Reordering lanes among tracks comes with track reordering.
