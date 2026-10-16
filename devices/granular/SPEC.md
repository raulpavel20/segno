# Granular: device spec

- **Status:** Approved by the product owner (2026-10-16, with every recommendation of G1–G24); IDs are permanent. [ADR 0065](../../docs/decisions/0065-granular-goes-beyond-live.md) accepted with it: the product owner asked for Granulator III plus extras, extensive and full-featured, so that for granular work most people won't reach for another plugin; open to experimental workflows; and with CPU optimisation even more of a focus than usual ("optimise everything where possible"). The visual mockup ("Drawn") approved by the product owner (2026-10-16), with the grey waveform behind the grains kept; step 1 (the instrument) next.
- **Type:** Instrument
- **Tier / milestone:** Tier 2, moved up from M5 into M3 as P8 by the product owner (2026-10-16), on its own branch ([next-synth.md](../../docs/devices/next-synth.md), chosen by the product owner on 2026-10-16).
- **Live target:** Granulator III (Live 12 Suite). References only ([ADR 0011](../../docs/decisions/0011-calibrated-ambition-and-consolidation.md)); this device goes well beyond it (G1).

## Purpose

The instrument that turns recordings into sound: pads and drones from a few seconds of strings, textures from a field recording, a voice stretched to a minute, a guitar taken live from another track and scattered into a cloud. Grains are short pieces of a sound, played over each other by the hundred.

It should stand next to the best granular instruments in depth. It should beat them where the integrated DAW helps:
- any clip or track in the song as a source;
- grains that land on the song's key;
- a playhead that follows the song;
- captures kept with the project.

It should also cost less CPU than any of them, because granular work is where CPU runs out first.

## What the market offers (research, October 2026)

Read from the manuals, release notes, reviews and forums of:
- Granulator III;
- the granular modes of Bitwig 6.1's Sampler, Logic 12.3's Alchemy, Padshop 2 and Reason's Grain;
- the granular engines of the leading wavetable and hybrid synths;
- dedicated granular instruments and effects: Quanta 2, Portal, Emergence, Crusher-X, Granite, and 2026's Grainferno and Phonon;
- Mutable Instruments' Clouds and Beads, whose code is open;
- EmissionControl2, Borderlands, and Csound's partikkel.

**Granulator III** (Robert Henke, Max for Live, last updated in 2024):
- **Three modes:**
  - Classic: two overlapping grains per channel;
  - Loop: crossfaded loops of longer sections;
  - Cloud: up to 20 unsynced grains per channel, where Shape is fixed to save CPU.
- **One sample,** normalised on loading.
- **Position** is set by a click on the waveform. Scan is a ramp from note-on, with Distance and Loop.
- **Grains** are 2 ms to 2 s. Variation is per grain, for position (forward only), size, transpose and volume.
- **Sound:** two state-variable filters in nine combinations, two envelopes, one stereo LFO (audio-rate since 1.2).
- **Modulation** is a fixed short list of sources per knob, with no matrix.
- **Playing:** MPE, eight voices.
- **Capture:**
  - a rolling buffer of a few seconds from an input or another track, grabbed after the fact;
  - captures that aren't saved are lost when the project reopens.
- **What reviewers miss:**
  - FM, AM, noise and a filter envelope, all dropped from Granulator II;
  - the display shows only the left channel;
  - after-the-fact capture is "disorienting";
  - LFO units are inconsistent;
  - modulation is "not totally obvious".

**What the others add:**
- **Sources:**
  - several at once: Pigments picks among six per grain; Grainferno has two with morph modes; EmissionControl2 and Borderlands play several files;
  - live input with freeze: Emergence, Crusher-X, Beads;
  - transient or grid capture: Efx Fragments.
- **How grains start:**
  - synchronous or stochastic;
  - at the note's pitch, giving tones from any sound: Alchemy 12.3's pitched grains, Padshop's period-length grains, Phase Plant's Align Phases;
  - by rate, density or a target count: Phase Plant, Phonon;
  - by clock, gate or probability: Beads, Bitwig's Grid;
  - triggered by modulation: Omnisphere;
  - repeats: Bitwig;
  - intermittency: EmissionControl2.
- **Per-grain processing:**
  - a filter on every grain, which users ask for by name ("a filter at the end of the chain is pretty useless");
  - per-grain crushing, feedback and comb filters tuned to a scale: Efx Fragments, Absynth's Aetherizer;
  - pan and spatial trajectories: Crusher-X.
- **Pitch:**
  - grain pitch quantised to a scale after modulation (Quanta 2);
  - chords (Phase Plant);
  - octaves for shimmer;
  - formant shift: Padshop, Alchemy;
  - root detection: Quanta 2's is only "an attempt", and samples rarely sit on C.
- **Spectral modes:** Bitwig 6.1 (formant shift, harmonic quantise), Reason's Spectral Grains (partials snapped to harmonics), Padshop's spectral oscillator, Clouds' phase vocoder.
- **Displays:** each grain drawn as it plays (Absynth draws pitch against time); emitters on a canvas (Tempera, Borderlands).

**What users praise and complain about:**
- **Praised:** dense, smooth clouds ("smooth instead of glitchy"), per-grain filters, jitter on most controls, the sample kept inside the preset.
- **Complaints:**
  - CPU (Omnisphere's manual warns; Granulator III drops features for it; Serum 2 is "more CPU-hungry than ever");
  - clicks, from grains running past the file's end or envelopes too short;
  - UIs hard to read, with modulation you can't see;
  - single-sample engines;
  - samples out of tune;
  - no live buffer, or one that loses its captures.

**What nobody does well yet** (the openings this spec takes):
1. Dense clouds cheaply. Every instrument here caps or trims its grains for CPU (Granulator's Cloud: 20 per channel).
2. Live granulation that keeps what you capture, as part of the song.
3. Grains that follow the song: its key, its tempo, its position, so a texture renders the same every time and moves with the arrangement.
4. Every grain visible as it plays, both channels, both sources.
5. Tuning that's right on loading: the root found, and grains landing on the key.

## Beyond Live

| Addition | Reference | Why it's worth it | Cost |
|---|---|---|---|
| **Two sources** (files, the Library's instruments, clips, or the live input) mixed per grain (G2) | Pigments, Grainferno, EmissionControl2 | One source is the most named limit; two give cross-textures and live-against-sample | Moderate |
| **Six modes:** Granulator's three, plus Pitched, Rhythm and, in step 3, Spectral (G3) | Alchemy's pitched grains, Padshop, Beads' clocked mode, the spectral modes | Tones from any sound, rhythmic glitch, and extreme stretches | Moderate; Spectral large |
| **Per-grain filter and colour** (crush, ring), each free when off (G4) | Absynth, Efx Fragments, users' first request | Movement inside the texture rather than over it | Moderate, kept cheap by fixed coefficients per grain |
| **Pitch that lands:** the root found, quantise to the song's key, chords, octaves and fifths, spray up or down, glide within a grain (G5) | Quanta 2, Phase Plant, Meld | Clouds in tune with the song, shimmer, glissandi | Small |
| **Live input that's kept:** any track through the sidechain, a rolling buffer, Hold on notes, feedback, captures saved as files in the project (G6) | Granulator III, Beads, Emergence | Live granulation and granular delays without losing what you caught | Moderate |
| **Position that follows the song:** a playhead locked to the song, snapping to transients and beats, a position by key (G7) | None does the first | A texture that's part of the arrangement and renders identically | Small |
| **A full modulation system:** 3 envelopes, 4 drawn LFOs, 2 random sources (one can roll per grain), 8 macros, a 32-route matrix, drag to modulate, Fire (G8) | The Wavetable's, which users liked | Granulator III's fixed short lists are its weakest part | Moderate: the Wavetable's UI kit, already built |
| **CPU first:** a grain kernel built for SIMD, a source pyramid, a fixed budget, and Quality and CPU in the header (G10–G13) | Clouds' and Beads' engines, measured here | Dense clouds where others cap them | Moderate, and the reason the rest is affordable |

## Stages

Built in three steps within P8, each built, tested and signed off on its own, as the Wavetable's were (G18). Nothing is released between them, so the parameter list below is approved whole now.

| Step | Scope |
|---|---|
| 1. The instrument | Sources from files, the Library's instruments and clips; Classic, Loop, Cloud, Pitched and Rhythm; position, scan, playheads, snap; grains, windows, sprays; pitch with the root found and quantise; the grain filter and colour; the voice filters, envelopes, LFOs, random sources, macros, matrix and drag to modulate; voices and MPE; the panel on three pages with the source display; Quality, Max Grains, CPU and grain count; the SIMD kernel and its benchmarks; factory presets; assistants' tools |
| 2. Live and the source editor | The live input from the sidechain, its modes, feedback and Capture; the Source editor in the detail panel ([ADR 0053](../../docs/decisions/0053-a-modules-editor-in-the-detail-panel.md)); Clip ▸ Make Granular; the live presets |
| 3. Spectral | The Spectral mode: freeze, smear, harmonics, clean, formant, with its own CPU budget |
| Later | More than two sources; per-grain spatial paths and outputs beyond stereo; microtuning, with a tuning for the whole DAW |

## Signal flow

```
note (MPE: per-note bend, pressure, slide)
 └─ voice (up to 16) ───────────────────────────────────────────────────────────────────┐
     scheduler (per block, per voice): the mode decides when grains start                │
       Classic · Loop · Cloud · Pitched · Rhythm · Spectral (step 3) · Fire             │
       at each start, every grain parameter samples its modulation once:                 │
       source (1 or 2) · place (position + scan + key + spray, snapped) · size · window │
       · pitch (root, transpose, key track, spray, quantise, glide) · pan · level ·     │
       filter coefficients · colour                                                       │
             ▼                                                                           │
     grain pool (up to Max Grains, a structure of arrays) ── each grain, over the block: │
       pass 1: fetch from the source pyramid's level for its speed (scalar, contiguous)  │
       pass 2: interpolate × window × colour → grain filter (8 grains a vector) → pan   │
             ▼  summed into the voice's stereo bus (density gain)                       │
     Filter 1 ─▶ Filter 2 (Routing: serial … parallel) ─▶ Amp (Env 1, velocity)          │
             ▼                                                                           │
 voice sum ─▶ Width ─▶ Volume ─▶ out ──▶ (Feedback ─▶ live buffer)                         │
live input (sidechain) ─▶ gain ─▶ rolling buffer + its pyramid (Rolling · Hold · Frozen) │
modulation, per voice on the 32-sample control grid: Env 1–3, LFO 1–4, Random 1–2,      │
Macro 1–8, velocity, note, pressure, slide, bend, mod wheel, Grain Random,               │
Grain Alternate, Scan, Input Level → 32 routes ──────────────────────────────────────────┘
```

## Parameters

State version 1. Every parameter is automatable except where noted. Families use `<n>`. **Per grain** in the Smoothing column means the value is read once at each grain's start and holds for that grain, which is both the cheap way and the granular one: a change is heard as the grains turn over, never as a zipper.

**Sources** (`src<n>`, n = 1–2):

| ID (permanent) | Name | Description | Range | Default | Unit | Curve | Smoothing |
|---|---|---|---|---|---|---|---|
| `src<n>.on` | Source n On | Turns the source on or off; with one off, every grain comes from the other. | Off / On | On (1), Off (2) | toggle | – | 5 ms fade |
| `src<n>.input` | Source n Input | Sample plays the source's audio; Live plays what the input buffer holds. | Sample, Live | Sample | choice | – | per grain |
| `src<n>.start` | Source n Start | Where the part of the source that grains come from begins. | 0 … 100 | 0 | % | linear | per grain |
| `src<n>.end` | Source n End | Where that part ends. | 0 … 100 | 100 | % | linear | per grain |
| `src<n>.gain` | Source n Gain | The source's level, after it's brought to full scale on loading. | −inf … +12 | 0 | dB | dB | per grain |
| `src<n>.tune` | Source n Tune | Shifts the source by semitones, from the root it was found or set at. | −48 … +48 | 0 | st | stepped | per grain |
| `src<n>.fine` | Source n Fine | Tunes the source slightly. | −100 … +100 | 0 | ct | linear | per grain |
| `src.mix` | Source Mix | How many grains come from Source 2: none at 0 %, all at 100 %. | 0 … 100 | 50 | % | linear | per grain |
| `src.pick` | Source Pick | Random picks each grain's source by the mix; Turns alternates them; Both plays every grain from each. | Random, Turns, Both | Random | choice | – | per grain |

**Live input** (`live`, step 2):

| ID (permanent) | Name | Description | Range | Default | Unit | Curve | Smoothing |
|---|---|---|---|---|---|---|---|
| `live.mode` | Live Mode | Rolling keeps recording; Hold stops while notes play, so they play what came before; Frozen stops recording. | Rolling, Hold, Frozen | Rolling | choice | – | 5 ms fade |
| `live.length` | Live Length | How many seconds of the input the buffer keeps. | 0.5 … 30 | 8 | s | centre 4 s | – |
| `live.gain` | Live Gain | The input's level into the buffer. | −inf … +24 | 0 | dB | dB | 20 ms |
| `live.feedback` | Feedback | Sends the module's own output back into the buffer, for trails that build on themselves. | 0 … 100 | 0 | % | linear | 20 ms |

**Position and scan:**

| ID (permanent) | Name | Description | Range | Default | Unit | Curve | Smoothing |
|---|---|---|---|---|---|---|---|
| `pos` | Position | Where grains come from: across the source's part, or how far back in the live buffer. | 0 … 100 | 0 | % | linear | per grain |
| `pos.spray` | Position Spray | How far each grain's start strays from the position, at random. | 0 … 100 | 0 | % | centre 10 % | per grain |
| `pos.dist` | Spray Shape | Even spreads grains over the spray; Near keeps most close to the position; Ahead strays only forward. | Even, Near, Ahead | Even | choice | – | per grain |
| `pos.key` | Position Key | How far each note moves the position, so notes play different places in the source. | −100 … +100 | 0 | % | linear | per grain |
| `pos.snap` | Snap | Starts grains on the source's transients, its beats or its zero crossings. | Off, Transients, Beats, Zero Crossings | Off | choice | – | per grain |
| `scan.rate` | Scan | How fast the position moves through the source: 100 % at the recording's own speed, 0 holds still, below zero backwards. | −400 … +400 | 0 | % | finer near 0 | 20 ms |
| `scan.sync` | Scan Sync | Locks the scan to the song: one pass through the source's part in this many note values. | Off, 1/4 … 32 bars, with dotted and triplet values | Off | choice | – | – |
| `scan.end` | Scan End | At the end of the part, Wrap starts over, Bounce turns back, Stop stays there. | Wrap, Bounce, Stop | Wrap | choice | – | – |
| `scan.from` | Playhead | Per Note scans from the position on each note; Shared runs one scan for every note; Song follows the song's position. | Per Note, Shared, Song | Per Note | choice | – | – |

**Grains:**

| ID (permanent) | Name | Description | Range | Default | Unit | Curve | Smoothing |
|---|---|---|---|---|---|---|---|
| `grain.mode` | Mode | How grains start: overlapping (Classic), as loops, scattered (Cloud), at the note's pitch, on the beat, or as spectra. | Classic, Loop, Cloud, Pitched, Rhythm, Spectral | Classic | choice | – | crossfade |
| `grain.size` | Size | How long each grain lasts; in Loop, how long each loop is. | 1 … 4 000 | 100 | ms | centre 100 ms | per grain |
| `grain.sizesync` | Size Sync | Sets each grain's length in note values instead. | Off, 1/128 … 1 bar, with dotted and triplet values | Off | choice | – | per grain |
| `grain.overlap` | Overlap | In Classic, how many grains sound over each other. | 1 … 16 | 2 | – | log | per grain |
| `grain.density` | Density | In Cloud, how many grains start each second. | 0.5 … 2 000 | 40 | /s | log | per grain |
| `grain.rate` | Rate | In Rhythm, how often grains start, in note values. | 1/128 … 4 bars, with dotted and triplet values | 1/16 | choice | – | – |
| `grain.periods` | Periods | In Pitched, how long each grain lasts, in cycles of the note. | 1 … 64 | 4 | – | log | per grain |
| `grain.formant` | Formant | In Pitched and Spectral, moves the sound's character up or down without changing the note. | −24 … +24 | 0 | st | linear | per grain |
| `grain.xfade` | Crossfade | In Loop, how much of each loop fades into the next. | 0 … 50 | 10 | % | linear | per grain |
| `grain.jitter` | Jitter | Moves each grain's start in time a little, at random, so a steady stream loosens. | 0 … 100 | 0 | % | linear | per grain |
| `grain.chance` | Chance | How likely each grain is to play: lower leaves gaps. | 0 … 100 | 100 | % | linear | per grain |
| `grain.repeats` | Repeats | Plays each grain this many times in a row, for stutters. | 1 … 16 | 1 | – | stepped | per grain |
| `grain.swing` | Swing | In Rhythm, delays every second grain. | 0 … 75 | 0 | % | linear | per grain |
| `grain.fire` | Fire | Starts a grain the moment it rises past half, in any mode: route a modulator to it for grains on your own rhythm. | 0 … 100 | 0 | % | linear | – |
| `grain.window` | Window | The shape each grain fades in and out with. | Hann, Gaussian, Triangle, Sharp Attack, Sharp End, Square, Drawn | Hann | choice | – | per grain |
| `grain.skew` | Skew | Moves the window's peak earlier or later. | −100 … +100 | 0 | % | linear | per grain |
| `grain.flat` | Flatness | How much of each grain plays at full level. | 0 … 100 | 0 | % | linear | per grain |
| `grain.reverse` | Reverse | How likely each grain is to play backwards. | 0 … 100 | 0 | % | linear | per grain |
| `grain.sizespray` | Size Spray | How much each grain's length varies, at random. | 0 … 100 | 0 | % | linear | per grain |
| `grain.levelspray` | Level Spray | How much each grain's level varies, at random. | 0 … 100 | 0 | % | linear | per grain |
| `grain.even` | Even | Raises grains from quiet parts of the source, so a dynamic recording makes a steady texture. | 0 … 100 | 0 | % | linear | per grain |
| `grain.comp` | Density Gain | Keeps the level steady as more grains overlap. | Off / On | On | toggle | – | 20 ms |
| `grain.max` | Max Grains | The most grains a note plays at once: fewer saves CPU. Not automatable. | 8 … 256 | 64 | – | log | – |

**Pitch:**

| ID (permanent) | Name | Description | Range | Default | Unit | Curve | Smoothing |
|---|---|---|---|---|---|---|---|
| `pitch.transpose` | Transpose | Shifts every grain by semitones. | −48 … +48 | 0 | st | stepped | per grain |
| `pitch.fine` | Fine | Tunes every grain slightly. | −100 … +100 | 0 | ct | linear | per grain |
| `pitch.key` | Key Track | How much the note played sets the pitch: at 0 every note plays the source as it is. | 0 … 100 | 100 | % | linear | per grain |
| `pitch.spray` | Pitch Spray | How far each grain's pitch strays, at random. | 0 … 48 | 0 | st | centre 12 st | per grain |
| `pitch.dir` | Spray Direction | Strays both ways, only up, or only down. | Both, Up, Down | Both | choice | – | per grain |
| `pitch.quant` | Quantize | Lands each grain's pitch on semitones, the song's key, octaves, octaves and fifths, or a chord. | Off, Semitones, Song Key, Octaves, Fifths, Chord | Off | choice | – | per grain |
| `pitch.chord` | Chord | The chord grains land on when Quantize is Chord. | Major, Minor, Sus 2, Sus 4, Major 7, Minor 7, Dominant 7, Major 9, Minor 9, Power, Key Chord | Major | choice | – | per grain |
| `pitch.glide` | Grain Glide | Slides each grain's pitch by this much over its life. | −24 … +24 | 0 | st | linear | per grain |

**Stereo:**

| ID (permanent) | Name | Description | Range | Default | Unit | Curve | Smoothing |
|---|---|---|---|---|---|---|---|
| `pan.spray` | Pan Spray | How far grains spread across the stereo field. | 0 … 100 | 30 | % | linear | per grain |
| `pan.mode` | Pan Mode | Random places grains anywhere in the spray; Alternate swings them left and right; By Pitch puts higher grains to the right. | Random, Alternate, By Pitch | Random | choice | – | per grain |
| `out.width` | Width | The width of the whole sound: 0 % is mono, 200 % wider than the source. | 0 … 200 | 100 | % | linear | 20 ms |

**Grain filter and colour:**

| ID (permanent) | Name | Description | Range | Default | Unit | Curve | Smoothing |
|---|---|---|---|---|---|---|---|
| `gfilter.on` | Grain Filter | Gives each grain a filter of its own. | Off / On | Off | toggle | – | per grain |
| `gfilter.type` | Grain Filter Type | Low, band or high pass, or a resonator that rings at the cutoff. | LP, BP, HP, Resonator | LP | choice | – | per grain |
| `gfilter.cutoff` | Grain Cutoff | Where each grain's filter acts. | 20 … 20 000 | 2 000 | Hz | log | per grain |
| `gfilter.res` | Grain Resonance | Emphasis at each grain's cutoff. | 0 … 100 | 20 | % | linear | per grain |
| `gfilter.spray` | Cutoff Spray | How far each grain's cutoff strays, at random. | 0 … 6 | 0 | oct | linear | per grain |
| `gfilter.key` | Cutoff Pitch Track | How much each grain's cutoff follows that grain's pitch. | 0 … 100 | 0 | % | linear | per grain |
| `gfilter.snap` | Cutoff to Key | Lands each grain's cutoff on a note of the song's key, for tuned resonances. | Off / On | Off | toggle | – | per grain |
| `gcolor.crush` | Crush | Lowers each grain's resolution and rate, for grit. | 0 … 100 | 0 | % | linear | per grain |
| `gcolor.crushspray` | Crush Spray | How much the crush varies from grain to grain. | 0 … 100 | 0 | % | linear | per grain |
| `gcolor.ring` | Ring | Ring-modulates each grain, for metallic and bell-like tones. | 0 … 100 | 0 | % | linear | per grain |
| `gcolor.ratio` | Ring Ratio | The ring's frequency against each grain's pitch. | 0.25 … 16 | 2 | × | log | per grain |
| `gcolor.ringspray` | Ring Spray | How much the ring's frequency varies from grain to grain. | 0 … 100 | 0 | % | linear | per grain |

**Filters** (`filter<n>`, n = 1–2) and the routing, as the Wavetable's. Filter 1 starts open (LP 24 at 20 000 Hz), filter 2 off:

| ID (permanent) | Name | Description | Range | Default | Unit | Curve | Smoothing |
|---|---|---|---|---|---|---|---|
| `filter<n>.on` | Filter n On | Turns the filter on or off. | Off / On | On (1), Off (2) | toggle | – | 5 ms fade |
| `filter<n>.type` | Filter n Type | The filter's kind: low, high or band pass, notch, peak, ladder or diode, comb, formant or phaser. | LP 12, LP 24, Ladder, Diode, HP 12, HP 24, BP 12, BP 24, Notch, Peak, Comb +, Comb −, Formant, Phaser | LP 24 (1), HP 12 (2) | choice | – | crossfade |
| `filter<n>.cutoff` | Filter n Cutoff | Where the filter acts: for a low pass, lower is darker. | 20 … 20 000 | 20 000 (1), 100 (2) | Hz | log | 20 ms multiplicative |
| `filter<n>.res` | Filter n Resonance | Emphasis at the cutoff; near the top the ladder and diode filters start to sing on their own. | 0 … 100 | 20 | % | linear | 20 ms |
| `filter<n>.drive` | Filter n Drive | Pushes the filter harder, for warmth and grit. | 0 … 100 | 0 | % | linear | 20 ms |
| `filter<n>.shape` | Filter n Shape | The type's own setting: the formant's vowel, the comb's damping, the phaser's spread. | 0 … 100 | 50 | % | linear | 20 ms |
| `filter<n>.key` | Filter n Key Track | How much the cutoff follows the note played: at 100 % it rises an octave per octave. | 0 … 100 | 50 | % | linear | 20 ms |
| `filter<n>.env` | Filter n Env | How far Env 2 moves the cutoff, up or down. | −100 … +100 | 0 | % | linear | 20 ms |
| `filters.routing` | Filter Routing | Filter 1 into filter 2 at 0 %, the two side by side at 100 %, or anywhere between. | 0 … 100 | 0 | % | linear | 20 ms |

**Amp and envelopes** (`env<n>`, n = 1–3): Env 1 is the amp, Env 2 moves the filters, Env 3 is free. The amp's defaults suit a pad:

| ID (permanent) | Name | Description | Range | Default | Unit | Curve | Smoothing |
|---|---|---|---|---|---|---|---|
| `amp.vel` | Velocity | How much harder playing makes notes louder. | 0 … 100 | 50 | % | linear | – |
| `env<n>.a` | *Env* Attack | How long the envelope takes to rise. | 0 … 20 000 | 50 (1), 2 (2, 3) | ms | centre 50 ms | – |
| `env<n>.h` | *Env* Hold | How long the envelope stays at its peak before it falls. | 0 … 10 000 | 0 | ms | centre 100 ms | – |
| `env<n>.d` | *Env* Decay | How long the envelope takes to fall to its sustain level. | 1 … 20 000 | 1 000 (1), 400 (2), 300 (3) | ms | centre 300 ms | – |
| `env<n>.s` | *Env* Sustain | The envelope's level while the key stays down. | 0 … 100 | 100 (1), 0 (2, 3) | % | linear | 20 ms |
| `env<n>.r` | *Env* Release | How long the envelope takes to fall after the key is released. | 1 … 20 000 | 800 (1), 300 (2, 3) | ms | centre 300 ms | – |
| `env<n>.acurve` | *Env* Attack Curve | How the attack rises: below zero it starts slowly, above zero quickly. | −100 … +100 | 30 | % | linear | – |
| `env<n>.dcurve` | *Env* Decay Curve | How the decay falls: below zero it starts slowly, above zero quickly. | −100 … +100 | 60 | % | linear | – |
| `env<n>.rcurve` | *Env* Release Curve | How the release falls: below zero it starts slowly, above zero quickly. | −100 … +100 | 60 | % | linear | – |
| `env<n>.mode` | *Env* Mode | Normal; Loop repeats attack to decay while the key is held; One-shot plays through and ignores the release. | Normal, Loop, One-shot | Normal | choice | – | – |

**LFOs** (`lfo<n>`, n = 1–4), each with a drawn shape, and **random sources** (`rnd<n>`, n = 1–2), as the Wavetable's, with one more mode, **Per Grain**: a new value for each grain, the same for every target of that grain, so a grain that goes higher can also go brighter and further right:

| ID (permanent) | Name | Description | Range | Default | Unit | Curve | Smoothing |
|---|---|---|---|---|---|---|---|
| `lfo<n>.rate` | LFO n Rate | How fast the LFO moves when it isn't synced to the tempo. | 0.01 … 100 | 2 | Hz | log | 20 ms multiplicative |
| `lfo<n>.sync` | LFO n Sync | Locks the LFO to the song's tempo, in note values. | Off, 1/64 … 8 bars, with dotted and triplet values | Off | choice | – | – |
| `lfo<n>.mode` | LFO n Mode | Free keeps running; Retrigger restarts it with each note; Envelope plays its shape once per note. | Free, Retrigger, Envelope | Retrigger | choice | – | – |
| `lfo<n>.phase` | LFO n Phase | Where in its shape the LFO starts. | 0 … 360 | 0 | ° | linear | – |
| `lfo<n>.smooth` | LFO n Smooth | Rounds off the shape's corners, so steps glide. | 0 … 100 | 0 | % | linear | 20 ms |
| `lfo<n>.fade` | LFO n Fade In | How long the LFO takes to fade in after each note starts. | 0 … 10 000 | 0 | ms | centre 500 ms | – |
| `lfo<n>.poly` | LFO n Per Note | On, each note has its own LFO; off, one LFO moves every note together. | Off / On | On | toggle | – | – |
| `rnd<n>.mode` | Random n Mode | Steps holds, Smooth glides, Chaos wanders; Per Note picks a value for each note, Per Grain one for each grain. | Steps, Smooth, Chaos, Per Note, Per Grain | Smooth | choice | – | – |
| `rnd<n>.rate` | Random n Rate | How often a new value comes when it isn't synced to the tempo. | 0.01 … 100 | 4 | Hz | log | 20 ms multiplicative |
| `rnd<n>.sync` | Random n Sync | Locks the new values to the song's tempo, in note values. | as LFO Sync | Off | choice | – | – |
| `rnd<n>.poly` | Random n Per Note | On, each note has its own values; off, every note follows the same ones. | Off / On | On | toggle | – | – |
| `macro<n>` | Macro n | A knob of your own: route it to any settings to move them together. | 0 … 100 | 0 | % | linear | 20 ms |

**The matrix** (`mod<n>`, n = 1–32), as the Wavetable's:

| ID (permanent) | Name | Description | Range | Default | Unit | Curve | Smoothing |
|---|---|---|---|---|---|---|---|
| `mod<n>.src` | Mod n Source | What drives this route: an envelope, an LFO, a macro, how the note is played, or the grain itself. | None, then every source below | None | choice | – | – |
| `mod<n>.dst` | Mod n Target | What this route changes. | None, then every target below | None | choice | – | – |
| `mod<n>.amt` | Mod n Amount | How much, and in which direction, this route moves its target. | −100 … +100 | 0 | % | linear | 20 ms |
| `mod<n>.curve` | Mod n Curve | Bends the route's response: below zero small moves of the source count for less, above zero for more. | −100 … +100 | 0 | % | linear | – |
| `mod<n>.aux` | Mod n Scale | Another source that scales this route's amount, such as the mod wheel or velocity. | None and the sources above | None | choice | – | – |

- **Sources,** in this order (permanent): None; Env 1, 2, 3; LFO 1 … 4; Random 1, 2; Macro 1 … 8; Velocity, Note, Pressure, Slide, Bend, Mod Wheel; Grain Random (a fresh bipolar value for each grain); Grain Alternate (+1 and −1 by turns); Scan (the playhead's place in the part, 0 to 1); Input Level (how loud the live input is, followed).
- **Targets,** in this order (permanent):
  - None; Pitch; Position, Position Spray, Scan;
  - Size, Size Spray, Overlap, Density, Periods, Formant, Crossfade, Jitter, Chance, Swing, Fire;
  - Skew, Flatness, Reverse, Level Spray, Even; Pitch Spray, Grain Glide; Pan, Pan Spray, Width;
  - Source Mix; for each source, Start, End, Gain, Tune;
  - Grain Cutoff, Grain Resonance, Cutoff Spray, Crush, Ring, Ring Ratio;
  - for each filter, Cutoff, Resonance, Drive, Shape; Filter Routing; Amp, Volume;
  - Live Gain, Feedback; LFO 1 … 4 Rate; Random 1, 2 Rate;
  - Smear, Harmonics, Clean (step 3).
  
  Pitch moves ±24 semitones at 100 %, cutoffs ±5 octaves, the rest their whole range.
- **Grain targets read their modulation once, at each grain's start** (the Smoothing column's "per grain"): an LFO on Pitch spreads pitches across the grains it starts, rather than bending grains already playing. Voice targets (the filters, Amp, Volume, Width) move per sample from the 32-sample control grid, as the Wavetable's do.
- **Routes are made in the matrix, or by dragging** a source onto a control; both set these parameters, so assistants and hosts see every route.

**Voice and output:**

| ID (permanent) | Name | Description | Range | Default | Unit | Curve | Smoothing |
|---|---|---|---|---|---|---|---|
| `voice.mode` | Voice Mode | Poly plays chords; Mono plays one note at a time; Legato is mono without restarting the envelopes when notes overlap. | Poly, Mono, Legato | Poly | choice | – | – |
| `voice.count` | Voices | How many notes can sound at once. | 1 … 16 | 8 | – | stepped | – |
| `voice.glide` | Glide | How long the pitch takes to slide from one note to the next. | 0 … 2 000 | 0 | ms | centre 200 ms | – |
| `voice.glidemode` | Glide Mode | Always slides; Legato slides only between overlapping notes. | Always, Legato | Always | choice | – | – |
| `voice.bend` | Bend Range | How far the pitch bend wheel bends, in semitones. | 0 … 48 | 2 | st | stepped | – |
| `voice.latch` | Latch | Keeps notes sounding after you let go, until you play the next; for drones. | Off / On | Off | toggle | – | – |
| `voice.tails` | Grain Tails | When a note ends, its grains finish playing; off, they fade with the amp envelope. | Off / On | On | toggle | – | – |
| `out.volume` | Volume | The instrument's output level. | −inf … +6 | −6 | dB | dB | 20 ms |
| `quality` | Quality | Eco saves CPU; High is cleanest for grains pitched far from the source. Not automatable. | Eco, Normal, High | Normal | choice | – | – |

**Spectral** (step 3; the Spectral mode's own settings):

| ID (permanent) | Name | Description | Range | Default | Unit | Curve | Smoothing |
|---|---|---|---|---|---|---|---|
| `spec.fft` | Resolution | How finely the spectrum is cut: higher sounds smoother, lower keeps attacks sharper. Not automatable. | 1 024, 2 048, 4 096, 8 192, 16 384 | 4 096 | – | – | – |
| `spec.smear` | Smear | Blurs each partial's phase, for washes and extreme stretches. | 0 … 100 | 0 | % | linear | per frame |
| `spec.harm` | Harmonics | Pulls the partials onto the note's harmonics. | 0 … 100 | 0 | % | linear | per frame |
| `spec.clean` | Clean | Keeps only the pitched partials, leaving the noise behind. | 0 … 100 | 0 | % | linear | per frame |

- **Quality** and **Max Grains** are CPU settings, so neither is automatable. Quality is a header setting; Max Grains sits in VOICE.
- **337 parameters** (G17):
  - 16 for the sources, 4 for the live input, 9 for position and scan;
  - 23 for the grains, 8 for pitch, 3 for stereo, 12 for the grain filter and colour;
  - 17 for the filters, 28 for the envelopes and velocity, 28 for the LFOs, 8 for the random sources, 8 macros, 160 for the matrix;
  - 9 for voice and output, 4 for Spectral.

**State that isn't a parameter** ([ADR 0017](../../docs/decisions/0017-heavy-data-and-non-parameter-state.md)):
- **Each source:**
  - its kind: a file, a multisample (an SFZ, the Library's instruments), a clip's part of a file, or a capture;
  - its reference: `library:` for the Library's, a project-relative path for the project's files, an absolute one otherwise;
  - its root: found, or set by hand.
- **Captures** (step 2): written as WAV files into the project's folder (Captures, [ADR 0030](../../docs/decisions/0030-projects-are-folders.md)) and referenced like any file. In the plugins, into `Documents/Segno/Captures`.
- **The drawn window,** the LFOs' shapes, the macros' names.
- **The roll kept,** when Keep holds one (G9).
- **A preset with a source of your own** copies the file beside the preset, so it always plays, as the Wavetable's presets carry their tables. The Library's sources stay references.

## How the modes start grains

All six share one grain pool and one renderer; only the scheduler differs.

| Mode | When a grain starts | Its length | What sets the pitch |
|---|---|---|---|
| **Classic** | Steadily, Overlap grains per Size (Jitter loosens it) | Size | The read speed: root, note (Key Track), Transpose |
| **Loop** | When the last loop reaches its crossfade | Size (the loop), Crossfade | The read speed |
| **Cloud** | At random, Density per second (Poisson) | Size | The read speed |
| **Pitched** | Once per period of the note (an emission rate in the audio range) | Periods × the period | The emission rate; the read speed sets only the formant |
| **Rhythm** | On the song's grid at Rate, with Swing and Chance; Repeats for bursts | Size | The read speed |
| **Spectral** (step 3) | One frame per hop, resynthesised | Resolution | Partials moved by the note, Harmonics |
| **Fire** (any mode) | When `grain.fire` rises past 50 % | Size | As the mode's |

- **Pitched is "a sample as an oscillator":** grains started at the note's frequency and aligned to the source's pitch periods (found when it loads) give a tone at any note with the source's character, and Formant moves the character without moving the note.
- **Rhythm and Song playheads are locked to the song's position**, so a pattern lands on the same beats every time and a bounce matches what played.

## DSP design

- **Sources:** decoded off the audio thread by the shared decoder (`libs/audiofile`), built into a **pyramid**, analysed, and handed over with `AssetHandoff` (ADR 0017). They're shared between instances that load the same file. Granular has its own source code: it uses the shared decoder and decimator, never the Sampler's code, which main's Drum Kit builds on.
  - **The pyramid:** level 0 at the file's rate, each next level halved with `libs/dsp`'s `decimateByTwo` (a linear-phase low-pass keeping 0.41 of the new rate). A grain playing faster than the source reads the level where it advances at most one sample per output sample, so nothing aliases. The level is chosen once, at the grain's start, from its speed's exponent.
  - **Oversampled levels:** each level is also kept at twice its rate (G12), so a cheap 4-point interpolator reaches about 66 dB of image rejection. This applies to sources up to 2 minutes; longer ones keep the plain pyramid.
  - **Multisamples:** each zone gets its pyramid; a note's grains read the zone the Sampler would choose for it. The SFZ is read with the Sampler's reader, used as it is.
- **What's found in a source, in the background, as the audio editor finds a clip's:**
  - the overview for the display;
  - a level map (RMS per 64 frames), so silent grains are skipped and Even can lift quiet ones;
  - transients and the tempo with its beats (`libs/dsp`'s onsets and tempo);
  - zero crossings;
  - the pitch and its period marks (`libs/dsp`'s pitch detector), for the root and for Pitched;
  - the key (`libs/dsp`'s pitch classes).
  
  The root comes from the file's metadata, then its name, then its sound, as the audio editor finds a clip's key; it's shown with "found" and set by hand in one click.
- **Live input** (step 2):
  - a power-of-two ring buffer, sized in `prepare()` for 30 s at the session's rate, with a mirrored guard so interpolation never wraps;
  - half-band decimators cascade off the input to fill its pyramid as it records;
  - a grain reading the buffer is held behind the write head by its own reach (size × speed), so it never reads what isn't written;
  - Feedback adds the module's output into the buffer through a soft clip that bounds the loop and a DC blocker, as the Delay's feedback path does (`libs/dsp`'s `FeedbackPath`).
- **Positions are 32.32 fixed point** and increments are fixed at a grain's start (Grain Glide adds a constant change of increment per sample). A position is exact in a source of any length; floats lose sample accuracy past about 6 minutes.
- **Windows** are polynomials in the grain's phase, not tables: Hann as the square of an even polynomial (four multiply-adds, error under 2 × 10⁻⁵), Gaussian through a vector exponential, Triangle and the sharp ones piecewise, Flatness as a flat top, Skew as a warp of the phase chosen at the start. They vectorise, need no lookup, and end exactly at zero, so grains never click. Only the Drawn window reads a table.
- **Grain filter:** the zero-delay-feedback state-variable filter with coefficients computed once at the grain's start (cutoff after its spray, pitch tracking and key snap). Resonator is its band pass at high Q, gain-compensated. Its state is cleared at the start.
- **Colour:** Crush holds samples and quantises them by an amount fixed per grain; Ring multiplies by a sine at the grain's pitch × Ratio, computed by the same polynomial as the windows. Each costs nothing at 0.
- **Pan:** gains computed once per grain (equal power), a 2 × 2 matrix for a stereo source; Width as mid and side on the voice sum.
- **Density gain:** the voice sum is scaled by 1/√N of the grains overlapping on average (from Size and the start rate), so a cloud of 200 grains sits at the level of a few.
- **Voice filters, envelopes, LFOs, random sources:** `libs/dsp`'s `VoiceFilter`, `CurvedEnvelope`, `ShapeLfo` and `RandomSource`, as the Wavetable uses them. Granular evaluates its matrix in its own code, built on them; the Wavetable's stays as it is (G20).
- **Spectral** (step 3): a short-time Fourier transform of the source at Resolution with a quarter hop. Frames are read at the scan's position, their magnitudes kept, and their phases advanced, or randomised by Smear (as an extreme stretch does). Partials are pulled onto the note's harmonics by Harmonics and thinned to the pitched ones by Clean, the spectral envelope moved by Formant, then resynthesised by overlap-add. One transform per voice, shared by its grains.
- **Real time:** every buffer is allocated in `prepare()`. A source changing arrives by handoff and crossfades over 20 ms. No locks; the analysis runs off the audio thread.
- **Latency:** none. Grains read a source that's already there, and the live buffer reads behind its head. **Tail:** the release plus the longest grain (Grain Tails), and the feedback's ringing.

## Performance (CPU first)

The product owner's priority. Every choice above is also a cost choice, and these are the rules and the budgets the build is held to.

**How a grain is rendered: one grain over the block, in two passes.** For each active grain:
1. **Pass 1** (scalar) copies the interpolator's taps for each output sample from the grain's level into a small buffer: one contiguous 16-byte read per sample, never a gather.
2. **Pass 2** (SIMD across the samples) interpolates, multiplies by the window, colour and level, filters, and adds into the voice's stereo bus.

Each grain's state stays in registers for the whole block. Measured on this laptop (Ryzen AI 9 HX 370, clang 21, one core; research for this spec, not yet in the repository), in cycles per grain per sample:

| Loop | SSE2 | AVX2 |
|---|---|---|
| Every grain per sample (the usual way) | 12–16 | 12–14 |
| One grain over the block, one pass | 8–12 | 6–9 |
| **One grain over the block, two passes** (linear / 4-point) | **5 / 7** | **3 / 5** |
| The same with AVX2 gathers instead of pass 1 | – | 7 |

**The rules:**
- **Nothing per sample that can be per grain.** Randomness, modulation, level choice, pan gains, filter coefficients, window and colour settings are computed once, at a grain's start: a few hundred cycles, under 1 % of the work.
- **Random numbers are hashed, not run:** a counter-based hash of the roll, the note and the grain's number (G9). No per-sample random draws, no state to carry, and a kept roll renders exactly.
- **Onsets are scheduled once per block per voice,** each with its sub-sample offset carried into the grain's first position and window phase, so a synchronous stream keeps its exact rate (Pitched depends on it).
- **The grain filter runs eight grains per vector,** across grains rather than along one, since a filter's next sample waits on its last (estimated 1–2 cycles per grain per sample instead of 8).
- **Silent work is skipped:**
  - a grain over a part of the source below −96 dB (by the level map) isn't started;
  - a voice with no grains and its gate off sleeps;
  - with no notes, only the live buffer (if any) and the display's data run;
  - the grain filter, colour, Width and Density Gain cost nothing when off or neutral.
- **SIMD without a new library:**
  - pass 2 is written as plain loops the compiler vectorises, checked in the build (clang's vectorisation remarks);
  - the kernel is also compiled for x86-64-v3 (AVX2, FMA) and chosen at run time (`target_clones` where the compiler has it, a dispatch on the CPU's features otherwise), since an SSE2-only build costs about 1.5×;
  - NEON on ARM64;
  - a SIMD library (xsimd, BSD-3) comes in only if a benchmark shows the plain loops can't get there (G11).
- **No threads inside the module.** The host runs tracks in parallel already, and CLAP's thread pool may break real-time rules.
- **No oversampling:** the grain path is linear, and aliasing is handled at the source by the pyramid.
- **Memory over time:** the oversampled pyramid takes about 4× a source's size in floats. A 16-bit copy saves memory but was slower to read, so it's used only for long sources (G13).

**The grain budget is fixed, never measured** (G10): CPU timing that changes the sound would make renders and tests unrepeatable. A note plays at most Max Grains (8–256, 64 by default), and an instance at most 1 024. When a new grain would pass either:
1. a grain that would be inaudible isn't started;
2. new grains use the cheaper interpolator (Eco's), as Clouds does under load;
3. the grain with the least time left is stolen, with a 2 ms fade;
4. the density thins, Density Gain keeping the level.

**Quality** (a header setting, not automatable):

| | Eco | Normal | High |
|---|---|---|---|
| Interpolation | linear, on the oversampled level | 4-point, optimal for 2× oversampled data | 6-point, 5th order, on the oversampled level |
| Images and aliasing | under −40 dB | under −60 dB | under −90 dB |
| Cost (AVX2, mono) | ~3 cycles | ~5 cycles | ~8 cycles |
| Sources over 2 minutes (plain pyramid) | linear | 8-tap polyphase | 8-tap polyphase |

**The header shows the cost:** the CPU the instance takes (as the Wavetable's does, W13), and the grains playing now ("214 grains").

**Budgets,** checked by Release benchmarks (`[.benchmark]`, the reference machine, 48 kHz; open question B74 sets the final ones):

| Load | Budget (share of one core) |
|---|---|
| 8 notes × 64 grains, mono source, Normal, AVX2 | ≤ 4 % |
| The same, the SSE2 build | ≤ 6 % |
| 8 notes × 64 grains, stereo source, grain filter and colour on | ≤ 10 % |
| 8 notes × 256 grains, stereo source, Normal | ≤ 25 % |
| Pitched, 8 notes at C5 (about 520 grains a second each), stereo | ≤ 8 % |
| Live buffer rolling, no notes | ≤ 0.3 % |
| Idle (a source loaded, no notes, no live input) | ≤ 0.05 % |
| Spectral (step 3), 8 notes at 4 096 | ≤ 15 % |

The display has its own budget: 1 024 grains drawn within the 8 ms frame budget (ui-principles), from a snapshot the audio thread publishes every 30 ms in an analysis slot.

## UI layout

On the module grid ([device-panels.md](../../docs/design/device-panels.md)), on three **pages** ([ADR 0051](../../docs/decisions/0051-pages-for-very-wide-modules.md)): the tabs Grains, Sound and Modulation sit in the header after the name. Sections still collapse. Knobs whose parameter depends on the mode follow it (`ParamKnob::follow`): one knob turns Overlap in Classic, Density in Cloud, Periods in Pitched, Rate in Rhythm.

**Grains** (`grains`):

| Section | Controls |
|---|---|
| `source` SOURCE [7] | **The display** (5 × 5): the selected source's waveform, both channels, its part shaded with handles to drag, Position as a line you drag, the spray as a band, the scan's playhead, and every grain playing as a mark (where it reads, its pitch as its height, its level as its brightness, its source as its colour). A 1 \| 2 switch; Input (Sample \| Live); Mix; Pick; Gain; Tune. Below the display, the source's name with previous and next, opening the browser; a file, a clip or a Library instrument dropped on the display loads it |
| `position` POSITION [4] | Position, Spray, Scan, Position Key (knobs). Scan Sync, Playhead, Scan End, Snap, Spray Shape |
| `grain` GRAIN [5] | Mode. Size (or Size Sync), the mode's knob (Overlap, Density, Periods or Rate), Jitter, Chance, Reverse. Window, with the window drawn small and dragged for Skew and Flatness; Crossfade, Formant, Repeats, Swing where the mode uses them |
| `pitch` PITCH [4] | Transpose, Fine, Key Track, Spray (knobs). Glide; Spray Direction, Quantize, Chord |
| `spread` SPREAD [3] | Pan Spray, Size Spray, Level Spray, Even; Pan Mode; Density Gain |

**Sound** (`sound`):

| Section | Controls |
|---|---|
| `gfilter` GRAIN FILTER [4] ⏻ | Type, Cutoff to Key; Cutoff, Resonance, Cutoff Spray, Pitch Track; the response drawn with its spray as a band |
| `gcolor` GRAIN COLOR [3] | Crush, Crush Spray; Ring, Ratio, Ring Spray |
| `filter` FILTER 1 \| 2 [6] ⏻ | As the Wavetable's: the switch, Type, both responses drawn, Cutoff, Resonance, Drive, Shape, Env, Key Track |
| `env` ENVELOPES [6] | Amp \| 2 \| 3, drawn and dragged; the times, sustain, curves, mode; Velocity on Amp |
| `voice` VOICE [3] | Mode, Voices, Glide and its mode, Bend, Latch, Grain Tails, Max Grains, Width, Volume |
| `live` LIVE [3] (step 2; shown while a source is Live) | Mode (Rolling \| Hold \| Frozen), Length, Gain, Feedback; Capture to 1 \| 2; the input's level |

**Modulation** (`modulation`):

| Section | Controls |
|---|---|
| `lfo` LFO [6] | 1 … 4 and Random 1, 2, as the Wavetable's; Random's Per Grain among its modes |
| `matrix` MATRIX [11] | As the Wavetable's: 32 routes, five showing, the rest scrolling |
| `macros` MACROS [4] | Macro 1 … 8, named by you |

| Also | |
|---|---|
| header | The page tabs; the sidechain picker, named **Input**, for the live buffer (ADR 0025: None and the song's tracks in the DAW; Off and External in a plugin); Quality; the grains playing and the CPU |
| meter strip | Output |

- **About 1 400 px wide:** the Sound page's 25 columns. The exact cells are settled with the visual mockup, against the layout lint and screenshots.
- **Drag to modulate** as in the Wavetable: a source's handle dragged onto any control, rings showing the range, hover showing routes, a right-click listing them. Fire is a knob too, so a modulator dragged onto it fires grains.
- **The Source editor** (step 2) is the detail panel's view for this module, named **Source** (ADR 0053).
  - Both sources as full-width waveforms on the shared time-view kit (zoom and scroll), with their parts, the position, the spray and the playheads.
  - The grains drawn large as they play: where they read, their pitch, their pan.
  - The found transients and beats as marks, and the root with its "found".
  - The live buffer rolling under its write head.
  - Its tools on its bar, one click away (audio-editor.md §20, ui-principles.md §1): Load…, Swap, Capture, Freeze, Snap, Root.
- **Everything by mouse** (ADR 0040):
  - the Module menu: Load Source 1…, Load Source 2…, Swap Sources, Edit Source, Capture to Source 1 or 2, Freeze Input, Keep as Played;
  - the Clip menu: Make Granular;
  - View: Next and Previous Module Page.

## Factory presets

Designed to [ADR 0069](../../docs/decisions/0069-factory-presets-are-designed-and-never-clip.md)'s bar, from research into how granular instruments are programmed well: [presets/BRIEF.md](presets/BRIEF.md) holds the research, the set's design, how the presets are played (eight macros in the same roles in every preset, the mod wheel as the "more granular" axis) and the catalogue; `presets/make_recipes.py` writes the recipes. Signed off by ear.
- **Step 1:** 60 from the Library's instruments, in nine families: pads, atmospheres and textures, drones, keys and plucks, leads, basses, rhythm and glitch, effects (risers, stops, freezes), experiments; every mode of step 1 and Fire.
- **Ten factory racks** pair a preset with the Reverb or the Delay and the Reverb (the Library's Instruments), its own macros on the rack beside Space and Echoes, since many granular sounds live in a space (G15).
- Live (step 2): granular delay, freeze, shimmer, stutter, reverse cloud, live chords. Spectral (step 3).

Tags from the shared list. Presets sound finished through the instrument alone; the racks give them their space.

## Automated test targets

| Metric | Target |
|---|---|
| Aliasing: a full-band source, grains pitched up 36 semitones | Under −40 dB (Eco), −60 dB (Normal), −90 dB (High), 48 kHz |
| Images: grains pitched down 24 semitones | The same targets |
| Pitch | Key Track, Transpose and Tune exact within 0.1 cent; Pitched's fundamental within 1 cent of the note, at every octave from C1 to C7 |
| Clicks | No discontinuity from any window but Square, at any size, at a part's edges, at a file's ends, on a source swapped while playing (click score) |
| Reading | No grain reads outside its part, its file, or ahead of the live buffer's write head |
| Level | Density Gain within 1 dB from 1 to 256 overlapping grains (uncorrelated content) |
| Quantize | Every grain's pitch on a note of the key (Song Key), of the chord (Chord) |
| Repeatability | Bit-identical output for any block size (conformance); with a kept roll, identical renders; with fresh rolls, two passes differ |
| Song playhead and Rhythm | Grains start on the song's grid within one sample after a jump |
| Feedback | Bounded at 100 % |
| MPE | A note's bend, pressure and slide move that note only |
| Matrix | Each source and target reaches its documented range; grain targets read once per grain |
| `process()` | No allocation, lock or file access (conformance), with every mode, the live buffer, a capture, a source swap |
| Budget | 1 024 grains at most per instance; over budget, the order of G10 |
| CPU | The budgets above, as Release benchmarks |
| Display | 1 024 grains drawn within the frame budget |
| (step 2) Live | At Position 0 a grain trails the input by at most its own length and one block |
| (step 2) Capture | The file written matches the buffer sample for sample |
| (step 3) Spectral | A held frame at Smear 0 resynthesises within −60 dB of the source frame |

## A/B cases (against the Live target)

| Case | Stimulus | Our settings | Benchmark settings | What to listen for |
|---|---|---|---|---|
| Time-stretch | A sung phrase | Classic, Overlap 2, Scan 25 % | Granulator III Classic, scanning | Smoothness, no flutter, no clicks |
| Cloud pad | Sustained violins | Cloud, 200 grains a second, Pitch Spray 0.2 st, Pan Spray 80 % | Granulator III Cloud, its 20 grains | Density, smoothness, width |
| Loop | A drum loop | Loop, Crossfade 10 %, Reverse 30 % | Granulator III Loop | Seams, character |
| Pitched tone | A vowel | Pitched, Periods 4 | (no counterpart; against Alchemy's pitched grains if at hand) | Tuning, character kept across the keyboard |
| Live (step 2) | A guitar from another track | Live, Rolling, Position 20 %, Cloud | Granulator III capturing the same track | Latency, smearing, level |

## A/B log

Dated listening notes from the product owner, and the measurable checks derived from them.

## Questions for review

| # | Question | Recommendation |
|---|---|---|
| G1 | **How far to go** | Beyond Live, as the product owner asked: Granulator III plus extras, to the depth of the leading granular instruments. [ADR 0065](../../docs/decisions/0065-granular-goes-beyond-live.md) lifts ADR 0011's "instruments stay close to Live's" for this instrument, as ADR 0050 did for the Wavetable. Innovation where the integrated DAW helps (the song's key, tempo and position; clips and tracks as sources; captures kept), and in CPU |
| G2 | **Sources** | Two, each a file, a Library instrument (its zones by note), a clip, or the live input, mixed per grain (Random, Turns, Both). More than two waits for a later stage: two cover cross-textures and live against sample, and each one more costs memory and UI |
| G3 | **Modes** | Six: Classic, Loop and Cloud (Granulator's), Pitched (a sample as an oscillator), Rhythm (on the song's grid), Spectral (step 3). Fire starts grains in any mode |
| G4 | **Per-grain processing** | A filter on every grain (LP, BP, HP, Resonator) with cutoff spray, pitch tracking and key snap; colour (crush, ring) with sprays. Each free when off. No delay or reverb inside (G15) |
| G5 | **Pitch** | The root found on loading (metadata, name, sound) and set in one click; Key Track; spray with a direction; Quantize to semitones, the song's key, octaves, fifths or a chord; glide within a grain; Formant in Pitched and Spectral |
| G6 | **Live input** (step 2) | Any track through the sidechain picker (named Input), or External in a plugin. A rolling buffer of up to 30 s; Rolling, Hold (frozen while notes play: a granular delay that plays what came before) and Frozen; Feedback. Capture writes the buffer into the project as a file, loaded into Source 1 or 2: kept, unlike Granulator III's |
| G7 | **Position and scan** | Scan from −400 % to +400 % for stretches, synced or free; three playheads (Per Note, Shared, Song); snap to transients, beats or zero crossings; Position Key to spread notes across the source |
| G8 | **Modulation** | 3 envelopes, 4 drawn LFOs, 2 random sources (with Per Grain), 8 macros, a 32-route matrix, drag to modulate, as the Wavetable's; grain sources (Grain Random, Grain Alternate, Scan, Input Level). Grain parameters read their modulation at each grain's start |
| G9 | **Randomness** | Follows ADR 0062: every spray, Chance and Jitter rolls again each time the song plays. Module ▸ Keep as Played holds a roll (and Song ▸ Keep All as Played includes it), so a pass you liked renders exactly. The values are hashed from the roll, the note and the grain's number, never run |
| G10 | **CPU** | The rules and budgets of "Performance": one grain over the block in two passes, everything per grain computed once, a fixed budget (Max Grains per note, 1 024 per instance; then the cheaper interpolator, stealing, thinning), skipping silence, Quality, and the CPU and grain count in the header. Budgets checked by Release benchmarks |
| G11 | **SIMD** | Plain loops the compiler vectorises, also compiled for AVX2 and chosen at run time; NEON on ARM64. A SIMD library (xsimd) only if a benchmark shows the need |
| G12 | **Clean pitch** | A pyramid of halved levels, each also kept at twice its rate, so a 4-point interpolator is clean to −60 dB and a 6-point one to −90 dB. No oversampling of the grain path |
| G13 | **Memory and long sources** | Sources up to 2 minutes keep the oversampled pyramid (about 4× their size); longer ones a plain pyramid in 16 bits, read with an 8-tap polyphase interpolator; over 20 minutes, the first 20, said so |
| G14 | **Seeing it** | Every grain drawn as it plays, both channels, both sources, on the panel; the Source editor in the detail panel (step 2) for the big view, parts, roots and the live buffer |
| G15 | **No effects inside** | As the Wavetable (W14): an instrument, its effects in the chain. Factory racks pair Granular with the Reverb or the Delay, since many granular sounds live in a space |
| G16 | **Voices** | 1 to 16 (8); Poly, Mono, Legato; Latch for drones; Grain Tails on, so grains finish after a note |
| G17 | **The size** | 337 parameters, routes included, so hosts, automation and assistants see everything; three pages, about 1 400 px |
| G18 | **Building it** | Three steps (the instrument; live and the source editor; spectral), each built, tested and signed off on its own; a visual mockup before step 1, drawn in every state with the real panel kit, approved before it's built, as racks and modulators were |
| G19 | **Its name and IDs** | Granular (`granular`, `Fgrn`, `org.segnoaudio.granular`), named for how it makes sound ([device-suite.md](../../docs/devices/device-suite.md)) |
| G20 | **What it shares** | `libs/dsp`'s filters, envelopes, LFOs, random sources, onsets, tempo, pitch and key finding, decimator and feedback path; `libs/audiofile`'s decoder; the Sampler's SFZ reader as it is; the Wavetable's modulation UI kit in `fd_device_ui`. Its matrix's evaluation is its own: sharing one with the Wavetable is a later refactor, so the Wavetable isn't touched while main works on it |
| G21 | **Factory presets** | About 56 from the Library's instruments and a few sources of Segno's own; a few factory racks with a space |
| G22 | **Assistants' tools** | `load_granular_source` (a file, a clip or a Library instrument into Source 1 or 2), `get_granular_source` (its kind, root, part, length, transients); step 2: `capture_granular`, `make_granular` (a clip into a Granular on a new track). Parameters through the module tools every module has |
| G23 | **Where in the plan** | P8 of M3, on the `synth` branch, beside main's work |
| G24 | **The experiments** | All in, since the product owner asked for room to experiment, and each is cheap: Rhythm with Chance, Repeats and Swing; Fire; Random Per Grain; Grain Glide; Position Key; the Song playhead; Hold and Feedback on the live input; Both sources on every grain; colour; Spectral's Smear. Any to drop? |

## Drawn (the visual mockup, 2026-10-16; approved by the product owner the same day, the grey waveform kept)

Every state drawn with the real panel kit: Granular's own panel, from its approved parameters and a stand-in for the engine (the grains, the CPU and the playhead published as the engine will publish them). Each state passes the module panel lint on every page (`tests/panels/GranularPanelTests.cpp`, `FD_DUMP_DIR`):
- **Grains:** Classic, Loop, Cloud (a cloud on octaves and fifths), Pitched, Rhythm, Spectral; Source 2 beside Source 1; an empty source; a source on Live, with the Input picker on a track; the routes' rings on Position, Density and Pitch Spray.
- **Sound:** a resonator on every grain with its cutoff's spray; LIVE while a source is on Live.
- **Modulation:** LFO 1 and the matrix with four routes; Random 1 per grain.
- Collapsed and folded.

**The layout as drawn:**
- **Grains** (about 1 480 px): SOURCE 7 columns, POSITION 5, GRAIN 6, PITCH 4, SPREAD 4.
  - SOURCE: the display (5 × 5), the picker under it; at its right, 1 | 2, Sample | Live, Gain, Tune, Mix and Pick.
  - POSITION: Position, Spray, Scan and Pos Key as knobs; Spray Shape, Scan Sync, Scan End, Playhead and Snap as a column of choices beside them.
  - GRAIN: the mode's five knobs and the window drawn small; Mode and Window; Size Sync (Rate in Rhythm, Resolution in Spectral), Skew; Repeats, Fire as a pad, Flatness. Placed again when the mode changes, so each mode shows its own; what a mode doesn't use is dimmed (Size Sync in Pitched, the window in Spectral).
  - PITCH: Semi, Fine, Key, Spray; Glide; the spray's direction (Both | Up | Down), Quantize, Chord (dimmed but for Chord).
  - SPREAD: Pan, Size, Level, Even; Pan Mode; Density Gain.
- **Sound** (about 1 480 px, 1 600 with LIVE): GRAIN FILTER 4 (Type, In Key, the response with its spray, Cutoff, Res, Spray, Track), GRAIN COLOR 3, FILTER 6 and ENVELOPES 7 as the Wavetable's, VOICE 5 (Glide, Bend, Width, Volume, Max; Mode, Latch; Glide Mode, Grain Tails; Voices, Filter Routing), LIVE 3 (Length, Gain, the input's level; Rolling | Hold | Frozen; Feedback; Capture).
- **Modulation:** LFO 6, MATRIX 11, MACROS 4, as the Wavetable's.
- **Header:** the page tabs, the presets, Input, Quality, the grains playing and the CPU.

**Settled while drawing:**
- **The parts every synth shares** (an envelope drawn and dragged, an LFO's shape, the filters' responses, the matrix, the modulation's rings and handles) moved from the Wavetable into the panel kit (`ui::synth`, `SynthParts.h`), so Granular's and the Wavetable's are one: the Wavetable unchanged, its panel tests passing.
- **The display's look follows the Sampler's** (the well, the waveform's columns, S and E, the file's line), with one difference to review: the waveform is grey behind the grains, which take the colour (Source 1 in the value colour, Source 2 in a new colour role, `secondSource`, violet), so the grains read over it.
- **Names that read apart on one page:** Scan Sync and Size Sync (not two "Sync"), Scan End (not "End", which the display's E is), Pos Key (not a second "Key"), Spray Shape; the grain filter's In Key (Cutoff to Key) and the Spectral Harm (Harmonics) as their knobs' short names.
- **Two choices renamed to fit their dropdowns:** Source Pick's Alternate is **Turns**, Chord's The Key's Chord is **Key Chord** (as the Wavetable's stack names it). Their descriptions follow.
- **Three units added to the device API** (an addition, as `TransportInfo::key` was): per second (Density), octaves (Cutoff Spray), times (Ring Ratio).
- **A toggle on a level** (Fire, 0 to 100 %) sets its parameter's most and least, and reads on above the middle, so a pad can fire it; on an on/off parameter, nothing changes.
- **Filter Routing sits in VOICE,** named in full there, since FILTER has no room; Source Fine waits for the Source editor (step 2), where the root is set.
- **LIVE's Feedback is a bar,** with the input's level beside Gain; the sample's name dims while its source is on Live (it stays loaded for when it's back on Sample).
- **Scan isn't finer near 0:** the parameters have no curve symmetric about zero, so it's linear.
- **The conformance suite** finds every parameter described as in this SPEC; its check that an instrument plays waits for step 1's engine, with a chord of Segno's own in Source 1 for it to play (`GrainSource::chord`).

## Built (step 1, the instrument, 2026-10-16)

The engine, the panel on live data, the DAW's menus, browser drops and assistants' tools, the plugin and 60 factory presets with ten racks. Tests: `devices/granular/tests/GranularTests.cpp` (16 cases), the conformance suite, the panel lint, `tests/integration` (the Module menu, MCP).

**Measured** (Release, `[.benchmark]`, this laptop, 48 kHz, one core):

| Load | Measured | Budget |
|---|---|---|
| 8 notes × 64 grains, mono, Eco / Normal / High | 3.5 / 4.6 / 6.3 % | ≤ 4 % (Normal) |
| The same, stereo | 7.0 % | – |
| The same, a long source (the plain 16-bit pyramid) | 11.7 % | – |
| The same, every grain filtered / crushed | 7.6 / 7.9 % | – |
| The same, stereo, every grain filtered and crushed | 17 % | ≤ 10 % |
| Pitched, 8 notes, 8 periods | 1.5 % | ≤ 8 % (stereo, C5) |
| Classic, 8 notes × 16 grains | 1.9 % | – |

Two budgets aren't met yet (B74): Normal by 15 %, the filtered and coloured stereo cloud by 70 %. What's left to try, measured one at a time: pass 1 writing its taps four at a time (one 16-byte store, transposed), two groups of filtered grains in one loop, held (crushed) samples read once.

**Quality** (a tone's images and noise beside it, a Blackman-Harris spectrum): Eco −62 dB, Normal −74 dB, High −103 dB; the plain pyramid −65 dB. Aliasing two to four octaves up stays under the source's level by more than 50 dB at every Quality.

**Settled while building:**
- **Each note's grains are rendered over the whole block, once,** at its end. The budget is counted where each new grain starts, from where every grain ends, rather than rendered up to it, so it's exact and a render is bit-identical for any block size (as the conformance suite checks). A voice ends at the exact sample its amp ends (with Grain Tails, its last grain and 20 ms for its filters to ring).
- **The instance's 1 024 grains are shared out evenly:** a note plays at most Max Grains, and never more than 1 024 ÷ Voices (128 at 8 voices), so the cap doesn't depend on what other notes are doing within a block.
- **At the budget,** a new grain reads with Eco's interpolator, and the grain nearest its end steps aside over 2 ms; only then, not before (a full cloud at Normal sounds like Normal).
- **Repeats replay the grain where it is,** back to back; its filter runs on through them (the window starts at zero, so nothing clicks). A grain stepping aside keeps repeating until its fade ends.
- **Grains with a filter are rendered eight at a time,** each in a SIMD lane, their filters run across the lanes, then each is added to the note in turn; grains without one first, each kind in the order they started, so every sample's sum is in the same order however the block is cut.
- **Grain Tails:** until the note is released, the amp envelope shapes the voice; from the release, each grain playing keeps the level it had, and each that starts takes the envelope's level as it starts. A One-shot envelope plays out as it is.
- **A DC blocker at 5 Hz** heads the voice stage: a Pitched grain shorter than a period of its source (a formant below the note) carries an offset. A formant exactly an octave under the note, on a pure sine at 4 periods, is silent (the Hann window's spectrum is zero at every harmonic); a source with harmonics isn't.
- **Rolls (ADR 0062):** a new device hook, `Device::startPass(roll)`, gives a module a pass's dice; the Session calls it with the modulators' when playback starts or a render does. With no host calling it (a plugin, the tests) Granular counts its own from a fixed base at each start of playback, so two new instances play alike and `reset()` is deterministic. Keep stores the roll (`roll`); the Module menu's Keep as played, Song ▸ Keep all as played and `keep_roll` (a new `module` argument) cover Granular. The plugins have no menu bar: there, each instance rolls by itself.
- **Rhythm and the Song playhead follow the song's samples,** not its beats, so a step lands on the same sample whatever the block. Swing makes every second step late by Swing × half a step (67 %: a triplet's feel); Jitter doesn't move Rhythm's grains, which are on the grid.
- **The plain pyramid's 8-tap sinc** is cut at 0.42 of each level's rate with a Kaiser β of 8.6 (−65 dB, the band kept to 20 kHz at 48 kHz), each output one 8-wide dot product.
- **The voice filters keep their last coefficients** (`dsp::VoiceFilter`: a tan and a pow a sample saved while the cutoff holds still), which the Wavetable shares, its output unchanged bit for bit.
- **A multisample's zones are each brought to full scale,** the file's volumes not applied on top (which doubled the Library's own gains): grains play evenly across the keyboard (ADR 0069), where a Sampler keeps each recording's balance between registers.
- **Loading is the pyramid's build** (97 % of it): its filters are Granular's own, in floats with their symmetric taps folded, and the loader is optimised in Debug builds too. A Library instrument loads in under a second in Debug, a tenth of that in Release.
- **One way to load a source** (`Granular::sourcePatch`, `sourceSettings`): from the panel, the browser and the assistants alike, the Library's as a `library:` reference, the root found again, the source on and playing all of the file (a clip's part from `load_granular_source`). The display says why a file didn't load.
- **The DAW:** Module ▸ Load Source 1…, Load Source 2…, Swap sources, and Keep as played for a Granular; a sample or multisample dropped from the browser goes into the source the panel shows (Source 1 when it's folded). Edit Source, Capture and Freeze Input come with step 2. `load_granular_source` takes a file, an SFZ or an audio clip (its part of its file); `get_granular_source` returns the kind, length, root, part, transients and beats.
- **The plugin** is built (`Fgrn`: VST3, CLAP, LV2), its sidechain bus ready for step 2's live input.
- **The factory presets** (replaced 2026-10-16 after the product owner's verdict on the first set, ADR 0069): 60, designed from research ([presets/BRIEF.md](presets/BRIEF.md)), each passing the keyboard check (no take above −1 dBFS at full velocity anywhere on the keyboard, in four-note chords, or with its macros and the mod wheel at their ends; about −24 dB RMS at C3; even within 12 dB in its range). A drum kit isn't a source (its SFZ has one drum a key, so a note plays one drum). Pitched wants a source with harmonics: it reads the source at its own pitch, so a nearly pure tone in long grains mostly cancels. Ten factory racks give the pads their space. The sources of Segno's own (rendered into the Library) wait for a Library build that adds them; the Live and Spectral presets come with steps 2 and 3.
- **Width and Volume are targets** the matrix offers that hadn't been wired: each note's width is now its own (the voice stage's mid and side), and Volume moves a note against the instrument's level.

## Sign-off

Step 1, the instrument:
- [x] Automated: unit and conformance tests, the CPU benchmarks (two budgets short, B74); the plugin validators not yet run
- [x] Layout lint passes (`fd_panel_tests`), every page
- [x] Snapshots reviewed (100 % / 200 %; every page; expanded, collapsed, folded): the visual mockup, approved
- [x] Sound signed off by the product owner (2026-10-16: "The instrument is good! It can be signed off.")
- [x] Feel signed off by the product owner (2026-10-16)
- [ ] The factory presets signed off by ear: the first set rejected (2026-10-16, ADR 0069), the second for review
