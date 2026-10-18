#include "Melody.h"

#include "../../midi-shared/PassThrough.h"

namespace fd::devices
{
namespace
{
constexpr Smoothing none{SmoothingKind::None, 0.0f};
constexpr std::array<std::string_view, 2> offOn{"Off", "On"};
constexpr std::array<std::string_view, 15> rateChoices{"1/1",  "1/2",   "1/2T", "1/4D",  "1/4",
                                                       "1/4T", "1/8D",  "1/8",  "1/8T",  "1/16D",
                                                       "1/16", "1/16T", "1/32", "1/32T", "1/64"};
constexpr std::array<std::string_view, 5> contourChoices{"Rise", "Fall", "Arch", "Wave", "Any"};
constexpr std::array<std::string_view, 4> phraseChoices{"Free", "AA\xE2\x80\xB2", "AABA", "ABAB"};
constexpr std::array<std::string_view, 2> changeModeChoices{"New", "Shuffle"};
constexpr std::array<std::string_view, 2> playsChoices{"In clips", "Always"};
constexpr std::array<std::string_view, 3> sectionsChoices{"By name", "Each", "One"};
constexpr std::array<std::string_view, 3> notesChoices{"Transpose", "Restart", "Ignore"};
constexpr std::array<std::string_view, 3> answerChoices{"Off", "Answer", "Counter"};
constexpr std::array<std::string_view, 3> fillChoices{"None", "Fill", "Drop out"};
constexpr std::array<std::string_view, 6> laneIds{"pitchLength",  "velocityLength", "gateLength",
                                                  "chanceLength", "ratchetLength",  "slideLength"};
constexpr std::array<std::string_view, 6> laneNames{"Pitch length",  "Velocity length", "Gate length",
                                                    "Chance length", "Ratchet length",  "Slide length"};

const std::array<ParamSpec, Melody::Count>& table()
{
    static const auto t = []
    {
        using M = Melody;
        std::array<ParamSpec, M::Count> p{};
        p[M::Sync] = {.id = "sync",
                      .name = "Sync",
                      .kind = ParamKind::Toggle,
                      .max = 1.0f,
                      .def = 1.0f,
                      .choices = offOn,
                      .smoothing = none,
                      .description = "Steps at a note value of the song's tempo, or at a rate of its own."};
        p[M::Rate] = {.id = "rate",
                      .name = "Rate",
                      .kind = ParamKind::Choice,
                      .max = 14.0f,
                      .def = 10.0f,
                      .choices = rateChoices,
                      .smoothing = none,
                      .description = "How often a step plays, as a note value."};
        p[M::Hz] = {.id = "hz",
                    .name = "Rate (free)",
                    .shortName = "Rate",
                    .min = 0.5f,
                    .max = 50.0f,
                    .def = 8.0f,
                    .skewCentre = 6.0f,
                    .unit = Unit::Hertz,
                    .smoothing = none,
                    .description = "How many steps a second, without Sync."};
        for (std::size_t i = 0; i < 6; ++i)
            p[M::PitchLength + i] = {.id = laneIds[i],
                                     .name = laneNames[i],
                                     .kind = ParamKind::Discrete,
                                     .min = 1.0f,
                                     .max = 64.0f,
                                     .def = 16.0f,
                                     .smoothing = none,
                                     .decimals = 0,
                                     .description = "How many steps the lane has before it starts again."};
        p[M::Density] = {.id = "density",
                         .name = "Density",
                         .max = 100.0f,
                         .def = 60.0f,
                         .unit = Unit::Percent,
                         .smoothing = none,
                         .decimals = 0,
                         .description = "How many steps it writes notes on."};
        p[M::Leaps] = {.id = "leaps",
                       .name = "Leaps",
                       .max = 100.0f,
                       .def = 25.0f,
                       .unit = Unit::Percent,
                       .smoothing = none,
                       .decimals = 0,
                       .description = "Whether the line moves by steps of the key, or leaps."};
        p[M::Contour] = {.id = "contour",
                         .name = "Contour",
                         .kind = ParamKind::Choice,
                         .max = 4.0f,
                         .def = 2.0f,
                         .choices = contourChoices,
                         .smoothing = none,
                         .description = "The line's shape over its length."};
        p[M::Phrase] = {.id = "phrase",
                        .name = "Phrase",
                        .kind = ParamKind::Choice,
                        .max = 3.0f,
                        .def = 1.0f,
                        .choices = phraseChoices,
                        .smoothing = none,
                        .description = "A motif and its repeats."};
        p[M::Lowest] = {.id = "lowest",
                        .name = "Lowest",
                        .kind = ParamKind::Discrete,
                        .max = 127.0f,
                        .def = 60.0f,
                        .unit = Unit::Note,
                        .smoothing = none,
                        .description = "The lowest note it writes."};
        p[M::Highest] = {.id = "highest",
                         .name = "Highest",
                         .kind = ParamKind::Discrete,
                         .max = 127.0f,
                         .def = 84.0f,
                         .unit = Unit::Note,
                         .smoothing = none,
                         .description = "The highest note it writes."};
        p[M::Change] = {.id = "change",
                        .name = "Change",
                        .max = 100.0f,
                        .unit = Unit::Percent,
                        .smoothing = none,
                        .decimals = 0,
                        .description = "How much each lap differs from the last; at 0 the line loops."};
        p[M::ChangeMode] = {.id = "changeMode",
                            .name = "Change mode",
                            .shortName = "Mode",
                            .kind = ParamKind::Choice,
                            .max = 1.0f,
                            .choices = changeModeChoices,
                            .smoothing = none,
                            .description = "Changes bring new notes, or the same notes in new orders."};
        p[M::Climb] = {.id = "climb",
                       .name = "Climb",
                       .kind = ParamKind::Discrete,
                       .min = -7.0f,
                       .max = 7.0f,
                       .smoothing = none,
                       .decimals = 0,
                       .description = "Steps of the key the line moves each lap, back home at each section."};
        p[M::Plays] = {.id = "plays",
                       .name = "Plays",
                       .kind = ParamKind::Choice,
                       .max = 1.0f,
                       .choices = playsChoices,
                       .smoothing = none,
                       .description = "Where its track has clips, or whenever the song plays."};
        p[M::Sections] = {
            .id = "sections",
            .name = "Sections",
            .kind = ParamKind::Choice,
            .max = 2.0f,
            .choices = sectionsChoices,
            .smoothing = none,
            .description =
                "Each section's name its own line, each section its own, or one line through the song."};
        p[M::Notes] = {.id = "notes",
                       .name = "Notes played",
                       .shortName = "Notes",
                       .kind = ParamKind::Choice,
                       .max = 2.0f,
                       .choices = notesChoices,
                       .smoothing = none,
                       .description =
                           "Played notes, and a clip's, transpose it, restart it, or are ignored."};
        p[M::Answer] = {.id = "answer",
                        .name = "Answer",
                        .kind = ParamKind::Choice,
                        .max = 2.0f,
                        .choices = answerChoices,
                        .smoothing = none,
                        .description =
                            "With Listen on another part: plays in its rests, against it, or neither."};
        p[M::Fill] = {.id = "fill",
                      .name = "Before a section",
                      .shortName = "Before",
                      .kind = ParamKind::Choice,
                      .max = 2.0f,
                      .choices = fillChoices,
                      .smoothing = none,
                      .description = "Before a section changes: a fill, a drop-out, or nothing."};
        const auto h = harmony::params();
        std::copy(h.begin(), h.end(), p.begin() + M::HarmonyFirst);
        return p;
    }();
    return t;
}

const DeviceInfo melodyInfo{.id = "melody",
                            .name = "Melody",
                            .category = "MIDI",
                            .kind = DeviceKind::MidiEffect,
                            .buses = midiEffectBuses,
                            .params = table()};
} // namespace

float Melody::neutral(int lane) noexcept
{
    switch (lane)
    {
    case PitchLane:
    case SlideLane:
        return 0.0f; // the chord's root; no slide
    case RatchetLane:
        return 1.0f;
    default:
        return 100.0f; // velocity, gate, chance: 100 %
    }
}

Melody::Melody() : Device(melodyInfo)
{
    for (int lane = 0; lane < LaneCount; ++lane)
    {
        lanes_[static_cast<std::size_t>(lane)].fill(neutral(lane));
        playing_[static_cast<std::size_t>(lane)] = -1;
    }
}

void Melody::process(ProcessContext& context) noexcept
{
    passThrough(context);
}

const DeviceFactory& melodyFactory()
{
    static const DeviceFactory f{[]() -> const DeviceInfo& { return melodyInfo; },
                                 []() -> std::unique_ptr<Device> { return std::make_unique<Melody>(); }};
    return f;
}
} // namespace fd::devices
