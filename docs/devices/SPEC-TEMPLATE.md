# <Working name>: device spec

> Copy this file to `devices/<name>/SPEC.md` before writing any code for the device. Parameter IDs are **frozen** once the spec is approved; changing them later requires a state migration.

- **Status:** Draft | Approved | In progress | Done
- **Type:** Instrument | Audio effect | MIDI effect
- **Tier / milestone:**
- **Live target:** the Live counterpart(s) this device covers and matches in features and sound. References only; nothing is copied ([ADR 0011](../../docs/decisions/0011-calibrated-ambition-and-consolidation.md)).

## Purpose

What problem this device solves, in 2–3 sentences, and what makes ours worth using.

## Beyond Live (only if this device goes further)

By default a device matches its Live target. If it goes further, list each addition, the external reference (if any), and why the benefit is worth the cost. Larger ideas belong in the [innovation backlog](../../docs/innovation-backlog.md).

## Stages

| Stage | Scope | Milestone |
|---|---|---|
| Core | The polished first release. It must be complete and usable on its own. | |
| Stage 2 | | |

## Parameters

| ID (permanent) | Name | Range | Default | Unit | Curve / skew | Smoothing | Automatable |
|---|---|---|---|---|---|---|---|
| `example.gain` | Gain | −inf…+35 | 0 | dB | dB-linear | 20 ms multiplicative | Yes |

Also describe any non-parameter state (sample references, wavetables, and so on).

## DSP design

Signal flow (ASCII), algorithms, oversampling and anti-aliasing approach, latency, tail, and the libraries reused.

## UI layout

The section map on the module grid ([device-panels.md](../design/device-panels.md)): each section's ID, title, width in columns and the controls in it, with their cells. Header settings, the meter strip, and the expanded view if any. Which visualiser is shown, and what it displays.

## Factory presets

The planned list, with tags.

## Automated test targets

| Metric | Target |
|---|---|
| e.g. magnitude response error vs analog prototype | ≤ ±0.1 dB to 0.9 × Nyquist @ 48 kHz |
| e.g. aliasing ratio, 1 kHz → 18 kHz sweep at 4× | < −80 dB |

## A/B cases (against the Live target)

| Case | Stimulus | Our settings | Benchmark settings | What to listen for |
|---|---|---|---|---|

## A/B log

Dated listening notes from the product owner, and the measurable checks derived from them.

## Sign-off

- [ ] Automated: unit, conformance and golden tests; validators
- [ ] Layout lint passes (`fd_panel_tests`)
- [ ] Snapshots reviewed (100% / 200%; expanded, collapsed, folded)
- [ ] Sound signed off by the product owner (date):
- [ ] Feel signed off by the product owner (date):
