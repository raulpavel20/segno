# 0071. Profiling: perf and hotspot, Tracy behind FD_PROFILE, callgrind and heaptrack

- **Status:** Accepted (product owner, 2026-10-16: "I approve the tools", with [profiling.md](../design/profiling.md), PR1–PR8)
- **Date:** 2026-10-16
- **Builds on:** [0006](0006-juce-free-core-and-module-boundaries.md) (no JUCE below the JUCE line)
- **Number:** 0065 and 0069 are taken on the `synth` branch; renumbered at merge if main has used 0071

## Context
- The product owner wants optimisation guided by measurement, not by reading code and guessing (2026-10-16).
- Today we have timed `[.benchmark]` tests and real-time safety checks, but no profiler:
  - Ubuntu's `perf_event_paranoid` of 4 blocks every perf event, and so every sampling profiler on Linux;
  - nothing in the code marks sections for a timeline.
- An audio app has two questions. Where does the time go on average? A sampling profiler answers it. Why did one block miss its deadline? Only an instrumented timeline shows that.

## Decision
- **Sampling:** perf, read with hotspot (perf's GUI) or `perf report` as text. perf events are allowed while profiling, by lowering `perf_event_paranoid` to 2, upstream Linux's default, until the next reboot.
- **Instrumenting:** Tracy (BSD-3-Clause), pinned in `cmake/Dependencies.cmake` and fetched only when the `FD_PROFILE` option is on.
  - Code marks itself through our own JUCE-free macros (`FD_ZONE`, `FD_FRAME`, `FD_PLOT` in `fd/profile/Profile.h`). They compile to nothing unless `FD_PROFILE` is on, so they're allowed anywhere, below the JUCE line too.
  - Tracy's GUI is built from the same pinned source, so it always matches the client.
- **A `profile` build preset:** optimised, with debug symbols and frame pointers, in `build/profile`. It's built only when profiling.
- **Exact before-and-after counts:** Valgrind's callgrind, read with KCachegrind or `callgrind_annotate`.
- **Memory:** heaptrack.
- **Later, for inner loops:** AMD uProf.

## Alternatives considered
- **Perfetto through `melatonin_perfetto`:** popular in JUCE plugins. But its trace is written only when the app quits cleanly, and it has no sampling, lock or memory views. Tracy covers more, live.
- **gperftools:** it samples without perf permissions, but it shows less than perf, and it isn't needed once perf events are allowed.
- **Timed tests only:** they say how long something takes, not where inside it the time goes.
- **Tracy's macros called directly in our code:** it ties every file to Tracy. Our own macros keep the choice in one header.

## Consequences
- Normal builds don't change: no Tracy, no zones, no new dependency fetched.
- **Profiling needs:**
  - the `profile` preset's own build;
  - perf events allowed while profiling (the product owner runs `sudo sysctl kernel.perf_event_paranoid=2`);
  - the tools installed (profiling.md §5).
- **Assistants can read profiles as text:** `perf report --stdio`, `tracy-csvexport`, `callgrind_annotate` and `heaptrack_print`.
- A thread's first Tracy zone allocates. That's only in profiling builds. The conformance suite's allocation check and RTSan don't use them.
- **Showing CPU per module in the DAW** (tracktion already measures it) is a product feature with its own design, outside this decision.
