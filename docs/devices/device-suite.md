# Device suite

The device suite is the core of the product (see [vision.md](../vision.md)). It must be **at least as comprehensive as Ableton Live Suite**: a producer should be able to make a complete, professional track without third-party plugins.

**Scope rules** ([ADR 0011](../decisions/0011-calibrated-ambition-and-consolidation.md)):
- **By default, match Live.** Every job a Live 12 Suite device does is covered by one of ours, with Live-level features and sound. For scale, Live 12 Suite ships 21 instruments, 59 audio effects, 14 MIDI effects, 17 MIDI tools and 6 modulators ([Ableton: compare editions](https://www.ableton.com/en/live/compare-editions/)).
- **Go beyond selectively.** Only where the benefit is large and the cost is moderate: mainly the mixing core (the EQ especially), plus small, cheap improvements anywhere. **Instruments stay close to Live's stock instruments.** The "Beyond Live" column says exactly where we go further; "–" means we match Live.
- **Merge only devices that do the same job** and differ mainly in depth or flavour. Devices whose job is clearly different stay separate.
- Bigger ideas go to the [innovation backlog](../innovation-backlog.md), not into device scope.

## Quality bar: every device, no exceptions

1. **Professional sound.** No audible aliasing (oversampling, anti-aliased waveshaping, band-limited oscillators), no zipper noise, a low noise floor. Holds up in A/B listening against its Live counterpart.
2. **Shows what it is doing, and the display is a control.** Live visual feedback, and where a curve is drawn you can grab it ([UI principles §7](../design/ui-principles.md#7-devices-look-and-behave-as-one-product)).
3. **Simple first, deep on demand.** Useful defaults, important controls up front, depth in the expanded view.
4. **Consistent.** Same panel anatomy, interaction rules, preset browser and state format as every other device.
5. **A good citizen.** Every parameter can be automated; latency and tail are reported; no allocations or locks on the audio thread; CPU cost is measured.
6. **Ships with presets.** A curated factory set that shows the device's range.
7. **Verified.** Automated tests and validators, plus product-owner listening sign-off ([ADR 0007](../decisions/0007-verification-without-ears.md)).

Each device gets a `devices/<name>/SPEC.md` ([template](SPEC-TEMPLATE.md)) before any code is written. Large devices **ship in stages**: a polished core first, then further stages in later milestones. Each stage must be complete and usable on its own.

## Tiers

- **Tier 1, Core:** enough to finish a track using only our devices. Milestones M0–M4.
- **Tier 2, Depth:** full coverage of Live Suite's roles. Milestones M5–M6. **Required for 1.0.**
- **Tier 3, Specialty:** physical modelling, spectral and other niche tools. Released in 1.x versions.

Working names are functional, not final brand names ([ADR 0008](../decisions/0008-naming-and-trade-dress.md)).

---

## Instruments

| Working name | Scope | Covers (Live) | Beyond Live | Tier | Milestone |
|---|---|---|---|---|---|
| **Subtractive Synth** | *Core:* 2 oscillators with shape morphing + noise, per-voice drift, low-pass filter (multiple types), 2 envelopes, LFO, small modulation matrix, MPE. *Stage 2:* dual oscillator→filter→amp paths with routing, in the expanded view | Drift, Analog | – | 1→2 | M1→M5 |
| **Sampler** | *Classic/one-shot* (M1) → *slice* with transient detection (M4) → *multisample zones*, velocity layers, round-robin (M5). Filter, envelopes, LFOs, warping | Simpler, Sampler | – | 1→2 | M1→M5 |
| **Drum Sampler** | One-shot drum sampler with built-in shaping (pitch, envelope, filter, simple effects), designed to sit on drum pads | Drum Sampler, Impulse (with Drum Rack) | – | 1 | M4 |
| **Drum Synth** | Synthesised drum voices in one device with a voice selector: kick, snare, hat, clap, tom, cymbal, FM percussion | Drum Synths (DS Kick, DS Snare, …) | – | 2 | M5 |
| **Wavetable Synth** | 2 wavetable oscillators + sub, wavetable import, unison modes, 2 filters with routing, modulation matrix, MPE | Wavetable | – | 2 | M5 |
| **FM Synth** | 4 operators, a set of fixed algorithms, a waveform per operator, filter, LFO, pitch envelope | Operator | – | 2 | M5 |
| **Granular Synth** | Grain playback from any sample: position, spray, grain size, density, pitch, MPE | Granulator III | – | 2 | M5 |
| Dual-engine MPE Synth | Two macro-oscillator engines, expressive MPE | Meld | – | 3 | 1.x |
| Mallet & Resonator | Physically modelled mallets and resonators | Collision | – | 3 | 1.x |
| Modelled Strings | Physically modelled string instruments | Tension | – | 3 | 1.x |
| Electric Piano | Modelled electric pianos | Electric | – | 3 | 1.x |

Bass and Poli (Max for Live instruments in Live) are covered by Subtractive Synth presets. Drum Rack is a DAW rack feature (see below).

## Audio effects

### EQ and filters

| Working name | Scope | Covers (Live) | Beyond Live | Tier | Milestone |
|---|---|---|---|---|---|
| **EQ** | *Core (M1):* any number of bands, added by clicking the curve; all band shapes; steep slopes; per-band L/R/M/S; pre/post spectrum; auto-gain; compact 3-band view for channel-strip use. *Stage 2 (M3):* dynamic bands with sidechain, linear-phase mode, EQ match, cross-track masking display | EQ Eight, EQ Three, Channel EQ | **Yes.** Aims at FabFilter Pro-Q: unlimited and dynamic bands, linear phase, matching, masking display | 1 | M1→M3 |
| **Filter** | Modelled filter types, envelope follower, LFO, sidechain | Auto Filter | – | 1 | M3 |

### Dynamics

| Working name | Scope | Covers (Live) | Beyond Live | Tier | Milestone |
|---|---|---|---|---|---|
| **Compressor** | Peak/RMS, knee, lookahead, sidechain + sidechain EQ, dry/wet. A "bus/glue" character mode covers Glue Compressor | Compressor, Glue Compressor | Small: scrolling gain-reduction history | 1 | M2 |
| **Limiter** | True peak, lookahead, maximise mode, mid/side | Limiter | Small: built-in loudness readout | 1 | M2 |
| **Gate** | Gate/expander, sidechain, hysteresis, flip mode | Gate | – | 1 | M2 |
| **Multiband Dynamics** | 3 bands, upward and downward compression and expansion | Multiband Dynamics | – | 1 | M4 |
| **Drum Bus** | Drive, crunch, transients, low-end "boom" resonator, compression | Drum Buss | – | 1 | M4 |
| **Transient Shaper** | Attack/sustain shaping | (no Live device) | **Yes.** A mixing staple that Live lacks | 2 | M6 |
| **De-esser** | Split-band sibilance control with listen mode | (no Live device) | **Yes.** A mixing staple that Live lacks | 2 | M6 |

### Distortion and character

| Working name | Scope | Covers (Live) | Beyond Live | Tier | Milestone |
|---|---|---|---|---|---|
| **Saturator** | Shaping curves (anti-aliased), oversampling, colour EQ, drive, dry/wet, transfer display | Saturator | – | 1 | M1 |
| **Distortion** | Multi-stage distortion with serial, parallel, multiband and mid/side routing; shapers, filters, feedback, modulation | Roar | – | 1 | M4 |
| **Pedal** | Guitar-pedal-style overdrive, distortion and fuzz with tone controls | Overdrive, Pedal | – | 2 | M6 |
| **Tube** | Tube saturation driven by the input envelope | Dynamic Tube | – | 2 | M6 |
| **Amp & Cabinet** | Amp models and cabinet simulation (each usable alone) | Amp, Cabinet | – | 2 | M6 |
| **Bitcrusher** | Sample-rate and bit-depth reduction | Redux | – | 2 | M6 |
| Erosion | Noise- and sine-modulation degradation | Erosion | – | 3 | 1.x |
| Vinyl | Vinyl crackle and tracing distortion | Vinyl Distortion | – | 3 | 1.x |

### Modulation effects

| Working name | Scope | Covers (Live) | Beyond Live | Tier | Milestone |
|---|---|---|---|---|---|
| **Chorus** | Chorus, ensemble, vibrato ([SPEC](../../devices/chorus/SPEC.md); named Chorus, C11) | Chorus-Ensemble | Sync; Shape and Offset in every mode; the pitch swing shown in cents | 1 | M3 |
| **Phaser & Flanger** | Phaser, flanger, doubler; sync; envelope follower ([SPEC](../../devices/phaser-flanger/SPEC.md)) | Phaser-Flanger | Through Zero; a Low Cut that keeps the lows whole; the response drawn | 1 | M3 |
| **Auto Pan & Tremolo** | LFO panning and tremolo, sync, shapes ([SPEC](../../devices/auto-pan-tremolo/SPEC.md)) | Auto Pan | A Low Cut that keeps the lows still; the movement drawn | 1 | M3 |
| **Shifter** | Frequency shift, ring modulation, pitch shift | Shifter | – | 2 | M6 |

### Delay

| Working name | Scope | Covers (Live) | Beyond Live | Tier | Milestone |
|---|---|---|---|---|---|
| **Delay** | Clean and character modes (tape, analog-style), ping-pong, filter in the feedback path, modulation, ducking, freeze, sync and swing | Delay, Echo | – | 1 | M3 |
| **Grain Delay** | Granular delay with pitch and spray | Grain Delay | – | 2 | M6 |
| **Stutter** | Rhythmic capture and repeat, gating, pitch decay | Beat Repeat | – | 2 | M6 |
| Filter Delay | Three filtered delay lines (L, R, L+R) | Filter Delay | – | 3 | 1.x |

### Reverb and space

| Working name | Scope | Covers (Live) | Beyond Live | Tier | Milestone |
|---|---|---|---|---|---|
| **Reverb** | *Core (M3):* algorithmic rooms, halls, plates, freeze, decay by frequency band. *Stage 2 (M6):* convolution engine with an impulse-response library and import; hybrid blend | Reverb, Hybrid Reverb | – | 1→2 | M3→M6 |

### Spectral and creative

| Working name | Scope | Covers (Live) | Tier | Milestone |
|---|---|---|---|---|
| Vocoder | Channel vocoder, internal carrier, formant shift | Vocoder | 2 | M6 |
| Spectral Resonator | Spectral partials driven by MIDI or pitch | Spectral Resonator | 3 | 1.x |
| Spectral Time | Spectral freeze and delay | Spectral Time | 3 | 1.x |
| Resonators | Tuned resonator bank | Resonators | 3 | 1.x |
| Body Resonance | Physically modelled resonant bodies | Corpus | 3 | 1.x |
| Pitch Correction | Real-time pitch correction and harmonisation | Auto Shift | 3 | 1.x |
| Looper | Recording and overdub looper | Looper | 3 | 1.x |

### Utility and analysis

| Working name | Scope | Covers (Live) | Beyond Live | Tier | Milestone |
|---|---|---|---|---|---|
| **Utility** | Gain, pan, width, mono, phase flip, DC filter, mute | Utility | – | 1 | M0 |
| **Spectrum** | FFT analyser, averaging, peak hold, mid/side; *stage 2 (M3):* a spectrogram view | Spectrum | **Yes.** Compare with any track without routing, shading where both are loud; a snapshot to compare before and after; a reference song's average spectrum, level-matched; a side overlay; peak labels with notes. Reasons people install a separate analyser, cheap in an integrated DAW ([SPEC](../../devices/spectrum/SPEC.md)) | 1 | M2 → M3 |
| **Tuner** | Chromatic tuner with reference pitch | Tuner | – | 1 | M3 |
| **Loudness Meter** | LUFS (integrated, short-term, momentary), true peak, correlation | (no Live device) | **Yes.** Cheap and essential for finishing tracks | 1 | M3 |

## MIDI effects

Delivered **natively in our DAW first**. Many hosts, including Live, have limited support for MIDI-effect plugins, so the plugin builds are best-effort.

| Working name | Scope | Covers (Live) | Tier | Milestone |
|---|---|---|---|---|
| **Arpeggiator** | Patterns, rate/sync, gate, steps, transposition, retrigger | Arpeggiator | 1 | M4 |
| **Chord** | Add up to 6 intervals | Chord | 1 | M4 |
| **Scale** | Snap to scale; follows the project-wide key and scale | Scale | 1 | M4 |
| Note Echo | MIDI delay with pitch and velocity feedback | Note Echo | 2 | M6 |
| Note Length | Fixed or gated lengths, trigger on note-off | Note Length | 2 | M6 |
| Pitch | Transpose, range limits | Pitch | 2 | M6 |
| Velocity | Curves, ranges, randomisation | Velocity | 2 | M6 |
| Random | Probability-based pitch within a scale | Random | 2 | M6 |
| CC Control | Automatable CC senders | CC Control | 2 | M6 |

## Racks, modulators and MIDI tools (DAW features)

These depend on the host, so they live in the DAW. The plugin builds offer modulation inside each device.

| Feature | Scope | Covers (Live) | Milestone |
|---|---|---|---|
| Racks + macros | Instrument, effect and MIDI racks; parallel chains; macros and macro variations | Instrument/Audio Effect/MIDI Effect Rack | M4 |
| Drum Rack | Pads, each holding any device chain; choke groups; drop samples onto pads | Drum Rack | M4 |
| Modulators | LFO, envelope follower, shaper, MIDI expression → any parameter, by drag and drop | Modulators | M4 |
| MIDI tools | Piano-roll generators and transformations (strum, arpeggiate, humanise, chordify, rhythm) | MIDI Tools | M6 / 1.x |

## Coverage check

Every Live 12 Suite device role must map to one of ours, or be an explicit non-goal in [vision.md](../vision.md) (for example Max for Live, External Instrument routing and CV tools). The "Covers" columns are reviewed at each milestone.

## Sound content

- **Factory presets** for every device: curated, tagged and searchable. A device without good presets is not done.
- **Core content:** drum kits, wavetables, impulse responses and a few multisampled instruments, all **CC0 or made in-house**.
- **Packs later**, as an optional download system with community contributions.
- We do not try to match Live's 70+ GB library.
