# EQ: device spec

- **Status:** Approved by the product owner (2026-10-10, with the M1 sign-off); IDs are permanent
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
- **Stage 2 (M3):** dynamic bands with sidechain, linear-phase mode, EQ match, and a cross-track masking display.

## Stages

| Stage | Scope | Milestone |
|---|---|---|
| Core | 24 bands, all types and slopes, stereo placement, pre/post spectrum, output gain, gain scale, compact view | M1 |
| Stage 2 | Dynamic bands + sidechain, linear-phase / natural-phase modes, EQ match, masking display, auto-gain | M3 |

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

## A/B cases (against the Live target)

Deferred until a Live machine is available.

## A/B log

–

## Sign-off

- [ ] Automated: unit, conformance and golden tests; validators
- [ ] Snapshots reviewed (100% / 200%)
- [ ] Sound signed off by the product owner (date):
- [ ] Feel signed off by the product owner (date):
