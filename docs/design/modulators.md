# Modulators: modules that move other modules' knobs

A **modulator** is a module that makes no sound. It sits on a track and moves other parameters there: a filter's cutoff, a plugin's knob, a rack's Morph, the track's volume. The first five are the **LFO**, **Random**, **Envelope**, **Follower** and **Steps**. A modulator routed to several targets gives each its own copy by default, so one Random moves eight knobs eight different ways. Their randomness is random: it rolls again every time it plays, unless you keep a roll you liked.

**Status:** approved by the product owner (2026-10-15), every recommendation of §21, with modulators anywhere in the chain instead of a zone of their own (§6, M2, revised after the review); ADRs 0061 and 0062 accepted. Step 0 done the same day: the spike passed (§22), and the UI drawn in every state and approved (§23). Step 1 built the same day (§24) and signed off by the product owner (2026-10-15), with how a choice shows a modulator (B87) approved and built after. M3 P5, the product owner's priority of 2026-10-15, moved up from M4, with [ADR 0061](../decisions/0061-modulators-move-any-parameter-on-their-track.md) and [ADR 0062](../decisions/0062-random-rolls-again-each-time-it-plays.md). It builds on [ADR 0060](../decisions/0060-motion-comes-from-modulation-modules.md) (motion comes from modulation modules). Nothing is built until the product owner says go, and the UI is drawn in every state and approved first (§19).

What the product owner asked (2026-10-15):
- "Let's start the modulation modules", with a name people understand: "CV tools is great for the people who know, but most people will not know what that is in reference to."
- GRM Tools Atelier's modulation as the inspiration that brings novelty: "they do a fantastic job."
- Each target its own copy (Atelier's "polyadic") **by default where it makes sense, with a switch to turn it off**, and visuals to match: several traces over each other, like an infinity mirror, while it's on.
- **The first set: LFO, Random, Envelope, Follower and Steps** (approved); more later.
- Randomness that's random: "its random, thats the whole point". Weighed both ways and agreed: chance rolls again each time it plays, and Keep holds a roll you liked (§13, ADR 0062).

## 1. What's there now

- **Modules with modulation of their own,** which they keep (ADR 0060):
  - the Wavetable: 4 envelopes, 8 drawable LFOs, 2 random sources, 8 macros, a 32-route matrix;
  - the Filter: an LFO and an envelope follower;
  - the Delay, Chorus, Phaser & Flanger and Auto Pan & Tremolo: A2's LFOs;
  - the Subtractive: an LFO.
- **The kit that shows modulation is built** ([device-panels.md](device-panels.md), "Modulation"):
  - rings outside a modulated knob's arc;
  - a source's handle, a named chip, dragged onto a knob;
  - Modulate with ▸ on a knob's right-click;
  - hovering a source lights its targets.

  Its word for a connection is a **route**, and for what's moved a **target**.
- **Racks** move many parameters from one macro (From, To, Curve), and Morph blends snapshots. Today they move by hand and by automation only (ADR 0060).
- **Nothing else moves one module's parameter from outside it,** apart from automation and a rack's macros.
- **A2's `ModulationLfo`** (`libs/dsp`): sine, triangle, saws, square, sample-and-hold and wander; free in Hz or synced, locked to the song's position while it plays.
- **tracktion_engine has modifiers** on every track and in every rack (`Track::getModifierList`, `RackType::getModifierList`): LFO, step, random, envelope follower, MIDI tracker and breakpoint. A modifier is assigned to any automatable parameter, ours or a plugin's, with an offset, a value and a curve. Two limits (`tracktion_AutomatableParameter.cpp`):
  - every assignment reads the modifier's one value;
  - values are read once per audio block.

## 2. How others do it (October 2026)

| | What | Where they sit | Connecting | What stands out |
|---|---|---|---|---|
| **Live 12** | LFO, Shaper, Envelope Follower (audio effects); Envelope MIDI, Shaper MIDI, Expression Control (MIDI effects); all Max for Live | In the chain, like effects | Map, then click a parameter; up to 8 maps each. Modulation mode moves around the knob's value (bipolar or unipolar, by an amount); Remote mode takes the knob over between Min and Max | Familiar. A modulator's place in the chain means nothing, except where a follower listens ([Ableton](https://www.ableton.com/en/live-manual/12/max-for-live-devices/)) |
| **Bitwig Studio** | Dozens: LFOs; envelopes (ADSR, AHDSR, 4-Stage, Segments); Steps, Random, Sample and Hold; Audio and Note Sidechain; Keytrack, Expressions; Math, Macro, Buttons, XY… | Slots on any device, on the track or on the project | Drag a modulator's arrow onto a knob, then set the amount on the knob; per voice inside instruments | The broadest set ([Bitwig](https://www.bitwig.com/en/bitwig-studio/devices/modulators.html)) |
| **Cubase 14** (2024) | LFO, Envelope Follower, Shaper, Macro Knob, Step Modulator, ModScripter | Up to 8 a track, in a Modulators tab of the Lower Zone | Assigned to parameters; modulators move each other; LFO, Shaper and Steps run free, synced to the project or retriggered by MIDI | Modulation on every track, in a place of its own ([Attack Magazine](https://www.attackmagazine.com/technique/tutorials/cubase-pro-14-modulators-redefining-creative-automation/), [MusicTech](https://musictech.com/reviews/digital-audio-workstations/steinberg-cubase-14-review)) |
| **GRM Tools Atelier** (2024; v1.0.2, 2026) | Agitation (random and cyclic movement), Peak (envelopes with repeats and chance), Control (a fader that smooths, delays and loops your movement) | A row under the processors | A wire dragged from a modulator's handle, with a range of −200 to +200 % from where the parameter is set. Wires add up and the sum is clamped. Modulators move modulators, loops refused; a switch flips while the sum is negative | **Polyadic:** each target gets its own engine, with the same settings and its own phase and random state, and a switch moves them together. A dot on every slider at its moving value. The same words in every modulator: Ease, Slant, Jitter, Chance ([docs](https://mqtthiqs.github.io/at-doc/atelier/modulation/), [Sound On Sound](https://www.soundonsound.com/reviews/grm-tools-atelier)) |

**What they share:** a modulator moves any parameter around where it's set, by an amount per connection. **What Atelier adds:**
- few modulators, each deep, in one vocabulary;
- a copy for each target;
- modulation you see moving on every control.

## 3. Principles

1. **Few, deep, one vocabulary.** Five modulators that each do a lot, in the words the Wavetable already uses (§10). Learn one, and you know them all.
2. **Each target its own,** by default where it means something (Per Target, §8), with one switch to move them together.
3. **A modulator moves the knob around where you set it,** by an amount per route. A macro sets a knob; a modulator moves it. The knob stays yours to set.
4. **Built once, on what's there.** A modulator is a module: the module grid and its lint, the rings and handles, Modulate with ▸, presets, the browser.
5. **Seen moving.** Every moved control shows where it is now, and every modulator shows its output, one trace per target.
6. **Random is random** (ADR 0062). It rolls again each time it plays, and Keep holds a roll you liked.
7. **In the DAW.** A modulator moves other modules, which a plugin in another host can't do. The plugins keep the modulation inside each module ([device-suite.md](../devices/device-suite.md)).

## 4. The name: Modulators

- **"Modulators"** is the browser's part, and each one is **a modulator** (Module ▸ Add modulator ▸).
  - Live, Bitwig and Cubase all use the word, so it's what people search for and what tutorials call them.
  - It's generic ([ADR 0052](../decisions/0052-generic-names-are-free.md)).
- **Not "Modulation":** it names what they do rather than the things in the folder. It's also already a section of the Delay, Chorus and Subtractive, and a page of the Wavetable.
- **Not "CV tools":**
  - it's jargon from modular synths;
  - it's the name of an Ableton pack ([ADR 0008](../decisions/0008-naming-and-trade-dress.md));
  - sending CV to hardware is a non-goal (vision.md), so the name would promise something that isn't there.
- **Not "Motion":** warmer, and ADR 0060's own word, but nobody searches for "motion" when they want an LFO.
- **"Random" is the modulator's** (M16). The suite plans a MIDI effect under that name for M6 (pitches by chance in a scale). It gets another name when its SPEC is written, so one word means one thing.

## 5. What a modulator is

- **A module that makes no sound.** Like any module, it has a panel on the module grid, presets and a power button. It folds, and it's renamed, moved, copied and removed, each in one undo step.
- **It moves any automatable parameter on its own track.** Those are its targets:
  - the parameters of every module, ours and plugins;
  - a rack's macros, its Morph, and its chains' volume and pan;
  - the track's volume, pan and sends;
  - other modulators' parameters (§8).
- **Return tracks and the master have modulators too.** Another track's parameters are out of reach (M8); a Follower can listen to another track instead (§12.4).
- **Its own parameters are parameters:** automated, mapped to a rack's macro, MIDI-learned, moved by another modulator.
- **Depth** (0–100 %) scales all its routes at once, so a wobble that comes in over eight bars is one automation lane.

## 6. Where they sit: anywhere in the chain

The product owner (2026-10-15): "Why not just let the user put them wherever? … If we allow all positions like everything else then it's also easy to make them group in racks." This replaces a zone of their own at the start of the chain, first proposed here (M2).

```
 ┌ ∿ LFO ─────────┐ ┌ WAVETABLE ───┐ ┌ ∿ RANDOM ────────┐ ┌ FILTER ──┐ ┌ ∿ FOLLOWER ┐ ┌ COMPRESSOR ┐  +
 │ ∿∿∿  ⤧ LFO · 2 │ │   (rings)    │ │ ≋≋≋ ⤧ Random · 4 │ │ (rings)  │ │ ⤧ hears    │ │  (rings)   │
 │ Rate Slant …   │ │              │ │ Rate Smooth …    │ │          │ │   here     │ │            │
 └────────────────┘ └──────────────┘ └──────────────────┘ └──────────┘ └────────────┘ └────────────┘
```

- **A modulator goes anywhere in the chain, like any module.** It's added, dragged, reordered, selected, copied and grouped into racks the same way. One rule for every module.
- **Where it sits doesn't change what it moves.** An LFO before a filter moves it the same as one after it. Two things make that clear:
  - a modulator looks like one: its header carries the Modulator icon (§14), and it has no level meters, since no sound passes through it;
  - its tooltip says so: "Where a modulator sits doesn't change what it moves."
- **The exception is the Follower, which listens where it sits** (§12.4), as Live's does: after the EQ, before the compressor.
- **Put it next to what it moves.** An LFO beside the filter it drives is a short drag, and you see what belongs together.
  - Modulate with ▸ New ▸ on a control puts the new modulator just before that control's module.
  - Module ▸ Add modulator ▸ puts it after the selected module, or at the end of the chain.
  - Dragging a handle to a module further along scrolls the chain at its edges, as the arrangement scrolls while dragging.
- **It hears the track's notes wherever it sits,** before or after the instrument, for its Notes triggers. (Live's modulators that follow notes must sit before the instrument.)
- **Modulators don't count for what a chain starts with.** An LFO followed by a Wavetable is still an instrument chain (racks.md §3).
- **Each modulator's panel is on the module grid,** six rows like every module's. The layout lint, folding, presets and the header are the ones every module has. Folded, it's the usual narrow strip, and its handle stays on it.

## 7. Routes

### 7.1 Making one

There are three ways, each enough on its own, as in the kit ([device-panels.md](device-panels.md), "Modulation"):
1. **Drag the modulator's handle onto a target.**
   - The handle is the named chip in its display's top-right corner, with the number of its routes beside the name ("⤧ LFO · 3").
   - While it's dragged, every control it can move shows a faint ring over its whole sweep: on the modules, in the track's header and in the inspector. The one under the pointer lights.
   - Letting go makes a route at 50 %. Escape cancels.
2. **Right-click any control: Modulate with ▸.** It lists the track's modulators by name, then **New ▸** LFO, Random, Envelope, Follower, Steps. A new one is added just before the control's module and routed in one undo step.
3. **Drop a modulator's preset from the browser onto a control.** The modulator is added with that preset and routed to the control (§14).

### 7.2 How much

- **An amount per route,** −200 … +200 %, 50 % for a new one:
  - **Both ways** (an LFO, Random, Steps): at 100 %, the modulator moves its target half its range up and half down.
  - **One way** (an Envelope, a Follower, or any with Both Ways off): at 100 %, it moves the target across its whole range.
  - **A negative amount** turns it the other way: a Follower at −100 % ducks.
  - **Over 100 %** reaches the ends sooner and stays there: a sine becomes nearly square.
- **Routes add up:** several modulators on one target, or one routed to it twice, are summed. The sum is clamped to the target's ends.
- **Ways to set it:**
  - drag the ring, as now: up and right for more, Shift for finer steps; it sets the route of the modulator last hovered;
  - type it in the modulator's route list (§7.4);
  - the control's right-click, which lists its routes with their amounts.

### 7.3 Around the knob's own value

- **The knob keeps its own value.** You set it, automation draws it, a macro sets it; the modulation moves around it. The ring and the moving value move along with it.
- **A modulated knob can still be dragged,** unlike a macro-mapped one (racks.md §4). Dragging it sets its own value.
- **Saved values leave modulation out,** as with a macro's mapping (the patch in racks.md §17). Saving, undo and presets keep what you set.
- **Switches and choices move as knobs with steps:** the value plus the modulation, snapped to the nearest step (M7).
  - A switch that's off turns on while an Envelope routed at 100 % is above half.
  - A choice of five steps through them.
  - Atelier instead flips a switch while the sum is negative. Snapping works the same way for one-way and both-way modulators.

### 7.4 Seen

- **Rings** on every modulated knob, as now. **Bars and faders** show the range as a band along them, in the same colours. **Switches and choices** (dropdowns, segmented buttons) a line along their bottom edge, full while the modulation has them on another value than the one set; in segmented buttons the choice it's on now is outlined (B87).
- **Where it is now:** a dot on the ring (a tick on a bar) at the target's moving value, on every moved control. This is new in the kit, and it never covers the arc's own value marker.
- **Hovering a handle** lights its targets and dims the rest, as now. It also draws a line from the handle to each target, in the modulation colour, over the panels. Lines show only on hover, since lines shown all the time would cross every panel.
- **Hovering a modulated control** lights the handles of the modulators that move it, as now.
- **The route list** is in a modulator's right-click and its header's menu. Each route ("Filter · Cutoff  40 %") has its amount, Show (selects and scrolls to the target) and Remove.

## 8. Each target its own: Per Target

The Wavetable's LFOs and random sources have **Per Note**: on, each note has its own; off, one moves every note together. A modulator has the same switch for what it moves: **Per Target**.

- **On:** each target gets its own copy of the modulator, with the same settings and its own phase, random path and rolls of chance.
- **Off:** one modulator moves every target together.
- **What it means for each modulator, and its default** (M4):

| Modulator | With Per Target on | Default |
|---|---|---|
| **Random** | Each target wanders its own way | On |
| **Envelope** | Each target rolls its own Chance; at 100 % Chance they're all the same | On |
| **Steps** | Each target rolls its own step chances | On |
| **LFO** | The copies' phases spread evenly over a cycle: two targets half a cycle apart, four a quarter. One LFO on eight EQ bands moves as a wave across them | Off: an LFO on a filter and a volume is usually meant to move them together |
| **Follower** | – (a level is a level) | No switch |

- **A modulator moved by another** with Per Target on: each copy is moved by the other modulator's own copy (Atelier's rule). Random on an LFO's Rate gives each of the LFO's targets its own rate.
- **Loops are refused.** A route that would make a modulator move itself, directly or through others, can't be made: the target doesn't light, and the tooltip says why.

## 9. Traces over each other

- **Every modulator's display shows its output moving:**
  - the LFO, Envelope and Steps show their shape (the cycle, the stages, the steps), with a dot where it is now;
  - Random and the Follower show their last few seconds as a trace.
- **With Per Target on, there's one trace for each target,** laid over each other: the first at full strength, each next one a step fainter. Several targets read as echoes of one movement, the infinity-mirror look the product owner described in Atelier. With it off, there's one trace.
  - The fading steps come from the theme (a new `Theme` value for the echo step), in the modulation colours, so no view mixes a colour of its own.
  - Past eight targets, only the first eight are drawn; the handle's count says how many there are.
- **Each target's dot moves on its own,** so the copies are seen where they land as well as in the display.
- **Drawn in step 0** (§19) and approved before it's built. Atelier's own screens are compared then.

## 10. One vocabulary

The words are the Wavetable's wherever it has them, and Atelier's where it doesn't. Each word means one thing everywhere, with the same tooltip.

| Word | Means | In |
|---|---|---|
| **Rate** | How fast, in Hz, while Sync is off | LFO, Random, Steps |
| **Sync** | Locked to the song's tempo, in note values: Off, 1/64 … 8 bars, dotted and triplet | LFO, Random, Steps, the Envelope's times |
| **Mode** | How the LFO runs: Free runs on; Retrigger restarts it with each note; Envelope plays its shape once per note | LFO |
| **Phase** | Where in its shape it starts | LFO |
| **Smooth** | Rounds off corners, so steps glide | LFO, Random, Steps |
| **Slant** | Time rising against time falling: 0 equal; toward −100 a quick rise; toward +100 a quick fall | LFO, Random |
| **Jitter** | How much the time between changes varies | Random, Steps |
| **Chance** | How likely each trigger or step is to happen | Envelope, Steps |
| **Fire** | A button that fires it now. It's a parameter, so anything that moves parameters can press it (§11) | Envelope, Steps |
| **Depth** | Scales all its routes | All |
| **Both Ways** | On: moves its targets up and down; off: up only (a negative amount turns it down) | LFO, Random, Steps |
| **Per Target** | Each target its own copy (§8) | LFO, Random, Envelope, Steps |
| **Keep** | Holds the roll you heard (§13) | Random, Envelope, Steps |
| **Attack, Hold, Decay, Sustain, Release** and their curves | As the Wavetable's envelopes | Envelope |

## 11. Time and triggers

- **Synced modulators follow the song.** While it plays, they're where they'd be at that bar, after a jump too (A2's rule). Stopped, they run on at the tempo, so held notes still move.
- **Free ones restart at their Phase when playback starts,** so what you hear from a place is what an export renders from it (randomness aside, §13). Stopped, they run on.
- **What starts them:**
  - **Notes:** each note played on the track, from clips or a keyboard. The LFO's Mode Retrigger restarts on each note; the Envelope fires on each note (or, with Legato, only on a note that starts while no other is held).
  - **Beat:** every bar or note value, on the song's grid (the Envelope; a restart for Steps).
  - **Fire:** the button on the panel. A click fires it. Because it's a parameter, a MIDI-learned pad, the Push, automation or **another modulator routed to it** fires it each time it rises past half:
    - Steps routed to an Envelope's Fire fire it on their steps;
    - a Follower routed there fires it on loud hits, its Gain setting how loud counts.

## 12. The five

Each modulator's controls are listed here with their ranges and defaults; step 0 lays out the panels (§19). Every one has Depth, and Per Target and Both Ways where §8 and §10 say.

### 12.1 LFO

Repeating movement.

| Control | Range | Default |
|---|---|---|
| Shape | Sine, Triangle, Saw up, Saw down, Square, or drawn (the Wavetable's LFO shape editor, shared) | Sine |
| Rate / Sync | 0.01 … 40 Hz / Off, 1/64 … 8 bars | 1 Hz / Off |
| Mode | Free, Retrigger, Envelope | Free |
| Phase | 0 … 360° | 0 |
| Slant | −100 … +100 % | 0 |
| Smooth | 0 … 100 % | 0 |
| Fade In | 0 … 10 s after each retrigger | 0 |

- The random shapes (sample-and-hold, wander) belong to Random, so randomness lives in one place and follows ADR 0062.
- Mode Envelope with a drawn shape covers Live's Shaper MIDI: a drawn curve played once per note.

### 12.2 Random

Wandering movement, after Atelier's Agitation. Every Rate, it picks a new value and moves to it.

| Control | Range | Default |
|---|---|---|
| Rate / Sync | 0.01 … 40 Hz / Off, 1/64 … 8 bars | 2 Hz / Off |
| Smooth | 0 % jumps (sample and hold) … 100 % glides all the way | 100 % |
| Slant | −100 … +100 % | 0 |
| Cyclic | 0 %: each value anywhere … 100 %: up and down in turn | 0 |
| Jitter | 0 … 100 % | 0 |
| Freeze | On, it stops where it is | Off |
| Keep | §13 | Off |

- Per Target is on by default: eight targets, eight paths, one modulator.
- Freeze is a switch, so Steps or an Envelope routed to it make Random stop and go (Atelier's tip, from its Running switch).

### 12.3 Envelope

A rise and fall each time it's fired: the Wavetable's envelope, with the extras of Atelier's Peak.

| Control | Range | Default |
|---|---|---|
| Trigger | Notes, Beat (every 1/64 … 8 bars), None (Fire alone) | Notes |
| Legato | Fires only on a note that starts while no other is held | Off |
| Delay | 0 … 10 s, or synced | 0 |
| Attack, Hold, Decay, Release | As the Wavetable's (ms, or synced) | 2 ms, 0, 300 ms, 200 ms |
| Sustain | 0 … 100 % | 0 |
| Curves | Attack, Decay and Release curves, −100 … +100 % | As the Wavetable's |
| Mode | Normal; Loop repeats Attack to Decay while held; One-shot ignores the release | Normal |
| Repeat | 0 … 16 more times after each firing, back to back: ratchets | 0 |
| Chance | 0 … 100 % that a firing plays | 100 % |

- The display is the Wavetable's draggable envelope, shared.
- Its times can be synced, so a one-bar swell is one bar at any tempo.
- **The Wavetable's ADSR words rather than Atelier's Time and Slant** (M9). The Wavetable's and the Subtractive's envelopes already use them, and everyone who has met a synth knows them. Atelier's best ideas come along: Repeat, Chance and Delay.

### 12.4 Follower

Follows how loud something is.

| Control | Range | Default |
|---|---|---|
| Listen | Here: the sound where it sits in the chain; or another track, heard after its modules and fader, as a sidechain hears it ([ADR 0025](../decisions/0025-sidechain-routing.md)) | Here |
| Band | Off, or Low and High: listen to only that part of the sound (the kick's lows, a hi-hat's highs) | Off |
| Gain | −24 … +24 dB | 0 dB |
| Attack | 0.1 … 500 ms | 5 ms |
| Release | 1 … 5000 ms | 150 ms |

- The display shows the level it hears as a trace. With Band on, it also shows the band over a small spectrum, drawn as the Spectrum draws.
- Routed at a negative amount, it ducks: a pad that dips with the kick, from any module, in two drags.
- Routed to an Envelope's Fire, it fires the Envelope on loud hits.
- Another track is chosen as a sidechain source is. Any track whose sound depends on this one is left out, so routing can't loop.
- **Listening where it sits** needs the Follower in the audio path: tracktion's modifiers hear a track only before or after all its modules, so a pass-through point of our own measures the sound there (§17).

### 12.5 Steps

A rhythm of values.

| Control | Range | Default |
|---|---|---|
| Steps | 1 … 32 values, drawn as bars on the display (a drag draws, as on the velocity lane) | 16, all 0 |
| Rate / Sync | 0.01 … 40 Hz / 1/64 … 1 bar a step | Sync 1/16 |
| Advance | Time; Notes (a step for each note played); Fire (a step each time it's fired) | Time |
| Smooth | 0 % jumps … 100 % glides from step to step | 0 |
| Jitter | 0 … 100 % | 0 |
| Chance | Per step, 0 … 100 %, on a second row under the bars. A step that doesn't happen sits at 0 | 100 % |
| Keep | §13 | Off |

- Notes or the beat restart it (§11). A restart goes back to the first step; Advance moves one step on.
- Advance on Fire, with a Follower routed to Fire, takes a step on each drum hit.

## 13. Randomness ([ADR 0062](../decisions/0062-random-rolls-again-each-time-it-plays.md))

The product owner (2026-10-15) said a chance that comes out the same every time it plays "kind of defeats the purpose of using randomness." Both sides were weighed. Rolling fresh loses three things: exports that match what you heard, exact bounces, and a song that sounds the same tomorrow. The decision is ADR 0062:
- **Random rolls again each time it plays:** each time playback starts, each time the loop comes round, and on each lap of a looped clip. This covers:
  - in the modulators: Random's values, the Envelope's Chance, Steps' chances, and both Jitters;
  - in notes: chance and velocity spread ([piano-roll.md](piano-roll.md) §26).
- **Keep holds the roll you heard.** Segno remembers the roll of what's playing, or of what played last.
  - Keep stores that roll on the modulator, which then plays it every time.
  - Let it roll goes back to fresh rolls.
  - A kept modulator shows it: Keep lit on its panel, and a lock on its handle.
  - Each is one undo step. Song ▸ Keep all as played keeps every roll in the song, on clips and modulators.
- **Export and bounce:** what's kept plays as kept, and the rest rolls fresh.
  - When the song leaves anything to chance, the export panel says so ("2 clips and 1 modulator play by chance: each export is different"), with Keep all as played beside the line.
  - Bounce in place keeps one roll as audio, as freezing does in Live.
- **Tools that write keep their seeds:** Humanize, Melody and the piano roll's other tools, and a rack's Vary. Their results are written where you see them, and a seed only makes the same settings give the same notes while you adjust them.
- **Modules' own random sources are unchanged for now.** A2's sample-and-hold and wander follow the song's position, and the Wavetable's random sources and phases are seeded per note. Whether they follow is open (B86).
- **How a roll works:**
  - A roll is a number chosen when playback starts.
  - A random value is a hash of the roll, the modulator and where it is: the step it's on when synced, or the time since playback started when free. While the transport loops, the loop's lap goes into the hash too.
  - No random state runs, so a kept roll renders exactly as it played.

## 14. Browser, presets, racks, copies

- **The browser's Modulators** is a part of its own, after Audio effects, with its icon (a new `Modulator`: a wave with a dot) in the rail and the open browser ([ADR 0059](../decisions/0059-racks-are-a-browser-part-and-every-part-has-its-icon.md)). Each modulator is a folder of its presets, as modules are ([ADR 0046](../decisions/0046-one-browser-modules-are-folders.md)).
  - **Dropped in the chain,** it goes where it's dropped, like any module; dropped on a track, at the end of its chain.
  - **Dropped on a control,** it's added and routed to that control at 50 %, in one undo step.
- **Presets** ([ADR 0044](../decisions/0044-presets-are-files.md)) keep a modulator's settings, never its routes: routes belong to the track. About eight factory presets each, about 40 in all (M17), for example:
  - a slow sweep, a tremolo, a breathing random;
  - a ratchet, a pluck;
  - a sidechain duck;
  - a trance gate, an octave ladder.
- **Racks hold modulators like any module** (M13). Group into rack takes them along, and a rack's chain takes one dropped into it.
  - A modulator in a rack moves only what's in the rack: its modules, macros, Morph and chain rows. A rack is saved and loaded whole, so grouping drops a modulator's routes to anything outside, with a toast to undo.
  - It's saved in the rack's `.segnorack`, so a riser rack keeps its LFO on Morph.
  - A track's modulators still move a rack's macros, Morph and modules from outside it.
  - The engine has a modifier list in every rack.
- **Copies and removals:**
  - Duplicating a track, or copying modules together with their modulators, keeps the routes among the copies.
  - A module moved or copied to another track leaves its routes behind, with a toast to undo.
  - A module grouped into a rack keeps the routes to it from the track's modulators.
  - Removing a modulator removes its routes, and removing a target removes the routes to it, in the same undo step.

## 15. Menu bar and keys ([ADR 0040](../decisions/0040-a-menu-bar-and-everything-by-mouse.md))

| Where | Item | |
|---|---|---|
| Module | Add modulator ▸ LFO, Random, Envelope, Follower, Steps | `module.addModulator.lfo` … `.steps` (Add LFO …): after the selected module, or at the end of the selected track's chain. Also the chain's "+" and the browser |
| Module | Keep as played / Let it roll | `module.keepRoll`: for the selected modulator. Dimmed unless it's a Random, Steps or an Envelope (as its panel's Keep). Also Keep on its panel |
| Module | Remove routes | `module.removeRoutes`: the selected modulator's. Also its right-click |
| Song | Keep all as played / Let all roll | `song.keepAllRolls`: every modulator and every clip with chance in the song. Also beside the export panel's line |
| Notes | Keep chance as played / Let chance roll | [piano-roll.md](piano-roll.md) §26 |
| A control's right-click | Modulate with ▸ (the track's modulators; New ▸ the five); its routes, each with its amount and Remove | After its automation items, as now |

There are no new shortcuts. Copy, paste, duplicate, delete, rename and fold are Edit's and the module's, as for every module.

## 16. Assistants (MCP, [ADR 0021](../decisions/0021-mcp-server.md))

| Tool | Arguments | Returns |
|---|---|---|
| `add_module` | `track`, `type`: `mod-lfo`, `mod-random`, `mod-envelope`, `mod-follower`, `mod-steps` (a modulator is a module) | Its id and parameters |
| `set_parameters`, `remove_module`, `set_module_on` | As for any module | |
| `get_modulators` | `track` | Each modulator's type, settings and Keep, and its routes (target, amount) |
| `route_modulator` | `modulator`; `target` (a module and parameter, a rack's macro or Morph, or the track's `volume`, `pan`, `send n`); `amount?` (50) | The route. Refused, saying why, for a loop or another track |
| `set_route`, `remove_route` | `modulator`, `target`, `amount` | |
| `keep_roll` | `modulator` or `clip`, `keep` (true or false); or `song: true` for everything | What's kept |
| `list_module_types` | `kind`, now also `modulator` | |

`reroll_chance` stays, for a kept clip. A test drives each tool through MCP.

## 17. Model and engine

- **Each modulator's signal is a pure function in `libs/dsp`** (JUCE-free, `[fast]` tests): the shapes, Random's values from its roll, the envelope's stages, the steps. The displays draw from the same functions, so the UI never reimplements DSP.
- **In the engine,** each modulator is a tracktion modifier of our own (a `te::Modifier` subclass) on the track's modifier list, and later on a rack's.
  - It's real-time safe, and checked for allocations.
  - Routes are modifier assignments, with their amounts mapped onto tracktion's offset and value.
- **Per Target needs a state per route,** which tracktion's assignments don't have: each one reads the modifier's single value. The spike (§19) chooses between:
  - one engine modifier per route, kept in step behind one modulator, with no patch;
  - a small tracktion patch that lets a modifier answer for each assignment.
- **Block steps:** tracktion reads modifiers once per audio block.
  - Our modules smooth their parameters as their tables say, so a fast LFO glides on them. A plugin might step.
  - The spike measures a 20 Hz LFO on a cutoff, ours and a plugin's, at 512 samples. If a plugin steps audibly, its block is split at the modulators' rate, or the limit is documented.
- **Where a modulator sits:** tracktion keeps modifiers in a list of their own, apart from the chain. The spike chooses between:
  - each modulator as a pass-through plugin in the chain that owns its modifier, so every chain edit (move, copy, group) treats it as a module;
  - modifiers kept in their list, with their place kept in our chain model.
- **The Follower listens where it sits:** a pass-through point of our own measures the sound there and hands the level to its modifier. Another track's sound comes through the sidechain routing.
- **Kept with the project:**
  - the modulators in the track's modifier list, with their settings and Keep's roll;
  - the routes in the targets' assignments.

  A saved parameter value leaves modulation out (the racks' patch).
- **`engine::Session`** adds, routes and removes modulators. The UI and MCP both go through it.
- **IDs** (permanent, `docs/ids.md`): the types `mod-lfo`, `mod-random`, `mod-envelope`, `mod-follower` and `mod-steps`. They have a prefix of their own, so no device ID can collide with them (a "random" MIDI effect is planned). Parameter IDs are listed as they're built.

## 18. Not now

- **Expression and Gesture,** approved for later:
  - Expression turns what's played into movement: velocity, pitch, wheels, pressure, MPE, CCs, each through a curve;
  - Gesture is Atelier's Control: a knob or pad you play, smoothed, delayed and looped.
- **Sections** (movement that follows the arranger track, such as opening across every build-up), **Combine** (mix, multiply or remap two signals), and an **XY pad**.
- **Per-voice modulation** stays inside instruments (the Wavetable's own). A modulator moves a module's parameter for all its voices.
- **Routes to another track's parameters.**
- **A route's amount as an automatable parameter.** Depth fades a whole modulator in.
- **Modulation at audio rate.**
- **Modulators on the Push,** which come with its support (proposed for M4).
- **Modulators in the plugin builds.**

## 19. Steps

- **Step 0, the spike.** One modulator of our own on tracktion's modifiers moves one of our parameters, a plugin's, a rack's Morph and the track's volume. The spike also:
  - places it anywhere in the chain, groups it into a rack, and has a Follower listen mid-chain (§17);
  - tries Per Target both ways (§17) and measures the block steps;
  - checks undo, save and load;
  - renders a kept roll as it played, and shows passes rolling fresh.
- **Step 0, drawn,** with the real panel kit from a stub. Every state below is checked by the layout lint and approved before building, as for racks (racks.md §5a):
  - modulators among the modules: beside a filter, before the instrument, inside a rack;
  - each of the five panels, unfolded and folded;
  - Per Target on with four targets (the traces), and off;
  - a handle being dragged (targets ringed, the chain scrolling), and a route made;
  - hovering a handle (the lines), and hovering a modulated knob, bar and switch;
  - the route list, and Keep on;
  - all at standard and compact widths, zoomed in.
- **Step 1:**
  - the five modulators, anywhere in the chain and in racks;
  - routes, Per Target and its traces, triggers;
  - Keep and fresh rolls, with the piano roll's chance (piano-roll.md §26);
  - the menus, the browser part, presets, MCP.

Before sign-off, the product owner does these tasks, as with racks:
- put an LFO on a filter's cutoff and set its amount on the ring;
- route one Random to four knobs, watch four traces, then turn Per Target off;
- fire an Envelope from notes, with Repeat;
- duck a pad with a Follower listening to the kick;
- step a filter in time with Steps;
- keep a Random's roll you liked, and export it.

## 20. Tests (done when)

- **`[fast]`:**
  - each function:
    - the LFO's shapes, Slant and Phase at known points, and synced, locked to the song after a jump;
    - Random within its range, held at Smooth 0, continuous at 100, alternating at Cyclic 100;
    - the Envelope's stages within a sample, Repeat's count, Chance over many firings;
    - the Follower's attack and release times;
    - Steps' values, glide and chances;
  - routes: amounts summed and clamped, both ways and one way, negative amounts; a switch turning at half; a choice stepping;
  - Per Target: Random's copies differ, the LFO's phases spread evenly, and all are the same with Per Target off;
  - rolls: one roll gives the same output twice, two rolls differ, and a kept roll reproduces the pass it came from;
  - loops refused.
- **Integration:**
  - a modulator moves a parameter of ours, a plugin's, a rack's Morph and the track's volume;
  - add, route, amount, remove and Keep are one undo step each;
  - a project saves and opens the same;
  - a kept roll renders as it played, sample for sample, and fresh passes differ;
  - no allocation in a modulator's processing;
  - the frame budget holds with 8 modulators and 32 routes moving.
- **Panels:** the layout lint for each modulator's panel; screenshots reviewed zoomed in, at standard and compact widths.
- **MCP:** every tool in §16 driven in a test.
- **Manual:** the hands-on tasks (§19), in `docs/manual-tests/M3.md`.

## 21. Questions and recommendations

| # | Question | Recommendation |
|---|---|---|
| M1 | **The name** | Modulators; each one a modulator (§4) |
| M2 | **Where they sit** | Anywhere in the chain, like any module; a modulator looks like one, and where it sits doesn't change what it moves, except where the Follower listens (§6). Revised after review: the product owner, 2026-10-15. A row under the chain (the first sketch) and a zone at the start of the chain (the first draft) were dropped |
| M3 | **The first set** | LFO, Random, Envelope, Follower, Steps (approved 2026-10-15); Expression and Gesture next |
| M4 | **Per Target's defaults** | On for Random, Envelope and Steps. Off for the LFO, whose copies spread their phases when it's on. No switch on the Follower (§8) |
| M5 | **The traces** | One per target, over each other, each a step fainter (§9) |
| M6 | **Amounts** | Per route, −200 … +200 %, around the knob's value, summed and clamped; Depth for the whole modulator (§7.2) |
| M7 | **Switches and choices** | Moved as knobs with steps, snapped (§7.3) |
| M8 | **What it can move** | Anything on its own track, other modulators included. Another track is reached through its sound (the Follower), not its parameters |
| M9 | **The Envelope's controls** | The Wavetable's ADSR words, with Delay, Repeat and Chance from Atelier (§12.3) |
| M10 | **Where the Follower listens** | Where it sits in the chain, or another track |
| M11 | **Randomness** | ADR 0062: it rolls again each time, and Keep holds a roll (§13) |
| M12 | **Exporting with things left to chance** | Fresh rolls unless kept; the export panel says so, with Keep all as played beside it |
| M13 | **Racks** | They hold modulators like any module; inside, a modulator moves only what's in the rack (§14) |
| M14 | **Free modulators** | Restart at their Phase when playback starts (§11) |
| M15 | **A preset dropped on a control** | Added and routed in one step (§14) |
| M16 | **"Random"** | The modulator's name. The planned M6 MIDI effect gets another name when its SPEC is written |
| M17 | **Factory presets** | About eight each |
| M18 | **The UI** | Drawn in every state and approved before building; hands-on tasks before sign-off (§19) |

## 22. Step 0, the spike (2026-10-15)

The spike's checks are kept as tests (`tests/integration/ModulatorSpikeTests.cpp`), on a modulator of our own type written for it. What it settled for §17:

- **Modulators are tracktion modifiers of our own types,** in the track's modifier list or a rack's, not plugins in the chain. A tracktion patch (`cmake/patches/tracktion_engine-custom-modifiers.patch`) lets the host name and make its own types. They're then modifiers wherever tracktion asks, their routes are automation sources (on load too), and adding or removing one rebuilds playback, undo and redo included. Copies of tracks, the clipboard, undo and racks keep working as they do for tracktion's own modifiers.
- **Per Target is one modulator answering for each route.** The patch asks a modifier for the value one route sees (`Modifier::getValueFor`). A route keeps its copy's number, and the modulator gives each copy its own phase or random path. One engine modifier per route, kept in step, isn't needed.
- **Values exact to the time.** The time-based modulators (LFO, Random, Steps, an Envelope on the beat) are functions of the song's time and their roll, so the patch's `getValueFor` answers for the time a parameter asks, not the block's start. tracktion already splits a modulated plugin's block into 128-sample pieces, so a plugin that doesn't smooth gets a new value every 128 samples (measured, 20 Hz LFO, ±10 % of the range, 512-sample blocks: steps of 0.06 dB, against 0.24 dB every 512 samples at block starts). Our modules ramp their parameters (steps of 0.002 dB). No splitting of our own; 128 samples is the documented limit for plugins.
- **Where a modulator sits:** the patch processes a modifier just before a plugin of its choosing (`ProcessingPosition::beforePlugin`), or after the chain without one. A Follower hears the sound there: before a Utility at −20 dB it hears the input, after the chain a tenth of it. Notes reach a modulator before the instrument and after it.
- **Undo, save and load:** a modulator and its route added in one step are undone and redone whole, and heard again after redo. A project read back from its state renders the same (nulls below −100 dB).
- **Rolls:** a random modulator's values come from a hash of its roll, its copy and the step, so a roll renders the same every time (two renders null to −103 dB) and another roll differently. Fresh rolls for each pass are the host setting the roll when playback starts (§13).
- **Racks:** moved into a rack's modifier list, a modulator is the same object, keeps its routes, and moves what's in the rack from the rack's graph.
- **It moves:** our module's parameter, a hosted plugin's (the Utility's VST3), a rack's macro (Morph is one) and the track's volume.
- **To do in step 1:**
  - keep each modulator's place as modules move (the plugin it's processed before), in `engine::Session`;
  - a Follower inside a rack hears its place through the rack's connections;
  - tracktion ignores modifiers on a plugin while automation reading is off (`Plugin::applyToBufferWithAutomation`); the DAW keeps reading on.

## 23. Step 0, drawn (2026-10-15; approved by the product owner the same day)

Every state of §19, drawn with the real panel kit, the modulators' real panels and our modules' panels from a stub of what the engine will hold (`tests/integration/ModulatorUiTests.cpp`, `FD_DUMP_DIR`). Each modulator's panel passes the module panel lint, now one shared check (`tests/panels/PanelLint.h`, which every module's panel also passes). The chain keeps its gaps, a rack's panels stay inside its frame, and the route list's rows keep their parts apart.

- **Among the modules:** a Utility, the LFO beside the Filter it moves (its Freq and the Chorus's Mix), the Filter, Random moving four controls with Per Target, the Chorus. Also at a compact width, the chain scrolled.
- **Before the instrument:** the Envelope, then the Wavetable whose oscillator position it opens (a ring one way), then a Follower ducking.
- **Inside a rack:** Steps beside the Utility it moves, in the rack's frame, after a Delay.
- **Each panel, unfolded and folded.**
- **Per Target on (four traces over each other, each fainter) and off (one); Keep on.**
- **A handle dragged; a handle hovered (the lines); a moved knob, bar and switch.**
- **The route list,** with three routes and with none.

What drawing settled:
- **The panels** (§12), on the module grid:
  - **LFO:** SHAPE [7] (display 7 × 3; Rate, Phase, Slant, Smooth, Fade In; Shape, Sync, Mode) and TARGETS [2] (Depth, Both Ways, Per Target).
  - **Random:** MOVEMENT [7] (display; Rate, Smooth, Slant, Cyclic, Jitter; Sync, Freeze, Keep) and TARGETS.
  - **Envelope:** SHAPE [8] (display; Attack, Hold, Decay, Sustain, Release; the three curves as bars), TRIGGER [4] (Trigger and Every; Mode and Fire; Legato and Keep; Delay, Repeat, Chance) and TARGETS (Depth, Per Target).
  - **Follower:** Listen as the header's sidechain picker, captioned Listen (Here, or a track); LEVEL [3] (display; Gain, Attack, Release); BAND ⏻ [2] (the shared cut-filter curve; Low, High); TARGETS (Depth).
  - **Steps:** PATTERN [8] (display; Rate, Steps, Smooth, Jitter; Sync, Advance, Restart; Keep, Fire) and TARGETS.
- **Each display:** a readout on its first line ("0.50 Hz", "On notes", "−33.9 dB", "16 steps · 1/16"), the handle at its right ("⤧ LFO · 2", a lock while kept), the output under them.
  - The LFO shows one cycle, a copy's trace for each target with Per Target, and each copy's dot where it is now.
  - Random shows its last eight steps.
  - The Envelope shows its stages spaced by the log of their times, with the points it's dragged by inside the well, and a dot for each copy playing.
  - The Follower shows what it heard on a dB scale.
  - Steps shows bars (the current step brighter) with each step's chance along the foot, and with Per Target what each copy plays this time round.
- **The header:** the Modulator icon (a wave and a dot where it is) between the power and the name, in `textSecondary`; no meters.
- **Folded:** the strip keeps the handle's arrow at its foot, under the name.
- **On the targets** (the kit, for every module):
  - a moved knob's ring has a dot where it is now;
  - a moved bar or switch has the ring's line along its bottom edge, inside it; a bar's "now" is a short bright stretch of that line, clear of its text; a switch's line is full while the modulation turns it over;
  - while a handle is dragged, every control shows a faint line or ring, and the one under the pointer lights.
- **The lines on hover:** from the chip's foot to a dot on each control it moves, in the modulation gradient over a dark halo, so they read where they cross other panels, sagging as a cable does, more the further they go.
- **The route list:** a popover, `Routes · LFO`, a row a route: the target, its amount bar (−200 … +200 %, from the middle), Show and Remove.
- **After the review** (the product owner): horizontal space is expensive. TARGETS's two-column toggles and the Follower's Listen picker (the header's fixed 128 px for "Here") made the panels wider than they need to be; step 1 tightens them (§24).
- **For the kit:** `Theme::echo` (each copy 55 % of the one before, `echoAlpha`), `Icon::Modulator`, a header icon and a folded accessory on `DevicePanel`, the source handle's count, lock, compact form and drags taken by its host, and `ParamBinding::modulated` and `modulatorDrag` for what the track's modulators do to a module's controls.

## 24. Step 1, built (2026-10-15)

Built on main in six steps (1a–1f), then the Follower's Listen, the master, Fire and the export line. What building settled:

- **The panels, tightened after the drawings' review:** one section a modulator (the Envelope keeps SHAPE and TRIGGER), Depth and the switches in the room the others leave, the Follower's Listen a two-column picker in its section. LFO, Random and Steps are 436 px wide, the Envelope 664, the Follower 332. The header's presets fit without widening any of them.
- **The engine:**
  - each modulator a tracktion modifier of our own type (the custom-modifiers patch), placed before the module after it in the chain;
  - along a chain every place counts its modulators: adding, moving, grouping into a rack and out of it, replacing an instrument, a rack added; modulators keep their places through all of them;
  - the master has them too (the master-modifiers patch);
  - the Follower listens to another track as a module's sidechain does (the modifier-sidechain patch): that track's sound on two channels after the track's own, only the track's own passed on. Loops are refused through Followers as through modules;
  - nothing a modulator does as it plays allocates (a test counts).
- **The chain view:**
  - a handle dragged onto any knob, bar or switch on the track's panels routes it at 50 %; the chain scrolls at its edges (`shell.dragScrollEdge`, `dragScrollSpeed`); Escape cancels;
  - the lines on hover are one shared painter (`ui::paintModulationCable`), which the drawn states use too;
  - the route list opens above the modulator's panel (the chain is the window's foot), so the panel and its count stay in view; each target is named as its control names it ("Chorus · Mix", not "Dry/Wet");
  - a modulator's header's right-click: Routes…, Remove routes, Keep as played;
  - a modulated knob's ring sets the route of the modulator hovered last, one undo step;
  - a control's right-click has one Modulate with ▸: the track's modulators (ticked where they move it), New ▸ the five, then a module's own sources. New puts the modulator just before the control's module, or before the rack holding it;
  - the chain's "+" lists the modulators after the audio effects; a rack's end cap doesn't, until modulators go into racks.
- **Menus:** Add modulator ▸ is five actions (`module.addModulator.lfo` …), so the command palette finds "Add LFO". Keep as played is enabled for an Envelope whether or not its Chance is under 100 %, as its panel's Keep is. Song ▸ Keep all as played takes clips too.
- **The browser:** Modulators is a part between Audio effects and Racks, in the rail and the tree. Modulators are in the module catalogue (keys `fd:mod-lfo` …), so a drop or a double-click adds one where it lands and search finds them and their presets. One (or its preset) dragged onto a control on the track's panels shows the controls that would take it, and the drop adds it just before that control's module, set up and routed at 50 %, one step.
- **Presets:** 40 factory presets, eight each, in `app/presets/<type>/`, compiled in. A preset keeps a modulator's settings and a Steps pattern, never its routes or its roll; your default applies to a new one.
- **Fire** is on only while it's held, as a pad.
- **The export panel's line** reads "2 clips and 1 modulator play by chance", Keep all as played under it; its tooltip says each export differs.
- **Assistants:** a route's target is an object, as automation names a parameter (`module` and `parameter`, or `track` and `volume`, `pan`, `send` with `return`); amounts in % (−200 … 200). `set_module_sidechain` sets a Follower's Listen. `keep_roll` takes a clip too, and `reroll_chance` refuses a clip that rolls.

- **In racks** (§14): a rack's chain holds modulators as a track's does, each tapping the chain where it sits (what comes there goes into it; nothing comes out). Group into rack takes them in place; their routes leaving the rack go, and a toast says how many with Undo. Rack files keep a modulator's type, settings, place and routes (by module place, a macro or Morph). A Follower in a rack listens where it sits only. Modulators move into and out of racks like modules; ungrouping brings the shown chain's out in place.
- **The LFO's Drawn shape:** the Wavetable's LFO shape editor is shared now (`ui::CurveEditor` in the panel kit, the curve model `dsp::DrawnCurve` in `libs/dsp`). On the LFO's panel it sits over the display's plot while Shape is Drawn, without a well or top row of its own; the named shapes stay in Shape. Each edit is one undo step; presets keep the shape; assistants draw it with `set_lfo_curve`.
- **The hands-on tasks** are in `docs/manual-tests/M3.md`.
- **Choices** (B87, the recommendation approved by the product owner, 2026-10-15): a handle lands on a dropdown or segmented buttons as on a switch, and they show it as a switch does: the line along the bottom edge (each button's, in segmented buttons), full while the modulation has it on another choice than the one set, that choice's button outlined. On a switch or button that's on, the line has a dark halo above it, as the lines on hover do: on the `active` fill it didn't show, the drag's lit line least of all (switches had it too). Drawn in `tests/integration/ModulatorUiTests.cpp` ("choices moved"), driven in `ModulatorChainTests.cpp`.

Signed off by the product owner (2026-10-15).

