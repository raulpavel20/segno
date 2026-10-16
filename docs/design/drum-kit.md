# Drum Kit: one instrument, a pad for each sound

The **Drum Kit** is an instrument with a pad for each note. Each pad holds a sound: a sample, played by the Sampler, or any other instrument, with effects after it if you want them. Drop a kick on a pad and the pad plays it. Drop a synth on the next one and that pad plays the synth. Hats choke each other. A whole kit is saved as one of the Drum Kit's presets.

**Status:** approved by the product owner (2026-10-16), every recommendation of K1–K18, with [ADR 0064](../decisions/0064-a-drum-kit-whose-pads-hold-any-instrument.md) accepted. It replaces M4's Drum Sampler and Drum Rack, and moves up to M3 as P7 at the product owner's word to start it. Step 0 done the same day: the spike passed (§20), the UI drawn in every state and approved (§21). Step 1, the kit, built and signed off by the product owner the same day (§22). Next: step 2 (macros on kits), the piano roll's drum view (asked for at sign-off, §23), and last the factory kits. The factory kits come last, from CC0 sets the product owner and Claude search for together (§17, step 3).

What the product owner asked (2026-10-16):
- A Drum Sampler module and a Drum Rack feature were "such a convoluted, roundabout way" of doing it. The Sampler already plays a single sample, and the rack "reads like something distinct from the collection of modules that the Rack feature is supposed to be".
- "A simple Drum kit instrument in the instrument list" that "presents a matrix of pads (perhaps even multiple pages of matrixes of pads like Live does)", presented as one instrument, not as two features with one using the other.
- Then: "have it like a nested instrument, so we fully reuse the sampler and any other instrument". When you open it you get the pads, "every pad comes preloaded with the sampler we already have, but you have the option to swap it out for any other synth".

## 1. What's there now

- **The Sampler** has had a **One-shot** mode since M1: a hit plays to its end, and note-off is ignored unless Gate is on. It also has:
  - a filter with its own envelope, an amp envelope, and velocity to volume;
  - since M3, multisample zones and SFZ files, so one pad can have velocity layers and round robins;
  - slicing, due in M4 ([SPEC](../../devices/sampler/SPEC.md)).
- **Racks** (M3, [racks.md](racks.md), [ADR 0056](../decisions/0056-racks-are-modules-of-modules.md)) have:
  - chains side by side, each with a mixer row (on, solo, volume, pan); every chain hears every note;
  - eight macros, files, undo and MCP tools;
  - modules that keep their ids inside a rack.

  They don't nest: tracktion refuses a rack in a rack.
- **Modulators** move any parameter on their track, including those of modules inside racks ([ADR 0061](../decisions/0061-modulators-move-any-parameter-on-their-track.md)).
- **The plan until now** ([device-suite.md](../devices/device-suite.md), the roadmap's M4):
  - a Drum Sampler module: one sample an instance, with built-in shaping, "designed to sit on drum pads";
  - a Drum Rack: a DAW rack whose pads each hold a chain, with choke groups.

  Both copied Live, by ADR 0011's "match Live by default". The Drum Synth (M5) is one synthesised voice an instance, made for a pad.
- **Waiting on it:**
  - Push 2's drum layout and step sequencing ([push-2.md](push-2.md) step 3);
  - the piano roll's drum view ([piano-roll.md](piano-roll.md) §16);
  - factory kits ([factory-library.md](factory-library.md), [presets.md](presets.md) P11).

## 2. How others do it (October 2026)

| | Pads | What a pad holds | A sample dropped | Choke | A kit saved as |
|---|---|---|---|---|---|
| **Live 12** (Drum Rack) | 128, one a note; 16 shown, moved over a pad overview | A chain of any devices; Receive and Play notes; up to six return chains in the rack | A chain with a Simpler; a second sample replaces the Simpler and keeps the effects. A pad's "Save as Default Pad" changes what's made (the Drum Sampler of 12.1, for one) | 16 groups | A Drum Rack preset; Save as Default Preset |
| **Bitwig 6** (Drum Machine, an instrument in the Drum Kit category) | 128, one a note; 16 shown, scrolled | A chain of any devices; the selected one gets a strip with volume, pan and meters | Sampler options ("Insert layered Sampler"); Alt-dragging several files onto a track makes a Drum Machine filled from C1 | No groups: each chain names its "Choke targets" or what it's "Choked by" | A device preset |
| **Logic Pro** (Drum Machine Designer, "not a plug-in" but a track stack) | 48: three pages of 16; each pad an input and an output note | Any instrument or plug-in, on a channel strip of its own | A Quick Sampler | 8 "Exclusive Groups" | A kit patch; each pad a "kit piece patch". A new DMD loads a kit unless "Use Empty Kit as Default" |
| **Maschine** | 16 a Group | A Sound: an instrument, then effects | – | 8 groups, each Sound Send or Receive | A Group |
| **FPC** (FL Studio) | 2 banks of 16 | Layered samples only | – | Cut / Cut by | A preset; each pad to a mixer track |
| **Impact** (Studio Pro 8.1) | 8 banks of 16 | Samples only | – | Self, or 1–32 | A preset; 16 stereo and 16 mono outputs |
| **Kong** (Reason 14) | 16 | One of 9 built-in drum modules, 2 effects | – | Mute, Link and Alt groups | A patch; 7 more output pairs |

- **Empty pads stay empty** in Live, Bitwig, Logic and Maschine until something is dropped. Only the sample-only kits (FPC, Impact) have a sampler on every pad, because their pads are sample slots.
- **Clicking a pad:** Live and Bitwig have a preview button on each pad, apart from selecting it. FPC, Impact and Maschine play a pad when it's clicked.
- **Slicing:** Live's Slice to New MIDI Track and Bitwig's Slice to Drum Machine each put every slice in its own Sampler on a pad.
- **The drum editor:** Bitwig's shows the notes of the pads that have sounds, when the instrument is a Drum Machine.
- **Push 2:** a pad lit in the track's colour has a sound, and a lighter one is empty. A tap selects a pad for step sequencing.

Sources: [Live: Drum Racks](https://www.ableton.com/en/manual/instrument-drum-and-effect-racks/), [Live: presets and defaults](https://www.ableton.com/en/manual/working-with-instruments-and-effects/), [Live: Drum Sampler](https://www.ableton.com/en/manual/live-instrument-reference/), [Live: slicing](https://www.ableton.com/en/manual/converting-audio-to-midi/), [Live: Push 2](https://www.ableton.com/en/manual/using-push-2/), [Bitwig: device concepts](https://www.bitwig.com/userguide/latest/advanced_device_concepts), [Bitwig 6.1 release notes](https://downloads.bitwig.com/6.1/Release-Notes-6.1.html), [Bitwig: note editing](https://www.bitwig.com/userguide/latest/working_with_note_events), [Logic: Drum Machine Designer](https://support.apple.com/guide/logicpro/drum-machine-designer-overview-lgsi7e4c8c73/mac), [Logic: pad controls](https://support.apple.com/guide/logicpro/use-pad-controls-lgcpdcdd1d7a/mac), [Logic: kit controls](https://support.apple.com/guide/logicpro/kit-controls-lgcp259f74aa/mac), [Maschine manual](https://docs.native-instruments.com/online-guides/maschine-software-manual/en/managing-sounds,-groups,-and-your-project), [FPC](https://www.image-line.com/fl-studio-learning/fl-studio-online-manual/html/plugins/FPC.htm), [Impact](https://fenderstudiopromanual.fender.com/en/Content/Built-In_Instruments_Topics/Impact.htm), [Kong](https://docs.reasonstudios.com/reason14/kong-drum-designer).

**What we take:**
- **Bitwig's framing:** one instrument in the instrument list, whose pads hold any device chain. Live's Drum Rack does the same job, but presents it as a rack.
- **Live's 128 pads and overview,** 16 shown, rather than Logic's 48 or fixed banks.
- **Live's 16 numbered choke groups.** They're easier to read than Bitwig's per-pad targets, and enough for any kit.
- **Logic's single note per pad** (its output note): our Plays. Live's Receive set to "All Notes" turns a chain into a keyboard range, which a track of its own does better (§16).
- **Bitwig's several samples making a kit from C1** (§11).
- **Empty pads, as all four containers have them.** Live's "Default Pad" and Logic's default kit are covered by a default kit of your own (§9).

**Beyond them:**
- A sample dropped on a Sampler pad goes into that Sampler and keeps the pad's settings, where Live replaces the Simpler. This means kicks can be auditioned with the pad's tuning and envelope in place.
- Kits are ordinary presets, with A/B.

## 3. Principles

1. **One instrument.** It's found, added, saved and talked about as one instrument. Its words are **kit**, **pad**, **choke group** and **Plays**, everywhere: panel, menus, tooltips, MCP and docs. "Rack" and "chain" never appear, though racks are what it's built on.
2. **Nothing built twice.** Samples are played by the Sampler, and the machinery is the racks'. What a drum pad needs and the Sampler lacks is added to the Sampler, so every Sampler gains it.
3. **A pad is empty until it has a sound.** An empty pad looks empty and costs nothing. Nothing is loaded that you didn't put there, except your own default kit (§9).
4. **Nothing confusing.** As for racks (racks.md §5a), the UI is drawn in every state and approved before it's built, and tried hands-on before sign-off.

## 4. What a Drum Kit is

- **An instrument with 128 pads,** one for each MIDI note. Each pad hears its own note and nothing else.
- **A pad is empty, or holds an instrument and the effects after it**: its **modules**. Any instrument (ours or a plugin), then any effects.
- **On a track it's the track's instrument.** Put where there is one, it replaces it, as any instrument does ([browser.md](browser.md) §11).
- **Not in a rack, and no rack or kit on a pad.** The engine doesn't nest racks (§15).
- **Only in the DAW,** as racks and modulators. The plugins stay one module each. In another DAW, Segno's drums are the Sampler and, from M5, the Drum Synth, each on that DAW's own pads.

## 5. The pads

```
 PADS                                          ····
 ┌─────────┐┌─────────┐┌─────────┐┌─────────┐  ····
 │ Tom hi  ││ Crash   ││         ││ Ride    │  ┌────┐
 │ C2      ││ C#2     ││ D2      ││ D#2     │  │▪▪·▪│  the overview: all 128 pads,
 ├─────────┤├─────────┤├─────────┤├─────────┤  │·▪▪▪│  4 a row, the lowest at the bottom;
 │         ││ Tom low ││ Hat open││ Tom mid │  │▪·▪·│  lit where a pad has a sound;
 │ G#1     ││ A1      ││ A#1   1 ││ B1      │  │▪▪▪▪│  the 16 shown are framed
 ├─────────┤├─────────┤├─────────┤├─────────┤  └────┘
 │ Snare 2 ││         ││ Hat cl. ││         │  ····
 │ E1      ││ F1      ││ F#1   1 ││ G1      │
 ├─────────┤├─────────┤├─────────┤├─────────┤
 │█Kick 1 █││ Rim     ││ Snare   ││ Clap    │   █ the selected pad
 │█C1     █││ C#1     ││ D1      ││ D#1     │   1 its choke group
 └─────────┘└─────────┘└─────────┘└─────────┘
```

*(A sketch of what a pad shows, not its sizes: §8 places it on the module grid.)*

- **16 pads at a time, 4 × 4, the lowest at the bottom left,** as on Push and hardware pads. A new kit shows C1 to D#2 (notes 36–51), where the General MIDI drum notes start, and where Push's drum layout starts too ([push-2.md](push-2.md) §7.1).
- **The overview,** beside the pads, shows all 128 pads in 32 rows of 4:
  - a pad with a sound is lit, an empty one isn't;
  - the 16 shown are framed;
  - a click or a drag on it moves the frame, a row of 4 at a time;
  - the mouse wheel over the pads does the same.

  This is "pages of pads", without pages that split a kit at the wrong note.
- **A pad shows:**
  - its **name**: the sample's file name, else the instrument's preset or module name. A double-click renames it, as names are renamed everywhere;
  - its **note** ("C1"), smaller;
  - its **choke group**, when it has one, as a small number in its corner.

  When it holds a sound it's drawn raised. An empty pad shows only its note, faint. A pad that's off is dimmed; a soloed pad shows Solo lit. A pad lights for a moment when it plays, from a click, a key or the song.
- **The selected pad** is outlined. Its settings show in PAD (§8) and its modules in the frame after the kit's panel. Which pad is selected is the kit's view state, as a rack's shown chain is, and is never an undo step.
- **A click on a pad selects it and plays it** (K8). The note sounds while the button is held, at velocity 100. That's what pads are for. Select pad, on the pad's right-click, selects it without playing it.
- **Dragging a pad onto another moves its sound there.** If the other pad has a sound, the two swap. Ctrl+drag copies. Either is one undo step.
- **Right-click on a pad:** Select pad, Rename pad…, Load sample…, Copy pad, Paste pad, Clear pad, On, Solo, and Choke group ▸ None, 1 … 16.

## 6. A pad's sound

**Putting sounds on pads** (one undo step each, named for what it did: "Load Kick 1 on C1"):

| What | Dropped on a pad | Dropped on the kit's panel, off the pads |
|---|---|---|
| **A sample** | A Sampler in One-shot mode with it. On a pad whose instrument is already a Sampler, the sample goes into that Sampler and its settings stay, so kicks can be auditioned one after another with the pad's tuning and envelope kept | On the selected pad, as on the left |
| **Several samples** | One a pad, from that pad up, in the order they were dragged; pads in the way are replaced | From the selected pad up |
| **A multisample (SFZ)** | A Sampler with its zones. Velocity layers and round robins on one pad come from here | On the selected pad |
| **An instrument, an instrument preset or a plugin** | Replaces the pad's instrument, keeping its effects ("Replaced Sampler with Subtractive on D1", with Undo in the toast) | On the selected pad |
| **An effect or effect preset** | After the pad's last module | After the selected pad's last module. In the frame, it goes where it's dropped among the pad's modules |
| **A rack or a Drum Kit** | Refused: the drop marker doesn't show, and the tooltip says "A pad can't hold a rack" | Loading a Drum Kit preset on a kit loads it into that kit, as a preset dropped on its module does |

- **An empty pad selected** shows "Drop a sample or an instrument here" in the frame, and the frame's "+" adds a module, as an empty rack's does.
- **Module > Load sample…** and the pad's right-click open a file chooser. The sample goes onto the selected pad as a drop does.
- **Plays:** the note a pad sends its instrument, whatever note it's hit with. The default is C3 (60).
  - **A Sampler made by a drop** gets its Root Key set to the pad's Plays note, so the sample plays at its own pitch, whatever its file says.
  - **Plays tunes a synth on a pad:** a Subtractive kick on D1 that Plays G1 sounds at G1. Changing a Sampler pad's Plays transposes it, as playing another key would.
  - A synth that's played across the keyboard belongs on a track of its own, not on a pad.
- **What reaches a pad:** its note's note-on and note-off with their velocities, and polyphonic pressure on its note (Push's pads), all rewritten to its Plays note. Pitch bend, channel pressure and controllers reach every pad. Notes with no pad sound make no sound.
- **The Sampler on a pad is the Sampler,** with its whole panel. One-shot mode dims what it ignores. A compact view for pads is open question B12, for after real use (K12).
- **Retriggering a pad** is up to its instrument. The Sampler fades a retriggered voice over 1.5 ms. A cymbal that should ring under its next hit uses Poly voices, as now.

## 7. Choke groups

- **Each pad has a choke group:** None (the default) or 1 to 16.
- **When a pad in a group is hit, every other pad in the group that's sounding stops.** The classic case is the closed hat cutting the open hat off. A pad never chokes itself: retriggering is up to its instrument.
- **How a pad stops:** its instrument gets **All sound off**, a new event (§15).
  - Ours stop within the fade a stolen voice gets (1.5 ms in the Sampler), so a choke never clicks.
  - Plugins get MIDI CC 120 (All Sound Off), which they stop on however they do.
  - Effects after the instrument keep going, so a reverb's tail rings on, as it does in Live.
- **Set in PAD** (§8) and on the pad's right-click. A pad in a group shows the group's number in its corner (§5), so a kit's hats are seen to be grouped at a glance.

## 8. In the chain: the kit's panel

```
 ┌ ‹ ● Drum Kit ──────── ‹  808 Kit        ▾  ›  [A] ┐ ┌ SAMPLER ──────────┐ ┌ COMPRESSOR ──┐  + │
 │ PADS                                 │ ● PAD · C1   │ │                   │ │              │    │
 │ ┌──────┐┌──────┐┌──────┐┌──────┐ ┌──┐│ [Vol  0.0 dB]│ │   …               │ │   …          │    │
 │ │Crash ││      ││Ride  ││      │ │▪▪││ [Pan       C]│ │                   │ │              │    │
 │ │C♯2   ││D2    ││D♯2   ││      │ │▫▪││ [Plays    C3]│ │                   │ │              │    │
 │ └──────┘└──────┘└──────┘└──────┘ │▪▪││ [Choke None▾]│ │                   │ │              │    │
 │   …  (4 × 4, the lowest at the   └──┘│ [Solo]       │ │                   │ │              │    │
 │       bottom left)                   │              │ │                   │ │              │    │
 └──────────────────────────────────────┴──────────────┘ └───────────────────┘ └──────────────┘    │
  └──────────────── PAD C1 · KICK 1 (the frame, under the selected pad's modules) ─────────────────┘
```

- **The kit's panel,** on the module grid like any module's ([device-panels.md](device-panels.md)):
  - **PADS** [7]: the 4 × 4 pads across 6 columns and the content's 6 rows, then the overview in 1 column. A pad is 74 × 37 px, room for a name of about ten characters, cut with an ellipsis after that, its whole name in the tooltip.
  - **PAD** [2]: the selected pad's settings, one a row: Vol, Pan and Plays as bars (as a chain's Vol and Pan, and the Sampler's Root Key), Choke as a combo with its name inside, Solo as a toggle. The pad's on is the section's power dot, and the title names the pad ("PAD · C1"). An empty pad has no settings: they show dimmed and can't be changed (settled while drawing, §21).
  - **MACROS** [4], from the first mapping (§10).
- **The selected pad's modules follow the panel in the frame** that holds a rack's shown chain (racks.md §5), so modules inside a module look the same wherever they are. The frame's label always names the pad, even with one pad: "PAD C1 · KICK 1", with "· OFF" and "SOLO" as a chain's does. The "+" at its end adds a module to the pad.
- **The header** is a module's: the name Drum Kit, then the preset group with the kit's name (§9).
- **Folded,** the kit is one strip that says what it holds: "Drum Kit · 12 pads".
- **Its width** is 508 px before its pad's modules (PADS 384, PAD 124), and 736 px with MACROS.
- **Every edit is one undo step,** as every chain edit is.

## 9. Kits: the Drum Kit's presets

- **A kit is a preset of the Drum Kit:** a `.segnopreset` file, device `drum-kit` ([ADR 0044](../decisions/0044-presets-are-files.md)). Modulators, also only in the DAW, keep their presets the same way.
  - **What it holds:** each pad with a sound (its note, name, Plays, choke group, volume, pan and on), its modules' states (ours as their presets hold them, plugins as their own state, as a rack file holds them), and the macros with their mappings.
  - **What it doesn't:** automation, routing, solo and the view, as for any preset.
- **Where they are:**
  - yours in `Documents/Segno/Presets/Drum Kit/`;
  - factory kits built into the DAW (`app/presets/drum-kit/`, as the modulators' are), playing samples from the Library by `library:` paths ([ADR 0055](../decisions/0055-factory-content-is-files-in-a-read-only-library.md)). They're made last (§17, step 3).
- **In the browser,** Instruments ▸ Drum Kit opens to Yours, then the factory kits in folders by their first tag (Acoustic, Electronic, Percussion…), as any instrument's presets are ([browser.md](browser.md) §5). Selected, with Preview on, a kit plays a one-bar beat through itself, rendered offline: kick, snare and hats on C1, D1, F#1 and A#1. A kit with a plugin on a pad isn't previewed, as for racks, and its tooltip says why.
- **In the header,** the preset group works as on any module: the kit's name, previous and next, the changed mark, Save preset, and **A/B**, which keeps two whole kits to switch between.
- **"Use as the default for a new Drum Kit"** makes your own starting kit, every pad loaded as you like it. This is how a new kit comes preloaded, if you want it to (K7). Without one, a new kit is empty.
- **Missing pieces:** a kit with a sample that isn't there loads the rest, and the Sampler on that pad says what's missing (as now). A plugin that isn't installed is named, as in a rack.

## 10. Macros

- **The kit has a rack's eight macros,** mapped the same ways: drag a macro's name onto a knob, right-click a knob and choose Map to macro, or use the macro editor (racks.md §4, §6). One knob can tune every tom or shorten every decay.
- **MACROS shows from the first mapping,** as CHAINS shows from a rack's second chain, so a kit without macros spends no width on them. A knob's right-click (Map to macro ▸ Macro 1 … 8) and Module > Edit macros are always there.
- **Snapshots and Morph** stay with racks for now (§16).

## 11. Elsewhere in Segno

- **The browser:**
  - several samples dropped in a track's module chain make a Drum Kit with them, from C1 up, in one undo step. Where the track has an instrument, the kit replaces it, as any instrument does;
  - one sample dropped there makes a Sampler, as now;
  - samples dropped on a track's lanes make audio clips, as now.
- **The piano roll's drum view** (M4, [piano-roll.md](piano-roll.md) §16) names its rows from the kit's pads, and can show only the pads with sounds. That design is the piano roll's. The kit gives it the names (`get_kit` and the snapshot).
- **Push 2** (proposed, [push-2.md](push-2.md) step 3): the drum layout's pads light where the kit's pads have sounds, in the track's colour, and the display names the selected pad. Step sequencing writes the selected pad's note.
- **Slicing** (the Sampler's M4 stage): "Slice to Drum Kit" makes a kit with a Sampler on each pad from C1, each playing one slice, and a MIDI clip that plays them in order. Its design is the Sampler's slicing stage.
- **The mixer** sees one channel, the kit's track. Pads as channels of their own come later (§16).
- **Automation and modulators** reach a pad's volume, pan and on, and every parameter of its modules, as they reach a rack's.
- **Bounce and render** treat the kit as any instrument.

## 12. Menu bar and keys ([ADR 0040](../decisions/0040-a-menu-bar-and-everything-by-mouse.md))

The **Module** menu, a group after the racks' items, dimmed unless a Drum Kit is selected. The commands act on the kit's selected pad.

| Item | Key | |
|---|---|---|
| Load sample… | | *Existing* (`module.loadSample`): on a kit, onto the selected pad, making a Sampler if it has none |
| Rename pad… | | *New* (`module.renamePad`): in place, on the pad; also a double-click on its name |
| Copy pad / Paste pad | | *New* (`module.copyPad`, `module.pastePad`): the pad's sound and settings, pasted onto the selected pad; also the pad's right-click. Ctrl+drag copies too |
| Clear pad | | *New* (`module.clearPad`): removes its modules and settings, one undo step |
| Edit macros | | *Existing*: the kit's macros, as a rack's |

- **On, Solo and the choke group** are the pad's settings, in PAD and on the pad's right-click, as a chain's are on its row.
- **No shortcuts at first:** the pads are on screen, and shortcuts can be added in Settings.
- **Add module ▸ Drum Kit** and the browser add an empty kit, or your default (§9).

## 13. Assistants (MCP, [ADR 0021](../decisions/0021-mcp-server.md))

| Tool | What it does |
|---|---|
| `get_kit` | Its pads with sounds: note, name, Plays, choke group, on, solo, volume, pan, and their modules (type, id, name); the selected pad; the macros as `get_rack` gives them |
| `set_pad` | A pad's `name`, `plays`, `choke`, `on`, `solo`, `volume`, `pan`, or `selected`. A pad is named by its note, as a number or a name ("C1") |
| `load_pads` | `kit`, `pad`, `paths`: samples (or SFZ files) onto pads from `pad` up, each making a Sampler or going into the pad's Sampler, as a drop does; one undo step |
| `move_pad` | `kit`, `from`, `to`, `copy?`: as a drag; swaps when `to` has a sound |
| `clear_pad` | `kit`, `pad` |

- **Modules on pads keep their ids,** so `set_parameters`, `get_module` and the others reach them as before. `add_module` and `move_module` take `kit` and `pad` as the place, as they take `rack` and `chain`. `add_module` with type `drum-kit` adds a kit.
- **Kits are presets:** `list_presets`, `load_preset` and `save_preset` work on a kit, and `add_module` takes a `preset`. `search` finds kits as presets of the Drum Kit.
- **Macros:** `map_macro`, `unmap_macro` and `set_macro` take a kit as they take a rack.
- **Playing a pad** is playing its note: an assistant writes notes in a clip, as for any instrument.

## 14. Model and views

- **The snapshot** (`model::DeviceSlot`): a kit is a rack slot of type `drum-kit`. Its chains are its pads, each with its note, name, Plays and choke group beside the mixer row and module slots that chains already have. It also holds the selected pad. Panels, automation and MCP see a pad's modules as they see a chain's.
- **The DeviceChain view** lays out the kit's panel, then the selected pad's modules in the rack's frame. The kit's panel is our own panel kit's (`DrumKitPanel`, in `app/src/ui`), on the module grid, checked by the layout lint.
- **The pads** are one new shared component (`ui::PadGrid`, with the overview), drawn from colour roles, text styles and metrics only, so Push 2's display and the piano roll's drum view can use the same look. What it needs that the kit doesn't have yet (a pad's surfaces, its playing flash) is added to `fd_ui` with its docs and tests (ui-foundations.md).

## 15. Engine (tracktion)

- **A kit is a tracktion rack** (`RackType`), placed as a `RackInstance` in the track's plugin list, as a rack is (racks.md §12). Each pad with a sound is a chain:
  - first a **pad inlet,** a small plugin of ours that isn't a module and isn't shown;
  - then the pad's modules;
  - then the chain's volume and pan, as a rack chain's.

  Empty pads have no chain, so they cost nothing.
- **The pad inlet:**
  - passes its pad's note, rewritten to Plays, and the messages every pad gets (§6), and drops every other note;
  - **does the choke itself.** It knows the notes of the other pads in its group. When one of them starts, it sends All sound off down its own chain, at that note's sample position. Every inlet hears every note, so choking needs no messages between pads.
- **All sound off** is an addition to the device API (`EventType::AllSoundOff`, [ADR 0034](../decisions/0034-device-api-frozen.md): the enum gains a value at its end; devices and saved states are unchanged).
  - Our instruments stop every voice with their steal fade, passing it to the shared `VoiceManager` as they pass All notes off now.
  - The JUCE host turns MIDI CC 120 into it, where today it turns it into All notes off. Plugins we host get CC 120.
- **Racks don't nest** (`RackType::isPluginAllowed`), so a kit can't go in a rack and a pad can't hold one.
- **Checked by the spike** (step 0), kept as tests:
  1. **Notes:** a pad hears only its note, at its Plays pitch, sample-exact; pressure reaches only its pad; bend reaches every pad.
  2. **Choke:** the open hat stops within 2 ms of the closed hat's note, without a click (the peak of the difference against a faded reference), at any position in a block.
  3. **CPU:** 16, 64 and 128 pads, each a Sampler with a sample, idle and playing a busy pattern, against the same Samplers on as many tracks. Measured in a Release `[.benchmark]` test.
  4. **Adding a pad while playing:** no gap in the other pads. Adding a module rebuilds the graph today, and a gap there would be heard here more.
  5. **Latency:** a pad with a lookahead Compressor beside a plain one stays in time (the racks' spike says tracktion lines chains up; checked again with many chains).
  6. **A/B and presets:** a kit's whole state swapped (A/B, a preset) sounds the same as one built by hand, and swapping 16 loaded pads takes under 100 ms, the samples loading in the background as the Sampler's do.
  7. **Undo:** moving and swapping pads, and clearing one, each one step back to the same sound.

## 16. Not now

- **Pads as channels of their own** in the mixer, with sends to the song's returns (Live shows a Drum Rack's chains there; Battery and Impact XT have outputs for it). If it's asked for, a pad gets "Its own track" later.
- **Returns inside the kit** (Live's return chains): a reverb on a pad, or a send to a return track, covers it.
- **Snapshots, Morph and Vary on kits:** they stay with racks.
- **Zones** (key, velocity and chain-select ranges on chains): racks.md §13 had them with the Drum Rack. Velocity layers on a pad come from the Sampler's zones instead. Nothing else asked for them.
- **A compact Sampler on pads** (B12).
- **Pads that play across the keyboard** (a pad as a keyboard range): a melodic instrument has a track of its own.

## 17. Steps

0. **The spike (§15) and the UI drawn and approved,** before anything else is built. Drawn with the real panel kit, from a stub, in every state:
   - a new, empty kit;
   - a kit with 12 pads, one selected, its Sampler and a Compressor in the frame;
   - a pad with a synth on it, and one being renamed;
   - the overview with the frame moved up;
   - a drag onto a pad, a drag between pads, and a refused drop;
   - pads in a choke group, one off, one soloed;
   - MACROS shown;
   - folded;
   - at standard and compact widths, zoomed.
1. **The kit:**
   - pads, the overview and PAD;
   - sounds by drop, Load sample… and copy;
   - Plays and choke groups;
   - kits as presets with A/B and a default;
   - the browser's Drum Kit and several samples making a kit;
   - the menus and MCP.

   Signed off after the product owner's hands-on tasks:
   - make a kit from eight samples;
   - put a synth on one pad and a Saturator after a snare;
   - choke the open hat with the closed one;
   - save the kit, and load it on another track;
   - make it your default;
   - swap two pads.
2. **Macros on kits** (§10).
3. **The factory kits, the final step** (the product owner, 2026-10-16). Together, the product owner and Claude search for good CC0 drum sets and kits, download them, and put together a comprehensive set of factory kits covering many genres. The samples go into the Library ([ADR 0055](../decisions/0055-factory-content-is-files-in-a-read-only-library.md)). The kits are made from recipes, as the factory racks are (racks.md §18.8), and signed off by ear.

Each step is built, tested and signed off on its own.

## 18. Tests (done when)

| What | Target |
|---|---|
| Notes | Each pad hears its own note only, at Plays, sample-exact; pressure to its pad; bend and controllers to all |
| A sample on a pad | Plays at its own pitch whatever its file's root; dropped on a Sampler pad, the Sampler keeps its settings |
| Choke | The others in the group stop within 2 ms, without a click; a pad never chokes itself; effects' tails go on |
| Pad mixer | Volume, pan, on and solo heard; solo not saved |
| Kits | Save and load: the same state, byte for byte; A/B swaps whole kits; the default makes new kits; a missing sample or plugin named |
| Browser | Drum Kit and its kits found, previewed (a kit with a plugin not), dropped and double-clicked as instruments are; several samples make a kit |
| Undo | Every edit one step: drops, moves, swaps, copies, clears, settings |
| Engine | The spike's checks (§15) kept as tests; under RealtimeSanitizer, the pad inlet allocates and locks nothing |
| UI | The panel passes the layout lint; every state drawn for review at standard and compact widths |
| MCP | Every tool above driven in a test, as the UI's edits are |

## 19. Questions for review

Approved by the product owner (2026-10-16) with every recommendation.

| # | Question | Recommendation |
|---|---|---|
| K1 | **One instrument or two things** | One instrument, the Drum Kit. No Drum Sampler, no Drum Rack (ADR 0064) |
| K2 | **What a pad holds** | Any instrument (ours or a plugin) and effects after it, as a rack's chain does |
| K3 | **The pads' sampler** | The Sampler, in One-shot mode, with what pads need added to it (All sound off). Not a Drum Sampler |
| K4 | **How many pads, and how they're shown** | 128, one a note; 4 × 4 shown, the lowest at the bottom left, C1 first; an overview of all 128 beside them to move the 16 shown. Not pages of 16, which split a kit at fixed notes |
| K5 | **A sample dropped on a pad with a Sampler** | Goes into that Sampler, keeping its settings, to audition kicks with the pad's shaping |
| K6 | **Plays** | Each pad sends its instrument one note, C3 by default; a Sampler made by a drop has its root at it, so a sample plays at its own pitch |
| K7 | **Preloaded pads** | Empty until they're given a sound; "Use as the default for a new Drum Kit" makes your own starting kit, loaded as you like |
| K8 | **A click on a pad** | Selects it and plays it while held, as FPC, Impact and Maschine do; Select pad on its right-click selects without playing. Live and Bitwig put a preview button on each pad instead, which costs room on a 74 px pad and a second target to aim for |
| K9 | **Choke groups** | None or 1 to 16; a hit stops the rest of its group with All sound off, never itself; effects' tails go on |
| K10 | **Each pad's mixer** | Volume, pan, on and solo, in PAD; pads as mixer channels later (§16) |
| K11 | **Kits saved as** | Presets of the Drum Kit (`.segnopreset`, device `drum-kit`), with A/B and a default, found under Instruments ▸ Drum Kit. Not a `.segnorack`, nor a file type of its own |
| K12 | **The Sampler's panel on pads** | The whole panel for now; a compact view (B12) after real use |
| K13 | **Macros** | A rack's eight, MACROS showing from the first mapping; snapshots and Morph stay with racks |
| K14 | **Plugins** | Allowed on pads; a kit with one isn't previewed in the browser |
| K15 | **In the plugin builds** | No: the kit is the DAW's, as racks are; the Sampler and the Drum Synth are the plugins' drums |
| K16 | **Several samples dropped in a chain** | Make a Drum Kit with them from C1 |
| K17 | **Names** | Drum Kit, kit, pad, choke group, Plays. Never rack or chain in its UI |
| K18 | **Building it** | The spike and the UI drawn for review first (step 0), then the kit (1), then macros (2), and last the factory kits, from CC0 sets searched for together (3) |

## 20. Step 0, the spike (2026-10-16)

Kept as tests (`tests/integration/DrumKitSpikeTests.cpp`). The pad inlet is built (`app/src/engine/PadInlet.h`), and so is All sound off: the device API's event, the shared `VoiceManager`'s `voiceStop`, the Sampler's stop with its steal fade, and CC 120 in the JUCE host.

**What it found (§15's checks, each kept as a test):**
1. **Notes:** each pad hears only its own note, rewritten to Plays, to the sample, mid-block too. Another pad's note, and a note with no pad, make no sound.
2. **Pressure and bend:** polyphonic pressure reaches only its pad, at the note the pad plays; pitch bend reaches every pad.
3. **Choke:** the closed hat stops the open hat within the Sampler's steal fade (1.5 ms), at two places in a block. No click: the largest step is under the tone's own and the fade's together. A pad in no group isn't choked, and a pad hit again never chokes itself.
4. **The track's own audio** goes round sixteen pads once: the output nulls against the audio clip (below −100 dB).
5. **Latency:** a pad with the Compressor's 10 ms lookahead beside a plain pad: both land on the note's sample.
6. **A pad added while playing,** live through the hosted device: no gap. The quietest block around it is within 0.01 % of the steady level.
7. **A kit's state read back** (16 Samplers with their samples) sounds the same (below −100 dB), its choke groups kept.
8. **Undo:** swapping two pads' notes, and clearing a pad, are each one step back to the same sound (below −100 dB).
9. **A pad sounds as the same instrument on a track does** (below −100 dB): the kit adds nothing and loses nothing. Added while building, because the live test (6) reads 3 dB lower than the offline ones. A pad and a track null offline, so those 3 dB come from somewhere on the live path, not from the kit. Not looked into further; the live test checks levels against each other, not absolutely.
10. **CPU** (Release, offline renders of 8 s of sixteenth notes, every pad a Sampler with a sample):

    | Pads | The kit | As many tracks | Kit / tracks | The kit, of real time |
    |---|---|---|---|---|
    | 16 | 675 ms | 578 ms | 1.17× | 8 % |
    | 64 | 1,179 ms | 757 ms | 1.56× | 15 % |
    | 128 | 1,866 ms | 960 ms | 1.94× | 23 % |

    A pad costs about 0.13 % of real time on this machine. In this pattern most pads are silent most of the time, so that's mostly what an idle pad costs. Kits of 16 to 64 pads are cheap. A full 128 costs twice what as many tracks would. That's acceptable, but worth a look before kits that size are common: skipping idle pads, or the Sampler's idle cost.
11. **A whole kit swapped** (A/B, a preset): 16 pads with half-second stereo samples are read back in 47 ms in Release (53 ms in Debug), under the 100 ms §15 asked for.

**Settled while building it:**
- **The inlet gets the kit's notes and nothing else.** Its audio out is silence, so a pad's sound starts from nothing. The track's own audio goes from the kit's input straight to its output, once, by one connection of its own: sixteen pads don't add the track's audio sixteen times.
- **The inlet filters without allocating:** it copies what goes on into an array of its own, then back. An array that only gets shorter never reallocates; removing messages in place could shrink its storage, which allocates.
- **Which notes choke a pad** is worked out on the message thread from every inlet's note and group (`PadInlet::updateChokes`), and read by the audio thread from two atomic words. An inlet works it out again when its own note or group changes (undo included) and when it's prepared. The kit does it after reading a kit in and after any change to its pads (step 1).
- **A pad's mixer uses the balance pan law,** as a rack chain's and every channel's do.
- **A kit's state read in gets new item ids,** as a rack file's does (the Session's loads already do this).

## 21. Step 0, drawn (2026-10-16; approved by the product owner the same day)

Every state of §17 step 0, drawn with the real panel kit and module panels from a stub (`tests/integration/DrumKitUiTests.cpp`, `FD_DUMP_DIR`). Each state passes the module panel lint, the pads' own checks (inside the grid, apart by the gap, name and note inside each pad and clear of each other, the choke number beside the note) and the frame's checks:
- a new, empty kit;
- 15 pads, the kick selected, its Sampler and Compressor in the frame;
- a pad playing a synth;
- a pad being renamed;
- the pads shown moved up a page;
- a sample over an empty pad, and a pad dragged onto another;
- the hats in a choke group, a pad off, a pad soloed;
- pads playing;
- MACROS shown;
- folded.

**What drawing settled:**
- **PAD is two columns, not four.** Vol, Pan and Plays are bars, as a chain's Vol and Pan and the Sampler's Root Key are. Choke is a combo and Solo a toggle. The pad's on is the section's power dot. With knobs, four columns were half empty. The panel is 508 px wide (736 with MACROS), not §8's first estimate of 612.
- **An empty pad is a well** (`surface0`) with its note. A pad with a sound is raised, as a control is (`surface3`). An empty kit reads as empty at a glance.
- **An empty pad's settings show dimmed and can't be changed:** PAD keeps its place, so the panel doesn't change width as pads are clicked.
- **The frame's label always names the pad,** even in a kit of one pad. A rack's names a chain only from its second.
- **The overview** sits in a well like a display's. A pad with a sound is a light cell, the selected pad's is in `active`, and the 16 shown are framed. At 1x a row is 3 px high.
- **A pad's tooltip** names it whole ("Hat closed · F♯1"), as its name is cut to fit. An empty pad's says what it takes.
- **The panel kit gained** `ui::PadGrid` and `ui::PadOverview` (`fd_ui`, shared, so Push 2's display and the piano roll's drum view can draw pads the same way), and the metrics `deviceGrid.padGap`, `padInset` and `overviewGap`.

## 22. Step 1, built (2026-10-16)

The kit, built on the spike (§20) and the drawings (§21): `app/src/engine/SessionKits.cpp` (the Session's kit), `app/src/ui/DrumKitPanel.*` and `DeviceChain` (its panel and frame in the chain), `app/src/mcp/tools/KitTools.cpp` (assistants). Tested through the Session (`DrumKitTests`), the chain view (`DrumKitChainTests`) and MCP (`McpToolTests`).

**What's there:**
- **The Drum Kit is an instrument** (`fd:drum-kit`): in Add module, the chain's +, the browser's Instruments with its kits as presets, and `list_module_types`. A new one is empty, or your default kit.
- **Pads:** a click selects the kit and the pad and plays it while held, as the computer keyboard plays the track (Session::computerKeyboardNote). A double-click renames a pad. A pad dragged onto another moves its sound there (swapped with what's there), Ctrl+drag copies. The wheel and the overview move the 16 shown. A pad lights a moment each time it's hit: its inlet counts its hits on the audio thread, and the chain looks every frame.
- **Sounds onto pads:** from the browser, a sample onto a pad (into its Sampler, or a One-shot Sampler of its own at its own pitch), a module or preset (an instrument in place of the pad's own), a table (a Wavetable with it); into the frame, onto the selected pad. Load sample… on the pad's right-click and in the Module menu. A rack or a kit never goes on a pad, and a modulator doesn't (it moves controls).
- **PAD:** on, Vol, Pan, Plays, Choke and Solo, each one undo step (a drag of Vol, Pan or Plays one step).
- **The pad's right-click:** Select pad, Rename pad…, Load sample…, Copy pad, Paste pad, Clear pad, On, Solo, Choke group ▸. The Module menu has Rename pad…, Copy pad, Paste pad and Clear pad.
- **Kits are presets** (§9): Save preset, the list, A/B, your default, the browser's Drum Kit folder; a kit preset previews as a bar of a beat through its pads.
- **Assistants:** `get_kit`, `set_pad`, `load_pads`, `move_pad`, `clear_pad`; `add_module` and `move_module` take `kit` and `pad`.

**Settled while building:**
- **Several samples make a kit from the desktop.** The browser drags one item at a time, so §11's "several samples dropped in a chain" comes from a file manager: several sample files dropped in a track's chain make a Drum Kit with them from C1 (in place of the track's instrument); dropped on a pad, they load from that pad up; one makes a Sampler, as from the browser.
- **An effect never goes ahead of a pad's instrument:** put first on a pad that has one, it goes after it.
- **The synths stop on a choke too:** All sound off stops the Subtractive and the Wavetable within 1.5 ms, through a quick stop their envelopes gained (`Adsr::stop`, `CurvedEnvelope::stop`, in `libs/dsp`). A plugin stops however it stops on CC 120.
- **The snapshot follows a pad's note, Plays and choke group, and a kit's selected pad and pads shown.** Changes inside a module's own state don't rebuild the snapshot, so these are named for it, a rack's shown chain with them.
- **An empty kit is Default settings:** its state says nothing, so a new kit shows no changed mark.
- **Modulators on a pad aren't kept in a kit preset yet:** they stay with the project; a preset made from a kit with one leaves it out. Few will put one there before macros come (step 2).

**Signed off by the product owner (2026-10-16).** Asked at sign-off: a drum view in the piano roll, for clips on a kit's track: only the lanes of the pads with sounds, named, and without the tools that make no sense for drums (chords and the like). Designed in §23 for review.
