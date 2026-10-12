# Phaser & Flanger: device spec

- **Status:** Approved by the product owner (2026-10-12, with every recommendation of P1–P12); IDs are permanent. Built (B3, 2026-10-12): what building settled is under "Settled while building". Done: signed off by the product owner (2026-10-12)
- **Type:** Audio effect
- **Tier / milestone:** Tier 1, M3 (B3)
- **Live target:** Phaser-Flanger. References only; nothing is copied ([ADR 0011](../../docs/decisions/0011-calibrated-ambition-and-consolidation.md)).

## Purpose

Sweeping effects, in three modes:
- **Phaser:** notches that sweep up and down through the sound, from a gentle swirl to a deep, resonant whoosh.
- **Flanger:** a comb of notches from a very short delay, from a metallic shimmer to the jet-plane sweep.
- **Doubler:** a second take a few milliseconds behind, moving a little, so one part sounds like two.

One module with three modes, as Live's Phaser-Flanger has. The sweep comes from an LFO (free or synced to the song), an envelope follower (the input's level), or both. It's built on the modulation core (M3 A2), as the Delay and the Chorus are, and on the dynamics core's level detector.

## Beyond Live

- **Through Zero:** the flanger's sweep can pass the dry sound itself, for the deep, cancelling jet of two-tape flanging. Live's flanger stops short of zero.
- **Low Cut that keeps the lows whole,** as the Chorus's does: below it, the sound is untouched and stays at its level at any mix. Live's Safe Bass is a high-pass on the effect.
- **The response drawn:** the display draws the notches (or the comb) where they are right now, and the range they sweep, from the functions the audio path uses. Drag in it to move them.

## Stages

| Stage | Scope | Milestone |
|---|---|---|
| Core | Everything below | M3 |
| Later | Live's second LFO and its Blend (sweeping Spread as well as Center); Duty Cycle; stepped and analog triangle shapes; an endless, barber-pole phaser | With demand |

## Parameters

| ID (permanent) | Name | Description | Range | Default | Unit | Curve / skew | Smoothing | Automatable |
|---|---|---|---|---|---|---|---|---|
| `mode` | Mode | The kind of effect: Phaser sweeps notches through the sound, Flanger a comb of them, Doubler adds a moving second take. | Phaser, Flanger, Doubler | Phaser | choice | – | 50 ms crossfade | Yes |
| `phaser.center` | Center | Where the phaser's notches sit, in the middle of their sweep. | 50 … 15 000 | 800 | Hz | centre 1 kHz | 20 ms multiplicative | Yes |
| `phaser.notches` | Notches | How many notches the phaser sweeps: more is deeper and more intense. | 1 … 12 | 2 | – (whole) | – | 30 ms crossfade | Yes |
| `phaser.spread` | Spread | How far apart the phaser's notches are: bunched close together, or spaced wide. | 0 … 100 | 50 | % | linear | 20 ms | Yes |
| `flanger.time` | Time | The flanger's longest delay; the sweep shortens it from there, towards zero. | 0.1 … 10 | 2 | ms | centre 2 ms | glide | Yes |
| `flanger.zero` | Through Zero | Holds the dry sound back 1 ms, so the flanger's sweep passes through it, for the deep jet of tape flanging. | Off / On | Off | toggle | – | – (changes the latency) | No |
| `doubler.time` | Time | How far behind the dry sound the second take plays. | 5 … 60 | 20 | ms | centre 20 ms | glide | Yes |
| `feedback` | Feedback | How much of the effect goes back through it, for a resonant, metallic edge. | 0 … 95 | 30 | % | linear | 20 ms | Yes |
| `invert` | Invert | Flips the feedback's polarity, for a hollower sound. | Off / On | Off | toggle | – | 20 ms crossfade | Yes |
| `warmth` | Warmth | Darkens and gently saturates the effect, like an old analog pedal, more so on each pass of the feedback. | 0 … 100 | 0 | % | linear | 20 ms | Yes |
| `rate` | Rate | How fast the sweep moves, in cycles per second. | 0.01 … 20 | 0.4 | Hz | centre 1 Hz | 20 ms multiplicative | Yes |
| `sync` | Sync | Moves the sweep in time with the song: the rate becomes a note value. | Off / On | Off | toggle | – | the sweep's glide | Yes |
| `div` | Sync Rate | How fast the sweep moves as a note value, one cycle per note, while Sync is on. | 8 bars … 1/32, as the Chorus | 1 bar | choice | – | the sweep's glide | Yes |
| `shape` | Shape | The sweep's wave: smooth, even, rising, falling, jumping, stepping at random or drifting. | Sine, Triangle, Saw Up, Saw Down, Square, Random, Drift | Triangle | choice | – | the LFO's smoothing | Yes |
| `amount` | Amount | How far the sweep moves: further is a deeper, more obvious effect. | 0 … 100 | 50 | % | linear | 20 ms | Yes |
| `stereo` | Stereo | How the two sides differ: Offset sweeps them apart in step, Spin at different speeds. | Offset, Spin | Offset | choice | – | the sweep's glide | Yes |
| `offset` | Offset | How far apart the two sides sweep: together at 0°, in opposite directions at 180°. | 0 … 180 | 90 | ° | linear | 50 ms glide | Yes |
| `spin` | Spin | How much faster the right side sweeps than the left, so the two drift in and out of step. | 0 … 100 | 20 | % | linear | 20 ms | Yes |
| `env` | Envelope | Lets the input's level move the sweep: playing louder pushes it further. | Off / On | Off | toggle | – | 20 ms crossfade | Yes |
| `env.amount` | Env Amount | How far, and which way, the input's level moves the sweep. | −100 … +100 | +50 | % | linear | 20 ms | Yes |
| `env.attack` | Env Attack | How quickly the sweep follows the input getting louder. | 1 … 200 | 10 | ms | centre 20 ms | – | Yes |
| `env.release` | Env Release | How quickly the sweep falls back once the input gets quieter. | 10 … 2 000 | 250 | ms | centre 250 ms | – | Yes |
| `lowcut` | Low Cut | Keeps the lows out of the effect, so they stay solid and centred. | Off / On | On | toggle | – | 20 ms crossfade | Yes |
| `lowcut.freq` | Low Cut Freq | Below this frequency, the sound passes through untouched. | 20 … 2 000 | 120 | Hz | centre 200 Hz | 20 ms multiplicative | Yes |
| `mix` | Dry/Wet | Blends the original sound with the effect; 50 % gives the deepest notches. | 0 … 100 | 50 | % | linear | 20 ms | Yes |
| `output` | Output | The level after the effect. | −24 … +12 | 0 | dB | linear | 20 ms, linear in amplitude | Yes |

No non-parameter state. State version 1.

**Ranges and limits:**
- Each mode keeps its own settings (Center, Notches and Spread; the Flanger's Time and Through Zero; the Doubler's Time), so switching modes and back loses nothing.
- **Where the sweep goes** (L is the LFO's value, −1 to 1; E is the envelope, 0 at −48 dBFS to 1 at 0 dBFS):
  - **Phaser:** the notches sit at Center × 2^(3 × (Amount × L + Env Amount × E)): up to three octaves either side, and up to three more from the envelope. Held between 20 Hz and 18 kHz.
  - **Flanger:** the delay is Time × (1 − Amount × (1 − L) / 2 − Env Amount × E): at Time when the LFO is at its top, sweeping shorter by Amount, down to zero at 100 %. The envelope shortens it further (or, negative, lengthens it, up to twice Time).
  - **Doubler:** the delay is Time + 5 ms × (Amount × L + Env Amount × E): it moves either way around Time. Never under 1 ms.
- The note values and the synced cycle's limit (100 s) are the Chorus's.
- Spin makes the right side's LFO up to twice as fast as the left's (at 100 %). With Sync on, the left side stays locked to the song.
- Feedback stops at 95 %, so the tail always ends. High feedback can ring loudly where it resonates; Warmth's soft clip bounds it.

## DSP design

```
in ─┬─> Low Cut crossover ─┬─ above ─> + ─> Phaser: the all-pass chain, tuned to the sweep ─────┬─> tone (Warmth) ─┬─> wet ─┐
    │                      │           │    Flanger, Doubler: line L/R (ModulatedDelay) at the sweep ┘                  │        │
    │                      │           ^                                                                                │        │
    │                      │           └──── × feedback (− if Invert) <─────────────────────────────────────────────────┘        │
    │                      └─ below ───────── × (1 − dry gain) ─────────────────────────────────────────────────────────────────┤
    ├─> level detector (Env Attack, Env Release) ──> E, into the sweep                                                           │
    └─> dry (Through Zero: held back 1 ms) ──── × dry gain ─── mix (equal power) ───────────────────────────────────────────────┴─> Output ─> out
```

**The sweep:**
- **The LFO:** a modulation-core `ModulationLfo` per side, free in Hz or synced to one cycle per note value, locked to the song's position while it plays (as the Chorus's). Its value is smoothed (a few milliseconds), so Square and Random step without clicks.
- **Stereo:** Offset makes the right side's LFO lead the left's by Offset; Spin runs the right side's faster (up to twice), so they drift in and out of step.
- **The envelope follower:** the dynamics core's `LevelDetector` (peak) on the input, with Env Attack and Env Release; its level in dB, from −48 dBFS to 0 dBFS, is E (0 to 1). Off: E is 0.
- **The position** of each side's sweep, and what it does in each mode, is a pure function, `PhaserFlanger::sweep`. The display calls it too.
- **Glides:** Time, Center, Offset and the LFO's phase jumps (Sync switched on, a new note value, the song jumping) glide, as in the Chorus, so nothing clicks.

**Phaser:**
- **The all-pass chain:** Notches second-order all-pass sections in a row, all tuned to the sweep's frequency, their Q set by Spread (from 4 at 0 % to 0.3 at 100 %, on a curve tuned by ear). Mixed with the dry sound, each section makes one notch.
- The sections are retuned every sample, so a fast sweep has no zipper.
- A change of Notches crossfades between the old chain and the new over 30 ms.
- **Feedback:** what comes out of the chain, after the tone stage, goes back into its input, times Feedback (negated by Invert): the peaks between the notches ring.

**Flanger and Doubler:**
- **The lines:** a modulation-core `ModulatedDelay` per side, read at the sweep's delay, as the Chorus's voices are.
- **Feedback:** the line's output, after the tone stage, goes back into it, times Feedback (negated by Invert).
- **Through Zero (Flanger):** the dry sound is held back 1 ms, so a sweep reaching under 1 ms passes it. At the crossing the two are the same sound; with feedback inverted, the sound folds in on itself there. The 1 ms is reported as latency, so the track stays in time; changing it changes the latency, so it isn't automatable (as the Compressor's Lookahead, ADR 0020).

**Tone, Low Cut and mix:** as the Chorus.
- **Warmth** is the core's `FeedbackPath` on each side's effect: its high cut falls from 20 kHz to 4 kHz and its saturation rises from 0 to 1. It shapes both the wet and what feeds back, and bounds the loop at any feedback.
- **Low Cut** is a Linkwitz-Riley crossover (24 dB/oct). The band above feeds the effect. The dry sound is the whole input; where the mix takes dry sound away, the band below takes its place, so the lows keep their level and never move.
- **Mix:** equal power. At 50 %, dry and wet are equal, so the notches are at their deepest, and it's about as loud as the dry sound. At 0 % it's the input exactly (held back 1 ms with Through Zero).
- **Mode changes:** the old mode and the new run side by side for 50 ms and crossfade.

**Timing and housekeeping:**
- **Latency:** none; 1 ms with Through Zero on.
- **Tail:** the time the feedback takes to fall 60 dB (a pass is the line's delay, or the all-pass chain's delay at its lowest notch), at most 10 s; without feedback, the longest delay.
- Parameters are read on control ticks (every 32 samples) and smoothed per sample, as in the other modules.
- **Analysis slots:**
  - 0/1 = input peak L/R; 2/3 = output peak L/R;
  - 4 = the LFO's position (cycles), for the Doubler's display;
  - 5/6 = each side's LFO value now (−1 to 1) and 7 = the envelope now (0 to 1, 0 while off): the response display works out where the sweep is with `sweep`.

## UI layout

On the module grid ([device-panels.md](../../docs/design/device-panels.md)), sections run left to right. Each is written `id` TITLE [columns], with controls at (column, row) and spans; knobs are 1 × 3 cells.

| Section | Controls |
|---|---|
| `effect` PHASER, FLANGER or DOUBLER (titled by the mode) [6] | Response display (0, 0, 4 × 3) · Mode, stacked [Phaser / Flanger / Doubler] (4, 0, 2 × 3) · Center or Time (0, 3) · Notches (1, 3) · Spread (2, 3) · Warmth (3, 3) · Feedback bar (4, 3, 2 wide) · Invert toggle (4, 4, 2 wide) · Through Zero toggle (4, 5, 2 wide) |
| `modulation` MODULATION [3] | Rate (0, 0) · Amount (1, 0) · Offset or Spin (2, 0) · Shape dropdown (0, 3, 2 wide) · Sync toggle (2, 3) · Stereo [Offset \| Spin] (0, 4, 2 wide) |
| `envelope` ENVELOPE, power = Envelope [3] | Amount (0, 0) · Attack (1, 0) · Release (2, 0) |
| `output` OUTPUT [2] | Dry/Wet (0, 0) · Output (1, 0) · Low Cut toggle (0, 3, 2 wide) · Low Cut Freq bar, named Freq (0, 4, 2 wide) |
| meter strip | In and out |

**The response display:**
- **Phaser and Flanger:** the response, 20 Hz to 20 kHz on the shared log-frequency axis: the notches (or the comb) where each side's sweep is now, the left side's over the right's, computed by `PhaserFlanger::response` from the same settings the audio path uses. A shaded band shows the range the sweep covers.
- **Doubler:** a doubler's comb is too fine to read, so it draws the second take's delay over two cycles, as the Chorus draws its voices.
- **Its line** reads where the sweep reaches, the envelope's reach included while it's on: "420 Hz–3.2 kHz" (Phaser), "0.4–2.0 ms" (Flanger, marked "through zero" when it passes the dry sound), "15–25 ms" (Doubler).
- **Dragging:** sideways moves Center or Time; up and down changes Amount, the band's width. Shift drags finely. (The Chorus's display also takes its movement's size vertically.)
- With nothing playing it (the snapshots, the gallery), it draws the sweep at rest.

**Controls:**
- **Center or Time follows the mode:** the knob turns `phaser.center`, `flanger.time` or `doubler.time` (`ParamKnob::follow`, device-panels.md); its name and value follow.
- **Offset or Spin follows Stereo,** the same way.
- **Rate follows Sync,** as in the Chorus.
- **Dimming:**
  - Notches and Spread outside the Phaser; Through Zero outside the Flanger.
  - The ENVELOPE section while Envelope is off (its power).
  - The Freq bar while Low Cut is off.
- Feedback is a value bar, as in the Chorus ("Feedback" is too long for a knob's name).
- About 850 px wide (14 columns and the meter strip).

## Factory presets

Built and named by Claude after approval ([presets.md](../../docs/design/presets.md) §13); signed off by the product owner by ear. Planned (14):

- **Classic phaser** (Guitar, Keys): Two notches on a slow triangle, the four-stage pedal sound.
- **Deep phaser** (Pad, Wide): Six notches with feedback, slow and wide, for pads.
- **Funky phaser** (Guitar): A quicker, resonant sweep for rhythm parts.
- **Bar phaser** (Drums, Bus): One sweep a bar, locked to the song.
- **Spinning phaser** (Wide, Keys): The sides sweeping at different speeds, drifting in and out of step.
- **Touch phaser** (Bass, Guitar): Notches that open with how hard you play.
- **Classic flanger** (Guitar): A slow, sweeping flanger with some feedback.
- **Jet flanger** (Drums, Heavy): The jet-plane whoosh: a slow, deep sweep with high feedback.
- **Tape flange** (FX, Heavy): Through Zero with the feedback inverted: the deep, cancelling sweep of two tapes.
- **Metallic flanger** (FX): Short, fast and resonant: a ringing, robotic edge.
- **Subtle flange** (Subtle, Vocals): A light, slow shimmer.
- **Touch flanger** (Drums): A flanger that sweeps with each hit.
- **Wide doubler** (Vocals, Wide): Two takes, one on each side, for a fuller voice.
- **Slap doubler** (Guitar): A looser, later second take.

## Automated test targets

| Metric | Target |
|---|---|
| Phaser response | With Amount 0 and Feedback 0, a log sweep's response matches `PhaserFlanger::response` within 0.2 dB (100 Hz–10 kHz) at 1, 2, 6 and 12 notches, Spread 0, 50 and 100 % |
| Notches | At Mix 50 % (Low Cut off), the response has Notches notches, each at least 40 dB deep |
| Flanger comb | With Amount 0, the comb's notches sit where Time puts them (odd multiples of 1 / (2 × Time)), within 1 % |
| Doubler | With Amount 0, an impulse comes back Time later, to the sample |
| Sweep | An impulse train through the Flanger at Amount 100 %: each echo arrives where `PhaserFlanger::sweep` says, within 1 sample; the Phaser's notch follows the sweep within 2 % |
| Through Zero | The latency reported is 1 ms and the dry is held back exactly that; at the crossing, with Feedback 0 and Mix 50 %, the output is the dry sound at +3 dB (equal power), within 0.1 dB |
| Envelope | A burst at 0 dBFS, Env Amount 100 %: the Phaser's notches rise by three octaves within Env Attack, and fall back within Env Release of the burst ending |
| Sync and Spin | Synced, the sweep is at the same place after a jump in the song as played through to it; with Spin, the right side's cycle is the left's divided by (1 + Spin) |
| Offset 0°, mono input | Left and right identical, in every mode |
| Low Cut | A 40 Hz sine at −12 dBFS, Low Cut at 200 Hz: within 0.5 dB at any Mix and in every mode, what isn't the tone at least 50 dB down |
| Mix 0 % | Null against the input (< −120 dBFS); with Through Zero, against the input 1 ms later |
| Level at Mix 50 % | Pink noise within ±1 dB of its input level in every mode, Feedback 0 |
| Feedback 95 % with Warmth 100 % | A 0 dBFS input: the output stays within +6 dBFS and doesn't grow, Invert on or off, in every mode |
| Mode, Notches, Shape, Sync and Stereo changes | No clicks (the conformance click score) |
| Display vs audio | The drawn response is `PhaserFlanger::response` at the sweep's position; the readout's range is where the sweep goes |
| Conformance suite | All pass (no allocation, no denormals, parameter jumps without clicks, the tail as reported, the latency as reported) |

## A/B cases (against the Live target)

Deferred until a Live machine is available.

## A/B log

–

## Questions for review

| # | Question | Recommendation |
|---|---|---|
| P1 | **One module, three modes** | Phaser, Flanger and Doubler, as Live. The first section takes the mode's name (PHASER, FLANGER, DOUBLER), and each mode keeps its own settings, so switching and back loses nothing |
| P2 | **The phaser** | 1–12 notches (second-order all-passes), with Center and Spread, as Live; 2 by default, the classic four-stage sound. Leave out Live's Blend, which sweeps Spread as well as Center: rarely used, and the LFO and envelope both sweep Center |
| P3 | **The flanger's Time** | The longest delay, with the sweep shortening it towards zero, as Live. The Doubler has a Time of its own (5–60 ms, 20 by default), moving either way around it |
| P4 | **Through Zero** | Yes (beyond Live), Flanger only: the dry sound held back 1 ms so the sweep can pass it. Reported as latency so the track stays in time, and so not automatable, as the Compressor's Lookahead |
| P5 | **The LFO** | Seven shapes: Sine, Triangle (the default, as Live), Saw Up, Saw Down, Square, Random (stepped) and Drift (smooth, the Delay's). Smoothed, so the jumping ones don't click. Leave out Live's analog and stepped triangles and Duty Cycle for now |
| P6 | **Stereo** | A switch between Offset (the Chorus's: 0–180°, 90° by default) and Spin (the right side up to twice as fast), with one knob that follows it, as Live |
| P7 | **Live's second LFO** | Leave it out for now (Later): the envelope gives a second source of movement, and one LFO keeps the panel clear |
| P8 | **The envelope follower** | Its own section with a power switch, as Live's Env Fol button: Amount (either way), Attack and Release; the display shows where it moves the sweep. At 100 % the input's level moves the sweep across its whole range |
| P9 | **Feedback, Invert and Warmth** | As in the Chorus: Feedback 0–95 % (30 % by default) with an Invert switch, and Warmth's tone stage in the loop |
| P10 | **Low Cut** | As in the Chorus: the lows bypass the effect and keep their level, on by default at 120 Hz (Live's Safe Bass is a high-pass on the effect) |
| P11 | **The display** | The response (Phaser and Flanger) or the delay over time (Doubler), with the sweep's range as a band; drag sideways for Center or Time, up and down for Amount |
| P12 | **Name and IDs** | Named **Phaser & Flanger**: unlike the Chorus's modes, a flanger isn't a kind of phaser, and both are what people look for. Device `phaser-flanger`, plugin code `Fphf`, CLAP `org.segnoaudio.phaser-flanger`; section IDs as above (`effect`, `modulation`, `envelope`, `output`) |

## Settled while building (B3, 2026-10-12)

- **Shared with the Chorus, in the cores:** the Low Cut's crossover (`dsp::Crossover`), the glide (`dsp::Glide`), the LFO that follows the song (`dsp::SongLfo`) and the note values (`dsp::lfoNoteValues`) moved out of the Chorus into `libs/dsp`, with tests of their own. The Chorus runs on them unchanged (its tests and golden metrics pass as before), and Auto Pan & Tremolo (B4) will too.
- **Both effects always run:** the Phaser's chain and the line run whatever the mode, so a change of mode crossfades from an effect already going. Each loop's feedback follows how much its mode is heard.
- **What goes round a loop is bounded softly:** exactly itself up to full scale, then rounding off towards +12 dBFS. A hard clamp at +12 dBFS clipped a ringing phaser harshly; the conformance suite heard it as a click when Feedback fell from 95 %.
- **Through Zero holds the whole module back 1 ms, in every mode** (the dry sound, the lows and the Phaser's input; the Doubler reads 1 ms later to match), so the latency doesn't change with Mode, which is automatable. In the Flanger, the line isn't held back, so its sweep can pass the dry sound.
- **The envelope moves its whole range (48 dB) in Env Attack or Env Release,** in dB, so "within Attack" and "within Release" hold exactly.
- **The readout and the band cover where the sweep reaches,** the envelope included while it's on, and write a unit once where both ends share it ("244–369 Hz").
- **Dragging the display right is always higher:** the Phaser's notches up, the Flanger's comb up (a shorter Time).
- **The plugins' files are named "Segno Phaser and Flanger".** An "&" in a bundle's file name broke the build's own commands; hosts still show "Segno Phaser & Flanger" (JUCE's plugin name, apart from its product name).
- **Metallic flanger's feedback is 85 %:** at 90 % it peaked at +0.14 dBFS.

## Sign-off

- [x] Automated: unit, conformance and golden tests; validators (pluginval at strictness 10 and clap-validator pass; 2026-10-12)
- [x] Layout lint passes (`fd_panel_tests`)
- [x] Snapshots reviewed (100% / 200%; expanded, collapsed, folded; every factory preset at 200 %, with `fd-snapshot --presets phaser-flanger`)
- [x] Sound signed off by the product owner (2026-10-12), with the 14 factory presets
- [x] Feel signed off by the product owner (2026-10-12)
