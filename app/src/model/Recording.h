#pragma once

#include <filesystem>
#include <optional>
#include <string>

namespace fd::model
{
// Recording (M2 step C5, docs/design/recording.md): a track's inputs and arm state, and the
// rules that follow from them.

enum class Monitor { Auto, In, Off };
enum class RecordKind { Midi, Audio };
enum class CountIn { Off, OneBeat, TwoBeats, OneBar, TwoBars };

/// A track's inputs (§3) and whether it's armed (§4).
struct InputInfo
{
    std::string audio;        // an audio input by name ("Input 1 + 2"); empty: none
    std::string midi = "all"; // "all", "keyboard", a MIDI device by name; empty: none
    int channel = 0;          // 0: every channel; 1-16: only that one
    Monitor monitor = Monitor::Auto;
    std::optional<RecordKind> records; // chosen by hand; otherwise it follows the inputs
    bool armed = false;     // by its arm button
    bool softArmed = false; // by the selection, for live MIDI (only while no track is armed by its button)

    [[nodiscard]] bool isArmed() const noexcept { return armed || softArmed; }

    friend bool operator==(const InputInfo&, const InputInfo&) = default;
};

/// What a track records: its choice; else MIDI with an instrument and a MIDI input; else audio
/// with an audio input; else MIDI.
[[nodiscard]] RecordKind recordKind(const InputInfo& input, bool hasInstrument) noexcept;

/// Whether a track hears its input now (§4): always with In, while armed with Auto, never with Off.
[[nodiscard]] bool hearsInput(const InputInfo& input) noexcept;

/// What a track listens to, short ("Input 1", "All MIDI", "Keyboard · Ch 2"): the input it records.
[[nodiscard]] std::string inputSummary(const InputInfo& input, bool hasInstrument);

/// The project file for a Save As choice (ADR 0030): `Name/Name.segno` beside the chosen file,
/// unless the chosen file is already in a folder of its name. The extension becomes `.segno`.
[[nodiscard]] std::filesystem::path projectFileFor(const std::filesystem::path& chosen);
} // namespace fd::model
