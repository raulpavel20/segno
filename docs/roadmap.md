# Roadmap

The product is **Segno** ([ADR 0024](decisions/0024-product-name.md)); before the first public build, register `segnoaudio.org` and run a trademark search (M7 packaging at the latest).

Device lists for each milestone come from [devices/device-suite.md](devices/device-suite.md); DAW innovations come from [innovation-backlog.md](innovation-backlog.md). *(Italics)* mark which stage of a staged device a milestone ships ([ADR 0011](decisions/0011-calibrated-ambition-and-consolidation.md)). Every milestone ends with **automated checks green** and **product-owner sign-off**: sound, feel and the manual checklist in `docs/manual-tests/Mx.md`.

**Status legend:** ☐ not started · ◐ in progress · ☑ done

| | Milestone | Devices | DAW / platform | Status |
|---|---|---|---|---|
| D | **Definition** | – | Vision, UI principles, device suite, architecture, ADRs | ☑ |
| M0 | **Foundations** | Utility | Repo, CMake + pinned dependencies, DSP/device/testkit libraries, `fd-render`, UI seed + **rendering spike** ([ADR 0012](decisions/0012-ui-toolkit-juce-everywhere.md)) + **design gate**, plugin builds (VST3/CLAP/LV2/AU), DAW skeleton playing audio through a native device, CI | ☑ |
| M1 | **Vertical slice** | EQ *(core)*, Saturator, Subtractive Synth *(core)*, Sampler *(classic/one-shot)* | Arrangement view (audio clips, cached waveforms within the 8 ms frame budget, smooth zoom/scroll, edit + undo), transport, MIDI input monitoring, docked device chain (native + third-party), action registry + shortcuts, save/open. **Device API frozen** ([ADR 0034](decisions/0034-device-api-frozen.md)). | ☑ |
| M2 | **Make music** | Compressor, Gate, Limiter *(signed off)*, Spectrum *(with compare, snapshot and reference curves)* | MIDI clips + piano roll, audio/MIDI recording with takes on their clips (project folders, every take kept: [ADR 0030](decisions/0030-projects-are-folders.md), [ADR 0031](decisions/0031-recording-keeps-every-take.md), [ADR 0032](decisions/0032-takes-belong-to-clips.md); a pass makes its own clip and a take plays alone: [ADR 0037](decisions/0037-a-pass-makes-its-own-clip.md); comping after fades), automation lanes ([ADR 0028](decisions/0028-automation-lanes.md), [ADR 0029](decisions/0029-independent-automation-lanes.md)), mixer (volume, pan, sends, returns, a mixer view), **bounce** in place and to new tracks ([design](design/bounce.md); signed off), clip fades and crossfades ([design](design/fades.md); signed off), comping (signed off), export ([design](design/export.md); signed off), command palette, a traditional **menu bar** in a title bar of our own, with everything reachable by the mouse ([ADR 0040](decisions/0040-a-menu-bar-and-everything-by-mouse.md), [design](design/menu-bar.md); signed off), managing **tracks and clips** (rename, delete, reorder, duplicate, colour, clipboard: [ADR 0041](decisions/0041-tracks-and-clips-managed-in-place.md); every track and lane moves on its own: [ADR 0042](decisions/0042-every-row-moves-on-its-own.md); [design](design/tracks-and-clips.md); signed off), **module panel kit** ([ADR 0022](decisions/0022-module-panels.md)), **UI foundations** ([ADR 0023](decisions/0023-ui-foundations.md)), **MCP server** for assistants ([ADR 0021](decisions/0021-mcp-server.md)), **sidechain routing** ([ADR 0025](decisions/0025-sidechain-routing.md)), **one selection and context panels per view** ([ADR 0027](decisions/0027-selection-and-context-panels.md)). Everything built up to 2026-10-10 is signed off ([plan](milestones/M2-make-music.md)) | ◐ |
| M3 | **Workflow & space** | Delay, Reverb *(algorithmic)*, Filter, Chorus & Ensemble, Phaser & Flanger, Auto Pan & Tremolo, Tuner, Loudness Meter, EQ *(stage 2: dynamic, linear phase, match, masking display)*, Spectrum *(stage 2: spectrogram)* | Browser with unified search, preset system, inspector, **arranger track**, **reference-track A/B**, out-of-process plugin scanner, CLAP hosting | ☐ |
| M4 | **Depth** | Drum Sampler, Sampler *(slice)*, Multiband Dynamics, Distortion, Drum Bus, Arpeggiator, Chord, Scale | Racks + macros, Drum Rack, modulators with **drag-to-modulate**, **project-wide key/scale**, warping/time-stretch, groove & quantise. **Tier 1 complete.** | ☐ |
| M5 | **Instruments** | Wavetable Synth, FM Synth, Granular Synth, Drum Synth, Sampler *(multisample)*, Subtractive Synth *(stage 2)* | MPE across the DAW | ☐ |
| M6 | **Breadth** | Reverb *(stage 2: convolution/hybrid)*, Grain Delay, Stutter, Pedal, Tube, Amp & Cabinet, Bitcrusher, Shifter, Vocoder, Transient Shaper, De-esser, remaining MIDI effects | MIDI tools (first set). **Tier 2 complete.** | ☐ |
| M7 | **1.0 release** | Factory preset pass across all devices, core content | Packaging (AppImage/Flatpak, Windows installer, notarised macOS), light theme, accessibility pass, autosave/crash recovery, DAWproject import/export, performance pass | ☐ |
| 1.x | **Specialty** | Tier 3: Dual-engine MPE Synth, physical modelling, spectral devices, Erosion, Vinyl, Filter Delay, pitch correction, looper | Plugin sandboxing, detachable panels, content packs | ☐ |

## Milestone principles

- **Every milestone produces something you can use:** plugins that load in your DAW, or a DAW build you can make music in.
- **Device families share a DSP core** (dynamics detector, modulated delay line, filter set, voice engine, sampler engine). This is why the device order above is not arbitrary.
- **Large devices ship in stages.** A milestone includes only the stages listed here; each stage must be complete and usable on its own.
- **The framework is frozen after M1.** Changing the device API or state format after that requires an ADR and a migration path.
- **No milestone is done without the product owner's sign-off.** Automated checks are necessary but not sufficient.

## Before M0 can start

- [x] All decisions accepted (ADRs 0001–0013, except 0010 which is superseded).
- [x] Build toolchain installed (command in [dev-environment.md](dev-environment.md#build-toolchain-to-install-before-m0)).
- [x] Hosting decided: GitHub (`raulpavel20/segno`). CI runs on demand for now (Actions tab, "Run workflow"), since early work is pushed straight to main; push and pull-request triggers return once changes go through pull requests.
- [x] Repository structure confirmed: one repository ([ADR 0013](decisions/0013-single-repository.md)).
