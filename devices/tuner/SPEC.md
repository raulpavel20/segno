# Tuner: device spec

- **Status:** Approved by the product owner (2026-10-15, with every recommendation of TU1–TU12); IDs are permanent. Built (B10, 2026-10-15): what building settled is under "Settled while building". Signed off by the product owner (2026-10-16).
- **Type:** Audio effect (analysis; audio passes unchanged unless Mute is on)
- **Tier / milestone:** Tier 1, M3 (B10)
- **Live target:** Live 12's Tuner (Classic and Strobe views, the Histogram of pitch over time, a reference of 410–480 Hz, sharps or flats). Checked against the tuners in Logic, Bitwig, Reaper, FL Studio, Cubase and Studio One, and against GTune, Peterson's StroboSoft, TC Electronic's PolyTune plug-in and MeldaProduction's MTuner (research, 2026-10-15). References only; nothing is copied ([ADR 0011](../../docs/decisions/0011-calibrated-ambition-and-consolidation.md)).

## Purpose

Tunes what's on the track: a guitar or a bass before recording, a voice, a sample whose root you don't know, a synth patch drifting out of tune. It names the note it hears and how far off it is in cents and hertz, steadily enough to tune by. A strobe view shows small offsets, and a history of the pitch over time shows vibrato, slides and intonation. The audio passes untouched.

What makes it worth using over the tuner every DAW has: it knows the song (TU5). Names are spelled in the song's key, and **In key** measures against the key's notes, so a singer aiming for the key's G reads "G +60" rather than "G♯ −40".

## Beyond Live

| Addition | Reference | Why it's worth it | Cost |
|---|---|---|---|
| **In key** (TU5): measures against the nearest note of the song's key | Reaper's ReaTune corrects to a key and mode; no tuner we found measures against one | Practising or checking a part in the song's key reads against the notes the song uses, not the nearest chromatic one | Small: the key is in `TransportInfo` (ADR 0057), and `libs/music` knows its notes |
| **Spelled by the key** | Logic spells by key signature elsewhere; Live's Tuner has a right-click choice | In F major the tuner says B♭, not A♯, as the piano roll does | None: `libs/music`'s spelling |
| **Mute** (TU6) | Logic, Cubase, Studio One, PolyTune | Tune without being heard. Live's Tuner has no mute | Small |
| **Hz and cents together** | Bitwig, Cubase, FL Studio | Live switches between them; there's room for both | None |

**Not here** (TU9):
- tuning systems and temperaments: they need tuning systems in the DAW first (Live 12.1 has them, Segno doesn't);
- names for transposing instruments (B♭ clarinet, E♭ sax), capo and drop tunings;
- polyphonic tuning (all strings strummed at once);
- a tuner without a module, in the top bar, as Logic has;
- pitch correction: a module of its own later (device-suite.md, Pitch Correction).

## Stages

| Stage | Scope | Milestone |
|---|---|---|
| Core | Everything below | M3 |
| Later | Tuning systems with the DAW's; transposing instruments; polyphonic tuning; a tuner in the top bar | With demand |

## Parameters

| ID (permanent) | Name | Description | Range | Default | Unit | Curve / skew | Smoothing | Automatable |
|---|---|---|---|---|---|---|---|---|
| `reference` | Reference | The pitch of the A above middle C, which every note is measured against. | 410 … 480 | 440 | Hz | linear, shown to 0.1 Hz | – | No |
| `view` | View | Needle shows how far off the note is on a scale; Strobe turns faster the further off it is. | Needle, Strobe | Needle | choice | – | – | No |
| `notes` | Notes | Which notes it measures against: every note, or only the song's key's. | All, In key | All | choice | – | – | No |
| `mute` | Mute | Silences what comes out, so you can tune without being heard. | Off, On | Off | toggle | – | 10 ms fade | Yes |

- **Reference** is the A above middle C: A3 in Segno's names (C3 = 60, daw-shell.md), A4 in scientific pitch. Its bar reads "440.0 Hz", and its tooltip names the note, so neither naming confuses.
- **Mute is automatable** so that a controller's button can mute while tuning on stage, once MIDI learn (C8) maps it.

**Non-parameter state:** none. The history is what it heard lately; it isn't saved.

## DSP design

```
in L/R ───────────────────────────────────────────────> out L/R (unchanged; silent while Mute is on)
   │
   └─> mono sum ─> low-pass and decimate to ~12 kHz ─> window (adaptive) ─> NSDF (McLeod) ─> the period
                                                                    │                         │
                                                                    │     phase across hops <─┘ (refines it)
                                                                    v
                                     level gate, clarity gate, note hysteresis ─> analysis bus, history ring
```

Detection runs on the audio thread in preallocated buffers, as the Spectrum's analysis does. The readings are published on the analysis bus, so they keep updating while the panel is closed and assistants can read them.

- **Audio:** passes unchanged (a null test), zero latency, no tail. Mute fades out over 10 ms and back in over 10 ms.
- **Decimation:** the mono sum, low-passed and decimated by a whole factor to between 16 and 25.6 kHz (×2 at 44.1 and 48 kHz, ×4 at 88.2 and 96 kHz; settled while building).
- **The period: the McLeod Pitch Method** (normalised square difference function, NSDF). It's robust on plucked strings, voices and synths, and it rarely jumps an octave. The steps:
  - the NSDF through an FFT, in place;
  - its first "key maximum" above 0.9 of the highest, refined by parabolic interpolation;
  - the peak's height is the **clarity** (0 … 1), how periodic the sound is.
- **Adaptive window:** four periods of the note, between 512 and 2048 decimated samples (about 21 to 85 ms at 24 kHz). Low notes get the length they need and high notes answer fast. Readings come every 256 decimated samples, about 90 a second.
- **Range:** 25 Hz to 4.2 kHz. That covers a 5-string bass's low B (31 Hz) tuned down a tone, and the piano's top C.
- **Cents to a tenth:** the NSDF's lag is coarse at high notes (at 1 kHz, a hundredth of a sample is 1.4 cents). Once the note is steady, the phase of the fundamental across two hops refines the frequency, as a phase vocoder does, to better than 0.1 cent on a steady tone. While the pitch moves, the NSDF's reading stands.
- **Gates:** a reading needs the level above −60 dBFS and clarity above 0.8. Otherwise the tuner shows no note: noise, silence and chords read as nothing rather than as a wrong note.
- **Steadiness:**
  - the note changes only after two readings agree, or past ±55 cents, so a note held on the boundary between two doesn't flicker;
  - cents for the needle are the median of the last three readings;
  - the strobe uses the raw reading.
- **In key** (TU5): the nearest note of the song's key (`TransportInfo::key`, `libs/music`'s scales). Its cents can go past ±50. Without a key, Notes: In key reads as All, and the panel says why ("The song has no key", or in another host, "No key here").
- **Published** on the analysis bus (`MeterSpec`s, so `measure_module` reads them): frequency (Hz), note (MIDI number), cents, clarity, level, and the key's root and scale (for spelling the name). It reports "no reading" while the gates are shut.
- **The history:** a ring of the last 12 seconds of readings (pitch as a fractional MIDI number, and clarity), written as atomics by the audio thread and read by the panel through `DeviceUiContext::device` (read-only, ADR 0017), as the Loudness Meter's history is.

## UI layout

On the module grid ([device-panels.md](../../docs/design/device-panels.md)): sections left to right, each `id` TITLE [columns], with controls at (column, row) and spans.

| Section | Controls |
|---|---|
| `tuner` TUNER [7] | Display (0, 0, 7 × 6): the note, large, with its octave; under it the needle's scale or the strobe; the cents and the frequency as readouts |
| `history` HISTORY [8] | Display (0, 0, 8 × 6): the pitch over the last 12 seconds on a grid of notes |
| `settings` SETTINGS [3] | Reference (0, 0, 3 wide) as a value bar · View: Needle \| Strobe (0, 1, 3 wide) · Notes: All \| In key (0, 2, 3 wide) · Mute (0, 3, 3 wide) |

18 columns, narrower than the Spectrum. HISTORY and SETTINGS fold to leave TUNER.

- **The note:** its name spelled by the song's key, with flats in a flat key and sharps otherwise (key-and-scale.md §4); its octave smaller beside it. It shows "–" dimmed while there's no reading, and the last note stays dimmed for a second after the sound stops, so a decaying string doesn't blank at once.
- **Needle:** a scale from −50 to +50 cents (wider with In key, as far as the next note of the key), ticks every 10 cents and the middle marked. The needle is a bar from the middle to the reading, and the band within ±3 cents is the in-tune zone (TU4).
  - In tune, the needle and the note take the on-target colour (the Loudness Meter's role).
  - Off, they're in the value colour. An arrow on the side to tune towards says which way.
  - Colour alone never says it: the arrow goes away when it's in tune.
- **Strobe:** a band of stripes that drift right when sharp and left when flat, faster the further off, standing still in tune. It's drawn from the raw reading at the panel's frame rate. Its movement is the reading's, never the UI's own calculation.
- **Readouts:** "+4 ¢" and "110.3 Hz", in the mono value font, under the scale.
- **History:**
  - rows of notes, as the piano roll's (the key's notes lighter), around the last note, ± 7 semitones;
  - the pitch drawn as a line, each note's centre its in-tune line;
  - stretches below the clarity gate left empty;
  - it follows the pitch, scrolling up and down as the pitch moves away;
  - hover shows the moment's note and cents.
- **Mute:** while on, the header shows "Muted" at its right end in the warning colour (as the EQ's latency badge sits), so it's seen with the panel folded too.
- **Reaching it by mouse** (ADR 0040): it's a module, added from the browser or Add module like any other. No menu items of its own.

## Factory presets

Built and named by Claude ([presets.md](../../docs/design/presets.md) §13; `devices/tuner/presets/`, made with `fd-presets` from recipes). Four, named by use, with tags from the shared list. The defaults (440 Hz, Needle, All) aren't a preset (a preset must differ from them).

- **Baroque, A 415** (Check): Reference 415 Hz, for period instruments.
- **A 432** (Check): Reference 432 Hz.
- **Orchestra, A 442** (Check): Reference 442 Hz, as many orchestras tune.
- **Silent tuning** (Check): Mute on, Strobe.

## Automated test targets

| Metric | Target |
|---|---|
| Audio through the device, Mute off | Null against the input (< −120 dBFS), zero latency |
| Mute | Silence after the 10 ms fade; no click (no step over −60 dBFS) |
| Sines from 27.5 Hz (A−1 in Segno's names) to 4186 Hz, every note, at 44.1, 48 and 96 kHz | The right note; cents within ±0.1 once steady |
| A sine 7 cents sharp; Reference 415 Hz on a 415 Hz sine | +7.0 ± 0.1 cents; A, 0.0 ± 0.1 |
| Saw, square, pulse and a tone without its fundamental (harmonics 2–8), every octave | The right note, no octave errors; within ±0.5 cents |
| The Library's multisamples (strings, bass, a voice) at their roots | Their roots, within each sample's own tuning |
| Time to a reading | A guitar's low E (82 Hz): within 70 ms; a bass's E (41 Hz): 90 ms; the low B (31 Hz): 120 ms (settled while building) |
| White noise; silence; a cluster of unrelated notes | No reading (settled while building: a major triad reads its root) |
| A sine gliding across the A–A♯ boundary with ±2 cents of vibrato | The note changes once |
| In key, A minor, a G♯ | G at a tie (the nearest note of the key below); 20 cents flat, G +80 |
| `measure_module` on the Tuner | Frequency, note, cents, clarity, level, by name and unit |
| No allocation, lock or file access in `process()` | Conformance |
| Conformance suite | All pass |

## A/B cases

| Case | Stimulus | Our settings | Reference | What to check |
|---|---|---|---|---|
| A guitar tuned by ear to a reference tuner | Each open string, plucked | Needle | A hardware strobe tuner, or Live's Tuner | The same note and cents (±1) as it settles |
| A 5-string bass's low B | Plucked, then let ring | Needle | The same | A steady reading within 150 ms; no octave jump as it decays |
| A sung phrase | A voice in a song with a key | In key, History | – | The history shows the slides and vibrato; In key reads against the key's notes |

## A/B log

–

## Questions for review

| # | Question | Recommendation |
|---|---|---|
| TU1 | **What it shows** | The note spelled by the song's key, with its octave; cents and hertz together, no switch between them (Live switches; there's room for both) |
| TU2 | **Views** | Needle by default, and Strobe as Live, Cubase and Studio One have, for the last tenth of a cent. History beside them, always there unless folded, as Live's Histogram and Bitwig's History: vibrato, slides and intonation over 12 seconds |
| TU3 | **Reference** | 410 … 480 Hz, default 440, per module, not automatable. Not project-wide: Segno has no concert pitch of its own yet. One comes with tuning systems |
| TU4 | **In tune** | Within ±3 cents, fixed, not a setting. The on-target colour, and the arrow gone. The strobe is there for finer tuning |
| TU5 | **The song's key** | Names spelled by the key. Notes: All \| In key, where In key measures against the nearest note of the key (dimmed, and saying why, where there's no key). No other tuner we found does this, and it's cheap here |
| TU6 | **Mute** | A switch, automatable (a foot controller through MIDI learn), off by default. "Muted" in the header's warning colour while it's on, so it isn't forgotten. Live has none; Logic, Cubase and Studio One do |
| TU7 | **Range and speed** | 25 Hz to 4.2 kHz, from a 5-string bass tuned down a tone to the piano's top C. Automatic, with no Reactivity or Bass switch: the window adapts to the note. About 47 readings a second |
| TU8 | **Detection** | Monophonic: the McLeod method, refined by phase to a tenth of a cent on a steady note. Polyphonic strum tuning later, if asked |
| TU9 | **Not now** | Tuning systems and temperaments (with the DAW's own, later), transposing instruments and capo names, polyphonic tuning, a tuner in the top bar, pitch correction (a module of its own) |
| TU10 | **Assistants** | `measure_module` reads frequency, note, cents, clarity and level. No tool of its own: an assistant can tell what a track plays and how in tune it is |
| TU11 | **Presets** | Four, named by use: Baroque A 415, A 432, Orchestra A 442, Silent tuning |
| TU12 | **Name and IDs** | Named **Tuner**. Device `tuner`, plugin code `Ftun`, CLAP `org.segnoaudio.tuner`; section IDs `tuner`, `history`, `settings` |

## Settled while building (B10, 2026-10-15)

- **Twice the analysis rate:** 16–25.6 kHz (×2 at 44.1 and 48 kHz, ×4 at 88.2 and 96), not 11–12.5. At 12 kHz the piano's top octave had under three samples a period and read an octave low. 25.6 kHz is the fastest rate at which the 2048-sample window still holds two periods of the lowest note (25 Hz). About 90 readings a second.
- **The period from the longest window, the pitch from four periods:** every reading takes its period from the longest window the sound allows, so a low note is never read an octave high, then its pitch from about four periods around that period, so vibrato and slides show.
- **Low notes, by the NSDF's own peak:** below about 70 Hz a window can't hold the six periods the phase method needs to keep a tone's harmonics apart, so the reading is the NSDF's peak, found by band-limited (Lanczos) interpolation rather than a parabola. Above, the phase of the loudest harmonic refines it, in three steps. A tone without its fundamental reads right either way.
- **The note changes past ±55 cents**, or in key where the other note is nearer by more than 10 cents, rather than "after two readings agree": at 90 readings a second, two readings are only 22 ms, and vibrato on a boundary flipped the note back and forth.
- **Time to a reading** (at 48 kHz): a guitar's low E (82 Hz) under 70 ms, a bass's E (41 Hz) under 90 ms, the low B (31 Hz) under 120 ms. The window grows from the sound's start until it holds two periods.
- **Chords:** a cluster of unrelated notes gives no reading, but a major triad reads its root two octaves down (its notes share that period), as most tuners do. The test uses a cluster.
- **Reference shows its tenths:** hertz of 100 or more show no decimals, except on a range 100 Hz wide or narrower ("440.0 Hz"). Only the Reference has one.
- **For the kit:**
  - a text style for the one figure a display is about (`Display`, Mono 40);
  - roles `tunerInTune` and `tunerInTuneBand`, and `Theme::dimmed`;
  - the EQ's latency badge is now a header badge any module can show (`DevicePanel::showBadge`): the Tuner's "Muted", and later "Bypassed while monitoring" (recording.md §22).
- **Device API:** meter kinds `Hertz`, `Pitch`, `Cents` and `Ratio` (architecture.md). `measure_module` names the note ("A2").
- **Presets:** a preset that mutes may render silence (the conformance suite's rule, as for a flipped gate).

## Sign-off

- [x] Automated: unit, conformance and integration tests (2026-10-15); validators to run with the release builds
- [x] Layout lint passes (`fd_panel_tests`)
- [x] Snapshots reviewed (100% / 200%; no signal, in tune, off both ways, Strobe, In key with and without a key, Muted, fading; 2026-10-15)
- [x] Sound signed off by the product owner (2026-10-16): the audio passes unchanged; Mute is clean
- [x] Feel signed off by the product owner (2026-10-16)
