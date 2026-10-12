# 0049. The master's row is the song row: sections that carry their music, over a miniature of every track

- **Status:** Accepted (product owner, 2026-10-12)
- **Date:** 2026-10-12
- **Builds on:** [0019](0019-one-kind-of-track.md) (clips never overlap), [0027](0027-selection-and-context-panels.md) (one selection), [0028](0028-automation-lanes.md) and [0029](0029-independent-automation-lanes.md) (automation lanes), [0032](0032-takes-belong-to-clips.md) (takes belong to clips), [0040](0040-a-menu-bar-and-everything-by-mouse.md) (the menu bar, everything by mouse), [0047](0047-the-editing-grid-in-each-editor.md) (each editor's grid), [0048](0048-clips-loop.md) (clip loops), [0021](0021-mcp-server.md) (assistants)
- **Amends:** open question B25 ([mixer.md](../design/mixer.md) Q1): the master leaves the end of the track list; the returns stay there
- **Design:** [arranger.md](../design/arranger.md)

## Context
- Songs are built from sections, and rearranging one by hand (selecting clips on every track, then fixing the automation) is slow and easy to get wrong. Live has only locators; Studio One, Cubase and Logic have arranger tracks whose sections carry their music.
- The master's row in the arrangement can't hold clips: it sat at the bottom with the returns, mostly empty.
- The product owner suggested putting the sections on the master's row, with every track drawn in it as a thin line. Live already keeps song-wide things on its master track (tempo automation).

## Decision
- **The master's row becomes the song row,** pinned under the ruler as the arrangement's first row, 40 px tall by default (resizable, 32 px at least). Its header stays the master's; its lane shows the song's sections over a miniature of every track. The returns stay at the end of the track list; the mixer view doesn't change.
- **A section** is a name, a colour, a start and an end. Sections never overlap; gaps are allowed; edges snap to the grid.
- **An edge moves the mark alone; the body moves the music.** Moving, copying (Ctrl+drag, Duplicate), inserting and deleting a section take everything in its time on every track and lane, and the rest of the song makes room. Delete takes the music; "Remove section mark" keeps it.
- **What you hear moves exactly:** clips crossing an edge are split there, and automation keeps its value at the edges.
- **A section is a selection of its own,** its channel the master. The inspector edits its marks.
- **A new Song menu** holds the sections' commands; assistants get seven tools.
- Sections are stored as the engine's arranger track (`te::ArrangerTrack`), saved and undone with the project.

## Consequences
- `model::ArrangementRows` gains a song row; `model::TrackLayout` pins it above the scrolling rows. Ctrl+Alt+R shows and hides the returns alone.
- New: `model::Sections` (each command's plan, tested without the engine), Session's section commands, the song row's drawing and gestures, the Song menu, a section in the selection and the inspector, seven MCP tools.
- When tempo and time-signature changes come, they live on the song row and move with their sections.
- **Open until asked:** chains (play orders), scratch pads, markers, exporting or bouncing a section (with range selection), clicking an overview line to reach its track.
