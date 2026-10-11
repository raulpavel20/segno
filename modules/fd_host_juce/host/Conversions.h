#pragma once

namespace fd::host
{
/// Converts one MIDI message to a device event (nullopt for messages devices don't use).
[[nodiscard]] std::optional<Event> toEvent(const juce::MidiMessage& m, int sampleOffset) noexcept;

/// Appends the events of a JUCE MidiBuffer to `out` (real-time safe; drops events when full).
void appendMidi(const juce::MidiBuffer& midi, EventList& out) noexcept;

/// What a plugin keeps beside its device's state: the editor's view (ADR 0022) and whether the
/// device listens to the host's sidechain input (ADR 0025). Never part of presets.
struct PluginExtras
{
    std::string view;               // a panel's view state, "" when all default
    bool sidechainExternal = false; // the sidechain picker on External
    std::string preset;             // the PresetMemo (ADR 0044): which preset, A/B; "" when none
};

/// Device state as UTF-8 JSON in a MemoryBlock (plugin get/setStateInformation). The extras go
/// under top-level keys that device state readers ignore ("view", "sidechain", "preset"), each only when
/// not the default; without them the bytes are exactly the device state.
[[nodiscard]] juce::MemoryBlock stateToMemory(const Device& device, const PluginExtras& extras = {});
ApplyResult stateFromMemory(Device& device, const void* data, int sizeInBytes);
/// The extras stored by stateToMemory (defaults where missing or malformed).
[[nodiscard]] PluginExtras extrasFromMemory(const void* data, int sizeInBytes);
} // namespace fd::host
