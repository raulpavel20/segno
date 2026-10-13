# 0008. Own names and visual identity; nothing taken from Ableton

- **Status:** Accepted; amended by [0052](0052-generic-names-are-free.md) (2026-10-14): generic terms of the trade, such as Sampler and Wavetable, are fine as final names; only names a vendor made up stay out
- **Date:** 2026-10-08

## Context
Ableton Live Suite is the benchmark for breadth and quality. Copying its device names, presets, graphics or layouts would create trademark and trade-dress risk, and would make the project look like a clone rather than a product in its own right. The same applies to other vendors' names, for example "OTT", which belongs to Xfer.

Plugin identifiers (plugin codes, CLAP IDs, bundle IDs, parameter IDs) are stored in users' sessions and presets. They cannot change once released.

## Decision
- **No Ableton names or close variants** for the product or its devices, including Operator, Wavetable, Drift, Simpler, Sampler, Drum Rack, Roar, Glue, EQ Eight, Echo, Hybrid Reverb and Auto Filter as brand names. The same goes for other vendors' product names used as benchmarks (Pro-Q, Saturn, Serum, Valhalla, EchoBoy, RC-20, OTT, and so on).
- **No copied presets, samples, graphics or layouts.** Our design language is our own ([ADR 0009](0009-dark-first-design-language.md)).
- **Working names are functional** ("EQ", "Sampler", "Delay"). Generic words like these are fine as working names and category labels. Final brand names for the product and devices must be distinctive and are chosen before the first public release.
- **Identifiers are separate from display names.** Permanent functional IDs are listed in `docs/ids.md` (created in M0). The organisation ID and product name are placeholders until the public release, then frozen.
- Ableton reference renders and listening notes are kept out of the repository except as written notes in `SPEC.md`.

## Consequences
- Renaming before 1.0 costs nothing, because IDs are independent of names.
- A naming pass for the product and devices is required before the first public build.
- Describing a device as "comparable to Live's EQ Eight" in internal docs is fine. Marketing and UI must not use Ableton's names.
