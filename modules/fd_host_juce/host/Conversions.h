#pragma once

namespace fd::host
{
/// Converts one MIDI message to a device event (nullopt for messages devices don't use).
[[nodiscard]] std::optional<Event> toEvent(const juce::MidiMessage& m, int sampleOffset) noexcept;

/// Appends the events of a JUCE MidiBuffer to `out` (real-time safe; drops events when full).
void appendMidi(const juce::MidiBuffer& midi, EventList& out) noexcept;

/// Device state as UTF-8 JSON in a MemoryBlock (plugin get/setStateInformation). A panel's
/// view state (ADR 0022), when not empty, goes under a top-level "view" key that device state
/// readers ignore; without one the bytes are exactly the device state.
[[nodiscard]] juce::MemoryBlock stateToMemory(const Device& device, const std::string& viewState = {});
ApplyResult stateFromMemory(Device& device, const void* data, int sizeInBytes);
/// The view state stored by stateToMemory ("" if none).
[[nodiscard]] std::string viewStateFromMemory(const void* data, int sizeInBytes);
} // namespace fd::host
