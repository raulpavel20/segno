# 0043. A clip's colour is its own

- **Status:** Accepted (product owner, 2026-10-11)
- **Date:** 2026-10-11
- **Supersedes in part:** [0041](0041-tracks-and-clips-managed-in-place.md): "clips follow their track's colour until given their own". The rest of 0041 stands.

## Context
- ADR 0041 had a clip without a colour of its own show its track's, now and whenever the track's changed. Only a clip coloured by hand kept a colour.
- In the M2 song test the product owner found the two colours tied together in ways that were hard to follow:
  - a clip moved to another track took the new track's colour;
  - with a clip selected, recolouring its track recoloured the clip;
  - a clip coloured by hand kept its colour, so some clips followed and some didn't, with nothing on screen to tell which;
  - switching takes lost a clip's colour, because its takes never had it.
- The product owner wants "initialization, not entanglement": a new clip takes its track's colour, and after that the two are independent (2026-10-11).
- In the field, Live works this way: a new clip takes its track's colour and keeps it. Studio One, Cubase and Bitwig have clips follow the track (tracks-and-clips.md §2).

## Decision
- **A clip's colour is its own.** It's set once, when the clip is made: its track's colour at that moment. After that, nothing changes it but recolouring the clip.
  - **Recolouring a track** recolours only the track.
  - **Moving a clip** to another track keeps its colour.
  - **Recolouring a clip** recolours only the clip, and its takes, so it keeps its colour whichever take it plays.
- **What's made from a clip keeps its colour:** a copy, a duplicate, the parts of a split, a bounce, a duplicated track's clips, takes moved to tracks.
- **What's new takes its track's colour:** a clip drawn, inserted from a file, captured or recorded, with its takes.
- **The palette has no "Track colour" item.** Choosing the track's swatch in the grid gives a clip its track's colour.

## Consequences
- Every clip stores its colour, main-lane clips and take clips alike. One step after every edit that makes clips gives any clip without a colour its track's, in the same undo step (`Session::Impl::colourNewClips`, from `settle`).
- **Older projects** stored no colour for clips that followed their track. As one opens, those clips take their track's colour, and take clips their clip's; the project stays saved, and nothing is added to undo.
- The snapshot loses `ClipInfo::ownColour`; the inspector's CLIP Colour swatch shows the clip's colour, like the TRACK one.
- **Assistants:** `set_clips` keeps its arguments. A `null` colour now gives each clip its track's colour once, rather than making it follow. `set_track` recolours only the track.
