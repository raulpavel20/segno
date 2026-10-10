#pragma once

#include "model/Recording.h"
#include "model/Snapshot.h"
#include "model/TempoMap.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace fd::model
{
// Capture MIDI (M2 step C5b revised, docs/design/recording.md §8): what was just played, as a clip.
// Pure; the Session feeds it what every MIDI input and the computer keyboard played.

/// A MIDI message as played: when (the input clock, seconds), where in the song if the transport
/// was playing, and from which input.
struct MidiEvent
{
    double time = 0.0;
    std::optional<double> song;
    unsigned char bytes[3]{};
    int size = 0;
    std::uint16_t source = 0; // 0: the computer keyboard; else a MIDI input (MidiTap::sourceName)

    [[nodiscard]] int status() const noexcept { return size > 0 ? bytes[0] & 0xf0 : 0; }
    /// 1-16 for channel messages, 0 otherwise.
    [[nodiscard]] int channel() const noexcept { return size > 0 && bytes[0] < 0xf0 ? (bytes[0] & 0x0f) + 1 : 0; }
    [[nodiscard]] bool isNoteOn() const noexcept { return status() == 0x90 && size >= 3 && bytes[2] > 0; }
    [[nodiscard]] bool isNoteOff() const noexcept
    {
        return size >= 3 && (status() == 0x80 || (status() == 0x90 && bytes[2] == 0));
    }
    [[nodiscard]] bool isSustain() const noexcept { return status() == 0xb0 && size >= 3 && bytes[1] == 64; }
};

/// Whether a track's MIDI input takes an event from `source` ("keyboard", or a MIDI input's name)
/// on `channel` (0: not a channel message).
[[nodiscard]] bool inputTakes(const InputInfo& input, const std::string& source, int channel);

/// The last phrase: the events since the last pause of at least `pause` seconds with no note or
/// sustain pedal held, from `notBefore` on (a capture or a pass uses up what came before; while
/// playing, so does starting). Controllers up to `lead` seconds before its first note come along.
/// Empty if no note was played.
[[nodiscard]] std::vector<MidiEvent> lastPhrase(const std::vector<MidiEvent>& events, double pause, double notBefore,
                                                double lead);

/// Where a capture goes: the armed MIDI tracks, each with what its input takes; with none armed,
/// the selected track, whatever it holds (no instrument is added, ADR 0033); else a new track.
struct CaptureDestinations
{
    std::vector<std::uint64_t> armed;
    std::optional<std::uint64_t> track;
    bool newTrack = false;
};
[[nodiscard]] CaptureDestinations captureDestinations(const Snapshot& snapshot, std::optional<std::uint64_t> selected);

/// Where a phrase lands: played while the transport played, where it was played (its first
/// note at its song time, the rest after it as played, so a loop's wrap doesn't fold it);
/// otherwise its first note on the playhead's beat. The clip runs from the beat before its first
/// event to the beat after its last (notes still held end at `now`), at least a beat long.
struct CapturedClip
{
    double start = 0.0, end = 0.0; // seconds
    std::vector<double> times;     // each event's place, seconds
};
[[nodiscard]] CapturedClip placeCapture(const std::vector<MidiEvent>& phrase, const TempoMap& tempo, double playhead, double now);
} // namespace fd::model
