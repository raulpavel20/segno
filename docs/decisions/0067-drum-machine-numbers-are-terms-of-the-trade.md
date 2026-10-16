# 0067. Drum machine numbers are terms of the trade; makers and model names stay out

- **Status:** Accepted (product owner, 2026-10-16)
- **Date:** 2026-10-16
- **Amends:** [0052](0052-generic-names-are-free.md) (generic names are free; names a vendor made up aren't), for drum machines
- **Number:** 0065 is taken on the `synth` branch; renumbered at merge if main has used 0067

## Context
- The factory kits ([drum-kit.md](../design/drum-kit.md) §17, step 3) include kits made from CC0 recordings of classic drum machines.
- ADR 0052 keeps names a vendor made up out of everything users read, presets included. Read strictly, that keeps out "808" as well.
- The product owner (2026-10-16): "'808' is an industry term at this point; we need to allow things like these if we want users to recognise them."
- The bare numbers of a few machines name sounds and genres, not only products:
  - an "808" is a long, tuned kick, as a bass in trap and hip-hop;
  - "909" hats and "808" claps are how producers name those sounds.
  - Other vendors' factory content uses them that way; Ableton ships an "808 Core Kit".
- What identifies the maker is its name and the full model name ("Roland", "TR-808"). Recognising the sound doesn't need those.

## Decision
- **Allowed:** the bare numbers that have become the trade's names for a sound or a style, in kit and preset names, tags and descriptions. They are 808, 909, 606, 707, 727, 626 and 303. More join this list only by amending this ADR.
- **Still out:**
  - makers' names: Roland, Linn, Oberheim, E-mu, Sequential, Simmons, Korg, Yamaha, Casio, Boss, Elektron, and the like;
  - model names with their prefixes or brands: TR-808, CR-78, LM-1, LinnDrum, DMX, SP-1200, Drumulator, SDS-V and the like.

  A kit from one of those machines is named for its sound and era ("Early digital", "Tom machine").
- **Sources stay credited where credit belongs,** in the Library's notices and the recipe, never in a name users browse. The recordist and the source's name are recorded with each CC0 source, though CC0 doesn't ask for it.
- **The Library's folders follow the same rule:** a folder users browse never carries a maker's or a model's name.

## Consequences
- Factory kits like "808 Trap" and "909 House" are possible.
- The naming pass before release (ADR 0052) checks kit names against the list above.
- If a maker objects to a number, renaming the kit is cheap: kit names aren't IDs ([ids.md](../ids.md)).
