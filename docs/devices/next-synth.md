# The next synth: which gap to fill

- **Status:** for review by the product owner (2026-10-16), on the `synth` branch
- **Asked for:** "another instrument, another synth … what kind of synth would best fit here to fill in the biggest gap now"; not drums, which main is designing (the Drum Kit, [drum-kit.md](../design/drum-kit.md), ADR 0064, proposed)
- **What follows approval:** the chosen synth's `devices/<name>/SPEC.md` from the [template](SPEC-TEMPLATE.md), researched and drawn as the Wavetable's was, before any code

## 1. What Segno plays today

| Instrument | What it covers |
|---|---|
| Subtractive | Basses, leads, pads, plucks: Drift's job, light and immediate |
| Wavetable | Deep modern sound design: 3 oscillators with FM, PM, RM and AM between them, 2 filters, a full modulation system, a table editor ([ADR 0050](../decisions/0050-the-wavetable-synth-goes-beyond-live.md)) |
| Sampler and the Library | 48 acoustic instruments from VSCO 2 CE and VCSL: pianos, harpsichord, organs, strings, woodwinds, brass, mallets, an orchestral kit |

Drums are being designed on main. Still planned ([device-suite.md](device-suite.md)): FM, Granular and the Subtractive's stage 2 (M5); Meld-, Collision-, Tension- and Electric-like instruments (1.x).

## 2. The gaps, by sound

| Sound | Covered today by | Gap |
|---|---|---|
| **Textures, grains, evolving pads from recordings** | Nothing: the Sampler plays its samples straight, the Wavetable loops single cycles | **Total** |
| **Electric pianos** | Nothing: the Library's pianos are acoustic | **Total**, and only a modelled or sampled electric piano fills it properly |
| FM bells, mallets, basses, DX-style pianos | The Wavetable's FM and PM warps, with presets: an FM piano or bell is two of its oscillators and an envelope on the warp | Partial |
| Plucked, struck and bowed models | The Library's samples, the Subtractive's plucks | Partial |
| Analog polys, basses, leads | Subtractive, Wavetable | None |

## 3. What others ship (researched 2026-10-16)

- **The new work in 2025–26 went into drums and granular, not new synths.** Live 12.2–12.4, Logic 12, Cubase 15, Studio Pro 8 and Reason 14 added no new synth.
- **Granular is now nearly universal:**
  - Live Suite has Granulator III ([Ableton](https://www.ableton.com/en/live-manual/12/max-for-live-devices/)).
  - Bitwig 6.1's Sampler gained Fragments, up to 256 grains a voice ([MusicTech](https://musictech.com/news/gear/bitwig-studio-6-1/)).
  - Logic has Alchemy, whose 12.3 update added pitched grains ([Synth Anatomy](https://synthanatomy.com/2026/06/apple-logic-pro-12.html)).
  - Cubase has Padshop 2, Reason has Grain, FL has Fruity Granulizer.
  - Serum 2 (2025) and Absynth 6 (2025) added granular engines.
  - Of the DAWs looked at, only Studio Pro has none.
- **FM is common but not universal:** Live (Operator), Bitwig (FM-4, Phase-4), Logic (EFM1), FL (Sytrus) and Reason (Algoritm) have one; Cubase and Studio Pro don't.
- **Modelled keys are rarer:** Live's Electric, Logic's vintage keys; nobody else.
- **What producers use:** only anecdotal. Live's Drift, Wavetable and Analog are liked; Operator divides opinion ([Elektronauts](https://www.elektronauts.com/t/is-it-me-or-abletons-synths-feel-liveless/221838)). There's no usage data on granular; the signal is that every vendor added it in 2025–26.

### What free software already has

| Kind | FOSS today | For a Segno user |
|---|---|---|
| **Granular** | **Weakest:** gRainbow's last release was in 2024; nothing maintained and polished | **A real gap** |
| Modelled electric piano | None maintained; Dexed's DX pianos are the nearest | A gap, but an expensive one to fill |
| FM | Strong: Dexed (DX7-style, 2025), Six Sines (6 operators, 2026), Surge's FM oscillators | A great free option exists |
| Physical modelling | Decent: RipplerX (2026), Uhhyou's synths | Some exists |
| VA, wavetable | Strong: Surge XT, OB-Xf, Odin 2 | Covered, and Segno has both |

## 4. The candidates

| | Gap in Segno | Gap in FOSS | Every DAW has it | Cost | What it unlocks |
|---|---|---|---|---|---|
| **Granular** | **Total** | **Weakest** | Nearly all | Moderate to high: a grain engine dense enough for clouds, its display, its CPU | Pads and textures from the Library's 48 instruments at once, and from any clip in the project |
| FM | Partial (the Wavetable's FM) | None | Most | Small to moderate | Bells, mallets, FM basses, DX-style pianos, with per-operator envelopes |
| Electric piano | Total | Yes | Few | High: physical modelling, hard to A/B against Electric | Rhodes- and Wurlitzer-style keys |
| Mallets and resonators | Partial | Some (RipplerX) | Some | High | Struck and plucked models |
| Meld-like | Little (overlaps the Wavetable) | Partial | Live only | High | MPE macro-oscillators |
| Subtractive stage 2 | None | None | – | Moderate | Analog's depth |

## 5. Recommendation: Granular

**Build Granular now, on this branch.**

- **Nothing in Segno does what it does.** Every other candidate adds a flavour of a sound Segno can already approach. Granular turns recordings into instruments: pads, drones, shimmering textures, frozen moments.
- **It's free software's weakest spot.** As a plugin it's useful in every DAW, and there's no maintained, polished free granular synth.
- **It's expected now:** Live, Bitwig, Logic, Cubase, FL and Reason all have one, and the newest work in the field is there.
- **Segno already has great material for it:**
  - the Library's 48 sustained acoustic instruments: violins, flutes, organs and brass make pads and textures straight away, so its factory presets need no new content;
  - any clip in the project, as the Wavetable makes a table from one (table-editor.md, Clip ▸ Make wavetable).
- **What the integrated DAW adds, cheaply:**
  - grain pitch spread that lands on the song's key ([ADR 0057](../decisions/0057-the-song-has-one-key.md)), so a cloud stays in tune with the song;
  - the source drawn with its grains as they play, its position dragged on the display;
  - the audio editor's analysis (transients, pitch) to find the good places in a file;
  - modulators on position and density ([ADR 0061](../decisions/0061-modulators-move-any-parameter-on-their-track.md)).
- **It stays clear of main.** It's a new device under `devices/` with its plugin and presets. It reads samples through the Sampler's loading code but doesn't change it, since main's Drum Kit is built on the Sampler.

**What it costs, and what it doesn't do:**
- Fewer tracks need it than need a bass or a piano: it's a sound-design and texture tool.
- Its engine is the most CPU-hungry so far: a dense cloud is hundreds of grains a voice. Its CPU shows in its header as the Wavetable's does (W13).
- It doesn't fill the electric piano gap. If that's the gap you feel most, see the runners-up.

**The runners-up:**
- **FM** is cheaper and fills bells, mallets, basses and DX-style pianos. But a free one exists, and the Wavetable already plays much of it. Until FM comes, a set of Wavetable presets (an FM piano, bells, an FM bass) covers the common sounds cheaply.
- **A modelled electric piano** is the only real fix for electric pianos. It's the most expensive candidate and the hardest to sign off by ear, so it stays in 1.x unless you want it sooner.

## 6. What Granular would be (for the SPEC)

An outline only. The SPEC settles it after research into Granulator III, Bitwig's Fragments, Alchemy, Padshop and Reason's Grain.

- **Live target: Granulator III** (ADR 0011: match Live). Its modes as the starting point: grains for classic granular, a loop for slow scanning, a cloud for density.
- **Sources:** any audio file or clip, the Library's instruments dropped on it, a sample loaded from the browser.
- **The engine:** position, spray, grain size, density, shape, pitch with spread, stereo spread, reverse grains; a filter and envelopes; MPE (pressure and slide to position and size).
- **Beyond Live, where it's cheap:**
  - pitch spread in the song's key;
  - Clip ▸ Make granular, an audio clip into a Granular on a new track;
  - the display as a control.
- **Later stages, if wanted:** live capture of an input (Granulator III does it), several sources.
- **About 40 factory presets** made from the Library's instruments.
- **ID `granular`**, named Granular ([device-suite.md](device-suite.md)).

## 7. Questions and recommendations

| # | Question | Recommendation |
|---|---|---|
| N1 | **Which synth next** | Granular (§5); FM after it, with FM-style Wavetable presets in the meantime |
| N2 | **When** | Now, on the `synth` branch, moved up from M5 into M3 as P7, as the Wavetable was (P1) |
| N3 | **How deep** | Match Granulator III (ADR 0011), going past it only where the DAW makes it cheap: the song's key, Make granular from a clip, the display as a control |
| N4 | **Its sources** | Files, clips and the Library's instruments; live capture as a later stage |
| N5 | **The Sampler's code** | Read samples through it, never change it: main's Drum Kit is built on the Sampler |
| N6 | **Factory presets** | About 40, from the Library's instruments, so they need no new content |

## Sources

Researched on the web on 2026-10-16. Unverified: Logic's full current instrument list (Apple's guide pages returned errors), Studio Pro's list (retailers' pages only).

- Ableton: [release notes](https://www.ableton.com/en/release-notes/live-12/), [editions](https://www.ableton.com/en/live/compare-editions/), [Max for Live devices](https://www.ableton.com/en/live-manual/12/max-for-live-devices/)
- Bitwig: [devices](https://www.bitwig.com/userguide/latest/device_descriptions), [what's new](https://www.bitwig.com/whats-new/), [6.1 at MusicTech](https://musictech.com/news/gear/bitwig-studio-6-1/)
- Logic: [Synthtopia on Logic 12](https://www.synthtopia.com/content/2026/01/30/apple-logic-pro-12-mainstage-4-now-available-heres-whats-new/), [Synth Anatomy on 12.3](https://synthanatomy.com/2026/06/apple-logic-pro-12.html), [plug-ins](https://www.apple.com/logic-pro/plugins-and-sounds/)
- FL Studio: [generators](https://www.image-line.com/fl-studio-learning/fl-studio-online-manual/html/generator_plugins.htm)
- Cubase: [15 at MusicTech](https://musictech.com/news/music/cubase-15-everything-you-need-to-know/)
- Reason: [Reason 14](https://www.reasonstudios.com/press/reason-14-available-worldwide)
- Others: [Serum 2](https://www.kvraudio.com/news/xfer-records-releases-serum-2---free-update-63286), [Absynth 6](https://www.kvraudio.com/news-print.php?id=65628)
- Use: [Elektronauts](https://www.elektronauts.com/t/is-it-me-or-abletons-synths-feel-liveless/221838), [Gearnews](https://www.gearnews.com/best-daw-stock-synths)
- FOSS: [Six Sines](https://synthanatomy.com/2026/07/baconpaul-six-sines-free-open-source-tx81z-style-mpe-fm-synthesizer.html), [RipplerX](https://synthtopia.com/content/2025/02/17/free-open-source-physical-modeling-synth-ripplerx), [Vital's source](https://forum.vital.audio/t/is-vital-actually-open-source/1396)
