#include "Pitch.h"

#include "../../midi-shared/PassThrough.h"

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

Pitch::Pitch() : Device(pitchInfo) {}

void Pitch::process(ProcessContext& context) noexcept
{
    passThrough(context);
}

const DeviceFactory& pitchFactory()
{
    static const DeviceFactory f{[]() -> const DeviceInfo& { return pitchInfo; },
                                 []() -> std::unique_ptr<Device> { return std::make_unique<Pitch>(); }};
    return f;
}
} // namespace fd::devices
