#pragma once

#include "Follower.h"

#include <array>
#include <atomic>
#include <climits>
#include <fd/device/Device.h>
#include <fd/device/NoteLedger.h>

namespace fd::devices
{
/// Notes a module plays later (a strum's, an arpeggio's ends): each at a sample of the module's
/// clock, with the note that started it, so they go when it ends. Fixed size, real-time safe.
class Schedule
{
public:
    struct Item
    {
        std::int64_t at = 0;
        Event event{};
        std::int32_t source = -1;
        std::uint32_t order = 0;
    };

    /// False when full (the note is lost: too many at once).
    bool add(std::int64_t at, const Event& e, std::int32_t source = -1) noexcept;
    /// The earliest, or null; at one time, note-offs first, then in the order added.
    [[nodiscard]] const Item* next() const noexcept;
    Item take() noexcept;
    /// Forgets the notes a source would still start (its strum, when it ends first).
    void dropStarts(std::int32_t source) noexcept;
    /// Forgets one note a source would still start: false if it isn't waiting (it has started).
    bool dropStart(std::int32_t source, std::uint8_t key) noexcept;
    void clear() noexcept { size_ = 0; }
    [[nodiscard]] bool empty() const noexcept { return size_ == 0; }
    /// How many more it can take.
    [[nodiscard]] std::size_t room() const noexcept { return items_.size() - size_; }

private:
    [[nodiscard]] std::size_t nextIndex() const noexcept;

    std::array<Item, 512> items_{};
    std::size_t size_ = 0;
    std::uint32_t order_ = 0;
};

/// A block's moments, in time order: the chord changing (the song's, or the notes the module
/// follows: the listened track's, those below the split), and the events coming in. At one
/// sample the chord settles first, so a note on a change plays the new chord.
class Moments
{
public:
    enum class Kind : std::uint8_t
    {
        Nothing, // notes heard that changed nothing
        Change,  // the chord followed changes here
        Input
    };
    struct Moment
    {
        Kind kind = Kind::Nothing;
        int offset = 0;
        Event event{};
    };

    /// With a tap, what it follows and what comes in are written to it (the note display).
    Moments(const ProcessContext& c, const BlockBeats& beats, Follower& follower,
            NoteTap* tap = nullptr) noexcept;
    /// The next moment's sample, or INT_MAX when there's none.
    [[nodiscard]] int next() const noexcept;
    Moment take() noexcept;

private:
    [[nodiscard]] bool played(const Event& e) const noexcept;

    const EventList* events_;
    const EventList* listen_;
    Follower& follower_;
    const BlockBeats& beats_;
    NoteTap* tap_;
    std::array<int, 64> changes_{};
    std::size_t changeCount_ = 0, change_ = 0, event_ = 0, heard_ = 0;
    int settled_ = -1; // the sample whose followed notes were taken in
};

/// What every MIDI effect of the suite shares (midi-effects.md §15): its beats and a clock, its
/// notes written through a ledger so none is left hanging, notes shared by what started them, and
/// its roll (ADR 0062). Notes in, notes out, never audio.
class NoteEffect : public Device
{
public:
    explicit NoteEffect(const DeviceInfo& info) : Device(info) { analysis().enableNotes(); }

    void prepare(const ProcessSpec& spec) override;
    void reset() noexcept override;
    void process(ProcessContext& context) noexcept final;
    /// A new pass: fresh dice (the host calls it before the pass's first block).
    void startPass(std::uint64_t roll) noexcept override;
    /// The roll the pass playing (or the last one) drew from: what Keep as notes plays again
    /// (midi-effects.md §9.1). Any thread.
    [[nodiscard]] std::uint64_t rollHeard() const noexcept
    {
        return rollHeard_.load(std::memory_order_relaxed);
    }

protected:
    /// The module's block: its notes in `c.events`, written to `c.outEvents`.
    virtual void processNotes(ProcessContext& c, const BlockBeats& beats) noexcept = 0;
    /// Its own state cleared (notes held, chords, steps): reset, and every note ended.
    virtual void clearNotes() noexcept = 0;

    /// The roll this pass draws from.
    [[nodiscard]] std::uint64_t roll() const noexcept { return roll_; }
    /// The module's clock at a sample of this block: samples since it was prepared.
    [[nodiscard]] std::int64_t clockAt(int offset) const noexcept { return blockClock_ + offset; }
    /// A clock time as a sample of this block (past its end when later).
    [[nodiscard]] std::int64_t offsetOf(std::int64_t at) const noexcept { return at - blockClock_; }
    [[nodiscard]] double sampleRate() const noexcept { return sampleRate_; }

    /// Starts a note for one more owner (a note played, a step); a note sounding starts again.
    void startNote(ProcessContext& c, const Event& on) noexcept;
    /// One owner lets go of a note: it ends when none holds it.
    void stopNote(ProcessContext& c, int offset, std::uint8_t channel, std::uint8_t key) noexcept;
    /// Ends every note it started, at `offset`, and forgets what it was going to play.
    void endAll(ProcessContext& c, int offset) noexcept;
    /// Events it doesn't change (controllers, bend, pressure) go on as they came; all notes off
    /// ends its own notes and its state first. False for the notes, which the module plays.
    bool passOn(ProcessContext& c, const Event& e) noexcept;
    /// The schedule's notes due at or before `offset` (note-offs only, or all), played.
    void playScheduled(ProcessContext& c, int offset, bool offsOnly) noexcept;
    /// The next scheduled note in this block: its sample and whether it's a note-off; INT_MAX if none.
    [[nodiscard]] int nextScheduled(int numSamples, bool& isOff) const noexcept;
    /// Where its note display reads what it follows and hears (Moments writes there).
    [[nodiscard]] NoteTap* tap() noexcept { return analysis().notes(); }

    Schedule schedule_;

private:
    [[nodiscard]] static std::size_t slot(std::uint8_t channel, std::uint8_t key) noexcept
    {
        return static_cast<std::size_t>(channel & 15) * 128 + (key & 127);
    }

    NoteLedger ledger_;
    std::array<std::uint8_t, 16 * 128> owners_{};
    double sampleRate_ = 48000.0;
    std::int64_t clock_ = 0, blockClock_ = 0;
    BlockBeats beats_; // this block's, for the marks the display reads
    double ownBeat_ = 0.0;
    bool wasPlaying_ = false;
    std::atomic<std::uint64_t> hostRoll_{0}, rollHeard_{1};
    std::atomic<bool> hostRolled_{false};
    std::uint64_t roll_ = 1, selfRolls_ = 0;
};
} // namespace fd::devices
