# M3: Workflow & space

**Goal:** make a track faster and make it sound finished. That means:
- presets for every module, found in one browser with devices, plugins, samples and files;
- the space effects: delay, reverb, chorus, phaser and flanger, auto pan and tremolo, a filter;
- song sections you can move, duplicate and reorder with everything in them (the arranger track);
- finishing tools: a loudness meter, a reference track to A/B against on the master, the EQ's second stage, a spectrogram, a tuner;
- third-party plugins scanned without risk to the session.

See [roadmap.md](../roadmap.md) and the device specs in `devices/*/SPEC.md`.

**Status:** ◐ plan approved by the product owner (2026-10-11), with every recommendation under "For review" (CLAP hosting moved to M4). A1 (presets) and B11 (factory presets, all but the Sampler's) built and signed off the same day. C1 (Settings) built and signed off the same day. C2 (the browser, [browser.md](../design/browser.md), ADR 0046) built and signed off 2026-10-12. A2 (the modulation core) built 2026-10-12. B1 (the Delay) built and signed off 2026-10-12, with its 15 factory presets. C9 (the editing grid, ADR 0047), asked for before going on, built and signed off 2026-10-12. Also asked for before going on, a window fix, signed off 2026-10-12: on Linux, Maximise and Restore are the window manager's (a small JUCE patch), so Restore, a double-click and a drag un-maximise, and clicks in the browser no longer show a maximised window as not maximised ([menu-bar.md](../design/menu-bar.md) §4). C10 (clip loops, [clip-loops.md](../design/clip-loops.md), ADR 0048), asked for before going on: designed, built and signed off 2026-10-12. Fixes from the product owner's use (2026-10-12): the inspector's sections scroll when they're taller than the panel; modules reordered are heard at once. B2 (the Chorus, [SPEC](../../devices/chorus/SPEC.md)) approved 2026-10-12 with every recommendation of C1–C11, built the same day with its 14 factory presets, and signed off. B3 (the Phaser & Flanger, [SPEC](../../devices/phaser-flanger/SPEC.md)) approved 2026-10-12 with every recommendation of P1–P12, built the same day with its 14 factory presets, and signed off. B4 (the Auto Pan & Tremolo, [SPEC](../../devices/auto-pan-tremolo/SPEC.md)) approved 2026-10-12 with every recommendation of T1–T12, built the same day with its 14 factory presets, and signed off: the four modules on the modulation core are done. B5 (the Reverb, [SPEC](../../devices/reverb/SPEC.md)) approved 2026-10-12 with every recommendation of R1–R12, its core built the same day with 16 factory presets, and signed off. B6 (the Filter): [SPEC](../../devices/filter/SPEC.md) drafted 2026-10-12, for review.

## Steps

Every device gets its `SPEC.md` before any code, and every DAW feature its design doc (with its menu bar place, ADR 0040, and its MCP tools, ADR 0021) before any code, as in M2.

### A. Framework

| # | Step | Done when | Status |
|---|---|---|---|
| A1 | **Presets** ([presets.md](../design/presets.md), [ADR 0044](../decisions/0044-presets-are-files.md)):<ul><li>a preset is a module's state (the JSON format of `architecture.md`) with a name, tags and a short description;</li><li>factory presets in `devices/<name>/presets/`, built into the plugins and the DAW;</li><li>user presets as files in one folder shared by the plugins and the DAW;</li><li>the module header's preset menu: search, tags, favourites, previous and next, Save, Save as, Rename, Delete, Revert;</li><li>A/B compare in the header: two states per module to switch between;</li><li>the same in the plugin builds.</li></ul> | `[fast]` tests: a preset round-trips byte for byte; older state versions migrate; a factory preset loads the state it stores. Integration: the plugin and the DAW see the same user presets; loading one is one undo step in the DAW; A/B switches without clicks. Panel lint passes with the preset menu in every header. MCP: `list_presets`, `load_preset`, `save_preset`, `compare` | ◐ design approved (2026-10-11, every recommendation of presets.md §15); built (2026-10-11):<ul><li>the file, the library and the controller in `libs/device` under `[fast]` tests;</li><li>factory presets generated into each device;</li><li>the header's preset bar and list in both hosts, the layout lint with it;</li><li>the DAW: one undo step per load and per browse, A/B and the memo kept with the project, your default for new modules, the Module menu;</li><li>the plugins: the program list, gestures, the memo in the plugin state;</li><li>MCP `list_presets`, `load_preset`, `save_preset`, `compare`, and `add_module` with a preset.</li></ul>Signed off by the product owner (2026-10-11), with B11's sets |
| A2 | **Modulated delay line and LFOs** (`libs/dsp`), shared by the Delay, Chorus & Ensemble, Phaser & Flanger and Auto Pan & Tremolo:<ul><li>a delay line read at fractional, moving positions (interpolated without zipper noise or dulling);</li><li>feedback paths with filters and saturation;</li><li>LFOs with the usual shapes, a stereo phase offset, and sync to the song's tempo and position (the device API's `TransportInfo` already carries both; no API change).</li></ul> | Unit tests: delay times exact to the sample and between samples; interpolation's frequency response within 0.1 dB to 15 kHz; a modulated sine without zipper products above −100 dB; a synced LFO locked to the song position after a jump; no allocation in `process()` | ☑ Built (2026-10-12) in `libs/dsp` (`Modulation.h`):<ul><li>`ModulatedDelay`: a 32-tap Kaiser-windowed sinc (full band, β 10) in 1024 phases, interpolated between phases; whole-sample delays exact; the shortest delay 16 samples (a through-zero flanger delays its dry signal as much);</li><li>`FeedbackPath`: low and high cut (12 dB/oct), a soft clip that leaves small signals alone and bounds the loop, a DC blocker; `magnitude()` for the panel;</li><li>`ModulationLfo`: sine, triangle, saw up and down, square, sample-and-hold, wander; a stereo offset; free in Hz or one cycle per so many beats, locked to the song while it plays; sample-and-hold and wander take their values from the cycle, so a position always gives the same value.</li></ul>Tests: whole-sample delays bit-exact; between samples, within −90 dB of the ideal to 10 kHz and of 0.1 dB at 15 kHz (44.1 and 48 kHz); a 10 ± 3 ms sweep of a 1 kHz sine at −107 dB; feedback bounded at 150 %; a synced LFO the same after a jump as played through; no allocation (a new `testkit::CountAllocations` scope) |

### B. Devices

Each: SPEC approved → built → validators → sign-off by ear and feel, with its factory presets.

| # | Device | Notes |
|---|---|---|
| B1 | **Delay** | Live's Delay and Echo in one: clean and character modes (tape, analog-style), ping-pong, a filter in the feedback path, modulation, ducking, freeze, sync and swing. On A2. ☑ [SPEC](../../devices/delay/SPEC.md) approved (2026-10-12, every recommendation of D1–D10); built 2026-10-12 with 15 factory presets (what building settled is in the SPEC); signed off 2026-10-12 |
| B2 | **Chorus & Ensemble** | Chorus, ensemble, vibrato. On A2. ☑ [SPEC](../../devices/chorus/SPEC.md) approved (2026-10-12, every recommendation of C1–C11; named Chorus); built 2026-10-12 with 14 factory presets (what building settled is in the SPEC); signed off 2026-10-12 |
| B3 | **Phaser & Flanger** | Phaser, flanger, doubler; sync; an envelope follower. On A2 and the dynamics detector. ☑ [SPEC](../../devices/phaser-flanger/SPEC.md) approved (2026-10-12, every recommendation of P1–P12); built 2026-10-12 with 14 factory presets (what building settled is in the SPEC); signed off 2026-10-12 |
| B4 | **Auto Pan & Tremolo** | LFO panning and tremolo, sync, shapes. On A2's LFOs. ☑ [SPEC](../../devices/auto-pan-tremolo/SPEC.md) approved (2026-10-12, every recommendation of T1–T12); built 2026-10-12 with 14 factory presets (what building settled is in the SPEC); signed off 2026-10-12 |
| B5 | **Reverb** *(core)* | Algorithmic rooms, halls and plates, freeze, decay by frequency band. Convolution is stage 2 (M6). ☑ [SPEC](../../devices/reverb/SPEC.md) approved (2026-10-12, every recommendation of R1–R12); core built 2026-10-12 with 16 factory presets (what building settled is in the SPEC); signed off 2026-10-12 |
| B6 | **Filter** | Modelled filter types, an envelope follower, an LFO, sidechain. On the synth's filters, A2's LFOs and the dynamics detector. ◐ [SPEC](../../devices/filter/SPEC.md) drafted 2026-10-12, for review (F1–F13) |
| B7 | **Loudness Meter** | LUFS (integrated, short-term, momentary), true peak, correlation. On the dynamics core's loudness (A1 of M2) |
| B8 | **EQ** *(stage 2)* | Dynamic bands with sidechain, linear-phase mode, EQ match, the cross-track masking display (on the Spectrum's collision rule) |
| B9 | **Spectrum** *(stage 2)* | A spectrogram view |
| B10 | **Tuner** | Chromatic, with a reference pitch |
| B11 | **Factory presets for the M1 and M2 modules** | Utility, EQ, Saturator, Subtractive Synth, Sampler, Compressor, Gate, Limiter, Spectrum. The quality bar says a module without good presets isn't done. ◐ Built and signed off by the product owner by ear (2026-10-11) for all but the Sampler (118 presets, [presets.md](../design/presets.md) §21); the Sampler's wait for the sounds the product owner brings |

### C. DAW

| # | Step | Done when | Status |
|---|---|---|---|
| C1 | **Settings** ([settings.md](../design/settings.md), [ADR 0045](../decisions/0045-settings-are-one-page.md); Settings…, Ctrl+, in the menu bar, menu-bar.md): audio (today's Audio settings), plugin folders, the browser's places, export defaults, and what later features need. A panel, not a modal dialog | Each setting kept across runs; the menu item; MCP `get_settings`, `set_settings` where they make sense; screenshots | ◐ design approved (2026-10-11, settings.md §13, revised resets); built 2026-10-11 (settled while building: settings.md §17; JACK on for Linux; `get_settings`, `set_settings`). Signed off by the product owner (2026-10-11) |
| C2 | **Browser** (ui-principles.md: one search box across devices, presets, plugins, samples and files):<ul><li>places: your folders, the project's folder;</li><li>previews built in, in time with the song when it plays;</li><li>drag onto a track, the arrangement, the module chain;</li><li>collect and save (copy outside files into the project), remove unused recordings and bounces;</li><li>the compact-width rail.</li></ul> | Search results under 50 ms on 10 000 files; previews play from a click and stop at the next; drags land where dropped; collect and save leaves a project that opens on another machine; frame budget held while scrolling results; MCP `search`, `preview`; screenshots | ◐ design approved (2026-10-11, every recommendation of browser.md §16, [ADR 0046](../decisions/0046-one-browser-modules-are-folders.md)); built 2026-10-12, for review (settled while building: browser.md §21; loops following the song's tempo, in previews and drops, wait for M4's warping; MCP `search`, `browse`, `preview`, `collect_files`). Signed off by the product owner (2026-10-12) |
| C3 | **Arranger track:** song sections (intro, verse, drop…) as a row above the tracks; move, duplicate, reorder and delete a section with all its content (clips, takes, automation, tempo and time-signature changes) | Pure models under `[fast]` tests (what moves, where clips split at section edges); each command one undo step; a moved section renders the same as before; MCP tools; screenshots | ☐ |
| C4 | **Reference track A/B on the master:** a reference song plays in place of the mix, matched to its loudness, without passing through the master's modules; one click switches | The switch is sample-accurate and click-free; loudness matched within 0.5 LU; never exported; MCP tools | ☐ |
| C5 | **Plugin scanning** in a separate process, so a plugin that crashes the scan never takes the DAW down. ~~CLAP hosting~~ moved to M4 (Q4) | A crashing test plugin is skipped and reported; scanning runs in the background with progress | ☐ |
| C6 | **Inspector at compact widths:** below 1440 px the inspector opens as an overlay (it closes today); module and note-range sections (selection.md) | Layout tests at compact widths; screenshots | ☐ |
| C7 | **Loose ends from M2:**<ul><li>mute automation: a track "on" parameter (B32);</li><li>notes after a change while playing (B55): a moved clip's sounding notes chased on its new track, MIDI effects added or removed mid-note, a MIDI device unplugged with keys held, the sustain pedal;</li><li>the Subtractive Synth's envelopes dragged on their displays (carried from M1);</li><li>recording: lower-latency monitoring, a round-trip latency test (recording.md);</li><li>export: MP3, Ogg and Opus; adding an export to the project (export.md).</li></ul> | Each with its tests and manual checks | ☐ |
| C8 | **MIDI learn** (B5, due in M2, not built): map a controller's knob, fader or button to any parameter by moving it; mappings kept with the project, listed in Settings | A mapped CC moves its parameter, with automation recording as for the mouse; one undo step per mapping | ☐ |
| C9 | **The editing grid** ([ADR 0047](../decisions/0047-the-editing-grid-in-each-editor.md), asked for by the product owner before the effects go on): each editor (the arrangement, the piano roll) has its own grid, zoom-adaptive at a width (Narrowest … Widest) or fixed (8 bars … 1/32), with triplets and its own snap; its step shown beside Snap on the editor and set from there, the right-click menu, or Options (Ctrl+1 to Ctrl+5); `get_grid`, `set_grid` | A fixed 1/16 draws sixteenths at any zoom; the two editors change independently; saved with the project; model, integration and MCP tests; screenshots | ☑ built 2026-10-12 (the step's chip is as wide as its value, so it sits beside Snap as one view option beside the next). Signed off by the product owner (2026-10-12) |
| C10 | **Clip loops** ([clip-loops.md](../design/clip-loops.md), [ADR 0048](../decisions/0048-clips-loop.md), asked for by the product owner before the effects go on): a Loop switch on every clip;<ul><li>in the arrangement, a looped clip's edges go as far as dragged, every repeat drawn and notched, a notch dragged changes the loop's length, the upper right edge loops a clip in one gesture;</li><li>a loop brace in the clip view sets the loop's start and end, in the piano roll and in a new audio clip view;</li><li>Loop, Loop start, Loop length in the inspector; Clip > Loop clips; `loop_clips` for assistants;</li><li>seams click-free (the edge-fade rule), crossfades into the next lap; clips with takes don't loop.</li></ul> | A looped clip renders its loop sample for sample over every lap; splitting it nulls against the whole; edges, notches and both braces by mouse; model, integration and MCP tests; screenshots | ☑ design approved (2026-10-12, every recommendation of clip-loops.md §14); built 2026-10-12 (settled while building: clip-loops.md §17; the loop cursor at a system cursor's size after the review). Signed off by the product owner (2026-10-12) |

## Order of work (recommended)

1. **A1 presets and C1 settings.** Every module from here on ships with presets, the M1 and M2 modules are still waiting for theirs, and the browser lists them. Settings hold the preset and browser folders.
2. **C2 browser,** with B11's factory presets written alongside (built and named by Claude, signed off by the product owner by ear, B4).
3. **A2, then B1 Delay, B2 Chorus & Ensemble, B3 Phaser & Flanger, B4 Auto Pan & Tremolo.** One core, four devices, as the dynamics family was in M2.
4. **B5 Reverb and B6 Filter.** With the delays, every space effect is there.
5. **C3 arranger track.** The biggest DAW feature, designed while the effects are built.
6. **B7 Loudness Meter, then C4 reference A/B.** Both are about finishing, and A/B uses the meter's loudness.
7. **B8 EQ stage 2 and B9 the spectrogram.** They share the analysis.
8. **B10 Tuner, C5 plugin hosting, C6, C7, C8,** fitted in where they unblock something; C7's note and latency items early if the song tests need them.

## Carried over from M2

- The Subtractive Synth's drag envelopes and the factory presets (both from M1) are C7 and B11.
- B47, keeping several comps of a clip, stays open; not in M3 unless asked.

## For review (product owner)

Agreed by the product owner on 2026-10-11: every recommendation below.

| # | Question | Recommendation |
|---|---|---|
| Q1 | **Order of work** | As above: presets and settings first, then the browser, then the delay family |
| Q2 | **Factory presets: how many, and who** (B4) | Claude drafts, you curate and rename (revised 2026-10-11: Claude builds and names them, you sign them off by ear). About 10–20 for an effect, 30 or more for the Subtractive Synth and the Sampler, 3–5 for the utilities and analysers (Utility, Spectrum, Tuner, Loudness Meter) |
| Q3 | **User presets shared by the plugins and the DAW** | Yes: one folder of plain files per user, so a preset saved in another DAW shows in Segno and the other way round, and backing up is copying a folder |
| Q4 | **CLAP hosting in M3?** Neither JUCE nor tracktion can host CLAP, so it means writing a plugin format of our own (parameters, automation, state, editor windows, threads). Most CLAP plugins also ship VST3 or LV2 | Keep the separate-process scanner in M3; move CLAP hosting to M4, so M3 stays about workflow and sound |
| Q5 | **MIDI learn in M3** (B5 was due in M2) | Yes, as C8: small, and the only way to use a controller today |
| Q6 | **The smaller items design docs deferred to M3** (C7: compressed export formats, the latency test, lower-latency monitoring, mute automation, note chasing) | All in M3, each a small step, so nothing deferred piles up |
| Q7 | **The M3 exit track** | A short track built with presets from the browser, using the space effects, rearranged with the arranger track, mixed against a reference, and exported at a loudness target, with the mouse alone |

## Exit criteria

**Automated:**
- Every M3 device meets the Device Definition of Done except the product owner's sign-off.
- Validators pass; frame budgets hold (the browser and arranger track included); the DAW tests pass.
- Every action is in the menu bar; every DAW feature has its MCP tools.

**Product owner:**
- Device specs approved; each device signed off by ear and by feel.
- Every module, M1 to M3, has factory presets you've curated.
- The exit track (Q7), made with the mouse alone.
