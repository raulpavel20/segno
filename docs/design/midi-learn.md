# MIDI learn (M3 C8)

A hardware controller's knob, fader, button or pad moves any control in Segno: you point at the control, move the hardware, and it's mapped. This is generic MIDI learn, for every controller. The Push 2's own support is a separate design ([push-2.md](push-2.md), ADR 0054); in User mode the Push is mapped here too. It builds on recording's MIDI inputs ([recording.md](recording.md) §3), automation ([automation.md](automation.md) §8), presets ([presets.md](presets.md) §11: mappings belong to the project and survive a preset load) and the settings page ([settings.md](settings.md)).

**Status:** approved by the product owner (2026-10-15), every recommendation of ML1–ML14. Built 2026-10-16 (§13).

## 1. What others do (research, 2026-10-15)

- **Ableton Live 12:**
  - **Learning:** MIDI Map Mode (Cmd/Ctrl+M) puts a blue overlay on every mappable control; you click one, move the hardware, and stay in the mode until you leave it.
  - **The Mapping Browser** lists each mapping with its Min and Max, editable, and inverts by swapping them.
  - **Takeover:** None, Pick-Up or Value Scaling, one setting for everything.
  - **Relative encoders:** four encodings, guessed from the first moves and shown so you can correct the guess.
  - **Notes:** a note toggles a switch, or flips a knob between its Min and Max.
  - **Stored** with the Set; hardware moves record automation like the mouse.
- **Bitwig:** "Learn Controller Assignment…" on a parameter's right-click (a green frame, "turn a knob"), or the Mappings panel, where everything mappable turns green. Mappings are kept per project.
- **Logic** (Mac): Cmd-L learns for the selected parameter. Assignments follow the focused track and are kept with the app, unless "pinned" to a track, which keeps them with the project.
- **Logic on iPad:**
  - learns in either order, screen or hardware first;
  - outlines a mappable control dotted, a mapped one solid;
  - offers buttons as momentary, toggle, increment or set.
- **Cubase's MIDI Remote:** each mapping has Jump, Pickup, Scaled or Toggle, a Min and Max with Invert, follows a fixed target or the selected track, and is kept with the project or globally.
- **REAPER:** Learn on a plug-in's parameter. Each mapping has soft takeover, can be relative or a toggle, and can act only while its plug-in is focused.
- **FL Studio:** right-click › Link to controller; Multilink pairs several at once. Pickup is one global setting.
- **Studio One / Studio Pro:** no mode. The last software control and the last hardware control touched sit side by side, and Assign links them. Mappings can be global, or focused on whichever plug-in window is in front.
- **Most of them:**
  - learn in a mode with marked targets, and on a control's right-click;
  - give each mapping a Min and Max, inverted by swapping;
  - choose how a knob takes over a value that differs from it;
  - guess a relative encoder's encoding;
  - list every mapping in one place, where it's removed;
  - record automation from the hardware as from the mouse;
  - can send values back to motorised faders and LED rings.
- **No manual says** whether making a mapping is an undo step.

## 2. Principles

1. **Point and move.** Mapping a control takes a click and a twist, with no dialog.
2. **What you map is what moves:** a mapping is to that control, wherever it is. It doesn't follow the selection: the Push 2's banks do that (push-2.md), and so can a later "follow the selected module" layer (§11).
3. **The project keeps its mappings** (presets.md §11), as Live and Bitwig keep theirs. Only the transport's buttons, which belong to your setup more than the song, are kept with the app (ML7).
4. **Hardware is a hand on the control:** it records automation, takes undo steps and shows its value as the mouse does.

## 3. Learning a mapping

Two ways, both by mouse (ADR 0040):

- **A control's right-click > Learn MIDI** (ML1): the control waits, outlined and pulsing, with "Move a control on your MIDI device" as its tooltip. The first message maps it. Escape or a click elsewhere gives up.
- **Options > MIDI map mode** (ML2), for mapping many at once:
  - every control that can be mapped is outlined, and mapped ones show their message;
  - click a control to choose it, then move the hardware: it's mapped, and the next click chooses the next;
  - you stay in the mode until you leave it (the same menu item, the banner's Done, or Escape).
  - While it's on, a banner across the top of the arrangement says "MIDI map mode · click a control, then move one on your device", with **Done**.
  - The inspector shows the project's mappings (§6), so you see each one land.
  - No default shortcut: Ctrl+M is the metronome's. The palette finds it, and Settings > Keys can give it one.

**What the first message makes:**
- **A CC:** a knob or fader. Two CCs 32 apart arriving together (0–31 with 32–63) are one 14-bit control.
- **A note:** a button (pads and keys).
- **Pitch bend:** a 14-bit fader.
- **Relative encoders** are guessed from the first moves (values just above 0 or 64, or just below 128), as Live does. The guess shows in the mapping's row (§6), where it's corrected.

**Learning picks only what it's waiting for:** the moment a control is chosen (or right-clicked to learn), the next message from any enabled input maps it. While nothing is waiting, the controller plays and moves what's already mapped.

**Mapped again:** a message already mapped elsewhere moves to the new control (one hardware control, one target; ML6). Live lets one message drive several, but then one stray twist moves things you can't see.

## 4. How it looks

Drawn for approval before it's built, as the modulators' states were (modulators.md §23).

- **In map mode:**
  - every mappable control has a 1 px outline in a new `midiMap` role (a colour the modulation gradient and `active` don't use);
  - a mapped control shows its message in `Micro` text inside its top-right corner, where the automation indicator sits on the left ("CC 21", "C3", "PB");
  - the chosen control's outline is solid and thicker;
  - a control that can't be mapped (a display, a name) shows nothing.
- **Outside map mode:** nothing shows on a mapped control. Its tooltip adds "Mapped to CC 21 (Launchkey)", and its right-click has **Remove MIDI mapping** after Learn MIDI.
- **The layout lint** checks the label fits inside every control kind at every module's tightest size, as it checks the automation indicator.

## 5. Playing a mapping

- **Takeover** (ML4): when the hardware's position differs from the control's value, **Pick up** (by default) leaves the control where it is until the hardware passes its value, then follows. Two more choices for everything, in Settings > Controllers:
  - **Jump**: the control goes straight to the hardware;
  - **Scale**: the hardware's travel left is spread over the control's travel left, so they meet at the end.
- **Min and Max** (ML3): each mapping moves its control between its own Min and Max, the whole travel by default. A Max below the Min turns it the other way. They're set in the mapping's row. There's no curve: the control's own skew applies, as with the mouse.
- **Buttons** (ML5): a note, or a CC that only sends 0 and 127, is a button. Each mapping's row chooses:
  - **Toggle** (by default): each press flips a switch, or flips a knob between its Min and Max; on segmented buttons and dropdowns, each press steps to the next choice;
  - **Momentary**: on while held, as the modulators' Fire is.
- **Relative encoders** add their steps to the value. A step is the control's own small step (as the arrow keys use), larger the faster it turns.
- **Automation and undo** (ML9): a mapped control's moves are one gesture, from the first message until it rests for half a second. So it records into a lane while automation is armed, as the mouse does (automation.md §8), and each gesture is one undo step ("Freq").
- **Mapped messages are used up** (ML8): a mapped CC or note doesn't also reach the armed track's instrument, so a mod wheel mapped to a filter doesn't bend the synth too. Messages that aren't mapped play as now.
- **Sending values back** (ML10): for motorised faders and LED rings, Settings > Controllers has **Send values back** for each input whose device also has an output of the same name: off by default. Each mapped control's value goes back on its own message while it changes, from automation, the mouse or a preset.

## 6. The list of mappings

The project's mappings are listed **in the inspector while MIDI map mode is on** (ML7), in place of the selection's sections, as Live's Mapping Browser appears in its mode. Each row:

```
MIDI MAPPINGS · 6
CC 21  Launchkey    Filter · Freq            Min 20 Hz   Max 8.0 kHz      ×
CC 22  Launchkey    Filter · Res             Min 0 %     Max 100 %        ×
C1     Launchkey    Delay · Freeze           Toggle ⌄                     ×
CC 23  Launchkey    Track 2 · Volume         Relative (two's) ⌄  …        ×
```

- **The message and the device;** **the target** as automation names it ("Filter · Freq"; a click selects its module and shows it); **Min and Max** as fields in the control's own units; **Toggle or Momentary** for buttons; **the relative encoding** where it's relative; and Remove (×).
- **Removing** a mapping, and changing its Min, Max or mode, is one undo step each, as making one is.
- **Mappings whose target is gone** (its module deleted) go with it, and come back with its undo.

Settings > Controllers (push-2.md §10, after MIDI) holds what's about your setup, not the song: Takeover, Send values back per device, and the transport mappings (ML7).

## 7. What can be mapped

- **Every control automation can reach:** modules' and plugins' parameters, modulators' parameters, racks' macros and Morph, and the mixer's volume, pan and sends (automation.md §2).
- **Mute, solo and arm** of each track, as buttons. These aren't automatable, but they're what controllers' buttons are for.
- **The transport:** Play, Stop, Record, Loop, Metronome and Capture MIDI, as buttons, in map mode on the top bar. These mappings are kept with the app (Settings > Controllers), so a foot switch records in every project (ML7).
- **Not now:** any menu action (Settings > Keys keeps the computer keyboard's); a mapping that follows the selected module or track (§11).

## 8. Saved

- **With the project:** each mapping's input (by name, as recording's inputs are), its message (CC with channel, a 14-bit pair, a note, pitch bend), its relative encoding if any, its target (as automation keeps its lanes' targets), Min, Max and mode.
- **A project opened without its controller** keeps the mappings: they work again when the device is back. Settings > MIDI's inputs say which are present.
- **Presets** don't carry mappings (presets.md §11). **Rack files** don't either: a rack can go to another project and another controller.
- **Transport mappings** go in the app's settings (ADR 0045), under Controllers.

## 9. Assistants (MCP)

| Tool | Arguments | Does |
|---|---|---|
| `get_midi_mappings` | – | The project's mappings and the app's transport mappings: message, device, target, Min, Max, mode |
| `map_midi` | `target` (as automation names a parameter, or a track's mute, solo, arm, or a transport button), `message` (`cc` with `number`, or `note`, or `pitchBend`; `channel`; `device`), `min`, `max`, `mode` (`toggle`, `momentary`), `relative` | Maps it, one undo step. Assistants can't learn (there's no hand on the hardware); they map by message |
| `remove_midi_mapping` | `target` or `message` | Removes it, one undo step |

`get_project` lists the inputs mapped. An MCP test drives each tool.

## 10. Model and engine

- **The input:** a listener on each MIDI input, as the MIDI tap's (`MidiTap`, recording.md §8), on the MIDI thread, puts mapped messages on a lock-free queue and drops them from what reaches the tracks (ML8). The message thread drains the queue each frame (a 60 Hz timer), so a mapped knob moves its control through the same calls the mouse uses: `Session::ParamGesture` for parameters, the mixer's for volume and pan.
- **Learning** uses the same listener: while a control waits, the next message from any enabled input is sent to the message thread instead.
- **Mappings** are a list on the project's root (`fdMidiMap`), changed only through the Session, each change one undo step. The snapshot carries them for the UI (`model::MidiMapping`), and a pure `model::` function does the maths (takeover, Min and Max, relative steps, buttons), unit-tested `[fast]`.
- **Sending values back** is a lane-follower's job: each frame, mapped controls whose value changed send their message to the device's output.
- **Real-time:** the MIDI thread only reads a lock-free copy of which messages are mapped, published when the mappings change.

## 11. What comes when

| Part | Scope | When |
|---|---|---|
| **C8 MIDI learn** | §3–§10 | M3 |
| Follow the selection | Eight knobs on the selected module's controls (as the Push's banks), the selected track's volume, pan and sends | M4, with the Push 2's banks (push-2.md) |
| Map many at once | Learn a row of knobs in one go (Logic's "fill in between", FL's Multilink) | When asked |
| Menu actions | Any action from MIDI, beside Settings > Keys | When asked |
| NRPN, MPE | NRPN pairs; MPE controllers' per-note expression | With MPE (M5) |

## 12. Questions for review

| # | Question | Recommendation |
|---|---|---|
| ML1 | **Learning one control** | Its right-click: Learn MIDI. The control waits until a message comes, Escape gives up |
| ML2 | **Learning many** | Options > MIDI map mode: every mappable control outlined, click then move, stay until Done. No default shortcut (Ctrl+M is the metronome's) |
| ML3 | **Range** | Min and Max per mapping, a Max below the Min inverts. No curves: the control's own skew applies |
| ML4 | **Takeover** | Pick up by default; Jump and Scale as the other choices; one setting for all mappings (Settings > Controllers), as Live, Bitwig and FL have it |
| ML5 | **Buttons** | Toggle by default, Momentary per mapping. A note on a knob flips it between Min and Max; on a choice, steps through it |
| ML6 | **One message, one target** | Mapping a message again moves it. Several targets from one knob is a rack's macro's job |
| ML7 | **Where mappings live** | With the project, listed in the inspector during map mode (not in Settings, which the plan said: Settings is the app's, mappings are the song's). The transport's buttons with the app, in Settings > Controllers, with Takeover and Send values back |
| ML8 | **Mapped messages** | Used up: they don't also reach instruments |
| ML9 | **Automation and undo** | A mapped move is a gesture until it rests for half a second: it records automation when armed and is one undo step. Making, changing and removing a mapping are undo steps too |
| ML10 | **Sending values back** | Per device in Settings > Controllers, off by default |
| ML11 | **The transport** | Play, Stop, Record, Loop, Metronome and Capture MIDI mappable; other menu actions later |
| ML12 | **Following the selection** | Not now: with the Push 2's banks (M4). Mappings stay on their control |
| ML13 | **The look** | Map mode's outlines in a new `midiMap` role, the message in `Micro` in a mapped control's corner; drawn for approval first |
| ML14 | **Assistants** | `get_midi_mappings`, `map_midi`, `remove_midi_mapping`. They map by message, since they can't move hardware |

## 13. Built (2026-10-16)

**Where it lives:** `model::MidiMap` (the maths: takeover, Min and Max, buttons, relative encoders and their guess, what's sent back; `[fast]`), `engine::MidiMapper` (a listener on each MIDI input), the Session's mappings (`SessionMidiMap.cpp`), and in the shell `MidiMapOverlay`, `MidiMapBanner`, `MidiMappingList` (the inspector's list), Settings > Controllers and three assistant tools. A small tracktion patch (`tracktion_engine-midi-input-consumers.patch`) lets an input's listener use a message up, so a mapped message reaches no track (ML8); Capture MIDI leaves mapped messages out too.

**Settled while building:**
- **The look (ML13):** in map mode a mapped control is tinted (`midiMapFill`) with its message centred over it in `Micro`, rather than in its top-right corner: there the label covered a knob's name. Where the whole label doesn't fit (a track's M and S buttons) it shows only the number ("74"). The waiting control's outline pulses outside map mode; the chosen one's is thicker in it.
- **The window had no tooltips at all:** nothing created JUCE's tooltip window, so no tooltip anywhere in Segno had ever shown. The shell now has one (`Tooltips`), which also adds "Mapped to CC 21 (Launchkey)" to a mapped control's tooltip and says "Move a control on your MIDI device" on the one waiting.
- **Learning:** a controller whose first value is 127 is taken for a button (Toggle); a coarse controller (0–31) waits 30 ms for its fine half before it's taken for a 7-bit one. The learning message doesn't move the control.
- **The encoder guess** needs the same value twice in a row (an absolute knob never repeats itself) and only small steps; it changes the mapping's mode quietly, and the row shows it.
- **Undo (ML9):** two controls moved together share one step; any other edit (a click, a command) first ends the moves still open, so it takes a step of its own.
- **Mixer controls in map mode:** what shows can be mapped: mute, solo and arm in the headers, volume and pan where they show (taller headers, the mixer view). The inspector shows the list during the mode, so its MIX section is mapped by its right-click (Learn MIDI) outside the mode. A third-party plugin's parameters have no controls in the chain: assistants map them by name (`map_midi`).
- **Send values back** is offered for each input whose device has an output of the same name.
- **Assistants:** `map_midi` takes the mode a learned mapping would (a note toggles, the rest set) unless told; `remove_midi_mapping` takes a target or a message; `get_project` lists `midiInputsMapped`.

