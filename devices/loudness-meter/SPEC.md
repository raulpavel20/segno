# Loudness Meter: device spec

- **Status:** Draft, for review by the product owner (questions L1–L12 below). IDs become permanent on approval.
- **Type:** Audio effect (analysis only; audio passes unchanged)
- **Tier / milestone:** Tier 1, M3 (B7)
- **Live target:** none: Live has no loudness meter, and people install a separate one. Checked against the meters people use instead (a widely used free one, broadcast meters, the meters built into other DAWs) and against the standards: ITU-R BS.1770-5, EBU R 128 with EBU Tech 3341 ("EBU Mode") and Tech 3342 (loudness range). References only; nothing is copied ([ADR 0011](../../docs/decisions/0011-calibrated-ambition-and-consolidation.md)).

## Purpose

Tells you how loud the song is, the way streaming services and broadcasters measure it, so you can finish it at the loudness you choose and see what a service will do to it. It reads the loudness now and over the whole song, how much the loudness moves, the true peak, and whether the stereo holds up in mono. The audio passes untouched.

What makes it worth using over a separate meter: it measures **the song, not the session**. Each moment of the song counts once, the last time it played. Looping the chorus ten times doesn't make the song read louder, and replaying a part you've changed updates the whole song's reading without playing it all again (L2).

**Not a limiter:** it never changes the sound. The Limiter keeps its own small loudness readout, on the same measurement.

## Beyond the usual meter

The usual meter shows loudness now and since you pressed reset. These additions use what only a meter inside the DAW knows, the song's position, at a small cost (ADR 0011).

| Addition | Reference | Why it's worth it | Cost |
|---|---|---|---|
| **Measures the song** (L2): each moment counted once, the latest pass | Broadcast meters that tie their history to the timecode and update the programme's reading after an edit | Integrated loudness, loudness range and the true peak are about the song. A running meter counts whatever you played, as often as you played it | Moderate: a store of the song's loudness, 10 values a second, updated as parts are replayed |
| **The history follows the song** | The same broadcast meters | The loudness of the whole song at a glance, with the parts not heard yet left empty, so you see where it's too loud or too quiet | Small, on the same store |
| **What normalising does** (L4): how much a service playing everything at the target turns this song down, or up | Tools sold to preview what streaming does to a master | Shows the cost of a master louder than the target: it will just be played quieter | Small: the rule the export already uses |
| **Stereo: correlation and balance** | Correlation meters on most mastering meters | Out-of-phase parts that vanish in mono, and a mix leaning to one side | Small |
| **Kept with the project** (L9) | The broadcast meters again | Reopen the project and the song's reading is still there | Small |

**Not here:**
- a vectorscope (the stereo picture as a cloud of points): the correlation and balance readouts cover the checks it's used for. Later, if asked for;
- a histogram of the loudness's spread: the loudness range is the number it summarises;
- measuring the whole song offline, faster than playing it: the assistants' `render` tool and the export already do it; inside the meter it comes later (Later stage);
- a loudness readout on the master channel: possible on the same measurement; a mixer question, not this module's;
- surround: the DAW is stereo. The measurement takes stereo or mono.

## Stages

| Stage | Scope | Milestone |
|---|---|---|
| Core | Everything below | M3 |
| Later | Measuring the song offline from the meter (the DAW renders what reaches it); a vectorscope; a loudness histogram; surround with the DAW | With demand |

## Parameters

All are display settings: they never affect audio, and they are not automatable.

| ID (permanent) | Name | Description | Range | Default | Unit | Curve / skew | Smoothing | Automatable |
|---|---|---|---|---|---|---|---|---|
| `target` | Target | The loudness you're aiming for: readings are coloured against it, and the scale is centred on it. | −36 … −5, in 0.5 steps | −14 | LUFS | linear | – | No |
| `ceiling` | Peak Ceiling | The highest true peak you allow: peaks above it are marked. | −9 … 0, in 0.1 steps | −1 | dBTP | linear | – | No |
| `measure` | Measure | Song counts each moment of the song once, the last time it played; Running counts everything as it plays. | Song, Running | Song | choice | – | – | No |
| `scale` | Scale | How much the loudness bars and the history show around the target: 9 LU above it, or 18. | +9, +18 | +9 | choice | – | – | No |
| `units` | Units | Shows loudness as LUFS, or as LU above or below the target. | LUFS, LU | LUFS | choice | – | – | No |

- **Scales** follow EBU Mode: +9 shows the target −18 LU to +9 LU, and +18 shows −36 LU to +18 LU. With the target at −14, +9 spans −32 to −5 LUFS.
- **Units:** in LU, the target reads 0.0 LU, a louder reading is positive and a quieter one negative. The target stays in the same place on the scale either way (EBU Mode).

**Actions** (requests on the analysis bus, never state): **Reset** clears integrated loudness, loudness range, the maximums, the history and the lowest correlation; **Pause** and **Resume** stop and continue counting them. While paused, the live readings still move.

**Non-parameter state** (`extra`, [ADR 0017](../../docs/decisions/0017-heavy-data-and-non-parameter-state.md)): `song.measurement`, the song store (DSP design), kept with the project and the plugin's state, not with presets (a session key, `DeviceInfo::sessionExtraKeys`). It's what the meter has measured, not an edit. It's written when the project is saved and read when the project opens, never through an undo step. State version 1.

## DSP design

```
in L/R ───────────────────────────────────────────────> out L/R (unchanged, zero latency)
   │
   ├─> K-weighting ─> mean square per 100 ms step ─┬─> momentary (4 steps), short-term (30 steps): live
   │                                                ├─> Song: the song store, by song position ─┐
   │                                                └─> Running: by time since Reset ────────────┤
   ├─> true peak, 4× ──────────────────────────────────> live peak, per step ───────────────────┤
   │                                                                                             v
   │                                integrated (gated), loudness range, maximums, true-peak maximum,
   │                                peak to loudness, normalising, history ─> analysis bus
   └─> L·R, L², R² per step ─> correlation (400 ms), lowest since Reset; balance (3 s) ─> analysis bus
```

Everything is computed on the audio thread and published on the analysis bus, as the Limiter's readout is. So the readings keep counting while the panel is closed, and assistants can read them. The panel only draws.

- **Audio:** passes unchanged: a null test against the input, zero latency, no tail. The true-peak detector's 8 samples of delay only delay the reading.
- **Loudness: `dsp::LoudnessMeter`** (libs/dsp, M2), as the Limiter and the export use it:
  - K-weighting and the channel sum of BS.1770, in 100 ms steps;
  - momentary loudness (400 ms) and short-term loudness (3 s), ungated, updated every step (10 times a second);
  - integrated loudness, gated as BS.1770 (−70 LUFS absolute, −10 LU relative) over 400 ms blocks overlapping by 75 %, kept as a histogram (0.01 LU bins);
  - **new: loudness range** (EBU Tech 3342): the short-term loudness every step (10 Hz), gated at −70 LUFS absolute and −20 LU relative, then the 95th percentile minus the 10th. Kept as a second histogram, so it costs the same at any length. It also goes into `model::measureAudio`, so the `render` tool and the export report it.
- **True peak: `dsp::TruePeakDetector`** (libs/dsp, M2): 4× oversampling, as BS.1770 asks for at 44.1 and 48 kHz (192 kHz or more). At higher rates it stays at 4× (more than needed). The reading is the larger of the sample peak and the interpolated one.
- **Measure, Song** (L2), new in libs/dsp as `dsp::SongLoudness`:
  - **The store:** one entry per 100 ms of song time: the step's K-weighted energy and its true peak. A step counts when all its samples were heard in one pass, in order; a step cut by a jump, a loop's wrap or a stop is dropped. A step heard again replaces what was there.
  - **The song's position** comes from `TransportInfo` (`samplePosition`, `playing`; no device API change). Steps are in seconds of song time, so the store survives a change of sample rate. Count-in before the song's start is left out.
  - **Readings from the store:** integrated loudness from the 400 ms blocks of four heard steps in a row; loudness range from the 3 s windows of thirty; maximum momentary and short-term from the same blocks; the true-peak maximum from the steps. Each kind of reading is a histogram updated as steps are replaced. So a reading costs the same for any song length, and replacing a step updates only the blocks it's in.
  - **Size:** room for 3 hours of song time (108 000 steps, about 1.3 MB), allocated in `prepare()`. Later steps aren't counted, and the panel says so.
  - **While the song is stopped,** nothing is added: the live readings (momentary, short-term, the live peak, stereo) still move, so you can check a sound, and the panel says it's counting while the song plays.
- **Measure, Running:** EBU Mode. Everything counts in order as it plays, from Reset, whether the song plays or not. For live input, broadcast work and hosts with no transport.
- **Pause** stops adding steps (both modes); Resume continues. EBU Mode asks for both, and for Reset, which clears integrated loudness and loudness range together.
- **Peak to loudness (PLR):** the true-peak maximum minus the integrated loudness, in LU: how much room the peaks take above the average. Under about 8 LU, a master is heavily limited.
- **Normalising** (L4): the gain a service playing everything at the target would give this song: target − integrated, but a turn-up is held so the true-peak maximum stays at the ceiling. That's the export's normalising rule (export.md). It moves from `app/src/model/Export` into libs/dsp (`dsp::normalisingGain`), so the export and the meter share it and the UI never reimplements it.
- **Correlation:** Σ(L·R) / √(ΣL² · ΣR²) over the last 400 ms (four steps), from +1 (mono) through 0 (unrelated) to −1 (out of phase). No reading while both sides are below −70 dBFS. The lowest value since Reset is kept: a moment that would cancel in mono is easy to miss.
- **Balance:** the short-term loudness of the left channel minus the right's, in LU: + leans left, − leans right.
- **Published** on the analysis bus (16 slots; `MeterSpec`s for MCP): momentary, short-term, integrated, loudness range, maximum momentary, maximum short-term, true-peak maximum, live true peak, peak to loudness, normalising gain, correlation, lowest correlation, balance, seconds of song counted, and a flag for paused. Two meter kinds are new, `LoudnessUnits` (LU, a range or a difference) and `Correlation` (−1 to 1). ADR 0034 allows new meter kinds without an ADR; they're listed in architecture.md's device contract.
- **The history** reads the store (Song) or a ring of the last 30 minutes of steps (Running), whose entries the audio thread writes as atomic values. The panel fetches it through `DeviceUiContext::device` (read-only, ADR 0017), at most once per frame.
- **Saved:** `writeExtraState` writes the heard steps (their song time, energy and peak), about 40 KB for a 5-minute song. `readExtraState` rebuilds the store and its histograms off the audio thread and hands them over with `AssetHandoff` (ADR 0017).

## UI layout

On the module grid ([device-panels.md](../../docs/design/device-panels.md)): sections left to right, each `id` TITLE [columns], controls at (column, row) and spans; knobs are 1 × 3 cells.

| Section | Controls |
|---|---|
| `loudness` LOUDNESS [6] | Bars (0, 0, 2 × 6): momentary and short-term, on the Scale, with the target marked and each one's maximum held · Readouts (2, 0, 4 × 6): Integrated, large (two rows), then Range, True peak, Peak to loudness, Normalised |
| `history` HISTORY [8] | Display (0, 0, 8 × 6): short-term loudness over time, momentary fainter behind it, the target and the ceiling's overs |
| `stereo` STEREO [3] | Correlation (0, 0, 3 × 3): a bar from −1 to +1 with the lowest held, and its value · Balance (0, 3, 3 × 3): a bar from left to right, and its value |
| `measure` MEASURE [4] | Target (0, 0), Peak Ceiling (1, 0) · Reset (2, 0, 2 wide), Pause (2, 1, 2 wide) · a sub-label and its dropdown per row: Measure (0, 3), Scale (0, 4), Units (0, 5) |

21 columns, as wide as the Spectrum.

- **Bars:** two vertical bars, M and S, on the Scale's range, with tick labels in LUFS or LU. The target is a line across both. Each bar holds its maximum as a short line, with the maximum's value above the bar. A bar's top beyond the target is drawn in the over colour.
- **Readouts** (the Limiter's `LoudnessReadout`, moved into `fd_device_ui` and shared):
  - **Integrated:** large, in LUFS or LU. Within 1 LU of the target it's drawn in the on-target colour, above that in the over colour, below it as plain text. "–" until there's a gated block.
  - **Range:** "6.2 LU". Dimmed for its first 60 seconds of counted song, as EBU Mode asks, since it isn't stable yet.
  - **True peak:** the maximum, "−0.8 dBTP", in the over colour above the ceiling.
  - **Peak to loudness:** "13.0 LU".
  - **Normalised:** "−3.1 dB", what normalising to the target does. Its tooltip says so in a sentence.
  - Every value carries its unit, with at most one decimal (EBU Mode).
- **History:**
  - **Song:** the time axis is the song, from its start to the furthest point heard, in minutes and seconds. Parts not heard yet are empty, and the playhead is a line.
  - **Running:** the time since Reset, compressed to fit as it grows, up to the last 30 minutes.
  - **In both:** the target is a line, with a faint band 1 LU either side. Overs (true peaks above the ceiling) are ticks along the top. Hover shows the time and that moment's short-term and momentary loudness.
- **Status line:** the history's label line says what the meter is doing, when it isn't simply counting: "Counting while the song plays" (Song, stopped), "Paused", "Past 3 hours: not counted".
- **Colour roles:** momentary, short-term, on target, over and the target line get roles of their own in `fd_ui`, documented in ui-foundations.md. The meter never tells on-target from over by colour alone: an over also reads as a positive number in LU and sits above the target's line.
- **Reaching it by mouse** (ADR 0040): Reset and Pause are buttons on the panel, as the Limiter's Reset is.
- **Dimming:** Pause while there's nothing counted; Range for its first minute.
- The panel is the module: HISTORY, STEREO and MEASURE can be collapsed to leave LOUDNESS.

## Factory presets

Built and named by Claude ([presets.md](../../docs/design/presets.md) §13; `devices/loudness-meter/presets/`, made with `fd-presets` from recipes), signed off by the product owner. 5 presets, named by use, not by service (ADR 0008, L3); each checked by the conformance suite.

- **Streaming** (Master): −14 LUFS, ceiling −1 dBTP, Song, +9, LUFS. Where most music services play songs.
- **Quieter streaming and podcasts** (Master): −16 LUFS, −1 dBTP. The level of the services that play quieter, and of spoken word.
- **Loud master** (Master): −9 LUFS, −1 dBTP, +18. For club masters; Normalised shows how much the services will turn it down.
- **Broadcast, Europe** (Broadcast): −23 LUFS, −1 dBTP, Running, +9, LU. EBU R 128, as EBU Mode meters show it.
- **Broadcast, US** (Broadcast): −24 LUFS, −2 dBTP, Running, +9, LU. ATSC A/85.

## Automated test targets

| Metric | Target |
|---|---|
| Audio through the device | Null against the input (< −120 dBFS), zero latency |
| EBU Tech 3341 cases 1–5 (stereo sines, gating) | M, S and I within ±0.1 LU, at 44.1, 48 and 96 kHz |
| Tech 3341 case 9 (short-term constant after 3 s) | S = −23.0 ±0.1 LUFS |
| Tech 3341 cases 11 and 14 (live: successive maximums) | Max S, then max M, reading −38, −37 … −19 ±0.1 LUFS in turn as the signal plays |
| Tech 3341 case 12 (momentary constant after 1 s) | M = −23.0 ±0.1 LUFS |
| Tech 3341 cases 15–23 (true peak) | Within +0.2 / −0.4 dB of the stated level, at 44.1, 48 and 96 kHz |
| EBU Tech 3342 cases 1–4 (loudness range) | 10, 5, 20, 15 LU, ±0.1 (the standard allows ±1) |
| Calibration: 1 kHz stereo sine at −18 dBFS | −18.0 LUFS |
| Song: a 60 s song played once, then again | The same integrated loudness, range and peak (±0.01) |
| Song: a 10 s part looped five times within a 60 s song played once | Integrated as the song played once (±0.05 LU); Running differs |
| Song: a part replaced by a louder one and replayed | Integrated equals the edited song's, measured offline (±0.1 LU) |
| Song: a jump in the middle of a step | That step dropped; the rest unchanged |
| Song, stopped: input still flowing | Momentary and short-term move; integrated doesn't |
| Pause, then audio | Integrated and range hold; the live readings move |
| Correlation: L = R; L = −R; two independent noises | +1.00; −1.00; 0 ± 0.05 |
| Balance: the right channel 6 dB down | +6.0 ± 0.1 LU |
| Normalised gain | Equals the export's for the same audio, target and ceiling |
| Saved and reopened (Song) | The same readings and history |
| The store's readings at 3 hours | Each reading's cost within the budget of one at 1 minute (±10 %) |
| `measure_module` on the meter | Every published reading, by name and unit |
| No allocation, lock or file access in `process()` | Conformance |
| Conformance suite | All pass |

The EBU's authentic programme cases (Tech 3341 7–8, Tech 3342 5–6) need the EBU's own files. They're checked by hand (A/B cases) and kept out of the repository.

## A/B cases (against reference meters)

| Case | Stimulus | Our settings | Reference | What to check |
|---|---|---|---|---|
| EBU programme files | The EBU's authentic programme test files | Running | Their stated values | I within ±0.1 LU, LRA within ±1 LU |
| A finished song | A released master, played through | Song | A widely used free meter, reset before | I, LRA and true peak within 0.1 |
| A song you're mixing | Your mix, a chorus looped several times | Song and Running | The free meter | Running agrees with it; Song reads the song as heard once |

## A/B log

–

## Questions for review

| # | Question | Recommendation |
|---|---|---|
| L1 | **What it reads** | Momentary, short-term and integrated loudness, loudness range, the true-peak maximum, peak to loudness, what normalising does, correlation and balance. Everything EBU Mode asks for, and what music meters add. Leave out a vectorscope and a loudness histogram (Later) |
| L2 | **Measuring the song, not the session** | Measure: Song by default. Each moment of the song counts once, the latest time it played, so looping a part or playing the intro ten times doesn't skew the reading, and replaying a changed part updates the song's reading. Counting happens while the song plays; stopped, the live readings still move. Running is there for live input, broadcast work and hosts without a transport: everything as it plays, from Reset (EBU Mode) |
| L3 | **Targets, named by use** | Target (−14 LUFS by default) and Peak Ceiling (−1 dBTP) as two knobs. Five presets named by use: Streaming, Quieter streaming and podcasts, Loud master, Broadcast Europe, Broadcast US. ADR 0008 keeps services' names out of the UI and presets, so the presets don't say which service plays at which level; the manual does. Naming them in the UI would need a new ADR |
| L4 | **Normalised** | One readout: how much a service playing everything at the target turns this song down (or up, as far as the peaks allow). It's the export's normalising rule, moved into the DSP library and shared |
| L5 | **The scale** | EBU Mode's two: +9 (target −18 to +9 LU) by default, +18 for wide or quiet material. LUFS or LU, the target in the same place either way |
| L6 | **The history** | Short-term loudness over the song (Song) or since Reset (Running), momentary fainter, the target band and overs marked; parts not heard yet empty |
| L7 | **Stereo** | Correlation over 400 ms with its lowest held, and balance from short-term loudness. Two bars, no vectorscope |
| L8 | **Reset and Pause** | Two buttons, as EBU Mode asks. Reset clears everything counted; Pause holds integrated and range while the live readings move. No automatic reset when playback starts: in Song mode it isn't needed, and in Running mode it's what broadcast meters avoid |
| L9 | **Kept with the project** | The song's measurement is saved with the project (not presets), so reopening shows the song's reading without playing it again. It's not an edit: no undo step |
| L10 | **Assistants** | `measure_module` reads every published reading (two new meter kinds: loudness units and correlation). The `render` tool and the export report gain the loudness range. Assistants measure a song offline with `render`, so they need no Reset |
| L11 | **Shared code** | The Limiter's readout and this meter share `dsp::LoudnessMeter`, the true-peak detector and the readout component. The Limiter doesn't gain Song mode |
| L12 | **Name and IDs** | Named **Loudness Meter**: what people search for, and "Loudness" alone would read like an effect. Device `loudness-meter`, plugin code `Flud`, CLAP `org.segnoaudio.loudness-meter`; section IDs `loudness`, `history`, `stereo`, `measure` |

## Sign-off

- [ ] Automated: unit, conformance and golden tests; validators. The audio is the input, so the null test stands in for a golden file
- [ ] Layout lint passes (`fd_panel_tests`)
- [ ] Snapshots reviewed (100% / 200%; expanded, collapsed, folded; counting, paused, stopped in Song mode, a song with overs)
- [ ] Sound signed off by the product owner (date): the audio passes unchanged; the readings match a reference meter
- [ ] Feel signed off by the product owner (date):
