# Chorus: device spec

- **Status:** Approved by the product owner (2026-10-12, with every recommendation of C1–C11); IDs are permanent. Building (B2)
- **Type:** Audio effect
- **Tier / milestone:** Tier 1, M3 (B2)
- **Live target:** Chorus-Ensemble. References only; nothing is copied ([ADR 0011](../../docs/decisions/0011-calibrated-ambition-and-consolidation.md)).

## Purpose

Movement and width from copies of the sound that drift in time. There are three kinds:
- a classic two-voice chorus that thickens a synth, a guitar or a voice;
- a three-voice ensemble for the lush, shimmering sound of the 1970s string machines;
- a vibrato that bends the pitch on its own.

One module with three modes, as Live's Chorus-Ensemble has. It's built on the modulation core made for the time effects (M3 A2), as the Delay is. It's named Chorus in the browser and in hosts. Its description names ensemble and vibrato, so a search for either finds it (C11).

## Beyond Live

Small things, all from the modulation core:
- **Sync:** the movement can follow the song's tempo and stay locked to its position, as the Delay's modulation does.
- **Shape and Offset in every mode.** Live has them in Vibrato only. Offset at 0° gives a mono chorus that sums safely to mono.
- **The voices drawn:** the display draws each voice's delay over two cycles, computed by the functions the audio path uses. It reads out the pitch swing in cents, so you know how much a setting bends before you hear it. Drag in it to set Rate and Depth.

## Stages

| Stage | Scope | Milestone |
|---|---|---|
| Core | Everything below | M3 |
| Later | More voices (an eight-voice ensemble); an envelope follower on Depth; the noise and clock whine of old analog choruses | With demand |

## Parameters

| ID (permanent) | Name | Description | Range | Default | Unit | Curve / skew | Smoothing | Automatable |
|---|---|---|---|---|---|---|---|---|
| `mode` | Mode | The kind of effect: Chorus thickens with two voices, Ensemble is lusher with three, and Vibrato bends the pitch alone. | Chorus, Ensemble, Vibrato | Chorus | choice | – | 50 ms crossfade | Yes |
| `rate` | Rate | How fast the voices move, in cycles per second. | 0.01 … 20 | 0.8 | Hz | centre 1 Hz | 20 ms multiplicative | Yes |
| `sync` | Sync | Moves the voices in time with the song: the rate becomes a note value. | Off / On | Off | toggle | – | the voices' glide (DSP design) | Yes |
| `div` | Sync Rate | How fast the voices move as a note value, one cycle per note, while Sync is on. | 8 bars, 4 bars, 2 bars, 1 bar, 1/2D, 1/2, 1/4D, 1/2T, 1/4, 1/8D, 1/4T, 1/8, 1/16D, 1/8T, 1/16, 1/16T, 1/32 | 1/2 | choice | – | the voices' glide | Yes |
| `depth` | Depth | How far the voices' delay moves: more depth, more pitch movement and a deeper effect. | 0 … 100 | 50 | % | linear (the sweep grows with its square) | 20 ms | Yes |
| `shape` | Shape | The movement's wave, from a smooth sine at 0 % to a triangle at 100 %, which swaps between two steady pitches. | 0 … 100 | 0 | % | linear | 20 ms | Yes |
| `offset` | Offset | How far apart the two sides move: together at 0°, and in opposite directions at 180°, the widest. | 0 … 180 | 180 | ° | linear | 50 ms glide | Yes |
| `delay` | Delay | How far the voices trail the dry sound at their closest; Depth sweeps them later from there. | 1 … 30 | 6 | ms | centre 8 ms | 50 ms glide (repitches) | Yes |
| `width` | Width | The stereo width of the voices, from mono at 0 % to extra wide at 200 %. | 0 … 200 | 100 | % | linear | 20 ms | Yes |
| `warmth` | Warmth | Darkens and gently saturates the voices, like an old analog chorus, more so on each pass of the feedback. | 0 … 100 | 0 | % | linear | 20 ms | Yes |
| `feedback` | Feedback | How much of the voices goes back through them, for a resonant, metallic edge. | 0 … 95 | 0 | % | linear | 20 ms | Yes |
| `invert` | Invert | Flips the feedback's polarity, for a hollower sound. | Off / On | Off | toggle | – | 20 ms crossfade | Yes |
| `lowcut` | Low Cut | Keeps the lows out of the effect, so they stay solid and centred. | Off / On | On | toggle | – | 20 ms crossfade | Yes |
| `lowcut.freq` | Low Cut Freq | Below this frequency, the sound passes through untouched. | 20 … 2 000 | 120 | Hz | centre 200 Hz | 20 ms multiplicative | Yes |
| `mix` | Dry/Wet | Blends the original sound with the voices; Vibrato is always all voices. | 0 … 100 | 50 | % | linear | 20 ms | Yes |
| `output` | Output | The level after the chorus. | −24 … +12 | 0 | dB | linear | 20 ms, linear in amplitude | Yes |

No non-parameter state. State version 1.

**Ranges and limits:**
- The note values run from the slowest to the fastest, so turning the knob always speeds the movement up. "Bars" follow the song's meter, as the Delay's do.
- A synced cycle longer than 100 s, at a slow tempo, is held at 100 s.
- **Depth's sweep:**
  - In Chorus and Ensemble, up to 8 ms beyond Delay. In Vibrato, up to 16 ms beyond the shortest delay the line has.
  - The sweep grows with the square of Depth, so the low half is fine-grained.
  - The pitch it bends grows with Rate: faster is more dramatic.
  - These numbers are starting points, tuned by ear while building.
- Feedback stops at 95 %, so the tail always ends.

## DSP design

```
in ─> Low Cut split ─┬─ lows ─────────────────────────────────────────────────────────────────────────────┐
                     │                                                                                    │
                     └─ highs ─┬─> + ─> line L/R (ModulatedDelay) ─> voices ─> tone (Warmth) ─┬─> Width ─> wet ─┤
                               │   ^                                                      │                     │
                               │   └──── × feedback (− if Invert) <── the voices' average ┘                     │
                               └─> dry ─────────────────────────────────────────────────────── mix (equal power) ─> + lows ─> Output ─> out
```

**Lines and voices:**
- **The lines:** each side has a modulation-core `ModulatedDelay` (M3 A2), up to 60 ms. A side's voices are read taps on that side's line, so a stereo input keeps its image.
- **Chorus:** one voice per side. The right side's movement leads the left's by Offset (180°: one rises as the other falls).
- **Ensemble:** three voices per side, a third of a cycle apart, so their sum is smoother and richer than any one of them. The right side's three lead the left's by Offset.
  - On top of the slow movement is a quicker shimmer, seven times as fast and a fifth as deep, as the string machines had.
  - The three voices are summed at 1/√3, so the wet sits at about the level of one voice.
- **Vibrato:** one voice per side, and the output is the voices only. Delay, Width, Feedback, Invert and Dry/Wet don't apply.

**The movement:**
- **The LFO:** one `ModulationLfo`, free in Hz or synced to one cycle per note value, locked to the song's position while it plays.
- **Each voice's position** is the LFO's position, plus its third (Ensemble) and the side's Offset.
- **Each voice's value** is `ModulationLfo::value` at that position, the sine and the triangle blended by Shape.
- **Each voice's delay:**
  - Chorus and Ensemble: Delay + sweep × (1 + value) / 2.
  - Vibrato: the line's shortest delay + sweep × (1 + value) / 2.
  - Computed by a pure function, `Chorus::voiceDelays`, which the display calls too. `Chorus::pitchSwing` gives the display's readout.
- **The voices' glide:** the delay each voice reads glides (a one-pole of about 2 ms), so a jump in the LFO's phase doesn't click. A jump comes from switching Sync, a jump in the song, or a sudden Offset.

**Feedback and tone:**
- **Feedback** (Chorus and Ensemble only): what feeds back is the average of a side's voices, after the tone stage, times Feedback (negated by Invert). Averaging means the loop's gain never passes Feedback, however the voices line up.
- **Tone (Warmth):** the core's `FeedbackPath` on each side's voices, so it shapes both the wet and what feeds back.
  - Warmth turns its high cut from 20 kHz down to 4 kHz and its saturation from 0 to 1, on a curve tuned by ear.
  - At 0 the stage is out of the path; it fades in over the first few percent.
  - Its soft clip bounds the loop at any feedback.

**Low Cut, width and mix:**
- **Low Cut:** the input is split by a 12 dB/oct high-pass at Low Cut Freq.
  - The part above feeds the effect, dry and wet.
  - The part below (the input less the high-passed part) goes straight to the output. It isn't scaled by Dry/Wet, so the lows stay at their level in every mode and at every mix.
  - The two parts sum back to the input exactly.
  - Off: the whole input feeds the effect.
- **Width:** mid-side on the wet: the side is scaled by Width (0 %: mono; 200 %: the sides twice as loud as the middle).
- **Mix:** equal power (cosine and sine). At 50 % the result is about as loud as the dry sound, so switching the module off doesn't drop the level. At 0 % it's the input exactly. Vibrato is always all wet.
- **Mode changes:** the old mode and the new run side by side for 50 ms and crossfade.

**Timing and housekeeping:**
- **Latency:** none. A vibrato always trails its input by its delay; that isn't reported as latency.
- **Tail:** the longest delay; with feedback, the time it takes to fall 60 dB as well, at most 10 s.
- Parameters are read on control ticks (every 32 samples) and smoothed per sample, as in the other modules.
- **Analysis slots:**
  - 0/1 = input peak L/R; 2/3 = output peak L/R;
  - 4 = the LFO's position (cycles), for the display's marker;
  - 5 = the rate the device follows (Hz), so the display draws a synced rate.

## UI layout

On the module grid ([device-panels.md](../../docs/design/device-panels.md)), sections run left to right. Each is written `id` TITLE [columns], with controls at (column, row) and spans; knobs are 1 × 3 cells.

| Section | Controls |
|---|---|
| `modulation` MODULATION [6] | Voices display (0, 0, 4 × 3) · Mode, stacked [Chorus / Ensemble / Vibrato] (4, 0, 2 × 3) · Rate (0, 3) · Depth (1, 3) · Shape (2, 3) · Offset (3, 3) · Sync toggle (4, 3, 2 wide) |
| `voices` VOICES [3] | Delay (0, 0) · Width (1, 0) · Warmth (2, 0) · Feedback bar (0, 3, 2 wide) · Invert toggle (2, 3) · Low Cut toggle (0, 4, 2 wide) · Low Cut Freq bar, named Freq (0, 5, 2 wide) |
| `output` OUTPUT [1] | Dry/Wet (0, 0) · Output (0, 3) |
| meter strip | In and out |

**Voices display:**
- It shows two cycles of the movement. Each voice's delay is a line, from the dry sound at the bottom up to the longest delay the settings reach: the left side's voices in the accent colour, the right side's in the secondary.
- A marker moves along with the LFO's position.
- Its label line reads the pitch swing ("±12 cents") and the delay range ("6–10 ms"), from `Chorus::pitchSwing` and `Chorus::voiceDelays`.
- **Dragging:** left or right changes Rate (stepping through the note values while Sync is on); up or down changes Depth. Shift drags finely.

**Controls:**
- **Rate follows Sync:** the Rate knob turns `rate` in Hz, or `div` in note values while Sync is on ("1/2"), through `ParamKnob::follow` (device-panels.md).
- **Feedback and Low Cut Freq are value bars:** they matter less often than the knobs, and a bar fits beside its switch. "Feedback" is also too long for a knob's name (46 px). Feedback reads whole percent, so its name and value fit the bar.
- **Dimming:**
  - In Vibrato: Delay, Width, the Feedback bar, Invert and Dry/Wet.
  - While Low Cut is off: the Freq bar.
- About 620 px wide (10 columns and the meter strip).

## Factory presets

Built and named by Claude after approval ([presets.md](../../docs/design/presets.md) §13); signed off by the product owner by ear. Planned (14):

- **Classic chorus** (Keys, Wide): Two voices on a triangle, the synth chorus of the early 80s.
- **Subtle width** (Subtle, Wide): Slow and shallow: a little width and life for anything.
- **Deep chorus** (Pad, Wide): Slow, deep and wide, for pads.
- **Guitar chorus** (Guitar): A clear, shimmering chorus for clean guitar.
- **Bass chorus** (Bass): Chorus above 250 Hz only, so the low end stays solid and centred.
- **Vocal thickener** (Vocals, Subtle): Two close voices that thicken without wobbling.
- **Mono-safe chorus** (Subtle, Clean): Both sides move together, so it sums to mono without losing anything.
- **String ensemble** (Pad, Keys): Three voices with the quick shimmer, for string machines and pads.
- **Ensemble wash** (Pad, Wide, Heavy): Deep ensemble with some feedback: a wide, swirling wash.
- **Warm ensemble** (Warm, Keys): Ensemble with Warmth, darker and softer, for electric pianos.
- **Lead vibrato** (Lead): A natural vibrato for a synth lead, both sides together.
- **Wide vibrato** (Wide, Lead): The sides bending in opposite directions: vibrato that also spreads.
- **Siren** (FX): A deep triangle vibrato that swaps between two pitches.
- **Metallic** (FX, Heavy): Short voices with inverted feedback: a hollow, ringing edge.

## Automated test targets

| Metric | Target |
|---|---|
| Delay, Depth 0 | An impulse arrives Delay after the input, to the sample, in Chorus and Ensemble (Mix 100 %, Low Cut off) |
| Voice delays | With an impulse train at Depth 100 % and Rate 0.5 Hz, each voice's echo arrives where `Chorus::voiceDelays` says, within 1 sample, in every mode and at Shape 0, 50 and 100 % |
| Pitch swing | A 1 kHz sine through Vibrato: its measured pitch follows `Chorus::pitchSwing` within 1 cent |
| Offset 0°, mono input | Left and right identical (bit-exact), in every mode |
| Width 0 % | The wet has no side (L − R of the wet < −120 dBFS) |
| Low Cut | A 40 Hz sine with Low Cut at 200 Hz comes out within 0.5 dB at any Mix, with no sidebands above −60 dBFS; a 2 kHz sine is chorused |
| Mix 0 % | Null against the input (< −120 dBFS), Low Cut on or off |
| Level at Mix 50 % | Pink noise comes out within ±1 dB of its input level, in Chorus and Ensemble at Depth 50 % |
| Feedback 95 % with Warmth 100 % | A 0 dBFS input: the output stays within +6 dBFS and doesn't grow, Invert on or off |
| Invert | The impulse response's second pass through the line flips sign |
| Sync | A synced LFO is at the same position after a jump in the song as when played through to it |
| Mode, Sync and Offset changes | No clicks (the conformance click score) |
| Display vs audio | The drawn delay curves within 1 sample of the rendered voices; the readout within 1 cent of the measured swing |
| Conformance suite | All pass (no allocation, no denormals, parameter jumps without clicks, the tail as reported) |

## A/B cases (against the Live target)

Deferred until a Live machine is available.

## A/B log

–

## Questions for review

| # | Question | Recommendation |
|---|---|---|
| C1 | **The modes** | Chorus, Ensemble and Vibrato, Live's three, in one module. Chorus is one voice per side. Live's Chorus also has a one-or-two taps switch; leave it out: Ensemble is the richer sound, and one switch fewer |
| C2 | **What Depth means** | A sweep in time (up to 8 ms; 16 ms in Vibrato), growing with the square of Depth. So the pitch it bends grows with Rate, as on hardware and in Live: faster is more dramatic. The display reads the swing in cents, so nobody has to work it out |
| C3 | **Delay** | One knob: the voices' shortest delay, with Depth sweeping later from there, so the sweep never runs into the line's start. Live has an Auto or fixed delay time instead. 1–30 ms, 6 ms by default |
| C4 | **Ensemble's shimmer** | Yes: a quicker movement on top of the slow one (seven times Rate, a fifth as deep), the string machines' sound. Tuned by ear while building |
| C5 | **Shape and Offset in every mode** | Yes (Live: Vibrato only). Offset 180° by default, the classic wide chorus. 0° is a mono chorus that sums safely (the Mono-safe preset), and the plain vibrato in the vibrato presets |
| C6 | **Sync** | Yes: one cycle per note value, locked to the song, as the Delay's modulation. 8 bars to 1/32, slowest first, 1/2 by default. Live's Chorus-Ensemble has no sync; the core makes it cheap |
| C7 | **Feedback** | 0–95 % and an Invert switch, as Live. Chorus and Ensemble only. It stops at 95 % so the tail always ends. Warmth sits in the loop, so the repeats darken as in the old analog chips |
| C8 | **Low Cut** | On by default at 120 Hz. The lows bypass the effect entirely, not mixed by Dry/Wet, so they stay at their level and centred in every mode. Off gives the full-range classic sound |
| C9 | **Mix law** | Equal power, so 50 % (the default, the classic chorus) is about as loud as the dry sound and switching the module off doesn't drop the level. The Delay uses equal gain, because its echoes are separate events; a chorus blends with what it plays against |
| C10 | **Left out for now** | Live's taps switch (C1); more voices; an envelope follower; analog noise and clock whine (noise would break "silence in, silence out", as in the Delay) |
| C11 | **Name and IDs** | Named **Chorus** in the browser, the chain and hosts ("Segno Chorus"). It's shorter than the working name "Chorus & Ensemble", and its modes and description name the rest. Device `chorus`, plugin code `Fchr`, CLAP `org.segnoaudio.chorus`; section IDs as above |

## Sign-off

- [ ] Automated: unit, conformance and golden tests; validators (pluginval at strictness 10 and clap-validator)
- [ ] Layout lint passes (`fd_panel_tests`)
- [ ] Snapshots reviewed (100% / 200%; expanded, collapsed, folded)
- [ ] Sound signed off by the product owner, with the factory presets
- [ ] Feel signed off by the product owner
