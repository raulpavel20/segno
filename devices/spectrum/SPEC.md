# Spectrum: device spec

- **Status:** Approved (product owner, 2026-10-11), with the extensions (compare, snapshot, reference, side overlay, peak labels; a spectrogram in stage 2) and the defaults under "For review". IDs are permanent. Built (B4, 2026-10-11): what building settled is under "Settled while building"; awaiting the product owner's feel check.
- **Type:** Audio effect (analysis only; audio passes unchanged)
- **Tier / milestone:** Tier 1, M2 (core) → M3 (stage 2)
- **Live target:** Spectrum. References only ([ADR 0011](../../docs/decisions/0011-calibrated-ambition-and-consolidation.md)).

## Purpose

Shows a track's frequency content while you mix: a smooth, readable spectrum with peak hold, for both channels, either one, or mid and side. It leaves the audio untouched and uses the same analyser as the EQ, so the two look and read alike.

What makes it worth using over a separate analyser: it compares the track with any other track without routing, shading where the two fight; it holds a snapshot to compare before and after; and it shows a reference song's average spectrum next to yours, level-matched.

## Beyond Live

Live's Spectrum shows one signal. People who want more install a separate analyser, most often a free one that shows two signals at once, or a paid suite. These additions cover the reasons they do, at a moderate cost, and use what only an integrated DAW knows (ADR 0011: "use the integrated DAW to do things standalone plugins can't"; small, cheap improvements to visual feedback).

| Addition | Reference | Why it's worth it | Cost |
|---|---|---|---|
| **Compare with another track,** shading where both are loud | Two-signal views in popular free analysers; the collision display of a well-known EQ | The DAW already routes any track to a module's sidechain ([ADR 0025](../../docs/decisions/0025-sidechain-routing.md)), so the comparison needs no routing. Kick against bass, vocal against guitars: the most common reason to compare. The collision rule is the groundwork for the EQ's masking display (M3). | Small: the sidechain bus exists (A2); one more curve and a band rule |
| **Snapshot:** freeze the curve to compare before and after | Static spectra in paid analysers | Checking what an EQ or saturator move did, without guessing from memory | Small |
| **Reference curve:** a song's average spectrum, level-matched | Tonal-balance tools sold for exactly this | "Does my mix's balance look like the record I'm aiming for?" is the main reason people buy a separate analyser | Moderate: decoding and averaging a file once, off the audio thread |
| **Side overlay:** the side signal under the main curve | Mid/side views | Shows at a glance what's stereo, above all in the lows, which should be mono | Small: one more curve |
| **Peak labels:** the strongest peaks with frequency and note | Hover readouts, made permanent | Finds resonances, hum and the key of a bass line without hunting with the mouse | Small |
| *Stage 2:* **a spectrogram view** | Analyser suites and standalone meters | Clicks, sibilance and resonances that come and go show over time, where a spectrum averages them away | Moderate: a GPU-drawn scrolling image; with the EQ's stage 2 (M3) |

**Not here,** because they have homes of their own:
- loudness (LUFS), true peak and stereo correlation: the Loudness Meter (M3);
- playing a reference song to A/B against the mix, loudness-matched: the master channel ([innovation backlog](../../docs/innovation-backlog.md), M3). Spectrum shows only its curve.

## Stages

| Stage | Scope | Milestone |
|---|---|---|
| Core | The analyser, compare with collisions, snapshot, reference curve, side overlay, peak labels | M2 |
| Stage 2 | A spectrogram view, with the EQ's stage 2 (which builds its masking display on the same collision rule) | M3 |

## Parameters

All are display settings: they never affect audio, and they are not automatable.

| ID (permanent) | Name | Description | Range | Default | Unit | Curve / skew | Smoothing | Automatable |
|---|---|---|---|---|---|---|---|---|
| `channel` | Channel | Which signal is shown: both channels, left, right, mid or side. | L+R, Left, Right, Mid, Side | L+R | choice | – | – | No |
| `resolution` | Resolution | How finely frequencies are resolved: higher shows more detail in the lows and reacts more slowly. | 2 048, 4 096, 8 192, 16 384 | 8 192 | FFT points | – | – | No |
| `average` | Averaging | How long the display averages, from instant to smooth. | 0 … 5 000 | 300 | ms | centre 300 ms | – | No |
| `hold` | Peak Hold | How long peaks stay marked. | Off, 1 s, 3 s, Infinite | 3 s | choice | – | – | No |
| `slope` | Slope | Tilts the display around 1 kHz, so a balanced mix looks flat. | 0, 1.5, 3, 4.5, 6 | 3 | dB/oct (choice) | – | – | No |
| `range` | Range | How many decibels the display spans from top to bottom. | 60, 90, 120 | 90 | dB shown | – | – | No |
| `side` | Side Overlay | Draws the side signal under the main curve, to show what's stereo. | Off / On | Off | toggle | – | – | No |
| `labels` | Peak Labels | Marks the strongest peaks with their frequency and note. | Off, 3, 6 | 3 | choice | – | – | No |
| `view` | View | Shows the spectrum now, or a spectrogram of how it changed over time (stage 2). | Spectrum, Spectrogram | Spectrum | choice | – | – | No |

- **Channel:** L+R is the average of the left and right power spectra (how both channels look together; a mono sum would hide what's out of phase). Mid is (L + R) / 2 and Side is (L − R) / 2.
- **Side Overlay** applies while Channel is L+R or Mid; with Left, Right or Side it has nothing to add and is dimmed.
- **View** ships in stage 2. Until then it's fixed at Spectrum and not shown.

**Compare source** (which track is compared) is host routing, not a parameter: the module's sidechain input ([ADR 0025](../../docs/decisions/0025-sidechain-routing.md)), chosen in the header like the Compressor's.

**Non-parameter state** (`extra`, [ADR 0017](../../docs/decisions/0017-heavy-data-and-non-parameter-state.md)). Curves are stored as levels in dB at the 121 analysis bands (1/12 octave, 20 Hz to 20 kHz), without the Slope tilt, so they follow the Slope set later:
- `snapshot.curve` and `snapshot.channel`: the frozen curve and the Channel it was taken with;
- `reference.name`: the file's name, as shown;
- `reference.path`: its absolute path, to load it again;
- `reference.curves`: its average spectrum, one curve per Channel choice.

The curves are stored, not recomputed, so a moved or deleted reference file still shows. Snapshot and reference are saved with the project and the plugin's state, not with presets. State version 1.

## DSP design

```
in L/R ──────────────────────────────────────> out L/R (unchanged, zero latency)
   └─> FIFO L, FIFO R ─┐
sidechain L/R ─────────┼─> FIFO SC L, FIFO SC R (when a compare source is connected)
                       v
        UI: dsp::Spectrum: windowed FFT ─> power average ─> 1/12-octave bands
             ─> Slope ─> curves, peak hold, peak labels, collisions ─> display
```

- **Audio:** passes unchanged (a null test against the input, zero latency, no tail).
- **Taps:** the audio thread pushes the input's left and right channels into two `SampleFifo`s, and the sidechain's into two more while a source is connected. Nothing else happens on the audio thread. Mid, side and L+R are formed in the analysis.
- **Analysis: `dsp::Spectrum` (libs/dsp, JUCE-free, `[fast]` tested).** The panel calls it; the UI never reimplements DSP. It covers:
  - a Hann-windowed FFT (`dsp::Fft`) at the chosen resolution, 75 % overlap, at the device's sample rate (reported with the taps);
  - power averaged over time with the Averaging constant (exponential, per hop);
  - 121 bands at 1/12 octave from 20 Hz to 20 kHz, each the average power of the bins in it (interpolated where a band is narrower than a bin), as a density: the same for noise at any Resolution or sample rate. A band's average spreads a sine's power over the band, so the curve shows noise and tonal balance truly, and peak readings (labels, the sine target) come from the bins;
  - the Slope tilt around 1 kHz;
  - peak hold per band: held for the Hold time, then falling at 20 dB/s;
  - peak picking for the labels (below);
  - collisions between two curves (below);
  - the long-term average of a whole buffer, for the reference.
- **The EQ moves onto it.** Its analyser (now a class inside its panel) and its drawing become the shared `dsp::Spectrum` and a `SpectrumDisplay` in `fd_device_ui`, so the EQ and the Spectrum look and read the same.
- **Compare:** the sidechain is analysed with the same Channel, Resolution, Averaging and Slope as the input and drawn as a second line.
- **Collisions:** a band collides when both curves are near their own loudest: each within 12 dB of its own loudest band between 30 Hz and 16 kHz, after Slope. Colliding bands are shaded behind the curves. Shading needs both signals to be loud there, so a kick's click against a bass's sub never shades.
- **Peak labels:** the strongest local maxima of the main curve (after smoothing and Slope) that stand at least 6 dB above the lowest point within 1/3 octave on each side, at least 1/3 octave apart, and inside the shown range.
  - Each reads as frequency and note: "62 Hz · B0", "3.15 kHz · G6": the nearest note, named as everywhere in Segno (middle C is C3, so 440 Hz is A3). The frequency and level come from the loudest bin near the peak, interpolated, so a sine reads its own frequency and level however wide the band.
  - A label moves only when its peak has moved for half a second, so labels don't flicker.
- **Snapshot:** Freeze stores the main curve as shown now, with its Channel. In the DAW it's one undo step ("Freeze spectrum"), through the host binding (ADR 0017).
- **Reference:** choosing a file starts a background job in the panel, off the audio and message threads:
  - it decodes the file with the JUCE-free decoders (`libs/audiofile`, as the Sampler does);
  - it computes the whole file's average spectrum for each Channel choice;
  - it hands the curves and the name to the host binding (`applyExtraState`): one undo step, "Load reference". Reading the state back only parses the stored curves.
  - **Level matching:** the reference curve is moved up or down so its average level over 50 Hz–15 kHz equals the live curve's, following it slowly (over about 3 s) so it doesn't jump.
- **Spectrogram (stage 2):** the same bands over the last several seconds, scrolling, drawn on the GPU. Designed with the EQ's stage 2.

## UI layout

On the module grid ([device-panels.md](../../docs/design/device-panels.md)): sections left to right, each `id` TITLE [columns], with controls at (column, row) and spans; knobs are 1 × 3 cells. As built (B4).

| Section | Controls |
|---|---|
| `spectrum` SPECTRUM [12] | Display (0, 0, 12 × 6), with a legend along its top |
| `curves` CURVES [4] | Side Overlay (0, 0, 2 wide) · Freeze (0, 1, 2 wide), Clear (2, 1, 2 wide) · Reference… (0, 2, 2 wide), Clear (2, 2, 2 wide) · the reference's name, or what reading it came to (0, 3, 4 wide) |
| `settings` SETTINGS [5] | A sub-label (2 wide) and its dropdown (2 wide) per row: Channel, Resolution, Peak hold, Range, Peak labels, Slope · Average (4, 0) |
| header | Compare (the sidechain picker, ADR 0025: None and the other tracks in the DAW; Off and External in a plugin) |

- **The display** draws, back to front:
  - the frequency and dB grid;
  - collision shading;
  - the reference curve and the snapshot (lines);
  - the side curve (a fainter line);
  - the main curve (flat fill and line) and its peak hold;
  - the compare curve (a line);
  - peak labels.
- **The legend** names each curve shown, only while more than the main one is: "Input", "Kick", "Side", "Snapshot", "Reference · Song.wav". In a plugin the compare curve is "Sidechain". A snapshot taken with another Channel says which ("Snapshot (Mid)").
- **Hover** shows a line, the frequency, the note and each shown curve's level there, in place of the legend.
- **Peak labels** sit on a shaded backing, so a curve behind them never makes them hard to read; louder peaks are placed first, and one that would overlap another gives way to the next.
- **Colour roles:** compare, side, snapshot, reference and collision get roles of their own in `fd_ui`, next to `spectrumFill`, `spectrumLine` and `spectrumPre`, documented in ui-foundations.md. The roles keep the curves apart by colour and dash, not colour alone.
- **Reaching it by mouse** (ADR 0040): Freeze, Reference… and Clear are buttons on the panel. Dropping an audio file on the display also loads it as the reference, as a shortcut.
- **Dimming:** the Clear buttons while there's nothing to clear; Side Overlay while Channel is Left, Right or Side.
- The display is the module: CURVES and SETTINGS can be collapsed to leave just the display.

## Factory presets

"Mixing (3 dB/oct)", "Flat", "Mid/side check" (Side Overlay on), "Find resonances" (16 384 points, fast averaging, 6 labels), "Fast", "Slow and smooth" (long averaging, for comparing with a reference).

## Automated test targets

| Metric | Target |
|---|---|
| Audio through the device | Null against the input (< −120 dBFS), zero latency |
| A 1 kHz sine at −12 dBFS, slope 0 | Its peak reading at 1 kHz (±1 bin), −12 dB (±0.5 dB) |
| Pink noise with slope 3 dB/oct | Flat display (±1.5 dB, 50 Hz–15 kHz, after averaging) |
| Mid and Side on L = R input | Side shows nothing (below the range floor) |
| Side overlay, right channel silent | The side curve equals the mid curve (±0.5 dB) |
| The same signal into the input and the sidechain | Main and compare curves coincide (±0.1 dB) |
| Two 100 Hz sines, input and sidechain | Collision shaded around 100 Hz (±1/6 octave), nowhere else |
| A 100 Hz sine against a 5 kHz sine | No collision |
| Sines at 62 Hz, 440 Hz and 3 150 Hz, −12 dBFS each | Three labels: "62 Hz · B0", "440 Hz · A3", "3.15 kHz · G6" |
| Pink noise, Peak Labels 6 | No labels (no peak stands out) |
| A snapshot, then Slope changed | The snapshot tilts with the display |
| A pink-noise file as reference, slope 3 | Its curve flat (±1 dB, 50 Hz–15 kHz) |
| The same audio live and as the reference | The curves coincide (±1 dB) after level matching |
| The reference file deleted, project reopened | Its curve and name still shown |
| Freeze and Load reference in the DAW | One undo step each; undo restores the state before |
| The audio thread while a reference loads | No allocation, lock or file access (conformance) |
| Analysis cost: four 16 384-point FFTs per frame | Within the panel's frame budget (ui-principles) |
| Conformance suite | All pass |

## A/B cases (against the Live target)

Deferred until a Live machine is available.

## A/B log

–

## For review

Approved as drafted (product owner, 2026-10-11):
- **Peak Labels default to 3,** so the feature is seen. Off by default would keep the display quieter on a full mix.
- **Side Overlay defaults to Off;** the "Mid/side check" preset turns it on.
- **Collisions:** each curve within 12 dB of its own loudest band. The EQ's masking display (M3) reuses the rule and may refine it.
- **No compare with the master:** the sidechain can't take the master, since the master depends on every track. A Spectrum on the master can compare with any track. The whole mix against one track waits for the master channel's reference work (M3).

## Settled while building (B4, 2026-10-11)

- **Bands and peaks:** the curve is each band's average power (a density), so noise reads truly at any resolution or sample rate and comparisons are fair; peak readings (labels, hover's peak frequency) come from the bins, interpolated, so a sine reads its own frequency and level. Note names follow Segno's convention (440 Hz is A3).
- **Slope is a choice** (0, 1.5, 3, 4.5, 6 dB/oct), not a knob: "3.0 dB/oct" doesn't fit a knob's value, and nobody needs a tilt between these.
- **Side Overlay sits with the curves,** and each setting has a sub-label: dropdowns show only their value, and "3" or "3 s" alone don't say what they are.
- **The colours:** compare and reference take two hues of the clip palette (blue, violet), the same in every theme; collisions a faint warm wash; the snapshot and side are grey, dashed and dotted.
- **Shared with the EQ:** the analysis (`dsp::SpectrumAnalyser`) and the axes and strokes (`ui::SpectrumPlot`); devices publish their sample rate on the analysis bus. Both displays also take their taps on the panel's refresh tick, so they keep going where there's no frame clock.
- **Sidechain caption:** a panel names its sidechain picker for what it does (`DevicePanel::setSidechainCaption`): here "Compare".

## Sign-off

- [x] Automated: unit, conformance and golden tests; validators (pluginval at strictness 10 and clap-validator pass; 2026-10-11). The audio is the input, so the null test stands in for a golden file
- [x] Layout lint passes (`fd_panel_tests`)
- [x] Snapshots reviewed (100% / 200%; expanded, collapsed, folded; and with sound, a compared track, a snapshot and a reference)
- [ ] Sound signed off by the product owner (date): (audio passes unchanged; the null test covers it)
- [ ] Feel signed off by the product owner (date):
