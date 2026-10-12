# Reverb: device spec

- **Status:** Approved by the product owner (2026-10-12, with every recommendation of R1–R12); IDs are permanent. Building (B5, core)
- **Type:** Audio effect
- **Tier / milestone:** Tier 1. Core in M3 (B5); stage 2 in M6
- **Live target:** Reverb, and Hybrid Reverb's algorithmic engine (its convolution engine is stage 2). References only; nothing is copied ([ADR 0011](../../docs/decisions/0011-calibrated-ambition-and-consolidation.md)). Benchmarked against the best algorithmic reverbs ([ADR 0010](../../docs/decisions/0010-live-is-the-floor-not-the-ceiling.md)).

## Purpose

Space, from a small room around a drum kit to a hall, a bright plate on a vocal, or an endless wash under a pad. One reverb, in four modes (Room, Hall, Plate, Space): Decay and Size set apart, the decay shaped by band (lows longer, highs shorter, or the other way), early reflections, movement in the tail, Freeze, a filter on what goes in, and ducking so the tail stays out of the singer's way. In M6 the same module gains a convolution engine and a blend of the two (ADR 0011: one reverb, in stages).

## Beyond Live

- **The decay drawn by frequency,** from the same filters the tail decays through: how long each part of the spectrum rings, with handles to drag the low and high bands and the crossovers between them.
- **Ducking,** as the Delay's: the tail dips while you play and swells in the gaps.
- **Decay by band** in three bands with free crossovers. Live's Reverb has two shelves; Hybrid Reverb's bass multiplier is one mode's.

## Stages

| Stage | Scope | Milestone |
|---|---|---|
| Core | Everything below | M3 |
| 2 | A convolution engine with an impulse-response library and import, blended with the algorithmic one (serial or parallel); tempo-synced predelay | M6 |
| Later | Shimmer (pitch-shifted feedback); gated and reverse modes; Freeze that lets new sound in; an EQ after the reverb | With demand |

## Parameters

| ID (permanent) | Name | Description | Range | Default | Unit | Curve / skew | Smoothing | Automatable |
|---|---|---|---|---|---|---|---|---|
| `mode` | Mode | The kind of space: Room is close and quick, Hall big and slow to build, Plate dense and bright, Space vast and moving. | Room, Hall, Plate, Space | Hall | choice | – | the wet dips out and back (20 ms each) | Yes |
| `decay` | Decay | How long the reverb rings in the middle frequencies before falling 60 dB. | 0.1 … 60 | 2.5 | s | centre 2 s | 50 ms multiplicative | Yes |
| `size` | Size | How big the space is: small is close and quick to build, big is distant and spacious. | 0 … 100 | 50 | % | linear | 100 ms glide (repitches) | Yes |
| `predelay` | Predelay | How long before the reverb starts, after the sound. | 0 … 500 | 15 | ms | centre 50 ms | 100 ms glide (repitches) | Yes |
| `diffusion` | Diffusion | How quickly the echoes blur into a smooth wash: low is grainy and echoey, high is smooth at once. | 0 … 100 | 80 | % | linear | 20 ms | Yes |
| `early` | Early | How loud the first reflections are against the tail: more is closer and clearer. | 0 … 100 | 50 | % | linear | 20 ms | Yes |
| `motion` | Motion | How much the tail moves and shimmers: none is still and exact, more is lusher and less metallic. | 0 … 100 | 25 | % | linear | 20 ms | Yes |
| `width` | Width | The stereo width of the reverb, from mono at 0 % to extra wide at 200 %. | 0 … 200 | 100 | % | linear | 20 ms | Yes |
| `freeze` | Freeze | Holds the reverb as it is, ringing forever, and lets nothing new in. | Off / On | Off | toggle | – | 50 ms crossfade | Yes |
| `low.mult` | Low Decay | How long the lows ring, as a share of Decay: over 100 % they ring longer, under it they thin out sooner. | 25 … 400 | 120 | % | centre 100 % | 50 ms | Yes |
| `low.freq` | Low Freq | Below this frequency, the reverb rings for Low Decay. | 50 … 1 000 | 250 | Hz | centre 250 Hz | 50 ms multiplicative | Yes |
| `high.mult` | High Decay | How long the highs ring, as a share of Decay: under 100 % the tail darkens as it fades, like a real room. | 10 … 200 | 60 | % | centre 60 % | 50 ms | Yes |
| `high.freq` | High Freq | Above this frequency, the reverb rings for High Decay. | 1 000 … 16 000 | 4 000 | Hz | centre 4 kHz | 50 ms multiplicative | Yes |
| `filter` | Filter | Filters what goes into the reverb: less low rumble and hiss to ring. | Off / On | On | toggle | – | 20 ms crossfade | Yes |
| `filter.low` | Low Cut | Below this, the sound doesn't reach the reverb. | 20 … 2 000 | 100 | Hz | centre 200 Hz | 20 ms multiplicative | Yes |
| `filter.high` | High Cut | Above this, the sound doesn't reach the reverb. | 1 000 … 20 000 | 12 000 | Hz | centre 6 kHz | 20 ms multiplicative | Yes |
| `duck` | Ducking | How much the reverb dips while you play; it swells back up in the gaps. | 0 … 100 | 0 | % | linear | 20 ms | Yes |
| `duck.release` | Duck Release | How quickly the reverb comes back up once you stop. | 50 … 2 000 | 300 | ms | centre 300 ms | – | Yes |
| `mix` | Dry/Wet | Blends the original sound with the reverb; on a return track, set it to 100 %. | 0 … 100 | 30 | % | linear | 20 ms | Yes |
| `output` | Output | The level after the reverb. | −24 … +12 | 0 | dB | linear | 20 ms, linear in amplitude | Yes |

No non-parameter state. State version 1.

**Ranges and limits:**
- **Decay by band:** the lows (below Low Freq) ring for Decay × Low Decay, the highs (above High Freq) for Decay × High Decay, and the middle for Decay; each held between 0.1 s and 60 s. Low Decay over 100 % and High Decay under 100 % is a natural room.
- **Size** spans each mode's own range: Room about 3–20 m, Hall 15–60 m, Space 40–150 m, and Plate a small to a large plate. It sets the space, not the time: Decay stays what it says at any Size.
- **Early** does nothing in Plate (a plate has no early reflections); it dims there.
- Dry/Wet is equal gain, as the Delay's: at 50 % the dry sound and the reverb are each at half.

## DSP design

```
in ─┬─> Filter (Low Cut, High Cut) ─> Predelay ─┬─> early reflections (taps) ──────────────────────────┐
    │                                           └─> diffusers ─> + ─> delay network (8 lines) ─┬─> tail ─┤
    │                                                            ^                              │        │
    │                                                            └── × matrix, band decay ──────┘        │
    ├─> ducking detector (input level, release) ─────────────────────────────── × duck gain <── Width <───┤
    └─> dry ─────────────────────────────────────────────────────────────────── mix (equal gain) ─> Output ─> out
```

**The network:** a feedback delay network of 8 lines, mixed through a Householder matrix (lossless, so the decay comes only from the band filters), its lengths set by the mode and scaled by Size (mutually prime, spread over an octave). Each line is a modulation-core `ModulatedDelay`, read at a moving position for Motion. Left and right are taken from different lines, so the tail is wide and decorrelated.

**Decay by band:** each line has a three-band loss: the gains for its low, middle and high bands (first-order crossovers at Low Freq and High Freq) set so each band falls 60 dB in its own time over that line's length. A pure function, `Reverb::decayAt(freq)`, gives the decay those filters make at any frequency; the display draws it.

**Early reflections:** a tapped delay after the predelay: 16 taps a side, in a pattern of the mode's (Room dense and close, Hall sparse and spaced, Space far apart), scaled by Size, at Early's level, and fed into the network as well so the tail grows from them. Plate has none.

**Diffusion:** four all-pass diffusers in a row before the network, and one in each line, their gains set by Diffusion: how fast the echoes multiply into a wash.

**Motion:** each line's read position wobbles (the core's `ModulationLfo`, each line at its own rate and phase around the mode's), up to about 1 ms at 100 %, so the tail doesn't ring metallic. Tuned by ear.

**Freeze:** the lines go lossless (the band filters out), the input stops entering, each over 50 ms; off again, the tail decays as set.

**Changes:**
- **Size and Predelay** glide the read positions (about 100 ms), repitching as a tape's would, with no click.
- **Mode** dips the wet out over 20 ms, clears and retunes the network, and brings it back over 20 ms.

**The rest:**
- **Filter:** low and high cut, 12 dB/oct each (the core's `FeedbackPath` filters), on what enters the reverb.
- **Width:** mid-side on the wet: the side scaled by Width.
- **Ducking:** as the Delay's: the dynamics core's peak detector on the input; the wet dips by up to 30 dB at 100 %, coming back over Duck Release.
- **Mix:** equal gain, as the Delay.
- **Latency:** none. **Tail:** the longest of the bands' decays plus Predelay and the early reflections, at most 60 s; with Freeze, 60 s.
- Parameters are read on control ticks (every 32 samples) and smoothed per sample, as in the other modules.
- **Analysis slots:** 0/1 = input peak L/R; 2/3 = output peak L/R; 4 = the duck gain (dB).

## UI layout

On the module grid ([device-panels.md](../../docs/design/device-panels.md)), sections run left to right. Each is written `id` TITLE [columns], with controls at (column, row) and spans; knobs are 1 × 3 cells.

| Section | Controls |
|---|---|
| `reverb` REVERB [7] | Decay display (0, 0, 5 × 3) · Mode [Room \| Hall / Plate \| Space] (5, 0, 2 × 2) · Freeze toggle (5, 2, 2 wide) · Decay (0, 3) · Size (1, 3) · Predelay (2, 3) · Diffusion (3, 3) · Early (4, 3) · Motion (5, 3) · Width (6, 3) |
| `bands` DECAY BANDS [2] | Low Decay, named Low (0, 0) · High Decay, named High (1, 0) · Low Freq, named Freq (0, 3) · High Freq, named Freq (1, 3) |
| `filter` FILTER [2], power = Filter | Curve display (0, 0, 2 × 3) · Low Cut (0, 3) · High Cut (1, 3) |
| `ducking` DUCKING [1] | Ducking (0, 0) · Release (0, 3) |
| `output` OUTPUT [1] | Dry/Wet (0, 0) · Output (0, 3) |
| meter strip | In and out |

**The decay display:**
- How long the reverb rings at each frequency, 20 Hz to 20 kHz on the shared log-frequency axis, the time on a log scale (0.1 s to 60 s): `Reverb::decayAt`, the same filters the tail decays through. The two crossovers are marked.
- **Its line** reads the decay in the middle ("2.50 s") and, for Room, Hall and Space, the size of the space ("24 m").
- **Dragging:** up and down changes the band under the pointer (Low Decay, Decay or High Decay); sideways near a crossover moves it (Low Freq or High Freq). Shift drags finely.
- With Freeze on, the curve runs off the top: "frozen".

**The filter curve:** the Delay's (FILTER, `FeedbackPath::magnitude`), dragged the same way: the same thing looks and behaves the same.

**Dimming:** Early in Plate; the FILTER section while Filter is off (its power).

About 820 px wide (13 columns and the meter strip).

## Factory presets

Built and named by Claude after approval ([presets.md](../../docs/design/presets.md) §13); signed off by the product owner by ear. Planned (16):

- **Small room** (Drums, Subtle): A tight, natural room around a kit.
- **Drum room** (Drums): A bigger, livelier room for drums that need air.
- **Vocal plate** (Vocals): The smooth, bright plate on a lead voice.
- **Snare plate** (Drums, Bright): A short, bright plate that makes a snare crack.
- **Ducked vocal hall** (Vocals): A hall that waits for the singer to stop.
- **Concert hall** (Keys, Wide): A large, natural hall.
- **Dark hall** (Warm, Pad): A long hall whose highs fade fast, for pads and strings.
- **Big hall** (Wide): Long and spacious, with a slow build.
- **Short ambience** (Subtle, Bus): Barely there: space without a tail you hear.
- **Guitar room** (Guitar): A medium room that sits a guitar in a space.
- **Bright space** (FX, Wide): A vast, airy space for effects.
- **Ambient wash** (Pad, Wide): An endless, moving tail under pads.
- **Frozen pad** (Pad, FX): Freeze on: a held chord that rings forever.
- **Grainy echoes** (FX): Low diffusion: the echoes heard one by one.
- **Bass-safe hall** (Bass, Bus): The lows decay quickly and are cut going in, so a mix stays clear.
- **Long plate** (Keys, Bright): A plate that rings on, for keys and leads.

## Automated test targets

| Metric | Target |
|---|---|
| Decay by band | An impulse's decay (Schroeder integration) in octave bands at 125 Hz, 1 kHz and 8 kHz rings for `Reverb::decayAt` within 10 %, in every mode, at Decay 1, 2.5 and 6 s, Low Decay 50 and 200 %, High Decay 40 and 150 % |
| Decay is the setting | The middle band's decay matches Decay within 10 % at Size 0, 50 and 100 % |
| Predelay | Nothing before Predelay (under −120 dBFS); the first early reflection at Predelay to the sample (Room, Hall, Space) |
| Echo density | Past each mode's build time (Room 30 ms, Plate 10 ms, Hall 80 ms, Space 120 ms after Predelay), the impulse response is dense: its normalised echo density at least 0.9 |
| Freeze | A held tail keeps its level within 0.5 dB over 30 s, and takes nothing new in |
| Stereo | Width 100 %: the tails' correlation under 0.3 on a mono input; Width 0 %: a mono wet |
| Ducking 100 % | The wet at least 25 dB down while the input plays at −10 dBFS; back within Duck Release |
| Bounded | A 0 dBFS burst: the output stays under +6 dBFS at any Decay, Size, Mode and Freeze |
| Mix 0 % | Null against the input (under −120 dBFS) |
| Size, Predelay and Mode changes | No clicks (the conformance click score) |
| Display vs audio | The display draws `Reverb::decayAt`; the measured decays match it (above) |
| Conformance suite | All pass (no allocation, no denormals, parameter jumps without clicks, the tail as reported) |

## A/B cases (against the Live target)

Deferred until a Live machine is available.

## A/B log

–

## Questions for review

| # | Question | Recommendation |
|---|---|---|
| R1 | **One reverb, in stages** | Algorithmic now (Live's Reverb and Hybrid Reverb's algorithmic engine, in one), convolution and the blend in M6, in the same module (ADR 0011). One network, tuned per mode, not four different algorithms |
| R2 | **The modes** | Room, Hall and Plate, as the plan says, and Space: a vast, moving space for pads and ambient sounds (Hybrid Reverb's Dark Hall and Prism territory). Four segments, two lines of two |
| R3 | **Decay by band** | Three bands (low, middle, high) with crossovers you set, the outer two as a share of Decay (Low 25–400 %, High 10–200 %): Live's two shelves and Hybrid Reverb's bass multiplier in one, drawn and dragged in the display |
| R4 | **Size and Decay apart** | Size sets the space, Decay the time, as Live and Hybrid Reverb; a change of Size glides (repitches), so no Smooth chooser |
| R5 | **Early reflections** | One level (Early) and Diffusion (how fast the echoes blur): Live's Reflect, Shape and Spin, simplified. Plate has none, so Early dims there |
| R6 | **Motion** | One knob for the tail's movement, its rate and depth tuned per mode, as the best reverbs' "character" knobs; Live's Chorus has an amount and a rate |
| R7 | **Freeze** | Holds the tail forever with nothing new in and no damping (Live's Freeze with Flat and Cut). Letting new sound into the frozen tail (Hybrid's Freeze In) later |
| R8 | **Filter going in** | Low Cut and High Cut with a power switch, the Delay's controls and curve, on by default (100 Hz to 12 kHz): less low rumble and hiss to ring |
| R9 | **Ducking** | Yes, the Delay's (Ducking and Duck Release), off by default: a vocal reverb that waits for the singer |
| R10 | **Predelay and the mix** | Predelay 0–500 ms (15 ms by default); in note values with stage 2. Dry/Wet equal gain as the Delay, 30 % by default; "100 % on a return track" in its description |
| R11 | **Left out for now** | Shimmer, gated and reverse modes, Freeze that lets sound in (Later); Live's density setting (one quality, the best); Hybrid Reverb's vintage degradation and its EQ (the EQ module after it does that) |
| R12 | **Name and IDs** | Named **Reverb**. Device `reverb`, plugin code `Frvb`, CLAP `org.segnoaudio.reverb`; section IDs as above (`reverb`, `bands`, `filter`, `ducking`, `output`) |

## Sign-off

- [ ] Automated: unit, conformance and golden tests; validators (pluginval at strictness 10 and clap-validator)
- [ ] Layout lint passes (`fd_panel_tests`)
- [ ] Snapshots reviewed (100% / 200%; expanded, collapsed, folded; every factory preset)
- [ ] Sound signed off by the product owner, with the factory presets
- [ ] Feel signed off by the product owner
