# Architecture Decision Records

Each file records one decision: the situation, what was decided, and what follows from it. ADRs are **append-only**. To change a decision, add a new ADR that supersedes the old one, and update the old one's status line to point to it.

**Statuses**
- **Accepted:** decided by the product owner.
- **Proposed:** recommended, awaiting product-owner review.
- **Superseded by NNNN:** replaced by a later ADR.

| # | Decision | Status |
|---|---|---|
| [0001](0001-arrangement-only-scope.md) | Arrangement-first DAW; no Session view | Accepted |
| [0002](0002-device-suite-and-daw-in-parallel.md) | Device suite (plugins) and DAW built in parallel from one device library | Accepted |
| [0003](0003-platforms-and-plugin-formats.md) | Linux, Windows, macOS; CLAP, VST3, LV2, AU, standalone | Accepted |
| [0004](0004-tech-stack.md) | C++20, JUCE 9, tracktion_engine, CPM-pinned dependencies | Accepted |
| [0005](0005-license-agpl-3.md) | License: AGPL-3.0 | Accepted |
| [0006](0006-juce-free-core-and-module-boundaries.md) | DSP and device core never depend on JUCE | Accepted |
| [0007](0007-verification-without-ears.md) | Automated, measurable verification + human sign-off | Accepted |
| [0008](0008-naming-and-trade-dress.md) | Own names and visual identity; nothing taken from Ableton | Accepted; amended by 0052 |
| [0009](0009-dark-first-design-language.md) | Dark-first, flat, minimal, contextual design language | Superseded by 0014 |
| [0010](0010-live-is-the-floor-not-the-ceiling.md) | Live is the minimum for coverage; best-in-class is the aim; consolidate into deeper devices | Superseded by 0011 |
| [0011](0011-calibrated-ambition-and-consolidation.md) | Match Live by default; go beyond selectively (mainly the mixing core); merge only devices that do the same job | Accepted |
| [0012](0012-ui-toolkit-juce-everywhere.md) | JUCE is the only UI toolkit; standard vs OpenGL rendering decided by an M0 spike | Accepted |
| [0013](0013-single-repository.md) | One repository (monorepo) for suite + DAW, with independent builds and releases | Accepted |
| [0014](0014-flat-darker-ableton-adjacent.md) | Flat everywhere, darker, close to Ableton's visual grammar; gradients only as a listed accent effect | Accepted; "almost no menu diving" amended by 0040 |
| [0015](0015-rendering-opengl-for-the-daw.md) | OpenGL renderer for the DAW window (GL-thread driven), software for plugin editors; cached heavy views; frame budgets | Accepted |
| [0016](0016-constant-device-latency.md) | Constant, whole-sample latency per device instance; parameters never change latency | Accepted |
| [0017](0017-heavy-data-and-non-parameter-state.md) | Heavy data via `AssetHandoff` (frees never on the audio thread); synchronous load in `readExtraState`; UI edits to extra state through the host binding | Accepted |
| [0018](0018-daw-app-structure.md) | DAW app: `Session` facade over tracktion, immutable snapshots for views, JUCE-free tested `app/src/model`, own `ActionRegistry` | Accepted |
| [0019](0019-one-kind-of-track.md) | One kind of track: audio and MIDI clips together, never overlapping (the placed clip wins); instruments add to the track's audio, never mute it | Accepted; item 4 (a default instrument with MIDI) superseded by 0033 |
| [0020](0020-latency-follows-lookahead.md) | Lookahead latency follows its setting (dynamics devices): non-automatable, zero when off, hosts told on the message thread; an exception under 0016 | Accepted |
| [0021](0021-mcp-server.md) | A built-in MCP server: assistants can drive the whole DAW and every device in it; only in the DAW, never in the plugins; opt-in and local | Accepted |
| [0022](0022-module-panels.md) | Module panels: one layout grid, titled sections with lines, collapse and fold, view state outside module state; "Modules" in the UI | Accepted |
| [0023](0023-ui-foundations.md) | UI foundations: colours by role, text styles, motion and input tokens, shell metrics and shared components, one time-view kit, a track header layout model, and a UI lint | Accepted |
| [0024](0024-product-name.md) | The product is called Segno: the name, the mark and the identifiers derived from it | Accepted; the wordmark's place amended by 0040 |
| [0025](0025-sidechain-routing.md) | Sidechain routing is host routing, chosen in the module's header: tracks in the DAW, Off or External in plugins; never a parameter | Accepted |
| [0026](0026-mixing-in-the-arrangement.md) | Mixing in the arrangement (track headers, inspector) and in a mixer view; return tracks and a master below the tracks; one volume taper, balance pan, solo in place (never undoable) | Accepted |
| [0027](0027-selection-and-context-panels.md) | One selection shared by both views; the detail panel and inspector follow the subject, per view (the mixer view: modules only, no inspector); inspector sections with titles and lines | Accepted |
| [0028](0028-automation-lanes.md) | Automation: every lane visible, as rows like tracks under their track; a straight line is a straight move of the control (normalised parameters, the volume taper); the drawn curve is what plays; moving an automated control turns its lane off | Accepted; placement and ownership superseded by 0029 |
| [0029](0029-independent-automation-lanes.md) | Automation lanes are rows of their own among the tracks, driving assigned parameters on any channel, each through its own range; one lane per parameter; the lane is the source of truth; popovers close on any outside click | Accepted |
| [0030](0030-projects-are-folders.md) | A project is a folder: `Name.segno` with `Recordings/` (later bounces and collected samples); clips refer to files relative to it; files are never deleted implicitly | Accepted |
| [0031](0031-recording-keeps-every-take.md) | Recording keeps every pass and loop lap on take lanes under the track (Segno's own state); the track's main lane is plain clips; choosing and comping write clips; takes move to new tracks | Accepted; where takes live and show superseded by 0032 |
| [0032](0032-takes-belong-to-clips.md) | Takes belong to clips: a clip shows its takes from two on ("3/4 ▾") and opens them under itself; a clip and its takes are edited as one; no take rows | Accepted; a pass over part of a clip and the range on a cut superseded by 0037 |
| [0033](0033-no-instrument-by-itself.md) | Segno never adds an instrument by itself: a MIDI clip on a track without one is silent until you add one (supersedes 0019 item 4) | Accepted |
| [0034](0034-device-api-frozen.md) | The device API (`libs/device`) and the device state format are frozen: additions that keep devices and saved states unchanged are allowed; anything else needs an ADR and a migration | Accepted |
| [0035](0035-bounce-keeps-its-source.md) | Bounce in place renders up to where the clip enters the chain and keeps the source as a take; bounce to a new track prints the whole chain before the fader with the source muted; clips can be muted | Accepted |
| [0036](0036-crossfades-at-the-boundary.md) | Crossfades belong to the join between two audio clips, which stay edge to edge and play their spare audio past it; every audio clip edge fades a few ms unless seamless; fades bend like automation | Accepted |
| [0037](0037-a-pass-makes-its-own-clip.md) | A recording pass makes its own clip: clips it covers in part are cut, clips it covers entirely give it their takes, what it replaced is kept as "Before take N"; choosing a take plays it alone; a clip's range never reaches over another clip | Accepted; strip clicks and Alt+↑/↓ on comps superseded by 0038 |
| [0038](0038-comping-you-can-listen-through.md) | Comping you can listen through: a take auditions without changing the clip; a click on a strip, or Alt+↑/↓, changes only the section of the comp it's in; a whole take is a deliberate choice | Accepted |
| [0039](0039-export-in-the-background.md) | Export is a popover panel and renders in the background: the mix or stems that add up to it, the song with its tail or the loop, WAV/FLAC/AIFF with automatic dither and loudness normalising; files in the project's Exports or anywhere chosen, never overwritten; a loudness report | Accepted |
| [0040](0040-a-menu-bar-and-everything-by-mouse.md) | A traditional menu bar (File … Help) holds every action with its shortcut, so everything can be done with the mouse; on Linux and Windows it sits in a title bar of our own with the project's name and the window buttons (the system's as an option); the macOS menu bar on macOS | Accepted |
| [0041](0041-tracks-and-clips-managed-in-place.md) | Tracks and clips are managed in place: renamed where their names are (F2 too), moved by dragging headers, deleted with Delete and duplicated with Ctrl+D, coloured from their right-click and the inspector; deleting takes what only made sense with it; a clipboard for clips, notes and points pasting at the playhead | Accepted; lanes moving with their track superseded by 0042, clips following their track's colour by 0043 |
| [0042](0042-every-row-moves-on-its-own.md) | Every track and lane moves on its own, droppable anywhere among the tracks and lanes; several at once comes with multi-track selection | Accepted |
| [0043](0043-a-clips-colour-is-its-own.md) | A clip's colour is its own: a new clip takes its track's colour once; after that, recolouring or moving either leaves the other | Accepted |
| [0044](0044-presets-are-files.md) | Presets are `.segnopreset` files of the module's state, factory ones built in, yours in `Documents/Segno/Presets` shared by the plugins and the DAW; in every module's header with A/B and defaults; one undo step to load; automation keeps playing | Accepted |
| [0045](0045-settings-are-one-page.md) | Settings are one searchable page in the centre of the window, applied as you change them, kept per user in one `settings.json`; shortcuts remappable there; JACK on in Linux builds | Accepted |
| [0046](0046-one-browser-modules-are-folders.md) | One browser on the left with one search over modules, presets, plugins and files; modules are folders of their presets; previews in time; files browsed where they are and gathered by Collect files and save | Accepted |
| [0047](0047-the-editing-grid-in-each-editor.md) | The arrangement and the piano roll each have their own grid: zoom-adaptive at a width (Narrowest … Widest) or a fixed value, triplets, snap; shown on each editor, Ctrl+1 to Ctrl+5 for the editor used last | Accepted |
| [0048](0048-clips-loop.md) | Any clip can loop a part of its content: edges go as far as dragged with every repeat notched, notches change the loop's length, the clip view's brace sets its start and end (piano roll and a new audio clip view); seams follow the edge-fade rule | Accepted |
| [0049](0049-the-master-row-is-the-song-row.md) | The master's row is the song row, pinned under the ruler: sections (a name, colour, start and end) over a miniature of every track; an edge moves the mark, the body moves the music, the rest of the song making room; clips split at the edges | Accepted |
| [0050](0050-the-wavetable-synth-goes-beyond-live.md) | The Wavetable Synth goes beyond Live, to the depth of the leading wavetable synths, with its innovation where the DAW helps; amends 0011 for this synth only | Accepted |
| [0051](0051-pages-for-very-wide-modules.md) | Pages for very wide modules: tabs in the header, one width for every page, sections still collapse; opt-in past about 1 600 px | Accepted |
| [0052](0052-generic-names-are-free.md) | Generic names (Sampler, Wavetable, Delay) are free to use, as final names too; names a vendor made up (Operator, Drift, Simpler) aren't; amends 0008 | Accepted |
| [0053](0053-a-modules-editor-in-the-detail-panel.md) | A module's editor (the Wavetable's tables) opens in the detail panel beside Modules and Clip; in the plugins, under the panel | Accepted |
| [0054](0054-push-2-support.md) | Ableton Push 2 is supported in the DAW, display included, as a client of the Session like the assistants; no clip launching; other controllers through MIDI learn; revisits the vision's non-goal | Proposed |
| [0055](0055-factory-content-is-files-in-a-read-only-library.md) | What ships besides the modules is files in a read-only, versioned Library, a place of its own in the browser before the User library; `library:` paths; found in standard places, Settings or `SEGNO_LIBRARY` | Accepted |
| [0056](0056-racks-are-modules-of-modules.md) | A rack is a module of modules: chains side by side, eight macros mapped with ranges and curves, no nesting; saved as `.segnorack` files apart from module presets; a DAW feature on tracktion's racks | Accepted; where the browser lists racks superseded by 0059; macros' motion superseded by 0060; the Drum Rack superseded by 0064 |
| [0057](0057-the-song-has-one-key.md) | The song has one key (a root and one of 36 scales, or none), set in the top bar; it describes and never changes anything by itself; modules see it through `TransportInfo::key`; shared theory in `libs/music`; changes along the song later, on the song row | Accepted |
| [0058](0058-the-piano-roll-writes-in-key-and-in-chords.md) | The piano roll writes in key and in chords: one surface without a tool palette, in key by default, chords read from the notes and rewritten voice-led, other tracks' notes, per-note chance the same in every render, controller lanes, previewed tools; a Notes menu; three stages | Accepted; chance in every render superseded by 0062 |
| [0059](0059-racks-are-a-browser-part-and-every-part-has-its-icon.md) | Racks are a part of the browser of their own, both kinds together; every part shows one icon, the same in the rail and the open browser; every search field shows the magnifier | Accepted |
| [0060](0060-motion-comes-from-modulation-modules.md) | Motion comes from a collection of modulation modules (LFO, envelope follower, others) that drive any parameter, a rack's Morph and macros too; racks get no motion of their own; modulation built once, not into each module | Accepted |
| [0061](0061-modulators-move-any-parameter-on-their-track.md) | Modulators (LFO, Random, Envelope, Follower, Steps) are modules that make no sound, anywhere in the chain like any module, moving any parameter on their track around where it's set; each target its own copy (Per Target) by default where it makes sense; the Wavetable's words; the DAW's only | Accepted |
| [0062](0062-random-rolls-again-each-time-it-plays.md) | Random rolls again each time it plays (note chance, velocity spread, the modulators); Keep holds a roll; tools that write keep their seeds | Accepted; supersedes in part 0058 |
| [0063](0063-audio-is-edited-as-properties-warped-in-real-time.md) | Audio clips are edited as their own settings, never their files: gain and a gain line pinned to the audio, transpose, reverse; warped in real time in three modes named for their job (Beats, Tones, Repitch), untouched at their own tempo; loops and recordings follow the tempo by default; Follow key plays a clip in the song's key when asked; the tools used while working on the editor's bar; warping moves from M4 to M3 (P6) | Accepted |
| [0064](0064-a-drum-kit-whose-pads-hold-any-instrument.md) | A Drum Kit instrument whose pads hold any instrument and effects, the Sampler by default; 128 pads, 16 shown; choke groups through a new All sound off event; kits are its presets; built on racks but never called one; the DAW's only; replaces the Drum Sampler and the Drum Rack | Accepted |
| [0066](0066-a-drum-kits-clips-open-on-its-pads.md) | A Drum Kit's clips open on its pads: a row for each pad with a sound, named; a toggle per clip for the whole roll; the chord lane, the key and the melodic tools off the surface there, dimmed in the menu bar; assistants see the pads (amends 0058's in-key default for them) | Accepted |
| [0067](0067-drum-machine-numbers-are-terms-of-the-trade.md) | The bare numbers that became the trade's names for a sound (808, 909, 606, 707, 727, 626, 303) are allowed in kit and preset names; makers' names and full model names (Roland, TR-808, LinnDrum) stay out (amends 0052) | Accepted |
| [0068](0068-the-factory-kits-sources.md) | The factory kits' sounds are CC0 only, verified and pinned; Fischer's 808 (CC0 added by its repackager) and recordings of sample-playback machines are accepted; Freesound waits; Segno renders its own drum voices where no CC0 recording exists | Accepted |
| [0070](0070-an-instrument-rack-plays-the-tracks-audio-once.md) | An instrument rack plays the track's audio once, through its first chain heard; its other chains start from silence with the notes alone; effect racks and the Drum Kit unchanged (answers B92) | Accepted |
| [0071](0071-profiling-tools.md) | Profiling: perf + hotspot for where the time goes (perf events allowed while profiling), Tracy behind `FD_PROFILE` and our own JUCE-free `FD_ZONE` macros for the timeline, a `profile` preset, callgrind for exact counts, heaptrack for memory | Proposed |

## Template

```markdown
# NNNN. Title

- **Status:** Proposed | Accepted | Superseded by NNNN
- **Date:** YYYY-MM-DD

## Context
What situation or problem forces a decision. Facts, constraints, options considered.

## Decision
What we decided, stated plainly.

## Consequences
What becomes easier, what becomes harder, what we must now do.
```
