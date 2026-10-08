# 0001. Arrangement-first DAW; no Session view

- **Status:** Accepted
- **Date:** 2026-10-08

## Context
Ableton Live has two main views: Session view (launching clips and scenes, mostly for live performance and jamming) and Arrangement view (a linear timeline). The product owner works only in Arrangement view. Session view adds a lot of engine and UI complexity: a clip-launching engine, follow actions, scenes, and keeping two views of one project in sync. Most of that only matters for live performance.

## Decision
The DAW is **arrangement-first**. The timeline is the main workspace, and the design is optimised for composing, arranging, editing and mixing on it. Session view and clip launching are **out of scope**.

## Consequences
- Simpler engine and project model: a linear edit, which is what tracktion_engine is built for.
- The UI design concentrates on one central view, the arrangement plus its context panels.
- Users who rely on clip launching are not the target audience for now. Revisiting this needs a new ADR, which would also have to show how clip launching fits the UI principles.
