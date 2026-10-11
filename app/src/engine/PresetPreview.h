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
} // namespace fd::engine
