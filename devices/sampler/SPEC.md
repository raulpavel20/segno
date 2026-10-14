# Sampler: device spec

- **Status:** Approved by the product owner (2026-10-10, with the M1 sign-off); IDs are permanent. Stage 2, Multisample, brought forward for the factory presets ([factory-library.md](../../docs/design/factory-library.md), ADR 0055): approved by the product owner (2026-10-14) with every recommendation of S1–S13; built the same day (what building settled is under "Settled while building (stage 2)")
- **Type:** Instrument
- **Tier / milestone:** Tier 1, M1 (classic/one-shot) → M3 (multisample, brought forward) → M4 (slice) → M5 (zone editing)
- **Live target:** Simpler (M1, M4), Sampler (M5). References only ([ADR 0011](../../docs/decisions/0011-calibrated-ambition-and-consolidation.md)).

## Purpose

Turn any sound into an instrument: drop a file on it and play it. M1 covers Classic mode (sustain and loop) and One-shot mode (drums and hits). Later stages add slicing and multisample zones on the same engine.

## Beyond Live

None in M1.

## Stages

| Stage | Scope | Milestone |
|---|---|---|
| Core | Classic and One-shot modes, below | M1 |
| Multisample | Key and velocity zones, round robin, SFZ import ("Stage 2", below) | M3, brought forward from M5 |
| Slice | Transient-detected slices, slice to pads | M4 |
| Zone editing | Zones edited by hand, groups, crossfades, several dropped files made into one instrument | M5 |

## Parameters

| ID | Name | Description | Range | Default | Notes |
|---|---|---|---|---|---|
| `mode` | Mode | Classic plays while the key is held, with optional looping; One-shot plays to the end, as for drums. | Classic, One-shot | Classic | |
| `start` | Start | Where in the sample playback starts. | 0 … 100 % | 0 | Of the sample length |
| `end` | End | Where in the sample playback ends. | 0 … 100 % | 100 | |
| `loop` | Loop | Loops between Loop Start and End while the key is held (Classic only). | Off / On | Off | Classic only |
| `loop.start` | Loop Start | Where the loop starts, within the start-to-end region. | 0 … 100 % | 0 | Relative to the start–end region |
| `loop.xfade` | Loop Crossfade | Crossfades the end of the loop into its start, to hide the seam. | 0 … 50 % | 2 | Of the loop length |
| `root` | Root Key | The note at which the sample plays at its original pitch. | 0 … 127 | 60 | Read from file metadata when present |
| `transpose` | Transpose | Shifts the pitch in semitones. | −48 … +48 st | 0 | |
| `detune` | Detune | Fine-tunes the pitch in cents. | −50 … +50 ct | 0 | |
| `gain` | Gain | The sample's level. | −inf … +12 dB | 0 | |
| `vel.amp` | Velocity → Volume | How much velocity changes the volume: at 0 % every note plays equally loud. | 0 … 100 % | 50 | |
| `amp.a` | Amp Attack | How long a note takes to reach full volume. | 0.5 ms … 20 s | 0.5 ms | |
| `amp.d` | Amp Decay | How long the volume takes to fall to the sustain level. | 0.5 ms … 20 s | 300 ms | |
| `amp.s` | Amp Sustain | The volume held while the key stays down. | 0 … 100 % | 100 % | One-shot ignores it unless `oneshot.gate` is on |
| `amp.r` | Amp Release | How long a note takes to fade out after the key is released. | 0.5 ms … 20 s | 50 ms | One-shot ignores it unless `oneshot.gate` is on |
| `oneshot.gate` | Gate | In One-shot mode, releasing the key fades the sample out instead of letting it play to the end. | Off / On | Off | One-shot: note-off fades out |
| `fade.in` | Fade In | A short fade at the start of each note, against clicks. | 0 … 100 ms | 0.5 ms | Click-free start |
| `fade.out` | Fade Out | A short fade at the end of each note, against clicks. | 0 … 100 ms | 2 ms | Click-free end |
| `filter.on` | Filter | Turns the filter on. | Off / On | Off | Same filter engine as Subtractive Synth |
| `filter.type` | Filter Type | The filter: low pass at 12 or 24 dB per octave, high pass or band pass. | LP 12, LP 24, HP 12, BP 12 | LP 24 | Same filter engine as Subtractive Synth |
| `filter.cutoff` | Cutoff | Where the filter starts to cut: lower is darker. | 20 … 20 000 Hz | 2 000 | log |
| `filter.res` | Resonance | Emphasis at the cutoff; near the top the 24 dB low pass starts to whistle on its own. | 0 … 100 % | 20 | |
| `filter.env` | Filter Env Amount | How far the filter envelope moves the cutoff, up or down. | −100 … +100 % | 0 | Filter envelope to cutoff |
| `filter.key` | Filter Key Track | How much the cutoff follows the note played: at 100 % it rises an octave per octave. | 0 … 100 % | 50 | |
| `fenv.a` | Filter Env Attack | How long the filter envelope takes to rise. | 0.5 ms … 20 s | 2 ms | |
| `fenv.d` | Filter Env Decay | How long the filter envelope takes to fall to its sustain level. | 0.5 ms … 20 s | 400 ms | |
| `fenv.s` | Filter Env Sustain | The filter envelope's level while the key stays down. | 0 … 100 % | 0 % | |
| `fenv.r` | Filter Env Release | How long the filter envelope takes to fall after the key is released. | 0.5 ms … 20 s | 300 ms | |
| `voice.mode` | Voice Mode | Poly plays chords; Mono plays one note at a time; Legato is mono without restarting the envelopes when notes overlap. | Poly, Mono, Legato | Poly | |
| `voice.count` | Voices | How many notes can sound at once. | 1 … 32 | 16 | |
| `voice.glide` | Glide | How long the pitch takes to slide from one note to the next. | 0 … 2 s | 0 | |
| `voice.bend` | Bend Range | How far the pitch bend wheel bends, in semitones. | 0 … 24 st | 2 | |

**Non-parameter state** (`extra`):
- `sample.path`: absolute path, or a `library:` reference to a sound in Segno's Library, resolved on each machine (ADR 0055; one that isn't there says so on the panel);
- `sample.rate` and `sample.frames`: the original sample rate and length, for checking.

Project-relative paths come with collect-and-save (M3). Embedding the audio in the project or preset comes later. State version 1.

## DSP design

- **Loading ([ADR 0017](../../docs/decisions/0017-heavy-data-and-non-parameter-state.md)):**
  - Files are decoded with JUCE-free decoders (`libs/audiofile`): WAV/W64/RF64, AIFF, FLAC, MP3. Decoding is synchronous in `readExtraState`, never on the audio thread.
  - The result is an immutable `SampleData`. It holds the channels (padded for the interpolator) and copies decimated to ½, ¼ … of the rate, plus a min/max overview for the display. It reaches the audio thread through `AssetHandoff`.
  - Swapping samples while playing fades the old voices out (1.5 ms). The old data is freed off the audio thread.
  - A missing file keeps its path in the state and is shown on the panel; the device plays silence.
  - When the panel loads a file with a root note in its metadata (`smpl`/`inst`), it sets Root Key through the host. That is a separate, undoable step.
- **Playback:**
  - Windowed-sinc interpolation from `dsp::SincKernels`: Kaiser, 48 taps, cutoff at 0.45 fs, polyphase tables.
  - The voice reads the smallest decimated copy that is at most 2× its rate. The kernel's cutoff is lowered for the remaining speed-up (up to 96 taps).
  - Playback pitch is limited to ±60 st from the sample's own pitch, which bounds one-shot tails.
- **Loop:** the loop's last *crossfade* frames are blended (equal power) with the frames just before the loop start. The crossfade is limited by the material before the loop start and by half the loop length.
- **Voices:**
  - The shared voice manager.
  - A stolen or retriggered voice fades out over 1.5 ms before the new note starts (click-free).
  - Per voice: amp envelope, stereo filter (the instruments' shared `dsp::SynthFilter`) and filter envelope.
  - Glide in all voice modes; legato keeps the playback position.
  - One-shots play to the end, ignoring note-off unless Gate is on. All-notes-off releases them too.
- **Analysis:**
  - slot 0 = output peak;
  - slot 1 = active voices;
  - slots 2–9 = playheads of up to eight voices (0..1 of the sample, −1 = unused).

  Start, end and loop markers come from parameters.

## UI layout

On the module grid ([device-panels.md](../../docs/design/device-panels.md)): sections left to right, each `id` TITLE [columns], with controls at (column, row) and spans; knobs are 1 × 3 cells. About 1480 px wide; sections collapse.

| Section | Controls |
|---|---|
| `sample` SAMPLE [6] | One sample: Waveform (0, 0, 6 × 6): file name and length; dimmed outside Start–End; loop tint; draggable S, E and L markers; live playheads. A multisample (stage 2): Zones (0, 0, 6 × 4) above the last zone's Waveform (0, 4, 6 × 2) |
| `pitch` PITCH [1] | Transpose (0, 0) · Detune (0, 3) |
| `level` LEVEL [1] | Gain (0, 0) · Velocity → Volume (0, 3) |
| `region` REGION [4] | [Classic \| One-shot] (0, 0, 2 cells per button) · Loop (0, 1, 2 wide), Gate (2, 1, 2 wide): the one the mode ignores is dimmed · Start bar (0, 2, 2 wide), End bar (2, 2, 2 wide) · Loop Start (0, 3), X-Fade (1, 3) |
| `fades` FADES [1] | In (0, 0) · Out (0, 3) |
| `env` ENVELOPES [6] | Amp display (0, 0, 2 × 3) · A D S R (2–5, 0) · Filter-envelope display (0, 3, 2 × 3) · A D S R (2–5, 3) |
| `filter` FILTER [4], power = Filter | Type buttons (0, 0, 4 × 1) · Response display (0, 1, 4 × 2) · Cutoff (0, 3), Res (1, 3), Env (2, 3), Key (3, 3) |
| `voice` VOICE [2] | Mode (0, 0) · Root bar (0, 1) · Voices bar (0, 2) · Glide bar (0, 3) · Bend bar (0, 4), all 2 wide |
| meter strip | Output |

- Drop a file anywhere on the panel to load it, or click the empty waveform (or double-click a loaded one) to browse. An empty Sampler shows "Drop a sample, or click to browse".
- Dropping onto a track in the DAW comes with the DAW slice (C4/C7).

## Stage 2: Multisample (approved 2026-10-14)

Brought forward from M5 for the factory presets ([factory-library.md](../../docs/design/factory-library.md) §4, L8). An orchestral instrument is recorded every few notes at a few loudnesses; it needs **zones**: which sample plays for which key and velocity. One sample stays what it is: a single zone across the keyboard.

### How others do it (October 2026)

- **Live's Sampler:** a Zone tab with Key, Velocity and Sample Select editors. Each is a list of the samples with a bar a sample across the range, with fades at the ends. Round robin and random choice are a switch on the Zone tab (Live 12). Samples over 1 GB stream from disk; a RAM switch loads them into memory ([Ableton](https://help.ableton.com/hc/en-us/articles/360000001690-The-multisample-exceeds-the-maximum-of-1024-MB)).
- **Bitwig's Sampler:** zones on a key × velocity map, groups, round robin a zone, crossfades by key, velocity and Select ([release notes 2.4](https://downloads.bitwig.com/stable/2.4/Release-Notes-2.4.html), [Sound On Sound](https://www.soundonsound.com/reviews/bitwig-studio-3?page=3)).
- **Logic's Sampler:** a Mapping pane with three views: zones across a keyboard, groups, and a zone list with its settings. Dropped files with the same root note become velocity layers ([Apple](https://support.apple.com/en-gb/102060)).
- **Every one** shows zones as rectangles over a keyboard, key across, velocity up, and edits them in a large editor apart from the device's own controls.

### What it does

- **A zone** is one sample with:
  - its keys (low to high) and velocities (low to high);
  - its root key, tuning (cents) and gain (dB);
  - its start and end, and its loop if the file has one;
  - its place in a round robin, or its share of a random choice.
- **A note plays every zone it falls in,** at most four at once, as layers.
  - Zones in a **round robin** take turns, key by key: the first note on a key plays the first, the next the second, and so on.
  - Zones in a **random choice** play one at a time, picked at random for each note. The random numbers start the same at every reset, so a render is the same every time.
- **The Sampler's controls shape the whole instrument,** zones and all: the envelopes, the filter, the voices, Transpose, Detune, Gain, Velocity → Volume.
  - **Root Key** is dimmed for a multisample: each zone has its own.
  - **Start and End** apply to each zone in proportion to its own length.
  - **Loop** uses a zone's own loop from the file; a zone without one loops between Loop Start and End, as one sample does.
- **No new parameters.** IDs stay as they are.

### SFZ files

The instruments come as SFZ files: plain text maps beside their samples, the format sfizz, Decent Sampler and most free libraries use. Loading one is like loading a sample: drop it, browse to it, or pick it in the browser.

| What | Opcodes | What the Sampler does with them |
|---|---|---|
| Headers | `<control>`, `<global>`, `<master>`, `<group>`, `<region>` | Inherited as SFZ says: a region takes its group's, master's and global's opcodes, overriding them |
| Paths | `default_path`, `sample` | Relative to the SFZ file; `\` read as `/`; a name not found as written is looked for ignoring case (maps made on Windows) |
| Keys | `lokey`, `hikey`, `key`, `pitch_keycenter`, `octave_offset`, `note_offset` | Note names or numbers; SFZ's `c4` is 60, Segno's C3 |
| Velocities | `lovel`, `hivel` | |
| Level and pitch | `volume`, `tune`, `transpose`, `amp_veltrack` | `amp_veltrack` of the whole file sets Velocity → Volume |
| Region | `offset`, `end`, `loop_mode`, `loop_start`, `loop_end` | `loop_mode=one_shot` across the file sets Mode to One-shot |
| Choice | `seq_length`, `seq_position`, `lorand`, `hirand` | Round robin and random choice |
| Envelope | `ampeg_attack`, `ampeg_decay`, `ampeg_sustain`, `ampeg_release` | Of the whole file: set the Amp envelope |

- **What it sets beyond zones** (Mode, Velocity → Volume, the Amp envelope) is set through the host as a separate undo step after loading, as a sample's root key is now.
- **Anything else is ignored and said:** the panel's sample line reads "3 settings in the file ignored", and its tooltip names them. Release triggers, key switches, crossfades and CC-driven regions are the common ones; they come with zone editing.
- **Missing samples are said too:** "4 of 22 samples missing", with the zones that would play them left silent. Never silent about it.
- **VSCO 2 CE's maps** use `sample`, the key and velocity ranges, `volume`, `ampeg_*`, `default_path`, `seq_*` and `*rand` (factory-library.md §3), all on the list.

### State, loading and memory

- **State:** `sample.path` names a sample or an SFZ, absolute or a `library:` reference. The map is read from the file each time, as a sample's audio is: the file is the instrument, and a preset or project holds only its path. `sample.zones` (the count) joins `sample.rate` and `sample.frames` for checking. State version 1 still.
- **Loading stays synchronous** ([ADR 0017](../../docs/decisions/0017-heavy-data-and-non-parameter-state.md)): when `readExtraState` returns, every zone is ready, so a render right after a load is right.
  - The zones' samples are decoded in parallel, one task a zone.
  - **Target:** a 50-zone instrument of 8-second stereo samples ready in under half a second on the reference machine, measured by a `[.benchmark]` test.
  - Asynchronous loading and streaming from disk wait for their own ADR (ADR 0017), when instruments outgrow memory.
- **Memory:**
  - A 16-bit file is held as 16-bit, half what floats take. The interpolator reads either.
  - A multisample keeps one decimated copy (½), not seven. Zones sit within a few notes of their roots, and one copy covers two octaves up; beyond that the interpolator lowers its cutoff, as it does now.
  - **The same file is decoded once,** whichever Samplers use it: a cache shared by every Sampler in the process. Two tracks with the same violins, or a preset previewed in the browser and then loaded, hold it once.
  - **Estimates:** the violin section's sustain (22 zones of about 8 s, stereo) about 45 MB; a 90-zone piano at 12 s about 290 MB. The Library's trims (§6 of factory-library.md) keep them so.

### The panel

On the module grid, in the SAMPLE section (6 × 6), nothing else moves:
- **One sample:** as now.
- **A multisample:**
  - **The zone map** (rows 0–3): keys across, the instrument's range and a little either side; velocity up. Each zone is a rectangle; zones that take turns or share a random choice show as one with a count ("×3"). A zone lights while it plays.
  - **The last zone played** (rows 4–5): its waveform, with Start, End and Loop markers that move every zone's, and live playheads.
  - **The sample line:** the SFZ's name, its zone count, and what was ignored or missing.
  - **Hovering a zone** names its sample, keys, velocities and root. **Clicking one** plays its root at the middle of its velocities.
- Dropping, browsing and the empty state are as now; browsing offers SFZ files beside samples.
- **The menu bar** ([ADR 0040](../../docs/decisions/0040-a-menu-bar-and-everything-by-mouse.md)): Module > **Load sample…**, for the selected Sampler, samples and SFZ files alike. The click on the panel stays.

### The browser and MCP

- **The browser** ([browser.md](../../docs/design/browser.md)): SFZ files are a kind of their own, **Multisamples**, MULTISAMPLES in search results, their zone count as the caption.
  - Selected, one plays the presets' phrase (a C3) through a Sampler of its own.
  - Dropped on a Sampler, it loads; anywhere else a Sampler with it, as a sample does. Double-click puts it on the selection.
- **MCP:**
  - `load_sample` takes an SFZ, and says how many zones loaded and what was ignored or missing;
  - **`get_zones`** (new, read-only) lists a Sampler's zones: sample, keys, velocities, root, tuning, gain, round robin;
  - `search` finds SFZ files as `multisample`.

### Not in stage 2

- **Editing zones by hand**, groups, crossfades between zones, and making a multisample from several dropped files: with the zone editor, in the detail panel as the Wavetable's table editor is ([ADR 0053](../../docs/decisions/0053-a-modules-editor-in-the-detail-panel.md)). M5, as planned.
- **Streaming from disk**, and SFZ's release triggers, key switches and CC-driven regions.

### Tests

| What | Target |
|---|---|
| Zone choice | Every key and velocity of a generated map plays the zone it should (`[fast]`) |
| Round robin | Takes turns key by key; restarts with reset (`[fast]`) |
| Random choice | Each zone in its share within 5 % over 10 000 notes; the same sequence after reset (`[fast]`) |
| SFZ reading | Headers inherited; note names; `\` paths and case; every opcode in the table; ignored ones counted (`[fast]`) |
| VSCO 2 CE's maps | The four fetched SFZs read with nothing ignored (`[fast]`) |
| Pitch | A zone plays its sample at its root to ±0.5 cent, tuning and transpose on top |
| Memory | A 16-bit file held at 2 bytes a sample, one decimated copy; one decode for two Samplers |
| Load time | Under 0.5 s for 50 eight-second stereo zones (`[.benchmark]`, Release) |
| Swap while playing | A multisample swapped mid-note: no allocation or locks in `process()`, no clicks |
| State | An SFZ path round-trips; a Library reference resolves; a missing file and missing samples are said |
| Conformance | All pass, with a generated two-zone map in `testSetup` too |

### Questions for review

| # | Question | Recommendation |
|---|---|---|
| S1 | **Where the map lives** | In the SFZ file, read each time, as a sample's audio is; the state holds only its path. Presets stay small and a Library fix reaches them |
| S2 | **SFZ's reach** | The opcodes in the table, which cover VSCO 2 CE and VCSL's maps; anything else ignored and said |
| S3 | **The Sampler's controls on a multisample** | Shape every zone: envelopes, filter, voices, pitch, level. Root Key dimmed; Start and End in proportion; Loop the zones' own or the controls' |
| S4 | **Layers** | A note plays every zone it falls in, up to four |
| S5 | **Round robin** | Key by key, as SFZ counts it; random choice seeded at reset, so renders repeat |
| S6 | **What the file sets beyond zones** | Mode, Velocity → Volume and the Amp envelope, through the host, a separate undo step after loading, as the root key now |
| S7 | **Loading** | Synchronous still (ADR 0017), the zones decoded in parallel; under 0.5 s for 50 eight-second zones. Streaming and asynchronous loading later, with their own ADR |
| S8 | **Memory** | 16-bit files kept 16-bit; one decimated copy; one decode for every Sampler that uses a file |
| S9 | **The panel** | The SAMPLE section shows the zone map and the last zone's waveform; nothing else moves; hover names a zone, click plays it |
| S10 | **The menu bar** | Module > Load sample… for the selected Sampler, samples and SFZ files alike |
| S11 | **The browser** | SFZ files a kind of their own, Multisamples, previewed with the presets' phrase, dropped as a sample is |
| S12 | **MCP** | `load_sample` takes SFZ; `get_zones` new; `search` finds multisamples |
| S13 | **Not now** | Zone editing, groups, crossfades, several dropped files made into one instrument, streaming: M5, with the zone editor in the detail panel |

## Settled while building (stage 2, 2026-10-14)

- **The engine:**
  - A voice plays up to four zones, its **layers**, through one amp envelope, one filter envelope and one filter. Pitch and level are each layer's own.
  - One sample is a map of one zone across every key and velocity, played by the same voices. Its renders are unchanged: its end fades on the voice's gain as before, and the golden metrics pass untouched.
  - Choosing zones is a pure function (`SampleMap::choose`), with the turns of each key and a xorshift generator in `SampleMap::Choice`, reset with the Sampler.
  - A region has its own loop end now, so a zone's loop from its file can end before the zone does.
- **Reading SFZ:**
  - **No loss, so not named:** labels (`*_label`) and ARIA's `ampeg_dynamic`.
  - **Leaves its region out, and named:** what plays only on a condition the Sampler doesn't read: key switches (`sw_*`), controller ranges (`loccN`, `hiccN`, `on_loccN`…), `trigger` other than attack, generators (`sample=*sine`).
  - **Named, its opcodes skipped:** headers other than control, global, master, group and region (`<curve>`, `<effect>`).
  - `#define` is read; `#include` is named.
  - **The whole file's envelope and velocity:** what most regions say, since the Sampler has one envelope. VSCO's percussion: 134 of its 220 regions release over 3 s, the rest over 0.3, 6 or 8 s; 3 s is set (and a one-shot plays to its end anyway).
  - **Mode and Loop:** a file whose every region is `one_shot` sets One-shot; one whose every region loops turns Loop on.
- **Memory and loading:**
  - One sample keeps floats and every level, as it always has: its memory was never the problem, and its sound stays the same.
  - A multisample's zones keep a 16-bit file as 16-bit and one half-rate copy.
  - The decode cache is keyed by path, change time, size and how the file is held. It holds files only while a Sampler does.
  - The zones load on as many threads as the machine has.
  - **Measured in Release:** 50 zones of 8-second stereo 16-bit WAV in **143 ms**, holding 115 MB. FLAC, the Library's format, is to be measured with the Library (factory-library.md §8, phase 3).
- **The analysis bus** has 32 slots, not 16: the Sampler's playheads' zones (10–17) and the zone the last note started (18).
- **The panel:**
  - The SAMPLE section is placed again on the grid when the sound changes between one sample and a multisample.
  - The zone map's line reads "Strings · 48 zones · 1 missing · 1 ignored". Its tooltip names the missing files and the ignored settings.
  - Root Key dims for a multisample.
  - Start, End and Loop Start dragged on the last zone's waveform set every zone's, in proportion. A zone's own loop shows without its L marker.
- **What the file sets, and undo:**
  - **In the DAW** (`Session::loadSample`: drops, double-clicks, MCP, the menu): one undo step, "Load sample Strings", with what the file sets. The file is read before it's put in, so a file that can't play changes nothing, and it isn't decoded twice.
  - **From the panel:** "Load sample Strings", then "Settings from Strings", as the root key always was.
- **The conformance suite** now loads a generated three-zone SFZ (16-bit tones, a round robin of two), so the suite and the snapshots exercise the multisample path. The layout lint also checks one sample's layout.
- **The browser:**
  - Multisamples are a kind of their own, between samples and wavetables.
  - The preview strip reads "A short phrase · 22 zones", as a preset's does.
  - A multisample never becomes a clip: on a track, a Sampler with it.
- **MCP:** `get_zones` gives keys and velocities as low and high (`{"low": "C-2", "high": "B2"}`), since a range written "C-2-B2" can't be read.
- **Tests:** VSCO 2 CE's four SFZ maps (CC0) are kept in `devices/sampler/tests/data`, read with nothing ignored.

## Factory presets

Waiting for sounds: the product owner brings the set they're built on (2026-10-11; [presets.md](../../docs/design/presets.md) §15, P11). Until then the Sampler has only Default settings.

## Automated test targets

| Metric | Target |
|---|---|
| Pitch accuracy (sine sample, ±24 st) | ±0.5 cent |
| Aliasing transposed up 24 st (sweep sample) | < −70 dB |
| Loop seam (crossfade on) | Click score < 8 |
| One-shot plays to the end, ignoring note-off | Exact length |
| Sample swap while playing | No allocation or locks in `process()`; no clicks |
| State round trip | Restores the sample path and parameters; same render |
| Conformance suite (instrument checks) | All pass |

## A/B cases (against the Live target)

Deferred until a Live machine is available.

## A/B log

–

## Sign-off

- [ ] Automated: unit, conformance and golden tests; validators
- [ ] Snapshots reviewed (100% / 200%)
- [ ] Sound signed off by the product owner (date):
- [ ] Feel signed off by the product owner (date):
