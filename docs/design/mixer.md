# Mixer: design (M2 step C3)

How mixing works in Segno: levels, panning, mute and solo, return tracks with sends, the master track, meters, and the mixer view. The decisions are in [ADR 0026](../decisions/0026-mixing-in-the-arrangement.md). It builds on the track header kit ([ui-foundations.md](ui-foundations.md), ADR 0023), one kind of track ([ADR 0019](../decisions/0019-one-kind-of-track.md)) and the arrangement-first scope ([ADR 0001](../decisions/0001-arrangement-only-scope.md)).

**Status:** approved by the product owner (2026-10-09), with a separate mixer view added (§6). Built in three parts (§12).

## 1. Where you mix

You mix where you arrange, or in the mixer view when you want every channel at once:

- **Track headers** carry mute and solo, a level meter, and (when the track is tall enough) volume and pan bars.
- **The inspector** shows the selected track's whole channel: volume, pan, mute, solo, its sends, and a larger meter.
- **The mixer view** (§6) shows every channel as a strip, side by side. It takes the arrangement's place in the centre of the window; the transport bar, browser, inspector and detail panel stay.
- **The detail panel** shows the selected track's modules, in either view. Selecting the master track shows the master's chain (where the Limiter goes).

Every place edits the same channel, through the same commands: a change in one shows at once in the others.

## 2. Tracks

| Kind | Clips | Chain | Mixer controls | Output |
|---|---|---|---|---|
| **Track** (audio and MIDI, ADR 0019) | Yes | Yes | Volume, pan, mute, solo, a send to each return | Master |
| **Return track** | No | Yes (usually reverbs and delays) | Volume, pan, mute, solo | Master |
| **Master track** | No | Yes (the Limiter, a meter) | Volume (the master level), pan | The audio output |

- Return tracks and the master sit **below the other tracks**, in that order, after a divider line. They have lanes, which stay empty until automation (C4) gives them something to show.
- **Add return track** (Ctrl+Alt+T, the palette, the + menu in the track list) adds one at the end of the returns, named "A Return", "B Return" and so on. Every track gets a send to it, at −inf (off).
- There is exactly one master track; it can't be removed.
- Folder and group tracks come later (with racks, M4).

## 3. Channel controls

| Control | Range | Default | Notes |
|---|---|---|---|
| **Volume** | −inf … +6 dB | 0 dB | A bar (headers, inspector) or a fader (mixer view): the same drag, Shift for fine, double-click resets, type a value |
| **Pan** | 50L … C … 50R | C | Balance (below) |
| **Mute** | on, off | off | |
| **Solo** | on, off | off | Solo in place (below) |
| **Send** (per return) | −inf … 0 dB | −inf | Post-fader by default; pre-fader per send |

**Gain law.** Volume is plain gain in dB; 0 dB passes the signal unchanged, so a neutral mix nulls against its sources.

**Volume taper.** Every volume control (bar or fader) and every send maps its travel the same way. At position p (0 at the bottom or left, 1 at the top or right), the gain is (p / p₀)ⁿ, with n ≈ 3.096 chosen so that the top of a volume control is exactly +6 dB (n = 0.3 · ln 10 / ln 1.25):
- volume: p₀ = 0.8, so 0 dB sits at 80 % of the travel, −6 dB at 64 %, −20 dB at 38 %, +6 dB at the top and −inf at the bottom;
- sends: p₀ = 1, so 0 dB is at the top.

The taper is one pure function (`model::volumeTaper`), unit-tested, used by every control.

**Pan law: balance.** Tracks are stereo (instruments are stereo, mono files play on both sides), so pan is a balance control. At centre both sides pass at 0 dB. Panning one way leaves the near side at 0 dB and turns the far side down along a quarter-cosine, to silence at the end (−3 dB halfway). The centre never changes level, so panning a finished part doesn't change its loudness at centre, and a mono-compatible mix stays predictable.

**Mute** is part of the mix: saved, undoable, and in the project's unsaved changes.

**Solo** is a listening aid:
- Soloing a track silences every track that isn't soloed, except return tracks and the master, so the soloed track is heard with its reverb and delay sends.
- A click solos that track alone (it clears other solos); Ctrl+click adds it to the soloed set.
- Solo is saved with the project but is never an undo step and never an unsaved change on its own.
- Sidechain sources keep feeding their sidechains when silenced by solo, as when muted (ADR 0025): a soloed bass still ducks to the kick.

**Sends.** Each track has one send per return track.
- Post-fader (default): the send follows the track's volume. Pre-fader: it doesn't (for headphone-style or parallel effects).
- Muting a track silences its sends too.
- Return tracks have no sends of their own in M2, so routing can't loop.

## 4. Meters

- **Where:** an 8 px strip on the right of every track header (two 3 px bars, left and right), a larger stereo meter in the inspector, and the master's header.
- **What:** the peak level after volume and pan, before the master. It shows what is heard: a muted or solo-silenced track shows nothing.
- **Ballistics:** the shared `ui::Meter` (instant rise, fall at about 20 dB/s, peak hold for 1.5 s).
- **Clipping:** above 0 dBFS the top of the bar lights in `meterClip`. On the master it stays lit until clicked.
- Meter values come from the engine's level measurers, read on the UI's frame clock; nothing is drawn for a view that isn't visible.
- **In the mixer view,** each strip has a tall stereo meter beside its fader, with a dB scale, and a peak readout above it: the loudest peak since it was last clicked, red above 0 dBFS.

## 5. Track headers

The track header kit's slots, filled:

| Slot | Shown when | Contents |
|---|---|---|
| Buttons | height ≥ 32 | Mute (M) and solo (S); arm (C5) joins them |
| Controls | height ≥ 80 | Volume and pan bars, side by side |
| Meter | always (right edge) | The track's meter |

The master's header shows its name, the meter and, when tall enough, its volume bar. Return tracks' headers are like tracks' headers, without arm.

## 6. Mixer view

A second main view: channel strips side by side, in place of the arrangement.

**Switching.** A two-segment switch in the transport bar ("Arrange" | "Mix") and Ctrl+Alt+X (`view.mixer`) toggle between the two. Each view keeps its own scroll position. Focus mode shows whichever is current.

**Layout:**

```
┌ header: Modules  Sends  Narrow ─────────────────────────────────────────┬────────┐
│ ▌Drums  │ ▌Bass   │ ▌Keys   │ ▌Vox    ┆ ▌A Return │ ▌B Return ┆      │ ▌Master │
│ EQ      │ Comp    │ Synth   │ EQ      ┆ (module   │           ┆      │ Limiter │
│ Comp    │ EQ      │         │ Comp    ┆  names)   │           ┆      │         │
│ A ▬▬ Pre│ A ▬  Pre│ A ▬▬▬Pre│ A ▬▬ Pre┆           │           ┆      │         │
│ B ▬  Pre│ B    Pre│ B ▬ Pre │ B ▬▬▬Pre┆           │           ┆      │         │
│ ◀ C ▶   │ ◀ 20L ▶ │ ◀ C ▶   │ ◀ 10R ▶ ┆ ◀ C ▶     │ ◀ C ▶     ┆      │ ◀ C ▶   │
│ [M] [S] │ [M] [S] │ [M] [S] │ [M] [S] ┆ [M] [S]   │ [M] [S]   ┆      │         │
│  -3.2   │  -0.4   │  -8.0   │  -1.1   ┆  -12.6    │  -20.1    ┆      │  -0.3   │
│ ┃ ▐▐    │ ┃ ▐▐    │ ┃ ▐▐    │ ┃ ▐▐    ┆ ┃ ▐▐      │ ┃ ▐▐      ┆      │ ┃ ▐▐    │
│ ╋ ▐▐    │ ┃ ▐▐    │ ╋ ▐▐    │ ┃ ▐▐    ┆ ╋ ▐▐      │ ┃ ▐▐      ┆      │ ╋ ▐▐    │
│ ┃ ▐▐    │ ╋ ▐▐    │ ┃ ▐▐    │ ╋ ▐▐    ┆ ┃ ▐▐      │ ╋ ▐▐      ┆      │ ┃ ▐▐    │
│ 0.0 dB  │ -2.0 dB │ -6.5 dB │ 1.0 dB  ┆ -3.0 dB   │ 0.0 dB    ┆      │ 0.0 dB  │
└─────────┴─────────┴─────────┴─────────┴───────────┴───────────┴──────┴─────────┘
```

- **Order:** tracks in track order, then the return tracks after a divider; the master is pinned at the right edge. The strips scroll horizontally (wheel, Shift+wheel, horizontal wheel); the master never scrolls away.
- **A strip, top to bottom:**
  1. the track's colour (a 3 px bar) and name; a click selects the track, as in the arrangement;
  2. **Modules:** the chain's module names, one 20 px row each, dimmed when off; a click selects the track and shows that module in the detail panel. Up to six rows; more scroll;
  3. **Sends:** one compact bar per return, with its Pre button;
  4. **Pan** (a bar);
  5. **Mute** and **Solo** (and Arm from C5);
  6. the **peak readout**;
  7. the **fader** and the **meter**, with a dB scale between them, filling the remaining height;
  8. the **volume value**, a number field: type a level.
- **Return strips** have no sends; the **master strip** has no sends, mute or solo.
- **Header toggles:** Modules and Sends show or hide those sections in every strip; Narrow makes strips 64 px wide instead of 96. They are view state: saved with the project, never undo steps.
- **Selection and keys** are shared with the arrangement: the selected track's strip is highlighted; Left and Right move the selection; Alt+M, Alt+S and every track action work here too.
- **Geometry** comes from a pure layout model (`model::ChannelStripLayout`, unit-tested like the track header layout): given a strip's size and which sections show, it places every part on the 4 px grid. Strips are drawn with the shared components (`ui::Fader`, `ui::Meter`, bars, toggles), so the UI lint applies.
- **Performance:** only visible strips are laid out and their meters updated; the view keeps 60 fps with 64 tracks.

## 7. Inspector (track selected)

From top to bottom, under the track's name and colour:

1. **Volume** and **Pan** bars, and the meter beside them.
2. **Mute** and **Solo** buttons.
3. **Sends:** one bar per return track ("A Return −12.0 dB"), each with a Pre button. Hidden when there are no returns, with "Add return track" in its place.

For a return track: volume, pan, mute, solo. For the master: volume, pan.

## 8. Editing and undo

| Change | Undo | Unsaved change |
|---|---|---|
| Volume, pan, send level (one step per gesture: a drag, a typed value, a reset) | "Volume", "Pan", "Send A" | Yes |
| Mute, send pre/post | "Mute", "Send A pre-fader" | Yes |
| Solo | Never | No (saved with the next save) |
| Add return track | "Add return track" | Yes |

Selecting several tracks and changing one's volume, pan or mute changes them all, by the same amount (volume, in dB) or to the same state (mute), in one step. (This waits for the arrangement to select several tracks; it selects one today.)

## 9. Keyboard and actions

| Action | ID | Shortcut |
|---|---|---|
| Mute selected tracks | `track.mute` | Alt+M |
| Solo selected tracks (exclusive) | `track.solo` | Alt+S |
| Add return track | `track.addReturn` | Ctrl+Alt+T |
| Show or hide returns and master (arrangement) | `view.returns` | Ctrl+Alt+R |
| Arrangement ⇄ mixer view | `view.mixer` | Ctrl+Alt+X |

All are in the command palette. Plain M and S are avoided: with the computer keyboard playing notes, S is a note.

## 10. Assistants (MCP)

Each comes with a test that drives it through MCP (ADR 0021):

| Tool | Arguments | Result |
|---|---|---|
| `set_mix` | `track` (any kind), `volume?` (dB or "-inf"), `pan?` (−1 … 1 or "25L"), `mute?`, `solo?` | The track's mix as set; one undo step (solo alone: none) |
| `add_return_track` | `name?` | The return's id; every track now has a send to it |
| `set_send` | `track`, `return`, `level?` (dB), `preFader?` | The send as set |
| `measure_mix` | `seconds?` (0.5) | Every channel's peak level over that window (dBFS, null when silent), and whether anything clipped. The meters' values, without taking them from the views |

`get_project` and the track resources give each channel its `kind` ("track", "return", "master") and its `mix` (volume, pan, mute, solo, sends); the return tracks are listed in `returns` and the master in `master`, beside `tracks`. `add_module`, `get_module` and the other module tools work on returns and the master too. `render` of the mix includes the returns and the master chain.

## 11. Engine (tracktion)

As built (C3a):
- **Channels.** Each track's volume and pan are tracktion's volume plugin at the end of its chain (hidden from the module chain); its level meter is the meter plugin after it. The master gets the same pair at the end of the master's chain; tracktion's own master volume is kept at unity (it starts at −3 dB).
- **Pan law.** tracktion's laws are equal-power or linear, so a small patch adds a balance law (`cmake/patches/tracktion_engine-balance-pan-law.patch`), which every channel uses.
- **Return tracks** are audio tracks marked as returns, with an aux return plugin at the start of their chain on their own bus (named after the track). They are solo-isolated, so a solo never silences them. New tracks are inserted before them.
- **Sends** are tracktion's aux send plugins, created when first set: just before the volume plugin (pre-fader) or just after it (post-fader).
- **Mute** is written through the edit's undo manager (tracktion's own setter isn't undoable); **solo** is written without it, and the "changed" mark it causes is taken back when there was none before.
- **Engine behaviour** (`engine::makeEngineBehaviour`, used for every engine, the tests' too): a track that isn't heard sends nothing to the returns, even when it keeps running to feed a sidechain.
- **Snapshots** list `tracks`, `returns` and `master` apart, each channel with its `mix`; volume, pan and send changes refresh them.
- **Meters** are read through `Session::levelReader`: a client of the channel's meter plugin of its own, so views and `measure_mix` never take each other's peaks.

## 12. Building it

Three parts, each its own commit with its tests, MCP tools and manual checks:

| Part | Scope | Done when |
|---|---|---|
| **C3a Engine and model** | Track kinds; the master and return tracks; sends (pre and post); volume, pan, mute and solo with their laws; the volume taper; the mix in the snapshot; Session commands with their undo rules; `set_mix`, `add_return_track`, `set_send`, `measure_mix` | Laws exact, the neutral-mix null, solo, undo (§13) and every tool through MCP |
| **C3b In the arrangement** | Return and master rows below the tracks; the track header slots (mute, solo, volume and pan bars, meter); the inspector's channel; the actions of §9 | Header layout tests; smoke tests of the controls; snapshots reviewed |
| **C3c Mixer view** | `model::ChannelStripLayout`, `ui::Fader`, the view and its header toggles, the Arrange/Mix switch, view state, selection and keys | Layout model tests; smoke tests (switching, selection, a fader drag is one undo step); 60 fps with 64 tracks; snapshots reviewed |

## 13. Tests (Done when)

- **Laws exact:** volume at x dB scales by exactly x dB; pan at each position gives the documented gains on each side (±0.01 dB); sends at x dB, pre and post.
- **A neutral mix nulls** against its sources: tracks at 0 dB, centre, no sends, rendered, against the summed sources (< −120 dBFS).
- **Solo:** a soloed track with a send plays with its return; others are silent; a sidechain source still keys while solo-silenced.
- **Undo:** each row of §7.
- **Taper:** the documented positions (0 dB at 80 %, −6 dB at 64 %, top and bottom) and the round trip position ↔ gain.
- **MCP:** each tool in §10.
- **Mixer view:** the strip layout model for every combination of sections and widths; switching views keeps scroll positions; a strip click selects its track.
- **Manual:** feel of the bars, faders and buttons in the headers, the inspector and the mixer view; meters against the sound.

## Decisions from the review (product owner, 2026-10-09)

1. Returns and the master sit at the end of the track list and scroll with it (B25).
2. Solo is exclusive on click; Ctrl+click adds (B26).
3. Pan is a balance with 0 dB at centre (B27).
4. A separate mixer view, in M2 (§6).
