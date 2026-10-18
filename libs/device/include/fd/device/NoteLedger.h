#pragma once

#include "fd/device/Events.h"

#include <array>
#include <cstdint>

namespace fd
{
/// The notes a module has started and not yet ended (docs/design/midi-effects.md §15): a MIDI
/// effect writes its notes through one, and ends them all when the song stops or jumps, when a
/// change reroutes them, or when it's turned off, so no note it started is left hanging. A note
/// started twice needs ending twice. Real-time safe: a fixed table, no allocation.
class NoteLedger
{
public:
    /// Writes `e` to `out`, counting the notes it starts and ends. False when `out` is full; a
    /// note-off for a note that isn't sounding is dropped (true: nothing was lost).
    bool write(EventList& out, const Event& e) noexcept
    {
        if (e.type == EventType::NoteOff)
        {
            auto& n = count(e.channel, e.key);
            if (n == 0)
                return true;
            if (!out.push(e))
                return false;
            --n;
            return true;
        }
        if (!out.push(e))
            return false;
        if (e.type == EventType::NoteOn)
        {
            auto& n = count(e.channel, e.key);
            if (n < 255)
                ++n;
        }
        return true;
    }

    [[nodiscard]] bool sounding(std::uint8_t channel, std::uint8_t key) const noexcept
    {
        return counts_[index(channel, key)] != 0;
    }

    [[nodiscard]] bool any() const noexcept
    {
        for (const auto c : counts_)
            if (c != 0)
                return true;
        return false;
    }

    /// A note-off at `offset` for every note sounding, as often as it was started.
    void endAll(EventList& out, int offset) noexcept
    {
        for (std::size_t i = 0; i < counts_.size(); ++i)
            while (counts_[i] != 0)
            {
                if (!out.push({.sampleOffset = offset,
                               .type = EventType::NoteOff,
                               .channel = static_cast<std::uint8_t>(i / 128),
                               .key = static_cast<std::uint8_t>(i % 128)}))
                    return;
                --counts_[i];
            }
    }

    /// Forgets every note without ending it (the host has ended them all).
    void clear() noexcept { counts_.fill(0); }

private:
    [[nodiscard]] static std::size_t index(std::uint8_t channel, std::uint8_t key) noexcept
    {
        return static_cast<std::size_t>(channel & 15) * 128 + (key & 127);
    }
    [[nodiscard]] std::uint8_t& count(std::uint8_t channel, std::uint8_t key) noexcept
    {
        return counts_[index(channel, key)];
    }

    std::array<std::uint8_t, 16 * 128> counts_{};
};
} // namespace fd
