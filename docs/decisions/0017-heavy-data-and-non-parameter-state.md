# 0017. Heavy data and non-parameter state

- **Status:** Accepted
- **Date:** 2026-10-10

## Context
The Sampler (M1) is the first device with state that isn't a parameter. Later devices follow: wavetables, multisample zones, impulse responses. Three problems come with that.

1. **Getting large data to the audio thread.** A decoded sample can be hundreds of megabytes. It has to be built off the audio thread and handed over without locks. The old data must then be freed, but never on the audio thread: freeing can take locks inside the allocator and can take a long time.
2. **Readiness.** Hosts restore state and then often render straight away: DAW export, our tests, `fd-render`. If a sample loads asynchronously, the first render can miss it, silently.
3. **Edits from the UI.** Dropping a file on the panel changes the device's `extra` state. The rule "devices never change their own state, edits go through the host" (CLAUDE.md) applies here too. Otherwise the change isn't undoable in the DAW, and the plugin host doesn't know the state changed (so it isn't saved).

## Decision
1. **`fd::AssetHandoff<T>`** (libs/device) moves immutable data from a loader to the audio thread:
   - **Loader side (not real-time):**
     - `publish(shared_ptr<const T>)` hands data over;
     - `latest()` gives the newest data, for state and UI;
     - `collect()` frees data the audio thread has retired.
   - **Audio side:**
     - `takeNew()` returns a reference to newly published data, if any;
     - `retire(ref)` gives a reference back once no voice uses it.
   - Both sides are wait-free. The audio thread only exchanges pointers and pushes retired pointers into a fixed-size ring. Every allocation and deallocation happens on the loader side.
   - A device may hold at most a few references at once (current, plus old data whose voices are still fading out).
2. **Loading is synchronous in `readExtraState()` for now.** It runs on the caller's thread (message or loader), never on the audio thread. When `readExtraState()` returns, the data has been published, so a render that follows always includes it. The audio thread picks it up at its next block.
   - A missing or unreadable file keeps its path in the state and is reported to the UI. The device plays silence for it.
   - Asynchronous loading with readiness reporting to hosts waits until data gets large enough to need it (disk streaming, multisample libraries in M5). That will need its own ADR.
3. **UI edits go through the host:** `ParamBinding::applyExtraState(patch)` merges a JSON patch into the device's `extra` state.
   - **Plugin adapter:** applies the patch, then tells the host the state changed (non-parameter state change), so the host marks the project dirty.
   - **DAW adapter:** writes the merged `extra` into the plugin's `ValueTree` with the Edit's undo manager. A listener applies it to the device, so undo and redo reload the previous sample.
   - **Local binding** (tools, gallery): applies it directly.
4. **Displays read data, never write it.** `DeviceUiContext::device` gives panels read-only access to their device on the message thread, e.g. the loaded sample's waveform via `latest()`. Panels must not write through it; all state changes use the binding.

## Consequences
- Loading a big file blocks the thread that applies the state, for as long as decoding takes (about 10–50 ms for a typical one-shot or loop). That is acceptable for M1 and revisited with streaming.
- Retired data stays in memory until the loader side next calls `collect()`. That happens on every publish and when the device is destroyed: at most one old sample lingers.
- Sample paths are absolute in M1. Project-relative paths and collect-and-save come with the browser (M3).
- The conformance suite can't make a sampler audible without a sample. `DeviceFactory` gains an optional `testSetup` hook that loads a generated test asset, so instrument checks apply to the Sampler too.
