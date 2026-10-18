#include "Chord.h"

#include "../../midi-shared/PassThrough.h"

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
        p[Chord::InKey] = {.id = "inKey",
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
                                          "the bottom, or wherever leads smoothest."};
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

Chord::Chord() : Device(chordInfo) {}

void Chord::process(ProcessContext& context) noexcept
{
    passThrough(context);
}

const DeviceFactory& chordFactory()
{
    static const DeviceFactory f{[]() -> const DeviceInfo& { return chordInfo; },
                                 []() -> std::unique_ptr<Device> { return std::make_unique<Chord>(); }};
    return f;
}
} // namespace fd::devices
