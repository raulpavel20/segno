#pragma once

#include <algorithm>
#include <cstdint>
#include <span>
#include <vector>

namespace fd
{
/// The song as a MIDI effect reads it (docs/design/midi-effects.md §5, ADR 0074): the harmony read
/// from the song's notes, its sections, and where this track's clips play and what they play,
/// ahead of the playhead. Built by the host off the audio thread whenever any of it changes, and
/// handed to the module whole (AssetHandoff); a module only reads it. Times are in beats from the
/// song's start, as TransportInfo::ppqPosition. A host without a song (any plugin host) hands none.
struct SongChord
{
    double start = 0.0, end = 0.0;
    int root = -1;           // 0 = C … 11 = B
    std::uint16_t notes = 0; // bit n: pitch class n is in the chord
    int bass = -1;           // the lowest note sounding (a MIDI pitch), −1 for none
};

struct SongSection
{
    double start = 0.0, end = 0.0;
    /// The section's name without a copy's number ("Chorus 2" is "Chorus"), as a hash: sections of
    /// the same name have the same one.
    std::uint32_t name = 0;
    int time = 1; // which time a section of this name plays: 1 for the first
    float energy = 0.5f;
};

/// A note one of this track's clips plays, in song beats.
struct SongNote
{
    double start = 0.0, length = 0.0;
    std::uint8_t key = 0, channel = 0;
    float velocity = 0.0f;
};

struct SongView
{
    std::vector<SongChord> chords;     // in time order, never overlapping
    std::vector<SongSection> sections; // in time order, never overlapping
    struct Span
    {
        double start = 0.0, end = 0.0;
    };
    std::vector<Span> clips;     // where this track's clips play, in time order
    std::vector<SongNote> notes; // what they play, by start

    /// The chord sounding at `beat`, or null.
    [[nodiscard]] const SongChord* chordAt(double beat) const noexcept { return at(chords, beat); }
    /// The first chord starting after `beat`, or null: what's coming.
    [[nodiscard]] const SongChord* chordAfter(double beat) const noexcept
    {
        const auto it = std::upper_bound(chords.begin(), chords.end(), beat,
                                         [](double b, const SongChord& c) { return b < c.start; });
        return it != chords.end() ? &*it : nullptr;
    }
    [[nodiscard]] const SongSection* sectionAt(double beat) const noexcept { return at(sections, beat); }
    /// Whether one of this track's clips plays at `beat`.
    [[nodiscard]] bool inClip(double beat) const noexcept { return at(clips, beat) != nullptr; }
    /// This track's notes starting in [from, to).
    [[nodiscard]] std::span<const SongNote> notesFrom(double from, double to) const noexcept
    {
        const auto byStart = [](const SongNote& n, double b) { return n.start < b; };
        const auto first = std::lower_bound(notes.begin(), notes.end(), from, byStart);
        const auto last = std::lower_bound(first, notes.end(), to, byStart);
        return {first, last};
    }

private:
    template <typename T> [[nodiscard]] static const T* at(const std::vector<T>& spans, double beat) noexcept
    {
        auto it = std::upper_bound(spans.begin(), spans.end(), beat,
                                   [](double b, const T& s) { return b < s.start; });
        if (it == spans.begin())
            return nullptr;
        --it;
        return beat < it->end ? &*it : nullptr;
    }
};
} // namespace fd
