# Auto Pan & Tremolo: device spec

- **Status:** Approved by the product owner (2026-10-12, with every recommendation of T1–T12); IDs are permanent. Building (B4)
- **Type:** Audio effect
- **Tier / milestone:** Tier 1, M3 (B4)
- **Live target:** Auto Pan-Tremolo. References only; nothing is copied ([ADR 0011](../../docs/decisions/0011-calibrated-ambition-and-consolidation.md)).

## Purpose

Rhythmic movement of level and place, in two modes:
- **Panning:** the sound moves from side to side, from a slow drift across the stereo field to a quick bounce in time with the song.
- **Tremolo:** the level pulses, from the gentle throb of an old amp to the hard chop of a gate. A harmonic tremolo, where the lows and highs pulse in turn, and a vintage, optical-style one are a switch away.

One module with two modes, as Live's Auto Pan-Tremolo has, built on the modulation core's LFO that follows the song (M3 A2, B2), as the Chorus and the Phaser & Flanger are.

## Beyond Live

- **Low Cut that keeps the lows whole,** as in the Chorus and the Phaser & Flanger: below it, the sound doesn't move, so a kick and a bass stay centred and steady while the rest pans or pulses.
- **The movement drawn** from the functions the audio path uses, with its depth in dB (Tremolo) or its swing (Panning) read out, as the Chorus's display does.

## Stages

| Stage | Scope | Milestone |
|---|---|---|
| Core | Everything below | M3 |
| Later | Live's Dynamic Frequency Modulation (the input's level speeding up or slowing the LFO), its shark-tooth wave and its Time mode (a period in milliseconds); a step pattern for rhythmic gating | With demand |

## Parameters

| ID (permanent) | Name | Description | Range | Default | Unit | Curve / skew | Smoothing | Automatable |
|---|---|---|---|---|---|---|---|---|
| `mode` | Mode | The kind of movement: Panning moves the sound from side to side, Tremolo makes its level pulse. | Panning, Tremolo | Panning | choice | – | 50 ms crossfade | Yes |
| `rate` | Rate | How fast the movement goes, in cycles per second. | 0.01 … 40 | 1 | Hz | centre 2 Hz | 20 ms multiplicative | Yes |
| `sync` | Sync | Moves in time with the song: the rate becomes a note value. | Off / On | On | toggle | – | the movement's glide | Yes |
| `div` | Sync Rate | How fast the movement goes as a note value, one cycle per note, while Sync is on. | 8 bars … 1/32, as the Chorus | 1/4 | choice | – | the movement's glide | Yes |
| `shape` | Shape | The movement's wave: smooth, even, rising, falling, jumping, stepping at random or drifting. | Sine, Triangle, Saw Up, Saw Down, Square, Random, Drift | Sine | choice | – | the edges' smoothing | Yes |
| `amount` | Amount | How far it moves: how far to each side, or how deep the level dips. | 0 … 100 | 50 | % | linear | 20 ms | Yes |
| `edge` | Edge | Pushes the wave to its extremes for a harder, choppier movement, or (negative) eases it for a softer one. | −100 … +100 | 0 | % | linear | 20 ms | Yes |
| `phase` | Phase | Where the cycle starts against the beat, while Sync is on: 180° starts half a cycle late. | 0 … 360 | 0 | ° | linear | 50 ms glide | Yes |
| `invert` | Invert | Turns the wave upside down: a pulse that dips becomes one that swells, a pan that starts left starts right. | Off / On | Off | toggle | – | 20 ms crossfade | Yes |
| `attack` | Attack | Lets each new note through untouched, the movement fading in over this time after it. | 0 … 1 000 | 0 | ms | centre 100 ms | – | Yes |
| `stereo` | Stereo | How the two sides pulse: Offset moves them apart in step, Spin at different speeds. | Offset, Spin | Offset | choice | – | the movement's glide | Yes |
| `offset` | Offset | How far apart the two sides pulse: together at 0°, in turn at 180°. | 0 … 180 | 0 | ° | linear | 50 ms glide | Yes |
| `spin` | Spin | How much faster the right side pulses than the left, so the two drift in and out of step. | 0 … 100 | 20 | % | linear | 20 ms | Yes |
| `harmonic` | Harmonic | Splits the tremolo at 600 Hz so the lows and highs pulse in turn, as the harmonic tremolos of old amps did. | Off / On | Off | toggle | – | 20 ms crossfade | Yes |
| `vintage` | Vintage | Makes the tremolo behave like an old optical one: it dips sooner, comes back slower and warms the sound a little. | Off / On | Off | toggle | – | 20 ms crossfade | Yes |
| `lowcut` | Low Cut | Keeps the lows out of the effect, so they stay solid and centred. | Off / On | Off | toggle | – | 20 ms crossfade | Yes |
| `lowcut.freq` | Low Cut Freq | Below this frequency, the sound passes through untouched. | 20 … 2 000 | 120 | Hz | centre 200 Hz | 20 ms multiplicative | Yes |
| `output` | Output | The level after the effect. | −24 … +12 | 0 | dB | linear | 20 ms, linear in amplitude | Yes |

No non-parameter state. State version 1.

**Ranges and limits:**
- The note values, the synced cycle's limit (100 s) and the way it stays locked to the song are the Chorus's (`dsp::lfoNoteValues`, `dsp::SongLfo`).
- **The wave** W (−1 to 1) is the LFO's value at its position plus Phase, shaped by Edge (W = sign(v) × |v|^k, with k from 1/8 at +100 % to 8 at −100 %, a power of two in between), and flipped by Invert.
- **Panning:** the sound's place is Amount × W (−1 left, +1 right), panned by a balance with equal power: in the middle each side is as it was; at a side, that side is 3 dB louder and the other silent.
- **Tremolo:** each side's level is 1 − Amount × (1 − W) / 2: full at the wave's top, down to 1 − Amount at its bottom (silence at 100 %).
- **Edges:** the level and place never change faster than a one-pole of 1 ms allows, so a square or random wave chops cleanly, without clicks.
- Rate goes down to 0.01 Hz, a cycle every 100 s, so Live's Time mode (a period in milliseconds) isn't needed.

## DSP design

```
in ─> Low Cut crossover ─┬─ below ────────────────────────────────────────────────────┐
                         └─ above ─┬─> Panning: the balance, at Amount × W ─────────────┤
                                   └─> Tremolo: each side's level (Harmonic: the lows   │
                                       and the highs at 600 Hz, in turn; Vintage) ──────┴─> Output ─> out
the LFO (SongLfo) ─> position + Phase ─> value ─> Edge ─> Invert ─> × attack ramp ─> W
transient detector (level detector) ──> the attack ramp
```

**The movement:**
- **The LFO:** the modulation core's `SongLfo` (the Chorus's and the Phaser & Flanger's): free in Hz, or one cycle per note value locked to the song's position, gliding over jumps.
- **Each side's position** (Tremolo): the left's, plus Offset for the right, or the right's own, faster by Spin. Panning has one position, the left's.
- **The wave** is `ModulationLfo::value` at the position plus Phase, shaped by Edge and flipped by Invert: a pure function, `AutoPanTremolo::wave`, which the display calls too.
- **Attack:** the dynamics core's `LevelDetector` (peak) feeds a fast and a slow follower; when the fast one rises 6 dB above the slow one (a new note or hit, above −50 dBFS, at most one every 50 ms), the movement's depth drops to nothing and comes back up over Attack. At 0 ms there's no ramp. Tuned by ear.
- **Smoothing:** the levels (or the balance) go through a 1 ms one-pole, so jumps in the wave don't click. Glides on Phase, Offset and the LFO's phase jumps, as in the Chorus.

**Panning:** an equal-power balance on the band above Low Cut: gains √2·cos((1 + p)·π/4) left and √2·sin((1 + p)·π/4) right, p the place. A stereo source keeps its image as it moves; a mono one pans.

**Tremolo:**
- Each side's level, from its own wave, on the band above Low Cut.
- **Harmonic:** a Linkwitz-Riley crossover at 600 Hz splits that band. The highs take the level above; the lows take its mirror, 1 − Amount × (1 + W) / 2, so one band swells as the other dips.
- **Vintage:** the level is squared (an optical cell's response: it dips sooner) and follows a one-pole that falls in 3 ms and rises in 30 ms (it comes back slower); the result then passes the modulation core's soft saturation (the Chorus's Warmth at 30 %). Tuned by ear.

**Low Cut:** the shared crossover (`dsp::Crossover`, 24 dB/oct). Below it, the sound passes through unmoved, at its level, in both modes. Off by default (T10).

**Timing and housekeeping:**
- **Mode changes:** the old mode and the new run side by side for 50 ms and crossfade.
- **Latency:** none. **Tail:** none (the output follows the input; the 1 ms smoothing aside).
- Parameters are read on control ticks (every 32 samples) and smoothed per sample, as in the other modules.
- **Analysis slots:** 0/1 = input peak L/R; 2/3 = output peak L/R; 4 = the LFO's position (cycles), for the display's marker; 5 = the rate it follows (Hz); 6 = the attack ramp now (0 to 1).

## UI layout

On the module grid ([device-panels.md](../../docs/design/device-panels.md)), sections run left to right. Each is written `id` TITLE [columns], with controls at (column, row) and spans; knobs are 1 × 3 cells.

| Section | Controls |
|---|---|
| `effect` PANNING or TREMOLO (titled by the mode) [6] | Movement display (0, 0, 4 × 3) · Mode, stacked [Panning / Tremolo] (4, 0, 2 × 2) · Shape dropdown (4, 2, 2 wide) · Rate (0, 3) · Amount (1, 3) · Edge (2, 3) · Phase (3, 3) · Attack (4, 3) · Sync toggle (5, 3) · Invert toggle (5, 4) |
| `tremolo` TREMOLO [3] | Offset or Spin (0, 0) · Stereo [Offset \| Spin] (1, 0, 2 wide) · Harmonic toggle (1, 1, 2 wide) · Vintage toggle (1, 2, 2 wide) |
| `output` OUTPUT [2] | Output (0, 0) · Low Cut toggle (0, 3, 2 wide) · Low Cut Freq bar, named Freq (0, 4, 2 wide) |
| meter strip | In and out |

**The movement display:**
- It shows two cycles of the movement and a marker where it is now (while the module plays).
- **Tremolo:** each side's level from silence at the bottom to full at the top, the left side's over the right's; Harmonic adds the lows' mirror, fainter.
- **Panning:** the sound's place, left at the top and right at the bottom (labelled L and R in a gutter, as the Delay's display labels its lanes).
- **Its line** reads the depth ("dips to −6.0 dB", "dips to silence") or the swing ("50L–50R"), and the rate in Hz (also while synced).
- **Dragging:** sideways changes Rate (note values while synced), up and down Amount, as the Chorus's display. Shift drags finely.

**Controls:**
- **Rate follows Sync,** and **Offset or Spin follows Stereo,** as in the Phaser & Flanger.
- **Dimming:** the TREMOLO section in Panning; Phase while Sync is off; Freq while Low Cut is off.
- About 680 px wide (11 columns and the meter strip).

## Factory presets

Built and named by Claude after approval ([presets.md](../../docs/design/presets.md) §13); signed off by the product owner by ear. Planned (14):

- **Slow pan** (Subtle, Wide): A gentle sine drifting side to side over two bars.
- **Eighth pan** (Drums, Wide): A bounce from side to side on every eighth.
- **Hard pan steps** (FX, Wide): A square wave jumping from side to side on the eighths.
- **Random pan** (FX, Wide): Sixteenths landing anywhere across the stereo field.
- **Drifting pan** (Pad, Wide): Slow, wandering movement for pads, not tied to the tempo.
- **Bass-safe pan** (Drums, Bus): A quarter-note pan with the lows held in the middle.
- **Classic tremolo** (Guitar, Keys): The steady throb of an amp's tremolo.
- **Vintage tremolo** (Keys, Warm): An optical tremolo's softer, warmer pulse, for electric pianos.
- **Harmonic tremolo** (Guitar, Warm): The lows and highs pulsing in turn, as on old amps.
- **Stereo tremolo** (Keys, Wide): The sides pulsing in turn on the eighths.
- **Spinning tremolo** (Wide, Keys): The sides pulsing at different speeds, drifting in and out of step.
- **Choppy gate** (FX, Heavy): Hard sixteenth-note chops, for pads and synths.
- **Beat pump** (Pad, Bus): The level dips on each beat and swells back, like a sidechain.
- **Swell after the hit** (Guitar, Subtle): Notes start clean and the tremolo fades in behind them.

## Automated test targets

| Metric | Target |
|---|---|
| Panning: equal power | A mono source: left² + right² stays within 0.05 dB of the input's power at every place; the place follows `wave` within 1 % |
| Tremolo: the level | Each side's level follows `levels` within 0.1 dB (Edge 0, a slow sine); Amount 100 % dips to silence (under −100 dB) |
| Offset and Spin | Offset 0°, a mono input: left and right identical; 180°: one side's level is the other's mirror; Spin 100 %: twice as many cycles on the right |
| Harmonic | A 200 Hz and a 3 kHz tone pulse in turn: their levels' correlation under −0.9 |
| Edge | At +100 %, a sine spends over 70 % of its cycle beyond 0.9 of its reach; at −100 %, under 10 % |
| Square edges | A square wave at Amount 100 %: no step in level faster than the 1 ms one-pole (the conformance click score at the chops) |
| Sync and Phase | Synced, the movement is at the same place after a jump in the song as played through to it; Phase 90° moves it a quarter cycle |
| Attack | After silence, a burst's first 5 ms come through unmoved, and the movement is at full depth Attack later (within 5 ms) |
| Low Cut | A 40 Hz sine at −12 dBFS, Low Cut at 200 Hz: within 0.1 dB and unmoved in both modes, Amount 100 % |
| Amount 0 | The output is the input (< −120 dBFS), in both modes |
| Display vs audio | The display draws `wave` and `levels`; the readout's depth matches the measured dip within 0.1 dB |
| Conformance suite | All pass (no allocation, no denormals, parameter jumps without clicks) |

## A/B cases (against the Live target)

Deferred until a Live machine is available.

## A/B log

–

## Questions for review

| # | Question | Recommendation |
|---|---|---|
| T1 | **One module, two modes** | Panning and Tremolo, as Live. The first section takes the mode's name (PANNING, TREMOLO), as the Phaser & Flanger's does |
| T2 | **Panning** | One movement of the sound's place, through an equal-power balance: in the middle nothing changes, at a side that side is 3 dB louder. Live moves the two sides' levels with two LFOs half a cycle apart; the Tremolo's Offset at 180° gives that sound too |
| T3 | **Tremolo's stereo** | Offset or Spin between the sides, with one knob that follows the switch, as the Phaser & Flanger; Offset 0° by default (the classic tremolo, both sides together). Dimmed in Panning |
| T4 | **The waves** | The Phaser & Flanger's seven (Sine, Triangle, Saw Up, Saw Down, Square, Random, Drift), Sine by default. Leave out Live's shark tooth for now |
| T5 | **Rate and Sync** | Hz, or note values locked to the song, as the other modulation modules. Sync on by default at 1/4: rhythmic panning and tremolo usually follow the song. Live's Time mode (a period in milliseconds) isn't needed: Rate goes down to 0.01 Hz |
| T6 | **Phase** | Where the cycle starts against the beat, while synced, as Live's Phase Offset: it puts a tremolo's dip on the off-beat, or a pan's start on the right. Dimmed while Sync is off |
| T7 | **Edge** | Live's Shape, renamed so it isn't confused with the wave's Shape: positive pushes the wave to its extremes (choppier), negative eases it (softer), the same in both modes |
| T8 | **Attack** | As Live: after a new note or hit, the movement fades in over Attack, so attacks come through clean. 0 (off) by default |
| T9 | **Harmonic and Vintage** | Both, Tremolo only, as Live: Harmonic splits at a fixed 600 Hz; Vintage is an optical-style curve with a little saturation |
| T10 | **Low Cut** | As in the Chorus and the Phaser & Flanger, but Off by default: a pan or a tremolo is expected to move the whole sound. On, a kick and a bass stay centred and steady (the Bass-safe pan preset) |
| T11 | **Output, no Dry/Wet** | An Output level, as every module; no Dry/Wet, as Live: Amount already sets how much of the effect there is |
| T12 | **Name and IDs** | Named **Auto Pan & Tremolo**, as the Phaser & Flanger names both its effects. Device `auto-pan-tremolo`, plugin code `Fapt`, CLAP `org.segnoaudio.auto-pan-tremolo` (files "Segno Auto Pan and Tremolo"); section IDs as above (`effect`, `tremolo`, `output`) |

## Sign-off

- [ ] Automated: unit, conformance and golden tests; validators (pluginval at strictness 10 and clap-validator)
- [ ] Layout lint passes (`fd_panel_tests`)
- [ ] Snapshots reviewed (100% / 200%; expanded, collapsed, folded; every factory preset)
- [ ] Sound signed off by the product owner, with the factory presets
- [ ] Feel signed off by the product owner
