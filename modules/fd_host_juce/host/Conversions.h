#pragma once

namespace fd::host
{
/// Appends the events of a JUCE MidiBuffer to `out` (real-time safe; drops events when full).
void appendMidi(const juce::MidiBuffer& midi, EventList& out) noexcept;

/// Device state as UTF-8 JSON in a MemoryBlock (plugin get/setStateInformation).
[[nodiscard]] juce::MemoryBlock stateToMemory(const Device& device);
ApplyResult stateFromMemory(Device& device, const void* data, int sizeInBytes);
} // namespace fd::host
