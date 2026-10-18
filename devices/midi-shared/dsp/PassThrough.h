#pragma once

#include <fd/device/Device.h>

namespace fd::devices
{
/// What a MIDI effect drawn before it's built does (midi-effects.md §18, step 0): passes every
/// event on as it came. Step 1 replaces it with the module's own processing.
inline void passThrough(ProcessContext& c) noexcept
{
    if (c.events == nullptr || c.outEvents == nullptr)
        return;
    for (const auto& e : *c.events)
        c.outEvents->push(e);
}

/// The bus of every MIDI effect: notes in and out, no audio (ADR 0074).
inline constexpr BusConfig midiEffectBuses{
    .inputChannels = 0, .outputChannels = 0, .acceptsMidi = true, .producesMidi = true};
} // namespace fd::devices
