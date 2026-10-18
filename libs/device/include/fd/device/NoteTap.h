#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace fd
{
/// What a MIDI effect heard, played and followed, for its note display (docs/design/midi-effects.md
/// §12; added 2026-10-18, ADR 0074): the audio thread writes marks, the UI reads them, one of each
/// (single producer, single consumer). Fixed size and lock-free: when the UI falls behind, new
/// marks are dropped, never the audio thread held up. Times are song beats.
class NoteTap
{
public:
    struct Mark
    {
        enum class Type : std::uint8_t
        {
            NoteOn,
            NoteOff,
            AllOff, // every note ends here (a stop, a jump)
            Chord   // the chord it follows from here
        };
        double beat = 0.0;
        Type type = Type::NoteOn;
        bool out = false; // a note it played; else one that came in
        std::uint8_t key = 0;
        float velocity = 0.0f;
        int root = -1;           // a chord's: 0 = C … 11 = B
        std::uint16_t notes = 0; // its pitch classes
        bool fallback = false;   // nothing to follow there: the key's home chord, or its own
    };

    // ---- the audio thread ----

    /// False when full (the mark is lost: the display shows less, nothing else).
    bool push(const Mark& m) noexcept
    {
        const auto w = write_.load(std::memory_order_relaxed);
        if (w - read_.load(std::memory_order_acquire) >= marks_.size())
            return false;
        marks_[w & (marks_.size() - 1)] = m;
        write_.store(w + 1, std::memory_order_release);
        return true;
    }
    /// The chord it follows from `beat`, written when it differs from the last one written.
    void chord(double beat, int root, std::uint16_t notes, bool fallback) noexcept
    {
        if (root == lastRoot_ && notes == lastNotes_ && fallback == lastFallback_)
            return;
        if (push({.beat = beat,
                  .type = Mark::Type::Chord,
                  .root = root,
                  .notes = notes,
                  .fallback = fallback}))
        {
            lastRoot_ = root;
            lastNotes_ = notes;
            lastFallback_ = fallback;
        }
    }
    /// Where the song is (the end of the last block) and its bar; the key it follows, `song` false
    /// where the host has no song (a plugin host).
    void setNow(double beat, int numerator, int denominator) noexcept
    {
        now_.store(beat, std::memory_order_relaxed);
        bar_.store(numerator * 64 + denominator, std::memory_order_relaxed);
    }
    void setKey(int root, std::uint16_t notes, bool song) noexcept
    {
        key_.store((song ? 1 << 20 : 0) | (root & 0xF) << 16 | notes, std::memory_order_relaxed);
    }

    // ---- the UI ----

    bool pop(Mark& m) noexcept
    {
        const auto r = read_.load(std::memory_order_relaxed);
        if (r == write_.load(std::memory_order_acquire))
            return false;
        m = marks_[r & (marks_.size() - 1)];
        read_.store(r + 1, std::memory_order_release);
        return true;
    }
    [[nodiscard]] double now() const noexcept { return now_.load(std::memory_order_relaxed); }
    [[nodiscard]] double beatsPerBar() const noexcept
    {
        const int bar = bar_.load(std::memory_order_relaxed);
        return (bar / 64) * 4.0 / std::max(1, bar % 64);
    }
    [[nodiscard]] int keyRoot() const noexcept { return (key_.load(std::memory_order_relaxed) >> 16) & 0xF; }
    [[nodiscard]] std::uint16_t keyNotes() const noexcept
    {
        return static_cast<std::uint16_t>(key_.load(std::memory_order_relaxed) & 0xFFFF);
    }
    [[nodiscard]] bool hasSong() const noexcept { return (key_.load(std::memory_order_relaxed) >> 20) != 0; }

private:
    std::array<Mark, 1024> marks_{};
    std::atomic<std::size_t> write_{0}, read_{0};
    std::atomic<double> now_{0.0};
    std::atomic<int> bar_{4 * 64 + 4};
    std::atomic<int> key_{0x0AB5}; // C major, no song, until a block says otherwise
    // The writer's: the last chord written.
    int lastRoot_ = -2;
    std::uint16_t lastNotes_ = 0;
    bool lastFallback_ = false;
};
} // namespace fd
