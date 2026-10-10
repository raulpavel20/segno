# 0038. Comping you can listen through: audition, and clicks that change one section

- **Status:** Accepted (product owner, 2026-10-10, after trying C5c)
- **Date:** 2026-10-10
- **Supersedes in part:** [0037](0037-a-pass-makes-its-own-clip.md): "choosing a take plays that take alone … wherever the choice comes from", for a click on a take's strip and for Alt+↑/↓. Choosing a whole take from the take menu, the inspector or an assistant still plays it alone. The rest of 0037 stands.
- **Builds on:** [0032](0032-takes-belong-to-clips.md) (takes on their clips), [0036](0036-crossfades-at-the-boundary.md) (crossfades at joins)

## Context
After C5c, a click on a take's strip and Alt+↑/↓ played a whole take, replacing the comp. To hear another take, you lost your comp (Ctrl+Z brought it back, one step at a time), so you had to know every part you wanted before the first swipe.

A survey of current DAWs (2026, from their manuals):
- **Hearing a take without changing the comp:**
  - Live 12: an audition button on each take lane ("take lanes are only audible when Audition Mode is enabled").
  - Pro Tools: a lane's Solo, which plays that lane in place of the main playlist.
  - Cubase: a lane's Solo.
  - Studio Pro: a layer's Solo.
  - REAPER 7: "Play only this lane".

  Logic and Bitwig make you switch the active take, so the comp is safe only if you saved it.
- **Changing one section:**
  - Logic: in Quick Swipe Comping, "click an unselected area of a take to make that particular area active".
  - REAPER: "move comp area to lane under mouse".
  - Bitwig: a click swaps the take into the nearest comp region.
  - Cubase's comp tool: brings a take to the front within a split segment.
- **Keys** step a segment through the takes in Live, Bitwig, Studio Pro and Pro Tools.
- **Several comps** are kept side by side as Logic's comp list, REAPER's comp lanes, Live's take lanes, or Pro Tools' and Cubase's versions.

## Decision
- **Audition a take:** a speaker button on each take's strip plays that take in place of its clip, across the clip's range, without changing anything.
  - One take per track at a time.
  - It lasts through play and stop. It ends with the button, by auditioning another take, by closing the takes, or by anything that changes the project (an edit, undo, save, a bounce or render, recording).
  - It's a listening aid, like solo: never an undo step, never an unsaved change, never saved.
- **A click on a take's strip uses that take for the section under the pointer.**
  - A clip's sections are the stretches between the lines where its take changes, plus gaps where nothing plays. The first section runs from the clip's start and the last to its end.
  - Other sections never change. New joins get the comp's 10 ms crossfade.
  - A clip that isn't a comp is one section, so a click there plays the whole take, as before.
- **Alt+↑ / Alt+↓** step the section at the playhead (or the first section, with the playhead outside the clip) through the takes that cover it.
- **A whole take** is a deliberate choice: the take menu ("Use Take 3 for the whole clip"), the inspector's "Use this take", or an assistant's `use_take` without a range. It plays alone, as ADR 0037 says.
- **Several comps side by side** are not in M2 (open question B47).

The design is [design/recording.md](../design/recording.md) §6, §7 and §21.

## Consequences
- You can listen to any take at any time and come back to your comp with one click, and building a comp never takes back what you chose elsewhere.
- The engine plays an audition by muting the clip's parts and adding the take's clips to its track outside the undo history. Every command, undo, save, render and recording ends an audition first, and the snapshot and saved files never contain one.
- The model gains a clip's sections (`model::sectionAt`). Strip clicks and Alt+↑/↓ comp a section in through the same path as a swipe.
- MCP gains `audition_take`.
