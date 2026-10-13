# Wavetable Synth: device spec

- **Status:** Approved by the product owner (2026-10-13, with every recommendation of W1–W19 and the pages of [ADR 0051](../../docs/decisions/0051-pages-for-very-wide-modules.md)); IDs are permanent. [ADR 0050](../../docs/decisions/0050-the-wavetable-synth-goes-beyond-live.md) accepted with it: the product owner asked for a wavetable synth that is full-featured, modern and innovative, past ADR 0011's "instruments stay close to Live's". Step 1 (the synth) in progress.
- **Type:** Instrument
- **Tier / milestone:** Tier 2, moved up to M3 by the product owner (2026-10-13; P1 of the M3 plan, before the rest of M3)
- **Live target:** Wavetable, with Meld's following of the key and scale. References only ([ADR 0011](../../docs/decisions/0011-calibrated-ambition-and-consolidation.md)); this device goes well beyond them (W1).

## Purpose

The synth for sounds you can't get from the Subtractive Synth: evolving pads, growling basses, glassy keys, vocal and metallic tones, all made by moving through tables of waveforms and reshaping them as they play. It should stand next to the best wavetable synths you can buy in depth and sound. It should beat them where an integrated DAW helps: turning any sound in your project into a wavetable, editing tables without losing the original, following the song's key, and showing what it costs.

## What the market offers (research, October 2026)

Read from the manuals, changelogs, reviews and forums of the current leaders: the leading paid wavetable synth (2025, 3 oscillators that can each be wavetable, sample, granular or spectral), the leading free one (frozen as open source since 2022), Live's Wavetable and Meld, a flagship multi-engine synth (7th version), a modular one, u-he's latest, Bitwig's, and the open-source Surge XT.

**The baseline a credible wavetable synth has in 2026:**
- 3 wavetable oscillators, a sub and a noise source;
- tables of 2 048 samples by up to 256 frames, morphed smoothly;
- two warps per oscillator (sync, bend, pulse, mirror, quantize, a drawn remap, formant, FM, PM and ring modulation from the others) and some spectral warps;
- an editor, with audio import that finds the pitch and the standard table format (a WAV whose `clm ` chunk gives the frame size);
- unison of 16 with detune laws, blend, width, octave and chord stacks, and each copy reading a different place in the table;
- 2 filters in series or parallel, with drive, comb and formant types;
- 4 or more curved envelopes, 8 drawable LFOs, random sources, 8 macros, a matrix with a curve per route, drag-and-drop with rings on the controls;
- MPE and polyphonic aftertouch; every setting automatable.

**What the leaders are praised and criticised for:**
- The leaders are praised for their sound, their warps and their matrices.
- They are criticised for:
  - CPU (the top complaint about the leading paid synth);
  - editors that are clumsy or missing (four of the leaders have none);
  - audio import that turns real recordings into poor tables;
  - plain randomise (about one usable sound in ten);
  - features only behind right-click or shortcuts.
- The DAWs' own synths are seen as basic next to them: one has no noise source, no drawable LFO and no editor.

**What nobody does well yet** (the openings this spec takes):
1. Turning real audio (chorused, multi-cycle, noisy) into clean wavetables, with a preview before committing.
2. Editing a table non-destructively: kept as a recipe of sources and changes, re-editable later.
3. Variation that respects the sound: locked sections, a set amount, instead of randomise.
4. A wavetable synth that follows the project's key and scale (Meld does it with no wavetables).
5. CPU you can see, and quality you can choose.
6. Warps that don't alias: every synth in this class either lets them alias or pays for oversampling.

## Beyond Live

| Addition | Reference | Why it's worth it | Cost |
|---|---|---|---|
| **3 oscillators, a sub and noise; 2 warps each; 16-voice unison with stacks and spreads** (W2, W4, W5) | The leading wavetable synths | Live's has 2 oscillators, one mode each, no noise | Large: the engine |
| **Alias-free warps:** each voice's cycle rendered from the table's spectrum, band-limited exactly for its pitch (W3) | Spectral rendering in the leading free synth, extended to every warp of the oscillator's own phase | Cleaner high notes and extreme warps without the CPU of oversampling; in this class only the cross-oscillator warps still need it | Moderate: a small inverse FFT per voice every few ms |
| **2 filters, serial to parallel, 14 types with drive** (W6) | The leading synths | Live's has 2 filters with fewer types | Moderate |
| **4 envelopes with curves and loop, 8 drawable LFOs, 2 random sources, 8 macros, a 32-route matrix, drag-to-modulate with rings** (W7) | The leading synths; the flagship's rings that show every route | Modulation is where wavetable sounds come alive | Large: the matrix and its UI, shared with M4's modulators |
| **The table editor:** a recipe of sources and changes, non-destructive, in the bottom panel (W9) | Four leaders have none; the free one's keyframe editor is the model | The gap users name most | Large |
| **Audio to wavetable, from anywhere in the project:** drop a file, or Make Wavetable from a clip (W10) | Resynthesis in the leading synths, done better | Turn any sound in the song into an instrument | Moderate |
| **The project's key:** unison stacks that play chords in the key, pitch modulation that lands on its notes (W11) | Meld; Bitwig's project key | Musical movement without wrong notes | Small, with P3 |
| **Vary:** controlled variation with locks (W12) | Two high-end synths' mutators | Finding new sounds from a good one | Small |
| **CPU you can see and quality you choose** (W13) | The leading synth colours its unison count by cost | CPU is the top complaint | Small |

## Stages

Built in three steps within P1, each built, tested and signed off on its own, as the EQ's stage 2 was (W18). Nothing is released between them, so the parameter list below is approved whole now.

| Step | Scope |
|---|---|
| 1. The synth | Oscillators, sub and noise; factory tables and import of table files and single cycles; per-voice rendering with every warp except the spectral ones; unison and stacks (not the key chord); filters; envelopes, LFOs with drawn shapes, random sources, macros, the matrix and drag-to-modulate; voices and MPE; the panel; Quality and the CPU readout; factory presets |
| 2. The tables | The table editor in the bottom panel (W9); audio to wavetable with preview, Make Wavetable from a clip (W10); images and formulas; the spectral warps; export |
| 3. The song | Key chord stacks and Snap to Key (W11, after P3 brings the project's key); Vary (W12) |
| Later | Microtuning (with a DAW-wide tuning); per-voice effects; morphing between presets |

## Signal flow

```
note (MPE: per-note bend, pressure, slide)
  └─ voice (up to 32) ──────────────────────────────────────────────────────────────┐
      ├─ Osc 1 ─┐  each: table → cycle rendered for its pitch every 64 samples      │
      ├─ Osc 2 ─┤        (position, spectral warps, phase warps, band limit)        │
      ├─ Osc 3 ─┤        → unison copies (detune, stack, spreads) → level, pan      │
      ├─ Sub ───┤        FM / PM / RM / AM from another source: per sample,         │
      └─ Noise ─┘        oversampled by Quality                                     │
            │ each to Filter 1, Filter 2, both or neither                           │
            ▼                                                                       │
   Filter 1 ─▶ Filter 2   (Filter Routing: serial … parallel)                       │
            ▼                                                                       │
   Amp (Env 1, velocity) ─▶ voice sum ─▶ Volume ─▶ out                              │
modulation, per voice on the 32-sample control grid: Env 1–4, LFO 1–8, Random 1–2, │
Macro 1–8, velocity, note, pressure, slide, bend, mod wheel → 32 routes ────────────┘
```

## Parameters

State version 1. Every parameter is automatable except where noted. Families use `<n>`.

**Oscillators** (`osc<n>`, n = 1–3):

| ID (permanent) | Name | Description | Range | Default | Unit | Curve | Smoothing |
|---|---|---|---|---|---|---|---|
| `osc<n>.on` | Osc n On | Turns the oscillator on or off. | Off / On | On (1), Off (2, 3) | toggle | – | 5 ms fade |
| `osc<n>.pos` | Osc n Position | Where in its wavetable the oscillator plays: moving it morphs through the table's waves. | 0 … 100 | 0 | % | linear | 20 ms |
| `osc<n>.oct` | Osc n Octave | Shifts the oscillator by whole octaves. | −4 … +4 | 0 | – | stepped | – |
| `osc<n>.semi` | Osc n Semi | Shifts the oscillator by semitones, for intervals. | −12 … +12 | 0 | st | stepped | – |
| `osc<n>.fine` | Osc n Fine | Tunes the oscillator slightly up or down. | −100 … +100 | 0 | ct | linear | 20 ms |
| `osc<n>.level` | Osc n Level | The oscillator's level. | −inf … +6 | 0 (1), −6 (2, 3) | dB | dB | 20 ms |
| `osc<n>.pan` | Osc n Pan | Places the oscillator left or right. | 50L … 50R | C | – | linear | 20 ms |
| `osc<n>.phase` | Osc n Phase | Where in its wave the oscillator starts each note. | 0 … 360 | 0 | ° | linear | – |
| `osc<n>.rand` | Osc n Phase Random | How much each note's starting point varies: at 100 % the oscillator seems to run freely. | 0 … 100 | 0 | % | linear | – |
| `osc<n>.warp<k>` (k = 1–2) | Osc n Warp k | A way to reshape the oscillator's wave: sync, bend, fold, frequency modulation and more. | Off, Sync, Window Sync, Bend +, Bend −, Bend ±, Squeeze, Pulse, Asym +, Asym −, Mirror, Flip, Quantize, Fold, Remap, FM, PM, RM, AM, Formant Shift, Harmonic Stretch, Inharmonic Stretch, Smear, Spectral Low Pass, Spectral High Pass, Phase Disperse, Frame Skew, Random Harmonics, Shepard | Off | choice | – | crossfade |
| `osc<n>.warp<k>.amt` | Osc n Warp k Amount | How strongly the warp reshapes the wave. | 0 … 100 | 0 | % | linear | 20 ms |
| `osc<n>.warp<k>.src` | Osc n Warp k Source | What modulates the wave in the FM, PM, RM and AM warps. | Osc 1, Osc 2, Osc 3, Sub, Noise | the next oscillator | choice | – | crossfade |
| `osc<n>.uni` | Osc n Unison | How many detuned copies of the oscillator play together. | 1 … 16 | 1 | – | stepped | – |
| `osc<n>.detune` | Osc n Detune | How far apart the unison copies are tuned. | 0 … 100 | 25 | % | linear | 20 ms |
| `osc<n>.unimode` | Osc n Unison Mode | How the copies spread in pitch: evenly, bunched in the middle, wider at the edges, or at random. | Even, Super, Exponential, Random | Super | choice | – | – |
| `osc<n>.stack` | Osc n Stack | Tunes the unison copies to intervals too: octaves, a fifth, or a chord, in the project's key if you like. | Off, Octave, 2 Octaves, Fifth, Fifth + Octave, Major, Minor, Key Chord | Off | choice | – | – |
| `osc<n>.blend` | Osc n Blend | The balance between the middle copy and the detuned ones. | 0 … 100 | 75 | % | linear | 20 ms |
| `osc<n>.width` | Osc n Width | How widely the unison copies spread across the stereo field. | 0 … 100 | 80 | % | linear | 20 ms |
| `osc<n>.spread` | Osc n Position Spread | Gives each unison copy its own place in the wavetable, for movement and width. | 0 … 100 | 0 | % | linear | 20 ms |
| `osc<n>.warpspread` | Osc n Warp Spread | Gives each unison copy its own warp amount. | 0 … 100 | 0 | % | linear | 20 ms |
| `osc<n>.route` | Osc n Filter | Which filter the oscillator plays through: the first, the second, both, or neither. | Filter 1, Filter 2, Both, None | Filter 1 | choice | – | crossfade |

- **Warps:** what each does is under "DSP design". Bend, Squeeze, Pulse and the others shape the wave's own cycle; FM, PM, RM and AM bring in another source; the spectral warps (Formant Shift to Shepard) work on the wave's harmonics. Remap's curve is drawn (state, below).
- **Stack's Key Chord** builds the chord from the project's key on the note played (step 3, with P3); without a key it plays a major or minor triad by the note's place in C major.

**Sub and noise:**

| ID (permanent) | Name | Description | Range | Default | Unit | Curve | Smoothing |
|---|---|---|---|---|---|---|---|
| `sub.on` | Sub On | Turns the sub oscillator on or off. | Off / On | Off | toggle | – | 5 ms fade |
| `sub.oct` | Sub Octave | How far below the note the sub plays. | −2, −1, 0 | −1 | – | stepped | – |
| `sub.shape` | Sub Shape | The sub's wave. | Sine, Triangle, Saw, Square | Sine | choice | – | crossfade |
| `sub.level` | Sub Level | The sub's level. | −inf … +6 | −6 | dB | dB | 20 ms |
| `sub.pan` | Sub Pan | Places the sub left or right. | 50L … 50R | C | – | linear | 20 ms |
| `sub.route` | Sub Filter | Which filter the sub plays through: the first, the second, both, or neither. | Filter 1, Filter 2, Both, None | None | choice | – | crossfade |
| `noise.on` | Noise On | Turns the noise source on or off. | Off / On | Off | toggle | – | 5 ms fade |
| `noise.type` | Noise Type | The kind of noise: from bright white to deep brown, crackle, breath or metallic. | White, Pink, Brown, Digital, Crackle, Breath, Metallic | White | choice | – | crossfade |
| `noise.color` | Noise Color | Tilts the noise darker or brighter. | −100 … +100 | 0 | % | linear | 20 ms |
| `noise.level` | Noise Level | The noise's level. | −inf … +6 | −12 | dB | dB | 20 ms |
| `noise.pan` | Noise Pan | Places the noise left or right. | 50L … 50R | C | – | linear | 20 ms |
| `noise.route` | Noise Filter | Which filter the noise plays through: the first, the second, both, or neither. | Filter 1, Filter 2, Both, None | Filter 1 | choice | – | crossfade |

**Filters** (`filter<n>`, n = 1–2):

| ID (permanent) | Name | Description | Range | Default | Unit | Curve | Smoothing |
|---|---|---|---|---|---|---|---|
| `filter<n>.on` | Filter n On | Turns the filter on or off. | Off / On | On (1), Off (2) | toggle | – | 5 ms fade |
| `filter<n>.type` | Filter n Type | The filter's kind: low, high or band pass, notch, peak, ladder or diode, comb, formant or phaser. | LP 12, LP 24, Ladder, Diode, HP 12, HP 24, BP 12, BP 24, Notch, Peak, Comb +, Comb −, Formant, Phaser | LP 24 (1), HP 12 (2) | choice | – | crossfade |
| `filter<n>.cutoff` | Filter n Cutoff | Where the filter acts: for a low pass, lower is darker. | 20 … 20 000 | 2 000 (1), 100 (2) | Hz | log | 20 ms multiplicative |
| `filter<n>.res` | Filter n Resonance | Emphasis at the cutoff; near the top the ladder and diode filters start to sing on their own. | 0 … 100 | 20 | % | linear | 20 ms |
| `filter<n>.drive` | Filter n Drive | Pushes the filter harder, for warmth and grit. | 0 … 100 | 0 | % | linear | 20 ms |
| `filter<n>.shape` | Filter n Shape | The type's own setting: the formant's vowel, the comb's damping, the phaser's spread. | 0 … 100 | 50 | % | linear | 20 ms |
| `filter<n>.key` | Filter n Key Track | How much the cutoff follows the note played: at 100 % it rises an octave per octave. | 0 … 100 | 50 | % | linear | 20 ms |
| `filter<n>.env` | Filter n Env | How far the filter envelope moves the cutoff, up or down. | −100 … +100 | 30 (1), 0 (2) | % | linear | 20 ms |
| `filters.routing` | Filter Routing | Filter 1 into filter 2 at 0 %, the two side by side at 100 %, or anywhere between. | 0 … 100 | 0 | % | linear | 20 ms |

**Amp and envelopes** (`env<n>`, n = 1–4; 1 is the amp envelope, 2 the filter envelope):

| ID (permanent) | Name | Description | Range | Default | Unit | Curve | Smoothing |
|---|---|---|---|---|---|---|---|
| `amp.vel` | Velocity | How much harder playing makes notes louder. | 0 … 100 | 50 | % | linear | – |
| `env<n>.a` | *Env* Attack | How long the envelope takes to rise. | 0 … 20 000 | 2 | ms | centre 50 ms | – |
| `env<n>.h` | *Env* Hold | How long the envelope stays at its peak before it falls. | 0 … 10 000 | 0 | ms | centre 100 ms | – |
| `env<n>.d` | *Env* Decay | How long the envelope takes to fall to its sustain level. | 1 … 20 000 | 300 (1), 400 (2), 300 (3, 4) | ms | centre 300 ms | – |
| `env<n>.s` | *Env* Sustain | The envelope's level while the key stays down. | 0 … 100 | 80 (1), 0 (2–4) | % | linear | 20 ms |
| `env<n>.r` | *Env* Release | How long the envelope takes to fall after the key is released. | 1 … 20 000 | 200 (1), 300 (2–4) | ms | centre 300 ms | – |
| `env<n>.acurve` | *Env* Attack Curve | How the attack rises: below zero it starts slowly, above zero quickly. | −100 … +100 | 30 | % | linear | – |
| `env<n>.dcurve` | *Env* Decay Curve | How the decay falls: below zero it starts slowly, above zero quickly. | −100 … +100 | 60 | % | linear | – |
| `env<n>.rcurve` | *Env* Release Curve | How the release falls: below zero it starts slowly, above zero quickly. | −100 … +100 | 60 | % | linear | – |
| `env<n>.mode` | *Env* Mode | Normal; Loop repeats attack to decay while the key is held; One-shot plays through and ignores the release. | Normal, Loop, One-shot | Normal | choice | – | – |

*Env* is "Amp" for `env1`, "Filter Env" for `env2`, and "Env 3" and "Env 4" for the others ("Amp Attack", "Filter Env Decay", "Env 3 Release").

**LFOs** (`lfo<n>`, n = 1–8; each LFO's shape is drawn, state below):

| ID (permanent) | Name | Description | Range | Default | Unit | Curve | Smoothing |
|---|---|---|---|---|---|---|---|
| `lfo<n>.rate` | LFO n Rate | How fast the LFO moves when it isn't synced to the tempo. | 0.01 … 100 | 2 | Hz | log | 20 ms multiplicative |
| `lfo<n>.sync` | LFO n Sync | Locks the LFO to the song's tempo, in note values. | Off, 1/64 … 8 bars, with dotted and triplet values | Off | choice | – | – |
| `lfo<n>.mode` | LFO n Mode | Free keeps running; Retrigger restarts it with each note; Envelope plays its shape once per note. | Free, Retrigger, Envelope | Retrigger | choice | – | – |
| `lfo<n>.phase` | LFO n Phase | Where in its shape the LFO starts. | 0 … 360 | 0 | ° | linear | – |
| `lfo<n>.smooth` | LFO n Smooth | Rounds off the shape's corners, so steps glide. | 0 … 100 | 0 | % | linear | 20 ms |
| `lfo<n>.fade` | LFO n Fade In | How long the LFO takes to fade in after each note starts. | 0 … 10 000 | 0 | ms | centre 500 ms | – |
| `lfo<n>.poly` | LFO n Per Note | On, each note has its own LFO; off, one LFO moves every note together. | Off / On | On | toggle | – | – |

**Random sources** (`rnd<n>`, n = 1–2):

| ID (permanent) | Name | Description | Range | Default | Unit | Curve | Smoothing |
|---|---|---|---|---|---|---|---|
| `rnd<n>.mode` | Random n Mode | Steps holds each new value; Smooth glides between them; Chaos wanders like a pendulum; Per Note picks one value for each note. | Steps, Smooth, Chaos, Per Note | Smooth | choice | – | – |
| `rnd<n>.rate` | Random n Rate | How often a new value comes when it isn't synced to the tempo. | 0.01 … 100 | 4 | Hz | log | 20 ms multiplicative |
| `rnd<n>.sync` | Random n Sync | Locks the new values to the song's tempo, in note values. | as LFO Sync | Off | choice | – | – |
| `rnd<n>.poly` | Random n Per Note | On, each note has its own values; off, every note follows the same ones. | Off / On | On | toggle | – | – |

**Macros and modulation** (`macro<n>`, n = 1–8; `mod<n>`, n = 1–32):

| ID (permanent) | Name | Description | Range | Default | Unit | Curve | Smoothing |
|---|---|---|---|---|---|---|---|
| `macro<n>` | Macro n | A knob of your own: route it to any settings to move them together. | 0 … 100 | 0 | % | linear | 20 ms |
| `mod<n>.src` | Mod n Source | What drives this route: an envelope, an LFO, a macro, or how the note is played. | None, Amp Env, Filter Env, Env 3, Env 4, LFO 1 … LFO 8, Random 1, Random 2, Macro 1 … Macro 8, Velocity, Release Velocity, Note, Pressure, Slide, Pitch Bend, Mod Wheel, Note Random | None | choice | – | – |
| `mod<n>.dst` | Mod n Target | What this route changes. | None, then every target below | None | choice | – | – |
| `mod<n>.amt` | Mod n Amount | How much, and in which direction, this route moves its target. | −100 … +100 | 0 | % | linear | 20 ms |
| `mod<n>.curve` | Mod n Curve | Bends the route's response: below zero small moves of the source count for less, above zero for more. | −100 … +100 | 0 | % | linear | – |
| `mod<n>.aux` | Mod n Scale | Another source that scales this route's amount, such as the mod wheel or velocity. | None and the sources above | None | choice | – | – |

- **Targets,** in this order (permanent): Pitch; for each oscillator: Position, Pitch, Fine, Level, Pan, Warp 1, Warp 2, Detune, Blend, Width, Position Spread, Warp Spread; Sub Level, Sub Pan; Noise Level, Noise Color, Noise Pan; for each filter: Cutoff, Resonance, Drive, Shape; Filter Routing; Amp; Pan; LFO 1 … 8 Rate; Random 1, 2 Rate. Pitch targets move ±24 semitones at 100 %, cutoff ±5 octaves, the rest their whole range.
- **Routes are made in the matrix, or by dragging** a source onto a control on the panel (W7); both set these parameters, so assistants and hosts see every route.

**Voice and output:**

| ID (permanent) | Name | Description | Range | Default | Unit | Curve | Smoothing |
|---|---|---|---|---|---|---|---|
| `voice.mode` | Voice Mode | Poly plays chords; Mono plays one note at a time; Legato is mono without restarting the envelopes when notes overlap. | Poly, Mono, Legato | Poly | choice | – | – |
| `voice.count` | Voices | How many notes can sound at once. | 1 … 32 | 8 | – | stepped | – |
| `voice.glide` | Glide | How long the pitch takes to slide from one note to the next. | 0 … 2 000 | 0 | ms | centre 200 ms | – |
| `voice.glidemode` | Glide Mode | Always slides; Legato slides only between overlapping notes. | Always, Legato | Always | choice | – | – |
| `voice.bend` | Bend Range | How far the pitch bend wheel bends, in semitones. | 0 … 48 | 2 | st | stepped | – |
| `voice.drift` | Drift | Small random wandering of each voice's pitch and filter, like an analog synth. | 0 … 100 | 10 | % | linear | – |
| `key.snap` | Snap to Key | Makes pitch modulation land on notes of the project's key. | Off / On | Off | toggle | – | – |
| `out.volume` | Volume | The synth's output level. | −inf … +6 | −6 | dB | dB | 20 ms |
| `quality` | Quality | Eco saves CPU; High is cleanest for frequency modulation and the lowest notes. | Eco, Normal, High | Normal | choice | – | – |

- **Quality** is a header setting. It changes how much the frequency-modulation warps are oversampled, which changes the delay by a few samples: it carries `ParamFlags::ChangesLatency` and isn't automatable (ADR 0020).
- **About 380 parameters** (W17): 23 per oscillator, 12 for sub and noise, 17 for the filters, 37 for the envelopes and velocity, 56 for the LFOs, 8 for the random sources, 8 macros, 160 for the matrix, 9 for voice and output.

**State that isn't a parameter** ([ADR 0017](../../docs/decisions/0017-heavy-data-and-non-parameter-state.md)):
- **Each oscillator's table:** a factory table by its permanent ID, or a user table (W8): its name, its recipe (step 2: the sources and changes it was made from) and its frames.
- **Each LFO's shape:** a drawn curve of points, each segment with its own bend, or steps. Factory shapes (sine, triangle, saw, square, steps, and more) come from a menu on the LFO display.
- **Each Remap warp's curve,** drawn the same way.
- **The macros' names.**

## DSP design

- **Tables:** 2 048 samples per frame, 1 to 256 frames, mono. Kept as spectra (1 025 bins per frame), made off the audio thread, handed to the audio thread with `AssetHandoff` (ADR 0017), and shared between oscillators and instances.
- **Each voice renders its own cycle** (the one band-limiting mechanism, W3). Every 64 samples, on the absolute sample grid so the sound never depends on block sizes, each voice builds the cycle it plays next:
  1. interpolates the spectra of the two frames around the position;
  2. applies the spectral warps (on the bins);
  3. if a phase or amplitude warp is on, rebuilds the cycle at twice the size, warps it, and analyses it again;
  4. clears every harmonic the note could alias with: those above min(20 kHz, 0.45 × rate) at the highest pitch the voice can reach before the next render (bend, glide, pitch modulation, its widest unison copy);
  5. inverse-transforms to 2 048 samples, and crossfades from the last cycle over the next 64 samples.

  Cycles are read with 4-point cubic interpolation, or 8 points at High quality (cleaner at the lowest notes). The result: every warp of the oscillator's own phase, at any setting, comes out without aliasing.
- **Warps:**
  - **in the cycle:** Sync, Window Sync (sync under a half-sine window: a formant), Bend +, Bend − and Bend ± (curved phase), Squeeze (the cycle pinched around a point), Pulse (the cycle played faster, then silence), Asym + and − (one half longer than the other), Mirror, Flip, Quantize (stepped phase), Fold (folded amplitude), Remap (the drawn curve);
  - **spectral:**
    - Formant Shift (the spectral envelope moves, the harmonics stay);
    - Harmonic Stretch and Inharmonic Stretch (harmonics spaced wider);
    - Smear (amplitudes blurred across harmonics);
    - Spectral Low Pass and High Pass (brick-wall harmonic masks);
    - Phase Disperse (phase rotating with the harmonic number);
    - Frame Skew (each harmonic read from a different frame);
    - Random Harmonics (fixed random amplitudes, chosen per note);
    - Shepard (octaves wrapped round, ever rising);
  - **per sample, from another source:** FM (as phase modulation of the read position: through zero, never detuning), PM, RM, AM. These run oversampled: none at Eco, 2× at Normal, 4× at High (the minimum-phase oversampler of the Filter, a few samples' delay).
- **Unison:** up to 16 copies per oscillator.
  - Their detunes follow the mode's law (Super: fine in the middle, steep at the edges).
  - Stacks add intervals.
  - Copies alternate sides at Width; Blend weighs the middle copy against the others, and levels stay even as the count changes.
  - Copies share one rendered cycle unless Position or Warp Spread give them their own, rendered in pairs.
  - Start phases come from a seed per note, so renders repeat exactly.
- **Filters:**
  - from `libs/dsp`: the state-variable filter (12 dB types; cascaded for 24 dB) and the ZDF ladder;
  - new: a diode ladder, comb filters on the modulation core's delay line (key-tracked, a damping filter in the loop), a formant filter (band passes morphing between vowels), and a phaser (an all-pass chain);
  - Drive is a soft saturation before the filter.
- **Envelopes:** the ADSR with hold, curves per segment, loop and one-shot. **LFOs:** drawn shapes rendered into tables, smoothed on request, synced to the song like the modulation core's LFOs. **Random:** steps, smooth (interpolated), chaos (a Lorenz-style attractor), per note.
- **Modulation:** routes evaluated per voice on the 32-sample control grid, then smoothed per sample; cutoff and position modulation interpolated per sample. Global LFOs and random sources once per block.
- **Voices:** the voice manager (32 voices; stealing, sustain, glide, MPE: per-note bend, pressure and slide). Drift as in the Subtractive Synth.
- **Real time:** every buffer allocated in `prepare()`; table changes arrive by handoff, crossfaded; no locks.
- **CPU readout:** the time `process()` takes, as a share of the block's duration, published every 250 ms (an analysis slot) for the header (W13).

## Tables and their files (W8)

- **Factory tables (about 80):** made in-house, by additive and formula scripts kept in the repository (so they're ours, CC0-compatible), plus single cycles from AKWF (CC0) built into morphing tables. Categories: Basic, Analog, Digital, Harmonic, Formant and Vocal, FM, PWM, Noise and Texture, Spectral.
- **User tables** are files in `Documents/Segno/Wavetables`, found by the browser like presets and samples. A preset or a project keeps the tables it uses with it (a project in its folder, as samples are; a preset inside its file), so it plays the same anywhere.
- **Import:**
  - WAV in any PCM or float format; frames from its `clm `, `srge`, `uhWT`, `cue ` or `smpl` data, or by size, or set by hand;
  - Surge's and Bitwig's `.wt`;
  - a folder of single cycles, resampled from any length;
  - (step 2) audio by pitch detection, images, formulas.
- **Export** (step 2): WAV with a `clm ` chunk (the frame size, never another vendor's flags) and `.wt`.

## The table editor (step 2, W9)

- **Where:** in the DAW, the bottom panel shows it where the piano roll opens (Module > Edit Wavetable, double-click on the table display). In the plugins, the window grows to show it under the panel.
- **What it edits:** a recipe, not samples.
  - Sources on keyframes: a drawn wave, harmonics, a formula, a file, an image, another table.
  - Changes stacked on them: normalise, phase-align, smooth, fold, filter, spectral warps.
  - In-between frames filled by crossfade or spectral morph.

  Every step stays editable. The baked frames are what plays and what's saved with them.
- **Views:** the waves in 3D and 2D, the spectrum, a draw tool and harmonic bars, a formula line.
- **Audio to wavetable** (W10), with a preview to hear before committing:
  - **Pitched:** the pitch is followed, then N cycles taken and aligned;
  - **Spectral:** the spectrum's shape sampled at the harmonics, for noisy or chorused sound;
  - **Slices:** fixed-size windows, for anything.
- **Make Wavetable** in the arrangement's Clip menu turns the selected clip, or the time selected in it, into a new table on the selected Wavetable Synth (or a new one on a new track).

## UI layout

On the module grid ([device-panels.md](../../docs/design/device-panels.md)), on three **pages** ([ADR 0051](../../docs/decisions/0051-pages-for-very-wide-modules.md)): the tabs Oscillators, Filter & Amp and Modulation sit in the header after the name. Sections show the *selected* oscillator, filter, envelope or LFO, as the EQ's BAND section does, and still collapse.

**Oscillators** (`osc`):

| Section | Controls |
|---|---|
| `sources` SOURCES [3] | Osc 1, Osc 2, Osc 3, Sub, Noise: a row each, with its on switch and level; a click selects it for the sections after. Filter Routing (0, 5, 3 wide) |
| `osc` OSC *n* (or SUB, NOISE) [8] ⏻ | **Oscillators:** the table display (0, 0, 4 × 4): the waves in 3D, the playing cycle highlighted, Position dragged on it. The table's name with previous and next, opening the browser (0, 4, 4). Position, Octave, Semi, Fine (4–7, 0). Phase, Random, Pan (4–6, 3). Filter route (4, 5, 4 wide). **Sub and Noise:** their own controls in the same cells |
| `warp` WARP [4] | Warp 1: type and source (0, 0, 2 wide each), amount bar (0, 1, 4 wide), the Remap curve when chosen (0, 2, 4 wide). Warp 2 the same (rows 3–5) |
| `unison` UNISON [4] | Unison, Detune, Blend, Width (0–3, 0). Mode, Stack (0, 3, 2 wide each). Position Spread, Warp Spread (bars, rows 4 and 5) |

**Filter & Amp** (`filter`):

| Section | Controls |
|---|---|
| `filter` FILTER 1 \| 2 [6] ⏻ | A 1 \| 2 switch (0, 0, 2 wide) and Type (2, 0, 4 wide). The response, both filters drawn (0, 1, 6 × 2). Cutoff, Resonance, Drive, Shape, Env, Key Track (0–5, 3) |
| `env` ENVELOPES [6] | Amp \| Filter \| 3 \| 4 (0, 0, 2 wide). The envelope drawn and draggable (0, 1, 2 × 5). Attack, Hold, Decay, Sustain (2–5, 0). Release (2, 3), the three curves as bars, Mode, Velocity (Amp) |
| `voice` VOICE [3] | Mode, Voices, Glide and its mode, Bend, Drift, Volume |

**Modulation** (`modulation`):

| Section | Controls |
|---|---|
| `lfo` LFO [6] | 1 … 8 and Random 1, 2. The shape drawn and edited (0, 0, 3 × 5). Rate or Sync, Phase, Smooth (3–5, 0). Mode, Fade In, Per Note |
| `matrix` MATRIX [11] | The modulation matrix: every route a row of source (2 wide), target (3 wide), amount (2 wide), curve (2 wide) and scale (2 wide), five showing at once and the rest scrolling. Its title row adds a route; a route's row removes it from its menu |
| `macros` MACROS [4] | Macro 1 … 8, two rows of knobs, named by you |

| Also | |
|---|---|
| header | The page tabs; Quality; the CPU it's taking now (W13). The sidechain picker doesn't apply |
| meter strip | Output |

- **About 1 200 px wide** (the Modulation page's 21 columns; the others leave room after their last section), instead of 2 000–2 800 px on one row. The exact cells are settled while building, against the layout lint and screenshots.
- **The matrix and dragging are one thing (W7):** a route made by dragging appears in the matrix, and one added in the matrix shows its ring on the knob.
- **Drag to modulate:** drag a source's handle (on the LFO, envelope, macro and random displays) onto any knob to make a route; a knob on another page is reached by holding the drag on its tab. A ring then shows the range it moves, and the ring is dragged to set Amount.
  - Hovering a source shows its rings on every target; hovering a knob shows what moves it.
  - A right-click on a knob lists its routes, with Remove.
  - The rings, handles and hover behaviour are built once in `fd_device_ui`, for M4's DAW-wide modulators too.
- **By mouse** (ADR 0040): every action has a control on the panel or an item in the menu bar: the Module menu (Edit Wavetable, Vary, Import Wavetable…), View (Next and Previous Module Page), and the Clip menu (Make Wavetable).

## Factory presets

About 40, built and named by Claude, signed off by ear: Bass (8), Lead (8), Pad (8), Pluck (6), Keys (4), FX (6). Tags from the shared list (Bass, Lead, Pad, Pluck, Keys, FX, Warm, Bright, Wide, Dirty, Clean, Heavy, Subtle). Presets sound finished through the synth alone: effects belong in racks (P2; W14).

## Automated test targets

| Metric | Target |
|---|---|
| Aliasing: a bright table swept from C1 to C8, every warp in the cycle and every spectral warp at its extremes | Non-harmonic energy under −90 dB (48 kHz) |
| Aliasing: FM, PM, RM, AM at full amount | Under −70 dB at Normal, −85 dB at High |
| Pitch | Within 0.1 cent at every octave |
| Position swept over a whole table; a table swapped while playing | No clicks (click score), no allocation |
| A table file read and written | WAV with `clm ` and `.wt`: bit-exact round trip; a 600-sample single cycle resampled within −100 dB of ideal |
| Unison | Levels within 0.5 dB from 1 to 16 copies; a rendered note identical every time (seeded phases) |
| MPE | A note's bend, pressure and slide move that note only |
| Matrix | Each source and target reaches its documented range |
| Block sizes | Bit-identical output for any block size (conformance) |
| `process()` | No allocation, lock or file access (conformance), with every warp and filter |
| CPU (Release, reference machine, 48 kHz) | 8 notes × 3 oscillators × 8 unison under 10 % of a core at Normal (B74 sets the final budget) |
| (step 2) Audio to wavetable | A recorded saw or vowel within −60 dB of its harmonics; pitch found within 1 cent on test tones |

## A/B cases (against the Live target)

| Case | Stimulus | Our settings | Benchmark settings | What to listen for |
|---|---|---|---|---|
| Supersaw pad | A held chord | 1 oscillator, Super unison of 7, Detune 30 % | Wavetable, Classic unison of 8 | Width, smoothness, no beating artefacts |
| Reese bass | Low notes | 2 detuned oscillators, Ladder filter | Wavetable, 2 oscillators | Weight, movement |
| Pluck | A fast arpeggio | Filter envelope, short decay | Same | Snap, clarity |
| Formant lead | A melody | Window Sync, LFO on Position | Wavetable, Modern mode warp | Vocal character, no aliasing on high notes |

## A/B log

Dated listening notes from the product owner, and the measurable checks derived from them.

## Questions for review

| # | Question | Recommendation |
|---|---|---|
| W1 | **How far to go** | Beyond Live, to the depth of the leading wavetable synths, as the product owner asked: [ADR 0050](../../docs/decisions/0050-the-wavetable-synth-goes-beyond-live.md) lifts ADR 0011's "instruments stay close to Live's" for this synth only. Innovation where the integrated DAW helps (W9–W13), not in more of everything |
| W2 | **Sources** | 3 wavetable oscillators, a sub and a noise source (7 kinds). No sample, granular or spectral oscillators: those are the Sampler's and the Granular Synth's jobs (ADR 0011's merge rule) |
| W3 | **Clean sound: how warps stay free of aliasing** | Each voice renders its own cycle from the table's spectrum every 64 samples, cut to what its pitch allows. Only FM, PM, RM and AM are oversampled, by Quality |
| W4 | **Warps** | Two per oscillator, from 28: 14 in the cycle (Bend and Asym in their directions), 4 between oscillators, 10 spectral (the list above) |
| W5 | **Unison** | Up to 16 copies per oscillator: 4 detune laws, stacks (octaves, fifths, chords, the key's chord), blend, width, and spreads of position and warp per copy |
| W6 | **Filters** | 2, serial to parallel by one knob; 14 types including ladder, diode, comb, formant and phaser; drive; each source sent to filter 1, 2, both or neither |
| W7 | **Modulation** | 4 envelopes with curves (Amp and Filter fixed), 8 drawn LFOs, 2 random sources, 8 macros, and a modulation matrix of 32 routes (source, target, amount, curve, a scaling source), with a section of its own on the Modulation page (the product owner liked the matrix). Routes also by dragging a source onto a knob, rings showing them, hover showing where a source goes: both fill the same matrix. Built once in the shared panel library, so M4's DAW-wide modulators look and work the same |
| W8 | **Tables and files** | About 80 factory tables made in-house (scripts in the repository) and from AKWF (CC0). User tables as files in `Documents/Segno/Wavetables`; presets and projects carry the tables they use, so they always play. Import WAV (with its frame data) and `.wt`; export the same |
| W9 | **The table editor, and where** | A recipe editor (sources on keyframes, a stack of changes, frames filled between), opened in the bottom panel where the piano roll opens; in the plugins, below the panel. Module panels stay six rows tall. Recorded in an ADR on approval (a module's editor in the bottom panel) |
| W10 | **Audio to wavetable** | Three ways (pitched, spectral, slices) with a preview. Dropped on an oscillator, or Make Wavetable from a clip in the arrangement (Clip menu, an MCP tool) |
| W11 | **The project's key** (step 3, after P3) | Stack's Key Chord plays the key's chord on each note; Snap to Key makes pitch modulation land on the key's notes |
| W12 | **Vary** (step 3) | Module > Vary: changes the sound by a set amount, leaving sections you lock alone; one undo step; again for another, Ctrl+Z to go back. Not a randomise |
| W13 | **CPU and Quality** | Quality in the header (Eco, Normal, High), and the CPU the synth takes beside it, so a heavy patch is seen, not guessed |
| W14 | **No effects inside** | The synth is an instrument; its effects are the chain's, and racks (P2) keep an instrument and its effects as one. Drive in the filters stays |
| W15 | **No arpeggiator inside** | The Arpeggiator MIDI effect (M4) plays every instrument. Drawn LFOs with steps cover rhythmic movement |
| W16 | **Expression** | MPE (bend, pressure, slide per note) and polyphonic aftertouch from the start; microtuning later, with a tuning for the whole DAW |
| W17 | **The size** | About 380 parameters, routes included, so hosts, automation and assistants see everything. The panel shows the selected oscillator, filter, envelope and LFO, on three pages (ADR 0051, the product owner's idea): about 1 200 px instead of 2 000–2 800 |
| W18 | **Building it** | Three steps (the synth; the tables; the song), each built, tested and signed off on its own. Assistants' tools come with each: `set_wavetable`, `list_wavetables`, then `make_wavetable`, then `vary_module` |
| W19 | **Its working name** | Wavetable Synth (`wavetable`, `Fwav`, `org.segnoaudio.wavetable`). A brand name with the naming pass before the first release (ADR 0008) |

## Sign-off

- [ ] Automated: unit, conformance and golden tests; validators
- [ ] Layout lint passes (`fd_panel_tests`)
- [ ] Snapshots reviewed (100% / 200%; expanded, collapsed, folded)
- [ ] Sound signed off by the product owner (date):
- [ ] Feel signed off by the product owner (date):
