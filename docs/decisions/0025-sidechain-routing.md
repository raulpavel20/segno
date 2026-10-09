# 0025. Sidechain routing: host routing, chosen in the module's header

- **Status:** Accepted (product owner, 2026-10-09)
- **Date:** 2026-10-09
- **Builds on:** [0006](0006-juce-free-core-and-module-boundaries.md) (one device API, a plugin adapter and a DAW adapter), [0021](0021-mcp-server.md) (every DAW feature has MCP tools), [0022](0022-module-panels.md) (the module header)

## Context
The Compressor and Gate can listen to another signal than the one they process: a kick ducking a bass, a hi-hat opening a pad (M2 step A2). The device API already has a sidechain bus (`BusLayout::sidechainChannels`, `ProcessContext::sidechain`), and both devices fall back to their input when it is empty. Their SPECs say the source is host routing, not a parameter.

Three questions remained:
- **Plugins:** hosts route a plugin's sidechain with their own UI. VST3 hosts activate an auxiliary input when it is used, but CLAP has no inactive ports: a CLAP host always hands the plugin a sidechain buffer, silent when nothing is routed. A compressor that always listened to that bus would stop compressing in such a host until the user routed something.
- **The DAW:** which track feeds the sidechain, where in that track's signal it is taken, and how the choice is stored and undone.
- **The UI:** where the choice is made, in a panel that runs in both hosts.

## Decision
- **The source is host routing.** It is never a device parameter: not automated, not in presets, not in the device's state.
- **The module's header shows a "Sidechain" picker** for every device with a sidechain bus. The host supplies the choices (`ui::SidechainRouting` in the panel context); the first choice is always "no sidechain", where the device listens to its input.
- **The DAW: tracks.** The picker lists the other tracks.
  - It leaves out any track whose sound already depends on this one through sidechains, so routing can't loop.
  - The source track is heard after its modules and fader, but before its mute: a muted "ghost" track can still drive a sidechain.
  - The choice uses tracktion's sidechain routing, is saved with the project, and is one undo step ("Set sidechain").
  - Removing the source track clears the routing in the same undo step.
- **Plugins: Off or External.** The plugin has a stereo auxiliary input named "Sidechain", inactive by default.
  - The picker offers Off (the default) and External. With External, the device listens to that input whenever the host provides it; with Off it never does, whatever the host sends.
  - The choice is stored in the plugin's state beside the device state (like the view state of ADR 0022). It is written only when External is chosen, so states without it stay byte-identical.
- **Assistants:** `get_module` reports a module's sidechain source, and `set_module_sidechain` sets it (a track, or none). It is one named undo step, like the picker.
- **Conformance:** with no sidechain connected, a device behaves exactly as without one, in both adapters.

## Consequences
- The Compressor and Gate (and later the EQ's dynamic bands, the Filter's envelope follower and Multiband Dynamics) get sidechains without a parameter each, and their presets stay portable.
- In a plugin host, the user turns the sidechain on in two places: the host's routing and the module's picker. That is the price of behaving the same in every format, and it matches what other sidechain-capable plugins ask for.
- Feedback through sidechains is impossible by construction, rather than detected while audio runs.
- Dragging a track onto a module's sidechain (innovation backlog) can later set the same routing.
