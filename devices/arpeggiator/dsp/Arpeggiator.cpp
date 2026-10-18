#include "Arpeggiator.h"

#include "../../midi-shared/PassThrough.h"

namespace fd::devices
{
namespace
{
constexpr Smoothing none{SmoothingKind::None, 0.0f};
constexpr std::array<std::string_view, 2> offOn{"Off", "On"};
constexpr std::array<std::string_view, 10> styleChoices{
    "Up", "Down", "Up-down", "Down-up", "Converge", "Diverge", "Played", "Chord", "Random", "Shuffled"};
constexpr std::array<std::string_view, 15> rateChoices{"1/1",  "1/2",   "1/2T", "1/4D",  "1/4",
                                                       "1/4T", "1/8D",  "1/8",  "1/8T",  "1/16D",
                                                       "1/16", "1/16T", "1/32", "1/32T", "1/64"};
constexpr std::array<std::string_view, 7> retriggerChoices{"Off",    "Note",   "1 bar",  "2 bars",
                                                           "4 bars", "8 bars", "Section"};
constexpr std::array<std::string_view, 5> laneIds{"velocityLength", "gateLength", "octaveLength",
                                                  "ratchetLength", "chanceLength"};
constexpr std::array<std::string_view, 5> laneNames{"Velocity length", "Gate length", "Octave length",
                                                    "Ratchet length", "Chance length"};

const std::array<ParamSpec, Arpeggiator::Count>& table()
{
    static const auto t = []
    {
        using A = Arpeggiator;
        std::array<ParamSpec, A::Count> p{};
        p[A::Style] = {.id = "style",
                       .name = "Style",
                       .kind = ParamKind::Choice,
                       .max = 9.0f,
                       .choices = styleChoices,
                       .smoothing = none,
                       .description = "The order the notes held are played in."};
        p[A::Sync] = {.id = "sync",
                      .name = "Sync",
                      .kind = ParamKind::Toggle,
                      .max = 1.0f,
                      .def = 1.0f,
                      .choices = offOn,
                      .smoothing = none,
                      .description = "Plays at a note value of the song's tempo, or at a rate of its own."};
        p[A::Rate] = {.id = "rate",
                      .name = "Rate",
                      .kind = ParamKind::Choice,
                      .max = 14.0f,
                      .def = 10.0f,
                      .choices = rateChoices,
                      .smoothing = none,
                      .description = "How often a step plays, as a note value."};
        p[A::Hz] = {.id = "hz",
                    .name = "Rate (free)",
                    .shortName = "Rate",
                    .min = 0.5f,
                    .max = 50.0f,
                    .def = 8.0f,
                    .skewCentre = 6.0f,
                    .unit = Unit::Hertz,
                    .smoothing = none,
                    .description = "How many steps a second, without Sync."};
        p[A::Gate] = {.id = "gate",
                      .name = "Gate",
                      .min = 5.0f,
                      .max = 200.0f,
                      .def = 80.0f,
                      .unit = Unit::Percent,
                      .smoothing = none,
                      .decimals = 0,
                      .description =
                          "How long each step plays, as a share of the step; past 100 % steps overlap."};
        p[A::Swing] = {.id = "swing",
                       .name = "Swing",
                       .max = 75.0f,
                       .unit = Unit::Percent,
                       .smoothing = none,
                       .decimals = 0,
                       .description = "Delays every second step."};
        p[A::Steps] = {.id = "steps",
                       .name = "Steps",
                       .kind = ParamKind::Discrete,
                       .min = 1.0f,
                       .max = 8.0f,
                       .def = 1.0f,
                       .smoothing = none,
                       .decimals = 0,
                       .description = "How many times the pattern plays, each time moved by Distance."};
        p[A::Distance] = {
            .id = "distance",
            .name = "Distance",
            .shortName = "Dist",
            .kind = ParamKind::Discrete,
            .min = -24.0f,
            .max = 24.0f,
            .def = 12.0f,
            .smoothing = none,
            .decimals = 0,
            .description =
                "How far each repeat of the pattern moves: semitones, or steps of the key with In key."};
        p[A::InKey] = {.id = "inKey",
                       .name = "In key",
                       .kind = ParamKind::Toggle,
                       .max = 1.0f,
                       .choices = offOn,
                       .smoothing = none,
                       .description = "Distance and the Octave lane count steps of the key."};
        p[A::Retrigger] = {.id = "retrigger",
                           .name = "Retrigger",
                           .shortName = "Retrig",
                           .kind = ParamKind::Choice,
                           .max = 6.0f,
                           .def = 1.0f,
                           .choices = retriggerChoices,
                           .smoothing = none,
                           .description = "When the pattern starts again from its first step."};
        p[A::Hold] = {.id = "hold",
                      .name = "Hold",
                      .kind = ParamKind::Toggle,
                      .max = 1.0f,
                      .choices = offOn,
                      .smoothing = none,
                      .description =
                          "Keeps playing the notes after they're let go, until new ones are played."};
        p[A::SongChords] = {.id = "songChords",
                            .name = "Song chords",
                            .kind = ParamKind::Toggle,
                            .max = 1.0f,
                            .choices = offOn,
                            .smoothing = none,
                            .description = "With no notes held, plays the chord sounding in the song."};
        p[A::From] = {.id = "from",
                      .name = "From",
                      .kind = ParamKind::Discrete,
                      .max = 127.0f,
                      .def = 60.0f,
                      .unit = Unit::Note,
                      .smoothing = none,
                      .description = "The note the song's chords are played from."};
        for (std::size_t i = 0; i < 5; ++i)
            p[A::VelocityLength + i] = {.id = laneIds[i],
                                        .name = laneNames[i],
                                        .kind = ParamKind::Discrete,
                                        .min = 1.0f,
                                        .max = 16.0f,
                                        .def = 16.0f,
                                        .smoothing = none,
                                        .decimals = 0,
                                        .description = "How many steps the lane has before it starts again."};
        const auto h = harmony::params();
        std::copy(h.begin(), h.end(), p.begin() + A::HarmonyFirst);
        return p;
    }();
    return t;
}

const DeviceInfo arpeggiatorInfo{.id = "arpeggiator",
                                 .name = "Arpeggiator",
                                 .category = "MIDI",
                                 .kind = DeviceKind::MidiEffect,
                                 .buses = midiEffectBuses,
                                 .params = table()};
} // namespace

float Arpeggiator::neutral(int lane) noexcept
{
    switch (lane)
    {
    case OctaveLane:
        return 0.0f;
    case RatchetLane:
        return 1.0f;
    default:
        return 100.0f; // velocity and gate: 100 % of the note's; chance: 100 %
    }
}

Arpeggiator::Arpeggiator() : Device(arpeggiatorInfo)
{
    for (int lane = 0; lane < LaneCount; ++lane)
    {
        lanes_[static_cast<std::size_t>(lane)].fill(neutral(lane));
        playing_[static_cast<std::size_t>(lane)] = -1;
    }
}

void Arpeggiator::process(ProcessContext& context) noexcept
{
    passThrough(context);
}

const DeviceFactory& arpeggiatorFactory()
{
    static const DeviceFactory f{[]() -> const DeviceInfo& { return arpeggiatorInfo; },
                                 []() -> std::unique_ptr<Device> { return std::make_unique<Arpeggiator>(); }};
    return f;
}
} // namespace fd::devices
