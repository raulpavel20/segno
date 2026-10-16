#pragma once

#include <fd/device/Device.h>
#include <fd/device/State.h>
#include <juce_audio_basics/juce_audio_basics.h>

namespace fd::engine
{
/// An instrument preset's preview (docs/design/browser.md §7): about two seconds of a phrase in
/// its register, played through a module of its own, offline, never touching the project. Bass
/// holds a C1, Pad a C3 major chord, anything else a C3 (C3 = MIDI 60). Empty if `info` isn't
/// one of our instruments or the state doesn't apply.
[[nodiscard]] juce::AudioBuffer<float> renderPresetPhrase(const DeviceInfo& info, const DeviceState& state,
                                                          const std::vector<std::string>& tags,
                                                          double sampleRate);

/// An instrument rack's preview (racks.md §18.9): the same phrase played through every chain of a
/// rack file, each chain's modules in order, the macros where the file has them, the chains
/// summed at their volume and pan; offline, never touching the project. Empty for an effect rack,
/// or one holding a plugin (plugins aren't rendered offline).
[[nodiscard]] juce::AudioBuffer<float> renderRackPhrase(const nlohmann::json& rack, double sampleRate);

/// A Drum Kit preset's preview (docs/design/drum-kit.md §9): a bar of a beat through its pads (kick,
/// snare and hats on C1, D1, F♯1 and A♯1; a kit without them its pads in turn), each pad's modules
/// in order at its Plays, its choke groups heard; offline, never touching the project. Empty for a
/// kit holding a plugin.
[[nodiscard]] juce::AudioBuffer<float> renderKitBeat(const DeviceState& kit, double sampleRate);
} // namespace fd::engine
