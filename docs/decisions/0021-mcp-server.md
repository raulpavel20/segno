# 0021. A built-in MCP server: assistants can drive the whole DAW and every device in it

- **Status:** Accepted (product owner, 2026-10-09)
- **Date:** 2026-10-09
- **Builds on:** [0006](0006-juce-free-core-and-module-boundaries.md) (module boundaries), [0017](0017-heavy-data-and-non-parameter-state.md) (non-parameter state through the host binding), [0018](0018-daw-app-structure.md) (`Session`, snapshots, `ActionRegistry`)

## Context
The Model Context Protocol (MCP) is the open standard AI assistants use to work with other software. An application exposes:
- **tools:** actions;
- **resources:** readable data;
- **prompts:** templates for common jobs.

Any assistant that speaks MCP can then use them: Claude, others, or local models. The current revision of the spec is 2026-07-28. Clients still on 2025-11-25 fall back to that version.

**Existing DAW integrations work from the outside.** They go through scripting or control-surface APIs and see only part of the program; one Bitwig bridge can't reach arrangement clips at all.

**Our architecture can do better at low cost:**
- `engine::Session` runs every edit as one undoable command and has no tracktion types in its interface;
- snapshots are plain, immutable data that serialise directly;
- every device parameter has a permanent ID, a range, a unit and display text (`ParamSpec`);
- every user action is in the `ActionRegistry`;
- note editing is pure, tested functions.

**Options considered:**
1. **No MCP; a scripting API** (OSC or a scripting language), leaving MCP to third parties. Indirect and partial, and every integration would rebuild the same mapping.
2. **A bridge process** built on an official MCP SDK (TypeScript, Python, Go) that talks to the app over a local socket. The SDK tracks the spec, but the product would have to ship a second runtime or toolchain and a second process.
3. **A server inside the app, written in C++.** No official C++ SDK exists, and the community ones lag the spec. But the protocol core is small: JSON-RPC 2.0 and a handful of methods.
4. **MCP inside the device plugins too.** Plugins run inside other DAWs, often many instances at once. The host owns the project, the undo history and the parameters. A network listener inside a plugin would surprise both users and hosts.

## Decision
1. **FossDAW includes a fully featured MCP server.** An assistant can do everything a user can do in the DAW and read everything the user can see:
   - **tools** for every editing, transport, device and project command. They use musical units (bars, beats, note names), and device values in their own units (dB, Hz, ms, %);
   - **resources** for the project and its parts: tracks, clips, notes, device chains, device states;
   - **change notifications**, so an assistant sees the user's edits;
   - **render and analyse:** render a range of the project or of a track offline and return measurements (levels, peaks, loudness, spectral balance). Assistants that accept audio also get the audio itself;
   - **prompts** for common jobs where they help. They are starting points, not features of their own;
   - **long jobs** (renders, plugin scans) report progress and can be cancelled.
2. **The whole device suite is covered, inside our DAW.** An assistant can drive every native instrument and effect in a chain:
   - every parameter, by its permanent ID;
   - presets: load and save;
   - non-parameter state, such as the Sampler's sample, through the same binding the panel uses ([ADR 0017](0017-heavy-data-and-non-parameter-state.md));
   - the device's analysis read-outs (meters, gain reduction, spectra), where it publishes them.

   Device coverage is generated from `ParamSpec` and the device registry. A new device is controllable the day it lands, with no MCP code of its own.

   Each device's reference (what it does, and what each parameter does) ships with the DAW for assistants to read.

   Third-party plugins in a chain are reachable through their generic parameters (name and normalised value), on a best-effort basis.
3. **Only in the DAW.**
   - The plugin builds (VST3, CLAP, LV2, AU, standalone) contain no MCP code and open no connections. When our devices run inside another DAW, that host is in charge.
   - The devices themselves never know about MCP. The server lives in `app/` and reaches devices only through the DAW's host binding.
   - A CTest boundary check enforces this: nothing in `libs/`, `devices/`, `modules/`, `plugins/` or `tools/` includes the MCP code.
4. **An assistant's edits go through the same paths as the user's.** Tools call `engine::Session` and the device host binding, never around them:
   - one tool call is one undo step, named after the tool (for example "Assistant: add notes"), so Ctrl+Z takes back what an assistant did;
   - "devices never write their own parameters" and real-time safety hold unchanged, because the server runs commands on the message thread, like the UI;
   - inputs are checked against the tool's schema and each parameter's range; ids come from snapshots.
5. **A server, not an assistant.** The DAW ships no language model, no chat panel, no account and no telemetry; users connect the assistant they choose. A built-in assistant (the DAW as an MCP client) would need its own ADR.
6. **Opt-in and local.**
   - Off by default; turned on in settings.
   - Listens only on the local machine (127.0.0.1), checks the `Origin` header, and needs a per-install token.
   - The transport bar shows when an assistant is connected, and its edits appear as they happen.
   - A read-only mode allows reading and analysis, but no edits.
   - Destructive steps (deleting tracks, overwriting files) ask for confirmation through the assistant, using the protocol's input-request pattern.
7. **Implementation.**
   - **The protocol layer is ours, in C++,** on `nlohmann_json`. JSON-RPC handling and the tool and resource registry are JUCE-free and unit-tested, like `app/src/model`.
   - **Spec versions:** we support the current revision and the previous one (2026-07-28 and 2025-11-25). We leave out the features the spec has deprecated: roots, sampling and logging.
   - **Two transports.** The running app serves Streamable HTTP on localhost. Clients that launch a process use a stdio bridge mode of the same binary instead. The bridge forwards to the running app, or, with `--headless`, opens a project without a window.
   - **Where the code lives:** `app/src/mcp/`. Like the UI, it uses `engine::Session` and `DeviceHandle`.
8. **Coverage rule.** A DAW feature is done when it ships with its MCP tools and a test that drives it through MCP. This is the same kind of rule as "every action has a shortcut and appears in the command palette".

   Tool names and their arguments are a public interface. Once released, they change only after a deprecation period.

## Consequences
- **Easier:**
  - assistants take on tedious and structural work: starting patterns, batch edits, setting up and checking a project, explaining devices;
  - natural-language control helps accessibility;
  - development gets a live way to inspect and drive the running app, beyond rendered screenshots.
- **Harder, or must be maintained:**
  - the protocol layer must track the spec, which changed a lot in 2026;
  - the tool surface grows with every feature (the coverage rule);
  - the local endpoint is a security surface: kept off by default, local only, and token-protected.
- **Scheduled:** M2 step C9: the server, tools for everything that exists by then, settings and the connection indicator. From then on, each feature adds its own tools.
- **Docs first:** the tool and resource catalogue goes in `docs/design/mcp.md` before the code.
