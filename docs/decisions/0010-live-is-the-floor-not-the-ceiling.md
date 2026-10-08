# 0010. Live is the floor, not the ceiling

- **Status:** Superseded by [0011](0011-calibrated-ambition-and-consolidation.md)
- **Date:** 2026-10-08

## Context
Ableton Live Suite was chosen as the benchmark for how comprehensive the device suite should be. Copying its device list one-to-one would also copy its limitations. Live ships three separate EQs (EQ Eight, EQ Three, Channel EQ), and none of them is as capable as a best-in-class EQ such as FabFilter Pro-Q: unlimited bands, dynamic bands, linear phase, EQ matching. The product owner does not want to imitate Ableton in either UI or functionality. They want to innovate and push further wherever possible.

## Decision
1. **Live sets the minimum for coverage.** Every role a Live Suite device covers must be covered by one of our devices, or be an explicit non-goal.
2. **Best-in-class sets the aim for quality and features.** Each device is benchmarked against both its Live counterpart and the best tool in its category, from any vendor. Examples: the EQ against FabFilter Pro-Q, the wavetable synth against Serum and Vital, the reverb against Valhalla.
3. **Consolidate when one deep device does the job better than several shallow ones.** One EQ, one compressor with character modes, one delay that covers all styles, one reverb covering algorithmic and convolution, one sampler that grows from simple to multisample. Depth stays behind progressive disclosure: a compact default view keeps quick tasks quick.
4. **Every device spec must say how it goes beyond its Live counterpart.**
5. **The same thinking applies to the DAW.** Where integration makes something possible that standalone plugins can't do, we use it: cross-track awareness, drag-to-modulate, a project-wide key and scale, an arranger track. Ideas are collected in [innovation-backlog.md](../innovation-backlog.md).
6. **Innovation must not break the UI principles.** No added menu diving, no clutter in the default view, and performance budgets still apply.

## Consequences
- Fewer devices than Live, each deeper. Comprehensiveness is measured by **role coverage**, tracked in the device suite's "Covers" columns.
- Flagship devices are large, so they **ship in stages**: a polished core first, with further stages in later milestones. Each stage must be complete and usable by itself.
- The risk of scope creep goes up. Each `SPEC.md` defines the core and later stages explicitly, and a milestone may only include the stages listed in the roadmap.
- A/B listening is mostly done against Live, which the product owner owns. Best-in-class tools serve as feature and quality references, and as A/B targets where the product owner has access to them.
- Other vendors' product names (Pro-Q, Serum, Valhalla, OTT, and so on) appear only as internal references, never in product names or the UI ([ADR 0008](0008-naming-and-trade-dress.md)).
