# Racks: modules kept together, and macros that move many at once

A **rack** holds a set of modules as one module in the chain: effects, or an instrument and its effects, in one chain or several side by side. It's saved whole as a file of its own and loaded whole from the browser, and its **macros** move many parameters across it with one knob, each along its own range and curve. Later, macros get snapshots to blend between, and motion of their own (LFOs, followers, a sequencer), after Transit's one-knob transitions.

**Status:** for review (M3 P2, the product owner's priority 2 of 2026-10-13), with [ADR 0056](../decisions/0056-racks-are-modules-of-modules.md).
- **The product owner, 2026-10-14:** chains side by side explained, liked and kept.
- **The UI must be very solid, with nothing confusing.** This comes before everything else in racks (§5a).
- Nothing is built until the product owner says go.

What the product owner asked:
- "Save a whole set of modules (effects, or an instrument and its effects) as one thing: a snapshot of the chain as it is, so a great patch is kept whole."
- "Kept apart from module presets, which never gain a rack variant."
- "On top, to plan: macros wired to points across the rack (one knob moving, and automating, many), presets of the whole rack, ideas from transition multi-effects (Transit and Transit 2)."

## 1. What's there now

- **A track's chain** is a row of modules: ours with their panels, third-party plugins as cards ([daw-shell.md](daw-shell.md), "Device chain"). Each module has its own presets, files of its state ([ADR 0044](../decisions/0044-presets-are-files.md)).
- **Nothing keeps a chain whole.** A sound made of a Wavetable, an EQ, a Saturator and a Delay can't be saved as one thing, found in the browser or put on another track. Copying the track is the only way.
- **One knob can't move several things.** The Wavetable has macros, but only for its own parameters. Automation moves one parameter per lane assignment, several only by assigning them one by one ([automation.md](automation.md) §2).
- **The innovation backlog** asks for "macro snapshots with morphing in racks": Live's variations, but blendable and automatable.

## 2. How others do it (October 2026)

| | What holds the modules | Macros | Saved as |
|---|---|---|---|
| **Live** | Instrument, Audio Effect, MIDI Effect and Drum Racks: chains side by side, each serial, their outputs summed; a chain list with volume, pan, on and solo; key, velocity and chain-select zones with fades; racks nest to any depth; Group (and Ungroup) from selected devices | Up to 16, 8 shown. A macro maps any number of parameters, each with Min and Max (Min above Max inverts); a mapped parameter can't be moved by hand. **Variations** store the macros' positions, recalled but not blended; **Rand** randomises, with macros left out on request | A rack preset in the browser, under the device kind it is ([Ableton](https://www.ableton.com/en/live-manual/12/instrument-drum-and-effect-racks/), [FAQ](https://help.ableton.com/hc/en-us/articles/360019103480)) |
| **Bitwig** | Containers: Chain (serial, with dry/wet), FX and Instrument Layer (parallel), Selectors (one chain at a time, round robin, key switches), Multiband, Mid-Side, XY crossfade | Remote controls, pages of 8, each a parameter or a mapping; modulators anywhere, on tracks and the project too | A device preset of the container ([Bitwig](https://www.bitwig.com/userguide/latest/container), [Sound On Sound](https://SoundOnSound.com/reviews/bitwig-studio-5-1)) |
| **Studio One** | FX Chains with a Splitter for parallel and nested routing; Multi Instruments (instruments only) | 8 knobs, 8 buttons and two XY pads, mapped across the chain | FX Chain and Multi Instrument presets ([Sound On Sound](https://www.soundonsound.com/techniques/studio-one-fx-chains)) |
| **Logic** | A channel strip, or a summing track stack | Smart Controls: a knob mapped to several parameters, each with min, max and invert | Patches ([Logic Studio Training](https://logicstudiotraining.com/smart-controls-logic-pro/)) |
| **Reason** | The Combinator | 4 rotaries and 4 buttons, Min and Max per target in a grid | Combinator patches ([Reason Studios](https://reasonstudios.com/news/post/the-hitchhikers-guide-to-the-combinator-part-i)) |
| **FL Studio** | Patcher: any graph of plugins | A Surface of controls you build | Patcher presets |

**Transit 2** (Baby Audio) is the transition multi-effect the product owner points at ([Baby Audio](https://babyaud.io/transit-2)):
- up to 28 effect modules in a chain you order;
- every parameter either static or linked to one **Transition** knob, each linked one with its own start, end and **slope**;
- the knob is moved by hand or automation (Macro), or by an LFO (synced, 1/64 to 8 bars), a Follower of the input's level, a Sidechain, a Gate held at a tempo division, or a **Sequencer** that runs the transition from the next bar;
- a randomiser with locks.

**What they share:** a container shown as one module, chains in it serial and side by side, a handful of macros mapped across it with a range each, and the whole thing saved as one preset. Live's variations can't be blended; Transit's curves and motion go furthest with one knob.

## 3. What a rack is

- **A module that holds modules.** In the chain it's one module: it can be moved, turned off, folded, removed and automated like any other.
- **Chains side by side.** A rack has one chain or more. Each chain is a row of modules played in order; every chain gets the rack's input, and their outputs are added together.
  - **One chain** keeps a sound whole: the product owner's "snapshot of the chain as it is".
  - **Several** layer instruments (two synths on every note) or process in parallel (a dry chain beside a crushed one). **A chain with no modules passes its input through**, so dry and wet sit side by side.
- **Each chain has a mixer row:** its name, on, solo, volume and pan. Solo hears that chain alone while you set it up; it isn't kept as the sound.
- **Two kinds, by what they hold,** as modules are:
  - an **instrument rack** holds an instrument at the start of a chain (any chain); notes go to every chain's instrument;
  - an **effect rack** holds effects only, and goes wherever effects go.
  - A rack is an instrument rack the moment one of its chains starts with an instrument. On a track, an instrument rack is the track's instrument: put where one is, it replaces it, as any instrument does ([browser.md](browser.md) §11).
- **Anything that goes in a chain goes in a rack:** our modules and third-party plugins.
- **Not in a rack:** a rack. The engine doesn't nest racks (§12), and one level covers what was asked. Key, velocity and chain-select zones come with the Drum Rack (M4), which is a rack with a chain per pad.

## 4. Macros

- **Eight macros a rack,** on two rows of knobs in its panel, named "Macro 1" … "Macro 8" until renamed (a double-click on the name). Live shows eight of its sixteen, Bitwig's pages and Studio One's knobs are eight, and eight fit the module grid as two knob rows of four.
- **A macro moves parameters across the rack:** any automatable parameter of any module in it, ours or a plugin's ([automation.md](automation.md) §2).
- **Each mapping has a range and a curve** (Transit's start, end and slope):
  - **From** and **To**: where the parameter is at the macro's 0 and 100 %. To below From turns it the other way.
  - **Curve**: straight, or bent toward either end, so a filter opens late and a reverb comes in early on one knob.
- **Mapping one:**
  1. **Drag the macro's name onto a knob** in the rack, as a modulation source is dragged ([device-panels.md](device-panels.md), "Modulation"). The mapping spans the knob's whole range, From where the knob is.
  2. **Right-click a knob: Map to macro ▸ Macro 1 … 8.**
  3. **In the macro editor (§6):** Add, then the module and its parameter.
- **A mapped parameter follows its macro.**
  - Its knob shows the mapped range as a ring in the macro colour, the modulation rings' look, and moves as the macro moves.
  - It can't be dragged by hand while mapped, as Live does. Its tooltip says "Set by Macro 2" and its right-click offers Unmap. Two macros can't map one parameter.
- **Macros are automated like any parameter:** assigned to lanes, drawn, recorded ([automation.md](automation.md)). That's how a transition is drawn across the bars before a drop.
- **The macros' own knobs** are ordinary parameters of the rack: saved with it, put back by undo, and reached by MCP.

## 5. In the chain

```
 ┌ RACK ─ Drop riser ───────────┐ ┌ EQ ──────────┐ ┌ SATURATOR ───┐ ┌ DELAY ───────┐  + │
 │ MACROS               CHAINS  │ │              │ │              │ │              │    │
 │ (Cut) (Wash) (Drive) (Space) │ │   ...        │ │   ...        │ │   ...        │    │
 │ ( 5 ) ( 6 ) ( 7 ) ( 8 )      │ │              │ │              │ │              │    │
 │                  ● Main  ▮ ◐ │ │              │ │              │ │              │    │
 │                  ● Dry   ▮ ◐ │ │              │ │              │ │              │    │
 └──────────────────────────────┘ └──────────────┘ └──────────────┘ └──────────────┘    │
  └──────────────────────────────── the rack's frame ───────────────────────────────────┘
```

- **The rack's panel,** on the module grid like any module's ([device-panels.md](device-panels.md)):
  - MACROS, 4 columns: eight knobs, their names their handles;
  - CHAINS, 4 columns: one row a chain (on, name, volume bar, pan; solo on right-click and in the row's menu), "+ Chain" at the end.
- **Its modules follow it in the chain, inside a frame:** a band in `surface1` behind them, from the rack's panel to its end, with "+" there to add a module to the chain shown.
  - **With several chains, the selected one's modules show;** clicking a chain's row shows its modules. The frame's top edge names the chain.
  - **Folding the rack** (its chevron, as for any module) folds its modules away with it: the rack becomes one strip, its macros still in the rack's panel when unfolded. This is how a finished rack stays out of the way.
- **Moving modules:**
  - dragged into the frame, a module joins the chain shown; dragged out, it leaves the rack;
  - dropped from the browser into the frame, it goes into the chain there.
- **Making one:**
  - **Group into rack** (Ctrl+G) wraps the selected modules in a rack, one chain, in their order; their automation, sidechains and mappings stay with them. Shift-click extends the selection along the chain, as in a list.
  - **Ungroup rack** (Ctrl+Shift+G) puts the selected chain's modules back in the track's chain where the rack was; the other chains and the macros' mappings go, in one undo step (with a toast to undo it).
  - **Add module ▸ Rack** makes an empty rack.
- **Every edit is one undo step,** as every chain edit is.

## 5a. The UI first: nothing confusing

The product owner's requirement (2026-10-14): **the rack's UI must be very solid; no confusion.** A rack puts modules inside a module, chains beside chains and knobs that move other knobs, so this is where a UI most easily confuses. So:

- **Drawn and approved before it's built.** Before step 1's engine work, the rack is drawn with the real panel kit, from a stub, and reviewed by the product owner, zoomed, at standard and compact widths. Every state is drawn:
  - a rack with one chain;
  - with two chains, each selected in turn;
  - folded;
  - a module being dragged in, and one dragged out;
  - a knob mapped to a macro, and one being mapped;
  - the macro editor;
  - an empty rack.

  What's confusing changes on paper, not after it's built.
- **Clarity rules** (proposed; R17):
  - **A rack with one chain looks like a group, no more.** Its CHAINS section shows only once a second chain is added. With one chain, the panel is MACROS and "+ Chain" in its menu. The chain list never shows a lone row to wonder about.
  - **What's inside is never in doubt.**
    - The frame runs unbroken from the rack's panel to its end, under the shown chain's modules, so it's clear which modules are in it and which come after it.
    - With several chains, the frame says which chain is shown ("Chain 2 · Crushed"), and the chain list marks the same row.
    - A folded rack says what it holds ("4 modules", "2 chains").
  - **Where a drop lands is shown before it lands:** inside the frame (into the rack's chain) or past its end (after the rack, in the track's chain), with the marker in the frame's colour or the chain's.
  - **A macro and what it moves are seen together.**
    - Hovering a macro lights the knobs it moves and dims the rest, as modulation sources do.
    - Hovering a mapped knob lights its macro.
    - A mapped knob always shows its ring and its tooltip says which macro sets it, so a knob that won't move is never a mystery.
  - **One word for each thing,** everywhere (panel, menus, tooltips, MCP, docs): **rack**, **chain**, **macro**, **map**. No synonyms ("layer", "container", "group" only in "Group into rack").
  - **Nothing hidden that changes the sound.** Solo is lit wherever the chain shows, and a muted chain is dimmed in the list and on its frame.
- **Tried before sign-off.** The product owner does a set of tasks unaided:
  - make a rack from three modules;
  - add a dry chain beside them;
  - map a macro to two parameters, with a curve;
  - fold the rack;
  - save it, load it on another track;
  - take a module out.

  Anything that made them hesitate is changed before sign-off.
- **As always:** the layout lint for the rack's panel and the macro editor, and the zoomed screenshot review ([ui-foundations.md](ui-foundations.md), "Layout quality").

## 6. The macro editor

The rack's mappings need more room than its panel: a module's editor in the detail panel, as the table editor is ([ADR 0053](../decisions/0053-a-modules-editor-in-the-detail-panel.md)). Module > Edit macros, or a double-click on a macro's name, opens it: "Modules · Clip · Macros ×".

- **Macros** (left pane): the eight, each with its name, its knob, and how many mappings it has. Selecting one shows its mappings.
- **Mappings** (the middle, flexible): one row each: the module and parameter, From and To as value bars in the parameter's unit, Curve as a bar (−100 … +100 %), and Remove; "+ Add" at the end.
- **Curves** (right pane): every mapping of the macro drawn across the macro's travel, one line each in its module's colour, the macro's position as a vertical line. What one knob does to the whole rack, seen at once, as Transit's linked knobs are but together.
- Escape or the × closes it; edits are kept as they're made, each an undo step.

## 7. Saving and loading

- **A rack is saved as a file of its own:** `.segnorack`, a new permanent identifier (`docs/ids.md`), apart from module presets (`.segnopreset`). Module presets never get a rack variant.
  - **What it holds:** its chains, in order, each with its mixer row and its modules' states (ours as their presets hold them, ADR 0044; plugins as their state, as a project holds it); the macros' names, positions and mappings; later, its snapshots (§8).
  - **What it doesn't:** automation, sidechain sources (host routing, ADR 0025), MIDI mappings, the panel's view state (as for module presets).
  - **Its name is the file's name;** tags, a description and an author, as a preset's (presets.md §4).
- **Where:** the User library's `Racks/` folder (Documents/Segno/Racks), any subfolders yours. Factory racks are files in the Library's `Racks/` (ADR 0055).
- **In the browser** ([browser.md](browser.md)): each of Instruments and Audio effects opens with a **Racks** folder, holding the racks of its kind, factory then yours. Search finds them as RACKS. Selected, an instrument rack plays the presets' phrase; dropped or double-clicked, a rack goes where a module would, an instrument rack in place of the track's instrument.
- **Module > Save rack…** (and Save rack as…) writes the selected rack; **Module > Show racks folder** opens the folder.
- **Missing pieces:** a rack with a plugin that isn't installed loads the rest and says which plugin is missing, as a project does; a module newer than this Segno is refused with its name.

## 8. From Transit: snapshots, motion, randomising (later steps)

What the product owner asked to plan. Each builds on step 1's macros.

- **Snapshots (step 2).** A rack keeps up to 8 snapshots of its macros' positions: Store, recall, rename, a macro left out of them on request (Live's variations).
  - **Beyond Live:** a **Morph** knob blends between two chosen snapshots, A and B, every macro gliding from one to the other. It's automatable, so one lane moves a whole rack from its verse sound to its drop sound (the innovation backlog's "macro snapshots with morphing").
- **Vary (step 2).** Module > Vary on a rack changes its macros by a set amount, leaving locked ones alone, as for the Wavetable (its W12). Transit's randomiser with locks, and Live's Rand, but never a blind randomise.
- **Motion (step 3).** A macro can be moved by something of its own as well as by hand and automation, Transit's six modes:
  - an **LFO** (synced 1/64 to 8 bars, or free);
  - a **follower** of the rack's input level, or of another track's (a sidechain);
  - a **sequencer** that runs the macro from 0 to 100 % over a number of bars, starting at the next bar, once, back and forth, or looping;
  - a **gate** that holds it at 100 % while a key is held;
  - a **random** source.

  These are the engine's modifiers (§12), and they're M4's DAW-wide modulators arriving early, for macros first. A macro's motion shows as a source chip on it.
- **Factory racks (with step 2):** about a dozen in the Library: transitions on one macro (riser, wash-out, lo-fi drop, tape stop, filter build), parallel processing (parallel compression, a widener), and layered instruments built on the Library's sounds.

## 9. Menu bar and keys ([ADR 0040](../decisions/0040-a-menu-bar-and-everything-by-mouse.md))

The **Module** menu, a group after the presets:

| Item | Key | |
|---|---|---|
| Group into rack | Ctrl+G | The selected modules; dimmed unless one is selected |
| Ungroup rack | Ctrl+Shift+G | The selected rack |
| Edit macros | | Opens the macro editor; ticked while it's open; also a double-click on a macro's name |
| Add chain | | To the selected rack; also its "+ Chain" |
| Save rack… / Save rack as… | | The selected rack |
| Show racks folder | | |
| *Step 2:* Store snapshot, Vary | | |

Map to macro and Unmap are on a knob's right-click, and the macro editor's Add does the same by mouse.

## 10. Assistants (MCP, [ADR 0021](../decisions/0021-mcp-server.md))

| Tool | What it does |
|---|---|
| `group_modules` | Modules of one chain into a new rack (one undo step) |
| `ungroup_rack` | A rack's chain back into the track's |
| `get_rack` | Its chains (each with its mixer row and modules), its macros (names, positions, mappings with From, To and Curve) |
| `add_rack_chain`, `remove_rack_chain`, `set_rack_chain` | A chain added, removed, or its name, on, solo, volume and pan set |
| `map_macro`, `unmap_macro` | A parameter mapped to a macro, with From, To and Curve; or unmapped |
| `set_macro` | A macro's position or name |
| `save_rack` | The rack as a file; `add_module` loads one by its path; `search` finds racks as `rack` |

Modules inside a rack keep their ids, so `set_parameters`, `get_module` and the others reach them as before; `add_module` and `move_module` take a rack's chain as the place.

## 11. Model and views

- **The snapshot** (`model::DeviceSlot`) gains, for a rack: its chains, each with its mixer row and the slots of its modules, and its macros with their mappings. A chain's modules are slots like a track's, so panels, automation and MCP see them as they see any module.
- **The DeviceChain view** lays out the rack's panel, then the shown chain's modules in its frame. The rack's panel is our own panel kit's (`RackPanel`, in `app/src/ui`), on the module grid, checked by the layout lint.
- **Mapped knobs:** the panel kit's rings already draw a range on a knob; a host-side mapping (`ParamBinding`) tells a knob it's set by a macro, with its range, so ours show the macro's ring and refuse a drag.

## 12. Engine (tracktion)

- **A rack is tracktion's `RackType`, placed as a `RackInstance`** in the track's plugin list. A `RackType` is a graph of plugins and connections. We build it as chains: the rack's input to each chain's first module, module to module, each chain's last module through a volume and pan of its own to the rack's output, where they're summed. tracktion refuses racks inside racks (`RackType::isPluginAllowed`), which is why §3 rules them out.
- **Notes reach every chain's instrument** through the rack's MIDI input.
- **Macros are the rack's macro parameters** (`MacroParameterElement`), automatable like any parameter. A mapping is a modifier assignment on the target parameter (`AutomatableParameter::addModifier`) with its value, offset and curve, which carries From, To and Curve as they are.
- **Motion (step 3)** uses the rack's modifier list: tracktion's LFO, step, breakpoint, envelope follower and random modifiers.
- **Checked by the spike** (step 0, 2026-10-14, `tests/integration/RackSpikeTests.cpp`, kept as tests):
  - **Parallel latency:** the EQ in linear phase (5,120 samples late) beside an empty chain sums to exactly twice the input, in time with the song (−300 dB residual). tracktion lines the chains up inside the rack's graph; the rack instance itself reports no latency.
  - **Macros reach the sound:** a macro mapped to our Utility's gain from −24 to 0 dB gives −24 dB at 0 %, 0 dB at 100 %, and the straight midpoint at 50 %. A curve bends it either way.
    - The mapping is the parameter's own value as From, and an assignment of To − From with the curve.
    - tracktion's curve runs −0.5 … +0.5 with 0 straight, so Curve's −100 … +100 % is that range.
  - **Undo:** taking two modules out of a track's chain into a rack, in one transaction, sounds the same as before, and one undo puts the chain back as it was.
  - **A rack's state** written out and read into another project keeps its modules' settings and its macros' positions, our modules and a hosted VST3 alike: the same render.
  - **Notes** reach an instrument in a rack.
  - **To watch:** tracktion's macro source takes a lock when its position updates. That's inside the engine, but racks get a run under the RealtimeSanitizer preset when step 1 is built.

## 13. Not in racks, for now

- Racks inside racks (the engine doesn't).
- Key, velocity and chain-select zones; the Drum Rack (M4).
- Chain selectors and crossfaders (Bitwig's Selector and XY): the Morph knob covers blending between sounds; a selector can come with the Drum Rack's per-pad chains.
- MIDI effect racks: Segno has no MIDI effects yet (M4).
- Racks in the plugins: the plugins are one module each. A rack is a DAW thing, as ADR 0002 foresaw ("racks and macros later") and as Live's are.

## 14. Steps

0. **The UI drawn and approved (§5a),** with the spike (§12): before anything else is built.
1. **Racks and macros:** the rack with its chains and mixer rows; group and ungroup; the panel and frame in the chain; eight macros with From, To and Curve, mapped by drag, right-click or the editor; the macro editor; automation of macros; rack files, the browser's Racks folders, save and load; menus and MCP. Signed off after the product owner's hands-on tasks (§5a).
2. **Snapshots and Morph, Vary for racks,** and the factory racks in the Library.
3. **Motion on macros:** LFO, follower and sidechain, sequencer, gate, random.

Each built, tested and signed off on its own.

## 15. Tests (done when)

| What | Target |
|---|---|
| A rack sounds as its modules did | Group into rack, ungroup: the render is the same to the sample, automation and sidechains kept |
| Parallel chains | Two chains of different latency sum without comb filtering (a null test against a single chain with the delay matched); an empty chain passes the input |
| Chain mixer | Volume, pan, on and solo heard; solo not saved |
| Macros | A mapping's From, To and Curve land the parameter where they say at 0, 50 and 100 %; a mapped knob refuses a drag; automating a macro moves every mapped parameter |
| Rack files | Save, load: the same state, byte for byte; a missing plugin said; one with a newer module refused |
| Browser | Racks in Instruments and Audio effects, found by search, previewed, dropped and double-clicked where a module would go |
| Undo | Every edit one step; ungroup undone puts the rack back whole |
| UI | The panel and the macro editor pass the layout lint; the frame and its chain drawn for review at standard and compact widths |
| MCP | Every tool above driven in a test, as the UI's edits |

## 16. Questions for review

| # | Question | Recommendation |
|---|---|---|
| R1 | **What a rack holds** | Chains side by side, each serial, summed; one chain by default (the "snapshot of the chain"), more for layers and parallel processing; an empty chain passes its input |
| R2 | **Its kinds** | An instrument rack (an instrument starts a chain) or an effect rack, by what it holds; an instrument rack is the track's instrument |
| R3 | **Each chain's mixer** | Name, on, solo, volume, pan; solo for setting up, not saved |
| R4 | **Racks in racks** | No: the engine doesn't nest them, and one level covers what was asked |
| R5 | **How many macros** | Eight, two knob rows of four in the panel |
| R6 | **A mapping** | From, To (reversed when To is below From) and Curve, Transit's start, end and slope; two macros never map one parameter |
| R7 | **A mapped knob** | Follows its macro, its range shown as a ring in the macro colour; not dragged by hand while mapped; Unmap on right-click |
| R8 | **Mapping by mouse** | Drag a macro's name onto a knob; right-click a knob, Map to macro; the macro editor's Add |
| R9 | **The macro editor** | A module editor in the detail panel (ADR 0053): the macros, the selected one's mappings, every mapping's curve drawn together |
| R10 | **In the chain** | The rack's panel (MACROS, CHAINS), then the shown chain's modules in a frame; folding the rack folds its modules away |
| R11 | **Making and unmaking** | Group into rack (Ctrl+G) from selected modules; Ungroup rack (Ctrl+Shift+G); Add module ▸ Rack |
| R12 | **Saving** | A `.segnorack` file, apart from module presets: chains, modules' states, mixer rows, macros and mappings; not automation or routing. In the User library's Racks folder; factory racks in the Library |
| R13 | **In the browser** | A Racks folder first in each of Instruments and Audio effects; RACKS in search; previewed and dropped as modules are |
| R14 | **From Transit, and when** | Step 1: per-mapping curves. Step 2: snapshots with Morph, Vary with locks, factory racks. Step 3: motion on macros (LFO, follower, sidechain, sequencer, gate, random), M4's modulators arriving early |
| R15 | **Engine** | tracktion's racks and macro modifiers, after a spike on parallel latency, undo and plugin state |
| R16 | **Not now** | Zones and the Drum Rack (M4), selectors and crossfaders, MIDI effect racks, racks in the plugins |
| R17 | **The UI, nothing confusing** (the product owner's requirement) | Drawn in every state and approved before it's built; one chain looks like a plain group (the chain list shows from the second chain); the frame always says what's inside; drops show where they land; macros and their knobs light each other; one word for each thing; tried hands-on before sign-off (§5a) |
