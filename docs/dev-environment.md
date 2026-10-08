# Development and test environment

The **reference machine** is where the product owner tests by ear and by hand, and where UI performance is measured. Until a Windows or macOS tester is available, **all manual testing happens on Linux, on this machine**. CI still builds and runs automated tests on all three platforms ([ADR 0003](decisions/0003-platforms-and-plugin-formats.md)).

## Reference machine (recorded 2026-10-08)

| | |
|---|---|
| Model | Framework Laptop 13 (AMD Ryzen AI 300 series) |
| CPU | AMD Ryzen AI 9 HX 370, 12 cores / 24 threads |
| GPU | Integrated Radeon 890M |
| RAM | 32 GB |
| OS | Ubuntu 26.04.1 LTS, GNOME on Wayland (fractional scaling enabled; X11 apps scale themselves under XWayland) |
| Displays | Internal 2880×1920 (eDP) + external 3840×2160 (4K, DP) |
| Audio | PipeWire 1.6.2 (with `pipewire-alsa` and `rtkit`). Built-in HD-Audio; no external interface yet |

## Audio setup for development

- **Install PipeWire's JACK support:** `sudo apt install pipewire-jack`. JUCE apps and plugins can then use the JACK backend, served by PipeWire, at low latency (e.g. 128 samples at 48 kHz). Run them directly or through `pw-jack <app>`.
- **Real-time priority:** PipeWire gets it from `rtkit`. If dropouts show up under load, raise the limits (`/etc/security/limits.d/` with an `@audio - rtprio 95` rule and membership of the `audio` group).
- **Listen on headphones or studio monitors, not the laptop speakers.** PipeWire runs a speaker-correction filter-chain for this laptop's speakers, which colours the sound. Sound sign-off on laptop speakers is not valid.

## Hosts for testing plugin builds on Linux

- **Our own app** (the DAW) and the **standalone** build of each device.
- **Third-party Linux hosts** to install as needed: Ardour, Reaper (Linux build), Bitwig Studio (Linux), Waveform Free (Linux). At least one of them should host the CLAP, VST3 and LV2 builds at each milestone.

## Comparing against Live

Ableton Live does not run on Linux, so **A/B listening against Live is deferred** until a Windows or macOS machine with Live is available. Until then:
- devices are signed off by ear on their own merits on this machine, with listening notes recorded in each `SPEC.md`;
- the Live target in each `SPEC.md` is described from Live's documentation and the product owner's experience with Live;
- the numeric A/B tools are built anyway, so reference renders can be dropped in later.

## Build toolchain (to install before M0)

```
sudo apt install cmake ninja-build clang lld ccache pkg-config python3-venv \
  libasound2-dev libjack-jackd2-dev ladspa-sdk libfreetype-dev libfontconfig1-dev \
  libx11-dev libxcomposite-dev libxcursor-dev libxext-dev libxinerama-dev \
  libxrandr-dev libxrender-dev libxi-dev libglu1-mesa-dev mesa-common-dev libegl-dev \
  pipewire-jack
curl -LsSf https://astral.sh/uv/install.sh | sh
```
