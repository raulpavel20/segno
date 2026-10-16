# The next synth: which gap to fill

- **Status:** for review by the product owner (2026-10-16), on the `synth` branch
- **Asked for:** "another instrument, another synth … what kind of synth would best fit here to fill in the biggest gap now"
- **What follows approval:** the chosen synth's `devices/<name>/SPEC.md` from the [template](SPEC-TEMPLATE.md), researched and drawn as the Wavetable's was, before any code

## 1. What Segno plays today

| Instrument | What it covers |
|---|---|
| Subtractive | Basses, leads, pads, plucks: Drift's job, light and immediate |
| Wavetable | Deep modern sound design: 3 oscillators with FM, PM, RM and AM between them, 2 filters, a full modulation system, a table editor ([ADR 0050](../decisions/0050-the-wavetable-synth-goes-beyond-live.md)) |
| Sampler and the Library | 48 acoustic instruments from VSCO 2 CE and VCSL: pianos, harpsichord, organs, strings, woodwinds, brass, mallets, an orchestral kit |

And planned ([device-suite.md](device-suite.md)): the Drum Sampler and Drum Rack (M4); the Drum Synth, FM, Granular and the Subtractive's stage 2 (M5); Meld-, Collision-, Tension- and Electric-like instruments (1.x).

## 2. The gaps, by what a track needs

| Sound | Covered today by | Gap |
|---|---|---|
| **Electronic drums** (kick, snare, clap, hats, toms, cymbals) | Nothing. The Library has an orchestral kit only; no electronic samples, no drum synth, no Drum Rack | **Total** |
| Keys: electric pianos, bells, FM basses | Acoustic pianos and organs as samples; the Wavetable's FM warps get near some FM tones, without per-operator envelopes | Partial |
| Textures and grains | Nothing (the Sampler plays samples straight) | Total, but for fewer tracks |
| Plucked, struck and bowed models | The Library's samples, the Subtractive's plucks | Partial |
| Analog polys, basses, leads | Subtractive, Wavetable | None |

Drums are the gap you hit in the first minutes of making a beat. Almost every genre Segno is for needs them, and today a track can't have them without a third-party plugin or your own samples. That breaks Tier 1's promise, "enough to finish a track using only our devices", and the M3 exit track itself (Q7: "a short track built with presets from the browser").

## 3. What others ship (researched 2026-10-16)

- **The new work in 2025–26 went into drums and granular, not new synths.** Live 12.2–12.4, Logic 12, Cubase 15, Studio Pro 8 and Reason 14 added no new synth.
- **Drum synthesis became standard:**
  - Bitwig 6 has 31 drum-synth devices in four families (808- and 909-style, digital, its older E-drums) beside its Drum Machine container ([Bitwig](https://www.bitwig.com/userguide/latest/device_descriptions)).
  - Cubase 14 added a Drum Machine with synth engines on its pads ([Sound On Sound](https://www.soundonsound.com/techniques/cubase-14-new-drum-machine)).
  - Live Suite has eight DS devices (Kick, Snare, HH, Clap, Cymbal, Tom, FM, Clang) for its Drum Rack ([Ableton](https://www.ableton.com/en/live-manual/12/max-for-live-devices/)).
  - Logic has Drum Synth inside Drum Machine Designer ([Apple](https://www.apple.com/logic-pro/plugins-and-sounds/)).
  - FL Studio has Drumaxx, BassDrum, Fruit Kick and others ([Image-Line](https://www.image-line.com/fl-studio-learning/fl-studio-online-manual/html/generator_plugins.htm)).
  - Of the DAWs looked at, only Studio Pro has none.
- **Granular moved into samplers:** Bitwig 6.1's Sampler gained Fragments ([MusicTech](https://musictech.com/news/gear/bitwig-studio-6-1/)); Logic 12.3's Alchemy gained pitched grains ([Synth Anatomy](https://synthanatomy.com/2026/06/apple-logic-pro-12.html)); Serum 2 and Absynth 6 added granular too.
- **FM is common but not universal:** Live, Bitwig, Logic, FL and Reason have one; Cubase and Studio Pro don't.
- **What producers use:** only anecdotal. Most start drums from samples and some synthesise them; a paid kick-synth niche exists ([KVR](https://www.kvraudio.com/forum/viewtopic.php?p=9238318), [MusicTech](https://musictech.com/guides/buyers-guide/best-kick-drum-plugins-2026-tested-reviewed)). Live's Drift, Wavetable and Analog are liked; Operator divides opinion ([Elektronauts](https://www.elektronauts.com/t/is-it-me-or-abletons-synths-feel-liveless/221838)).

### What free software already has

| Kind | FOSS today | For a Segno user |
|---|---|---|
| FM | Strong: Dexed (DX7-style, 2025), Six Sines (6 operators, 2026), Surge's FM oscillators | A great free option exists |
| VA, wavetable | Strong: Surge XT, OB-Xf, Odin 2; Vital's source stalled since 2023 | Covered, and Segno has both |
| Physical modelling | Decent: RipplerX (2026), Uhhyou's MembraneSynth and GenericDrum | Some exists |
| **Drum synthesis** | **Weak:** Geonkick is the one maintained drum synth; no polished cross-platform 808/909-style set | **A real gap** |
| **Granular** | **Weakest:** gRainbow's last release was in 2024; nothing maintained and polished | **A real gap** |
| Modelled electric piano | None maintained; Dexed's DX pianos are the nearest | A gap, but an expensive one to fill |

## 4. The candidates

| | Gap in Segno | Gap in FOSS | Every DAW has it | Cost | What it unlocks |
|---|---|---|---|---|---|
| **Drum Synth** | **Total** | **Weak** | Nearly all | Moderate: the hats and cymbals are the hard part | Beats today; M4's Drum Rack and Push 2's drum pads; the Library's electronic kits, made from it |
| Granular | Total, fewer tracks | Weakest | Nearly all | Moderate to high: Live's Granulator III has three modes and live capture | Textures from the Library's sustains and any clip |
| FM | Partial (the Wavetable's FM) | None | Most | Small to moderate | Electric pianos, bells, metallic basses |
| Electric piano | Partial (FM would cover DX pianos) | Yes | Few | High: physical modelling, hard to A/B against Electric | Rhodes and Wurlitzer-style keys |
| Subtractive stage 2 | None | None | – | Moderate | Analog's depth |
| Meld-like | Little (overlaps the Wavetable) | Partial | Live only | High | MPE macro-oscillators |

## 5. Recommendation: the Drum Synth

**Build the Drum Synth now, on this branch.** Of the candidates it's the only one that fills a gap you can't work around today, and one that free software leaves open as well.

- **The biggest gap.** No electronic drums at all is the most visible thing Segno can't do. Every other candidate adds a flavour where Segno already has a way to make the sound.
- **It fills a FOSS gap too.** As a plugin it's useful in every DAW, and there's no polished free equivalent.
- **It works with what's coming.**
  - It's the instrument M4's Drum Rack is for.
  - Push 2's drum pads need something to play.
  - The piano roll's Generate ▸ Rhythm and the Steps modulator have something to drive.
- **It makes the Library's electronic kits.** CC0 electronic drum samples are scarce, while one-shots rendered from the Drum Synth's presets are Segno's own by construction. M4's Drum Sampler and Drum Rack then ship with kits on day one ([ADR 0055](../decisions/0055-factory-content-is-files-in-a-read-only-library.md)).
- **It stays clear of main.** It's a new device under `devices/` with its plugin and its presets; the DAW gains it through the module list, so the merge stays small.

**What it costs, and what it doesn't do:**
- Until M4's Drum Rack, a kit is a track per drum: racks get key zones only with the Drum Rack ([racks.md](../design/racks.md) R16). That's workable on an arrangement (a drum per track is how many people mix anyway), but writing a beat across five tracks is slower than on one.
- Most producers start from samples, so the Drum Synth alone isn't the whole drum story: it's half of it, and it makes the other half (the kits).
- Making hats and cymbals sound good is hard. They'd be A/B'd against Live's DS devices before sign-off, as every module is.

**The runners-up, in order:**
- **Granular** fills the biggest FOSS gap and is now in every major DAW. Next after the Drum Synth, as its own instrument (ADR 0011 keeps it apart from the Sampler).
- **FM** comes later: a free one exists, and the Wavetable already does some of its job.

## 6. What the Drum Synth would be (for the SPEC)

An outline only. The SPEC settles it after research into Live's DS devices, Bitwig's drum families, Logic's Drum Synth and Geonkick.

- **One drum per instance,** with a voice selector: Kick, Snare, Clap, Hi-hat, Tom, Cymbal, FM and Metal (Live's eight). Each voice keeps its own parameters, so changing the voice never loses settings, and every parameter can be automated.
- **About as deep as Live's DS devices** (each voice on one panel, six to ten controls), a little beyond where it's cheap:
  - the hit drawn as it plays: its pitch and amplitude envelopes, dragged on the display, as our envelopes are;
  - velocity and an accent;
  - the hi-hat open and closed on two notes of one instance, the closed cutting the open, so a hat needs no choke groups.
- **About 40 factory presets** (five a voice), then three electronic kits rendered from them into the Library.
- **ID `drum-synth`**, named Drum Synth ([device-suite.md](device-suite.md); the name keeps "Synth", which says what it is).

## 7. Questions and recommendations

| # | Question | Recommendation |
|---|---|---|
| N1 | **Which synth next** | The Drum Synth (§5); Granular after it, then FM |
| N2 | **When** | Now, on the `synth` branch, moved up from M5 into M3 as P7, as the Wavetable was (P1) |
| N3 | **One drum or a kit per instance** | One drum per instance with a voice selector, as Live, Bitwig and Logic do. The kit is the Drum Rack's job (ADR 0011: a device and the rack shouldn't both make kits); until M4, a track per drum |
| N4 | **Which voices** | Live's eight: Kick, Snare, Clap, Hi-hat, Tom, Cymbal, FM, Metal |
| N5 | **How deep** | Match Live's DS devices (ADR 0011), and go past them only where it's cheap: the drawn, draggable hit, velocity and accent, the self-choking hat. Bitwig's 31 devices stay a later stage, if they're wanted |
| N6 | **The Library's electronic kits** | Yes: three kits rendered from the presets, for M4's Drum Sampler and Drum Rack |
| N7 | **Drum Rack sooner** | Recommend making the Drum Rack M4's first DAW step, so the Drum Synth plays as a kit soon after it ships |

## Sources

Researched on the web on 2026-10-16. Unverified: Logic's full current instrument list (Apple's guide pages returned errors), Studio Pro's list (retailers' pages only), which Reason edition has which instrument.

- Ableton: [release notes](https://www.ableton.com/en/release-notes/live-12/), [editions](https://www.ableton.com/en/live/compare-editions/), [Max for Live devices](https://www.ableton.com/en/live-manual/12/max-for-live-devices/)
- Bitwig: [devices](https://www.bitwig.com/userguide/latest/device_descriptions), [what's new](https://www.bitwig.com/whats-new/), [6.1 at MusicTech](https://musictech.com/news/gear/bitwig-studio-6-1/)
- Logic: [Synthtopia on Logic 12](https://www.synthtopia.com/content/2026/01/30/apple-logic-pro-12-mainstage-4-now-available-heres-whats-new/), [Synth Anatomy on 12.3](https://synthanatomy.com/2026/06/apple-logic-pro-12.html), [plug-ins](https://www.apple.com/logic-pro/plugins-and-sounds/)
- FL Studio: [generators](https://www.image-line.com/fl-studio-learning/fl-studio-online-manual/html/generator_plugins.htm), [2026 at Rekkerd](https://rekkerd.org/image-line-launches-fl-studio-2026-redesigned-flex-secure-cloud-project-backup-smart-assistance-more/)
- Cubase: [15 at MusicTech](https://musictech.com/news/music/cubase-15-everything-you-need-to-know/), [14's Drum Machine at Sound On Sound](https://www.soundonsound.com/techniques/cubase-14-new-drum-machine)
- Reason: [Reason 14](https://www.reasonstudios.com/press/reason-14-available-worldwide)
- Use: [KVR thread](https://www.kvraudio.com/forum/viewtopic.php?p=9238318), [Elektronauts](https://www.elektronauts.com/t/is-it-me-or-abletons-synths-feel-liveless/221838), [Gearnews](https://www.gearnews.com/best-daw-stock-synths), [kick plugins 2026](https://musictech.com/guides/buyers-guide/best-kick-drum-plugins-2026-tested-reviewed)
- FOSS: [Six Sines](https://synthanatomy.com/2026/07/baconpaul-six-sines-free-open-source-tx81z-style-mpe-fm-synthesizer.html), [RipplerX](https://synthtopia.com/content/2025/02/17/free-open-source-physical-modeling-synth-ripplerx), [Vital's source](https://forum.vital.audio/t/is-vital-actually-open-source/1396)
