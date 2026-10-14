# Spectrum: device spec

- **Status:** Approved (product owner, 2026-10-11), with the extensions (compare, snapshot, reference, side overlay, peak labels; a spectrogram in stage 2) and the defaults under "For review". IDs are permanent. Built (B4, 2026-10-11): what building settled is under "Settled while building". Done: signed off by the product owner (2026-10-11).
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
| `view` | View | Shows the spectrum now, or a spectrogram of how it changed over time. | Spectrum, Spectrogram | Spectrum | choice | – | – | No |
| `length` | Length | How many seconds the spectrogram shows, from now back. | 2 s, 5 s, 10 s, 20 s | 5 s | choice | – | – | No |

- **Channel:** L+R is the average of the left and right power spectra (how both channels look together; a mono sum would hide what's out of phase). Mid is (L + R) / 2 and Side is (L − R) / 2.
- **Side Overlay** applies while Channel is L+R or Mid; with Left, Right or Side it has nothing to add and is dimmed.
- **View** and **Length** are in the header (stage 2); Length shows only with the spectrogram.

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
- **Spectrogram (stage 2):** designed under "Stage 2: the spectrogram" below.

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

Built and named by Claude ([presets.md](../../docs/design/presets.md) §13; `devices/spectrum/presets/`, made with `fd-presets` from recipes); signed off by the product owner by ear (2026-10-11). 6 presets, each checked by the conformance suite: it loads, plays cleanly under 0 dBFS and differs from the rest.

- **Mixing** (Mix): Tilted 4.5 dB per octave so a full, balanced mix looks about flat, and averaged to show its overall balance.
- **Flat** (Check): No tilt: the spectrum as measured, for test tones and noise.
- **Mid and side check** (Check, Stereo): The mid with the side drawn under it, to see what's stereo and where.
- **Find resonances** (Fix): Fine resolution, quick averaging and six labelled peaks, to find a ringing frequency.
- **Fast** (Check): Follows every hit: a short window, little averaging, no held peaks.
- **Slow and smooth** (Mix): Long averaging, for a song's overall balance and for comparing with a reference.

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

## Stage 2: the spectrogram (for review)

**Status:** approved by the product owner (2026-10-15) with every recommendation of G1–G10, and built the same day (below, "Settled while building stage 2"). Waiting for the product owner's sign-off.

### How others do it (October 2026)

| | A spectrogram? | How |
|---|---|---|
| **Live 12** (Spectrum) | No | One curve |
| **Bitwig** (Spectrum) | None found | Two traces, with a fall rate for persistence |
| **Studio One** (Spectrum Meter) | Yes | Sonogram and Waterfall modes beside the FFT and third-octave views |
| **Cubase** (SuperVision) | Yes | A Spectrogram module among about 18; FFT size and level range settings |
| **iZotope Insight 2** | Yes | 2D or 3D; colour schemes, scroll speed, FFT size and overlap; Freeze and zoom |

Most draw time across and frequency up (a [Computer Music](https://www.pressreader.com/australia/computer-music/20221102/284490052423504) guide calls that the usual way), level as colour. Sources: [Fender Studio Pro manual: metering](https://fenderstudiopromanual.fender.com/en/Content/Built-In_Effects_Topics/Metering,%20Analysis,%20and%20Signal%20Generation.htm), [Cubase 13 plug-in reference](https://archive.steinberg.help/cubase_pro_plugin_reference/v13/en/Cubase_Pro_13_Plug_in_Reference_en.pdf), [Bitwig user guide](https://www.bitwig.com/userguide/bws44-504/device_descriptions), [iZotope Insight: spectrogram](https://www.izotope.com/en/products/insight/features/spectrogram).

### The design

- **View** (`view`, already reserved) switches the display between the spectrum and the spectrogram, in place: the same display, the same size.
- **Frequency runs across, as in the spectrum; time runs down, the newest at the top.** A frequency sits where it sits in the spectrum, so switching views never moves what you were looking at, and the frequency axis keeps the display's long side: about 60 px an octave, against 16 if it ran up the short side. Most analysers draw time across; with a display four times wider than tall, frequency is what needs the room.
- **Each line is one transform, as it was heard:** no averaging over time (Average would smear exactly what this view is for: what comes and goes). It reads the transform at every pixel across, finer than the curve's 121 bands, so harmonics and a narrow resonance show apart.
- **A new line about every 10 ms** whatever the Resolution (the transforms overlap more at the larger sizes), so a click is a single line. Resolution still trades detail in the lows for time: at 16 384 points a transform spans a third of a second.
- **Length:** a new setting (`length`): 2, 5, 10 or 20 seconds on the display, default 5 s.
- **Colour:** one hue, the analyser's own: from the display's background at the bottom of the Range, through the curve's colour, to near white at its top. Brightness carries the level, so it reads as Segno's analyser and as one scale. Range sets what's dark; Slope tilts it as it tilts the curve.
- **When it moves:** while the song plays, or while sound comes in (playing live). When both stop, it holds the last screenful, so what just happened can be studied. A plugin follows its host's transport the same way.
- **Compare with another track:** where both are loud at the same moment, the line is tinted with the collision colour (the spectrum's warm wash, by the same rule). It shows exactly when a kick and a bass collide, which the spectrum averages away. The compare track's own spectrogram isn't drawn: one image, not two.
- **Hover:** a crosshair, and the frequency, the note, how long ago ("1.2 s ago") and the level there, in place of the legend.
- **What applies:** Channel, Resolution, Range and Slope apply to both views. Average, Peak hold, Peak labels, Side overlay, the snapshot and the reference belong to the curve: in the spectrogram they dim (CURVES as a whole), visible and kept for when the view turns back.
- **Where the settings sit:** View in the header, right-aligned, as a module-wide display setting (device-panels.md); Length beside it, shown only in the spectrogram. Nothing on the grid moves.
- **Drawing:** an image the size of the display, a line written per transform, drawn as two halves of a ring: each frame costs its new lines and one copy. The first plan said "drawn on the GPU"; Segno draws with JUCE's software renderer, and a ring image needs nothing more (the frame-budget test checks it).
- **The plugin:** the same view in the plugin and the DAW.
- **Factory presets:** two more, **Spectrogram** (Mix: the view at its defaults) and **Find clicks** (Fix: the view, Resolution 2 048, Length 2 s, no tilt). Eight in all.
- **IDs** (permanent once approved): `view` (Spectrum, Spectrogram; reserved since stage 1) and `length` (2 s, 5 s, 10 s, 20 s).

### Tests (done when)

| What | Target |
|---|---|
| A sine sweeping 100 Hz to 10 kHz over 2 s | The brightest point of each line follows it across, within 1/24 octave |
| A click in silence | One line lit across the band, those around it dark |
| A 1 kHz sine at −12 dBFS, Slope 0, Range 90 | Its point at the colour for −12 dB |
| The transport stopped, no sound | The image holds; playing again, it moves |
| Two 100 Hz bursts, input and compare, half a second apart then together | Tinted only where they overlap in time |
| Switching views | The settings kept; a frequency at the same x in both |
| The panel | Layout lint passes; screenshots of both views at 100 % and 200 %, with sound and a compared track |
| Drawing at 16 384 points, 20 s | Within the panel's frame budget |

### Settled while building stage 2 (2026-10-15)

- **The analysis:** `dsp::SpectrumAnalyser` gained its own hop (`setHop`: a line every 480 samples at 48 kHz), a hook on each transform, and two readings of the last transform alone: the loudest bin across each pixel's span of frequencies (`lineAt`; a sine reads its own level, within the Hann window's loss between bins, up to 1.4 dB) and its bands (`currentBands`, for collisions in time). `dsp::SpectrogramHistory` keeps the rows: each the loudest of the lines in its time, with where they collided ([fast] tests).
- **A row is a pixel:** the rows fill the display above a strip kept for the frequency labels, which never sit on the picture; the time marks and the hover readout sit on the shade, as peak labels do. Each row spans Length divided by the rows (5 s over 146 rows: 34 ms, three or four lines).
- **The picture is written whole when it changes,** once a frame at most (about 90 000 points through a 256-step lookup of the theme's colours), simpler than the ring the design described and well within the frame budget (the panel test times the heaviest case, 16 384 points over 20 s).
- **The colours** are a role function in `fd_ui` (`Theme::spectrogram`): the display's background, the analyser's grey and near white, as one ramp, with the collision colour mixed in where both tracks are loud (ui-foundations.md).
- **When it moves:** the device publishes whether the song plays in an analysis slot; a line is kept while it plays or while any of it is louder than −150 dB. Just after a sound stops, the window still holds its tail, so it moves on for one window's length (up to a third of a second at 16 384 points) before it holds.
- **Below the range,** the readout says so ("< −90 dB"), not the window's own floor.
- **Presets:** Spectrogram and Find clicks; the six before gained Length at its default.
- **Found while testing:** the panel tests' feeding helper played a few hundred samples twice at each quarter second; the averaged curve hid it, the spectrogram showed it as a line across. Fixed in the tests.

### Questions for review

Approved by the product owner (2026-10-15) with every recommendation.

| # | Question | Recommendation |
|---|---|---|
| G1 | **Orientation** | Frequency across (as in the spectrum), time down with the newest at the top. Not time across: frequency would get a quarter of the room, and switching views would move everything |
| G2 | **What a line shows** | One transform, unaveraged, read at every pixel across (finer than the 121 bands) |
| G3 | **Time** | A line about every 10 ms at any Resolution; Length 2, 5, 10 or 20 s, default 5 s |
| G4 | **Colour** | One hue, the analyser's, dark to near white over the Range. Not a multi-hue map: it would bring a palette nothing else in Segno uses |
| G5 | **When it moves** | While the song plays or sound comes in; held when both stop. No Freeze button of its own |
| G6 | **Compare** | Collisions tinted in time; the compare track's own spectrogram not drawn |
| G7 | **Settings that don't apply** | Dimmed in the spectrogram view, kept for the spectrum |
| G8 | **Where View and Length sit** | The header, right-aligned; Length only while it applies |
| G9 | **Drawing** | A ring image in the software renderer, not the GPU |
| G10 | **Presets** | Two more: Spectrogram and Find clicks |

## Sign-off

- [x] Automated: unit, conformance and golden tests; validators (pluginval at strictness 10 and clap-validator pass; 2026-10-11). The audio is the input, so the null test stands in for a golden file
- [x] Layout lint passes (`fd_panel_tests`)
- [x] Snapshots reviewed (100% / 200%; expanded, collapsed, folded; and with sound, a compared track, a snapshot and a reference)
- [x] Sound signed off by the product owner (2026-10-11): the audio passes unchanged; the null test covers it
- [x] Feel signed off by the product owner (2026-10-11)
