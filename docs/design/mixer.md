# Mixer: design (M2 step C3)

How mixing works in Segno: levels, panning, mute and solo, return tracks with sends, the master track, and meters. The decisions are in [ADR 0026](../decisions/0026-mixing-in-the-arrangement.md) (proposed). It builds on the track header kit ([ui-foundations.md](ui-foundations.md), ADR 0023), one kind of track ([ADR 0019](../decisions/0019-one-kind-of-track.md)) and the arrangement-first scope ([ADR 0001](../decisions/0001-arrangement-only-scope.md)).

**Status:** proposed, for review before building.

## 1. Where you mix

There is no separate mixer window or view in M2. You mix where you arrange:

- **Track headers** carry mute and solo, a level meter, and (when the track is tall enough) volume and pan bars.
- **The inspector** shows the selected track's whole channel: volume, pan, mute, solo, its sends, and a larger meter.
- **The detail panel** shows the selected track's modules, as now. Selecting the master track shows the master's chain (where the Limiter goes).

A mixer layout with channel strips side by side (a "Mixer" tab in the detail panel) is a later step, recorded in the innovation backlog. The controls described here are its building blocks.

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
| **Volume** | −inf … +6 dB | 0 dB | A bar like the modules' bars, the same drag, Shift for fine, double-click resets, type a value |
| **Pan** | 50L … C … 50R | C | Balance (below) |
| **Mute** | on, off | off | |
| **Solo** | on, off | off | Solo in place (below) |
| **Send** (per return) | −inf … 0 dB | −inf | Post-fader by default; pre-fader per send |

**Gain law.** Volume is plain gain in dB; 0 dB passes the signal unchanged, so a neutral mix nulls against its sources.

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
- Meter values come from the engine's level measurers, read on the UI's frame clock; nothing is drawn when the arrangement isn't visible.

## 5. Track headers

The track header kit's slots, filled:

| Slot | Shown when | Contents |
|---|---|---|
| Buttons | height ≥ 32 | Mute (M) and solo (S); arm (C5) joins them |
| Controls | height ≥ 80 | Volume and pan bars, side by side |
| Meter | always (right edge) | The track's meter |

The master's header shows its name, the meter and, when tall enough, its volume bar. Return tracks' headers are like tracks' headers, without arm.

## 6. Inspector (track selected)

From top to bottom, under the track's name and colour:

1. **Volume** and **Pan** bars, and the meter beside them.
2. **Mute** and **Solo** buttons.
3. **Sends:** one bar per return track ("A Return −12.0 dB"), each with a Pre button. Hidden when there are no returns, with "Add return track" in its place.

For a return track: volume, pan, mute, solo. For the master: volume, pan.

## 7. Editing and undo

| Change | Undo | Unsaved change |
|---|---|---|
| Volume, pan, send level (one step per gesture: a drag, a typed value, a reset) | "Volume", "Pan", "Send A" | Yes |
| Mute, send pre/post | "Mute", "Send A pre-fader" | Yes |
| Solo | Never | No (saved with the next save) |
| Add return track | "Add return track" | Yes |

Selecting several tracks and changing one's volume, pan or mute changes them all, by the same amount (volume, in dB) or to the same state (mute), in one step.

## 8. Keyboard and actions

| Action | ID | Shortcut |
|---|---|---|
| Mute selected tracks | `track.mute` | Alt+M |
| Solo selected tracks (exclusive) | `track.solo` | Alt+S |
| Add return track | `track.addReturn` | Ctrl+Alt+T |
| Show or hide returns and master | `view.returns` | Ctrl+Alt+R |

All are in the command palette. Plain M and S are avoided: with the computer keyboard playing notes, S is a note.

## 9. Assistants (MCP)

Each comes with a test that drives it through MCP (ADR 0021):

| Tool | Arguments | Result |
|---|---|---|
| `set_mix` | `track` (any kind), `volume?` (dB or "-inf"), `pan?` (−1 … 1 or "25L"), `mute?`, `solo?` | The track's mix as set; one undo step (solo alone: none) |
| `add_return_track` | `name?` | The return's id; every track now has a send to it |
| `set_send` | `track`, `return`, `level?` (dB), `preFader?` | The send as set |

`get_project` and the track resources gain each track's `mix` (volume, pan, mute, solo, sends) and its `kind` ("track", "return", "master"); the master and the returns are listed after the tracks. `render` of the mix includes returns and the master chain, as now.

## 10. Engine (tracktion)

- Each track's volume and pan are tracktion's volume plugin at the end of its chain (already there, hidden from the module chain); its level meter is the meter plugin after it.
- A send is tracktion's aux send plugin, placed before the volume plugin (pre-fader) or after it (post-fader); a return track has an aux return plugin at the start of its chain, on the same bus.
- The master is tracktion's master track: its plugin list is the master chain.
- Pan uses the balance law above. If tracktion's pan laws can't express it exactly, the volume plugin's pan is left at centre and our own balance stage applies it, so the "laws exact" test holds.

## 11. Tests (Done when)

- **Laws exact:** volume at x dB scales by exactly x dB; pan at each position gives the documented gains on each side (±0.01 dB); sends at x dB, pre and post.
- **A neutral mix nulls** against its sources: tracks at 0 dB, centre, no sends, rendered, against the summed sources (< −120 dBFS).
- **Solo:** a soloed track with a send plays with its return; others are silent; a sidechain source still keys while solo-silenced.
- **Undo:** each row of §7.
- **MCP:** each tool in §9.
- **Manual:** feel of the bars and buttons in the headers and the inspector; meters against the sound.

## Questions for review

1. **Returns and master at the bottom of the track list, scrolling with it** (proposed), or pinned to the bottom of the arrangement so they're always visible? Pinning is nicer while mixing but takes height from the tracks.
2. **Solo: exclusive on click, Ctrl+click to add** (proposed, as in Live), or add on click and exclusive with Ctrl?
3. **Balance pan with 0 dB at centre** (proposed), or a −3 dB constant-power law (centre −3 dB)? Balance keeps levels stable; constant power is what some engineers expect from mono sources.
