# 0052. Generic names are free to use; names a vendor made up aren't

- **Status:** Accepted (product owner, 2026-10-14); drum machine numbers allowed as terms of the trade by [0067](0067-drum-machine-numbers-are-terms-of-the-trade.md)
- **Date:** 2026-10-14
- **Amends:** [0008](0008-naming-and-trade-dress.md): its list of names, and its rule that every device's final name be distinctive

## Context
- ADR 0008 keeps Ableton's device names out of ours. Its list mixes two kinds of name:
  - generic terms of the trade (Sampler, Wavetable, Echo, Auto Filter);
  - names Live made up (Operator, Drift, Simpler, Roar, Glue, EQ Eight, Hybrid Reverb, Drum Rack).
- It also asks for a distinctive brand name for every device before the first release.
- The product owner dropped "Synth" from the instruments' names (Subtractive, Sampler, Wavetable). They pointed out that Sampler and Wavetable are generic terms used across the industry: naming a device for what it is isn't copying Live.
- A generic term says what kind of device something is: many DAWs and plugins have a sampler, a wavetable synth, a delay, a compressor. A name a vendor invented is what identifies that vendor's product, and that's what ADR 0008 is there to protect against.

## Decision
- **Generic terms are allowed**, as final names and not only as working ones. A generic term is the trade's name for a kind of device, or for how it makes or changes sound: Sampler, Wavetable, Subtractive, FM, Granular, Delay, Echo, Reverb, Compressor, Limiter, EQ, Filter, Auto Filter, Chorus, Phaser, Vocoder, Looper, Tuner.
- **Names original to Ableton or another vendor stay out**: invented or distinctive names and their close variants, in product names, the UI, presets and anything users read.
  - Ableton's: Operator, Drift, Simpler, Roar, Glue, EQ Eight, Hybrid Reverb, Drum Rack, Meld, Collision, Corpus, Beat Repeat.
  - Other vendors': Serum, Pro-Q, Saturn, Valhalla, EchoBoy, RC-20, OTT, and so on.
- **The test, where a word could be either:** is it the trade's name for this kind of device? "Sampler" is; "Operator" isn't (an operator is a part of an FM synth, not the instrument), so an FM instrument named Operator would take Live's name. A generic word joined to a vendor's own (EQ Eight, Glue Compressor) makes a name of its own, which we don't use either.
- **The rest of ADR 0008 holds:**
  - no copied presets, samples, graphics or layouts;
  - identifiers stay separate from display names;
  - the naming pass before release now checks every name against this rule, instead of replacing every working name.

## Consequences
- Subtractive, Sampler and Wavetable can keep their names past the first release, and so can our other generic names (EQ, Delay, Reverb, Compressor). Echo and Auto Filter, listed in ADR 0008, are allowed too.
- The product's identity comes from its own name (Segno, [ADR 0024](0024-product-name.md)) and its design language, not from a brand name for each device.
- The naming pass before release gets smaller: it checks for names original to a vendor, and the product's trademark search (B24) stays as it is.
