#include "Pitch.h"

#include "../../midi-shared/dsp/PassThrough.h"

#include <cmath>
#include <fd/music/Following.h>

namespace fd::devices
{
namespace
{
constexpr Smoothing none{SmoothingKind::None, 0.0f};
constexpr std::array<std::string_view, 2> offOn{"Off", "On"};
constexpr std::array<std::string_view, 3> fitChoices{"Off", "Key", "Chord"};
constexpr std::array<std::string_view, 3> towardChoices{"Near", "Up", "Down"};
constexpr std::array<std::string_view, 2> changeChoices{"Move", "Hold"};
constexpr std::array<std::string_view, 3> wayChoices{"Up", "Down", "Both"};
constexpr std::array<std::string_view, 3> outsideChoices{"Fold", "Drop", "Limit"};

const std::array<ParamSpec, Pitch::Count>& table()
{
    static const auto t = []
    {
        std::array<ParamSpec, Pitch::Count> p{{
            {.id = "semitones",
             .name = "Semitones",
             .shortName = "Semis",
             .kind = ParamKind::Discrete,
             .min = -48.0f,
             .max = 48.0f,
             .unit = Unit::Semitones,
             .smoothing = none,
             .decimals = 0,
             .description = "Moves every note up or down by semitones."},
            {.id = "steps",
             .name = "Steps",
             .kind = ParamKind::Discrete,
             .min = -14.0f,
             .max = 14.0f,
             .smoothing = none,
             .decimals = 0,
             .description =
                 "Moves every note by steps of the key, so it stays in the key; semitones without one."},
            {.id = "mirror",
             .name = "Mirror",
             .kind = ParamKind::Toggle,
             .max = 1.0f,
             .choices = offOn,
             .smoothing = none,
             .description = "Turns the melody upside down around the Around note, by steps of the key."},
            {.id = "axis",
             .name = "Around",
             .kind = ParamKind::Discrete,
             .max = 127.0f,
             .def = 60.0f,
             .unit = Unit::Note,
             .smoothing = none,
             .description = "The note Mirror turns the melody around."},
            {.id = "fit",
             .name = "Fit",
             .kind = ParamKind::Choice,
             .max = 2.0f,
             .choices = fitChoices,
             .smoothing = none,
             .description = "Moves notes onto the key, onto the chord sounding, or leaves them as they are."},
            {.id = "toward",
             .name = "Toward",
             .kind = ParamKind::Choice,
             .max = 2.0f,
             .choices = towardChoices,
             .smoothing = none,
             .description = "Which way a note moves to fit: to the nearest note, up or down."},
            {.id = "passing",
             .name = "Passing notes",
             .kind = ParamKind::Toggle,
             .max = 1.0f,
             .def = 1.0f,
             .choices = offOn,
             .smoothing = none,
             .description =
                 "Fitting to the chord, notes off the beat fit to the key instead, so passing notes stay."},
            {.id = "change",
             .name = "At a change",
             .shortName = "Change",
             .kind = ParamKind::Choice,
             .max = 1.0f,
             .choices = changeChoices,
             .smoothing = none,
             .description =
                 "What a note held across a chord change does: moves to fit the new chord, or holds."},
            {.id = "chance",
             .name = "Chance",
             .min = 0.0f,
             .max = 100.0f,
             .unit = Unit::Percent,
             .smoothing = none,
             .decimals = 0,
             .description = "How likely each note is to move by chance."},
            {.id = "spread",
             .name = "Spread",
             .kind = ParamKind::Discrete,
             .min = 1.0f,
             .max = 12.0f,
             .def = 2.0f,
             .smoothing = none,
             .decimals = 0,
             .description = "How far a note moves by chance, in steps of the key (semitones without one)."},
            {.id = "way",
             .name = "Way",
             .kind = ParamKind::Choice,
             .max = 2.0f,
             .def = 2.0f,
             .choices = wayChoices,
             .smoothing = none,
             .description = "Which way a note moves by chance."},
            {.id = "lowest",
             .name = "Lowest",
             .kind = ParamKind::Discrete,
             .max = 127.0f,
             .unit = Unit::Note,
             .smoothing = none,
             .description = "The lowest note let through."},
            {.id = "highest",
             .name = "Highest",
             .kind = ParamKind::Discrete,
             .max = 127.0f,
             .def = 127.0f,
             .unit = Unit::Note,
             .smoothing = none,
             .description = "The highest note let through."},
            {.id = "outside",
             .name = "Outside",
             .shortName = "Out",
             .kind = ParamKind::Choice,
             .max = 2.0f,
             .choices = outsideChoices,
             .smoothing = none,
             .description = "What a note outside the range does: folds in by octaves, is left out, or plays "
                            "the nearest edge."},
        }};
        const auto h = harmony::params();
        std::copy(h.begin(), h.end(), p.begin() + Pitch::HarmonyFirst);
        return p;
    }();
    return t;
}

const DeviceInfo pitchInfo{.id = "pitch",
                           .name = "Pitch",
                           .category = "MIDI",
                           .kind = DeviceKind::MidiEffect,
                           .buses = midiEffectBuses,
                           .params = table()};
} // namespace

Pitch::Pitch() : NoteEffect(pitchInfo) {}

void Pitch::processNotes(ProcessContext& c, const BlockBeats& beats) noexcept
{
    follower_.begin(c, params(), HarmonyFirst);
    Moments moments(c, beats, follower_, tap());
    const bool moves = params().getInt(Fit) == 2 && params().getInt(Change) == 0;
    while (moments.next() != INT_MAX)
    {
        const auto m = moments.take();
        if (m.kind == Moments::Kind::Change && moves)
            refit(c, m.offset, beats);
        else if (m.kind == Moments::Kind::Input && !passOn(c, m.event))
            play(c, m.event, beats);
    }
}

namespace
{
music::PitchMoves movesOf(const ParamStore& p)
{
    return {.semitones = p.getInt(Pitch::Semitones),
            .steps = p.getInt(Pitch::Steps),
            .mirror = p.getBool(Pitch::Mirror),
            .axis = p.getInt(Pitch::Axis),
            .chance = p.get(Pitch::Chance) / 100.0,
            .spread = p.getInt(Pitch::Spread),
            .way = p.getInt(Pitch::Way)};
}

music::PitchFit fitOf(const ParamStore& p)
{
    return {.fit = p.getInt(Pitch::Fit),
            .toward = static_cast<music::Toward>(p.getInt(Pitch::Toward)),
            .passing = p.getBool(Pitch::Passing),
            .lowest = p.getInt(Pitch::Lowest),
            .highest = p.getInt(Pitch::Highest),
            .outside = p.getInt(Pitch::Outside)};
}

/// A note's place for its roll: its beat in 960ths and its key.
std::uint64_t place(double beat) noexcept
{
    return static_cast<std::uint64_t>(std::llround(beat * 960.0));
}
} // namespace

void Pitch::play(ProcessContext& c, const Event& e, const BlockBeats& beats) noexcept
{
    auto& h = held_[static_cast<std::size_t>(e.channel & 15) * 128 + (e.key & 127)];
    if (e.type == EventType::PolyPressure)
    {
        // Pressure follows its note to the note it became.
        if (h.out >= 0)
        {
            Event p = e;
            p.key = static_cast<std::uint8_t>(h.out);
            c.outEvents->push(p);
        }
        return;
    }
    if (h.out >= 0)
    {
        // Ended (or played again over itself): the note it became ends.
        stopNote(c, e.sampleOffset, e.channel, static_cast<std::uint8_t>(h.out));
        h.out = -1;
        --holding_;
    }
    if (e.type != EventType::NoteOn)
        return;

    const double beat = beats.at(e.sampleOffset);
    const int tie = last_ >= 0 && e.key > last_ ? 1 : -1;
    last_ = e.key;
    const auto key = follower_.key();
    const int moved =
        music::pitchMoved(e.key, movesOf(params()), key, music::mix(roll(), place(beat), e.key));
    const int out = music::pitchFitted(moved, fitOf(params()), key, follower_.chordAt(beat).notes,
                                       music::onBeat(beat), tie);
    if (out < 0 || out > 127)
        return;
    h = {.out = static_cast<std::int16_t>(out),
         .moved = static_cast<std::int16_t>(moved),
         .velocity = e.value,
         .tie = static_cast<std::int8_t>(tie)};
    ++holding_;
    startNote(c, {.sampleOffset = e.sampleOffset,
                  .channel = e.channel,
                  .key = static_cast<std::uint8_t>(out),
                  .value = e.value});
}

void Pitch::refit(ProcessContext& c, int offset, const BlockBeats& beats) noexcept
{
    if (holding_ == 0)
        return;
    const double beat = beats.at(offset);
    const auto key = follower_.key();
    const auto chord = follower_.chordAt(beat).notes;
    const auto fit = fitOf(params());
    for (std::size_t i = 0; i < held_.size(); ++i)
    {
        auto& h = held_[i];
        if (h.out < 0)
            continue;
        const int out = music::pitchFitted(h.moved, fit, key, chord, music::onBeat(beat), h.tie);
        if (out == h.out)
            continue;
        // A held note can't change pitch: it ends, and the note that fits starts at the change.
        const auto channel = static_cast<std::uint8_t>(i / 128);
        stopNote(c, offset, channel, static_cast<std::uint8_t>(h.out));
        if (out < 0 || out > 127)
        {
            h.out = -1;
            --holding_;
            continue;
        }
        h.out = static_cast<std::int16_t>(out);
        startNote(c, {.sampleOffset = offset,
                      .channel = channel,
                      .key = static_cast<std::uint8_t>(out),
                      .value = h.velocity});
    }
}

void Pitch::clearNotes() noexcept
{
    held_.fill({});
    holding_ = 0;
    last_ = -1;
    follower_.reset();
}

const DeviceFactory& pitchFactory()
{
    static const DeviceFactory f{[]() -> const DeviceInfo& { return pitchInfo; },
                                 []() -> std::unique_ptr<Device> { return std::make_unique<Pitch>(); }};
    return f;
}
} // namespace fd::devices
