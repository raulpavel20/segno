#include "model/Modulators.h"

#include <fd/dsp/Modulators.h>
#include <span>

namespace fd::model
{
namespace
{
using namespace fd::model::modulator;
namespace mod = fd::dsp::mod;

constexpr std::string_view kLfoModes[] = {"Free", "Retrigger", "Envelope"};
constexpr std::string_view kTriggers[] = {"Notes", "Beat", "None"};
constexpr std::string_view kEnvelopeModes[] = {"Normal", "Loop", "One-shot"};
constexpr std::string_view kAdvances[] = {"Time", "Notes", "Fire"};
constexpr std::string_view kRestarts[] = {"Off", "Notes", "Beat"};
// Every: Sync's note values without Off.
constexpr std::span<const std::string_view> kEvery = std::span(mod::syncValues).subspan(1);

constexpr ParamSpec choice(std::string_view id, std::string_view name,
                           std::span<const std::string_view> choices, float def)
{
    return {.id = id,
            .name = name,
            .shortName = name,
            .kind = ParamKind::Choice,
            .min = 0.0f,
            .max = static_cast<float>(choices.size() - 1),
            .def = def,
            .choices = choices,
            .smoothing = {SmoothingKind::None, 0.0f}};
}

constexpr ParamSpec toggle(std::string_view id, std::string_view name, bool on)
{
    return {.id = id,
            .name = name,
            .shortName = name,
            .kind = ParamKind::Toggle,
            .min = 0.0f,
            .max = 1.0f,
            .def = on ? 1.0f : 0.0f,
            .smoothing = {SmoothingKind::None, 0.0f}};
}

constexpr ParamSpec percent(std::string_view id, std::string_view name, float min, float def)
{
    return {.id = id,
            .name = name,
            .shortName = name,
            .min = min,
            .max = 100.0f,
            .def = def,
            .unit = Unit::Percent,
            .decimals = 0};
}

constexpr ParamSpec time(std::string_view id, std::string_view name, float min, float max, float def,
                         float centre)
{
    return {.id = id,
            .name = name,
            .shortName = name,
            .min = min,
            .max = max,
            .def = def,
            .skewCentre = centre,
            .unit = Unit::Milliseconds,
            .decimals = 0};
}

constexpr ParamSpec rate(float def)
{
    return {.id = "rate",
            .name = "Rate",
            .shortName = "Rate",
            .min = 0.01f,
            .max = 40.0f,
            .def = def,
            .skewCentre = 1.0f,
            .unit = Unit::Hertz,
            .decimals = 2};
}

// The words every modulator shares (modulators.md §10), said the same way everywhere.
constexpr ParamDescription kShared[] = {
    {"rate", "How fast it moves while it isn't synced to the song."},
    {"sync", "Locks it to the song's tempo, in note values."},
    {"smooth", "Rounds off its corners, so steps glide."},
    {"slant", "How long it rises against how long it falls: below zero it rises quickly, above zero it falls "
              "quickly."},
    {"jitter", "How much the time between its changes varies."},
    {"chance", "How likely each firing is to play."},
    {"fire", "Fires it now; anything that moves parameters can press it."},
    {"depth", "How far it moves everything it's routed to, all at once."},
    {"bothways", "On, it moves its targets up and down around where they're set; off, up only (a negative "
                 "amount turns it down)."},
    {"pertarget", "On, each thing it moves has its own copy, moving its own way; off, it moves them all "
                  "together."},
};

template <std::size_t N>
constexpr std::array<ParamSpec, N> described(std::array<ParamSpec, N> specs,
                                             std::span<const ParamDescription> own)
{
    return describe(describe(specs, kShared), own); // its own words over the shared ones
}

constexpr ParamDescription kLfoText[] = {
    {"shape", "The shape it repeats; Drawn plays a shape you draw."},
    {"mode",
     "Free keeps running; Retrigger restarts it with each note; Envelope plays its shape once per note."},
    {"phase", "Where in its shape it starts."},
    {"fade", "How long it takes to fade in after each restart."},
};
constexpr auto kLfo = described(std::array<ParamSpec, Lfo::Count>{{
                                    choice("shape", "Shape", mod::shapeNames, 0.0f),
                                    rate(1.0f),
                                    choice("sync", "Sync", mod::syncValues, 0.0f),
                                    choice("mode", "Mode", kLfoModes, 0.0f),
                                    {.id = "phase",
                                     .name = "Phase",
                                     .shortName = "Phase",
                                     .max = 360.0f,
                                     .unit = Unit::Degrees,
                                     .decimals = 0},
                                    percent("slant", "Slant", -100.0f, 0.0f),
                                    percent("smooth", "Smooth", 0.0f, 0.0f),
                                    time("fade", "Fade In", 0.0f, 10000.0f, 0.0f, 500.0f),
                                    percent("depth", "Depth", 0.0f, 100.0f),
                                    toggle("bothways", "Both Ways", true),
                                    toggle("pertarget", "Per Target", false),
                                }},
                                kLfoText);
static_assert(allDescribed(kLfo));

constexpr ParamDescription kRandomText[] = {
    {"smooth",
     "How much of each step it spends gliding to its new value: 0 jumps (sample and hold), 100 % glides "
     "all the way."},
    {"cyclic", "How often it turns back: 0 picks each value anywhere, 100 % goes up and down in turn."},
    {"freeze", "Stops it where it is; off, it goes on."},
    {"rate", "How often it picks a new value while it isn't synced to the song."},
};
constexpr auto kRandom = described(std::array<ParamSpec, Random::Count>{{
                                       rate(2.0f),
                                       choice("sync", "Sync", mod::syncValues, 0.0f),
                                       percent("smooth", "Smooth", 0.0f, 100.0f),
                                       percent("slant", "Slant", -100.0f, 0.0f),
                                       percent("cyclic", "Cyclic", 0.0f, 0.0f),
                                       percent("jitter", "Jitter", 0.0f, 0.0f),
                                       toggle("freeze", "Freeze", false),
                                       percent("depth", "Depth", 0.0f, 100.0f),
                                       toggle("bothways", "Both Ways", true),
                                       toggle("pertarget", "Per Target", true),
                                   }},
                                   kRandomText);
static_assert(allDescribed(kRandom));

constexpr ParamDescription kEnvelopeText[] = {
    {"trigger", "What fires it: each note played on the track, the beat, or only its Fire."},
    {"every", "With Beat, how often it fires."},
    {"legato", "Fires only on a note that starts while no other is held."},
    {"delay", "How long it waits after each firing before it rises."},
    {"attack", "How long it takes to rise."},
    {"hold", "How long it stays at its peak before it falls."},
    {"decay", "How long it takes to fall to its sustain level."},
    {"sustain", "Its level while the note stays down."},
    {"release", "How long it takes to fall after the note is released."},
    {"acurve", "How the attack rises: below zero it starts slowly, above zero quickly."},
    {"dcurve", "How the decay falls: below zero it starts slowly, above zero quickly."},
    {"rcurve", "How the release falls: below zero it starts slowly, above zero quickly."},
    {"mode",
     "Normal; Loop repeats attack to decay while the note is held; One-shot plays through and ignores the "
     "release."},
    {"repeat", "How many more times it plays after each firing, back to back."},
};
constexpr auto kEnvelope = described(std::array<ParamSpec, Envelope::Count>{{
                                         choice("trigger", "Trigger", kTriggers, 0.0f),
                                         choice("every", "Every", kEvery, 15.0f), // 1 bar
                                         toggle("legato", "Legato", false),
                                         time("delay", "Delay", 0.0f, 10000.0f, 0.0f, 500.0f),
                                         time("attack", "Attack", 0.0f, 20000.0f, 2.0f, 50.0f),
                                         time("hold", "Hold", 0.0f, 10000.0f, 0.0f, 100.0f),
                                         time("decay", "Decay", 1.0f, 20000.0f, 300.0f, 300.0f),
                                         percent("sustain", "Sustain", 0.0f, 0.0f),
                                         time("release", "Release", 1.0f, 20000.0f, 200.0f, 300.0f),
                                         percent("acurve", "A Curve", -100.0f, 30.0f),
                                         percent("dcurve", "D Curve", -100.0f, 60.0f),
                                         percent("rcurve", "R Curve", -100.0f, 60.0f),
                                         choice("mode", "Mode", kEnvelopeModes, 0.0f),
                                         {.id = "repeat",
                                          .name = "Repeat",
                                          .shortName = "Repeat",
                                          .kind = ParamKind::Discrete,
                                          .max = 16.0f,
                                          .smoothing = {SmoothingKind::None, 0.0f},
                                          .decimals = 0},
                                         percent("chance", "Chance", 0.0f, 100.0f),
                                         toggle("fire", "Fire", false),
                                         percent("depth", "Depth", 0.0f, 100.0f),
                                         toggle("pertarget", "Per Target", true),
                                     }},
                                     kEnvelopeText);
static_assert(allDescribed(kEnvelope));

constexpr ParamDescription kFollowerText[] = {
    {"gain", "How loud the sound it hears is taken to be: more makes it move further."},
    {"attack", "How quickly it follows the sound up."},
    {"release", "How quickly it follows the sound down."},
    {"band", "Listens to only part of the sound, between Low and High."},
    {"low", "The lowest sound it hears while Band is on."},
    {"high", "The highest sound it hears while Band is on."},
};
constexpr auto kFollower = described(std::array<ParamSpec, Follower::Count>{{
                                         {.id = "gain",
                                          .name = "Gain",
                                          .shortName = "Gain",
                                          .min = -24.0f,
                                          .max = 24.0f,
                                          .unit = Unit::Decibels},
                                         {.id = "attack",
                                          .name = "Attack",
                                          .shortName = "Attack",
                                          .min = 0.1f,
                                          .max = 500.0f,
                                          .def = 5.0f,
                                          .skewCentre = 20.0f,
                                          .unit = Unit::Milliseconds},
                                         time("release", "Release", 1.0f, 5000.0f, 150.0f, 200.0f),
                                         toggle("band", "Band", false),
                                         {.id = "low",
                                          .name = "Low",
                                          .shortName = "Low",
                                          .min = 20.0f,
                                          .max = 20000.0f,
                                          .def = 100.0f,
                                          .skewCentre = 640.0f,
                                          .unit = Unit::Hertz,
                                          .decimals = 0},
                                         {.id = "high",
                                          .name = "High",
                                          .shortName = "High",
                                          .min = 20.0f,
                                          .max = 20000.0f,
                                          .def = 2000.0f,
                                          .skewCentre = 640.0f,
                                          .unit = Unit::Hertz,
                                          .decimals = 0},
                                         percent("depth", "Depth", 0.0f, 100.0f),
                                     }},
                                     kFollowerText);
static_assert(allDescribed(kFollower));

constexpr ParamDescription kStepsText[] = {
    {"rate", "How fast it steps while it isn't synced to the song."},
    {"sync", "Locks its steps to the song's tempo: one step each note value."},
    {"steps", "How many steps it plays before it repeats."},
    {"advance", "What moves it on a step: time, each note played, or its Fire."},
    {"restart", "What starts it again from its first step: nothing, each note, or each bar."},
    {"smooth", "How much of each step it spends gliding into its value."},
};
constexpr auto kSteps = described(std::array<ParamSpec, Steps::Count>{{
                                      rate(4.0f),
                                      choice("sync", "Sync", mod::syncValues, 5.0f), // 1/16
                                      {.id = "steps",
                                       .name = "Steps",
                                       .shortName = "Steps",
                                       .kind = ParamKind::Discrete,
                                       .min = 1.0f,
                                       .max = static_cast<float>(Steps::maxSteps),
                                       .def = 16.0f,
                                       .smoothing = {SmoothingKind::None, 0.0f},
                                       .decimals = 0},
                                      choice("advance", "Advance", kAdvances, 0.0f),
                                      choice("restart", "Restart", kRestarts, 0.0f),
                                      percent("smooth", "Smooth", 0.0f, 0.0f),
                                      percent("jitter", "Jitter", 0.0f, 0.0f),
                                      toggle("fire", "Fire", false),
                                      percent("depth", "Depth", 0.0f, 100.0f),
                                      toggle("bothways", "Both Ways", true),
                                      toggle("pertarget", "Per Target", true),
                                  }},
                                  kStepsText);
static_assert(allDescribed(kSteps));

// A modulator takes no sound in and gives none out; DeviceKind and buses are the panel kit's.
constexpr BusConfig kNoBuses{.inputChannels = 0, .outputChannels = 0};
constexpr DeviceInfo kInfos[] = {
    {.id = "mod-lfo", .name = "LFO", .category = "Modulators", .buses = kNoBuses, .params = kLfo},
    {.id = "mod-random", .name = "Random", .category = "Modulators", .buses = kNoBuses, .params = kRandom},
    {.id = "mod-envelope",
     .name = "Envelope",
     .category = "Modulators",
     .buses = kNoBuses,
     .params = kEnvelope},
    {.id = "mod-follower",
     .name = "Follower",
     .category = "Modulators",
     .buses = kNoBuses,
     .params = kFollower},
    {.id = "mod-steps", .name = "Steps", .category = "Modulators", .buses = kNoBuses, .params = kSteps},
};
} // namespace

const DeviceInfo& modulatorInfo(ModulatorKind kind) noexcept
{
    return kInfos[static_cast<size_t>(kind)];
}

std::optional<ModulatorKind> modulatorKind(std::string_view typeId) noexcept
{
    for (const auto kind : allModulators)
        if (modulatorInfo(kind).id == typeId)
            return kind;
    return std::nullopt;
}

size_t depthParam(ModulatorKind kind) noexcept
{
    switch (kind)
    {
    case ModulatorKind::Lfo:
        return Lfo::Depth;
    case ModulatorKind::Random:
        return Random::Depth;
    case ModulatorKind::Envelope:
        return Envelope::Depth;
    case ModulatorKind::Follower:
        return Follower::Depth;
    case ModulatorKind::Steps:
        return Steps::Depth;
    }
    return 0;
}

std::optional<size_t> perTargetParam(ModulatorKind kind) noexcept
{
    switch (kind)
    {
    case ModulatorKind::Lfo:
        return Lfo::PerTarget;
    case ModulatorKind::Random:
        return Random::PerTarget;
    case ModulatorKind::Envelope:
        return Envelope::PerTarget;
    case ModulatorKind::Steps:
        return Steps::PerTarget;
    case ModulatorKind::Follower:
        return std::nullopt; // a level is a level
    }
    return std::nullopt;
}

std::optional<size_t> bothWaysParam(ModulatorKind kind) noexcept
{
    switch (kind)
    {
    case ModulatorKind::Lfo:
        return Lfo::BothWays;
    case ModulatorKind::Random:
        return Random::BothWays;
    case ModulatorKind::Steps:
        return Steps::BothWays;
    case ModulatorKind::Envelope:
    case ModulatorKind::Follower:
        return std::nullopt; // they move one way
    }
    return std::nullopt;
}
bool hasKeep(ModulatorKind kind) noexcept
{
    return kind == ModulatorKind::Random || kind == ModulatorKind::Envelope || kind == ModulatorKind::Steps;
}
} // namespace fd::model
