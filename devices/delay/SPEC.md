# Delay: device spec

- **Status:** Approved by the product owner (2026-10-12, with every recommendation of D1–D10); IDs are permanent. Built (B1, 2026-10-12): what building settled is under "Settled while building". Done: signed off by the product owner (2026-10-12)
- **Type:** Audio effect
- **Tier / milestone:** Tier 1, M3 (B1)
- **Live target:** Delay and Echo, in one module. References only; nothing is copied ([ADR 0011](../../docs/decisions/0011-calibrated-ambition-and-consolidation.md)).

## Purpose

Echoes, from a clean slapback to a dark, wobbling tape echo: times in milliseconds or in time with the song, stereo, ping-pong or mid-side, a filter and saturation in the feedback so each repeat changes, modulation, ducking so the echoes stay out of the way while you play, and freeze. One module where Live has two (Delay for clean, Echo for character), on the modulation core built for it (M3 A2).

## Beyond Live

None in sound or features: the aim is Delay and Echo together. Two things in how it's shown:
- **The repeats drawn:** a display of each repeat as a bar on a time line, left and right, at its level, computed by the same functions the audio path uses (times, ping-pong, feedback, filter). You see where the echoes fall against the beat before you hear them.
- **The filter's curve,** drawn by the feedback path's own `magnitude()`.

## Stages

| Stage | Scope | Milestone |
|---|---|---|
| Core | Everything below | M3 |
| Later | A reverb in the feedback (Echo's), a gate, an envelope follower on the modulation; tap tempo | With demand |

## Parameters

| ID (permanent) | Name | Description | Range | Default | Unit | Curve / skew | Smoothing | Automatable |
|---|---|---|---|---|---|---|---|---|
| `mode` | Mode | The echoes' character: Clean is exact, Tape darkens and saturates like tape, Analog is darker and softer still. | Clean, Tape, Analog | Clean | choice | – | 30 ms crossfade of the feedback path's tone | Yes |
| `sync` | Sync | Times in note values, following the song's tempo, instead of milliseconds. | Off / On | On | toggle | – | as a time change (`transition`) | Yes |
| `time.left` | Left | How long before the left side echoes, in milliseconds. | 1 … 5 000 | 375 | ms | centre 250 ms | as `transition` says | Yes |
| `time.right` | Right | How long before the right side echoes, in milliseconds. | 1 … 5 000 | 375 | ms | centre 250 ms | as `transition` says | Yes |
| `div.left` | Left Sync | The left side's time as a note value, while Sync is on. | 1/64, 1/32T, 1/32, 1/16T, 1/16, 1/8T, 1/16D, 1/8, 1/4T, 1/8D, 1/4, 1/2T, 1/4D, 1/2, 1/2D, 1 bar, 2 bars | 1/8D | choice | – | as `transition` says | Yes |
| `div.right` | Right Sync | The right side's time as a note value, while Sync is on. | as Left Sync | 1/8D | choice | – | as `transition` says | Yes |
| `link` | Link | The right side takes the left side's time. | Off / On | On | toggle | – | – | Yes |
| `swing` | Swing | Moves the right side's synced time off the grid by a share of itself, for a swung, looser feel. | −33 … +33 | 0 | % | linear | as `transition` says | Yes |
| `stereo` | Stereo | How the sides echo: each on its own side, bouncing from side to side, or as mid and side. | Stereo, Ping-pong, Mid-side | Stereo | choice | – | 30 ms crossfade | Yes |
| `feedback` | Feedback | How much of each echo comes back as the next: more feedback, more repeats. | 0 … 100 | 35 | % | linear | 20 ms | Yes |
| `freeze` | Freeze | Holds what's in the echoes, repeating it forever, and lets nothing new in. | Off / On | Off | toggle | – | 20 ms | Yes |
| `filter` | Filter | Turns the feedback filter on: each repeat loses a little more of the lows and highs. | Off / On | On | toggle | – | 20 ms crossfade | Yes |
| `filter.low` | Low Cut | Below this, each repeat thins out. | 20 … 2 000 | 120 | Hz | centre 200 Hz | 20 ms multiplicative | Yes |
| `filter.high` | High Cut | Above this, each repeat darkens. | 500 … 20 000 | 6 000 | Hz | centre 4 kHz | 20 ms multiplicative | Yes |
| `drive` | Drive | Saturates what feeds back, as the mode does: repeats thicken and stay bounded at any feedback. | 0 … 100 | 0 | % | linear | 20 ms | Yes |
| `wobble` | Wobble | Slow and quick wavers in the echoes' time, like tape or a worn machine. | 0 … 100 | 0 | % | linear | 20 ms | Yes |
| `mod.shape` | Mod Shape | The modulation's wave. | Sine, Triangle, Drift | Sine | choice | – | – | Yes |
| `mod.rate` | Mod Rate | How fast the modulation moves. | 0.01 … 10 | 0.5 | Hz | centre 0.5 Hz | 20 ms multiplicative | Yes |
| `mod.time` | Mod Time | How far the modulation moves the echoes' time: a chorus-like shimmer. | 0 … 100 | 0 | % | linear | 20 ms | Yes |
| `mod.filter` | Mod Filter | How far the modulation sweeps the feedback filter. | 0 … 100 | 0 | % | linear | 20 ms | Yes |
| `duck` | Ducking | How much the echoes dip while you play; they come back up in the gaps. | 0 … 100 | 0 | % | linear | 20 ms | Yes |
| `duck.release` | Duck Release | How quickly the echoes come back up once you stop. | 50 … 2 000 | 300 | ms | centre 300 ms | – | Yes |
| `transition` | Time Change | What a time change sounds like: Repitch glides, like tape speeding up or slowing down; Fade crossfades. | Repitch, Fade | Repitch | choice | – | – | No |
| `mix` | Dry/Wet | Blends the original signal with the echoes. | 0 … 100 | 35 | % | linear | 20 ms | Yes |
| `output` | Output | The level after the delay. | −24 … +12 | 0 | dB | linear | 20 ms, linear in amplitude | Yes |

No non-parameter state. State version 1.

**Ranges and limits:**
- The longest delay is 5 s in milliseconds; a synced value longer than 10 s at a slow tempo is held at 10 s.
- Swing moves only the right side, so ping-pong repeats swing against the left side's straight ones.
- Link makes the right side take the left side's time, synced or not; Swing still applies to the right.

## DSP design

```
in ─┬─> [mid-side: encode] ─> + ─────────────> delay L/R (ModulatedDelay, time ± mod ± wobble) ─┬─> wet ─> [mid-side: decode] ─> × duck gain ─┐
    │                         ^                                                                │                                             │
    │                         └── × feedback ── FeedbackPath (low/high cut, mode, drive) <─────┘ (ping-pong: L↔R)                            │
    ├─> duck detector (input level, release) ──────────────────────────────────────────────────────────────────────────────────────────────┤
    └─> dry ──────────────────────────────────────────────────────────────────────────────────── mix (equal gain) ─> output ──────────────────┘
```

- **The delay lines** are the modulation core's `ModulatedDelay` (M3 A2): read anywhere between samples, while moving, with no zipper or dulling. Up to 10 s each side.
- **Times:** milliseconds, or note values at the song's tempo (`TransportInfo`), Swing on the right. Computed by a pure function, `Delay::times`, that the repeats display calls too.
- **Time changes (`transition`):**
  - **Repitch:** the read position glides to the new time (a one-pole glide of about 100 ms), so pitch bends like tape;
  - **Fade:** a second read position starts at the new time and the two crossfade over 50 ms.
- **Stereo:**
  - **Stereo:** each side its own line and feedback;
  - **Ping-pong:** the input's mono sum enters the left line; the left feeds back into the right and the right into the left;
  - **Mid-side:** the lines run on mid and side; Left is the mid's time, Right the side's.
- **The feedback path** is the core's `FeedbackPath`: Low Cut and High Cut (12 dB/oct each), then saturation as the mode says, bounded at any feedback:
  - **Clean:** the filter as set; Drive a soft clip;
  - **Tape:** the filter, then a gentle high-frequency loss and head bump on each pass (a low shelf +1 dB at 100 Hz, a high cut at 9 kHz), Drive the tape curve;
  - **Analog:** darker (a fixed high cut at 4.5 kHz before the filter), Drive a softer, earlier clip.
- **Wobble:** the core's LFOs on the read position: wow (wander at about 0.6 Hz) and flutter (sine at about 6 Hz, one tenth as deep), up to ±0.6 % of the time at 100 %.
- **Modulation:** a `ModulationLfo` with a quarter cycle between the sides; Mod Time moves the time up to ±2 % (at most ±5 ms), Mod Filter moves both cutoffs up to ±1.5 octaves.
- **Freeze:** the input stops entering the loop, feedback becomes 100 %, and the loop's filter and saturation are bypassed, so what's held repeats unchanged; turning it off lets the echoes decay as before.
- **Ducking:** the dynamics core's peak detector on the input, attack 5 ms and Duck Release; the wet signal dips by up to 30 dB (Ducking at 100 %) while the input is above −40 dBFS, scaled by how far above. The detector falls linearly in dB, so from full ducking the echoes are back after Duck Release.
- **Mix:** equal gain (dry and wet each at their share), so 50 % is half of each.
- **Latency:** none. **Tail:** reported as the time the echoes take to fall 60 dB at the current feedback, at most 60 s (Freeze: 60 s).
- Parameters are read on control ticks (every 32 samples) and smoothed per sample, as in the other modules.
- **Analysis slots:** 0/1 = input peak L/R; 2/3 = output peak L/R; 4 = the duck gain (dB, for the panel).

## UI layout

On the module grid ([device-panels.md](../../docs/design/device-panels.md)): sections left to right, each `id` TITLE [columns], with controls at (column, row) and spans; knobs are 1 × 3 cells.

| Section | Controls |
|---|---|
| `time` TIME [5] | Repeats display (0, 0, 5 × 3) · Left (0, 3) · Right (1, 3) · Swing (2, 3) · Sync toggle (3, 3, 2 wide) · Link toggle (3, 4, 2 wide) · Stereo [Stereo \| Ping-pong \| Mid-side] (3, 5, 2 wide) |
| `feedback` FEEDBACK [3] | Feedback, named Amount (0, 0) · Drive (1, 0) · Wobble (2, 0) · Mode [Clean \| Tape \| Analog] (0, 3, 3 wide) · Freeze toggle (0, 4, 3 wide) |
| `filter` FILTER [2], power = Filter | Curve display (0, 0, 2 × 3) · Low Cut (0, 3) · High Cut (1, 3) |
| `modulation` MODULATION [3] | Rate (0, 0) · Time (1, 0) · Filter (2, 0) · Shape [Sine \| Triangle \| Drift] (0, 3, 3 wide) |
| `ducking` DUCKING [1] | Ducking (0, 0) · Release (0, 3) |
| `output` OUTPUT [1] | Dry/Wet (0, 0) · Output (0, 3) |
| meter strip | In and out |
| header | Time Change [Repitch \| Fade] |

- **Repeats display:** the next four seconds (or eight repeats, whichever is longer), the beat grid when synced; a bar for each repeat, left above the line and right below, its height its level (feedback, ping-pong and the filter's loss at 1 kHz). Drag a side's first bar left or right to change its time; with Sync on, it snaps to the note values. Shift drags finely.
- **Filter curve:** 20 Hz–20 kHz, the response one pass through the filter makes, from `FeedbackPath::magnitude`; drag the low or high edge to move that cutoff.
- **Synced times:** while Sync is on, the Left and Right knobs step through the note values (Left Sync, Right Sync) and read them ("1/8D"); off, they turn in milliseconds.
- **Dimming:** Right dims while Link is on (Swing stays live: it still moves the right side); Swing dims while Sync is off (it moves only synced times); the FILTER section dims while Filter is off; Drive and Wobble work in every mode.
- About 940 px wide (15 columns and the meter strip).

## Factory presets

Built and named by Claude after approval ([presets.md](../../docs/design/presets.md) §13); signed off by the product owner by ear. Planned (15):

- **Slapback** (Vocals, Guitar): One short tape echo, about 90 ms, for a vintage voice or guitar.
- **Quarter echo** (Clean): A plain echo on each beat, fading over a few repeats.
- **Dotted eighth** (Guitar, Lead): The dotted-eighth echo that fills the gaps between notes.
- **Ping-pong eighths** (Wide): Eighth notes bouncing between the sides.
- **Swung ping-pong** (Wide, Drums): Ping-pong with the right side swung.
- **Tape echo** (Warm, Vocals): Three-sixteenth repeats that darken and waver like tape.
- **Analog bucket** (Warm, Dirty): Dark, soft repeats from a small analog delay.
- **Dub space** (FX, Heavy): Long feedback, thinning and darkening, with wobble: the dub throw.
- **Vocal throw** (Vocals, Ducking): Echoes that wait for the singer to stop.
- **Wide double** (Wide, Subtle): Two short delays, a different one on each side, without repeats: a doubled, wider sound.
- **Mid-side space** (Wide): On stereo sources, the centre echoes on the beat and the sides on the dotted beat: a wide, moving space.
- **Shimmer drift** (Pad, FX): Slow modulation on long repeats for pads.
- **Telephone repeats** (FX): Narrow, filtered echoes, like through a phone.
- **Build to infinity** (FX): Feedback near the top; freeze it to hold the last bar.
- **Short room** (Subtle): A few quick, soft repeats that read as a small room.

## Automated test targets

| Metric | Target |
|---|---|
| Delay times, ms and synced at 60, 120 and 174 BPM, Swing ±33 % | Impulse arrives at the exact sample (Clean, Filter off) |
| Repeat levels, Clean, Filter off, Drive 0 | Each repeat the feedback times the last, within 0.05 dB |
| Filter in the loop | Each repeat's spectrum the last times `FeedbackPath::magnitude`, within 0.2 dB (100 Hz–10 kHz) |
| Ping-pong | An impulse in the left only: repeats alternate left, right, left… (the input enters the left line) |
| Mid-side | A mono input: echoes in the mid only, with Right's time not heard |
| Freeze | A held loop keeps its level within 0.5 dB over 30 s, and takes no new input |
| Feedback 100 % with Drive 100 % | A 0 dBFS input: the echoes stay within +6 dBFS and don't grow, in every mode |
| Repitch and Fade time changes | No clicks (the conformance click score); Repitch glides, Fade crossfades |
| Ducking 100 % | Wet at least 25 dB down while the input plays at −10 dBFS; back within Duck Release of the input stopping |
| Mix 0 % | Null against the input (< −120 dBFS) |
| Repeats display vs audio | The bars' times and levels within 1 sample and 0.1 dB of the rendered repeats |
| Conformance suite | All pass (no allocation, no denormals, parameter jumps without clicks, the tail as reported) |

## A/B cases (against the Live target)

Deferred until a Live machine is available.

## A/B log

–

## Questions for review

| # | Question | Recommendation |
|---|---|---|
| D1 | **One module or two** (Live has Delay and Echo) | One, with a Mode: Clean is Delay, Tape and Analog are Echo's character; fewer modules to choose between, one set of presets |
| D2 | **Times per side, Link, synced by default** | Left and Right, Link on and Sync on by default (1/8D), as Live's Delay |
| D3 | **Swing on the right side only** | Yes: ping-pong repeats swing against the straight left side; ±33 % (Live's offset range) |
| D4 | **Stereo modes** | Stereo, Ping-pong, Mid-side (Echo's three) |
| D5 | **The filter in the feedback** | Low and high cut, 12 dB/oct, on by default (120 Hz – 6 kHz), so repeats sit back in a mix |
| D6 | **Character: Drive and Wobble in every mode** | Yes: the mode sets the tone and saturation curve, Drive and Wobble how much |
| D7 | **Time changes** | Repitch (default) and Fade; no Jump, which clicks |
| D8 | **Ducking** | One amount and a release, no threshold: it dips the echoes by how loud the input is (up to 30 dB) |
| D9 | **Left out for now** | Echo's reverb (the Reverb module follows), gate, envelope follower and noise control; tap tempo |
| D10 | **IDs** | Device `delay`, plugin code `Fdly`, CLAP `org.segnoaudio.delay`; section IDs as above |

## Settled while building (B1, 2026-10-12)

- **The note values run from the shortest to the longest** (1/8T before 1/16D, 1/4T before 1/8D, 1/2T before 1/4D), so turning the knob always lengthens the time. Choices are stored by index, and nothing had been saved yet.
- **The Feedback knob reads "Amount"** under its section's title (the knob's name has 46 px), and the smooth random shape is named **Drift**, for what it does ("Random" didn't fit its segment). Display names only; the IDs stand.
- **Stereo is a dropdown,** not a segmented control: three names don't fit two columns.
- **Analog has no noise floor:** noise in the loop would break "silence in, silence out" (the conformance suite) and play forever at high feedback. Analog's character is its darker tone and softer clip.
- **Bounded:** with full feedback and full drive, the loop's saturation holds each pass under the input's level. A 0 dBFS input stays within +6 dBFS and doesn't grow. "Below 0 dBFS" was too strict: a 0 dBFS input plus its echoes passes 0 dBFS at any feedback.
- **Duck Release is a recovery time:** the detector falls linearly in dB, so from full ducking (the input at −10 dBFS or above) the echoes are fully back after Duck Release, as the tests check. A plain time constant took about 3.5 times as long.
- **The first block after a reset starts at the song's tempo** instead of gliding there from 120 BPM, so the first echo of a song at another tempo isn't bent.
- **Ping-pong's first echo is on the left:** the input's mono sum enters the left line, as the DSP design says; the test target now says the same.
- **Two presets changed with the design:**
  - **Wide double** has a different short delay on each side: an echo on one side only, mixed with the dry signal, combs that side.
  - **Mid-side space** echoes the centre and the sides at different times: the mid always echoes, so the centre can't be left dry.
- **The time knobs change parameters with Sync:** Left and Right turn `time.*` in milliseconds or `div.*` in note values, through `ParamKnob::follow` (device-panels.md). Automation and the host see the parameter that plays.
- **The repeats display** labels its lanes (L and R, or M and S) in a gutter at the left, and its horizon is fixed while you drag, so the bar stays under the pointer. Without a song (the snapshots, the gallery) it draws at 120 BPM in 4/4. The device publishes the tempo it follows on the analysis bus for the display.

## Sign-off

- [x] Automated: unit, conformance and golden tests; validators (pluginval at strictness 10 and clap-validator pass; 2026-10-12)
- [x] Layout lint passes (`fd_panel_tests`)
- [x] Snapshots reviewed (100% / 200%; expanded, collapsed, folded)
- [x] Sound signed off by the product owner (2026-10-12), with the 15 factory presets
- [x] Feel signed off by the product owner (2026-10-12)
