# MIDI effects: modules that play with notes and know the song

A **MIDI effect** is a module that works on notes before the instrument hears them. Some **shape what you play**: an arpeggiator, a chord, a groove. Some **write a part of their own**: a beat, a bassline, a melody. Segno's MIDI effects **know the song**: its key, the chord sounding now and the one coming next, the section they're in and where it ends, and what the other tracks play. They follow all of it by default. A riff copied into the chorus bends to the chorus's chords. A bassline lands on the kick and walks into the next chord, because it sees the chord coming. A beat fills before the drop, and every chorus a generator plays is the same chorus. Whatever they play can be kept as notes at any moment, with the notes from before kept as a take.

**Status:** approved by the product owner (2026-10-18), every recommendation of §19 (MX1–MX25), harmony read from the notes (MX6) included; [ADR 0074](../decisions/0074-midi-effects-know-the-song.md) accepted. Worked on the branch `midi-effects` in `../foss-daw-midifx`, beside main's groups (P9). Step 0 first: the engine spike, and the UI drawn in every state and approved before step 1; each module gets its SPEC before its code, and each step is signed off on its own (§18).

What the product owner asked (2026-10-18):
- "We will be working on MIDI effects. A new category of modules in this DAW."
- "Start with research to come up with a cohesive, comprehensive set of MIDI effects and perhaps even generators that would expand creativity and make coming up with ideas and flipping ideas into new things easy and fun."
- "See what modern suites already offer and also think about innovating in cool ways, especially here where these plugins can integrate deeply with the DAW since we are making both."
- "They have to be smart and adaptive to your project."

## 1. What's there now

- **The device API reserved MIDI effects at M1's freeze** (ADR 0034), but nothing uses them:
  - `DeviceKind::MidiEffect`, `BusConfig::producesMidi` and `ProcessContext::outEvents` exist;
  - the DAW's adapter (`DevicePlugin`) turns tracktion's MIDI into events and never writes any back;
  - the plugin adapter passes the host's MIDI through unchanged for a module that produces MIDI;
  - the snapshot marks a module as a MIDI effect, and bounce counts MIDI effects before the instrument as part of the render ([bounce.md](bounce.md)).
- **What a module hears of the song** (`TransportInfo`): tempo, position, time signature, playing, and the song's key ([ADR 0057](../decisions/0057-the-song-has-one-key.md)). `startPass(roll)` gives fresh dice each pass ([ADR 0062](../decisions/0062-random-rolls-again-each-time-it-plays.md)).
- **`libs/music`** (JUCE-free): 36 scales, keys and their spelling, key detection; 28 chord kinds named from notes, voicings, voice leading, next-chord suggestions.
- **The piano roll** ([ADR 0058](../decisions/0058-the-piano-roll-writes-in-key-and-in-chords.md)):
  - chords read from the notes, never kept as separate data ("chords are notes");
  - Others: other tracks' notes and their chords, faint;
  - per-note chance rolling each pass, with Keep;
  - thirteen tools, each described once as data and run by one pure function: Transform (Quantise, Humanize, Strum, Arpeggiate, Chop, Ornament, Velocity, Span, Randomise pitch) and Generate (Rhythm, Melody, Chords, Bassline).

  The tools live in `app/src/model/NoteToolkit`, above the JUCE line's libraries, so a module can't call them yet (ADR 0006).
- **The song row's sections** ([ADR 0049](../decisions/0049-the-master-row-is-the-song-row.md)): a name, a colour, a start and an end; a copy is named with a number ("Chorus 2").
- **Capture MIDI** keeps the last phrase played. **Bounce in place keeps its source** as a take on the clip ([ADR 0035](../decisions/0035-bounce-keeps-its-source.md), [ADR 0032](../decisions/0032-takes-belong-to-clips.md)).
- **Modulators** move any parameter on their track, and a Follower listens to another track through a tracktion patch. **Racks** have macros, snapshots, Morph, Vary and locks. **The Drum Kit**'s pads have names, which assistants already write by.
- **tracktion_engine** has a chord track, chord clips and a pattern generator (arpeggio, chords, bass, melody written into a clip, `tracktion_Musicality.h`). Segno uses none of them: its chords are read from notes.
- **The plan until now** ([device-suite.md](../devices/device-suite.md)): Arpeggiator, Chord and Scale in M4; Note Echo, Note Length, Pitch, Velocity, Random and CC Control in M6; "MIDI tools" in M6, which the piano roll's tools have since covered.
  - "Random" is the modulator's name now; the MIDI effect needs another ([modulators.md](modulators.md) M16).
  - MIDI effect racks were left for later ([racks.md](racks.md) R16).
  - A MIDI effect inserted or removed mid-note can leave the note hanging, to be "made right with our own MIDI effects" ([B55](../open-questions.md), item 2).

## 2. How others do it (October 2026)

Researched from the vendors' manuals and release notes, reviews, forums and papers, October 2026. "Unverified" marks what couldn't be confirmed.

### 2.1 DAWs

| | MIDI effects | Generators | What they follow | Notes out of the effects |
|---|---|---|---|---|
| **Live 12.4** | Arpeggiator, Chord, Pitch, Scale, Random, Velocity, Note Length, CC Control; in Max for Live: Note Echo, Envelope MIDI, Shaper MIDI, Expression Control, MPE Control, Rotating Rhythm Generator, Melodic Steps; the Sequencers pack (Step Arp, SQ Sequencer with conditions, Rhythmic Steps) | MIDI Tools: Rhythm, Seed, Shape, Stacks, Euclidean, and transformations (Arpeggiate runs on the Arpeggiator's engine) | Each clip's scale (or a track's, a Drum Rack's): Arpeggiator, Chord, Pitch, Random, Scale and the tools | No print command: a second track records another's output |
| **Bitwig 6.1** | 22 Note FX: Arpeggiator, Echo, Note Repeats (Euclid), Dribble (a bouncing ball), Ricochet (balls in a room), **Harmonize** (fits notes to what another track plays), Multi-note, Strum, Humanize, Randomize, Quantize, Key Filter+, Stepwise (steps), Latch, Note Length…; Note Receiver; clip operators (Chance, Repeats, Occurrence, Recurrence) | Note Grid (modular) | The project key ("Use Global Key" on six, automatable); another track's notes as they play (Harmonize) | None found |
| **Logic 12.4** | Arpeggiator, Chord Trigger, Modifier, Modulator, Note Repeater, Randomizer, Scripter, Transposer, Velocity Processor | Session Players (Drummer, Bass, Keyboard, Synth from 12.0); the Step Sequencer, whose rows can be chord degrees | The chord track, found from audio or MIDI by Chord ID (12.0): the players and the Step Sequencer; "Follow Rhythm Of" another track. The MIDI FX follow nothing | Record MIDI to Track Here (a point in the chain); the arpeggiator's capture buttons drag the arpeggio out as a region |
| **Cubase 15** | 17: Arpache 5, Arpache SX (another part as its pattern), Auto LFO, Beat Designer, Chorder, Compressor, Context Gate, Density, MIDI Control, MIDI Echo, MIDI Modifiers, Micro Tuner, Note to CC, Quantizer, StepDesigner, Transformer, MIDI Monitor | Chord Pads with players; the Pattern Editor (drums in 14, melodic in 15) | Follow Chord Track per track (six modes); the Scale Assistant | The inserts' divider (recorded above it); Freeze MIDI Modifiers; Merge MIDI in Loop |
| **Studio Pro 8.1** | Arpeggiator (32-step lanes), Chorder, Repeater, Input Filter | Patterns (probability, repeats) | Follow Chords per track (Parallel, Narrow, Bass). The Note FX follow nothing | Render Instrument Tracks |
| **FL Studio 26.1** | The channel's arpeggiator and echo; VFX in Patcher: Sequencer, Key Mapper, Keyboard Splitter, Level Scaler, Color Mapper, Envelope, Script (Python) | Riff Machine; the Chord Progression tool (voice-led in 26.1) | Snap to scale | The piano roll's tools write notes |
| **Reason 14** | Players: Beat Map (an X/Y map of beats, density per drum), Dual Arpeggio, Note Echo, Note Tool, Random Tool (conditions), Scales & Chords | – | Scales & Chords' own key | Direct Record; Send to Track (renders to a note lane, then bypasses the players) |
| **REAPER 7.8** | About 40 JSFX, ReaControlMIDI (snap to scale or chord) | MIDI Sequencer Megababy | – | Apply FX as a new take (MIDI output) |

Sources: [Live's MIDI effects](https://www.ableton.com/en/live-manual/12/live-midi-effect-reference/), [MIDI Tools](https://www.ableton.com/en/live-manual/12/midi-tools/), [release notes](https://www.ableton.com/en/release-notes/live-12/); [Bitwig Note FX](https://www.bitwig.com/userguide/latest/note_fx/), [operators](https://www.bitwig.com/userguide/latest/operators/), [Bitwig 6](https://www.bonedo.de/artikel/bitwig-studio-6-test/); [Logic MIDI FX](https://support.apple.com/guide/logicpro/use-midi-plug-ins-lgcef1c11e8f/mac), [Step Sequencer chords](https://support.apple.com/guide/logicpro/chords-and-pitch-in-step-sequencer-lgcp0426e066/mac), [Record MIDI to Track Here](https://support.apple.com/guide/logicpro/record-midi-to-track-lgcp423beea5/mac); [Cubase Follow Chord Track](https://archive.steinberg.help/cubase_pro/v12/en/cubase_nuendo/topics/chord_functions/chord_functions_follow_chord_track_r.html); [Studio Pro Note FX](https://fenderstudiopromanual.fender.com/en/Content/Built-In_Instruments_Topics/Note_FX.htm), [chord track](https://fenderstudiopromanual.fender.com/en/Content/Arranging_Topics/Chord%20Track.htm); [FL Studio](https://www.image-line.com/fl-studio-learning/fl-studio-online-manual/html/WhatsNew.htm); [Reason Players](https://docs.reasonstudios.com/reason14/working-with-players); [REAPER](https://www.reaper.fm/userguide/ReaperUserGuide781c.pdf).

- **What every DAW has:** an arpeggiator, chords from one note, a note echo, velocity shaping, transposing or snapping to a scale, a step sequencer somewhere, and a way to script your own.
- **Rare and loved:** Bitwig's physics (Dribble, Ricochet), Harmonize and clip operators; Cubase's Context Gate and Density; Logic's print point and capture buttons; Live's Play One groups and tools you can preview; Reason's Beat Map.
- **The key** reaches effects in Live and Bitwig. **Chords** reach generators only through a chord track kept by hand (Logic's players and Step Sequencer, Cubase and Studio Pro's following), or from one other track as it plays (Bitwig's Harmonize). **No effect sees a change coming.**
- **Sections:** only Logic's players change by region and fill at their edges; Bitwig's Occurrence knows a fill mode.
- **Live and offline:** Live's Arpeggiate tool runs its Arpeggiator's engine; Cubase mirrors its Transformer and Quantizer offline; FL Studio pairs tools with VFX. Nobody makes every effect a tool.

### 2.2 Plugins

| | What it is | What stands out |
|---|---|---|
| **Scaler 3** (3.3, July 2026) | A theory workstation: chords, "Motions" (performance patterns), an arrangement page; hosts instruments | Detects key and chords from MIDI and audio; suggests next chords, voice-led; substitutions and negative harmony; everything on its timeline follows its own chord progression. Users: "like entering a labyrinth" ([product](https://scalermusic.com/products/scaler-3/), [forum](https://forum.scalermusic.com/t/frustrated-with-scaler-3/20730?page=4)) |
| **Captain Plugins 8** (Feb 2026) | Five linked plugins: Chords, Melody, Deep (bass), Beat, Play | Melody follows the progression "with adjustable tension"; the bass "auto-adjusts" when the chords change, but only between its own plugins; v8 applies a progression or rhythm to selected notes only ([Mixed In Key](https://mixedinkey.com/release-notes/captain-plugins/)) |
| **Cubase Chord Pads** | Chords on pads with players | Adaptive Voicing keeps a progression from jumping; Pattern mode re-voices a 3–5-voice loop to each chord; Proximity lays chords out by distance ([Steinberg](https://steinberg.help/cubase_pro_artist/v10/en/cubase_nuendo/topics/chord_pads/chord_pads_using_the_pattern_player_t.html)) |
| **Riffer 4** (Oct 2026) | Riff and sequence generator, five engines | Big Dice, locks per note and step, density, a new riff every cycle; scale-aware, not chord-aware ([Sonicstate](https://sonicstate.com/news/2026/10/02/audiomodern-releases-riffer-4/), [Dubspot](https://blog.dubspot.com/plugins/audiomodern-riffer-4)) |
| **Playbeat 4** (2025) | Beat generator | Random, remix, smart (built from your saved presets) and instant modes; genres; locks per channel, which its reviewer says make it usable ([Attack](https://www.attackmagazine.com/reviews/gear-software/playbeat-4-ai-driven-sequencing-done-right/)) |
| **Thesys, Consequence, Obscurium** (Sugar Bytes) | Lane sequencers; chord snapshots to voices; a generative instrument | Lanes of independent lengths; Mutate apart from Randomize; Morph between two sequencer states ([Sugar Bytes](https://sugar-bytes.de/obscurium)) |
| **Cthulhu, Kirnu Cream, BlueARP, Stepic** | Chord memory and arps; lane arps; step sequencers | Chord memory per key; per-step "order" (first, last, high, low note); 200+ randomisations with locks (Stepic 1.6). Cream's last update was 2015 |
| **Fugue Machine Rubato** (2025) | Up to 8 playheads over one piano roll | Each at its own speed and direction, inverted, frozen: canon as a MIDI effect. AUv3 only; doesn't run in Live 12, which can't host MIDI plugins ([Synth Anatomy](https://synthanatomy.com/2025/06/fugue-machine-rubato-alexandernauts-brilliant-multi-playhead-midi-sequencer-takes-a-time-bending-and-automation-route.html)) |
| **RapidComposer 6** and **MIDI Mutator** | A composition workstation; a "flip" tool | Phrases adapt to the master track's chords; Mutator re-rhythms (keeping pitches), imports another file's rhythm, reshapes contour, Markov "similar style", all offline ([MusicDevelopments](https://www.musicdevelopments.com/), [Sonicstate](https://sonicstate.com/news/2022/06/14/mutate-your-midi/)) |
| **Hookpad Aria** (V2, April 2026) | A web songwriting tool on a fine-tuned Anticipatory Music Transformer | Fills a span from what's before and after it. 23 % of suggestions accepted; users "rarely used the suggestions verbatim", treating them as sparks ([arXiv 2502.08122](https://arxiv.org/abs/2502.08122)) |
| **Free and open:** Stochas (GPL-3.0), Ripchord (GPL-3.0), Music Pattern Generator (GPL-3.0), Magenta Studio (Apache-2.0, archived Jan 2026) | | Several notes per step chosen by weight, a step played only if another did (Stochas); MIDI file → chord map (Ripchord); Continue, Interpolate, Groove, Drumify (Magenta) |

### 2.3 Hardware and generative ideas

| | The idea | Why it's loved |
|---|---|---|
| **Torso T-1** (fw 2.1.5, Aug 2026) | Euclidean tracks with Repeats (Pace, Ramp), Voicing styles, Phrase (a pitch shape over 1–64 bars), Accent grooves, Cycles (stored variations played in turn), bipolar random on every knob ([docs](https://docs.torsoelectronics.com/t1/)) | Every control is a musical gesture; random is a property of each knob, not a mode |
| **NDLR** (fw 3.3.6) | Pad, Drone and two Motifs follow one chord. A motif is a list of chord-tone indexes, its rhythm a separate list: lengths that differ phase (5 against 8) ([manual](https://cdn.shopify.com/s/files/1/1004/2320/5174/files/The-NDLR-Rev2-User-Manual-for-Firmware-3.3.x-v3.0.pdf)) | Can't play a wrong note; one press reharmonises every part |
| **Elektron** | Trig conditions: chance, A:B (pass A of every B), Fill, First, Last, Previous and Neighbour (play if the last condition, or the neighbour track's, came true), each negated ([Elektronauts](https://www.elektronauts.com/t/nei-and-pre-trig-conditions/51959)) | Variation that's composed, not random; correlated chance keeps a flam with its ghost note |
| **Squarp Hapax** (3.22, Sept 2026) | Live effects: Register (a shift register), Chance per note or "Lot" (one roll a beat), Euclid, a polyrhythmic arp, Harmonizer, Scaler, Echo with pitch, Swing ([manual](https://squarp.net/hapax/manual/modefx/)) | The nearest hardware to a DAW's MIDI-effect chain |
| **Polyend Play+** | Chance per step, Group with last, Play N / Skip M; Randomize previews live and commits only on Save ([manual](https://polyend.com/wp-content/themes/polyend/manuals/?id=14800)) | Non-destructive randomising |
| **Intellijel Metropolix** | Stages of 1–8 pulses, ratchets, mod lanes of their own lengths; the Accumulator moves a stage by steps of the scale each pass ([manual](https://www.analoguehaven.com/intellijel-designs/metropolix-solo/manual.pdf)) | One riff becomes a rising sequence with one knob |
| **OP-Z, OP-XY** | Step components: pulse, multiply (ratchet), jump, ramp through the scale, tonality, play every Nth time ([guide](https://teenage.engineering/guides/op-xy/step-components)) | A compact grammar of per-step behaviour |
| **Mutable Instruments** (open source) | Marbles: random that loops (déjà vu: new ↔ locked ↔ shuffled), a scale learned from what you play, simplified by dropping the least-played notes. Grids: a 5×5 map of drum patterns learned from music, each step ranked, so density only adds hits ([Marbles](https://pichenettes.github.io/mutable-instruments-documentation/modules/marbles/manual/), [Grids](https://pichenettes.github.io/mutable-instruments-documentation/modules/grids/manual/)) | Randomness you freeze when it's good; density that never breaks the groove. Marbles' code is MIT; Grids' map is GPL-3.0, usable in AGPL Segno with its notices |
| **Turing Machine** (Music Thing) | A 16-step shift register; one knob from locked loop through slow drift to noise ([Music Thing](https://www.musicthing.co.uk/Turing-Machine/)) | "Occasionally changing notes" |
| **Fugue Machine, Otomata, Rozeta Particles** | Canon playheads; cells bouncing on a grid; a note cloud bouncing off walls | Visible, playful, emergent |

**From the research literature:**
- **Euclidean patterns aren't nested:** sweeping the pulses of E(3,8) to E(4,8) reshuffles the beat. A density knob needs steps ranked by importance, so more density only adds hits (Grids, Hapax's Every N).
- **Metric weights** (Longuet-Higgins and Lee, [Fitch and Rosenfeld 2007](https://web.uvic.ca/~aschloss/course_mat/MUS%20511/ARTICLES%20AND%20REFS%20FOR%20320/FitchRosenfeld20071.pdf)) say which steps are strong; groove peaks at medium syncopation, an inverted U ([Witek et al. 2014](https://journals.plos.org/plosone/article?id=10.1371/journal.pone.0094446)). A generator aims at a band, not a maximum.
- **Melodies that sound intentional:** mostly steps, a leap followed by a step back, a rise then a fall, phrase ends on the 1st, 3rd or 5th, chord tones on strong beats and passing notes between ([von Hippel and Huron 2000](https://labs.sonicfield.org/library/why-do-skips-precede-reversals-the-effect-of-tessitura-on-melodic-structure)). **Repetition** turned random-sounding music into music heard as "composed by a human" ([Margulis 2013](https://www.frontiersin.org/articles/10.3389/fpsyg.2013.00167/full)): a motif and its varied repeat (AA′, AABA) does more than any single rule.
- **Humanising:** players' timing drifts with long-range correlation (1/f), and listeners prefer that to random jitter ([Hennig et al. 2011](https://pmc.ncbi.nlm.nih.gov/articles/PMC3202537/)).
- **Voice leading:** the least total movement, without parallel fifths and octaves or crossed voices; only order-keeping mappings need searching ([Tymoczko 2005](https://mtosmt.org/classic/mto.05.11.4/mto.05.11.4.tymoczko.pdf)).
- **Interlocking parts:** Balinese [kotekan](https://en.wikipedia.org/wiki/Kotekan), one part playing the other's gaps, is the simplest complement rule; no algorithm for call and response was found.
- **Builds into a drop** ([Solberg 2014](https://dj.dancecult.net/index.php/dancecult/article/view/451)): drum rolls whose rate doubles, and the kick and bass taken out before the drop come back.

### 2.4 What users complain about

- **Getting the notes into the DAW:** a plugin's MIDI needs a second track or a virtual bus in Live and Logic; Live 12 can't host MIDI plugins at all; drags that come out empty ([Ableton](https://help.ableton.com/hc/en-us/articles/209070189-Accessing-the-MIDI-output-of-a-VST-plug-in)).
- **Chord changes in time:** a plugin can't see a change coming, so it plays the old chord on the downbeat or changes on beat 2 (Scaler's forum); a suite of linked plugins drifting into "quarter time" (Orb).
- **Not chord-aware:** most sequencers and riff generators know the scale, not the chords.
- **Follow chord track** (Cubase): wrong notes when a note hangs over a change, voicings that move when a chord is edited, "trial and (lots of!) error", originals muted ([SOS](https://www.soundonsound.com/node/4903885)). Logic's "Use Default Chord Progression", on by default, makes players ignore the chord track.
- **Too complicated** (Scaler, InstaChord), **generic output** ("4 bars copied to make 8"), **lost results:** no tool found keeps a history of past rolls. **Cloud and accounts** (Staccato needs to be online).
- **What they like:** following that's non-destructive and switches off (Studio Pro), locks at every level, a continuous "how much" knob rather than only dice, several results to choose from, generating into a selection.

### 2.5 Local models

Open-weight symbolic models now run offline: the Anticipatory Music Transformer (small 128M and medium 360M, Apache-2.0 code and weights, continuing, filling and accompanying), Notochord (MIT, about 6 ms per event on a laptop), Composer's Assistant 2 (MIT code, trained only on public-domain and permissively licensed MIDI, filling bars in REAPER). MIDI-GPT's weights are non-commercial. They're for the backlog (§16).

## 3. What this means for Segno

- **The shapers are table stakes:** every DAW has an arpeggiator, chord, scale, echo, length and velocity. Segno needs them at Live's level, and they get better by knowing the song.
- **Following chords needs a chord track kept by hand** (Cubase, Studio Pro, Logic), and the complaints are about that copy drifting from the music and mapping by guesswork; or it listens to one track as it plays and can't see a change coming (Bitwig's Harmonize). **Segno reads the harmony from the notes** that already make it, ahead of the playhead, and shows what it read.
- **Almost nothing reads the song's form** (Logic's players by region). **Nothing locks a bass to the kick.** **No effect sees what comes next.** Segno can do all three, because the arrangement is known ahead of the playhead.
- **The best generative ideas are in hardware and plugins that can't see the song** (T-1, NDLR, Elektron's conditions, Marbles, Grids, Riffer). Brought inside the song, they play in its chords, fill before its sections and repeat where it repeats.
- **Live effects and note tools are mostly separate:** a few are paired (Live's Arpeggiate, Cubase's Transformer, FL's Level Scaler). In Segno every MIDI effect is also a tool.
- **Printing the effects to notes** exists in most DAWs, REAPER's into a new take; Live and Bitwig have none. Segno's Keep as notes keeps the notes before as a take, and the roll you heard.
- **Nobody keeps the rolls you heard.** Segno's randomness is computed from a roll ([ADR 0062](../decisions/0062-random-rolls-again-each-time-it-plays.md)), so any roll heard can be played again.

## 4. Principles

1. **Know the song, follow it by default, work without it.** Every MIDI effect reads the key, the harmony, the form and other tracks where it has a use for them (§5). Each has its own settings for when there's nothing to follow: no key, a plugin host, or a choice to ignore the song. It says when it falls back.
2. **The notes are the truth.** The harmony is read from the song's notes, as the piano roll's chord lane reads a clip. There's no chord track to keep in step ([ADR 0058](../decisions/0058-the-piano-roll-writes-in-key-and-in-chords.md)).
3. **Following never edits.** A MIDI effect changes what's heard, never the notes in a clip, until you keep its notes (§9). Turn it off and the part is as written.
4. **Few, deep, one job each** ([ADR 0011](../decisions/0011-calibrated-ambition-and-consolidation.md) rule 3). Eleven modules cover Live's MIDI effects and add what its users reach for plugins and hardware to get (§6). Merged only where the job is the same.
5. **One vocabulary**, shared with the modulators and racks (§8.1). Learn one and you know them all.
6. **Seen working.** Every MIDI effect shows what came in, what goes out and what it follows, live (§12). Generated notes are drawn in their clips.
7. **Random is random** (ADR 0062). It rolls again each pass, and Keep holds a roll you liked. Randomness is computed from the roll and the place, never run, so a jump, a render and a kept roll all play what playing through would have.
8. **Nothing is lost, and nothing is stuck.** What they play can be kept as notes, with the notes before kept as a take. Turning one on or off, removing it, or jumping ends its notes.
9. **One algorithm in every place.** A MIDI effect, the clip view's Tools pane and assistants run the same JUCE-free functions in `libs/music`.
10. **Arrangement first** (vision.md). Generators play where their track has clips, so the arrangement says where a part plays (§7).

## 5. The song, as a MIDI effect reads it

| | What it is | From | Used by | Without it |
|---|---|---|---|---|
| **Time** | Tempo, bar, beat, time signature; where 4- and 8-bar phrases start | The transport | Every rate and step, accents, retriggers | The host's transport |
| **Key** | Root and notes ([ADR 0057](../decisions/0057-the-song-has-one-key.md)) | The song | Steps of the key everywhere | A key of the module's own, or chromatic |
| **Harmony** | The chord sounding now, its bass note, and the next chord and when it comes | The song's notes (§5.1) | Chord, Pitch's Fit, the Arpeggiator with no notes held, the generators | The key's home chord; chords played into it; a chord of its own |
| **Form** | The section's name and which time it plays, the bar within it, the bars to the next one and its name, its Energy | The song row (§5.2) | Fills, drop-outs, retriggers, conditions, same section same music, density | Bars counted from the start; fills every N bars |
| **This track ahead** | The notes its clips will play in the next bars | The track's clips | Strums and pushes ahead of the beat, legato, approach notes, with no latency | Notes only as they arrive |
| **Another track** | Its notes, as they play and ahead from its clips | Listen, in the header (§5.3) | The Bassline on the kick, Rhythm in the gaps, Melody answering, Groove's feel | A second note input, where the format has one |
| **Pads** | A Drum Kit's pad names and notes | The track's kit | Rhythm's lanes on the right pads | General MIDI drum notes |
| **The roll** | Fresh dice each pass, or a kept roll | ADR 0062 | Everything that chooses | Rolls each time the host starts |

### 5.1 The harmony, read from the notes

- **A MIDI effect that uses harmony has a Follow choice:**
  - **Song** (the default): the chords the song's notes make (below);
  - **A track:** one track's notes, chosen as a sidechain is (the pad, the piano);
  - **Played:** the chord held on the effect's own input, below a split note or on a channel of its own, for jamming and for plugin hosts;
  - **Own:** one chord, chosen in the module.
- **Which tracks make the song's harmony:** each track has **Harmony: Auto, On, Off** (its right-click and the inspector's TRACK). Auto counts a MIDI track whose clips play chords (three notes or more together) and the lowest part for the bass, and leaves out Drum Kits and one-note parts. A track's own MIDI effects never count, so nothing follows itself.
  - **A muted track, or one with no instrument, still counts.** A silent track of chords is how you guide the song without hearing the guide: a chord track by another name, made of notes and written with the piano roll's chord tools.
- **Read as a musician would, not note by note:** a chord changes at most once a beat (the song's setting, Song ▸ Harmony…), short notes off the beat are passing notes, a gap keeps the last chord, the bass is the lowest note sounding in a track that counts.
- **Ahead, not only now:** the clips are in the arrangement, so an effect knows the next chord and when it comes. A bassline approaches it, a chord voices toward it, a fill lands on it. Chords played in are known only as they're played.
- **No chords** at a place: the key's home chord (I, or i in a minor key), as Logic's players do; no key either: the module's own chord. The display says which.
- **Seen:**
  - each effect's display names the chord now and the next one, or what it fell back to (§12);
  - **View ▸ Song chords** names them along the song row, under the sections, read-only. A click on a chord there opens the clip whose notes make it. What the MIDI effects follow is never hidden.
- **Audio clips aren't read for chords yet;** their key is (P6c). Chords from audio go to the backlog (§16).

### 5.2 The song's form

- **From the song row's sections:** the name and which time it plays ("Chorus 2" is the chorus's second time: the copy's number is dropped to match names), the bar within it, the bars until the next and its name.
- **Energy** (new): each section has one, 0–100 % (50 by default), set in the inspector's SECTION. Generators' Density and Fill follow it by an amount of their own, so a quiet verse and a full chorus come from the same modules, as Logic's Drummer takes a complexity per region.
- **What uses the form:**
  - **Fills** land in the last bar (or beat) before a section changes, onto the next one's downbeat; a **drop-out** takes a part out before it instead, as producers take the kick and bass out before a drop;
  - **Retrigger** at each section restarts an arpeggio or a sequence;
  - **Conditions** on steps: the last bar of a section, a section by name, the Nth time it plays (§8.3);
  - **Same section, same music** (§8.4).
- **Without sections,** the song is one long section, and fills come every 4, 8 or 16 bars.

### 5.3 Listening to another track

- **A Listen picker in the header,** first among its settings, as a sidechain's is ([ADR 0025](../decisions/0025-sidechain-routing.md)), and as narrow ([panel space](device-panels.md)): None, then the tracks. A track that listens to this one is left out, so nothing loops.
- **What's heard:** the other track's notes after its own MIDI effects, as they play, and ahead from its clips. A muted track is still heard (its notes, not its sound).
- **What it's for** (each SPEC says):
  - the **Bassline** plays on the kick's hits, or between them;
  - **Rhythm** fills the gaps another part leaves, or doubles its hits (kotekan);
  - **Melody** answers a lead (plays while it rests), or plays a counter-line against it;
  - **Groove** takes another track's feel;
  - **Follow ▸ A track** reads harmony from it.

### 5.4 Without the song

- **In a plugin host** there's no song:
  - the module's own key;
  - Played or Own chords, and the host's chord where VST3 gives one (`kChordValid`; no host was found that fills it, so it's read if present);
  - no sections: fills every N bars;
  - Listen as a second note input where the format has one (CLAP and VST3; best effort per host);
  - generators play whenever the host plays.
- **Each SPEC states what its module does without each piece of context,** as the key's design asks of every module that follows the key ([key-and-scale.md](key-and-scale.md) §9).

## 6. The set

Eight shape what you play; three write a part of their own.

| Module | What it does | Covers (Live) | What's new | Step |
|---|---|---|---|---|
| **Arpeggiator** | Plays held notes one at a time | Arpeggiator; Step Arp (Sequencers pack) | Step lanes of their own lengths; the song's chord with no notes held; retrigger at sections | 1 |
| **Chord** | Adds notes to each note | Chord | Steps of the key; the song's chord on any note, voice-led; keys to chords; learn a shape | 1 |
| **Pitch** | Moves, fits, mirrors and limits notes | Pitch, Scale, Random | Fit to the song's chords, so any part follows them; mirror in the key | 1 |
| **Velocity** | Shapes how hard notes play | Velocity | The curve drawn on its display; accents by the meter | 3 |
| **Note Length** | Sets how long notes last | Note Length | – | 3 |
| **Note Echo** | Repeats notes | Note Echo (Max for Live) | Each repeat a step of the key away; rolls that speed up or slow down; canon voices | 3 |
| **Groove** | Moves notes in time | (none: Live's groove pool works on clips) | Swing and grooves on the track, live; another track's feel; humanising that drifts | 3 |
| **Controllers** | Sends controllers from knobs | CC Control | – | 3 |
| **Rhythm** | Writes beats | Rotating Rhythm Generator, Rhythmic Steps (Max for Live) | Lanes on a kit's pads by name; density that only adds; fills and drop-outs before sections; another part's gaps; learns a beat | 2 |
| **Bassline** | Plays the song's harmony as a bass part | (none) | On the kick; walks into the next chord, which it sees coming | 2 |
| **Melody** | Steps you write, or it writes from the song and changes as it goes | Melodic Steps, SQ Sequencer (Max for Live) | Chord tones on strong beats; a motif and its varied repeat; changes in its own style; learns a clip; answers or counters another part | 2 |

**Elsewhere already, or later:**
- **Live's Envelope MIDI, Shaper MIDI and Expression Control** are modulators in Segno: the Envelope, the LFO's Drawn shape, and Expression (planned) ([ADR 0061](../decisions/0061-modulators-move-any-parameter-on-their-track.md)).
- **MPE Control** comes with MPE (M5). **Microtuner** comes with tunings (1.x).
- **Live's MIDI Tools** are the piano roll's tools (built), and any MIDI effect becomes one (§9.3).
- **Merged** (ADR 0011 rule 3, the same job): Pitch, Scale and Random all decide which note plays, so they're one module, and the name Random stays the modulator's. A step sequencer and a melody generator both make a melodic line from steps, so they're one: Melody. Rolls are repeats, so they're Note Echo's.
- **Named after the Generate tools** they share their algorithms with: Rhythm, Bassline and Melody are the piano roll's Generate tools played live, changing as they go.

### 6.1 Arpeggiator

- **Plays the notes held, one at a time:** Up, Down, Up-down, Down-up, Converge, Diverge, Played, Chord (all together), Random, and Shuffled (a random order kept until the notes change).
- **Rate** synced (1/1 to 1/64, dotted, triplets) or free; **Gate**; **Swing**; **Octaves**, each a transposition in semitones or steps of the key; **Hold**.
- **Retrigger:** a new note, every N bars, or each section.
- **Step lanes on its display:** up to 16 steps of Velocity, Gate, Octave, Ratchet and Chance. Each lane has its own length, so a 5-step accent lane drifts against a 16-step pattern, and the line keeps changing without being random (the NDLR's 5 against 8).
- **Follow:** with no notes held, it plays the chord sounding in the song, in a range you set. A pluck that arpeggiates the song's chords needs no clip of chords.

### 6.2 Chord

- **Intervals:** up to six notes added to each note, in semitones (as Live) or **in steps of the key**, so a third is major or minor wherever it lands. Each has its own velocity.
- **The song's chord:** each note played becomes the chord sounding in the song, voiced around it (the note on top, at the bottom, or nearest the last chord, voice-led with `libs/music`), as Cubase's pattern player re-voices a loop. One note from a rhythm part plays stabs that follow every change. **Richer:** adds the 7th or 9th the key allows. **At a change:** a held chord moves to the new one, holds, or plays again.
- **Keys to chords:** each note of the key plays its chord (I, ii, iii…), as a triad, 7th or 9th, voice-led: whole progressions with one finger.
- **Learn:** hold a chord and press Learn; its shape becomes the intervals.
- **Strum:** time, direction, curve; ahead of the beat for clips, which it reads ahead. **Voicing:** close, open, drop 2, spread.

### 6.3 Pitch

- **Transpose:** semitones, octaves, or steps of the key.
- **Fit:** off, to the key, to the chord's notes, or to the chord's notes on strong beats and the key between (passing notes stay passing notes); upward, downward or nearest. **At a change,** a held note moves, holds, or plays again: Cubase users' first complaint, made a choice.
  - **This makes any part follow the song's chords:** a riff copied from the verse into the chorus bends to the chorus's chords, live, with no chord track and no edit. (Studio Pro's Narrow mode does this from a chord track; its Parallel mode, which keeps a part's shape, needs to know what the part was written against, and comes later, §16.)
- **Mirror:** turns the melody upside down around a note, in the key: the classic inversion.
- **Range:** lowest and highest; notes outside fold by octaves, are dropped, or are held at the edge.
- **Random:** a chance, a range in steps of the key (or semitones), up, down or both, in the key or in the chord. A section of Pitch, not a module (M16).

### 6.4 Velocity

- **The curve drawn on its display,** which is the control. **Drive** and **compand**, input and output **ranges**, a fixed velocity, **random** with a chance.
- **Accents by the meter:** strong steps louder by their metric weight (downbeats, backbeats, offbeats), or a 16-step accent lane.

### 6.5 Note Length

- **Fixed** (synced or in ms), a **share** of the played length, **legato** (to the next note, which it reads ahead in clips), a maximum.
- **On release:** plays a note when a key is let go, at the release velocity, with a decay.

### 6.6 Note Echo

- **Echo:** time (synced or ms), repeats, velocity per repeat, and **pitch per repeat in steps of the key**: one note becomes a run up the scale.
- **Rolls:** while a note is held, repeats at a rate that speeds up or slows down along a curve (Pace), like a ball bouncing, with a velocity ramp (the T-1's Repeats, Bitwig's Dribble).
- **Canon:** up to three voices, each a delay, a step of the key away, mirrored, at half or double speed (Fugue Machine's playheads, in key). One phrase, and a round in three parts.

### 6.7 Groove

- **Swing** (8ths or 16ths), a **groove** from the Library's grooves, and an amount.
- **Another track's feel:** the groove taken from a track's notes, or from an audio clip's transients, which the audio editor finds (P6b): a programmed part locks to a recorded drummer.
- **Humanise** time, velocity and length with a drift that wanders slowly and is shared by the track (1/f), as a player's does, rather than jitter on every note.
- **Ahead of the beat:** clip notes are read ahead, so pushing them adds no latency. Notes played live can only be pulled.

### 6.8 Controllers

- **Eight knobs,** each sending a controller (a CC number, pitch bend or aftertouch), each named, automated and moved by modulators. For plugins and synths that answer controllers.

### 6.9 Rhythm

- **Up to eight lanes,** each on a pad or a note. On a Drum Kit, lanes find their pads by name (Kick, Snare, Hat, Clap…), so a kit swapped under them keeps playing the right parts.
- **Each lane:** steps, pulses and rotation (Euclidean), accents, rolls, chance and conditions, and steps set by hand over the pattern.
- **Density,** one knob for all lanes: every step has a rank, so turning it up only adds hits and the groove never reshuffles (Grids). It follows the section's Energy by an amount.
- **Styles:** a map of patterns (house, techno, breaks, hip-hop, trap, afro, Latin…) to start from and move between, as Reason's Beat Map and Grids have. Grids' map (GPL-3.0, its notices kept) can seed it; the rest is Segno's own.
- **Before a section changes:** a fill, a roll whose rate doubles, or a drop-out (§5.2).
- **Change** varies steps each lap. **Listen** to a part to fill its gaps or double its hits.
- **Learn:** drop a drum clip on it, and its lanes become that beat, ready to vary.

### 6.10 Bassline

- **Plays the song's harmony as a bass part:** roots, roots and fifths, octaves, arpeggios, walking, pedal, syncopated. Mostly root position, as pop is (94 % of its chords, [de Clercq and Temperley 2011](https://rockcorpus.midside.com/2011_paper/declercq_temperley_2011.pdf)).
- **Rhythm:** its own steps, or another track's (Listen): on the kick's hits, or between them.
- **Approach:** on the last beat before a change, into the next chord, which it sees coming: by the scale, chromatically, or from the fifth. **Anticipate:** lands a change an 8th or 16th early, as rock and pop do.
- **Range** around a note; **Density**, following Energy; **Change**; a drop-out before a drop.
- **A note in its clip** sets the bass note under that chord (an inversion, a pedal).

### 6.11 Melody

- **Steps you write, or that it writes:** up to 64 steps, lanes of Pitch (a chord tone, a step of the key, or a note), Velocity, Gate, Chance, Ratchet and Slide, each its own length.
- **Writes from the song:** a range, Density, Contour (rising, falling, arch, wave), steps or leaps, rests. It writes as a musician would: chord tones on strong beats, steps of the key between, a leap followed by a step back, phrases ending on the 1st, 3rd or 5th, a note leading into the next chord.
- **Phrase:** a motif and its varied repeat (AA′, AABA, ABAB, or free), because repetition is what makes a line sound meant.
- **Change:** how much each lap differs: at 0 the line loops; up to 100 % it's new every time; or **Shuffle**, the same notes in new orders (Marbles' three ways). **Lock** a step to keep it. Changes draw on the line's own intervals and rhythms, so a variation still sounds like the idea.
- **Climb:** moves the line by steps of the key each lap, back home at each section (the Metropolix's accumulator): a riff becomes a rising sequence.
- **Played notes** transpose it, as a sequencer's do, or restart it.
- **Learn:** drop a clip on it. On the pitch lane, the clip's notes become the steps; on the rhythm lanes, its rhythm; on both, the whole line. Change then varies your own idea, and one clip's notes on another's rhythm is a new idea.
- **With Listen on another part:** **Answer** plays while it rests; **Counter** plays against it, in contrary motion, consonant on strong beats.

## 7. Generators: when they play

- **Where their track has a clip** (the default; Logic's Session Players are regions for the same reason). An empty MIDI clip lets a generator play there: the arrangement says where the bass comes in.
- **Notes in the clip steer it:** they transpose or restart Melody, set the Bassline's bass note, add to Rhythm's hits. Notes played into the track steer it the same way while held.
- **Always** (a switch on each generator): plays whenever the song plays, for jamming.
- **What it plays is drawn in the clip,** faint, in the arrangement and in the clip view, so a clip with a generator never looks empty, and you see what you'll hear. Rendered by the same functions off the audio thread: the roll playing, the last one heard, or the kept one.
- **Other MIDI effects' notes** show in the clip view with View ▸ Played notes (off by default, as Others is), so a clip's own notes stay what you see unless you ask (MX13).

## 8. One vocabulary, and the fun parts

### 8.1 Words

| Word | Means | Where else |
|---|---|---|
| **Follow** | What it follows: Song, a track, Played, Own | – |
| **Rate** | Synced to the song, or free | Modulators, the Delay |
| **Chance** | How likely each step or note plays | The piano roll, modulators |
| **Conditions** | When a step plays: every Nth pass, first, fill, after another played, last bar of a section, in a section by name | – |
| **Change** | How much each lap differs: 0 loops, 100 % is new every time; or Shuffle | – |
| **Density** | How many notes, ranked, so more only adds | – |
| **Energy** | A section's; Density and Fill follow it by an amount | – |
| **Fill** | Something more (or a drop-out) before a section changes | – |
| **Vary** | A new pattern now, from the same settings | Racks |
| **Lock** | A step or lane that Vary and Change leave alone | Racks' macro locks |
| **Keep** | Holds the roll you heard | ADR 0062, modulators, the piano roll |

### 8.2 Rolls heard

- **Keep's menu lists the last 16 rolls heard,** each pass and loop lap ("This pass", "1 pass ago"…). Choosing one keeps it. Because randomness is computed from the roll, a roll heard four loops ago plays again exactly.
- It answers the complaint every generator draws, "it played something great a minute ago and it's gone", and no tool researched offers it.

### 8.3 Conditions

- **On any step** of the Arpeggiator's lanes, Rhythm and Melody: every Nth pass (A:B), first pass, fill, after the previous conditioned step played, after the lane above played, and the song's: the last bar of a section, in a section by name, the Nth time a section plays.
- Composed variation, not dice: a crash on the first bar of every chorus, a ghost note only with its flam, a fill only the second time the verse comes round.

### 8.4 Same section, same music

- **A generator's roll depends on the section's name,** without the copy's number: every Chorus plays the same chorus within a pass, every Verse its verse. Kept, they're the same every pass. Repetition is what makes a song a song; generated parts that never repeat sound like noodling.
- **Change continues within a section** (bar 1 to bar 8 of the chorus evolves), the same way in each one.
- **Switchable** per generator: by section name (the default), each section its own, or one through the whole song. A condition can still vary the second chorus on purpose.

## 9. Keeping what they play

### 9.1 Keep as notes

- **Module ▸ Keep as notes** (and the MIDI effect's right-click): the selected clips, the time selection, or the whole track get the notes the track's MIDI effects play, up to and including the selected one.
  - A clip's notes before are kept as a take, "Before MIDI effects" ([ADR 0032](../decisions/0032-takes-belong-to-clips.md)), as bounce keeps its source ([ADR 0035](../decisions/0035-bounce-keeps-its-source.md)): switch back any time.
  - The MIDI effects it kept are turned off, not removed.
  - One undo step.
- **It keeps the roll you heard** (the pass playing, or the last one), so what's kept is what you liked. Third-party MIDI effects are kept through a render.

### 9.2 Drag out what it played

- **A MIDI effect's display holds what it played,** the last 32 bars: drag a stretch of it into the arrangement as a clip. Let a generator run, hear four bars you love, drag them out.

### 9.3 Any MIDI effect as a tool

- **The clip view's Tools pane** gets **MIDI effects**: any of them, with any preset, run over the selected notes, previewed live, Apply one undo step, as the pane's tools are ([piano-roll.md](piano-roll.md) §25).
  - The Arpeggiator's best preset arpeggiates a clip in place; Pitch fits a melody to new chords for good; Groove gives a part another track's feel; Melody, learning the selection, writes a variation of it. None of them stays on the track.
  - **Generators offer three results at once,** side by side, ← and → for more: people pick from several rather than rolling one at a time (Hookpad's carousel; 23 % of its suggestions kept).
- **The algorithms move to `libs/music`** (JUCE-free), where the modules, the tools and assistants all run them. Arpeggiate, Strum, Humanize, Velocity and Randomise pitch become the Arpeggiator, Chord, Groove, Velocity and Pitch run over notes; Rhythm, Bassline and Melody are the same functions as the generators.

## 10. Players: parts that play along

- **Factory racks** in the Library, each an instrument with MIDI effects in front and effects after, and macros: Density, Change, Energy, Swing.
- About twenty, each following the song: a pad that holds the song's chords, a pluck arpeggiating them, piano stabs on a house rhythm, a walking upright bass, an acid line, trap hats with rolls, a breakbeat with fills, lo-fi keys.
- **Dropped on a track, a player plays along with the song,** in its key and chords, with no notes to write. That's what Logic's Session Players give, built from modules you can open, change and keep as notes, not a closed box.

## 11. Where they go

- **Before the instrument,** on any track. Dropped after it, a MIDI effect lands just before it, the drop line showing where. With no instrument, at the end of the chain.
- **In racks:** a chain's MIDI effects play into its instrument. **MIDI effect racks** (chains side by side, their notes merged): a chain an octave up with an echo beside the notes as played. Key and velocity zones come later.
- **On a Drum Kit's pad,** before its instrument: an echo on the snare alone, rolls on the hats.
- **Modulators move their parameters** like any others: an LFO on the Arpeggiator's gate, a Random on Melody's Change.
- **Turned off, removed or moved,** a MIDI effect ends the notes it started; nothing hangs (B55, item 2).

## 12. Seen working: the note display

- **One shared display,** on the module grid like any other: a strip of the bars just played and the next ones.
  - What came in, faint; what goes out, in the track's colour.
  - The chord now and the next one along the top, or what it fell back to ("No chords here: the key's I"); the section's end marked.
  - The playhead, and the steps and lanes where a module has them: the display is the control (the Arpeggiator's lanes, Rhythm's and Melody's steps are drawn on it).
- **Fed through the analysis bus,** as meters are; never through the device's state.
- It holds the last 32 bars it showed, which is what §9.2 drags out.

## 13. Browser, presets, menus and keys

- **Browser:** a **MIDI effects** part with its icon, A to Z, each module a folder of its presets ([ADR 0046](../decisions/0046-one-browser-modules-are-folders.md)). Players are in Racks. A preset's preview plays it on a short phrase, through a matching instrument, in the song's key.
- **Presets:** every module ships a designed set, to [ADR 0069](../decisions/0069-factory-presets-are-designed-and-never-clip.md)'s bar applied to notes: researched, using the module fully, right in any key.
- **Menu bar** ([ADR 0040](../decisions/0040-a-menu-bar-and-everything-by-mouse.md)):
  - Module ▸ **Add MIDI effect ▸** (before the instrument), **Keep as notes**, **Vary**, **Keep roll ▸** (with the rolls heard), **Learn from clip**;
  - Notes ▸ Transform ▸ **With a MIDI effect ▸**;
  - Song ▸ **Harmony…** (how fast chords may change);
  - Track ▸ **Harmony ▸** Auto, On, Off;
  - View ▸ **Show song chords** (the song row), **Show played notes** (the clip view), as Show automation lanes and Show other tracks' notes are named.
  - Each also where it's used: the module's right-click and surface, the track's right-click, the inspector.

## 14. Assistants (MCP, [ADR 0021](../decisions/0021-mcp-server.md))

- **Modules as today:** `add_module`, `set_parameters`, `load_preset`, `get_module`, `vary_module` and `keep_roll` (extended to MIDI effects and the rolls heard).
- **New:**
  - `get_song_context` (`start?`, `length?`): what MIDI effects read: the key, the chords with their times and bass notes, the sections with their Energy, and which tracks make the harmony;
  - `set_track_harmony` (`track`, `auto` / `on` / `off`); `set_section` gains `energy`;
  - `set_module_listen` (`module`, `track`), as `set_module_sidechain`;
  - `get_played_notes` (`track`, `start`, `length`): what the track's MIDI effects play there, without keeping it;
  - `keep_as_notes` (`track`, `clips?` or `start` and `length`, `upTo?`), as the menu;
  - `transform_notes` and `generate_notes` gain `module` and `preset`: any MIDI effect as a tool.
- An assistant can then hear a generator's idea, read it as notes, and keep the one asked for. A local model can drive the same tools as an assistant does (§16).

## 15. Engine and the device API

- **MIDI out in both hosts:** the DAW's adapter writes a module's events back into tracktion's MIDI for the next module; the plugin adapter into the host's buffer. The conformance suite gains MIDI checks (§17).
- **No stuck notes:** a shared ledger in `libs/device` ends a module's sounding notes on stop, a jump, a loop's wrap, a change that reroutes them, being turned off and being removed.
- **The song for modules,** additions under ADR 0034's rule 3 (nothing existing changes):
  - `ProcessContext::song`: a read-only view of the harmony (chords with their times and bass notes), the sections with Energy, the kit's pads, and this track's clip notes ahead; built on the message thread whenever they change and handed over with `AssetHandoff`; null in a plugin host;
  - `ProcessContext::listen`: the listened track's notes for the block, empty when nothing is chosen, and its notes ahead in the song view.
- **Ahead** is read from the song view, never by delaying notes: nothing adds latency. A note played early from the view is matched when it arrives, so it plays once.
- **Deterministic:** output depends on the input, the position, the song view, the parameters and the roll. Rendering any span gives what playback gave; Keep as notes and the notes drawn in clips use the same path offline.
- **The engine spike** (step 0) proves the hooks: MIDI between our modules in tracktion's chain, a track hearing another's notes (as the Follower's patch does for sound), generators in clips, notes read ahead and matched, and the ledger through stop, jump and loop.

## 16. Not now (backlog)

- **Local models** for continuing a phrase, filling a gap, accompanying, turning a rhythm into drums. The Anticipatory Music Transformer (Apache-2.0, code and weights) and Composer's Assistant 2 (MIT, trained on public-domain MIDI) could run offline as a process of their own that drives Segno through MCP, as an assistant does: no model bundled, no network ([ADR 0021](../decisions/0021-mcp-server.md), vision.md). The modules stay explainable algorithms.
- **Pitch's Parallel fit**, keeping a part's shape over new chords: it needs to know the chords a part was written against (the clip's own chord lane, where it has one).
- **Chords read from audio** (Logic 12's Chord ID). **Key changes along the song** (ADR 0057's later step). **Settings per clip** for generators (Logic's regions carry theirs). **A reharmonising walk** (P, L, R moves, voice-led). **Rhythms from physics** (bouncing balls, cells). **A CLAP extension** carrying the key, chords and sections, proposed to the CLAP project. **MIDI out to hardware** (a non-goal).

## 17. Tests (done when)

- **MIDI conformance** for every MIDI effect: the same notes for any block size and sample rate; every note-on matched by a note-off after stop, jump, loop and bypass, under random note streams; no allocation in `process()`; state round trip.
- **Golden notes:** each module's output for set inputs and songs, kept as event lists.
- **Render equals playback** with a kept roll; two passes differ without one; a roll heard is replayed exactly.
- **Context:** the harmony read from fixture songs (passing notes, gaps, inversions, a muted guide track); fills on the right bar; every Chorus the same; Listen refusing a loop; every module without a song (the plugin host's case).
- **Ahead:** a strum ahead of the beat and a pushed note play once, early, with no latency reported.
- **Keep as notes** gives the notes heard, with the source as a take, in one undo step.
- **MCP:** each feature driven through its tools.
- **UI:** the layout lint on every panel; screenshots zoomed in at standard and compact widths.

## 18. Steps

- **Step 0:** the engine spike (§15), and the UI drawn in every state with the real panel kit, for approval: the note display, the three modules of step 1, one generator, Keep as notes, View ▸ Song chords and Played notes.
- **Step 1, following the song:** MIDI out and the ledger; the song view, harmony (Follow, Harmony per track, Song chords), Listen; Keep as notes; the note display; **Pitch, Chord and Arpeggiator** (M4's three, deepened); the browser part and their presets; the plugins.
- **Step 2, generators:** **Rhythm, Bassline and Melody**, in clips and drawn there; Energy, conditions, same section same music, rolls heard, Learn; MIDI effects on pads and in racks; the players.
- **Step 3, the rest:** **Velocity, Note Length, Note Echo, Groove, Controllers**; any MIDI effect as a tool; MIDI effect racks.

## 19. Questions and recommendations

| # | Question | Recommendation |
|---|---|---|
| MX1 | **What they're called** | **MIDI effects:** the browser's part, Module ▸ Add MIDI effect ▸, a MIDI effect. What Live, Cubase and Logic ("MIDI FX") call them and what people search for. Generators are MIDI effects too, as Live's sequencers are; not "Note FX" (Bitwig's word) or "MIDI modules" |
| MX2 | **The set** | The eleven of §6 in three steps: following the song (Pitch, Chord, Arpeggiator), generators (Rhythm, Bassline, Melody), the rest (Velocity, Note Length, Note Echo, Groove, Controllers). Live's Envelope MIDI, Shaper MIDI and Expression Control are modulators; MPE Control waits for M5, Microtuner for tunings |
| MX3 | **Merges** | Pitch takes Live's Pitch, Scale and Random (one job: which note plays; and "Random" stays the modulator's, M16). Melody is the step sequencer and the melody generator (one job: a line from steps). Rolls are Note Echo's. Everything else stays apart (ADR 0011 rule 3) |
| MX4 | **Names** | Arpeggiator, Chord, Pitch, Velocity, Note Length, Note Echo (not "Echo", which finds the Delay's tape echo too), Groove, Controllers (it sends bend and aftertouch, not only CCs), Rhythm, Bassline, Melody (as the Generate tools they share algorithms with). All generic (ADR 0052) |
| MX5 | **Following the song** | By default, with settings of the module's own as the alternative, and the display saying when it falls back. As K10 already decided for M4's three |
| MX6 | **Where the song's harmony comes from.** (a) Read from the notes of the tracks that make it (Harmony: Auto, On, Off per track; a muted guide track counts). (b) A chord lane on the song row, kept by hand or filled from a clip, which effects follow (Cubase, Studio Pro, Logic; the research's recommendation). (c) One chosen harmony track | **(a)**, honestly weighed:<ul><li>**For (b):** familiar from three DAWs; explicit; works before any chord is written; the harmony can differ from what plays.</li><li>**Against (b):** a second copy of the harmony that drifts from the notes, which ADR 0058 chose against; chords edited twice; and most follow-chord complaints (Cubase) are about that drift and the mapping.</li><li>**For (a):** nothing to keep up; chords edited where they're written, with the piano roll's chord tools; ahead of the playhead; a silent guide track gives (b)'s freedom, made of notes.</li><li>**Against (a):** a reading can surprise (a melody note read as a change). Answered by the beat limit, passing notes, Harmony per track, and the chords shown on the song row.</li></ul>Revisit if people miss sketching chords with no track at all |
| MX7 | **How fast the song's chords may change** | At most once a beat by default; Song ▸ Harmony… sets a bar, half a bar, a beat or any note |
| MX8 | **No chords at a place** | The key's home chord (Logic's players do the same); without a key, the module's own chord. Said on the display |
| MX9 | **A note held across a chord change** | A choice in Pitch and Chord: move it, hold it, or play it again (move by default). Cubase users' first complaint, made a setting |
| MX10 | **Listening to another track** | A Listen picker in the header, as narrow as the Sidechain's (ADR 0025); loops refused; a muted track still heard. Not a parameter |
| MX11 | **Energy on sections** | Yes: one value per section (0–100 %, 50 by default) in the inspector's SECTION and `set_section`; generators' Density and Fill follow it by their own amount. Drawing energy as a line along the song row comes later (§16) |
| MX12 | **When generators play** | Where their track has a clip (the default; Logic's regions), or Always (a switch). The arrangement says where a part plays |
| MX13 | **Played notes drawn in clips** | For generators, on: a clip with only a generator's notes would look empty. For other effects, View ▸ Played notes (off by default), so the clip's own notes stay what you see |
| MX14 | **Same section, same music** | By section name, the copy's number dropped (the default); or each section its own, or one through the song |
| MX15 | **Rolls heard** | The last 16 in Keep's menu, played again exactly; no other tool offers it |
| MX16 | **Conditions** | Elektron's set (A:B, first, fill, previous, neighbour, each negated) and the song's (last bar of a section, a section by name, the Nth time) on every step lane |
| MX17 | **Keep as notes** | The notes before kept as a take (as bounce and REAPER do), the effects turned off, one undo step, the roll you heard; on the module's right-click and Module ▸; plus dragging what played off the display (Logic's capture buttons) |
| MX18 | **MIDI effects as tools** | Yes: a MIDI effects tile in the Tools pane runs any module and preset over the selection, previewed; generators show three results. The signed-off tools stay as they are; their algorithms move into `libs/music` and the modules reuse them |
| MX19 | **Where they go** | Before the instrument only (a drop after it lands before it, as Live allows no other). In racks' chains, on a Drum Kit's pads, and MIDI effect racks (parallel chains, notes merged) in step 3; key and velocity zones later |
| MX20 | **Players** | About twenty factory racks in the Library (step 2), each a part that follows the song, with Density, Change, Energy and Swing macros |
| MX21 | **Groove and M4's "groove & quantise"** | The Groove module is that plan's half for MIDI: grooves on a track, live. Audio keeps the audio editor's quantise. A song-wide groove that everything follows is left for M4's design |
| MX22 | **The plugins** | All eleven ship as plugins (CLAP, VST3, AU's MIDI-effect type, LV2), best effort: hosts differ, and Live 12 can't host MIDI-effect plugins at all. They read the host's chord where VST3 gives one |
| MX23 | **Machine learning** | None in the modules: they're explainable algorithms. Local models are in the backlog, driving Segno through MCP from a process of their own (§16); no model bundled ([vision.md](../vision.md)) |
| MX24 | **Going beyond Live** | [ADR 0074](../decisions/0074-midi-effects-know-the-song.md): this category goes beyond Live, as the Wavetable and Granular do (ADRs 0050, 0065); ADR 0011 holds everywhere else |
| MX25 | **Where it goes in the plan** | M3 P11, on this branch beside groups, as Granular and the audio editor were; the device suite's MIDI effects (M4, M6) are replaced by this set once approved |

## 20. Step 0a, the spike (2026-10-18)

The spike's checks are kept as tests (`tests/integration/MidiEffectSpikeTests.cpp`), on modules written for it (Up, Roots, Early, Answer, Hold and a probe that marks what reaches an instrument). What it settled for §15:

- **MIDI effects run in tracktion's chain as our modules.** The DAW's adapter hands a MIDI effect's notes on in place of the ones it was given, to the sample, and two of them add up (Up twice: two octaves). The track's audio passes a MIDI effect untouched: it's given no channels, and its bus passes them by.
- **A tracktion bug, fixed:** a plugin whose parameter moves (automation, a modulator) is processed in pieces of 128 samples, and the notes it passed on in a later piece came out early by the piece's offset. Any plugin passing notes on was affected; a MIDI effect would have played early whenever its knob moved. A one-line patch puts each piece's notes back on the block's time (`cmake/patches/tracktion_engine-sub-block-midi-time.patch`).
- **The song reaches a module whole** (`fd::SongView`, `ProcessContext::song`, handed over with `AssetHandoff`). A generator played the song's chord roots on the beats where its track has a clip, and nothing outside it: "in clips" (§7) needs no engine hook, only the clips' spans in the view.
- **Notes read ahead play early, once.** A module played the clip's notes a sixteenth early from the view and left each one out as it arrived on time. Chance on clip notes is decided where they play (ADR 0062), so in step 1 the view decides each note the same way, by the same pure function the clip's filter uses.
- **Listening to another track needs a patch of its own.** tracktion's sidechain mixes the other track's notes into the module's own, and a Drum Kit's rack passes no notes on at all. The new `tracktion_engine-note-sources.patch`:
  - a track whose notes a module hears sends them on a bus of their own from before its first plugin that takes notes and isn't a MIDI effect: after its MIDI effects, before its instrument;
  - before the listening module's block, a node hands it those notes (`ProcessContext::listen`) and passes its own input on unchanged;
  - heard after the other track's MIDI effects (its Up), to the sample, never mixed with the listener's notes;
  - a muted track others hear still plays its notes for them, silently, as a sidechain source keeps processing.
- **Run off the audio thread, a module plays what it played:** the same modules, fresh, fed a clip's notes block by block, gave the same notes to the sample as the render. This is how Keep as notes and the notes drawn in clips will run (§9, §7).
- **No note left hanging:**
  - **the song stops:** the adapter hands a MIDI effect an all-notes-off as playback stops, and its ledger (`fd::NoteLedger`) ends what it started;
  - **a jump:** tracktion's all-notes-off reaches the module the same way;
  - **turned off:** the Session's existing rule (a MIDI effect turned on or off ends its track's notes);
  - **removed:** new, the Session ends the track's notes when a module that takes notes leaves its chain, undo included. That closes B55's item 2;
  - **a loop's lap** ends the notes held from the lap before.
- **For step 1:**
  - the plugin adapter's MIDI out (CLAP, VST3, AU's MIDI-effect type, LV2), with the same conformance checks;
  - tracktion's MIDI buffers can grow, and allocate, the first block a module writes more notes than it was given: checked under the RealtimeSanitizer, and reserved when the graph is prepared if it shows;
  - the chance decision shared by the clip's filter and the song view;
  - the patch series: main has added `tracktion_engine-submix-sidechain.patch` since this branch began. Both lines patch `tracktion_EditNodeBuilder.cpp`, so the series is checked on a fresh source at the merge.

## 21. Step 0b, drawn (2026-10-18)

Every state drawn with the real panel kit and the real arrangement, for review (`tests/integration/MidiEffectUiTests.cpp`, `FD_DUMP_DIR`; the latest set in `build/dev/drawn/midi-effects/`). The modules' panels are drawn from their SPECs' parameters with a stand-in for the engine (what they'd hear and play), and each passes the module panel lint. The arrangement is drawn from a snapshot: the song's chords read by the real function, the notes MIDI effects play from a stand-in.

- **Pitch:** idle, the song's chords along the top; fitting a riff to the chords (the notes as written faint, as fitted bright); following a track's chords (Listen in the header); HARMONY open (its own key); in a plugin host (no song: its own key, said); folded.
- **Chord:** Add, Song and Keys, VOICES coming in ADD's place with the mode.
- **Arpeggiator:** the velocity lane (accents, the step playing), the gate lane (five steps against sixteen), the octave lane (both ways), Song chords (no notes held, the song's chords played).
- **Melody,** the generators' example: the pitch lane (a motif and its varied repeat), Write, Change and Play.
- **The arrangement,** at 1800 and 1280 px: the song's chords under the song row's overview; an empty Bass clip a generator plays in, its notes faint; a Lead riff with what Pitch makes of it behind it.

What drawing settled:
- **The note display** (`ui::NoteDisplay`, the panel kit): two bars back and half a bar ahead of the playhead, rows fitted to the notes in sight (an octave at least), what came in in a ghost of the text colour, what goes out in the value colour by velocity, bar and beat lines, the section's end, the playhead. Its label line: the chord now → the next, or why there's none; what it follows at the right. With nothing in sight, only "Nothing played yet": nothing is drawn through it.
- **The step display** (`ui::StepDisplay`): the note display with a lane over its upper part and the notes going out under it. Bars from the floor (from the middle for a lane both ways; notes on rows for a pitch lane); a step at its neutral value grey, since it changes nothing; the step playing the brightest; the steps past the lane's length shaded, its length a line after the last, dragged.
- **One base for MIDI effects' panels** (`ui::MidiEffectPanel`): NOTES or STEPS, HARMONY, and Listen.
  - **HARMONY** is the same section in every module, collapsed by default: the song is what it follows, and the display says so. The kit gained sections collapsed by default (`PanelSection::setCollapsedByDefault`; the view state lists such a section under `"expanded"` when it's open; device-panels.md).
  - **Listen** is the header's sidechain picker, captioned Listen (ADR 0025's place), in the same place in every MIDI effect. It shows while what it hears is used: Follow on Track, or a module's own use (Melody's Answer). Otherwise it takes no room and the presets have it: a fixed picker saying "None" doesn't earn its place.
- **Combos carry their names inside** ("Change  Move", "Style  Up"), short where cells are few ("Out  Fold"). HARMONY's scale shows its name alone: the longest names don't fit beside "Scale".
- **The Chord's strum** under a STRUM sub-label, its direction just above its knobs (Time, Curve, Swell): above Mode's row, it read as part of Mode.
- **The song's chords on the song row** (View ▸ Show song chords): a band of 14 px (`shell.songChordHeight`) above the row's divider, the row as much taller while it shows. Each chord's name at its start after a tick, left out where it doesn't fit; the tick stays.
- **The song's harmony read for real** (`model::songHarmony`, in the snapshot): the tracks that play chords somewhere (three notes together; never a Drum Kit's), every lap of a looped clip, named in the key; a chord shorter than a beat joins the chord before it.
- **Played notes** (what a track's MIDI effects play in a clip, `ClipInfo::played`): in the arrangement, a ghost of the clip's colour behind its own notes, so an empty clip a generator plays in never looks empty; in the clip view (View ▸ Show played notes), outlines in the clip's colour, so they read apart from Others' filled notes.
- **For tests and snapshots:** `DevicePanel::refreshNow` does what the panel's refresh tick does, on screen or not.
- **Not drawn:** Keep as notes is a menu item and a right-click item, built with step 1.

**Approved by the product owner (2026-10-18):** the drawings and the SPECs of step 1's three ([Pitch](../../devices/pitch/SPEC.md), [Chord](../../devices/chord/SPEC.md), [Arpeggiator](../../devices/arpeggiator/SPEC.md)); their IDs are permanent (docs/ids.md). Melody's SPEC is an early draft, drawn to show a generator, and is reviewed with step 2.

## 22. Step 1a, the modules (2026-10-18)

Pitch, Chord and Arpeggiator are built and registered: in the DAW's device list, as plugins, and under the conformance suite. The DAW's side (placement before the instrument, the song view handed over, Harmony per track, Listen, the display's feed, the browser and menus, assistants) is step 1b; Keep as notes and the presets are 1c.

- **Their theory is `libs/music`'s,** pure and allocation free, for the clip view's tools and assistants too (principle 9): steps of a key, fitting, mirroring (`Following.h`); Pitch's note as two functions, moved (Mirror, Semitones, Steps, Random) then fitted (Fit, Range), so a chord change fits a held note again from where it was before it was fitted; a chord of a kind, its third, fifth, seventh and ninth from the chord where it has them, else from the key (Chord's Kind; a sus chord keeps its 4th); voicings around a played note or near a note (`voiceAround`, `voiceNear`); the arpeggio's orders (`Arpeggio.h`).
- **What they share** (`devices/midi-shared/dsp`, JUCE-free):
  - **`NoteEffect`,** the base: a clock in samples, its own beats while the song is stopped (an arpeggio under your hands at the song's tempo), its roll (ADR 0062: the host's pass, or its own each time playback starts), and its notes written through the ledger. **A note has owners:** one played twice (two notes fitted to the same one, a step overlapping the next with Gate past 100 %) starts again, and ends when the last owner lets go, so a note still held is never cut by another's end. All notes off ends everything and the module's state.
  - **`Follower`:** the key (the song's, or its own when it has none or Key is Own) and the chord now. Chords from notes held (Follow on Track or Played) **stay after the notes end,** until new ones come: chords played short don't fall back to the key's between them.
  - **`Moments`:** a block in time order. At one sample the chord settles first (the listened track's notes, the notes below the split, the song's change), then the notes coming in: a note on a change plays the new chord. A module's own later notes (a strum's, a step's end) go before what comes in when they're ends, after when they're starts.
  - **A beat on a sample lands on it however the blocks are cut** (rounded, never truncated): steps and changes are the same to the sample for any block size and after a jump.
- **Settled while building:**
  - **Parameter IDs are lowercase and dotted,** as docs/ids.md has them: the approved SPECs had `inKey`, `songChords` and `velocityLength`; they're `inkey`, `song.chords` and `velocity.length` (likewise each lane, and Melody's `change.mode`). Corrected before any build used them.
  - **Passing notes** (Pitch): a note off the beat fits to the key. "Off the strong beats" would have made every second beat's note a passing note.
  - **Swing** (Arpeggiator): every odd step later by up to half a step: 50 % is a triplet's feel, 75 % a dotted one.
  - **Free rate** (Sync off): its clock starts with the first note, so the first step plays as you press it.
  - **Song chords** (Arpeggiator): the pattern runs on through a chord change, as through new notes with Retrigger off; Retrigger on bars or sections lines it up with the form.
  - **Keys** (Chord): the chords sit around Around wherever the key is played; the octave you play in doesn't move them.
  - **Strum:** Curve above zero speeds up through the chord; Swell only with a strum.
  - **Learn** (Chord) writes the shifts in semitones from the lowest note held and turns In key off, one undo step; a single note learns nothing.
  - **Drawing a lane's step** writes the lane to the module's state through the host binding.
- **The plugins:** MIDI effects with no audio bus (AU's MIDI processor, CLAP's note effect, VST3, LV2), and no standalone. A host has no song: they follow their own key and chord, or the chord played below the split.
- **Tests:** each module's unit tests (the pure functions, every note ended under random streams and settings, timing to the sample); the conformance suite's MIDI checks for every MIDI effect (the same notes for any block size, every note ended through stops and jumps, state and reset reproducing the notes, no allocation); the plugin adapter's notes out.
