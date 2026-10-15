#pragma once

#include "engine/MidiMapper.h"
#include "model/Capture.h"

#include <array>
#include <functional>
#include <memory>
#include <string>
#include <tracktion_engine/tracktion_engine.h>
#include <vector>

namespace fd::engine
{
namespace te = tracktion;

/// Everything played into Segno, kept for Capture MIDI (docs/design/recording.md §8): whether or
/// not a track is armed or has an instrument. A listener on each physical MIDI input (tracktion
/// calls it on the MIDI thread for every message) and the computer keyboard each feed a
/// lock-free queue; the message thread drains them into the last two minutes.
class MidiTap
{
public:
    explicit MidiTap(te::Engine& engine);
    ~MidiTap();
    MidiTap(const MidiTap&) = delete;
    MidiTap& operator=(const MidiTap&) = delete;

    /// Follows the physical MIDI inputs (when they change).
    void syncDevices();
    /// What MIDI learn takes (MidiMapper) isn't played: it's left out (set before syncDevices).
    void setMapper(MidiMapper* mapper) noexcept { mapper_ = mapper; }
    /// A computer keyboard message (message thread).
    void pushKeyboard(const juce::MidiMessage& message);
    /// Moves what arrived into the kept events, oldest first (message thread). `songAt` says where
    /// in the song a time on the input clock was played (nullopt: the transport wasn't playing).
    void drain(const std::function<std::optional<double>(double)>& songAt);
    [[nodiscard]] const std::vector<model::MidiEvent>& events() const noexcept { return events_; }
    /// Grows every time events arrive (for caching what's derived from them).
    [[nodiscard]] std::uint64_t version() const noexcept { return version_; }
    /// The input clock now: the audio stream's time (so it runs with the audio, in tests too).
    [[nodiscard]] double now() const;
    /// "keyboard", or a MIDI input's name.
    [[nodiscard]] std::string sourceName(std::uint16_t source) const;

private:
    struct Raw
    {
        double time = 0.0;
        unsigned char bytes[3]{};
        int size = 0;
    };
    /// One input's queue: a single producer (its MIDI thread, or the message thread).
    struct Queue
    {
        juce::AbstractFifo fifo{4096};
        std::array<Raw, 4096> buffer;
        void push(const Raw& raw);
    };
    struct Device;
    [[nodiscard]] static bool keep(const juce::MidiMessage& m);

    te::Engine& engine_;
    MidiMapper* mapper_ = nullptr;
    std::vector<std::unique_ptr<Device>> devices_; // index + 1: the source
    Queue keyboard_;
    std::vector<model::MidiEvent> events_;
    std::uint64_t version_ = 0;
};
} // namespace fd::engine
