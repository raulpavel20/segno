# The table editor (Wavetable, step 2)

The Wavetable's tables, made and changed in Segno: drawn, built from harmonics or a formula, taken from audio or an image, then shaped by a stack of changes. Step 2 of [devices/wavetable/SPEC.md](../../devices/wavetable/SPEC.md) (W8–W10), with the spectral warps and export. **Status:** approved by the product owner (2026-10-14, every recommendation of T1–T13); built 2026-10-14, revised after the first use (§11), signed off by the product owner (2026-10-14). What building settled is in the SPEC's "Settled while building (step 2)"; the editor's layout system in [device-panels.md](device-panels.md), "A module's editor".

## 1. What the leading synths do (research, October 2026)

| | Serum | Vital | Surge XT | Live's Wavetable |
|---|---|---|---|---|
| Where | A window over the synth | A window over the synth | A script editor | No editor: import only |
| Making frames | Draw, a formula per frame, harmonic bars (FFT) | Layered sources (drawn waves, lines, harmonics, audio, text), each on keyframes | Lua scripts generating the frames | – |
| Between frames | Crossfade, or spectral morph (a few frames grown to 256) | Interpolated between keyframes (none, linear, cubic) | Written by the script | – |
| Changing frames | A process menu: normalise, remove DC, and more | Modifiers on top: phase, window, filter, slew, fold, warp | In the script | – |
| From audio | Fixed frames, by pitch, by FFT | Pitch splice, vocode | – | Dropped as frames |

- Both editors that matter keep one idea: a few **keyframes** define the table; the rest is **filled in between**. Vital adds the one that made its editor loved: everything stays editable, as layers.
- Their weak points: a separate window covering the synth, and audio import whose result you only hear after committing.

## 2. Where it lives

- **The DAW:** in the bottom panel, where the piano roll opens ([ADR 0053](../decisions/0053-a-modules-editor-in-the-detail-panel.md)). While a table is open, the panel's tabs read **Modules · Clip · Table**; Tab goes round them. Its height is dragged at its top edge, like the clip view's (default 320 px, kept with the view). The module stays in view above it, so you play and hear it while you edit.
- **Opening it:** Module > Edit Wavetable, a double-click on the table display, or Edit in the table's menu. It edits the oscillator shown in OSC.
- **The plugins:** the window grows downward by the editor's height, under the panel, the same editor. Closing it shrinks the window back.
- **The × on its tab** closes it (§11, T14); the table it made stays on the oscillator.

## 3. What it edits: a recipe

A table is a **recipe**: layers that make frames, changes that shape them, and the table baked from them. The baked frames are what plays and what's saved beside the recipe; the recipe is what you edit.

- **Frames:** 1 to 256 (default 64), 2 048 samples each.
- **Layers** (up to 4), summed. Each is one source on **keyframes** across the table, and fills the frames between them by **crossfade** or **spectral morph** (magnitudes and phases interpolated harmonic by harmonic: no thinning in the middle of the morph).
- **Sources:**
  - **Draw:** a curve of points and bends (the LFO's editor, bigger), or freehand, snapping to a grid;
  - **Harmonics:** bars for the first 64 harmonics' levels and phases;
  - **Formula:** one line, in `x` (the phase, 0–1) and `t` (where in the table, 0–1): `sin(2*pi*x) + 0.5*t*sin(6*pi*x)`;
  - **Audio:** a file or a clip, in one of three ways (§5);
  - **Image:** each row a frame, its brightness the wave or the harmonics' levels;
  - **Table:** a factory table or one of yours, as a starting point.
- **Changes**, applied in order to every frame, each with an amount that can differ from keyframe to keyframe: Normalise, Remove DC, Phase Align (each frame's fundamental starts at zero, so morphs don't cancel), Smooth (a low pass on the harmonics), Fold, Filter (harmonic low, high or band pass), Reverse, Mirror, and the spectral warps of the oscillator (§6).
- **Editing a factory table** starts a recipe with that table as its layer; the oscillator then plays "Vowels (edited)", a table of yours carried in the project or preset.
- **Saving:** the recipe goes in the module's state beside the baked frames (`oscN.recipe`), so a project or preset reopens it editable.

## 4. The editor

*Revised after the first use: §11 (the result and the layer apart, the × on its tab). What follows is the first design, kept for the record.*

```
│ Modules   Clip   Table: Osc 1, Vowels (edited)                    ▶ Preview      Done │
├──────────────────┬───────────────────────────────────────────────┬────────────────────┤
│ LAYERS        +  │  3D   2D   Harmonics            frame 23 of 64 │ CHANGES          + │
│ ● Table: Vowels  │                                                │ Normalise          │
│ ● Draw           │        the view: the table in 3D, the          │ Phase Align        │
│                  │        selected frame in 2D (drawn on), or     │ Smooth   ▬▬▬  20 % │
│ DRAW             │        its harmonics as bars (set by drag)     │ Formant  ▬▬   +3 st│
│ Fill  Crossfade ⌄│                                                │                    │
│ Grid  16       ⌄ ├────────────────────────────────────────────────┤                    │
│ Level ▬▬▬▬ 80 %  │ ◆─────────◆───────────────────◆──────────◆    │                    │
│                  │ keyframes · Frames 64 ⌄                        │                    │
└──────────────────┴────────────────────────────────────────────────┴────────────────────┘
```

- **Left:** the layers (on/off dots, a click selects one, + adds one, the menu removes or reorders), then the selected layer's settings.
- **Middle:** the view, then the **keyframe strip**: the table's frames left to right, a ◆ for each keyframe of the selected layer. Click a frame to see it; double-click to add a keyframe there; drag one to move it; its menu removes it.
- **Right:** the changes stack, + to add, each a row of shared controls; a change's amount at a keyframe is set while that keyframe is selected.
- **Preview** plays a held note on the module through the edited table, before anything is committed (§5), and stops at a second click.
- Built from the shared kit: `PanelHeader` with tabs, sections in the detail panel's look, the module grid's controls, the curve editor the LFO uses. Its own layout lint runs as the panels' does.

## 5. Audio to wavetable (W10)

- **Three ways**, chosen when audio arrives (a file dropped on the editor or the table display, Import Wavetable…, or Make Wavetable from a clip), with a preview to hear each before choosing:
  - **Pitched:** the pitch is found (YIN), the audio cut into single cycles, N of them spread through the sound and phase-aligned. For notes, voices, instruments.
  - **Spectral:** the harmonics' levels sampled at the sound's pitch over time (an STFT at N points): the sound's colour without its noise or chorus. For pads, noisy and chorused sounds.
  - **Slices:** fixed windows, as they are. For anything; the classic "drop a loop" table.
- **Frames:** 8 to 256 (default 64). Where in the audio they come from: the whole file, or the time selected in the clip (*built:* the whole clip for now, the arrangement having no time selection yet, B81; assistants give `from` and `to`).
- **Make Wavetable** (Clip menu): the selected audio clip, or the time selected in it, opens in the editor on the selected Wavetable's selected oscillator. With no Wavetable selected, one is added on a new MIDI track named for the clip.

## 6. The spectral warps (W4)

Built in the oscillator's renderer, on the harmonics before the cycle is made, as the DSP design says: Formant Shift, Harmonic Stretch, Inharmonic Stretch, Smear, Spectral Low Pass and High Pass, Phase Disperse, Frame Skew, Random Harmonics, Shepard. The warp menus offer them from step 2; the same functions are the editor's spectral changes, so a warp heard live can be baked in.

## 7. Files and the browser (W8)

- **Export Wavetable…** writes WAV with a `clm ` chunk (the frame size) or `.wt`, into the Segno folder's **Wavetables** by default.
- The browser gets a **Wavetables** place (ADR 0046): your tables, found like samples, previewed on a held note; dragged onto a table display (or a Wavetable on a track), they load.
- **About 20 more factory tables** from AKWF's single cycles (CC0), built into morphing tables by recipes kept in the repository, fetched pinned like every dependency: the "about 80" of W8.

## 8. By mouse, keys and assistants

| Where | Item | Shortcut |
|---|---|---|
| Module menu | Edit wavetable | – (a double-click on the table display does it; Ctrl+Shift+E is Export) |
| Module menu | Import wavetable… | – |
| Module menu | Export wavetable… | – |
| Clip menu | Make wavetable | – |
| View menu | Switch modules / clip view / table (Tab), as now | Tab |

All dimmed unless a Wavetable (or, for Make Wavetable, an audio clip) is selected. Assistants (ADR 0021):
- `make_wavetable`: `module`, `oscillator`, a `clip` (with `from?`, `to?`) or a `path`, `mode` (pitched, spectral, slices), `frames?`;
- `get_wavetable`: an oscillator's table: its name, frames and recipe;
- `edit_wavetable`: an oscillator's recipe, replaced (checked whole first; one undo step);
- `export_wavetable`: `module`, `oscillator`, `path?`, `format?`.

## 9. Tests

- **Recipes:** a recipe bakes the same frames every time, on any machine; a factory table edited and saved reopens identical; each source and change against its definition (a formula's sine is a sine within −100 dB).
- **Audio:** a recorded saw or vowel within −60 dB of its harmonics, pitch found within 1 cent on test tones (the SPEC's targets); slices exact.
- **Spectral warps:** each at its extremes free of aliasing below −90 dB, as the cycle warps.
- **Files:** export and import round trip bit-exact.
- **UI:** the editor's layout lint at the narrowest and widest panel, snapshots reviewed; every action driven through MCP.

## 10. Questions for review

| # | Question | Recommendation |
|---|---|---|
| T1 | **Where the editor opens** | In the bottom panel as a third view, Modules · Clip · Table, the module in view above it (ADR 0053). In the plugins, the window grows under the panel |
| T2 | **What a table is** | A recipe (layers on keyframes, a stack of changes) and the frames baked from it, both saved, so it reopens editable |
| T3 | **Layers** | Up to 4, summed, each one source on keyframes, filled by crossfade or spectral morph |
| T4 | **Sources** | Draw, Harmonics, Formula, Audio, Image, Table. No text-to-wavetable (a novelty) |
| T5 | **Changes** | Normalise, Remove DC, Phase Align, Smooth, Fold, Filter, Reverse, Mirror and the spectral warps, amounts per keyframe |
| T6 | **Audio to wavetable** | Pitched, Spectral and Slices, each previewed before choosing; Make Wavetable from a clip or the time selected in it |
| T7 | **Editing a factory table** | Becomes a table of yours, "(edited)", carried with the project; the factory one is never changed |
| T8 | **Preview** | A held note on the module through the edited table, before committing audio or a big change |
| T9 | **The spectral warps** | Built now, in the oscillator and as changes in the editor, one function for both |
| T10 | **Files and the browser** | Export WAV (`clm `) and `.wt` into Segno/Wavetables; a Wavetables place in the browser, dragged onto a table display to load |
| T11 | **More factory tables** | About 20 from AKWF single cycles (CC0), by recipes in the repository, fetched pinned |
| T12 | **Menus and keys** | Module > Edit Wavetable, Import…, Export…; Clip > Make Wavetable. No shortcut of its own: a double-click on the table display opens it |
| T13 | **Assistants** | `make_wavetable`, `get_wavetable`, `edit_wavetable`, `export_wavetable` |

## 11. Revised after the first use (2026-10-14)

**Status:** approved by the product owner (2026-10-14, every recommendation of T14–T20); built the same day and signed off (2026-10-14).

**Settled while building:**
- **The Result is 4 columns, not 5,** and the layer's view at least 5, so the editor's least width is about 964 px: it fits the DAW's narrowest window (1 024 px, less the panel's margins).
- **The layer's title** reads "LAYER 2 · DRAW"; at its right, where the frame shown is: "keyframe at 23", "between 12 and 40", "held from 12" (before the first keyframe or after the last), "64 frames, filled", "made at each frame". Each well names itself and whether it's edited there ("Wave: draw here", "Harmonics: set here"); its hover says where the layer is set otherwise.
- **Add keyframe** (2 columns) and **Remove keyframe** (3 columns) under the strip, dimmed where they can't act; they show for drawings and harmonics only.
- **A new drawing keyframe between two** takes their points crossfaded when they're at the same places and the fill crossfades, else the frame there as 33 straight points: what you see doesn't jump when the first stroke adds it.
- **The Result** is picked by a click on a frame line, or dragged up and down; it bakes again every 120 ms at most while a drag edits (the layer follows at once): a bake takes about 100 ms at 64 frames in a debug build.
- **Phases** show only for harmonics at 1 % or more.
- **The table menus** (the oscillator's table field, a Table layer's, + > Table) list the nine categories as submenus.
- **The browser:** the factory tables are files in the Library's Wavetables, a folder a category ([ADR 0055](../decisions/0055-factory-content-is-files-in-a-read-only-library.md); the Tables folder in the Wavetable's module folder is gone). One is heard on a held C3 when selected, found by search among WAVETABLES, and put in like a file of yours: on a track a Wavetable with it, on a Wavetable where its OSC shows. Each file carries its factory ID, so loading it sets that table, as the table menu does.

**What the product owner hit, trying it:**
- **How to close it couldn't be found.** Done sat at the far right of the panel's header, away from the tab and the editor.
- **What the middle view showed wasn't clear.** Most of the time it showed the sum of the layers; it switched to the selected layer only in one view for each kind (2D for a drawing, Harmonics for harmonics), and only on a keyframe. Nothing said which it was showing.
- **The 3D, 2D and Harmonics tabs meant different things** depending on the layer and the frame.
- **The 20 AKWF tables couldn't be found:** they were only in the table's menu, at the ends of long categories, and no preset used them. (Fixed already: a factory preset for each.)

**How Phase Plant does it** ([its manual](https://kilohearts.com/docs/wavetables)): "a miniature version of the whole wavetable" at the top to move between frames; under it the selected frame's waveform and its spectrum, **both shown at once**. A keyframe is placed by clicking a frame and starting to edit; edits are heard as you play.

**The principle:**
- The result and what you edit are **two places, both always shown, each named**.
- Nothing switches what a place shows behind your back.
- You can edit wherever you are: editing a frame that has no keyframe adds one there.

```
│ Modules   Clip   Table ×   Osc 1, Vowels (edited)                                      ▶ Preview │
├────────────────┬───────────────────────────────────────────────┬───────────────────┬───────────────┤
│ LAYERS      +  │ LAYER 2 · DRAW                keyframe at 23  │ RESULT      23/64 │ CHANGES     + │
│ ● Vowels       │ ┌─ its wave: draw here ──────┐┌─ its harmonics ┐ │ ┌─ the table ──┐ │ ● Smooth 20 % │
│ ● Draw         │ │                            ││                │ │ │ every layer, │ │ ● Formant     │
│                │ │                            ││                │ │ │ then the     │ │               │
│ DRAW           │ └────────────────────────────┘└────────────────┘ │ │ changes, 3D  │ │               │
│ Level ▬▬ 80 %  │ ◆────────────◆────────────────────◆─────────── │ └──────────────┘ │               │
│ Fill Crossfade │ Add keyframe                                   │ Frames 64        │               │
└────────────────┴───────────────────────────────────────────────┴───────────────────┴───────────────┘
```

| # | Question | Recommendation |
|---|---|---|
| T14 | **How it closes** | An **×** on the Table tab, as tabs close elsewhere; in the plugins, the editor's header shows the same tab with its ×. Module > Edit wavetable is ticked while it's open, and choosing it again closes it. Done goes: one way in the header, where you look for it |
| T15 | **The result and the layer** | Two places side by side, left to right as the sound goes: the selected layer, then the **Result**: the whole table as the module plays it (every layer summed, then the changes), in 3D, the frame shown lit, a click or drag choosing the frame. Frames (the table's count) sits under the Result |
| T16 | **What the layer shows** | Its own frame at the frame shown, before the changes: its **wave and its harmonics side by side, always both**. The 3D, 2D and Harmonics tabs go. The place's title names the layer ("LAYER 2 · DRAW"), and on the right says where you are: "keyframe at 23", "between keyframes 12 and 40", or "its 16 frames, filled" for audio, an image or a table |
| T17 | **Where you edit** | Where the layer is made: a drawing on its wave, harmonics on their bars (the phases in a strip under them). Formula, audio, image and table layers are set on the left; their wave and harmonics are there to look at. The side that doesn't edit says so on hover |
| T18 | **Editing between keyframes** | Allowed: the first stroke there adds a keyframe at that frame, made of what the layer has there, and draws on it: one undo step, "Draw a wave". **Add keyframe** and **Remove keyframe** buttons sit under the strip, so neither needs a double-click or a right-click |
| T19 | **Changes** | They shape the Result, which shows them; the layer shows what goes in. The Changes title stays; its empty text says "Each shapes the result, in order from the top" |
| T20 | **Finding the tables** | The table's menu lists its categories as **submenus** (nine at a glance, Acoustic among them), not one list of 84. The browser's Wavetable folder (ADR 0046) gets a **Tables** folder of the factory tables by category, each heard on a held note when selected and loaded when dropped, as your own tables are. *Replaced by [ADR 0055](../decisions/0055-factory-content-is-files-in-a-read-only-library.md): the tables are files in the Library's Wavetables* |

**Height:** the same ten rows as now (about 316 px). The layer's wave and harmonics get the view's rows less two (the strip and its buttons); the Result its rows less one (Frames). **Width:** the Result is 5 columns, so the editor's least width grows from about 790 px to about 1 070 px, still inside the narrowest window the app and the plugins open.
