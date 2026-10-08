# 0011. Calibrated ambition: match Live by default, go beyond selectively

- **Status:** Accepted
- **Date:** 2026-10-08
- **Supersedes:** [0010](0010-live-is-the-floor-not-the-ceiling.md)

## Context
[ADR 0010](0010-live-is-the-floor-not-the-ceiling.md) aimed every device at the best tool in its category and merged aggressively. On review, the product owner judged that it went too far in two ways:
- **It aimed too high.** Targeting Serum and Vital for the wavetable synth, an 8-operator any-to-any FM synth, or a 30-model multiband distortion is not realistic. Instruments should stay close to Live's stock suite.
- **It merged too much.** Folding Granulator into the Sampler, or drum synths into a pad sampler, combines tools whose jobs are clearly different. Each would be harder to use and harder to build.

What stays valid from 0010: we don't copy Live's limitations, we use the integrated DAW to do things standalone plugins can't, and an EQ in the style of FabFilter Pro-Q is better than three simple EQs.

## Decision
1. **By default, match Live.** Each device's target is its Live Suite counterpart's features and sound quality. Covering every role Live covers remains the minimum.
2. **Go beyond selectively.** A device goes past its Live counterpart only when the benefit is large *and* the cost is moderate and well understood:
   - **The mixing core:** the EQ (unlimited bands, dynamic bands, linear phase, match), and clearer displays and a few character modes for dynamics.
   - **Small, cheap improvements anywhere:** better visual feedback, visualisers you can edit directly, MPE where it fits.
   - **Instruments stay close to Live's stock instruments.**
3. **Merge only when devices do the same job** and differ mainly in depth or flavour (three EQs → one EQ; Compressor + Glue Compressor; Delay + Echo; Reverb + Hybrid Reverb; Simpler + Sampler). **Keep devices separate when the job or the way they're used is clearly different** (Granulator vs Sampler; Roar vs a pedal-style overdrive; Beat Repeat vs Delay; drum synths vs drum sampler).
4. **Bigger ideas go into the [innovation backlog](../innovation-backlog.md), not into a device's scope.** They are scheduled only through an explicit roadmap decision.
5. **DAW-level innovation continues** as in 0010 (arranger track, drag-to-modulate, a project-wide key, and so on), within the [UI principles](../design/ui-principles.md).

## Consequences
- The device list is closer to Live's: more devices, each with a smaller and more predictable scope. Overall scope goes down, mainly in the synths.
- Each `SPEC.md` states its Live target. A "Beyond Live" section is filled in only for devices that go further, with the reason the extra cost is worth it.
- Devices that are merged still ship in stages where they are large (EQ, Reverb, Sampler).
- A/B listening against Live becomes the main quality gate for nearly every device.
