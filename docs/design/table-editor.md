# The table editor (Wavetable, step 2)

The Wavetable's tables, made and changed in Segno: drawn, built from harmonics or a formula, taken from audio or an image, then shaped by a stack of changes. Step 2 of [devices/wavetable/SPEC.md](../../devices/wavetable/SPEC.md) (W8–W10), with the spectral warps and export. **Status:** approved by the product owner (2026-10-14, every recommendation of T1–T13); being built.

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
- **The plugins:** the window grows downward by the editor's height, under the panel, the same editor. Done shrinks it back.
- **Done** (in its header) closes it; the table it made stays on the oscillator.

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
- **Frames:** 8 to 256 (default 64). Where in the audio they come from: the whole file, or the time selected in the clip.
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
| Module menu | Edit Wavetable | – (a double-click on the table display does it; Ctrl+Shift+E is Export) |
| Module menu | Import Wavetable… | – |
| Module menu | Export Wavetable… | – |
| Clip menu | Make Wavetable | – |
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
