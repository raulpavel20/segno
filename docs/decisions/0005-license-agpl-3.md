# 0005. License: AGPL-3.0

- **Status:** Accepted
- **Date:** 2026-10-08

## Context
The project must be free and open-source software. Its dependencies constrain the choice:
- JUCE's open-source license is AGPLv3.
- tracktion_engine is GPLv3 or later.
- The sst-* libraries are GPLv3.
- Rubber Band is GPLv2 or later.
- Everything else is permissive (MIT, BSD, ISC, BSL).

GPLv3 code can be combined with AGPLv3 code (GPLv3 §13). A permissive license for our own code would not change the license of the combined binaries.

## Decision
The project is licensed **AGPL-3.0-or-later**. We follow the **REUSE** specification (SPDX headers and a `LICENSES/` folder), and `reuse lint` runs in continuous integration.

## Consequences
- Compatible with every planned dependency. GPLv2-*only* code must never be added.
- Anyone distributing modified versions, including as a network service, must share their source.
- Commercial closed-source forks are impossible unless every relevant copyright holder relicenses, including the commercial JUCE and tracktion_engine licenses. This fits the FOSS goal.
- Third-party content (samples, impulse responses, fonts) needs licenses that are compatible and can be redistributed: CC0, CC-BY or OFL. These are tracked by REUSE.
