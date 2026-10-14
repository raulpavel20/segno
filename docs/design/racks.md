# Racks: modules kept together, and macros that move many at once

A **rack** holds a set of modules as one module in the chain: effects, or an instrument and its effects, in one chain or several side by side. It's saved whole as a file of its own and loaded whole from the browser, and its **macros** move many parameters across it with one knob, each along its own range and curve. Later, macros get snapshots to blend between, and motion of their own (LFOs, followers, a sequencer), after Transit's one-knob transitions.

**Status:** M3 P2, the product owner's priority 2 of 2026-10-13, with [ADR 0056](../decisions/0056-racks-are-modules-of-modules.md). Approved 2026-10-14 (§16); step 0 drawn and approved, step 1 built (§17) and waiting for the hands-on tasks; step 2 designed and approved (§18), its UI being drawn for review.
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

- **Eight macros a rack,** on two rows of knobs in its panel, named "Macro 1" … "Macro 8" until renamed (a double-click on the name, or its right-click). Live shows eight of its sixteen, Bitwig's pages and Studio One's knobs are eight, and eight fit the module grid as two knob rows of four.
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
  - **One word for each thing,** everywhere (panel, menus, tooltips, MCP, docs): **rack**, **chain**, **macro**, **map**, and from step 2 **snapshot**, **Morph**, **lock** (§18). No synonyms ("layer", "container", "variation", "preset" for a rack or a snapshot, "group" only in "Group into rack").
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

### Drawn (step 0, 2026-10-14; approved by the product owner the same day)

Every state above, drawn with the real panel kit and module panels from a stub (`tests/integration/RackUiTests.cpp`, `FD_DUMP_DIR`), each checked by the layout lint and in its frame:

- **The frame** is a tray in `rackFrame` (surface0) edged in `line`, `shell.rackInset` round the rack's panel, the shown chain's modules and the end cap's "+". Without its edge it barely showed against the detail panel, so it has one. Its strip under the modules (`shell.rackLabelHeight`) names the shown chain when there are several ("CHAIN 2 · CRUSHED"), adds "· OFF" when it's off and "SOLO" in `active` when it's soloed. Every panel in the chain sits `rackInset` lower so their tops line up; the detail panel is about 20 px taller for it.
- **CHAINS** looks like the table editor's layers, so a list looks the same everywhere: a row a chain (its on dot, its name to choose it, S), "+ Chain" under the names, and at the bottom the shown chain's name over its Vol and Pan bars. Choosing a name shows that chain's modules in the frame; the chosen name is lit as a choice is. Room for 4 chains (question below).
- **A chain that's off** shows its dot off and its name dimmed in the list, its modules faded in the frame, and "· OFF" in the frame's label.
- **Macros:** one that maps nothing yet is faded (it does nothing); a mapped knob shows its range as the modulation ring and follows its macro. Hovering a macro's name lights it and dims every other macro's rings; hovering a mapped knob lights its macro's name. Dragging a macro's name marks the knobs in the rack that can take it (faint rings) and lights the one under the pointer; nothing outside the rack is marked.
- **Folded,** the rack is one strip that reads "Drop riser  2 chains" (or "4 modules"), still in its tray.
- **An empty rack** holds "Drop modules here" in a card's width, then its "+".
- **Drops:** into the rack's chain, the tray's edge turns `active` and the line runs the panels' height; past its end, the tray stays as it is and the line runs the chain's whole height.
- **The macro editor** (at least 944 px wide): MACROS lists the eight (unmapped ones faded); MAPPINGS · WASH is a table, headings over Parameter (a colour swatch, the parameter, its module fainter), From and To in the parameter's unit, Curve (filled from its middle) and ×, with + in its title; CURVES draws each mapping's curve in its swatch's colour, the engine's own curve (`model::macroMapped`), with the macro's position as a line, and the macro's bar under it. A macro that maps nothing says how to map one.
- **Settled while drawing:**
  - a double-click on a macro's name renames it, as names are renamed everywhere; the editor opens from Module > Edit macros and the macro's right-click (§6 said a double-click did both);
  - a rack with one chain gets its second from Module > Add chain, the rack's right-click, and the end cap's "+" menu (Add module ▸ …, then Add chain);
  - the panel kit gained what the rack needs: a knob that follows (`Knob::setFollowing`), a section shown only while it matters (`DevicePanel::showSectionWhen`), a panel's own name and folded detail (`setDisplayName`, `setFoldedDetail`), `DeviceGrid::power` for every power dot, and `ui::NoteText` shared from the table editor.

## 6. The macro editor

The rack's mappings need more room than its panel: a module's editor in the detail panel, as the table editor is ([ADR 0053](../decisions/0053-a-modules-editor-in-the-detail-panel.md)). Module > Edit macros, or the macro's right-click (Edit macros), opens it: "Modules · Clip · Macros ×".

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
- **In the browser** ([browser.md](browser.md)): **Racks** is a part of its own, after Audio effects (ADR 0059; first designed as a Racks folder in each of Instruments and Audio effects), holding every rack, instrument and effect racks together, factory then yours. Search finds them as RACKS. Selected, an instrument rack plays the presets' phrase; dropped or double-clicked, a rack goes where a module would, an instrument rack in place of the track's instrument.
- **Module > Save rack…** (and Save rack as…) writes the selected rack; **Module > Show racks folder** opens the folder.
- **Missing pieces:** a rack with a plugin that isn't installed loads the rest and says which plugin is missing, as a project does; a module newer than this Segno is refused with its name.

## 8. From Transit: snapshots, motion, randomising (later steps)

What the product owner asked to plan. Each builds on step 1's macros.

Step 2 is designed in §18; what follows is the plan it started from.

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
| Edit macros | | Opens the macro editor; ticked while it's open; also on a macro's right-click |
| Add chain | | To the selected rack; also its "+ Chain" |
| Save rack… / Save rack as… | | The selected rack |
| Show racks folder | | |
| *Step 2* (§18.9): Store, Update, Rename and Remove snapshot, Morph, Vary, Vary more | | |

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
1. **Racks and macros:** the rack with its chains and mixer rows; group and ungroup; the panel and frame in the chain; eight macros with From, To and Curve, mapped by drag, right-click or the editor; the macro editor; automation of macros; rack files, the browser's Racks part, save and load; menus and MCP. Signed off after the product owner's hands-on tasks (§5a).
2. **Snapshots and Morph, Vary for racks,** and the factory racks in the Library (§18, for review).
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

Approved by the product owner (2026-10-14) with every recommendation, then the UI drawn in every state (§5a), with its four points: the frame, room for 4 chains, a macro that maps nothing faded, the second chain from Module > Add chain, the rack's right-click and the end cap's "+".

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
| R13 | **In the browser** | ~~A Racks folder first in each of Instruments and Audio effects~~ A part of its own, Racks, both kinds together (the product owner, 2026-10-14, ADR 0059); RACKS in search; previewed and dropped as modules are |
| R14 | **From Transit, and when** | Step 1: per-mapping curves. Step 2: snapshots with Morph, Vary with locks, factory racks. Step 3: motion on macros (LFO, follower, sidechain, sequencer, gate, random), M4's modulators arriving early |
| R15 | **Engine** | tracktion's racks and macro modifiers, after a spike on parallel latency, undo and plugin state |
| R16 | **Not now** | Zones and the Drum Rack (M4), selectors and crossfaders, MIDI effect racks, racks in the plugins |
| R17 | **The UI, nothing confusing** (the product owner's requirement) | Drawn in every state and approved before it's built; one chain looks like a plain group (the chain list shows from the second chain); the frame always says what's inside; drops show where they land; macros and their knobs light each other; one word for each thing; tried hands-on before sign-off (§5a) |

## 17. Settled while building step 1 (2026-10-14)

- **Solo is an undo step** ("Solo chain"). A chain's solo rewires the rack, and tracktion's undo history must hold every edit of the wiring, or undoing another edit could break it. It's still a listening aid, never kept in a rack's file.
- **Macros start at 0 %** (tracktion starts them half way), so a knob mapped from where it is stays there. Mapping by a drag, a right-click or the editor's + takes From from where the knob is and To at the top of its range (the bottom, when it's at the top).
- **A parameter's saved value leaves out what macros add.** tracktion wrote the moved value back into the project, so saving, a render or a bounce took a macro's position as the mapping's new From. A small patch to tracktion keeps the parameter's own value there (`cmake/patches/tracktion_engine-modifier-base-value.patch`); automation still saves what it plays.
- **A rack's modules keep their ids**: every module tool, lane and sidechain reaches them as before. A module that leaves its rack is unmapped, keeping its value now. A track copied with a rack on it gets a copy of the rack of its own.
- **Rack files are JSON** (`segnoRack: 1`): the chains with their mixer rows, our modules' states as their presets hold them, plugins' their own state, and the macros, their mappings referring to modules by chain and place. A loaded rack saved again makes the same file, byte for byte.
- **Save rack** writes `Documents/Segno/Racks/<name>.segnorack`; **Save rack as…** renames the rack in place, then saves; **Show racks folder** opens the folder.
- **The browser** has Racks as a part of its own after Audio effects, both kinds together (the Library's racks first, then yours; the product owner, 2026-10-14, ADR 0059; built first as a folder in each of Instruments and Audio effects), finds racks in search as RACKS, and puts them where a module would go, an instrument rack in place of the track's instrument. **Not yet:** a rack's preview (the presets' phrase through it); it comes with step 2's factory racks.
- **Right-clicks:** the rack's header (Rename rack…, Add chain, Edit macros, Ungroup rack); a chain's name (Rename chain…, Solo, On, Remove chain); a macro (Rename macro…, Edit macros, then its automation); a knob in a rack (Map to macro ▸, Unmap from …, Edit macros). Double-clicking a macro's name renames it.
- **Group into rack** takes the selected module and those Shift-clicked along the track's chain.
- **MCP**, beyond §10's tools: `add_module` takes `rack` and `chain` (and type `rack`, with `path` a rack file); `move_module` takes `rack` (null: out of it) and `chain`; `set_parameters` refuses a parameter a macro sets, naming the macro; `get_module` says `setByMacro`, and a rack's macros are its parameters.
- **Still to do before sign-off:** the product owner's hands-on tasks (§5a). The lock tracktion's macro source takes (§12, "To watch") is still to be checked: the `rtsan` preset builds the JUCE-free core only, so it needs the DAW built under the sanitizer (open question B84).

## 18. Step 2: snapshots, Morph, Vary and factory racks (for review)

**Status:** approved by the product owner (2026-10-14) with every recommendation of S1–S14. As for step 1, the UI is drawn in every state for review (§18.11) before anything else is built.

### 18.1 How others do it (October 2026)

| | Snapshots | Blending | Randomising | Leaving a control out |
|---|---|---|---|---|
| **Live 12** | Macro Variations: New stores the macros' positions; a click recalls them at once; overwrite, rename, delete. Recalling can't be automated, only mapped to MIDI | None | Rand: every mapped macro at random | Two flags on a macro's right-click, Exclude from variations and from randomisation (Ableton's FAQ and its users disagree on whether one covers both) |
| **Bitwig, Studio One** | None found for containers or FX chains: presets of the whole only | – | – | – |
| **Transit 2** | – | One Transition knob over every linked parameter, each with start, end and slope | A randomiser | Locks |
| **SnappySnap** (2026, a plugin host) | 12 snapshots of a plugin's whole state | A fader and an XY pad across all 12 | "Breeding": crossover and mutation | Pinned knobs, left out of morphing |
| **Flux** (IRCAM Trax, Spat) | Slots A and B | A morph slider, the only thing automation reads; an edit while it's between A and B moves it to the nearer one | – | – |
| **Reaktor, Reason** (Morpher) | Snapshots | A morph between two | – | – |

Sources: [Ableton's Macros and Variations FAQ](https://help.ableton.com/hc/en-us/articles/360019103480), [Ableton forum: automating variations](https://forum.ableton.com/viewtopic.php?p=1836659), [Garnish: Macro Variations](https://edu.garnishmusicproduction.com/?p=19008), [Sound On Sound: Studio One FX Chains](https://www.soundonsound.com/techniques/studio-one-fx-chains), [Baby Audio: Transit 2](https://babyaud.io/transit-2), [Sonicstate: SnappySnap](https://www.sonicstate.com/news/2026/02/06/snappysnap-plugin-snapshot-morphing/), [Flux: preset section](https://doc.flux.audio/ircam-trax/Preset_Section.html), [Reason Studios: Morpher](https://reasonstudios.com/shop/rack-extension/morpher-cv-utility).

**What we take:**
- **Snapshots of the macros only,** as Live's variations: small, every value continuous, and about what the rack's maker chose to put on its knobs.
- **A morph between two chosen snapshots,** as Flux, Reaktor and Reason have, built as a macro that moves the macros, so it's automated as one lane, in time as any automation is.
- **A lock on a macro,** as Transit's locks and SnappySnap's pinned knobs: one lock for recall, Morph and Vary, never Live's two flags.
- **Vary that nudges** (the Wavetable's W12), never a blind randomise.

**Beyond them:** in Live a variation can't be automated at all, and no DAW blends between them. Here one Morph lane takes a whole rack from its verse sound to its drop sound. Snapshots also behave as presets do (the current one named, a mark when it's changed, Update), so there's nothing new to learn about keeping one.

### 18.2 Snapshots

- **A saved rack and a snapshot are two different things, and never called by each other's name:**
  - **a saved rack** is the whole rack: its chains, its modules and their settings, its macros and their mappings. It's a file of its own, found in the browser (§7);
  - **a snapshot** is where the rack's eight macro knobs are, nothing else. It lives inside one rack, on its panel.

  Two versions that differ in anything not on a macro are two racks.
- **Racks have no presets** in the sense modules do (a preset field in the header, browsing in place). Considered after approval and declined by the product owner (2026-10-14): presets of a rack would hide what belongs to what. Each saved rack is a rack of its own.
- **A snapshot is the positions of the rack's eight macros, with a name.** A rack keeps up to eight.
- **Store:** Module > Store snapshot, the rack's right-click, a macro's right-click, or "+ Store" in SNAPSHOTS once there's one. It takes every macro where it is, named "Snapshot 1", "Snapshot 2"… until renamed, and becomes the current one.
- **Recall:** a click on a snapshot's name. Every macro jumps to where the snapshot has it, except locked and automated ones (§18.4). There's no glide: Morph is the glide.
- **The current snapshot** is the one last stored or recalled. Its name is lit, as a choice is. Once a macro moves away from it, its name gets the changed mark, as a preset's name does in a module's header (presets.md §6). Module > Update snapshot stores the macros over it; it's dimmed until something has changed. The rack keeps which one is current, and nothing else does.
- **Rename:** a double-click on its name (as names are renamed everywhere), its right-click, or Module > Rename snapshot… for the current one.
- **Remove:** its right-click, or Module > Remove snapshot for the current one.
- **Each is one undo step:** store, recall, update, rename, remove.
- **Kept** with the project and in the rack's file (§18.9).

### 18.3 Morph

- **A ninth knob, Morph, 0 to 100 %, between two snapshots chosen as A and B.**
  - At 0 % every macro is where A has it; at 100 %, where B has it.
  - In between, each macro moves in a straight line between its two positions. Each macro's own mappings still bend the parameters they move, as they do now.
- **On and off.** MORPH has a power dot in its title, as a module's sections do (the Filter's LFO).
  - **On:** the macros follow Morph. Each shows its A-to-B range as the ring a mapped knob shows, follows Morph, and can't be dragged by hand. Its tooltip says "Set by Morph: Verse → Drop". This is the rule a knob mapped to a macro already follows.
  - **Off:** the macros stay where Morph left them and move by hand again, as a knob keeps its value when it's unmapped.
- **Seen together:** hovering Morph lights the macros it moves and dims the rest; hovering a macro lights Morph. The macros already work this way with the knobs they move.
- **Automated** like any parameter (`morph`): one lane takes the rack from A to B across the bars before a drop.
- **While Morph is on:**
  - a click on a snapshot turns Morph off and recalls that snapshot, in one undo step, so a click on a snapshot always does the same thing; the dot shows Morph is off;
  - Store snapshot works, and keeps the in-between as a new snapshot;
  - Update snapshot and Vary are dimmed, and say why ("Morph sets the macros").
- **Changing A or B,** or locking or unlocking a macro, while Morph is on: Morph follows at once.
- **The first two snapshots start as A and B.** Removing A or B turns Morph off and clears that choice.
- **Engine:** Morph is one more macro of the rack, mapped to each unlocked, unautomated macro, From its position in A To its position in B, straight. tracktion works out a macro moved by a macro (its code refuses only a macro mapped to itself), so Morph follows its lane as any automated parameter does, a block at a time, and needs no patch.
- **Checked by the spike** (2026-10-14, `tests/integration/RackSpikeTests.cpp`, kept as a test): Morph takes a macro from A to B in the sound, half way at 50 %; its lane's step lands within a block of where it's drawn; the mapping is one undo step; and a rack's state read into another project morphs the same. tracktion reads a lane 10 ms after it changes, so a test lets the message loop run before it renders.

### 18.4 Locks, and automation first

- **A lock on each macro:** Lock on the macro's right-click, and a lock toggle on each row of the macro editor's MACROS list, so the mouse reaches it without a right-click.
  - A locked macro shows a lock beside its name in the panel. Nothing shows on unlocked ones.
  - **Recall, Morph and Vary leave a locked macro alone.** You can still turn it by hand and automate it.
  - A snapshot still stores a locked macro's position. Unlock it and recalls move it again, so locking never loses anything.
- **One lock for all three.** Live's two flags, and the disagreement about them, show what two locks cost.
- **An automated macro follows its lane:** recall, Morph and Vary leave it alone, as a lock does. This is the rule presets already follow: an automated parameter keeps its lane over a preset. Its tooltip says so.

### 18.5 Vary

- **Module > Vary** (Ctrl+Alt+V) moves each mapped macro by a random amount, up to ±15 % of its travel, from where it is. **Vary more** (Ctrl+Alt+Shift+V) moves it up to ±40 %. Both are also on the rack's right-click.
- **Left alone:** locked macros, automated ones, and unmapped ones (shown faded; they move nothing).
- **One undo step each:** Vary again for another, Ctrl+Z to go back, Store snapshot to keep one you like.
- **Not a randomise:** a good sound stays near itself, and several Varies in a row walk away from it a step at a time.
- **While Morph is on,** both are dimmed: Morph sets the macros.
- **The Wavetable's Vary** (W12, its step 3) uses the same two items, keys and amounts, with locks on its sections in place of macros. One MCP tool, `vary_module`, serves both.

### 18.6 In the rack's panel

```
 ┌ RACK ─ Verse to drop ──────────────────────────────────────────────────────────────────┐
 │ MACROS                       SNAPSHOTS                 ● MORPH         CHAINS          │
 │ (Cut) (Wash) (Drive)(Width)  [ Verse      ][ Build •  ]   ( Morph )     ● Main     S   │
 │  30 %  55 %   70 %   0 %🔒   [ Drop       ][ + Store  ]     40 %        ● Dry      S   │
 │ ( 5 ) ( 6 )  ( 7 )  ( 8 )                              [A  Verse ▾]     + Chain        │
 │                                                        [B  Drop  ▾]   MAIN  Vol  Pan   │
 └────────────────────────────────────────────────────────────────────────────────────────┘
```

- **SNAPSHOTS,** 4 columns. It shows once there's a snapshot, as CHAINS shows from the second chain, so a rack without snapshots looks as it does now. Names sit two to a row, four rows, room for eight. "+ Store" takes the next free place until there are eight.
- **MORPH,** 3 columns, shows from the second snapshot: its power dot in the title, the Morph knob centred (the macros' knob, since Morph is a macro of macros), then A and B under it as dropdowns with their letter inside ("A  Verse"), as combos name their parameter inside.
- **Everything is from the panel kit:** the chain list's choice buttons, `Knob`, `ChoiceBox` with its name inside, the section power dot, and the changed mark from the preset bar. The layout lint checks it.
- **Folded,** the strip says what's inside as before ("2 chains"); its snapshots don't fit there (§18.11, "Drawn").

### 18.7 The macro editor

- **MACROS:** each row gains its lock toggle (§18.4).
- **CURVES:** while Morph is on, the selected macro's positions in A and B show as two faint lines marked A and B, with the macro's own line between them, so what Morph does to one macro, and through it to the rack, shows at once.
- **Snapshots aren't edited here:** store, recall and update are on the panel, as a module's presets are in its header.

### 18.8 Factory racks

- **About a dozen, in the Library's `Racks/`,** in three folders: Transitions, Parallel and Instruments. The browser's Racks part lists them, the Library's first. Each has tags, a description and "Segno" as its author.
- **Transitions:** one or two macros moving many parameters, as Transit does, with two snapshots and Morph where a change of section is the point.
  - **Riser:** the Filter's high-pass climbing, the Reverb and Delay coming in, Auto Pan speeding up (Rise).
  - **Wash out:** the Reverb growing and swallowing the sound, a low-pass closing (Wash).
  - **Lo-fi drop:** the Saturator, a low-pass and the Chorus's wobble (Crush).
  - **Filter build:** the Filter opening with resonance, the Phaser (Build).
  - **Echo throw:** the Delay with filtered feedback (Throw, Tone).
  - **Chop:** the Tremolo in time, its depth and rate (Chop).
  - **Verse to drop:** Cut, Wash, Drive and Width, with snapshots Verse and Drop and Morph between them: step 2's showcase.
- **Parallel:** chains side by side.
  - **Parallel compression:** a dry chain beside the Compressor and Saturator (Squash, Blend).
  - **Wide chorus:** a dry chain beside the Chorus and the Utility's width (Width).
  - **Parallel drive:** a dry chain beside the Saturator into the EQ (Drive, Tone).
- **Instruments:** layers.
  - **Layered pad:** a Wavetable pad beside the Sampler's strings from the Library (Air, Strings).
  - **Bass stack:** a Subtractive sub beside a Wavetable growl (Growl, Cutoff).
- **Made as the factory presets are:**
  - Recipes in the repository (`tools/library/racks/recipes.json`): each module by its ID and a factory preset, with what differs; mappings with From and To in the parameter's own unit; snapshots by macro positions.
  - A tool writes each `.segnorack` file. A conformance test checks that the files are what the recipes make, and that each rack loads, plays cleanly within level, and its macros and Morph move what they're named for.
  - The Library build copies them in, and the Library's version goes up.
- **Signed off by the product owner by ear,** as the factory presets are. Names and contents can change in review.

### 18.9 Preview, menus, assistants and files

- **Preview in the browser** (deferred from step 1, §17):
  - An instrument rack plays the presets' phrase (browser.md §7) through itself, rendered offline: each chain's modules in order, summed at its volume and pan, the macros where the file has them.
  - A rack that holds a plugin isn't previewed, because plugins aren't rendered offline; its tooltip says so.
  - Effect racks wait for effect presets' previews (browser.md R9): one design for both, later.
- **The Module menu,** after Add chain:

  | Item | Key | |
  |---|---|---|
  | Store snapshot | | A new snapshot of the macros; also "+ Store", the rack's and a macro's right-click |
  | Update snapshot | | The macros over the current snapshot; dimmed unless changed, or while Morph is on |
  | Rename snapshot… | | The current one; also a double-click on its name |
  | Remove snapshot | | The current one; also its right-click |
  | Morph | | Ticked while on; also MORPH's power dot |
  | Vary / Vary more | Ctrl+Alt+V / Ctrl+Alt+Shift+V | The selected rack's macros (§18.5); later the Wavetable's sections too |

  A and B are chosen on the panel, where they show.
- **Assistants (MCP):**

  | Tool | What it does |
  |---|---|
  | `get_rack` | Adds the snapshots (names and positions), the current one, Morph (on, A, B, position) and each macro's lock |
  | `store_snapshot`, `recall_snapshot`, `remove_snapshot` | As the panel's; `store_snapshot` with `snapshot` updates that one |
  | `set_snapshot` | A snapshot's name, or its positions directly |
  | `set_morph` | On, A and B; its position is the rack's parameter `morph`, set like any other |
  | `set_macro` | Gains `locked` |
  | `vary_module` | Vary or Vary more on a rack (later the Wavetable) |

- **Files and IDs** (permanent once approved, `docs/ids.md`):
  - the rack's parameter `morph`, beside `macro1` … `macro8`, automated as `m<rack id>/morph`;
  - in a project, on the rack: its snapshots, the current one, Morph's A, B and on, and each macro's lock;
  - in a `.segnorack` file: `snapshots` (name and eight positions each), `morph` (on, A, B, position), and `locked` on each macro.
  - The format stays `segnoRack: 1`: nothing has been released, the additions are optional, and a file saved by step 1 loads as it is.

### 18.10 Tests (done when)

| What | Target |
|---|---|
| Snapshots | Store and recall land every unlocked, unautomated macro exactly; locked and automated ones stay; the current one and its changed mark follow; each edit one undo step |
| Morph | At 0, 50 and 100 % the macros are at A, half way and B, and so are the parameters they map, in a render; automated Morph lands within a block of its lane; off keeps the positions; a click on a snapshot while on turns it off; a macro refuses a drag while on |
| Locks and automation | Recall, Morph and Vary never move a locked or automated macro |
| Vary | Only mapped, unlocked, unautomated macros move, each within its amount; one undo step |
| Files | Snapshots, locks and Morph round trip byte for byte; a step 1 file loads as it is |
| Factory racks | The files are what their recipes make; each loads and plays cleanly within level; its macros and Morph move what they name |
| Preview | An instrument rack's phrase sounds as the rack does in a project |
| UI | SNAPSHOTS and MORPH pass the layout lint; every state drawn for review |
| MCP | Every new tool driven in a test, as the UI's edits are |

### 18.11 Order of work within step 2

- **2a. The spike and the UI drawn.**
  - The spike: Morph moving macros moving parameters, in time, saved and undone. Kept as tests. Done (§18.3).
  - The UI drawn for review in every state:
    - one snapshot;
    - three, one of them changed;
    - eight;
    - Morph off, on, and automated;
    - a locked macro;
    - hovering Morph;
    - Vary dimmed while Morph is on;
    - the editor's locks and its A and B lines.
- **Drawn (2a, 2026-10-14),** with the panel kit, from a stub (`tests/integration/RackUiTests.cpp`, "racks step 2, drawn for review"), every state checked by the layout lint. What drawing settled:
  - **MORPH is 3 columns, not 2:** its knob centred over A and B. In 2 columns it sat in the left one beside an empty one.
  - **The folded strip** doesn't add the snapshots: "2 chains · 3 snapshots" was cut to "3 snaps…". What a rack holds is what tells racks apart there.
  - **While Morph is on, no snapshot is lit:** Morph puts the macros where they are, not a snapshot.
  - **Morph's name lights** as a macro's does: hovered, it and the macros it moves are lit.
  - **The editor's locks** show the padlock open while a macro is free and closed, lit, while it's locked. A closed padlock on every row read as eight locked macros.
  - **The kit gained:** `PanelSection::setPowerSwitch` (a section's power for state that isn't a parameter), `Toggle::setCutsText` and `setModified` (a name of your own and its changed mark), `Knob::setLocked`, `Icon::Lock` and `Icon::LockOpen`. The rack's parameters gained `morph` after the macros.
- **2b. Snapshots, locks, Morph and Vary,** with the menus, MCP and files.
- **2c. The factory racks and the preview,** signed off by ear.
- **Hands-on tasks before sign-off**, done unaided as in §5a:
  - store two snapshots and rename them;
  - morph between them with automation over eight bars;
  - lock a macro and Vary the rest;
  - update a snapshot;
  - load a factory rack and use its Morph.

### 18.12 Questions for review

Approved by the product owner (2026-10-14) with every recommendation.

| # | Question | Recommendation |
|---|---|---|
| S1 | **What a snapshot holds** | The eight macros' positions, as Live's variations: small, continuous, blendable. Not whole module states, which would jump on every switch and choice |
| S2 | **How many** | Eight a rack, as there are eight macros |
| S3 | **Recall** | A jump; Morph is the glide |
| S4 | **Keeping one** | As a preset: the current one is lit, a changed mark when the macros move, Update stores over it |
| S5 | **Morph between what** | Two snapshots chosen as A and B. Not a sweep across all of them in order: harder to read, and A and B say exactly what a lane does |
| S6 | **Morph on and off** | A power dot. While on, the macros follow it, as mapped knobs follow their macro; a click on a snapshot turns it off and recalls |
| S7 | **Editing while Morph is on** | The macros can't be dragged; turn Morph off, recall, change, Update. Not Flux's way, where an edit moves Morph to the nearer end: an edit that moves another control is confusing |
| S8 | **Locks** | One lock a macro for recall, Morph and Vary; a snapshot still stores a locked macro |
| S9 | **Automated macros** | Their lane comes first: recall, Morph and Vary leave them alone, as presets do |
| S10 | **Vary** | Two items with keys, Vary (±15 %) and Vary more (±40 %), shared with the Wavetable later. Not one item with an amount kept somewhere else |
| S11 | **When SNAPSHOTS and MORPH show** | From the first snapshot and the second, as CHAINS shows from the second chain, so a plain rack stays plain |
| S12 | **Factory racks** | The twelve above, in Transitions, Parallel and Instruments, made from recipes and signed off by ear |
| S13 | **Preview** | Instrument racks now; effect racks later, with effect presets' previews |
| S14 | **Building it** | The spike and the UI drawn for review first (2a), then built (2b, 2c) |
