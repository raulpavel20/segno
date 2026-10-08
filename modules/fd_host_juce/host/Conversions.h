#pragma once

namespace fd::host
{
/// Converts one MIDI message to a device event (nullopt for messages devices don't use).
[[nodiscard]] std::optional<Event> toEvent(const juce::MidiMessage& m, int sampleOffset) noexcept;

/// Appends the events of a JUCE MidiBuffer to `out` (real-time safe; drops events when full).
void appendMidi(const juce::MidiBuffer& midi, EventList& out) noexcept;

/// Device state as UTF-8 JSON in a MemoryBlock (plugin get/setStateInformation).
[[nodiscard]] juce::MemoryBlock stateToMemory(const Device& device);
ApplyResult stateFromMemory(Device& device, const void* data, int sizeInBytes);
} // namespace fd::host
