# Profiling: finding where the time goes

How we find what to optimise by measuring it, rather than by reading code and guessing. Tools for the CPU on the audio thread and on the UI, for memory, and for checking that a change made things faster.

**Status:** approved by the product owner (2026-10-16): "I approve the tools" (PR1–PR8), [ADR 0071](../decisions/0071-profiling-tools.md) accepted. Work starts 2026-10-17 with §5's order. Nothing is installed or built yet; perf's permission (PR2) is the product owner's to set when it starts.

What the product owner asked (2026-10-16): "research into profiler tools for such apps … tools to help profile exactly what the weak points are and what needs optimising and where, instead of just blindly reviewing code to find bits to optimise. There must be some industry standards for C++ app profilers."

## 1. What's there now

- **Timed tests:** hidden `[.benchmark]` Catch2 tests, built with the `rel` preset, time pieces with `std::chrono` (the EQ, the Wavetable Synth, the Sampler, the arrangement, the Drum Kit). They answer "how long does this take?" but not "where inside it does the time go?".
- **Real-time safety:** the conformance suite checks `process()` for allocations, and the `rtsan` preset (RealtimeSanitizer) catches locks, allocations and system calls on the audio thread. That's correctness, not speed.
- **The engine already times itself, and we don't use it:**
  - tracktion times every plugin's processing (`Plugin::getCpuUsage()`, on by default: `TRACKTION_ENABLE_PLUGIN_CPU_MEASUREMENT`);
  - it times every audio callback, with its mean, minimum and maximum (`DeviceManager::getCPUStatistics()`).
  - Segno shows only the total, as the top bar's CPU load (`Session::cpuUsage()`).
- **`perf` is installed but locked.** Ubuntu sets `kernel.perf_event_paranoid` to 4, a level of its own that blocks every performance event for normal users (upstream Linux stops at 2). Every sampling profiler on Linux is built on those events, so none of them runs here today.
- **Not installed:** Valgrind, KCachegrind, heaptrack, hotspot, Tracy, AMD uProf.

## 2. Two questions, two kinds of tool

An audio app has two different performance questions:

1. **Where does the CPU time go on average?** Which functions are hot in a render, a benchmark, or a minute of playback. A **sampling profiler** answers it. It interrupts the program thousands of times a second, records the call stack, and adds the stacks up into a flame graph. It needs no changes to the code, and it sees everything, ours and tracktion's and JUCE's.
2. **Why was that one audio block late?** A dropout happens when one block out of thousands misses its deadline. An average hides it. An **instrumenting profiler** answers this: it records the start and end of marked sections (the callback, each module, each pad) on a timeline, so the slow block can be found and opened up. Lock waits and what other threads were doing at the time show beside it.

Games have the same split (frame time against average cost), which is why the tools below are what the games industry uses.

## 3. The tools, by what they're for

### 3.1 Sampling: perf, read with hotspot

- **perf** is the Linux kernel's profiler, the standard for C++ on Linux. It samples every thread with call stacks, at almost no cost to the program.
- **hotspot** (KDAB, GPL-2.0+) is perf's GUI:
  - a flame graph, top-down, bottom-up and caller/callee views;
  - a timeline to pick out one thread (the audio thread) or a time range;
  - cost per instruction, with the source beside the assembly;
  - off-CPU time: where threads wait on locks or I/O.
  - It isn't packaged for Ubuntu 26.04; KDAB's AppImage runs without installing.
- **samply** (MIT or Apache-2.0) is the alternative: one command records, and the result opens in the Firefox Profiler in a browser. On Linux it sees only on-CPU time.
- **Both need perf events,** so `perf_event_paranoid` has to come down to 2 (upstream's default: your own programs, not the kernel) while profiling (PR2).
- **Text for assistants too:** `perf report --stdio` prints the same profile as text, so Claude can read a profile in the terminal and say where the time goes.

### 3.2 Instrumenting: Tracy

- **Tracy** (BSD-3-Clause, v0.14.1) is the games industry's standard frame profiler. It's free and works on Linux, Windows and macOS.
- **The program marks sections:** a `ZoneScoped` at the top of a function, a frame mark at the end of each audio block. Tracy shows them live on a timeline, nanosecond-accurate.
- **What it shows:**
  - every zone on every thread: the audio callback, each module's process, each pad, the UI's paint;
  - statistics per zone: mean, median, and the worst case;
  - plots of any value over time (a block's time against its budget);
  - lock contention, memory allocations, and messages.
  - With perf events allowed, it also samples call stacks and context switches, so it covers §3.1's job too.
- **Cost:** about 2.25 ns per zone, by its manual's benchmark. With `TRACY_ON_DEMAND`, nothing is recorded until the profiler connects.
- **Compiled out completely** unless the build asks for it, so normal builds don't change.
- **For assistants:** `tracy-capture` records without the GUI, and `tracy-csvexport` turns a capture into per-zone statistics as CSV.
- **What to watch:** a thread's first zone sets up Tracy's queue for that thread, which allocates. The conformance suite's allocation check, and RTSan, run without Tracy, so they're unaffected. The GUI (the "server") must be the same version as the client built into the app, so we build it from the same pinned source.
- **Considered instead:** Perfetto, through the `melatonin_perfetto` JUCE module, which the JUCE community uses. Its trace is written only when the app quits cleanly, then read in a browser. Tracy is live, records locks and memory, and also samples, so it covers more.

### 3.3 Exact counts: Valgrind's callgrind, read with KCachegrind

- **callgrind** runs the program on a simulated CPU and counts every instruction, per function and per source line. It also simulates the caches.
- **Exact and repeatable:** the same run gives the same counts every time. That suits "did my change make this faster?" on a laptop, where turbo, heat and battery move timings by 10 % or more between runs.
- **Slow:** 20 to 50 times slower than normal, so it's for offline renders and benchmark tests, not live playback.
- **No permissions needed.** It's in Ubuntu: `valgrind` (3.26) and `kcachegrind` (its GUI). `callgrind_annotate` prints the same as text.

### 3.4 Memory: heaptrack

- **heaptrack** (KDE) records every allocation with its call stack:
  - where memory goes (the Wavetable's 12.5 MB an instance, B75; undo's copies of a module's state, B79);
  - allocation hot spots, and temporary allocations made and freed at once (churn on the UI thread);
  - leaks.
- **No permissions needed.** It's in Ubuntu (`heaptrack`, `heaptrack-gui`, 1.5.0), and `heaptrack_print` prints text.

### 3.5 Later, for one function's inner loop: AMD uProf

- **AMD uProf** (free, v5.3, May 2026) reads the Ryzen's own counters: cache misses, branch mispredictions, and what stalls the pipeline (IBS), for Zen 5.
- It's for tuning one hot loop with SIMD, after the tools above have found that loop. Not needed to start.

### 3.6 Not chosen

- **gperftools' CPU profiler** samples with a timer signal and needs no permissions. It's only worth having if perf stays locked (PR2).
- **Intel VTune:** its hardware sampling needs an Intel CPU.
- **For the Windows and macOS ports** (M7): Superluminal and the Visual Studio profiler on Windows, Instruments on macOS. Tracy's zones work on all three, which is one more reason to put them in the code.

## 4. What we'd add to the repo

### 4.1 A `profile` build preset

- An optimised build with debug symbols and frame pointers: `-O2 -g -fno-omit-frame-pointer -mno-omit-leaf-frame-pointer`.
- Frame pointers let perf, hotspot and Tracy follow call stacks cheaply and reliably; a Release build's stacks break off.
- Its own folder, `build/profile`, built only when profiling, never after ordinary work. The first build takes as long as a `rel` build.

### 4.2 Zones, behind `FD_PROFILE`

- **A small JUCE-free header** of our own (`fd/profile/Profile.h`, a header-only `fd_profile` target) with:
  - `FD_ZONE("name")`;
  - `FD_ZONE_TEXT(text)`, naming the module or pad;
  - `FD_FRAME("audio")`;
  - `FD_PLOT("name", value)`.
- They become Tracy's macros when the `FD_PROFILE` option is on (only in the `profile` preset), and nothing otherwise. Code anywhere, below the JUCE line too, can mark itself without depending on Tracy.
- **Tracy is pinned** in `cmake/Dependencies.cmake` (v0.14.1, CPM) and fetched only with `FD_PROFILE`. Its GUI is a target of the same build (`tracy-profiler`), so the versions always match.
- **Zones to start with:**
  - the audio callback, as Tracy's frame;
  - each module's process (`DevicePlugin`), named by its module;
  - a rack's chains and a kit's pads;
  - the Sampler's and the synths' voice rendering;
  - the snapshot the UI reads, and the heavy paints (the arrangement, the piano roll, the waveforms, the spectrogram);
  - loading a project, a preset, the Library.
- **One plot:** each block's time as a share of its deadline. The worst blocks stand out.

### 4.3 How to profile, written down

A section in `docs/dev-environment.md`: what to install, and which tool answers which question.

| The question | The tool |
|---|---|
| A render, a benchmark or playback is slow: where does the time go? | perf + hotspot on the `profile` build (perf events allowed) |
| A dropout, a spike, a stutter: which block, and what was it doing? | Tracy on the `profile` build |
| Did my change make it faster, for certain? | callgrind's instruction counts, before and after; then the `[.benchmark]` test |
| Where does memory go, and what allocates? | heaptrack |
| Does it stay fast? | the `[.benchmark]` tests against budgets (B74) |

Benchmarks on a laptop also need the power profile set to Performance and the charger plugged in. Compare medians of several runs, never one run.

### 4.4 Later: CPU in the DAW itself

tracktion already times every module and every block. Showing it would help anyone using Segno, not only us. Bitwig shows each device's DSP load, Live has a CPU meter per track, and REAPER has its performance meter. It's a product feature with its own design, menu item and MCP tool (`get_performance`), so an assistant could find the heavy module too. It isn't part of this doc (PR8).

## 5. Order of work

1. **Install** valgrind, kcachegrind, heaptrack and heaptrack-gui from Ubuntu, and hotspot's AppImage. Allow perf events while profiling (PR2). No code changes.
2. **The `profile` preset** (§4.1). A first look at B95, the Drum Kit's idle pads, with perf and hotspot, to show the workflow.
3. **`FD_PROFILE` with Tracy** (§4.2), its zones and the deadline plot.
4. **The guide** (§4.3).

Steps 1 and 2 are small. Step 3 is about a day, the zones included.

## 6. Questions for review

Approved by the product owner (2026-10-16) with every recommendation.

| # | Question | Recommendation |
|---|---|---|
| PR1 | **The set of tools** | perf + hotspot for where the time goes; Tracy for late blocks and the timeline; callgrind + KCachegrind for exact before-and-after counts; heaptrack for memory. AMD uProf later, for inner loops. Not gperftools, VTune or Perfetto |
| PR2 | **perf's permission** (Ubuntu's level 4 blocks it) | Lower it to 2, upstream Linux's default, only while profiling: `sudo sysctl kernel.perf_event_paranoid=2`, which lasts until the next reboot. Level 2 lets you profile your own programs but not the kernel. Ubuntu chose 4 because perf has had local security holes. A file in `/etc/sysctl.d/` would make it permanent, if you'd rather not type it each time. You'd run it; Claude doesn't run sudo |
| PR3 | **Tracy in the code** | Yes, behind `FD_PROFILE` and our own `FD_ZONE` macros, compiled out of every other build, pinned and fetched only for profiling (ADR 0071) |
| PR4 | **Zones below the JUCE line** (`libs/dsp`, the devices) | Yes, through `fd/profile/Profile.h`, which is JUCE-free and empty unless profiling. The DSP is where the time goes |
| PR5 | **A `profile` preset** | Yes: optimised, debug symbols, frame pointers, in `build/profile`, built only when profiling |
| PR6 | **Tracy's GUI** | Built from the same pinned source by the `profile` build (`tracy-profiler`), so client and GUI always match |
| PR7 | **First target** | B95, the Drum Kit's idle pads: a known cost, measured before, to show the tools find its cause |
| PR8 | **CPU per module and per track in the DAW** | Worth doing, as its own design (a meter on each module and track, `get_performance` for assistants), after the tools are in. Not part of this |

## 7. Sources

- Tracy: [repository](https://github.com/wolfpld/tracy) and its manual (v0.14.1): 2.25 ns a zone, `TRACY_ON_DEMAND`, CMake's `Tracy::TracyClient`, sampling with elevated rights on Linux.
- hotspot: [KDAB/hotspot](https://github.com/KDAB/hotspot).
- samply: [mstange/samply](https://github.com/mstange/samply), which documents the `perf_event_paranoid` levels it needs.
- heaptrack: [KDE/heaptrack](https://github.com/KDE/heaptrack).
- melatonin_perfetto: [sudara/melatonin_perfetto](https://github.com/sudara/melatonin_perfetto), and [its JUCE forum thread](https://forum.juce.com/t/new-module-profile-your-juce-ui-dsp-performance-with-perfetto/54589).
- perf's permission levels: the kernel's [perf security guide](https://kernel.org/doc/html/v7.0/admin-guide/perf-security.html); Ubuntu's level 4 comes from its "Allow further restriction of perf_event_open" kernel patch ([as posted](https://lists.altlinux.org/pipermail/devel-kernel/2022-June/007588.html)).
- AMD uProf: [AMD's page](https://www.amd.com/en/developer/uprof.html); version 5.3, May 2026, per [igor'sLAB](https://www.igorslab.de/amd-uprof-5-3-profiling-werkzeug-bekommt-duckdb-schnellere-reports-und-mehr-zen-analyse/).
- gperftools: [gperftools/gperftools](https://github.com/gperftools/gperftools).
- Ubuntu 26.04's packages, checked on this machine: valgrind 3.26, kcachegrind 25.12, heaptrack 1.5.0, libgoogle-perftools 2.18; hotspot isn't packaged.
- tracktion's own timing: `tracktion_engine.h` (`TRACKTION_ENABLE_PLUGIN_CPU_MEASUREMENT`), `Plugin::getCpuUsage()`, `DeviceManager::getCPUStatistics()`.
