# Filter: device spec

- **Status:** Approved by the product owner (2026-10-12, with every recommendation of F1–F13); IDs are permanent. Built (B6, 2026-10-12): what building settled is under "Settled while building". Awaiting sign-off by ear and feel
- **Type:** Audio effect
- **Tier / milestone:** Tier 1, M3 (B6)
- **Live target:** Auto Filter (as redesigned in Live 12.2). References only; nothing is copied ([ADR 0011](../../docs/decisions/0011-calibrated-ambition-and-consolidation.md)). Checked against the best creative filters for what's worth having, not for scope.

## Purpose

One strong filter that moves and has a sound of its own: a sweep that opens a build-up, a wobble locked to the song, a wah that follows how hard you play, a pad that closes on every kick, a squelchy acid line, a telephone voice. Ten kinds of filter, four circuits from clean to gritty, Drive, an LFO and an envelope follower that can listen to another track.

**Not an EQ** (F1): the EQ fixes and balances the tone, with many clean, exact bands that mostly stay put. The Filter is one filter (two poles or four) whose movement and character are the point. It has no shelves, bells or bands, and the EQ gets no resonant circuits or LFOs.

## Stages

| Stage | Scope | Milestone |
|---|---|---|
| Core | Everything below | M3 |
| Later | What F4, F9 and F10 leave out of Live's Auto Filter (6 and 48 dB slopes on Morph; the LFO's Morph, Steps and S&H quantize; the envelope's Hold and S&H; a sidechain mix, gain, filter and listen); a second filter in series or parallel, as the best creative filters have; drawn LFO shapes, with M4's modulators | With demand |

## Parameters

| ID (permanent) | Name | Description | Range | Default | Unit | Curve / skew | Smoothing | Automatable |
|---|---|---|---|---|---|---|---|---|
| `type` | Type | The kind of filter: the classic passes and a notch, a morph between them, a DJ sweep, a comb, a vowel, or resampling. | Low Pass, High Pass, Band Pass, Notch, Morph, DJ, Comb, Vowel, Notch + LP, Resampling | Low Pass | choice | – | 30 ms crossfade | Yes |
| `circuit` | Circuit | The filter's character: Clean is exact, Warm thick and smooth, Acid rubbery and squelchy, Gritty raw where it resonates. | Clean, Warm, Acid, Gritty | Clean | choice | – | 30 ms crossfade | Yes |
| `slope` | Slope | How steeply the filter cuts: 12 dB per octave is gentle, 24 dB steeper and more dramatic. | 12 dB, 24 dB | 24 dB | choice | – | 30 ms crossfade | Yes |
| `freq` | Freq | Where the filter works: its cutoff, a comb's pitch, or where resampling starts to fold the highs back. | 20 … 20 000 | 2 000 | Hz | log (centre 632 Hz) | 20 ms multiplicative | Yes |
| `res` | Resonance | How much the filter rings at its cutoff: more is sharper and more vocal, and near 100 % it whistles. | 0 … 100 | 25 | % | linear | 20 ms | Yes |
| `sweep` | Sweep | The DJ filter: left of the centre a low pass closes on the highs, right of it a high pass on the lows; at the centre, nothing. | −100 … +100 | 0 | % | linear | 20 ms | Yes |
| `vowel` | Vowel | Which vowel the filter speaks: a, e, i, o or u, and the sounds between them. | 0 … 100 (a, e, i, o, u at 0, 25, 50, 75, 100) | 0 (a) | % | linear | 20 ms | Yes |
| `morph` | Morph | Morph: low to band to high pass. Comb: peaks to notches. Notch + LP: the notch nearer the cutoff. Vowel: a deep voice to a small one. | 0 … 100 | 0 | % | linear | 20 ms | Yes |
| `drive` | Drive | How hard the sound hits the filter: more is warmer and grittier, at about the same level. | 0 … 24 | 0 | dB | linear | 20 ms | Yes |
| `lfo` | LFO | Moves the filter with a wave, freely or in time with the song. | Off / On | Off | toggle | – | 20 ms crossfade | Yes |
| `lfo.rate` | LFO Rate | How fast the LFO moves the filter, in cycles per second. | 0.01 … 40 | 1 | Hz | centre 2 Hz | 20 ms multiplicative | Yes |
| `lfo.sync` | LFO Sync | Moves the filter in time with the song: the rate becomes a note value. | Off / On | On | toggle | – | the LFO's glide | Yes |
| `lfo.div` | LFO Sync Rate | How fast the LFO moves the filter as a note value, one cycle per note, while Sync is on. | 8 bars … 1/32, as the Chorus | 1 bar | choice | – | the LFO's glide | Yes |
| `lfo.shape` | LFO Shape | The LFO's wave: smooth, even, rising, falling, jumping, stepping at random or drifting. | Sine, Triangle, Saw Up, Saw Down, Square, Random, Drift | Sine | choice | – | the LFO's smoothing | Yes |
| `lfo.amount` | LFO Amount | How far the LFO moves the filter: at 100 %, five octaves each way. | 0 … 100 | 50 | % | linear | 20 ms | Yes |
| `lfo.phase` | LFO Phase | Where the cycle starts against the beat, while Sync is on: 180° starts half a cycle late. | 0 … 360 | 0 | ° | linear | 50 ms glide | Yes |
| `lfo.stereo` | LFO Stereo | How the two sides move: Offset moves them apart in step, Spin at different speeds. | Offset, Spin | Offset | choice | – | the LFO's glide | Yes |
| `lfo.offset` | LFO Offset | How far apart the two sides move: together at 0°, in opposite directions at 180°. | 0 … 180 | 0 | ° | linear | 50 ms glide | Yes |
| `lfo.spin` | LFO Spin | How much faster the right side moves than the left, so the two drift in and out of step. | 0 … 100 | 20 | % | linear | 20 ms | Yes |
| `env` | Envelope | Lets the sound's level move the filter: the input's, or the sidechain's when one is chosen. | Off / On | Off | toggle | – | 20 ms crossfade | Yes |
| `env.amount` | Env Amount | How far, and which way, the level moves the filter: at 100 %, up to five octaves. | −100 … +100 | +50 | % | linear | 20 ms | Yes |
| `env.attack` | Env Attack | How quickly the filter follows the sound getting louder. | 1 … 200 | 10 | ms | centre 20 ms | – | Yes |
| `env.release` | Env Release | How quickly the filter falls back once the sound gets quieter. | 10 … 2 000 | 250 | ms | centre 250 ms | – | Yes |
| `clip` | Clip | Softly rounds off the filtered sound's peaks, so resonance never goes over 0 dBFS. Quieter sound passes exactly. | Off / On | On | toggle | – | 20 ms crossfade | Yes |
| `mix` | Dry/Wet | Blends the original sound with the filtered one. | 0 … 100 | 100 | % | linear | 20 ms | Yes |
| `output` | Output | The level after the filter. | −24 … +12 | 0 | dB | linear | 20 ms, linear in amplitude | Yes |

No non-parameter state. State version 1. The sidechain's source is host routing, chosen in the module's header ([ADR 0025](../../docs/decisions/0025-sidechain-routing.md)), not a parameter.

**Ranges and limits:**
- **Which controls each type uses:**

  | Type | Main control | Resonance | Morph | Slope and Circuit |
  |---|---|---|---|---|
  | Low Pass, High Pass, Band Pass, Notch | Freq | Yes | – | Yes |
  | Morph | Freq | Yes | low → band → high pass | Yes |
  | DJ | Sweep | Yes | – | Yes |
  | Comb | Freq (its pitch) | how sharp | peaks → notches | – |
  | Vowel | Vowel | how sharp | deep → small voice | – |
  | Notch + LP | Freq | Yes | the notch's place | Yes |
  | Resampling | Freq (half the held rate) | – | – | – |

- **The main control** is the one the LFO and the envelope move. Each type keeps its own (Freq, Sweep, Vowel), so switching types and back loses nothing.
- **How far modulation reaches:** the main control on its knob's own scale (0 to 1), plus LFO Amount × L / 2 plus Env Amount × E / 2, held between 0 and 1. L is the LFO's value (−1 to 1) and E the envelope (0 to 1). On Freq's log scale that's five octaves each way at 100 %; on Sweep and Vowel, half their travel each way.
- **The envelope** E is the level in dB, from 0 at −48 dBFS to 1 at 0 dBFS (the Phaser & Flanger's).
- **Band Pass** keeps its peak at 0 dB at any Resonance (it narrows rather than boosts), so a sweep doesn't jump in level. The other types' resonance rises above the passband, as a filter's should.
- **Resonance at 100 %:** Warm and Acid whistle at the cutoff but don't keep ringing on their own: their ring fades within about 2 s of silence, so the tail ends (F8). Gritty clips its resonance, so it screams without running away.
- **Comb's pitch** is held between 20 Hz and 10 kHz.
- **DJ:** below the centre, a low pass whose cutoff falls from above hearing to about 50 Hz at −100 %; above it, a high pass rising from below hearing to about 10 kHz at +100 %, both on a log scale. Resonance adds a bump that grows with the distance from the centre, so the centre stays flat.
- The note values, the synced cycle's limit (100 s) and the way the LFO stays locked to the song are the Chorus's (`dsp::lfoNoteValues`, `dsp::SongLfo`).

## DSP design

```
in ─┬─> Drive ─> 2× up ─> the filter: Type, Circuit, Slope, at each side's main control ─> 2× down ─> Clip ─> wet ─┐
    │                                    ^                                                                          │
    │          LFO (SongLfo, a side each) ┤ main control + LFO Amount × L / 2 + Env Amount × E / 2                  │
    ├─> envelope (level detector, on the input or the sidechain) ┘                                                  │
    └─> dry, held back by the latency ─────────────────────────────────────────────── mix (equal gain) ─> Output ─> out
```

**The circuits** shape every type built from a low, band and high pass (Low Pass, High Pass, Band Pass, Notch, Morph, DJ, Notch + LP):
- **Clean:** the synth's state-variable filter (zero-delay feedback): linear and exact. 24 dB is two in a row, the resonance in the second. Drive saturates softly before it (the modulation core's soft clip), so at 0 dB it's exact.
- **Warm:** a transistor ladder (the synth's LP 24, zero-delay feedback), saturating in its loop: thick and smooth; the lows thin a little as the resonance rises. Its high and band pass and notch come from mixing its stages' outputs; 12 dB from two stages.
- **Acid:** a diode ladder: its stages load one another, so the resonance is rubbery and squelchy and the slope softer near the cutoff. Its other types come from mixing, as Warm's.
- **Gritty:** a two-pole Sallen-Key whose resonance passes a hard clipper: raw and aggressive, its resonance held in check. 24 dB adds a clean two-pole before it.
- Each circuit's nonlinear stage is solved each sample with one estimate (as the synth's ladder), so the cutoff can move every sample with no zipper.
- **What the display draws:** each circuit's small-signal response is a pure function, `Filter::response(settings, hz, sampleRate)`. At low levels, the audio matches it (the test targets).

**The other types:**
- **Morph:** the circuit's low, band and high outputs, crossfaded: low pass at 0 %, band pass at 50 %, high pass at 100 %, with the level held. At 24 dB both stages morph.
- **DJ:** the circuit's low pass or high pass, per the Sweep's side. Within 2 % of the centre, it crossfades to the dry sound, so the centre is exactly flat.
- **Comb:** a delay of 1 / Freq, read between samples (cubic), crossfading from a comb of peaks (feedback) at 0 % to a comb of notches (feed-forward) at 100 %, both at multiples of Freq; Resonance sets their gain (0 to 95 %).
- **Vowel:** three band passes in parallel (the Clean state-variable's) at the vowel's first three formants. They come from a table of five vowels (published averages), interpolated between neighbours, and are shifted up by Morph (up to an octave, from a deep voice to a small one). Resonance sharpens them; their levels come from the table.
- **Notch + LP:** the circuit's low pass at Freq, then a notch from two octaves below Freq (Morph 0 %) to Freq itself (100 %). Resonance sets the low pass's resonance and narrows the notch.
- **Resampling:** the sound is held at 2 × Freq samples a second (no smoothing between), so whatever is above Freq folds back down: a lo-fi grit that moves with the cutoff.

**Oversampling:** the filter, from Drive to Clip, runs at twice the rate, through the Saturator's oversampler (its first stage). The circuits' saturation stays clear of aliasing, and resonant peaks near the top keep their shape (F7).
- **Latency:** the oversampler's, the same at every setting ([ADR 0016](../../docs/decisions/0016-constant-device-latency.md)): about 1.3 ms at 48 kHz, reported; the dry sound is held back to match.

**Drive:** raises the level into the filter by up to 24 dB, and lowers it after by the same, so turning it changes the colour more than the level.

**Modulation:**
- **The LFO:** a modulation-core `SongLfo` per side (the Phaser & Flanger's): free in Hz, or one cycle per note value locked to the song's position, gliding over jumps. Its value is smoothed (a few milliseconds), so Square and Random step without clicks.
- **Stereo:** Offset makes the right side lead the left by Offset; Spin runs the right side up to twice as fast. Phase shifts both against the beat while synced.
- **The envelope:** the dynamics core's `LevelDetector` (peak) with Env Attack and Env Release, on the input before Drive, or on the sidechain when one is chosen (ADR 0025: with none connected, it behaves exactly as without one). One envelope moves both sides, from the louder of the two.
- **Where each side's main control is now** is a pure function, `Filter::modulated`, which the display calls too. The filter is retuned every sample.

**Changes:** Type, Circuit and Slope run the old and the new filter side by side for 30 ms and crossfade. Freq, Resonance, Morph, Sweep, Vowel and Drive are smoothed (20 ms).

**Output:**
- **Clip:** a soft clip on the filtered sound: exact under −6 dBFS, approaching 0 dBFS above it and never passing it. It sits before Dry/Wet, so at Mix 0 % the output is the input exactly.
- **Mix:** equal gain, as the Delay and the Reverb.
- **Tail:** the filter's longest ring at the current settings: the comb's (up to about 7 s at 20 Hz and Resonance 100 %), the ladders' near 100 % (up to 2 s), otherwise well under a second.
- Parameters are read on control ticks (every 32 samples) and smoothed per sample, as in the other modules.
- **Analysis:** slots 0/1 = input peak L/R; 2/3 = output peak L/R; 4/5 = each side's main control now (0 to 1), for the display; 6 = the envelope (E). A FIFO carries the output (both sides summed) for the display's spectrum, as the EQ's.

## UI layout

On the module grid ([device-panels.md](../../docs/design/device-panels.md)), sections run left to right. Each is written `id` TITLE [columns], with controls at (column, row) and spans; knobs are 1 × 3 cells.

| Section | Controls |
|---|---|
| `filter` FILTER [6] | Response display (0, 0, 6 × 3) · Freq, Sweep or Vowel (0, 3) · Resonance, named Res (1, 3) · Morph (2, 3) · Drive (3, 3) · Type dropdown (4, 3, 2 wide) · Circuit dropdown (4, 4, 2 wide) · Slope [12 dB \| 24 dB] (4, 5, 2 wide) |
| `lfo` LFO [3], power = LFO | Rate (0, 0) · Amount (1, 0) · Offset or Spin (2, 0) · Shape dropdown (0, 3, 2 wide) · Sync toggle (2, 3) · Stereo [Offset \| Spin] (0, 4, 2 wide) · Phase bar (0, 5, 3 wide) |
| `envelope` ENVELOPE [3], power = Envelope | Amount (0, 0) · Attack (1, 0) · Release (2, 0) |
| `output` OUTPUT [2] | Dry/Wet (0, 0) · Output (1, 0) · Clip toggle (0, 3, 2 wide) |
| meter strip | In and out |

The LFO and ENVELOPE sections are the Phaser & Flanger's MODULATION and ENVELOPE, with a power switch on the LFO and a Phase bar: the same thing looks and behaves the same.

**The response display:**
- **The response** of each side where the modulation puts it now (the left's over the right's), from `Filter::response`. It's drawn over the output's spectrum, faint (the EQ's analyser), on the shared log-frequency axis (`ui::SpectrumPlot`), 20 Hz to 20 kHz.
- **A shaded band** shows how far the LFO and the envelope reach (as the Phaser & Flanger's).
- **The handle:** where the type has a cutoff, a handle sits on the curve at Freq.
- **Resampling** draws the band below Freq, and shades what folds back above it.
- **Its line** reads the main control now: the cutoff ("1.20 kHz"), or the range the modulation covers ("400 Hz–3.6 kHz"). For DJ, which filter and where ("low pass 1.2 kHz", "flat"); for Vowel, the vowel ("a", "a–e").
- **Dragging:** sideways moves the main control (Freq, Sweep or Vowel), up and down changes Resonance. Shift drags finely.
- With nothing playing it (the snapshots, the gallery), it draws the response at rest.

**Controls:**
- **The main knob follows the type:** it turns `freq`, `sweep` or `vowel` (`ParamKnob::follow`); its name and value follow.
- **Rate follows Sync,** and **Offset or Spin follows Stereo,** as in the Phaser & Flanger.
- **Dimming:**
  - Resonance in Resampling; Morph outside Morph, Comb, Vowel and Notch + LP; Slope and Circuit for Comb, Vowel and Resampling.
  - Phase while Sync is off.
  - The LFO and ENVELOPE sections while they're off (their power).
- About 850 px wide (14 columns and the meter strip).

## Factory presets

Built and named by Claude after approval ([presets.md](../../docs/design/presets.md) §13); signed off by the product owner by ear. Planned (16):

- **Slow sweep** (Pad, Bus): A low pass opening and closing over two bars.
- **Wobble bass** (Bass, Heavy): A warm, resonant low pass wobbling in eighth notes.
- **Auto-wah** (Guitar, Keys): A band pass that opens with each note you play.
- **Acid squelch** (Bass, Dirty): A resonant acid low pass, driven, that bites on each note.
- **DJ sweep** (Bus, Master): The DJ filter, a little to the low side: turn Sweep either way.
- **Telephone** (Vocals, FX): A narrow, driven band of the voice, as down a phone line.
- **Underwater** (Pad, FX): A dark, slowly moving low pass.
- **Sidechain pump** (Pad, Bus): Closes on each hit: choose the kick under Sidechain, and the pad breathes with it.
- **Talking bass** (Bass, Lead): The vowel filter moving from a to o in time with the song.
- **Comb resonator** (FX, Drums): A comb tuned to a pitch, so drums ring like a metal tube.
- **Lo-fi resample** (Drums, Dirty): Resampling that folds the highs back into grit.
- **Spinning filter** (Pad, Wide): The two sides drifting in and out of step, wide.
- **Gentle warmth** (Bus, Warm, Subtle): A soft, warm low pass with a little drive, to take the edge off.
- **Gated chop** (Drums, Keys): A square wave opening the filter in sixteenths.
- **Screaming lead** (Lead, Dirty): Gritty resonance, driven hard.
- **Notch sweep** (Guitar, Keys): A notch swept slowly through the sound.

## Automated test targets

| Metric | Target |
|---|---|
| Response | With the LFO and envelope off, at −40 dBFS: a log sweep's response matches `Filter::response` within 0.2 dB (Clean) or 1 dB (Warm, Acid, Gritty), 30 Hz–15 kHz, for every type with a cutoff, both slopes, Freq 100 Hz, 1 kHz and 8 kHz, Resonance 0, 50 and 90 % |
| Slopes | Two octaves past the cutoff, at Resonance 0: 12 and 24 dB/oct within 1.5 dB/oct, every circuit |
| Band Pass level | The peak within 0.5 dB of 0 dB at any Resonance (Clean) |
| Resonance at 100 % | Warm and Acid ring at Freq within 3 %; after the input stops, the ring falls 60 dB within 2 s |
| DJ | Sweep 0 %: the output is the input (held back by the latency), under −100 dBFS apart; −100 % and +100 %: a low pass near 50 Hz and a high pass near 10 kHz |
| Comb | Peaks (Morph 0 %) or notches (100 %) at multiples of Freq within 1 %, 50 Hz–5 kHz |
| Vowel | The three formant peaks where the table puts them for each vowel, within 5 % |
| Resampling | A 1 kHz sine with Freq 4 kHz: the output's images where an 8 kHz hold puts them, within 1 % |
| Modulation reach | LFO Amount 100 % (or Env Amount 100 %, at 0 dBFS): the cutoff, read from analysis slots 4/5, reaches five octaves from Freq, within 2 % |
| Sync, Phase and Spin | Synced, the LFO is at the same place after a jump in the song as played through to it; Phase 180° is half a cycle behind; with Spin, the right side's cycle is the left's divided by (1 + Spin) |
| Offset 0°, mono input | Left and right identical, every type |
| Envelope | A burst at 0 dBFS, Env Amount +100 %: the cutoff rises within Env Attack and falls back within Env Release of the burst ending |
| Sidechain | A sidechain connected: the envelope follows it, not the input; none: as without one (ADR 0025) |
| Drive | At −30 dBFS, Clean, Drive 24 dB: within 0.5 dB of Drive 0 (level-matched) |
| Aliasing | Warm, Drive 12 dB, Resonance 50 %, Freq 5 kHz, a 2.5 kHz sine at −6 dBFS: whatever isn't a harmonic of the tone under −60 dB in the audible band |
| Clip | On: under −6 dBFS untouched (null within −120 dBFS); a 0 dBFS input at Resonance 100 %, every type and circuit: the wet never over 0 dBFS. Off: never over +12 dBFS |
| Mix 0 % | Null against the input held back by the latency (under −120 dBFS) |
| Latency | Measured equals reported, at every setting |
| Type, Circuit, Slope, Shape and Sync changes | No clicks (the conformance click score) |
| Display vs audio | The drawn response is `Filter::response` at each side's main control now; the readout's range is where the modulation reaches |
| Conformance suite | All pass (no allocation, no denormals, parameter jumps without clicks, the tail as reported, the latency as reported) |

## A/B cases (against the Live target)

Deferred until a Live machine is available. When it is: each circuit against Live's nearest (Clean against SVF, Warm against the ladder, Gritty against the Sallen-Key, Acid against DFM's role), each at Resonance 0, 50 and 90 % with a sweep, on a saw bass and a drum loop; listen for the resonance's character, the lows' loss, and how Drive breaks up.

## A/B log

–

## Questions for review

| # | Question | Recommendation |
|---|---|---|
| F1 | **Filter or EQ** | One filter whose movement and character are the point, separate from the EQ, which fixes and balances the tone with many clean bands. Neither borrows the other's job: no shelves, bells or bands here; no resonant circuits or LFOs in the EQ |
| F2 | **The types** | All ten of Live's, in one dropdown: Low Pass, High Pass, Band Pass, Notch, Morph, DJ, Comb, Vowel, Notch + LP and Resampling. Each is cheap beside the circuits, and together they cover what people reach for a filter to do |
| F3 | **The circuits** | Four, named by how they sound: Clean (a state-variable), Warm (a transistor ladder), Acid (a diode ladder), Gritty (a Sallen-Key with clipped resonance). They fill the roles of Live's four (SVF, DFM, MS2, PRD) without naming hardware (ADR 0008). They shape every type built from a low, band and high pass; Comb, Vowel and Resampling have their own sound |
| F4 | **Slopes** | 12 and 24 dB/oct, for every type with a slope; 24 by default. Leave out the 6 and 48 dB that Live offers on Morph alone (Later) |
| F5 | **One knob per role** | The main knob follows the type (Freq, Sweep or Vowel), each keeping its own value, so switching and back loses nothing. Morph is one parameter whose meaning follows the type (as Live), dimmed where unused |
| F6 | **Drive** | 0–24 dB into the circuit, level-matched (lowered after by the same), so it changes the colour more than the level. Clean saturates softly before the filter, so at 0 dB it's exact |
| F7 | **Oversampling** | Always 2×, through the Saturator's oversampler: the circuits' saturation stays clean and the top octave keeps its shape. The cost is a constant latency of about 1.3 ms, reported (ADR 0016). The other way, no oversampling and no latency, aliases audibly once Drive is up |
| F8 | **Resonance at 100 %** | Warm and Acid whistle at the cutoff but stop within 2 s of silence, so the tail ends and nothing sings on unseen. Gritty clips its resonance. A filter that oscillates on its own, as a sound source, comes later if asked for |
| F9 | **The LFO** | The Phaser & Flanger's: Rate or a synced note value, seven shapes, Offset or Spin, plus the Auto Pan & Tremolo's Phase against the beat, with a power switch (off by default). At 100 % it moves the cutoff five octaves each way. Leave out Live's LFO Morph and its Steps and S&H quantize (Later) |
| F10 | **The envelope and sidechain** | The Phaser & Flanger's envelope (Amount either way, Attack, Release), with a power switch. It follows the input, or another track chosen under the header's Sidechain (ADR 0025), so a pad can close on every kick. One envelope moves both sides. Leave out Live's sidechain mix, gain, filter and listen, and its Hold and S&H (Later) |
| F11 | **Output** | Clip on by default: a soft clip on the filtered sound only, exact under −6 dBFS, so loud resonance can't blast, and Mix 0 % stays the input exactly. Dry/Wet with equal gain (100 % by default), and Output |
| F12 | **The display** | Each side's response where the modulation puts it now, over the output's spectrum, with the modulation's reach shaded, as Live and the Phaser & Flanger. Drag sideways for the main control, up and down for Resonance |
| F13 | **Name and IDs** | Named **Filter**: what it is, and what people look for. Device `filter`, plugin code `Fflt`, CLAP `org.segnoaudio.filter`; section IDs as above (`filter`, `lfo`, `envelope`, `output`) |

## Settled while building (B6, 2026-10-12)

- **The latency is 73 samples** (1.5 ms at 48 kHz, 1.7 ms at 44.1 kHz), not 1.3 ms: the shared oversampler pads every factor to its longest chain's latency, so a module's latency never depends on its quality setting (ADR 0016). The same at every setting, as F7 says.
- **Resonance:** the 2-poles' Q runs from 0.71 at 0 % (no peak: Freq is where the filter is 3 dB down) to 25 at 100 %, on a curve that gives the knob's lower half the finer steps. The ladders' resonance stops just short of ringing on their own, closer at higher cutoffs, so a ring always falls 60 dB within 2 s.
- **Warm has a gentle saturation going in** as well as in its loop. At high resonance the loop's feedback cancels most of the sound at its saturating stage, so it hardly coloured anything (0.2 % distortion where Clean had 4.5 %).
- **Acid is the transistor ladder's structure with a high pass in its feedback and a saturation that leans one way,** not a diode ladder whose stages load one another. Stages that load one another can't be mixed into a clean high or band pass (the mixes leak the lows), and these two carry its sound: the deepest lows kept as the resonance rises, and the squelch. Its slope doesn't soften near the cutoff.
- **Gritty is the state-variable filter with its band-pass state clipped** (where the resonance builds), and a hard clip going in, rather than a Sallen-Key: the clipped resonance is its sound, and small signals follow the same response Clean's does, which the display draws.
- **Acid's feedback high pass sits at 0.3 × its cutoff,** and its low pass gets some of its lows back, as Warm's does: its resonance still took a 110 Hz saw under an 800 Hz cutoff down 9 dB. Its saturation leans one way (even harmonics, the second at about −24 dB in the golden test), the offset that leaves taken out at 5 Hz.
- **The DJ filter's fade to the dry sound is at the module's rate,** after Clip: at the centre, the output is the input, held back by the latency, exactly (the oversampler's filters alone don't give that).
- **The vowel's level:** its three narrow formants take most of a sound away; they're made up by 15 dB and by how sharp Resonance makes them, so pink noise comes out about 3 dB down.
- **The comb's display** draws the comb's teeth until they're closer than a third of a pixel column, then the band between its peaks and troughs, filled: drawn point by point, the teeth up high read as noise.
- **The display's handle** sits whole inside the plot (in a notch, or at either end of the range). Its size is the panel kit's new `DeviceGrid::displayHandle`, which the Compressor's threshold point now uses too.
- **The spectrum behind the curve is the EQ's,** moved into the panel kit as `ui::FifoSpectrum` and shared, so the two read alike.
- **The presets are levelled with Output:** the band passes (Auto-wah, Telephone) take most of a sound away, so they carry +12 and +10 dB; every preset ends within 3 dB of its input on the conformance suite's test signal (−3.0 to +1.1 dB).
- **The conformance suite measures latency through the dry path** where a module has a Dry/Wet: the Filter's default low pass delays the sound by its own group delay (about 10 samples), which isn't latency. The Filter's own test measures the latency through the filter, at a high pass at 20 Hz.

## Sign-off

- [x] Automated: unit, conformance and golden tests; validators (pluginval at strictness 10 and clap-validator pass; 2026-10-12)
- [x] Layout lint passes (`fd_panel_tests`)
- [x] Snapshots reviewed (100% / 200%; expanded, collapsed, folded; every factory preset at 200 %, with `fd-snapshot --presets filter`)
- [ ] Sound signed off by the product owner, with the factory presets
- [ ] Feel signed off by the product owner
