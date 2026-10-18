#include "Chord.h"

#include "../../midi-shared/dsp/PassThrough.h"

#include <algorithm>
#include <cmath>
#include <fd/music/Following.h>

namespace fd::devices
{
namespace
{
constexpr Smoothing none{SmoothingKind::None, 0.0f};
constexpr std::array<std::string_view, 2> offOn{"Off", "On"};
constexpr std::array<std::string_view, 3> modeChoices{"Add", "Song", "Keys"};
constexpr std::array<std::string_view, 3> kindChoices{"Triad", "7th", "9th"};
constexpr std::array<std::string_view, 4> voicingChoices{"Close", "Open", "Drop 2", "Spread"};
constexpr std::array<std::string_view, 3> placeChoices{"Top", "Bottom", "Smooth"};
constexpr std::array<std::string_view, 2> changeChoices{"Move", "Hold"};
constexpr std::array<std::string_view, 3> directionChoices{"Up", "Down", "Alt"};
constexpr std::array<std::string_view, 6> shiftIds{"shift1", "shift2", "shift3",
                                                   "shift4", "shift5", "shift6"};
constexpr std::array<std::string_view, 6> shiftNames{"Shift 1", "Shift 2", "Shift 3",
                                                     "Shift 4", "Shift 5", "Shift 6"};
constexpr std::array<float, 6> shiftDefaults{4.0f, 7.0f, 0.0f, 0.0f, 0.0f, 0.0f};

const std::array<ParamSpec, Chord::Count>& table()
{
    static const auto t = []
    {
        std::array<ParamSpec, Chord::Count> p{};
        p[Chord::Mode] = {.id = "mode",
                          .name = "Mode",
                          .kind = ParamKind::Choice,
                          .max = 2.0f,
                          .choices = modeChoices,
                          .smoothing = none,
                          .description = "Adds notes at your intervals, plays the song's chord on each note, "
                                         "or the key's chord for each key."};
        for (std::size_t i = 0; i < 6; ++i)
            p[Chord::Shift1 + i] = {.id = shiftIds[i],
                                    .name = shiftNames[i],
                                    .shortName = shiftNames[i],
                                    .kind = ParamKind::Discrete,
                                    .min = -36.0f,
                                    .max = 36.0f,
                                    .def = shiftDefaults[i],
                                    .smoothing = none,
                                    .decimals = 0,
                                    .description =
                                        "A note added over each note played, this far from it; 0 adds none."};
        p[Chord::InKey] = {.id = "inkey",
                           .name = "In key",
                           .kind = ParamKind::Toggle,
                           .max = 1.0f,
                           .choices = offOn,
                           .smoothing = none,
                           .description = "Shifts count steps of the key, so each added note stays in it."};
        p[Chord::Level] = {.id = "level",
                           .name = "Level",
                           .max = 100.0f,
                           .def = 100.0f,
                           .unit = Unit::Percent,
                           .smoothing = none,
                           .decimals = 0,
                           .description = "How hard the added notes play, as a share of the note played."};
        p[Chord::Kind] = {
            .id = "kind",
            .name = "Kind",
            .kind = ParamKind::Choice,
            .max = 2.0f,
            .choices = kindChoices,
            .smoothing = none,
            .description = "How many notes the chords have in Song and Keys: three, a 7th added, or a 9th."};
        p[Chord::Voicing] = {
            .id = "voicing",
            .name = "Voicing",
            .kind = ParamKind::Choice,
            .max = 3.0f,
            .choices = voicingChoices,
            .smoothing = none,
            .description =
                "How the chord's notes are spread: close, open, drop 2 or spread over two octaves."};
        p[Chord::Place] = {.id = "place",
                           .name = "Place",
                           .kind = ParamKind::Choice,
                           .max = 2.0f,
                           .def = 2.0f,
                           .choices = placeChoices,
                           .smoothing = none,
                           .description = "In Song, where the note you play goes in the chord: on top, at "
                                          "the bottom, or wherever leads smoothest from the last chord."};
        p[Chord::Change] = {.id = "change",
                            .name = "At a change",
                            .shortName = "Change",
                            .kind = ParamKind::Choice,
                            .max = 1.0f,
                            .choices = changeChoices,
                            .smoothing = none,
                            .description = "In Song, what a held chord does when the song's chord changes: "
                                           "moves to the new one, or holds."};
        p[Chord::Around] = {.id = "around",
                            .name = "Around",
                            .kind = ParamKind::Discrete,
                            .max = 127.0f,
                            .def = 60.0f,
                            .unit = Unit::Note,
                            .smoothing = none,
                            .description = "In Keys, the note the chords are voiced around."};
        p[Chord::Strum] = {.id = "strum",
                           .name = "Strum",
                           .shortName = "Time",
                           .max = 400.0f,
                           .skewCentre = 60.0f,
                           .unit = Unit::Milliseconds,
                           .smoothing = none,
                           .decimals = 0,
                           .description = "Spreads each chord's notes out in time, the first on the beat."};
        p[Chord::Curve] = {.id = "curve",
                           .name = "Curve",
                           .min = -100.0f,
                           .max = 100.0f,
                           .unit = Unit::Percent,
                           .smoothing = none,
                           .decimals = 0,
                           .description = "Whether the strum speeds up or slows down through the chord."};
        p[Chord::Swell] = {.id = "swell",
                           .name = "Swell",
                           .min = -100.0f,
                           .max = 100.0f,
                           .unit = Unit::Percent,
                           .smoothing = none,
                           .decimals = 0,
                           .description = "Each note of a strum louder (or softer) than the one before."};
        p[Chord::Direction] = {.id = "direction",
                               .name = "Direction",
                               .kind = ParamKind::Choice,
                               .max = 2.0f,
                               .choices = directionChoices,
                               .smoothing = none,
                               .description =
                                   "The order a strum plays the chord's notes in: up, down, or alternating."};
        const auto h = harmony::params();
        std::copy(h.begin(), h.end(), p.begin() + Chord::HarmonyFirst);
        return p;
    }();
    return t;
}

const DeviceInfo chordInfo{.id = "chord",
                           .name = "Chord",
                           .category = "MIDI",
                           .kind = DeviceKind::MidiEffect,
                           .buses = midiEffectBuses,
                           .params = table()};
} // namespace

Chord::Chord() : NoteEffect(chordInfo) {}

void Chord::processNotes(ProcessContext& c, const BlockBeats& beats) noexcept
{
    follower_.begin(c, params(), HarmonyFirst);
    Moments moments(c, beats, follower_);
    const bool moves = params().getInt(Mode) == Song && params().getInt(Change) == 0;
    for (;;)
    {
        // At one sample: the strums' ends, then what comes in, then the strums' notes.
        bool off = false;
        const int scheduled = nextScheduled(c.numSamples, off);
        const int moment = moments.next();
        if (scheduled == INT_MAX && moment == INT_MAX)
            break;
        if (scheduled < moment || (scheduled == moment && off))
        {
            playScheduled(c, scheduled, scheduled == moment);
            continue;
        }
        const auto m = moments.take();
        if (m.kind == Moments::Kind::Change && moves)
            move(c, m.offset, beats);
        else if (m.kind == Moments::Kind::Input && !passOn(c, m.event))
            play(c, m.event, beats);
    }
}

std::size_t Chord::chordFor(int key, float velocity, double beat, std::span<int> notes,
                            std::span<float> velocities) noexcept
{
    const auto& p = params();
    const auto keySet = follower_.key();
    const int kind = 3 + p.getInt(Kind);
    const auto voicing =
        std::array{music::Voicing::Close, music::Voicing::Open, music::Voicing::Drop2,
                   music::Voicing::Spread}[static_cast<std::size_t>(std::clamp(p.getInt(Voicing), 0, 3))];
    const std::span<const int> previous(previous_.data(), previousCount_);
    std::array<int, 16> voiced{};
    std::size_t n = 0;
    switch (p.getInt(Mode))
    {
    case Add:
    {
        // The note, and each shift that lands on a note not there yet, at Level.
        voiced[n++] = key;
        for (std::size_t i = 0; i < 6; ++i)
        {
            const int shift = p.getInt(Shift1 + i);
            if (shift == 0)
                continue;
            const int note = p.getBool(InKey) ? music::stepBy(key, keySet, shift) : key + shift;
            if (note >= 0 && note <= 127 &&
                std::find(voiced.begin(), voiced.begin() + static_cast<std::ptrdiff_t>(n), note) ==
                    voiced.begin() + static_cast<std::ptrdiff_t>(n))
                voiced[n++] = note;
        }
        break;
    }
    case Song:
    {
        const auto chord = follower_.chordAt(beat);
        const auto set = music::chordOfKind(chord.root, chord.notes, keySet, kind);
        n = music::voiceAround(key, chord.root, set, static_cast<music::Place>(p.getInt(Place)), voicing,
                               previous, voiced);
        break;
    }
    default: // Keys: the key's chord on the note's step; a note outside the key as it is
        if (music::inSet(keySet, key))
        {
            std::array<int, 5> tones{};
            const auto count = music::stackThirds(key, keySet, kind, tones);
            music::PitchSet set = 0;
            for (std::size_t i = 0; i < count; ++i)
                set = static_cast<music::PitchSet>(set | (1u << (((tones[i] % 12) + 12) % 12)));
            n = music::voiceNear(key % 12, set, p.getInt(Around), voicing, previous, voiced);
        }
        break;
    }
    if (n == 0)
        voiced[n++] = key;
    n = std::min<std::size_t>(n, std::min(notes.size(), static_cast<std::size_t>(maxNotes)));
    std::sort(voiced.begin(), voiced.begin() + static_cast<std::ptrdiff_t>(n));
    const float level = p.getInt(Mode) == Add ? p.get(Level) / 100.0f : 1.0f;
    for (std::size_t i = 0; i < n; ++i)
    {
        notes[i] = voiced[i];
        velocities[i] = voiced[i] == key ? velocity : velocity * level;
    }
    if (p.getInt(Mode) != Add && n > 1) // a chord, not a note left as it is
    {
        std::copy_n(voiced.begin(), n, previous_.begin());
        previousCount_ = n;
    }
    return n;
}

void Chord::start(ProcessContext& c, std::size_t source, int offset, std::span<const int> notes,
                  std::span<const float> velocities) noexcept
{
    const auto& p = params();
    auto& s = sources_[source];
    const auto channel = static_cast<std::uint8_t>(source / 128);
    const auto n = notes.size();
    // The strum: the notes in Direction's order, the first on the note's time, the rest spread
    // over Strum by Curve (above zero it speeds up through the chord), each Swell louder.
    const double strum = p.get(Strum) / 1000.0 * sampleRate();
    const double bend = std::exp2(-p.get(Curve) / 50.0);
    const float swell = p.get(Swell) / 100.0f;
    bool down = p.getInt(Direction) == 1;
    if (p.getInt(Direction) == 2)
    {
        down = down_;
        down_ = !down_;
    }
    s.count = 0;
    for (std::size_t i = 0; i < n; ++i)
    {
        const std::size_t at = down ? n - 1 - i : i;
        const double x = n > 1 ? static_cast<double>(i) / static_cast<double>(n - 1) : 0.0;
        const auto delay = static_cast<std::int64_t>(std::llround(strum * std::pow(x, bend)));
        float velocity = velocities[at];
        if (strum > 0.0)
            velocity *= 1.0f + swell * static_cast<float>(x);
        const Event on{.sampleOffset = offset,
                       .channel = channel,
                       .key = static_cast<std::uint8_t>(notes[at]),
                       .value = std::clamp(velocity, 1.0f / 127.0f, 1.0f)};
        s.notes[s.count++] = static_cast<std::uint8_t>(notes[at]);
        if (delay <= 0)
            startNote(c, on);
        else
            schedule_.add(clockAt(offset) + delay, on, static_cast<std::int32_t>(source));
    }
}

void Chord::stop(ProcessContext& c, std::size_t source, int offset) noexcept
{
    auto& s = sources_[source];
    const auto channel = static_cast<std::uint8_t>(source / 128);
    for (std::size_t i = 0; i < s.count; ++i)
        if (!schedule_.dropStart(static_cast<std::int32_t>(source), s.notes[i]))
            stopNote(c, offset, channel, s.notes[i]);
    s.count = 0;
}

void Chord::play(ProcessContext& c, const Event& e, const BlockBeats& beats) noexcept
{
    const std::size_t source = static_cast<std::size_t>(e.channel & 15) * 128 + (e.key & 127);
    auto& s = sources_[source];
    if (e.type == EventType::PolyPressure)
    {
        // Pressure reaches every note of its chord.
        for (std::size_t i = 0; i < s.count; ++i)
        {
            Event p = e;
            p.key = s.notes[i];
            c.outEvents->push(p);
        }
        return;
    }
    if (s.active)
    {
        stop(c, source, e.sampleOffset);
        s.active = false;
        --holding_;
    }
    auto& held = heldIn_[e.key & 127];
    if (e.type == EventType::NoteOn)
        held = static_cast<std::uint8_t>(std::min(255, held + 1));
    else if (held > 0)
        --held;
    publishHeld();
    if (e.type != EventType::NoteOn)
        return;

    std::array<int, maxNotes> notes{};
    std::array<float, maxNotes> velocities{};
    const auto n = chordFor(e.key, e.value, beats.at(e.sampleOffset), notes, velocities);
    s.active = true;
    s.velocity = e.value;
    ++holding_;
    start(c, source, e.sampleOffset, std::span<const int>(notes.data(), n),
          std::span<const float>(velocities.data(), n));
}

void Chord::move(ProcessContext& c, int offset, const BlockBeats& beats) noexcept
{
    if (holding_ == 0)
        return;
    for (std::size_t source = 0; source < sources_.size(); ++source)
    {
        auto& s = sources_[source];
        if (!s.active)
            continue;
        // The note played is the one the source's key names.
        std::array<int, maxNotes> notes{};
        std::array<float, maxNotes> velocities{};
        const auto n =
            chordFor(static_cast<int>(source % 128), s.velocity, beats.at(offset), notes, velocities);
        bool same = n == s.count;
        for (std::size_t i = 0; same && i < n; ++i)
            same =
                std::find(s.notes.begin(), s.notes.begin() + s.count, notes[i]) != s.notes.begin() + s.count;
        if (same)
            continue;
        stop(c, source, offset);
        start(c, source, offset, std::span<const int>(notes.data(), n),
              std::span<const float>(velocities.data(), n));
    }
}

void Chord::publishHeld() noexcept
{
    std::array<std::uint64_t, 2> mask{};
    for (std::size_t k = 0; k < 128; ++k)
        if (heldIn_[k] > 0)
            mask[k / 64] |= std::uint64_t{1} << (k % 64);
    heldMask_[0].store(mask[0], std::memory_order_relaxed);
    heldMask_[1].store(mask[1], std::memory_order_relaxed);
}

std::size_t Chord::heldNotes(std::span<int> out) const noexcept
{
    std::size_t n = 0;
    for (int k = 0; k < 128 && n < out.size(); ++k)
        if ((heldMask_[static_cast<std::size_t>(k / 64)].load(std::memory_order_relaxed) >> (k % 64)) & 1u)
            out[n++] = k;
    return n;
}

void Chord::clearNotes() noexcept
{
    sources_.fill({});
    holding_ = 0;
    previousCount_ = 0;
    down_ = false;
    heldIn_.fill(0);
    publishHeld();
    follower_.reset();
}

const DeviceFactory& chordFactory()
{
    static const DeviceFactory f{[]() -> const DeviceInfo& { return chordInfo; },
                                 []() -> std::unique_ptr<Device> { return std::make_unique<Chord>(); }};
    return f;
}
} // namespace fd::devices
