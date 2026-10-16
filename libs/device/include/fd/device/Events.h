#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace fd
{
enum class EventType : std::uint8_t
{
    NoteOn,
    NoteOff,
    Controller,      // key = CC number, value 0..1
    PitchBend,       // value -1..1
    ChannelPressure, // value 0..1
    PolyPressure,    // key = note, value 0..1
    AllNotesOff,
    /// Every sound stops now, within a short fade (MIDI CC 120): a Drum Kit's choke group
    /// (ADR 0064). Added after M1's freeze (ADR 0034); a device that doesn't handle it ignores it.
    AllSoundOff
};

/// A timestamped event within the current block.
struct Event
{
    int sampleOffset = 0;
    EventType type = EventType::NoteOn;
    std::uint8_t channel = 0; // 0..15
    std::uint8_t key = 0;     // note or CC number
    float value = 0.0f;       // velocity / CC / bend / pressure (normalised as above)
    std::int32_t noteId = -1; // host note id (CLAP/MPE), -1 if unknown
};

/// Fixed-capacity event list. Capacity is reserved in prepare(); push() is real-time safe
/// and drops events when full.
class EventList
{
public:
    void reserve(size_t capacity) { events_.resize(capacity); }

    void clear() noexcept { size_ = 0; }

    bool push(const Event& e) noexcept
    {
        if (size_ >= events_.size())
            return false;
        events_[size_++] = e;
        return true;
    }

    /// Stable insertion sort by sampleOffset (no allocation; lists are short and nearly sorted).
    void sortByTime() noexcept
    {
        for (size_t i = 1; i < size_; ++i)
        {
            const Event e = events_[i];
            size_t j = i;
            while (j > 0 && events_[j - 1].sampleOffset > e.sampleOffset)
            {
                events_[j] = events_[j - 1];
                --j;
            }
            events_[j] = e;
        }
    }

    [[nodiscard]] size_t size() const noexcept { return size_; }
    [[nodiscard]] bool empty() const noexcept { return size_ == 0; }
    [[nodiscard]] size_t capacity() const noexcept { return events_.size(); }
    [[nodiscard]] const Event& operator[](size_t i) const noexcept { return events_[i]; }
    [[nodiscard]] const Event* begin() const noexcept { return events_.data(); }
    [[nodiscard]] const Event* end() const noexcept { return events_.data() + size_; }

private:
    std::vector<Event> events_;
    size_t size_ = 0;
};
} // namespace fd
