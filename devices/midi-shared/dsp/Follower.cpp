#include "Follower.h"

#include <algorithm>
#include <cmath>
#include <fd/music/Following.h>

namespace fd::devices
{
long BlockBeats::sampleOf(double beat) const noexcept
{
    if (perSample <= 0.0)
        return 0;
    return std::lround((beat - first) / perSample);
}

music::PitchSet ownChordIntervals(int kind) noexcept
{
    static constexpr std::array<music::PitchSet, 8> kinds{
        0b000010010001, // Major: 0 4 7
        0b000010001001, // Minor: 0 3 7
        0b010010010001, // 7: 0 4 7 10
        0b100010010001, // Maj 7: 0 4 7 11
        0b010010001001, // Min 7: 0 3 7 10
        0b000010100001, // Sus 4: 0 5 7
        0b000001001001, // Dim: 0 3 6
        0b000100010001, // Aug: 0 4 8
    };
    return kinds[static_cast<std::size_t>(std::clamp(kind, 0, 7))];
}

void Follower::begin(const ProcessContext& context, const ParamStore& params, std::size_t first) noexcept
{
    using namespace harmony;
    follow_ = params.getInt(first + Follow);
    split_ = params.getInt(first + Split);
    const int root = params.getInt(first + Root);
    const auto& songKey = context.transport.key;
    if (params.getInt(first + Key) == 0 && songKey.known())
    {
        key_ = songKey.notes;
        keyRoot_ = songKey.root;
    }
    else
    {
        // Its own key: also the song's when the song has none (a plugin host, a song without one).
        const auto ids = scaleIds();
        const auto index = std::clamp(params.getInt(first + Scale), 0, static_cast<int>(ids.size()) - 1);
        const auto* scale = music::findScale(ids[static_cast<std::size_t>(index)]);
        key_ = music::rotated(scale != nullptr ? scale->intervals : music::PitchSet{0x0AB5}, root);
        keyRoot_ = root;
    }
    own_ = {root, music::rotated(ownChordIntervals(params.getInt(first + harmony::Offset::Chord)), root),
            false};
    song_ = context.song;
}

void Follower::reset() noexcept
{
    played_.fill(0);
    heard_.fill(0);
    hasPlayed_ = hasHeard_ = false;
}

Follower::Chord Follower::home() const noexcept
{
    // The key's chord on its root: a triad of its notes.
    std::array<int, 3> tones{};
    const auto n = music::stackThirds(keyRoot_ + 60, key_, 3, tones);
    music::PitchSet set = 0;
    for (std::size_t i = 0; i < n; ++i)
        set = static_cast<music::PitchSet>(set | (1u << (((tones[i] % 12) + 12) % 12)));
    return {keyRoot_, set, true};
}

bool Follower::heldChord(const std::array<std::uint8_t, 128>& held, Chord& out) noexcept
{
    Chord c;
    int lowest = -1;
    for (int k = 0; k < 128; ++k)
        if (held[static_cast<std::size_t>(k)] > 0)
        {
            if (lowest < 0)
                lowest = k;
            c.notes = static_cast<music::PitchSet>(c.notes | (1u << (k % 12)));
        }
    if (lowest < 0)
        return false;
    c.root = lowest % 12; // the bass names it
    out = c;
    return true;
}

Follower::Chord Follower::chordAt(double beat) const noexcept
{
    switch (follow_)
    {
    case 1:
        return hasHeard_ ? heardChord_ : home();
    case 2:
        return hasPlayed_ ? playedChord_ : home();
    case 3:
        return own_;
    default:
        break;
    }
    if (song_ != nullptr)
        if (const auto* c = song_->chordAt(beat); c != nullptr && c->notes != 0)
            return {c->root, c->notes, false};
    return home();
}

std::size_t Follower::songChanges(const BlockBeats& beats, std::span<int> out) const noexcept
{
    if (follow_ != 0 || song_ == nullptr || out.empty())
        return 0;
    // Chords ending after the block's first sample can change inside it (a margin of a sample each
    // side: a change belongs to the block whose sample it rounds to).
    const double from = beats.first - beats.perSample, to = beats.end() + beats.perSample;
    const auto& chords = song_->chords;
    auto it = std::lower_bound(chords.begin(), chords.end(), from,
                               [](const SongChord& c, double b) { return c.end < b; });
    std::size_t n = 0;
    const auto add = [&](double at)
    {
        const long s = beats.sampleOf(at);
        if (s >= 0 && s < beats.numSamples && n < out.size() && (n == 0 || out[n - 1] != static_cast<int>(s)))
            out[n++] = static_cast<int>(s);
    };
    for (; it != chords.end() && it->start < to; ++it)
    {
        add(it->start);
        add(it->end);
    }
    return n;
}

bool Follower::takePlayed(const Event& e) noexcept
{
    if (follow_ != 2 || (e.type != EventType::NoteOn && e.type != EventType::NoteOff) || e.key >= split_)
        return false;
    auto& n = played_[e.key];
    if (e.type == EventType::NoteOn)
        n = static_cast<std::uint8_t>(std::min(255, n + 1));
    else if (n > 0)
        --n;
    return true;
}

void Follower::hear(const Event& e) noexcept
{
    if (e.type == EventType::AllNotesOff)
    {
        heard_.fill(0);
        return;
    }
    if (e.type != EventType::NoteOn && e.type != EventType::NoteOff)
        return;
    auto& n = heard_[e.key];
    if (e.type == EventType::NoteOn)
        n = static_cast<std::uint8_t>(std::min(255, n + 1));
    else if (n > 0)
        --n;
}

bool Follower::settle() noexcept
{
    const auto before = chordAt(0.0);
    if (Chord c; heldChord(heard_, c))
    {
        heardChord_ = c;
        hasHeard_ = true;
    }
    if (Chord c; heldChord(played_, c))
    {
        playedChord_ = c;
        hasPlayed_ = true;
    }
    return followsHeld() && !(chordAt(0.0) == before);
}
} // namespace fd::devices
