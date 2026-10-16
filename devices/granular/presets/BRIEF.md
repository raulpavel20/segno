# Granular's factory presets: the brief

The set's design, per [ADR 0069](../../../docs/decisions/0069-factory-presets-are-designed-and-never-clip.md): the research it rests on, what each preset is for, how they're played, and how they're checked. The recipes are made by `make_recipes.py` beside this file, which also writes the catalogue below (`catalogue.md`).

## 1. What the research says

Collected 2026-10-16 from the manuals of the best-regarded granular instruments (Ableton's Granulator II and III, Steinberg's Padshop, Spectrasonics' Omnisphere, Logic's Alchemy, Kilohearts' Phase Plant, Mutable Instruments' Clouds and Beads), sound designers' tutorials and Barry Truax's and Curtis Roads' writing on granular synthesis.

**What each size of grain is for**
- **Under about 20 ms:** grains are clicks, and grains started faster than about 20 a second fuse into a pitch: what's inside the grain becomes its formant. That's Pitched mode, "a sample as an oscillator" ([Padshop 2 manual](https://archive.steinberg.help/padshop/v2/en/Padshop_2_Operation_Manual_en.pdf); [Granulator III](https://roberthenke.com/technology/granulator3.html)).
- **10–100 ms:** textures. Below about 20 ms the source fades into the grain's own buzz; above, the source stays recognisable ([SFU Microsound](https://www.sfu.ca/sonic-studio-webdav/cmns/Handbook%20Tutorial/Microsound.html), [Truax](https://www.sfu.ca/~truax/gran.html)).
- **200–600 ms:** pads, frozen and stretched (Padshop's "Swarm Intelligence": 200 ms grains, position random raised "until the sound is no longer repetitive").

**Techniques**
- **Freeze and stretch:** grains keep their order while the playhead moves on its own. Stretching shows a sound's inside: bells become "slowly dying organ clusters" ([Truax](https://www.sfu.ca/~truax/gsample.html)). Keep the attack and stretch only the body to keep a sound recognisable. Freezing wants a little motion, an LFO at low depth on Position ([Alchemy, MusicTech](https://musictech.com/tutorials/logic-granular-1/)).
- **Formant-preserving pitch:** Hann windows about two periods long, re-spaced at the new period, keep the formants ([KAIST](https://mac.kaist.ac.kr/~juhan/gct535/Slides/13%20Time%20Scale%20Modification%20and%20Pitch%20Shifting.pdf)). It suits steady, harmonic, monophonic sources; a nearly pure tone cancels (Granular's SPEC). Two periods is the clean case; one is brighter and buzzier; four to eight keep more detail.
- **Spray:** "things start getting interesting as soon as there is some random variation" ([Henke](https://roberthenke.com/technology/granulator.html)). Pitch spray unquantised is "harmonically unusable" ([Output](https://output.com/blog/granular-synthesis)); quantised to octaves and fifths it's a shimmer, to a chord a harmony ([Audio Damage](https://www.audiodamage.com/blogs/news/a-field-guide-to-our-granular-plugins)). A few cents apart is a unison ([Kilohearts](https://kilohearts.com/blog/granular_generator_techniques)).
- **Windows:** Hann smooth and blurry; Gaussian the cleanest spectrum but narrow, so it wants an overlap of 3 or more; Sharp Attack snappy; a late peak (Skew) swells; Square clicks, for glitches ([Clouds manual](https://cdn.softube.com/user-manuals/mutable-instruments-clouds), [Beads](https://pichenette.readthedocs.io/en/latest/modules/beads/manual/)).
- **Rhythm:** synced sample-and-hold on Position, clocked density as a divider or a probability, repeats for stutters ([SOS on Granulator III](https://www.soundonsound.com/techniques/ableton-live-12-granulator-iii)).

**What makes granular presets sound bad, and the cures**
1. **Phasey, hollow mush:** identical overlapping grains comb-filter. Spray beyond the comb range, add Size Spray, a little Jitter and a few cents of detune; or go fully coherent (Pitched, or no spray while scanning).
2. **Chorusy smear on tonal sources:** spray across a vibrato's cycle (140–200 ms) makes an ensemble: right for pads, wrong for leads.
3. **Clicks:** square or zero-attack windows; pitched-down grains without overlap.
4. **Harshness:** short grains with sharp windows; cure with Gaussian windows, longer grains, a low pass.
5. **Mud:** clouds bury the low end; a high pass under pads, following the note.
6. **Level jumps across the keyboard:** sweep the keyboard and measure (ADR 0069's check).
7. **Clipping as overlap and polyphony multiply:** N uncorrelated grains add 10·log10 N dB, coherent ones 20·log10 N; Density Gain's 1/√N holds only the first. Measure every preset.
8. **Too dry:** the instruments with the best reputations build in reverb; Segno's presets get their space from Pan Spray, Width, long releases and Grain Tails, and come again as racks with the Reverb or the Delay.
9. **Static:** slow LFOs under 0.1 Hz at unrelated rates.
10. **Stepped modulation:** grain targets are read once a grain; sweep voice targets (filters, amp) for smooth moves.

**Treatments by source** (the Library's instruments): string sustains past the bow's attack, frozen or as clouds; tremolo as glitter (it's granular already); pizzicato and spiccato as rain, strums and sequences; piano held past the hammer, in the first quarter of its file where every note still sounds; organ as the best Pitched source and the steadiest drone; tuned percussion's ring frozen ("bowed vibes"), struck notes as sparkles; brass and reeds as Pitched leads and basses for their formants; flute and harmonica breath as wind; timpani rolls as thunder.

## 2. The set

- **60 presets** in nine families: pads, atmospheres and textures, drones, keys and plucks, leads, basses, rhythm and glitch, effects (risers, stops, freezes), experiments.
- **Every mode of step 1:** Classic, Loop, Cloud, Pitched, Rhythm, and Fire (from a random source).
- **Across the set, used where they make the sound:** both sources (picked at random, together, blended by a macro), parts of a source (the piano's first quarter, a bell's ring), Position and its spray shapes, Snap to transients, Scan with Bounce and Stop, the Song playhead, Size Spray, Level Spray, Even, Jitter, Chance, Repeats, Swing, Reverse, Skew, the Hann, Gaussian, Sharp Attack, Sharp End and Square windows, Grain Glide, pitch spray quantised to octaves, fifths, chords, the Key Chord and the song's key, pan by alternation and by pitch, per-grain filters and resonators snapped to the key, Crush with its spray, Ring with its spray, both voice filters (a low pass above, a high pass below that follows the note; ladder and drive for basses), all three envelopes (Env 3 on Formant for a wah, on Density, Pitch and the high pass for a riser, on Scan for a tape stop), LFOs and random sources (Smooth, Steps synced to the song), Grain Random, the matrix's Scale, Latch, Mono and Legato with glide.

## 3. Playing it

**The macros keep their place in every preset**, as the best factory libraries do, so a hand learns them:

| | Role | Usually | Moves |
|---|---|---|---|
| 1 | where | **Place** | Position, both ways from the preset's own |
| 2 | how scattered | **Scatter** | Position Spray, Pan Spray and Size Spray together |
| 3 | the grain | **Grain** (Character on Pitched) | Size, both ways (Formant on Pitched) |
| 4 | the tone | **Tone** | the voice filter's cutoff, both ways |
| 5 | pitch | **Shimmer**, Fifths, Range, Chord, Blend | pitch spray onto octaves, fifths or a chord; the source mix |
| 6 | movement | **Motion**, Vibrato, Jumps | the depth of the preset's LFOs and random sources (the matrix's Scale) |
| 7 | time | **Drift**, Density, Freeze, Stop | Scan, Density or Chance |
| 8 | character | **Reverse**, Crush, Glow, Grit | Reverse, Crush, Ring, drive |

A macro that reads both ways (Place, Grain, Tone, Blend, Range) sits at 50 with the preset as designed there; the others add from 0. The mod wheel is each preset's "more granular" axis (more spray, density, formant, freeze); velocity plays level and, mostly, brightness or density; pressure, where it's natural, more pitch spray.

## 4. Checked

- **ADR 0069's keyboard check** (`fd-presets check devices/granular/presets/recipes.json`): a note in every octave and four-note chords on five roots at full velocity, the song playing, and the chord on C3 again with every macro at 0, every macro at full, the mod wheel and pressure up: nothing above −1 dBFS; about −24 dB RMS for a C3; the notes in the preset's range within 12 dB. A preset's range is the middle five octaves unless it says: basses C0–C3, the high instruments from C2.
- **Levels** set by `fd-presets level … out.volume`, by measurement.
- **The product owner signs the set off by ear.**

## 5. The catalogue

Written by `make_recipes.py` into `catalogue.md`.
