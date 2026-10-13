# EQ: device spec

- **Status:** Core approved by the product owner (2026-10-10, with the M1 sign-off); IDs are permanent. Stage 2 (M3, B8) approved by the product owner (2026-10-13, with every recommendation of E1–E14); its IDs are permanent. Step 2a (no delay) and step 2b (the spectral engine: linear phase and spectral bands) built 2026-10-13, and signed off by the product owner the same day.
- **Type:** Audio effect
- **Tier / milestone:** Tier 1, M1 (core) → M3 (stage 2)
- **Live target:** EQ Eight, EQ Three, Channel EQ (all covered by this one device). References only ([ADR 0011](../../docs/decisions/0011-calibrated-ambition-and-consolidation.md)).

## Purpose

The one EQ for everything: surgical fixes, broad tone shaping and channel-strip work. Bands are added by clicking on the curve and edited directly on the display.

## Beyond Live

This is one of the few devices that deliberately goes further (aimed at FabFilter Pro-Q):
- **Up to 24 bands**, added and removed on the display. Plugin formats need a fixed parameter set, so 24 is the cap, and it's rarely reached.
- **Steep slopes** for cuts, up to 96 dB/oct.
- **Per-band stereo placement:** Stereo, Left, Right, Mid or Side.
- **Analog-matched** response with no cramping near Nyquist.
- **Compact view:** a simple 3-band layout for channel-strip use, on the same engine.
- **Stage 2 (M3):** dynamic bands with a sidechain, spectral bands that tame resonances, a linear-phase mode, a masking display that makes room for another track, auto gain, and listening to a band or to what the dynamics change. See "Stage 2" below.

## Stages

| Stage | Scope | Milestone |
|---|---|---|
| Core | 24 bands, all types and slopes, stereo placement, pre/post spectrum, output gain, gain scale, compact view | M1 |
| Stage 2 | Dynamic and spectral bands with a sidechain, a linear-phase mode, the masking display and Make room, auto gain, Listen and Delta (see "Stage 2" below) | M3 |
| Later | EQ match (the product owner, 2026-10-13); a natural-phase mode; spectral bands at zero latency; an all-pass band; masking against every track at once; drawing a curve to make bands | With demand |

## Parameters

**Per band** (`n` = 1…24; IDs are `band<n>.<param>`, e.g. `band3.freq`):

| ID | Name | Description | Range | Default | Unit | Curve | Smoothing |
|---|---|---|---|---|---|---|---|
| `band<n>.on` | Band n On | Turns the band on or off. | Off / On | Off (bands 1–4 On) | toggle | – | 10 ms crossfade |
| `band<n>.type` | Band n Type | The band's filter shape: bell, shelf, cut, notch, band pass or tilt. | Bell, Low Shelf, High Shelf, Low Cut, High Cut, Notch, Band Pass, Tilt | Bell (band 1 Low Cut, band 4 High Cut) | choice | – | crossfade on change |
| `band<n>.freq` | Band n Freq | The band's centre or corner frequency. | 10 … 30 000 | spread across bands | Hz | log (centre 632 Hz) | 20 ms multiplicative |
| `band<n>.gain` | Band n Gain | How much the band boosts or cuts (bells, shelves and tilt). | −30 … +30 | 0 | dB | linear | 20 ms |
| `band<n>.q` | Band n Q | How narrow the band is: a higher Q affects a narrower range. | 0.1 … 40 | 0.71 | – | log (centre 2) | 20 ms multiplicative |
| `band<n>.slope` | Band n Slope | How steeply the band cuts, in dB per octave (cut filters). | 6, 12, 18, 24, 36, 48, 72, 96 | 12 | dB/oct (cuts) | – | crossfade on change |
| `band<n>.place` | Band n Placement | Which part of the signal the band changes: both channels, left, right, mid or side. | Stereo, Left, Right, Mid, Side | Stereo | choice | – | crossfade on change |

**Global:**

| ID | Name | Description | Range | Default | Unit |
|---|---|---|---|---|---|
| `output` | Output | The level after the EQ. | −24 … +24 | 0 | dB |
| `scale` | Gain Scale | Scales every band's boost or cut at once; 0 % makes the EQ flat. | 0 … 200 | 100 | % (scales every band's gain) |
| `analyzer` | Analyzer | Which signal the spectrum behind the curve shows: before the EQ, after it, or both. | Off, Pre, Post, Pre+Post | Post | choice (UI only, not automatable) |

Non-parameter state: the UI view (full or compact), stored in `extra`. State version 1.

## DSP design

- **Each band** is one analog-matched second-order section (`fd::dsp::designMatched`). Three candidates are computed and the one closest to the analog magnitude is kept:
  - a 5-point fit: poles and zeros solved together to match at DC, the band edges, the centre and Nyquist;
  - Vicanek's 3-point design: exact pole mapping, numerator matched at DC, centre and Nyquist;
  - a prewarped bilinear design.

  Measured accuracy is in the test targets below. Bilinear designs err by up to 6–8 dB at high frequencies; ours stay within about 0.6 dB for most settings. Resonant shelves (Q > 1) near Nyquist remain approximate.
- **Cuts** above 12 dB/oct cascade Butterworth sections, with a first-order section for odd orders.
- **Coefficients** are recomputed on control ticks from the smoothed freq, gain and Q. Recomputing every 32 samples is inaudible and avoids zipper noise.
- **Stereo placement:** Mid and Side bands run in an M/S-encoded domain; Left and Right bands run on that channel only.
- **Zero latency** in the core (minimum-phase). Linear phase arrives in stage 2.
- **Analysis taps:** pre and post sample FIFOs for the spectrum (A4). Display data is computed in the UI.
- The UI curve uses `biquadResponse()`-style pure functions on the same coefficients.

## UI layout

On the module grid ([device-panels.md](../../docs/design/device-panels.md)): sections left to right, each `id` TITLE [columns], with controls at (column, row) and spans; knobs are 1 × 3 cells. About 788 px wide.

| Section | Controls |
|---|---|
| `curve` CURVE [10] | Display (0, 0, 10 × 6): spectrum (pre/post, flat) and curve (solid line, flat fill), draggable band nodes |
| `band` BAND *n* [3], power = the band's On | Freq (0, 0), Gain (1, 0), Q (2, 0) · Type, Slope and Place: a sub-label (column 0) and a 2-wide combo (columns 1–2) in rows 3, 4 and 5 |
| `output` OUTPUT [1] | Output (0, 0) · Scale (0, 3) |
| header | Analyzer |

- **Display interactions:**
  - double-click empty space: add a Bell band there;
  - drag a node: frequency and gain; mouse wheel on a node: Q;
  - double-click a node: band on/off; right-click a node: type, slope and placement menu.
- BAND shows the selected band; its title follows it ("BAND 3"). Gain is dimmed for types without gain, and Slope for types without a slope, so nothing moves when the type changes.
- **Compact view (later):** three fixed bands (low shelf, bell, high shelf) shown as knobs, plus low cut and output.

## Factory presets

Built and named by Claude ([presets.md](../../docs/design/presets.md) §13; `devices/eq/presets/`, made with `fd-presets` from recipes); signed off by the product owner by ear (2026-10-11). 18 presets, each checked by the conformance suite: it loads, plays cleanly under 0 dBFS and differs from the rest.

- **Low cut 80 Hz** (Clean): Clears rumble and low mud below 80 Hz, 24 dB per octave.
- **Subsonic cut** (Master, Bus, Clean): Takes out what you can't hear below 30 Hz, which still uses headroom.
- **Vocal presence** (Vocals, Bright): A low cut, less box at 300 Hz, presence at 3.5 kHz and a little air.
- **Vocal warmth** (Vocals, Warm): Body at 200 Hz, smoother upper mids and a softer top.
- **De-mud** (Clean, Bus): Cuts the boxy low mids where mixes get muddy.
- **Kick tighten** (Drums): Thump at 60 Hz, less box at 350 Hz and the beater's click at 4 kHz.
- **Snare body and crack** (Drums): Body at 200 Hz, less ring around 900 Hz and crack at 5 kHz.
- **Bass definition** (Bass): A clean low end, less mud and the growl at 800 Hz that makes a bass read on small speakers.
- **Acoustic guitar** (Guitar, Bright): Less boom, a little sparkle at 5 kHz and air on top.
- **Piano clarity** (Keys): A gentle low cut, less mud and a little definition and air.
- **Hi-hat clean** (Drums, Bright): Everything below 300 Hz out, a little more shimmer.
- **Air shelf** (Bright, Subtle, Master): Opens up the top above 12 kHz.
- **Telephone** (FX, Vocals): Only the band a phone line carries, with its honk.
- **Mid-side widen** (Wide, Master, Bus): More highs in the side and none of the lows there, so the width grows and the bass stays centred.
- **Surgical notch** (Fix, Clean): A narrow notch to sweep onto a ringing frequency; move it to taste.
- **Tilt brighter** (Master, Bright): Tilts the whole spectrum around 1 kHz: brighter highs, leaner lows.
- **Tilt warmer** (Master, Warm): Tilts the whole spectrum around 1 kHz: fuller lows, softer highs.
- **Master polish** (Master, Subtle): A subsonic cut, a touch less mud, a little presence and air.

## Automated test targets

| Metric | Target |
|---|---|
| Bells vs analog prototype (48 kHz, up to 20 kHz, Q 0.3–6) | ≤ 0.8 dB for fc ≤ 12 kHz; ≤ 1.6 dB at 16 kHz; exact gain at fc |
| Shelves and tilt vs analog (Q 0.3–1) | ≤ 0.7 dB for fc ≤ 8 kHz; ≤ 2 dB at 12 kHz |
| Every type | Never worse than the bilinear (RBJ-style) design |
| Cut slopes | Asymptotic slope within ±0.5 dB/oct of nominal |
| All bands off, output 0 dB | Null against input (< −120 dBFS) |
| Fast freq sweep (20 Hz→20 kHz in 1 s) on a sine | No clicks (click score < 8); stable |
| Mid/Side placement on L = R input | Side bands have no effect |
| Conformance suite | All checks pass |

## Stage 2 (M3, B8)

What the EQ gains in M3. The core above stays as it is: its parameter IDs, its sound at zero latency, its panel.

### Purpose

The EQ learns to move. A band can follow the sound: cut a harsh "s" only when it's sung, boom only when a note blooms, or the bass's low end only when the kick hits (another track as its trigger). A **spectral** band goes further, in the spirit of the dedicated resonance suppressors people buy separately: inside its range, it follows each frequency on its own and turns down only what sticks out, so a harsh guitar or a ringing vocal is smoothed without dulling the rest. Around that: a **linear-phase** mode for mastering, with its delay always in plain sight; a **masking display** that shows where this track and another fight and makes room in one click; **auto gain**, so a change is judged by its tone, not its loudness; and **Listen** and **Delta**, to hear a band's range or what the dynamics take away.

### Beyond the usual

Most EQs with dynamics stop at a dynamic bell. These additions are where the best paid EQs and the separate resonance suppressors are today; the integrated DAW makes the sidechain and the masking display free (any track is one pick away, ADR 0025).

| Addition | Reference | Why it's worth it | Cost |
|---|---|---|---|
| **Dynamic bands** with a sidechain | Dynamic EQs, free and paid | De-essing, taming boom and harshness only when they happen, ducking one track's range under another's | Moderate: a detector and gain computer per band, the gain moving the band's own filter |
| **Spectral bands** (E2) | The resonance suppressors sold on their own; the leading paid EQ's spectral dynamics | The gentlest way to tame resonances and harshness that move around; people buy a separate plug-in for it | Large: a short-time spectral engine (with delay), detection and smoothing per frequency |
| **Linear phase** (E4) with the delay shown | Every mastering EQ | No phase shift: steep cuts keep transients, parallel and mid/side work stays aligned | Large: the same spectral engine, a filter rebuilt as settings move |
| **Masking display** and **Make room** (E7) | Masking meters and spectral ducking tools | "Why can't I hear the bass?": see the collision, click, and the bass ducks there only when the kick plays | Moderate: the Spectrum's compare and collision rule, and a band made for you |
| **Auto gain** (E8) | The leading paid EQ | A boost always sounds better because it's louder; matched level makes EQ honest | Small: an estimate from the curve |
| **Listen** and **Delta** (E9) | Band solo in most EQs; delta in the resonance suppressors | Finding the frequency to fix; hearing exactly what a dynamic or spectral band removes | Small |

### Parameters

New parameters come after the core's, with `sinceVersion` 2; the state version becomes 2. A saved core state loads unchanged: every new parameter's default leaves the sound as it was.

**Per band** (`n` = 1…24):

| ID (permanent) | Name | Description | Range | Default | Unit | Curve / skew | Smoothing | Automatable |
|---|---|---|---|---|---|---|---|---|
| `band<n>.dyn` | Band n Dynamics | Lets the band move with the sound: it turns further up or down as its range gets louder. | Off / On | Off | toggle | – | 20 ms crossfade | Yes |
| `band<n>.spectral` | Band n Spectral | Follows each frequency in the band on its own, so only what sticks out moves: tames resonances and harshness. | Off / On | Off | toggle | – | 50 ms crossfade | No (changes latency) |
| `band<n>.range` | Band n Range | How far the band moves when its range is loud: below zero it cuts, above zero it boosts. | −30 … +30 | −6 | dB | linear | 20 ms | Yes |
| `band<n>.threshold` | Band n Threshold | How loud the band's range must get before it moves. | −60 … 0 | −24 | dB | linear | 20 ms | Yes |
| `band<n>.auto` | Band n Auto Threshold | Sets the threshold from the sound itself, so the band moves on what stands out from its usual level. | Off / On | On | toggle | – | 20 ms crossfade | Yes |
| `band<n>.attack` | Band n Attack | How quickly the band moves when its range gets loud. | 0.5 … 200 | 5 | ms | centre 10 ms | 20 ms multiplicative | Yes |
| `band<n>.release` | Band n Release | How quickly the band moves back once its range gets quieter. | 10 … 2 000 | 120 | ms | centre 150 ms | 20 ms multiplicative | Yes |
| `band<n>.detail` | Band n Detail | Spectral bands: higher finds narrow peaks and cuts them narrowly, lower treats broad build-ups. | 0 … 100 | 50 | % | linear | 50 ms | Yes |
| `band<n>.trigger` | Band n Trigger | What the band listens to: its own range of the sound, or the same range of the sidechain. | Input, Sidechain | Input | choice | – | 20 ms crossfade | Yes |

- **Range and Gain:** a dynamic band rests at its Gain and moves toward Gain + Range as its trigger rises above the threshold, all the way once it's 12 dB above (with a soft knee near the threshold). A dynamic band at 0 dB Gain and −6 dB Range is a band that only ever cuts, and only when loud.
- **Which types move:** Bell, Low Shelf, High Shelf and Tilt. Cuts, Notch and Band Pass have no gain to move: their dynamics controls are dimmed.
- **Spectral** needs Dynamics on, and works on Bell, Low Shelf and High Shelf: the band's shape becomes where it may act, and Range the deepest it may cut (or boost). It makes the EQ delay its sound (see "Latency").
- **Trigger, Sidechain** with no sidechain connected listens to the input, as the Compressor's does.

**Global:**

| ID (permanent) | Name | Description | Range | Default | Unit | Automatable |
|---|---|---|---|---|---|---|
| `phase` | Processing | Zero latency keeps the analog-style phase and adds no delay; Linear phase changes no phase but delays the sound. | Zero latency, Linear phase | Zero latency | choice | No (changes latency) |
| `resolution` | Resolution | How fine the linear-phase and spectral processing is in the lows: higher is finer, with more delay. | Low, Medium, High | Medium | choice | No (changes latency) |
| `autogain` | Auto Gain | Keeps the level about the same as bands boost or cut, so you judge the tone, not the loudness. | Off / On | Off | toggle | Yes |
| `delta` | Delta | Plays only what the dynamic and spectral bands change, to hear what they take away or add. | Off / On | Off | toggle | No (not kept) |
| `listen` | Listen | Plays only a band's range, to find the frequency to fix. | Off, Band 1 … Band 24 | Off | choice | No (not kept) |

- **Listen and Delta aren't kept:** they're how you listen, not how the EQ sounds. They take a new flag, `ParamFlags::Transient` (E10): not saved, not in presets, no undo step. The panel sets them through the host binding like any parameter.
- **The sidechain** is host routing, chosen in the header (ADR 0025): the EQ gains a stereo sidechain bus (E11).

### DSP design

```
in ─┬──────────────────────────────────────────────────────────────── delay (when there's latency) ─┐
    │                                                                                                 │
    ├─> Zero latency: the bands as now (minimum phase), a dynamic band's gain moved by its detector ──>├─> Auto gain, Output ─> out
    │                                                                                                 │
    └─> Linear phase, or any Spectral band: the spectral engine (STFT) ─> every band's gain per bin ──┘
sidechain ─> the dynamic and spectral detectors (Trigger: Sidechain); FIFO for the masking display
```

- **Dynamic bands (zero latency):** each band's trigger is its own range: the band's shape made into a band pass (a bell's bell, a shelf's shelf), on the input or the sidechain, in the band's placement (left, right, mid, side or both). A level detector from the dynamics core (`dsp::LevelDetector`, RMS-like, with the band's attack and release) feeds a gain computer (threshold, soft knee, 12 dB of travel, Range). The band's gain moves on the EQ's 32-sample control grid, its coefficients interpolated as they are now, so a moving band never zips.
- **Auto threshold:** the threshold rides 6 dB above the trigger's slower average (over about 2 s), so the band moves on what stands out from the band's usual level, at any input level. Off, the threshold is the knob's, in dBFS: for ducking under another track, or anything level-dependent.
- **The spectral engine** (new, `libs/dsp`, JUCE-free, `[fast]` tested): a short-time Fourier transform with overlapping windows (weighted overlap-add, 75 % overlap). Each frame, it multiplies the spectrum by the EQ's whole magnitude response at that moment, per stereo placement: left/right and mid/side gains in the same frame, so mixing placements doesn't double the delay. The frame's response is built from the same band designs the curve shows (`Eq::bandResponse`), so the UI and the audio agree.
  - **Linear phase:** every band runs here. The response is magnitude only, so no phase changes. Dynamic bands move per frame.
  - **Zero latency with a spectral band:** the other bands stay minimum phase (as now), delayed to line up; the spectral bands run in the engine.
  - **Resolution:** the frame length, in time, so the lows resolve the same at any sample rate. Low, Medium and High aim at about 50, 100 and 200 ms of delay; their low-frequency resolution and the test targets below pin them down while building.
- **Spectral bands:** each frame, inside the band's shape:
  - the detector's spectrum (input or sidechain, in the band's placement) is tilted 3 dB/oct up, so a full mix's natural slope doesn't make the lows trigger first;
  - **Auto:** each frequency's level is compared with a smoothed copy of the spectrum around it (how much it sticks out); Detail sets that smoothing: high finds narrow peaks, low broad build-ups. **Manual:** compared with the threshold;
  - what's over is turned down (or up, with a positive Range) by its excess, up to Range, weighted by the band's shape so the edges fade in;
  - each frequency's gain follows with the band's attack and release, faster in the highs and slower in the lows (as the resonance suppressors do), so the lows don't flutter;
  - the gains are smoothed across neighbouring frequencies, so the cuts are filters, not holes.
- **Latency:** constant for a setting (ADR 0016), reported, and changed only by Processing, Resolution and Spectral, which carry `ParamFlags::ChangesLatency` (ADR 0020: hosts re-read it). Zero latency with no spectral band: none, as now.
- **The masking display:** the sidechain is analysed with the Spectrum's analyser and drawn as a second curve; its collisions with the EQ's output use the Spectrum's rule (`dsp::spectrumCollisions`), the groundwork it was made for.
- **Make room:** from a collision, the panel adds one band through the host binding: a dynamic Bell at the collision's centre, as wide as the collision, Gain 0, Range −6 dB, Trigger Sidechain, the threshold set 6 dB under the sidechain's level there (Auto off). Spectral if the collision is wider than an octave. One undo step, "Make room".
- **Auto gain:** an estimate from the curve, not a measurement (it never pumps, and it's right offline): the loudness change of pink noise through the static curve (every band at its Gain), K-weighted as BS.1770 is, applied as a gain before Output and smoothed over 50 ms. Dynamic and spectral moves aren't in it: they're momentary, and following them would undo them.
- **Listen:** the output is the input through the band's shape as a band pass (as Trigger hears it), crossfading over 20 ms. **Delta:** the output is the wet signal minus what it would be with every band static: what the dynamics did.
- **Analysis taps:** FIFO 0 and 1 (pre and post) as now; FIFO 2 the sidechain, for the masking display; FIFO 3 the dynamics, every 10 ms: each dynamic band's current gain, and the spectral bands' gain across frequency (the reduction the display draws).

### Latency, shown wherever it's chosen (E5)

The product owner asked for the delay to be impossible to miss. It's a cost: the DAW keeps every track in time, but anything played or sung live through this track is heard that much later.
- **Where it's chosen, it's named:** the Processing and Resolution menus list each choice with its delay ("Linear phase: Medium, about 100 ms"); the Spectral toggle's tooltip says it adds delay.
- **While there's any, the header says so:** a "+100 ms" badge in the warning colour beside Processing, whose tooltip explains it in a sentence ("This EQ delays its track by 100 ms. The DAW keeps the tracks in time; what you play live on this track is heard 100 ms late."). Zero latency shows nothing.
- **The first time a project's EQ adds delay,** a toast says so once, with the same sentence.
- **Assistants** see a module's latency in `get_module` (in ms), so they can say what a change costs.

### UI layout

| Section | Controls |
|---|---|
| `curve` CURVE [10] | Display (0, 0, 10 × 6): as now, plus the sidechain's curve and collisions, each dynamic band's movement (its curve moving live, its range shaded), spectral bands' reduction across frequency, and a Listen toggle as the display's accessory (it listens to the selected band) |
| `band` BAND *n* [3] | As now |
| `dynamics` DYNAMICS ⏻ [4], power = the band's Dynamics | Range (0, 0), Threshold (1, 0), Attack (2, 0), Release (3, 0) · Spectral (0, 3, 2 wide), Auto (2, 3, 2 wide) · Detail as a value bar (0, 4, 4 wide) · Trigger: Input \| Sidechain (0, 5, 4 wide) |
| `output` OUTPUT [2] | Output (0, 0), Scale (1, 0) · Auto Gain (0, 3, 2 wide), with the gain it applies beside Output · Delta (0, 4, 2 wide) |
| header | Sidechain (the picker, ADR 0025) · Analyzer · Processing · Resolution (while it matters) · the latency badge |

- About 1 050 px wide (19 columns), from 788.
- **On the display:** a dynamic band's node shows a second, hollow node at Gain + Range, dragged up and down to set Range, and its live position between them. Threshold shows as a line in the band's trigger level while the band is selected. A collision, hovered, shows a "Make room for Kick" button (the track's name; "Sidechain" in a plugin).
- **Dimming:** DYNAMICS while the type can't move; Detail unless Spectral; Threshold while Auto; Trigger's Sidechain while nothing is connected (it still works: it listens to the input).
- **By mouse** (ADR 0040): every control is on the panel; Alt+drag on a node listens to its band (a shortcut to Listen); Make room is a button on the collision.

### Factory presets (added)

Built and named by Claude, signed off by ear, as before. About six:
- **De-ess** (Vocals, Fix): a spectral band over 5–10 kHz, Auto, quick attack and release.
- **Tame harshness** (Fix, Bus): a broad spectral band over 2–6 kHz, low Detail.
- **Resonance smoother** (Fix, Master, Subtle): a wide spectral band, gentle Range, high Detail.
- **Boom control** (Bass, Fix): a dynamic bell at 120 Hz that cuts only when notes bloom.
- **Bass under the kick** (Bass, Ducking): a dynamic bell at 60 Hz, Trigger Sidechain: choose the kick in the header.
- **Linear-phase master** (Master, Clean): Linear phase, Medium; a subsonic cut and gentle tilt.

### Automated test targets (added)

| Metric | Target |
|---|---|
| A core state (version 1) loaded | Sounds the same: null against the version-1 render (< −120 dBFS) |
| Dynamic bands off, Zero latency | Null against the core (< −120 dBFS) |
| A dynamic bell, −6 dB Range, on a sine stepping 20 dB over its threshold | Settles 6 dB down (±0.2) at its frequency, at the set attack (±20 %); back within the release |
| Trigger Sidechain: a kick in the sidechain, a bass through | The bass dips at 60 Hz only while the kick plays |
| Linear phase, every band static | Magnitude within 0.1 dB of the zero-latency curve above 40 Hz (Medium); phase linear: the impulse response symmetric about the reported latency (±1 sample) |
| Linear phase, flat | Null against the input delayed by the reported latency (< −120 dBFS) |
| Reported latency | Exactly the delay measured, for every Processing, Resolution and Spectral setting, at 44.1, 48 and 96 kHz |
| A spectral band on a sweep with a 12 dB resonance at 3 kHz | The resonance down by at least 9 dB; frequencies an octave away within 1 dB |
| A spectral band on pink noise (nothing sticks out) | Under 1 dB of change anywhere |
| Auto gain, a +6 dB high shelf at 4 kHz | Pink noise's loudness (BS.1770) within 0.5 LU of the bypassed |
| Delta, every band static | Silence (< −120 dBFS) |
| Make room | One undo step; the band as described |
| No allocation, lock or file access in `process()` | Conformance, with every setting on |
| CPU: 24 dynamic bands, or 4 spectral bands at High | Under 10 % of a core each at 48 kHz (Release, the reference machine; there is no module budget yet, open question B74) |

### Questions for review

| # | Question | Recommendation |
|---|---|---|
| E1 | **Dynamic bands** | On any bell, shelf or tilt: Range (cut or boost, ±30 dB, −6 by default), Threshold with Auto on by default, Attack and Release per band, Trigger Input or Sidechain. A band moves from Gain toward Gain + Range as it gets loud. Keep the "below threshold" (upward) modes of some dynamic EQs for later |
| E2 | **Spectral bands: the resonance suppressor, inside the EQ** (the product owner's idea) | A Spectral switch on a dynamic bell or shelf: inside the band, each frequency on its own, Auto comparing it with the smoothed spectrum around it (the suppressors' soft mode) or Manual against the threshold (their hard mode), Detail for narrow or broad, Range as the deepest cut, attack and release scaled faster in the highs, a fixed 3 dB/oct detection tilt. What the separate suppressors do with one curve and eight bands, here per band, so several can work on different ranges. Their extras (separate tilts per control, a mix, multichannel) stay out |
| E3 | **What Spectral costs** | It needs the spectral engine, so it adds delay even in Zero latency (the other bands stay minimum phase, delayed to line up). A spectral mode with no delay, as one suppressor offers, is far harder: later |
| E4 | **Linear phase** | One more Processing choice, with Low, Medium and High resolution (about 50, 100 and 200 ms). Dynamic and spectral bands work in it. Leave out a natural-phase mode: the core is already analog-matched in magnitude, and its phase benefit is small (Later) |
| E5 | **Showing the delay** (the product owner) | Each choice names its delay; a warning-coloured badge in the header while there's any, with a sentence on what it means for live playing; a one-time toast; `get_module` reports it |
| E6 | **One engine for both** | Linear phase and spectral bands share one STFT engine and one Resolution, with left/right and mid/side in the same frame, so mixing placements never doubles the delay |
| E7 | **Masking: the sidechain, and Make room** | The header's Sidechain is both the dynamic bands' trigger and the masking display's compared track: its curve and collisions, the Spectrum's rule. A collision offers Make room: a dynamic band that ducks this track there only when the other plays. Masking against every track at once waits for a mixer-wide view (Later) |
| E8 | **Auto gain** | Off by default (old projects mustn't change level). An estimate from the static curve on pink noise, K-weighted: steady, no pumping, right offline. The gain it applies shows beside Output |
| E9 | **Listen and Delta** | Listen plays the selected band's range (a toggle on the display, and Alt+drag on a node); Delta plays what the dynamic and spectral bands change. Neither is kept |
| E10 | **A device API addition: transient parameters** | `ParamFlags::Transient`: not saved, not in presets, no undo step, never automatable. For Listen and Delta here; the Compressor's Listen could use it later. Allowed by ADR 0034 (a new flag), listed in architecture.md |
| E11 | **The EQ gets a sidechain bus** | A stereo sidechain input, off unless chosen (as the Compressor's). Saved projects and plugin sessions load with it unconnected; the validators and a version-1 project test check it |
| E12 | **EQ match** | Later, as the product owner decided (2026-10-13) |
| E13 | **The size** | 216 per-band parameters and 5 global ones are added (392 in all). Hosts handle it; the module reference lists them by band |
| E14 | **Building it in two steps** | 2a: dynamic bands, the sidechain, the masking display and Make room, auto gain, Listen and Delta (no delay). 2b: the spectral engine, then linear phase and spectral bands on it. Each step built, tested and signed off on its own |


### Settled while building (step 2a, 2026-10-13)

- **Built:** dynamic bands (Range, Threshold and Auto, Attack, Release, Trigger), the sidechain, the masking display and Make room, Auto Gain, Listen and Delta. Spectral bands, Detail, Processing and Resolution come with step 2b (their parameters then, at state version 3).
- **Auto Threshold** averages the trigger's level in dB while it sounds (silence holds it), starting from the first level heard. Averaging its power let one loud passage raise the threshold within a fraction of a second.
- **Dynamics move on the 32-sample control grid,** the coefficients interpolated between ticks as before: an attack under about 0.7 ms acts as about 0.7 ms.
- **Listen and Delta** are transient (`ParamFlags::Transient`, E10): never saved, in presets or undo steps, and back to Off whenever a state is applied. The conformance suite checks every transient setting.
- **Layout in step 2a:** DYNAMICS has Range, Thresh, Attack, Release, then Auto, the Trigger (Input or Sidechain) and a trigger meter: the band's trigger level on a −60 to 0 dB bar with its threshold marked, to set it by eye. OUTPUT [2] has Output and Scale, Auto Gain with the correction it applies under it, and Delta. Step 2b adds Spectral beside Auto and Detail.
- **The knob's name is "Thresh",** as the Compressor's and Gate's: "Threshold" is wider than a knob.
- **Make room's threshold** comes from the compared track's spectrum there: its bands' power summed, read as a peak, 9 dB under (6 dB under its RMS). Set it by ear and the trigger meter after.
- **One undo step for a panel's action:** `ParamBinding::beginEdit(name)` starts a named step in the DAW ("Make room", "Add band"); plugin hosts group their own undo.
- **Found and fixed on the way:** undo and redo of any module's settings in the DAW changed the stored values but not what the module played or showed (tracktion leaves parameters where they are when their stored values change). Modules now follow their stored values after undo and redo.
- **Assistants:** the EQ's Auto Gain correction is a meter (`measure_module`); every new setting is in `set_parameters`.
- **Presets:** Boom control and Bass under the kick added; every EQ preset remade at state version 2 (its sound unchanged).

### Settled while building (step 2b, 2026-10-13)

- **Built:** the spectral engine (`dsp::SpectralFilter`), linear phase with Low, Medium and High resolution, spectral bands with Detail (`dsp::SpectralDynamics`), and the delay shown wherever it's chosen. Parameters at state version 3: `band<n>.spectral`, `band<n>.detail`, `phase`, `resolution` (392 in all).
- **The engine:** frames of N/4 samples (a Hann window, half overlapping), each padded to N and convolved exactly with a linear-phase kernel 3N/4 long made from the frame's response, then overlap-added: an exact filter while the response holds, a crossfade over a frame when it moves. Rather than the 75 % weighted overlap-add the design named: the same smoothness for half the frames, and no window error when nothing moves. The delay is 5N/8:

  | Resolution | N at 44.1 and 48 kHz | Delay at 48 kHz | at 44.1 kHz |
  |---|---|---|---|
  | Low | 4 096 | 53 ms | 58 ms |
  | Medium | 8 192 | 107 ms | 116 ms |
  | High | 16 384 | 213 ms | 232 ms |

  N doubles at 88.2 and 96 kHz and again above 140 kHz (the same delay in time), and halves below 35 kHz. Both channels go through one complex FFT; left/right and mid/side bands meet as a 2×2 gain matrix per frequency, in band order, so mixing placements never doubles the delay. A frame whose response is flat skips its transforms (the sound is only delayed, exactly).
- **When the engine runs:** Linear phase, or any band's Spectral switch on, even on a band that's off: the delay then depends only on Processing, Resolution and Spectral (ADR 0016, 0020). Zero latency with a spectral band: the other bands stay minimum phase as before, and the engine applies only the spectral bands' moves after them; a spectral band keeps its Gain there too.
- **Spectral detection, tuned on pink noise and resonances:** the trigger's spectrum (the input or the sidechain, in the band's placement: the louder of left and right for Stereo) is smoothed a forty-eighth of an octave either side and over 40 ms (faster in the highs, slower in the lows, within 2×), and tilted 3 dB/oct. Detail sets the surroundings: 2 octaves either side at 0, 0.4 at 50 %, a twelfth at 100 %; their peaks are clipped three times so a resonance doesn't raise its own baseline. A frequency sticking out by more than noise does by chance (2.5 dB) starts to come down, and all the way down to its surroundings once it's sure to be a peak (4 dB more), up to Range. A fixed 2 dB allowance, as first written, left too much of a resonance or cut noise. The gains are smoothed a ninety-sixth of an octave either side. Measured: a 12 dB resonance (Q 8) over pink noise comes down 10.7 dB at Detail 50 %, frequencies an octave away move under 0.1 dB, and pink noise alone moves under 0.3 dB anywhere.
- **The resonance target is measured on pink noise through the resonance,** not a sweep: a sweep is one frequency at a time, so all of it sticks out.
- **Dynamic bands in linear phase** move frame by frame (every 21 ms at Medium), their detection as at zero latency.
- **Listen and Delta are delayed with the sound,** their fades too, so switching them lines up with what you hear.
- **The delay, shown:** the Processing and Resolution menus name each choice's delay at the rate the EQ runs at, to 5 ms ("Linear phase: about 105 ms"; Zero latency names the spectral bands' delay while there are any); in the header they're captioned **Mode** and **Quality** (their short names; the full names stay in tooltips, automation and the assistants' reference: the product owner, 2026-10-13, after trying it), and Quality shows only while there's a delay (it matters only then); the badge ("+107 ms") sits at the header's right end with the sentence in its tooltip; the Spectral switch's tooltip names its delay. The badge is a shared part of the module header (`DevicePanel::showLatency`) for any module whose delay is a choice.
- **The toast** comes once per project for any module that delays its track by 20 ms or more (a lookahead of a few ms isn't worth a word): "EQ on Vocals: 107 ms of delay. The tracks stay in time; what you play live on Vocals is heard 107 ms late." Saved with the project, no undo step. Assistants read `latencyMs` in `get_module`.
- **Layout:** DYNAMICS has Spectral and Auto side by side; a spectral band shows Detail in the row the trigger meter takes for any other band (each means nothing to the other: a spectral band's threshold is per frequency, not the band's level). Spectral and Detail dim on types that can't be spectral (Tilt).
- **Make room stays a dynamic band, never a spectral one:** a spectral band would add delay without being asked (E5). Switch it to Spectral by hand.
- **CPU** (Release, the reference machine, 48 kHz): a 4-band EQ 0.4 % of a core; 24 dynamic bands 6.7 %; linear phase, 8 bands, 0.8–0.9 % at any Resolution; 4 spectral bands 4.3 % (Low) to 4.9 % (High); 24 spectral bands at High in linear phase 13 %. (Measured again after the shared FFT got ten times faster, building the Wavetable Synth.)
- **Found and fixed on the way:** since step 2a every band's trigger filter was designed again on every control tick, on bands that are off too (a plain 4-band EQ took 18 % of a core; now 0.4 %); Delta's still copy of a moving band was designed on every tick even with Delta off (24 dynamic bands took 28 %; now 6.8 %); and silence in the compared track counted as collisions in the highs, as the display's tilt lifted its floor (`dsp::spectrumCollisions` now judges silence untilted).
- **Presets:** De-ess, Tame harshness, Resonance smoother and Linear-phase master added; every EQ preset remade at state version 3 (its sound unchanged).
- **The conformance suite** measures delays of up to 10 000 samples now: the latency estimate correlates through FFTs.

## A/B cases (against the Live target)

Deferred until a Live machine is available.

## A/B log

–

## Sign-off

- [ ] Automated: unit, conformance and golden tests; validators
- [ ] Snapshots reviewed (100% / 200%)
- [ ] Sound signed off by the product owner (date):
- [ ] Feel signed off by the product owner (date):
