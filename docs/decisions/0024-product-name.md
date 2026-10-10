# 0024. The product is called Segno

- **Status:** Accepted (product owner, 2026-10-09); where the wordmark sits amended by [0040](0040-a-menu-bar-and-everything-by-mouse.md): at the title bar's left end on Linux and Windows, in the top bar on macOS
- **Date:** 2026-10-09
- **Builds on:** [0008](0008-naming-and-trade-dress.md) (own names, identifiers separate from names)

## Context
The working name "FossDAW" was a placeholder (ADR 0008, `docs/ids.md`). It was being baked into more and more of the product: the window title, the data folder, the plugins' vendor name and IDs, the MCP server and its resource addresses. The product owner wanted a modern name that feels like a proper music tool rather than a niche project.

The first candidates (Cadence, Forma, Sonora, Strata, Timbre, Prisma, Modus) all collide with existing music software: KXStudio's Cadence, Forma Labs Audio and NI FORM, Sonora Cinematic (and SONAR, a DAW), several plugins called Strata, the Timbre audio editor, Rob Papen Prisma and NI Prism, and Physical Audio's Modus. Searches for Segno found no DAW, plugin or instrument by that name (open question B24).

## Decision
- **The product is Segno:** the suite and the DAW. *Segno* is Italian for "sign": in notation, 𝄋 marks the point the music returns to (*dal segno*). Pronounced "SEN-yo".
- **The mark is the segno sign**, drawn as one vector in `fd_ui` (`paintMark`): a monoline S of two bowls joined by a diagonal spine, a slash crossing the spine, a dot on either side, in the accent colour. It isn't taken from a font, so it matches the icon set's strokes and renders the same everywhere.
- **The wordmark** is the mark followed by "SEGNO" in JetBrains Mono bold, all caps and tracked (text style `Wordmark`). It sits at the top left of the DAW, in the transport bar (`paintBrand`).
- **The app icon** is the mark on a dark rounded square (`appIcon`), rendered from the same vector: `fd-snapshot --app-icon app/resources/AppIcon.png`.
- **Names and identifiers** (`docs/ids.md`):

  | What | Value |
  |---|---|
  | App, vendor and window name | `Segno`; plugins are "Segno EQ", "Segno Sampler" … |
  | Plugin manufacturer code | `Sgno` |
  | Reverse-DNS prefix (bundle and CLAP IDs) | `org.segnoaudio`; the app is `org.segnoaudio.segno` |
  | LV2 URI prefix | `https://segnoaudio.org/plugins/` |
  | Project files | `.segno` |
  | User data folder | `<user data>/Segno` |
  | MCP server name and resources | `segno`, `segno://project` … |

- **Code keeps its `fd` prefix:** the `fd` namespace, the `fd_` targets and `FD_` options, and tool names like `fd-render`. Users never see them, and renaming them would touch every file for no gain.

## Consequences
- **Before the first public build:**
  - register the domain `segnoaudio.org`, which was free on 2026-10-09; the LV2 URIs and reverse-DNS IDs depend on it;
  - run a proper trademark search (EU, US and WIPO; software classes 9 and 42).

  The vendor identifiers above freeze at that release.
- **Nothing released depends on the old values,** so the switch is free now. Locally:
  - plugin builds from before this change appear as a different vendor in hosts;
  - projects saved as `.fdproj` open once renamed to `.segno` (the format is the same);
  - the DAW's settings move from `<user data>/FossDAW` to `<user data>/Segno` (audio settings and scanned plugins start fresh unless the folder is moved).
- Device and parameter IDs, plugin codes and section IDs are unchanged.
