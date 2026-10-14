#include "WavetableSynth.h"

#include "TableFiles.h"
#include "fd/dsp/Math.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <nlohmann/json.hpp>
#include <numbers>
#include <optional>
#include <tuple>

namespace fd::devices
{
namespace
{
using W = WavetableSynth;

constexpr std::string_view kWarps[] = {"Off",
                                       "Sync",
                                       "Window Sync",
                                       "Bend +",
                                       "Bend −",
                                       "Bend ±",
                                       "Squeeze",
                                       "Pulse",
                                       "Asym +",
                                       "Asym −",
                                       "Mirror",
                                       "Flip",
                                       "Quantize",
                                       "Fold",
                                       "Remap",
                                       "FM",
                                       "PM",
                                       "RM",
                                       "AM",
                                       "Formant Shift",
                                       "Harmonic Stretch",
                                       "Inharmonic Stretch",
                                       "Smear",
                                       "Spectral Low Pass",
                                       "Spectral High Pass",
                                       "Phase Disperse",
                                       "Frame Skew",
                                       "Random Harmonics",
                                       "Shepard"};
constexpr std::string_view kWarpSources[] = {"Osc 1", "Osc 2", "Osc 3", "Sub", "Noise"};
constexpr std::string_view kUnisonModes[] = {"Even", "Super", "Exponential", "Random"};
constexpr std::string_view kStacks[] = {"Off",   "Octave", "2 Octaves", "Fifth", "Fifth + Octave",
                                        "Major", "Minor",  "Key Chord"};
constexpr std::string_view kRoutes[] = {"Filter 1", "Filter 2", "Both", "None"};
constexpr std::string_view kSubShapes[] = {"Sine", "Triangle", "Saw", "Square"};
constexpr std::string_view kNoiseTypes[] = {"White",   "Pink",   "Brown",   "Digital",
                                            "Crackle", "Breath", "Metallic"};
constexpr std::string_view kFilterTypes[] = {"LP 12",  "LP 24",  "Ladder",  "Diode", "HP 12",
                                             "HP 24",  "BP 12",  "BP 24",   "Notch", "Peak",
                                             "Comb +", "Comb −", "Formant", "Phaser"};
constexpr std::string_view kEnvModes[] = {"Normal", "Loop", "One-shot"};
constexpr std::string_view kSync[] = {"Off",   "1/64", "1/32T", "1/32",   "1/16T",  "1/16",  "1/8T",
                                      "1/16D", "1/8",  "1/4T",  "1/8D",   "1/4",    "1/2T",  "1/4D",
                                      "1/2",   "1/2D", "1 bar", "2 bars", "4 bars", "8 bars"};
// Each value's length in beats (quarter notes); bars from the time signature (negative: bars).
constexpr double kSyncBeats[] = {0.0,     1.0 / 16, 1.0 / 12, 1.0 / 8, 1.0 / 6, 1.0 / 4, 1.0 / 3,
                                 3.0 / 8, 1.0 / 2,  2.0 / 3,  3.0 / 4, 1.0,     4.0 / 3, 3.0 / 2,
                                 2.0,     3.0,      -1.0,     -2.0,    -4.0,    -8.0};
constexpr std::string_view kLfoModes[] = {"Free", "Retrigger", "Envelope"};
constexpr std::string_view kRandomModes[] = {"Steps", "Smooth", "Chaos", "Per Note"};
constexpr std::string_view kVoiceModes[] = {"Poly", "Mono", "Legato"};
constexpr std::string_view kGlideModes[] = {"Always", "Legato"};
constexpr std::string_view kQualities[] = {"Eco", "Normal", "High"};
constexpr std::string_view kNamedShapes[] = {"sine", "triangle", "saw-up", "saw-down", "square", "steps"};

constexpr auto kAutoInf = ParamFlags::Automatable | ParamFlags::MinusInfinityAtMin;

/// The parameter table, generated once (families like "osc2.warp1.amt" need stored strings).
struct Tables
{
    std::array<std::string, W::NumParams> ids, names, shortNames;
    std::array<std::string_view, W::NumParams> descriptions{};
    std::array<ParamSpec, W::NumParams> specs{};
    std::vector<std::string> sourceNames, targetNames;
    std::vector<std::string_view> sourceViews, targetViews;
    DeviceInfo info{};

    void set(size_t i, std::string id, std::string name, std::string shortName, std::string_view description,
             ParamSpec spec)
    {
        ids[i] = std::move(id);
        names[i] = std::move(name);
        shortNames[i] = std::move(shortName);
        descriptions[i] = description;
        specs[i] = spec;
    }

    static ParamSpec percent(float def, float min = 0.0f)
    {
        return {.min = min, .max = 100.0f, .def = def, .unit = Unit::Percent, .decimals = 0};
    }
    static ParamSpec levelDb(float def)
    {
        return {.min = -70.0f,
                .max = 6.0f,
                .def = def,
                .skewCentre = -12.0f,
                .unit = Unit::Decibels,
                .flags = kAutoInf};
    }
    static ParamSpec choice(std::span<const std::string_view> choices, float def,
                            std::uint32_t flags = ParamFlags::Automatable)
    {
        return {.kind = ParamKind::Choice,
                .min = 0.0f,
                .max = static_cast<float>(choices.size() - 1),
                .def = def,
                .choices = choices,
                .flags = flags};
    }
    static ParamSpec discrete(float min, float max, float def, Unit unit = Unit::None)
    {
        return {.kind = ParamKind::Discrete, .min = min, .max = max, .def = def, .unit = unit};
    }
    static ParamSpec toggle(float def)
    {
        return {.kind = ParamKind::Toggle, .min = 0.0f, .max = 1.0f, .def = def};
    }
    static ParamSpec ms(float min, float max, float def, float centre)
    {
        return {.min = min,
                .max = max,
                .def = def,
                .skewCentre = centre,
                .unit = Unit::Milliseconds,
                .decimals = 0};
    }
    static ParamSpec pan() { return {.min = -1.0f, .max = 1.0f, .def = 0.0f, .unit = Unit::Pan}; }

    Tables()
    {
        // The modulation lists.
        sourceNames = {"None", "Amp Env", "Filter Env", "Env 3", "Env 4"};
        for (int n = 1; n <= W::numLfos; ++n)
            sourceNames.push_back("LFO " + std::to_string(n));
        for (int n = 1; n <= W::numRandoms; ++n)
            sourceNames.push_back("Random " + std::to_string(n));
        for (int n = 1; n <= W::numMacros; ++n)
            sourceNames.push_back("Macro " + std::to_string(n));
        for (const char* s : {"Velocity", "Release Velocity", "Note", "Pressure", "Slide", "Pitch Bend",
                              "Mod Wheel", "Note Random"})
            sourceNames.emplace_back(s);
        targetNames = {"None", "Pitch"};
        constexpr const char* oscTargets[] = {"Position",        "Pitch",      "Fine",   "Level", "Pan",
                                              "Warp 1",          "Warp 2",     "Detune", "Blend", "Width",
                                              "Position Spread", "Warp Spread"};
        for (int n = 1; n <= W::numOscs; ++n)
            for (const char* t : oscTargets)
                targetNames.push_back("Osc " + std::to_string(n) + " " + t);
        for (const char* t : {"Sub Level", "Sub Pan", "Noise Level", "Noise Color", "Noise Pan"})
            targetNames.emplace_back(t);
        for (int n = 1; n <= W::numFilters; ++n)
            for (const char* t : {"Cutoff", "Resonance", "Drive", "Shape"})
                targetNames.push_back("Filter " + std::to_string(n) + " " + t);
        targetNames.emplace_back("Filter Routing");
        targetNames.emplace_back("Amp");
        targetNames.emplace_back("Pan");
        for (int n = 1; n <= W::numLfos; ++n)
            targetNames.push_back("LFO " + std::to_string(n) + " Rate");
        for (int n = 1; n <= W::numRandoms; ++n)
            targetNames.push_back("Random " + std::to_string(n) + " Rate");
        for (const auto& s : sourceNames)
            sourceViews.emplace_back(s);
        for (const auto& t : targetNames)
            targetViews.emplace_back(t);

        // Oscillators.
        for (int n = 0; n < W::numOscs; ++n)
        {
            const std::string id = "osc" + std::to_string(n + 1) + ".";
            const std::string name = "Osc " + std::to_string(n + 1) + " ";
            const auto at = [n](W::OscParam p) { return W::osc(n, p); };
            set(at(W::OscOn), id + "on", name + "On", "On", "Turns the oscillator on or off.",
                toggle(n == 0 ? 1.0f : 0.0f));
            set(at(W::OscPos), id + "pos", name + "Position", "Pos",
                "Where in its wavetable the oscillator plays: moving it morphs through the table's waves.",
                percent(0.0f));
            set(at(W::OscOct), id + "oct", name + "Octave", "Oct", "Shifts the oscillator by whole octaves.",
                discrete(-4.0f, 4.0f, 0.0f));
            set(at(W::OscSemi), id + "semi", name + "Semi", "Semi",
                "Shifts the oscillator by semitones, for intervals.",
                discrete(-12.0f, 12.0f, 0.0f, Unit::Semitones));
            set(at(W::OscFine), id + "fine", name + "Fine", "Fine",
                "Tunes the oscillator slightly up or down.",
                {.min = -100.0f, .max = 100.0f, .def = 0.0f, .unit = Unit::Cents, .decimals = 0});
            set(at(W::OscLevel), id + "level", name + "Level", "Level", "The oscillator's level.",
                levelDb(n == 0 ? 0.0f : -6.0f));
            set(at(W::OscPan), id + "pan", name + "Pan", "Pan", "Places the oscillator left or right.",
                pan());
            set(at(W::OscPhase), id + "phase", name + "Phase", "Phase",
                "Where in its wave the oscillator starts each note.",
                {.min = 0.0f, .max = 360.0f, .def = 0.0f, .unit = Unit::Degrees, .decimals = 0});
            set(at(W::OscRand), id + "rand", name + "Phase Random", "Random",
                "How much each note's starting point varies: at 100 % the oscillator seems to run freely.",
                percent(0.0f));
            for (int k = 0; k < 2; ++k)
            {
                const std::string warp = "warp" + std::to_string(k + 1);
                const std::string warpName = "Warp " + std::to_string(k + 1);
                const auto base = k == 0 ? W::OscWarp1 : W::OscWarp2;
                set(at(base), id + warp, name + warpName, warpName,
                    "A way to reshape the oscillator's wave: sync, bend, fold, frequency modulation and "
                    "more.",
                    choice(kWarps, 0.0f));
                set(at(static_cast<W::OscParam>(base + 1)), id + warp + ".amt", name + warpName + " Amount",
                    "Amount", "How strongly the warp reshapes the wave.", percent(0.0f));
                set(at(static_cast<W::OscParam>(base + 2)), id + warp + ".src", name + warpName + " Source",
                    "From", "What modulates the wave in the FM, PM, RM and AM warps.",
                    choice(kWarpSources, static_cast<float>((n + 1) % W::numOscs)));
            }
            set(at(W::OscUni), id + "uni", name + "Unison", "Unison",
                "How many detuned copies of the oscillator play together.", discrete(1.0f, 16.0f, 1.0f));
            set(at(W::OscDetune), id + "detune", name + "Detune", "Detune",
                "How far apart the unison copies are tuned.", percent(25.0f));
            set(at(W::OscUniMode), id + "unimode", name + "Unison Mode", "Mode",
                "How the copies spread in pitch: evenly, bunched in the middle, wider at the edges, or at "
                "random.",
                choice(kUnisonModes, 1.0f));
            set(at(W::OscStack), id + "stack", name + "Stack", "Stack",
                "Tunes the unison copies to intervals too: octaves, a fifth, or a chord, in the project's "
                "key if you like.",
                choice(kStacks, 0.0f));
            set(at(W::OscBlend), id + "blend", name + "Blend", "Blend",
                "The balance between the middle copy and the detuned ones.", percent(75.0f));
            set(at(W::OscWidth), id + "width", name + "Width", "Width",
                "How widely the unison copies spread across the stereo field.", percent(80.0f));
            set(at(W::OscSpread), id + "spread", name + "Position Spread", "Pos Spread",
                "Gives each unison copy its own place in the wavetable, for movement and width.",
                percent(0.0f));
            set(at(W::OscWarpSpread), id + "warpspread", name + "Warp Spread", "Warp Spread",
                "Gives each unison copy its own warp amount.", percent(0.0f));
            set(at(W::OscRoute), id + "route", name + "Filter", "Filter",
                "Which filter the oscillator plays through: the first, the second, both, or neither.",
                choice(kRoutes, 0.0f));
        }
        // Sub and noise.
        set(W::SubOn, "sub.on", "Sub On", "On", "Turns the sub oscillator on or off.", toggle(0.0f));
        set(W::SubOct, "sub.oct", "Sub Octave", "Oct", "How far below the note the sub plays.",
            discrete(-2.0f, 0.0f, -1.0f));
        set(W::SubShape, "sub.shape", "Sub Shape", "Shape", "The sub's wave.", choice(kSubShapes, 0.0f));
        set(W::SubLevel, "sub.level", "Sub Level", "Level", "The sub's level.", levelDb(-6.0f));
        set(W::SubPan, "sub.pan", "Sub Pan", "Pan", "Places the sub left or right.", pan());
        set(W::SubRoute, "sub.route", "Sub Filter", "Filter",
            "Which filter the sub plays through: the first, the second, both, or neither.",
            choice(kRoutes, 3.0f));
        set(W::NoiseOn, "noise.on", "Noise On", "On", "Turns the noise source on or off.", toggle(0.0f));
        set(W::NoiseType, "noise.type", "Noise Type", "Type",
            "The kind of noise: from bright white to deep brown, crackle, breath or metallic.",
            choice(kNoiseTypes, 0.0f));
        set(W::NoiseColor, "noise.color", "Noise Color", "Color", "Tilts the noise darker or brighter.",
            percent(0.0f, -100.0f));
        set(W::NoiseLevel, "noise.level", "Noise Level", "Level", "The noise's level.", levelDb(-12.0f));
        set(W::NoisePan, "noise.pan", "Noise Pan", "Pan", "Places the noise left or right.", pan());
        set(W::NoiseRoute, "noise.route", "Noise Filter", "Filter",
            "Which filter the noise plays through: the first, the second, both, or neither.",
            choice(kRoutes, 0.0f));
        // Filters.
        for (int n = 0; n < W::numFilters; ++n)
        {
            const std::string id = "filter" + std::to_string(n + 1) + ".";
            const std::string name = "Filter " + std::to_string(n + 1) + " ";
            const auto at = [n](W::FilterParam p) { return W::filter(n, p); };
            set(at(W::FilterOn), id + "on", name + "On", "On", "Turns the filter on or off.",
                toggle(n == 0 ? 1.0f : 0.0f));
            set(at(W::FilterType), id + "type", name + "Type", "Type",
                "The filter's kind: low, high or band pass, notch, peak, ladder or diode, comb, formant or "
                "phaser.",
                choice(kFilterTypes, n == 0 ? 1.0f : 4.0f));
            set(at(W::FilterCutoff), id + "cutoff", name + "Cutoff", "Cutoff",
                "Where the filter acts: for a low pass, lower is darker.",
                {.min = 20.0f,
                 .max = 20000.0f,
                 .def = n == 0 ? 2000.0f : 100.0f,
                 .skewCentre = 632.0f,
                 .unit = Unit::Hertz,
                 .smoothing = {SmoothingKind::Multiplicative, 20.0f},
                 .decimals = 0});
            set(at(W::FilterRes), id + "res", name + "Resonance", "Res",
                "Emphasis at the cutoff; near the top the ladder and diode filters start to sing on their "
                "own.",
                percent(20.0f));
            set(at(W::FilterDrive), id + "drive", name + "Drive", "Drive",
                "Pushes the filter harder, for warmth and grit.", percent(0.0f));
            set(at(W::FilterShape), id + "shape", name + "Shape", "Shape",
                "The type's own setting: the formant's vowel, the comb's damping, the phaser's spread.",
                percent(50.0f));
            set(at(W::FilterKey), id + "key", name + "Key Track", "Key",
                "How much the cutoff follows the note played: at 100 % it rises an octave per octave.",
                percent(50.0f));
            set(at(W::FilterEnv), id + "env", name + "Env", "Env",
                "How far the filter envelope moves the cutoff, up or down.",
                percent(n == 0 ? 30.0f : 0.0f, -100.0f));
        }
        set(W::FiltersRouting, "filters.routing", "Filter Routing", "Routing",
            "Filter 1 into filter 2 at 0 %, the two side by side at 100 %, or anywhere between.",
            percent(0.0f));
        set(W::AmpVelocity, "amp.vel", "Velocity", "Velocity", "How much harder playing makes notes louder.",
            {.min = 0.0f,
             .max = 100.0f,
             .def = 50.0f,
             .unit = Unit::Percent,
             .smoothing = {SmoothingKind::None, 0.0f},
             .decimals = 0});
        // Envelopes.
        constexpr const char* envNames[W::numEnvs] = {"Amp ", "Filter Env ", "Env 3 ", "Env 4 "};
        for (int n = 0; n < W::numEnvs; ++n)
        {
            const std::string id = "env" + std::to_string(n + 1) + ".";
            const std::string name = envNames[n];
            const auto at = [n](W::EnvParam p) { return W::env(n, p); };
            set(at(W::EnvAttack), id + "a", name + "Attack", "Attack", "How long the envelope takes to rise.",
                ms(0.0f, 20000.0f, 2.0f, 50.0f));
            set(at(W::EnvHold), id + "h", name + "Hold", "Hold",
                "How long the envelope stays at its peak before it falls.", ms(0.0f, 10000.0f, 0.0f, 100.0f));
            set(at(W::EnvDecay), id + "d", name + "Decay", "Decay",
                "How long the envelope takes to fall to its sustain level.",
                ms(1.0f, 20000.0f, n == 1 ? 400.0f : 300.0f, 300.0f));
            set(at(W::EnvSustain), id + "s", name + "Sustain", "Sustain",
                "The envelope's level while the key stays down.", percent(n == 0 ? 80.0f : 0.0f));
            set(at(W::EnvRelease), id + "r", name + "Release", "Release",
                "How long the envelope takes to fall after the key is released.",
                ms(1.0f, 20000.0f, n == 0 ? 200.0f : 300.0f, 300.0f));
            set(at(W::EnvAttackCurve), id + "acurve", name + "Attack Curve", "A Curve",
                "How the attack rises: below zero it starts slowly, above zero quickly.",
                percent(30.0f, -100.0f));
            set(at(W::EnvDecayCurve), id + "dcurve", name + "Decay Curve", "D Curve",
                "How the decay falls: below zero it starts slowly, above zero quickly.",
                percent(60.0f, -100.0f));
            set(at(W::EnvReleaseCurve), id + "rcurve", name + "Release Curve", "R Curve",
                "How the release falls: below zero it starts slowly, above zero quickly.",
                percent(60.0f, -100.0f));
            set(at(W::EnvMode), id + "mode", name + "Mode", "Mode",
                "Normal; Loop repeats attack to decay while the key is held; One-shot plays through and "
                "ignores the release.",
                choice(kEnvModes, 0.0f));
        }
        // LFOs.
        for (int n = 0; n < W::numLfos; ++n)
        {
            const std::string id = "lfo" + std::to_string(n + 1) + ".";
            const std::string name = "LFO " + std::to_string(n + 1) + " ";
            const auto at = [n](W::LfoParam p) { return W::lfo(n, p); };
            set(at(W::LfoRate), id + "rate", name + "Rate", "Rate",
                "How fast the LFO moves when it isn't synced to the tempo.",
                {.min = 0.01f,
                 .max = 100.0f,
                 .def = 2.0f,
                 .skewCentre = 1.0f,
                 .unit = Unit::Hertz,
                 .smoothing = {SmoothingKind::Multiplicative, 20.0f},
                 .decimals = 2});
            set(at(W::LfoSync), id + "sync", name + "Sync", "Sync",
                "Locks the LFO to the song's tempo, in note values.", choice(kSync, 0.0f));
            set(at(W::LfoMode), id + "mode", name + "Mode", "Mode",
                "Free keeps running; Retrigger restarts it with each note; Envelope plays its shape once per "
                "note.",
                choice(kLfoModes, 1.0f));
            set(at(W::LfoPhase), id + "phase", name + "Phase", "Phase", "Where in its shape the LFO starts.",
                {.min = 0.0f, .max = 360.0f, .def = 0.0f, .unit = Unit::Degrees, .decimals = 0});
            set(at(W::LfoSmooth), id + "smooth", name + "Smooth", "Smooth",
                "Rounds off the shape's corners, so steps glide.", percent(0.0f));
            set(at(W::LfoFade), id + "fade", name + "Fade In", "Fade In",
                "How long the LFO takes to fade in after each note starts.",
                ms(0.0f, 10000.0f, 0.0f, 500.0f));
            set(at(W::LfoPoly), id + "poly", name + "Per Note", "Per Note",
                "On, each note has its own LFO; off, one LFO moves every note together.", toggle(1.0f));
        }
        for (int n = 0; n < W::numRandoms; ++n)
        {
            const std::string id = "rnd" + std::to_string(n + 1) + ".";
            const std::string name = "Random " + std::to_string(n + 1) + " ";
            const auto at = [n](W::RandomParam p) { return W::random(n, p); };
            set(at(W::RandomMode), id + "mode", name + "Mode", "Mode",
                "Steps holds each new value; Smooth glides between them; Chaos wanders like a pendulum; Per "
                "Note picks one value for each note.",
                choice(kRandomModes, 1.0f));
            set(at(W::RandomRate), id + "rate", name + "Rate", "Rate",
                "How often a new value comes when it isn't synced to the tempo.",
                {.min = 0.01f,
                 .max = 100.0f,
                 .def = 4.0f,
                 .skewCentre = 1.0f,
                 .unit = Unit::Hertz,
                 .smoothing = {SmoothingKind::Multiplicative, 20.0f},
                 .decimals = 2});
            set(at(W::RandomSync), id + "sync", name + "Sync", "Sync",
                "Locks the new values to the song's tempo, in note values.", choice(kSync, 0.0f));
            set(at(W::RandomPoly), id + "poly", name + "Per Note", "Per Note",
                "On, each note has its own values; off, every note follows the same ones.", toggle(1.0f));
        }
        for (int n = 0; n < W::numMacros; ++n)
            set(W::macro(n), "macro" + std::to_string(n + 1), "Macro " + std::to_string(n + 1),
                "Macro " + std::to_string(n + 1),
                "A knob of your own: route it to any settings to move them together.", percent(0.0f));
        for (int n = 0; n < W::numRoutes; ++n)
        {
            const std::string id = "mod" + std::to_string(n + 1) + ".";
            const std::string name = "Mod " + std::to_string(n + 1) + " ";
            set(W::route(n, W::RouteSource), id + "src", name + "Source", "Source",
                "What drives this route: an envelope, an LFO, a macro, or how the note is played.",
                choice(sourceViews, 0.0f));
            set(W::route(n, W::RouteTarget), id + "dst", name + "Target", "Target",
                "What this route changes.", choice(targetViews, 0.0f));
            set(W::route(n, W::RouteAmount), id + "amt", name + "Amount", "Amount",
                "How much, and in which direction, this route moves its target.", percent(0.0f, -100.0f));
            set(W::route(n, W::RouteCurve), id + "curve", name + "Curve", "Curve",
                "Bends the route's response: below zero small moves of the source count for less, above zero "
                "for more.",
                percent(0.0f, -100.0f));
            set(W::route(n, W::RouteScale), id + "aux", name + "Scale", "Scale",
                "Another source that scales this route's amount, such as the mod wheel or velocity.",
                choice(sourceViews, 0.0f));
        }
        set(W::VoiceMode, "voice.mode", "Voice Mode", "Mode",
            "Poly plays chords; Mono plays one note at a time; Legato is mono without restarting the "
            "envelopes when notes overlap.",
            choice(kVoiceModes, 0.0f));
        set(W::VoiceCount, "voice.count", "Voices", "Voices", "How many notes can sound at once.",
            discrete(1.0f, 32.0f, 8.0f));
        set(W::VoiceGlide, "voice.glide", "Glide", "Glide",
            "How long the pitch takes to slide from one note to the next.", ms(0.0f, 2000.0f, 0.0f, 200.0f));
        set(W::VoiceGlideMode, "voice.glidemode", "Glide Mode", "Glide Mode",
            "Always slides; Legato slides only between overlapping notes.", choice(kGlideModes, 0.0f));
        set(W::VoiceBend, "voice.bend", "Bend Range", "Bend",
            "How far the pitch bend wheel bends, in semitones.",
            discrete(0.0f, 48.0f, 2.0f, Unit::Semitones));
        set(W::VoiceDrift, "voice.drift", "Drift", "Drift",
            "Small random wandering of each voice's pitch and filter, like an analog synth.", percent(10.0f));
        set(W::KeySnap, "key.snap", "Snap to Key", "Snap",
            "Makes pitch modulation land on notes of the project's key.", toggle(0.0f));
        set(W::OutVolume, "out.volume", "Volume", "Volume", "The synth's output level.", levelDb(-6.0f));
        set(W::Quality, "quality", "Quality", "Quality",
            "Eco saves CPU; High is cleanest for frequency modulation and the lowest notes.",
            choice(kQualities, 1.0f, ParamFlags::ChangesLatency));

        for (size_t i = 0; i < specs.size(); ++i)
        {
            specs[i].id = ids[i];
            specs[i].name = names[i];
            specs[i].shortName = shortNames[i];
            specs[i].description = descriptions[i];
        }
        info = DeviceInfo{.id = "wavetable",
                          .name = "Wavetable",
                          .category = "Instruments",
                          .kind = DeviceKind::Instrument,
                          .buses = {.inputChannels = 0, .outputChannels = 2, .acceptsMidi = true},
                          .params = specs,
                          .stateVersion = 1};
    }
};

const Tables& tables()
{
    static const Tables t;
    return t;
}

double hzForPitch(double pitch) noexcept
{
    return 440.0 * std::exp2((pitch - 69.0) / 12.0);
}

std::uint32_t hash(std::uint32_t x) noexcept
{
    x ^= x >> 16;
    x *= 0x7feb352dU;
    x ^= x >> 15;
    x *= 0x846ca68bU;
    x ^= x >> 16;
    return x == 0 ? 1 : x;
}

/// Balance: the far side turned down, the near one left at unity.
std::pair<double, double> balance(double pan) noexcept
{
    pan = std::clamp(pan, -1.0, 1.0);
    return {pan > 0.0 ? 1.0 - pan : 1.0, pan < 0.0 ? 1.0 + pan : 1.0};
}

} // namespace

// ---- curves and the modulation lists --------------------------------------------------------

std::span<const std::string_view> WavetableSynth::namedShapes() noexcept
{
    return kNamedShapes;
}

dsp::CurveTable WavetableSynth::Curve::table() const
{
    dsp::CurveTable t{};
    const auto n = t.size() - 1;
    for (size_t i = 0; i <= n; ++i)
    {
        const double x = static_cast<double>(i) / n;
        double y = 0.0;
        if (!points.empty())
        {
            if (x <= points.front()[0])
                y = points.front()[1];
            else if (x >= points.back()[0])
                y = points.back()[1];
            else
                for (size_t k = 0; k + 1 < points.size(); ++k)
                    if (x >= points[k][0] && x <= points[k + 1][0])
                    {
                        const double span =
                            std::max(1e-9, static_cast<double>(points[k + 1][0] - points[k][0]));
                        const double bent =
                            dsp::CurvedEnvelope::shape((x - points[k][0]) / span, points[k][2]);
                        y = points[k][1] + (points[k + 1][1] - points[k][1]) * bent;
                        break;
                    }
        }
        else if (shape == "triangle") // from the middle, up first, as the sine
            y = 0.5 + 0.5 * (1.0 - 4.0 * std::abs(std::fmod(x + 0.25, 1.0) - 0.5));
        else if (shape == "saw-up")
            y = x;
        else if (shape == "saw-down")
            y = 1.0 - x;
        else if (shape == "square")
            y = x < 0.5 ? 1.0 : 0.0;
        else if (shape == "steps")
            y = std::min(3.0, std::floor(x * 4.0)) / 3.0;
        else // sine
            y = 0.5 + 0.5 * std::sin(dsp::twoPi * x);
        t[i] = static_cast<float>(std::clamp(y, 0.0, 1.0));
    }
    return t;
}

std::optional<size_t> WavetableSynth::targetParam(int target) noexcept
{
    if (target >= TargetOsc && target < TargetSubLevel)
    {
        const int n = (target - TargetOsc) / OscTargetCount;
        constexpr OscParam params[OscTargetCount] = {OscPos,   OscParamCount, OscFine,     OscLevel,
                                                     OscPan,   OscWarp1Amt,   OscWarp2Amt, OscDetune,
                                                     OscBlend, OscWidth,      OscSpread,   OscWarpSpread};
        const auto p = params[(target - TargetOsc) % OscTargetCount];
        return p == OscParamCount ? std::nullopt : std::optional<size_t>(osc(n, p));
    }
    switch (target)
    {
    case TargetSubLevel:
        return SubLevel;
    case TargetSubPan:
        return SubPan;
    case TargetNoiseLevel:
        return NoiseLevel;
    case TargetNoiseColor:
        return NoiseColor;
    case TargetNoisePan:
        return NoisePan;
    case TargetRouting:
        return FiltersRouting;
    default:
        break;
    }
    if (target >= TargetFilter && target < TargetRouting)
    {
        const int n = (target - TargetFilter) / FilterTargetCount;
        constexpr FilterParam params[FilterTargetCount] = {FilterCutoff, FilterRes, FilterDrive, FilterShape};
        return filter(n, params[(target - TargetFilter) % FilterTargetCount]);
    }
    if (target >= TargetLfoRate && target < TargetRandomRate)
        return lfo(target - TargetLfoRate, LfoRate);
    if (target >= TargetRandomRate && target < NumTargets)
        return random(target - TargetRandomRate, RandomRate);
    return std::nullopt;
}

std::optional<int> WavetableSynth::targetOf(size_t param) noexcept
{
    for (int t = 1; t < NumTargets; ++t)
        if (targetParam(t) == param)
            return t;
    return std::nullopt;
}

int WavetableSynth::latencyFor(int /*quality*/) noexcept
{
    return 0;
}

// ---- lifetime --------------------------------------------------------------------------------

WavetableSynth::WavetableSynth() : Device(tables().info)
{
    for (auto& t : tables_)
        t.table = wavetable::factoryTable(t.id);
    for (auto& r : remaps_)
        r.shape = "saw-up"; // a straight line: no change until drawn
    publishAssets();
}

void WavetableSynth::publishAssets()
{
    auto a = std::make_shared<Assets>();
    for (int n = 0; n < numOscs; ++n)
        a->tables[static_cast<size_t>(n)] = tables_[static_cast<size_t>(n)].table;
    for (int n = 0; n < numLfos; ++n)
        a->lfoShapes[static_cast<size_t>(n)] = lfoShapes_[static_cast<size_t>(n)].table();
    for (size_t r = 0; r < remaps_.size(); ++r)
        a->remaps[r] = remaps_[r].table();
    assets_.publish(std::move(a));
}

void WavetableSynth::prepare(const ProcessSpec& spec)
{
    sampleRate_ = spec.sampleRate > 0.0 ? spec.sampleRate : 48000.0;
    renderer_.prepare();
    for (auto& v : voice_)
    {
        v.sub.prepare(sampleRate_);
        for (auto& pair : v.filters)
            for (auto& f : pair)
                f.prepare(sampleRate_);
        for (auto& e : v.envs)
            e.prepare(sampleRate_);
    }
    spareRight_.assign(static_cast<size_t>(std::max(1, spec.maxBlockSize)), 0.0f);
    delayL_.assign(64, 0.0f);
    delayR_.assign(64, 0.0f);
    reset();
}

void WavetableSynth::reset() noexcept
{
    grid_.reset();
    voices_.reset();
    for (auto& v : voice_)
    {
        v.sounding = false;
        for (auto& e : v.envs)
            e.reset();
        for (auto& pair : v.filters)
            for (auto& f : pair)
                f.reset();
    }
    for (auto& l : lfos_)
        l.reset();
    for (int n = 0; n < numRandoms; ++n)
        randoms_[static_cast<size_t>(n)].reset(static_cast<std::uint32_t>(1000 + n));
    lfoValue_ = {};
    randomValue_ = {};
    bend_ = {};
    pressure_ = {};
    slide_ = {};
    modWheel_ = 0.0f;
    notes_ = 0;
    newest_ = -1;
    lastPitch_ = 60.0;
    smoothedPrimed_ = false;
    std::fill(delayL_.begin(), delayL_.end(), 0.0f);
    std::fill(delayR_.begin(), delayR_.end(), 0.0f);
    delayWrite_ = 0;
    quality_ = -1;
    applyQuality();
    volume_.reset(dsp::dbToGain(params().get(OutVolume), -70.0f));
}

double WavetableSynth::tailSeconds() const noexcept
{
    return params().get(env(0, EnvRelease)) * 0.001 + 0.05;
}

int WavetableSynth::latencySamples() const noexcept
{
    return latencyFor(params().getInt(Quality));
}

void WavetableSynth::applyQuality() noexcept
{
    const int q = std::clamp(params().getInt(Quality), 0, 2);
    if (q == quality_)
        return;
    quality_ = q;
    // Eco renders cycles less often; High more often, and reads them through 8 points.
    renderInterval_ = q == 0 ? 256 : q == 1 ? 128 : 64;
    for (auto& v : voice_)
        for (auto& o : v.oscs)
            o.setFineReads(q == 2);
}

// ---- state -----------------------------------------------------------------------------------

std::shared_ptr<const dsp::Wavetable> WavetableSynth::table(int n) const
{
    return tables_[static_cast<size_t>(std::clamp(n, 0, numOscs - 1))].table;
}

const std::string& WavetableSynth::tableName(int n) const
{
    const auto& t = tables_[static_cast<size_t>(std::clamp(n, 0, numOscs - 1))];
    return t.table != nullptr ? t.table->name : t.name;
}

const std::string& WavetableSynth::tableId(int n) const
{
    return tables_[static_cast<size_t>(std::clamp(n, 0, numOscs - 1))].id;
}

std::string WavetableSynth::macroName(int n) const
{
    const auto& name = macroNames_[static_cast<size_t>(std::clamp(n, 0, numMacros - 1))];
    return name.empty() ? "Macro " + std::to_string(n + 1) : name;
}

dsp::Oscillator::Mix WavetableSynth::subShape(int shape) noexcept
{
    dsp::Oscillator::Mix m{0.0f, 0.0f, 0.0f, 0.0f, 0.5f};
    switch (shape)
    {
    case 0:
        m.sine = 1.0f;
        break;
    case 1:
        m.triangle = 1.0f;
        break;
    case 2:
        m.saw = 1.0f;
        break;
    default:
        m.square = 1.0f;
        break;
    }
    return m;
}

nlohmann::json WavetableSynth::factoryTablePatch(int n, std::string_view id)
{
    const std::string key = "osc" + std::to_string(n + 1) + ".";
    return {{key + "table", std::string(id)}, {key + "name", nullptr}, {key + "pcm", nullptr}};
}

nlohmann::json WavetableSynth::userTablePatch(int n, const std::string& name, const dsp::Wavetable& table)
{
    std::vector<unsigned char> bytes;
    bytes.reserve(static_cast<size_t>(table.frames()) * dsp::Wavetable::frameSize * 2);
    for (int f = 0; f < table.frames(); ++f)
        for (int i = 0; i < dsp::Wavetable::frameSize; ++i)
        {
            const auto v = static_cast<std::int16_t>(
                std::lround(std::clamp(table.samples(f)[i], -1.0f, 1.0f) * 32767.0f));
            bytes.push_back(static_cast<unsigned char>(v & 0xFF));
            bytes.push_back(static_cast<unsigned char>((v >> 8) & 0xFF));
        }
    const std::string key = "osc" + std::to_string(n + 1) + ".";
    return {{key + "table", "user"}, {key + "name", name}, {key + "pcm", wavetable::toBase64(bytes)}};
}

void WavetableSynth::writeExtraState(nlohmann::json& extra) const
{
    for (int n = 0; n < numOscs; ++n)
    {
        const auto& t = tables_[static_cast<size_t>(n)];
        const std::string key = "osc" + std::to_string(n + 1) + ".";
        if (t.id == "user")
        {
            extra[key + "table"] = "user";
            extra[key + "name"] = t.name;
            extra[key + "pcm"] = t.pcm;
        }
        else if (t.id != wavetable::defaultTableId)
            extra[key + "table"] = t.id;
    }
    const auto writeCurve = [&extra](const std::string& key, const Curve& c, std::string_view standard)
    {
        if (!c.points.empty())
            extra[key + ".points"] = c.points;
        else if (c.shape != standard)
            extra[key + ".shape"] = c.shape;
    };
    for (int n = 0; n < numLfos; ++n)
        writeCurve("lfo" + std::to_string(n + 1), lfoShapes_[static_cast<size_t>(n)], "sine");
    for (int n = 0; n < numOscs * 2; ++n)
        writeCurve("osc" + std::to_string(n / 2 + 1) + ".remap" + std::to_string(n % 2 + 1),
                   remaps_[static_cast<size_t>(n)], "saw-up");
    for (int n = 0; n < numMacros; ++n)
        if (!macroNames_[static_cast<size_t>(n)].empty())
            extra["macro" + std::to_string(n + 1) + ".name"] = macroNames_[static_cast<size_t>(n)];
}

void WavetableSynth::readExtraState(const nlohmann::json& extra)
{
    const auto text = [&extra](const std::string& key) -> std::string
    {
        const auto it = extra.find(key);
        return it != extra.end() && it->is_string() ? it->get<std::string>() : std::string();
    };
    for (int n = 0; n < numOscs; ++n)
    {
        auto& t = tables_[static_cast<size_t>(n)];
        const std::string key = "osc" + std::to_string(n + 1) + ".";
        std::string id = text(key + "table");
        if (id.empty())
            id = std::string(wavetable::defaultTableId);
        if (id == "user")
        {
            const auto pcm = text(key + "pcm");
            if (pcm == t.pcm && t.id == "user" && t.table != nullptr)
                continue;
            const auto bytes = wavetable::fromBase64(pcm);
            const int frames = std::clamp(static_cast<int>(bytes.size() / (2 * dsp::Wavetable::frameSize)), 0,
                                          dsp::Wavetable::maxFrames);
            if (frames > 0)
            {
                std::vector<float> samples(static_cast<size_t>(frames) * dsp::Wavetable::frameSize);
                for (size_t i = 0; i < samples.size(); ++i)
                    samples[i] = static_cast<float>(
                                     static_cast<std::int16_t>(bytes[2 * i] | (bytes[2 * i + 1] << 8))) /
                                 32767.0f;
                auto table = std::make_shared<dsp::Wavetable>(
                    dsp::Wavetable::fromCycles(samples.data(), dsp::Wavetable::frameSize, frames, false));
                table->name = text(key + "name");
                t = {"user", table->name, pcm, std::move(table)};
                continue;
            }
            id = std::string(wavetable::defaultTableId); // unreadable: back to the default
        }
        if (id != t.id || t.table == nullptr)
        {
            auto table = wavetable::factoryTable(id);
            if (table == nullptr)
            {
                id = std::string(wavetable::defaultTableId);
                table = wavetable::factoryTable(id);
            }
            t = {id, table->name, {}, std::move(table)};
        }
    }
    const auto readCurve = [&extra, &text](const std::string& key, std::string_view standard)
    {
        Curve c;
        if (const auto it = extra.find(key + ".points"); it != extra.end() && it->is_array())
            for (const auto& p : *it)
                if (p.is_array() && p.size() == 3)
                    c.points.push_back({p[0].get<float>(), p[1].get<float>(), p[2].get<float>()});
        const auto shape = text(key + ".shape");
        c.shape = shape.empty() ? std::string(standard) : shape;
        std::sort(c.points.begin(), c.points.end(), [](const auto& a, const auto& b) { return a[0] < b[0]; });
        return c;
    };
    for (int n = 0; n < numLfos; ++n)
        lfoShapes_[static_cast<size_t>(n)] = readCurve("lfo" + std::to_string(n + 1), "sine");
    for (int n = 0; n < numOscs * 2; ++n)
        remaps_[static_cast<size_t>(n)] =
            readCurve("osc" + std::to_string(n / 2 + 1) + ".remap" + std::to_string(n % 2 + 1), "saw-up");
    for (int n = 0; n < numMacros; ++n)
        macroNames_[static_cast<size_t>(n)] = text("macro" + std::to_string(n + 1) + ".name");
    publishAssets();
}

// ---- notes -----------------------------------------------------------------------------------

dsp::CurvedEnvelope::Settings WavetableSynth::envelopeSettings(int n) const noexcept
{
    const auto& p = params();
    dsp::CurvedEnvelope::Settings s;
    s.attackMs = p.get(env(n, EnvAttack));
    s.holdMs = p.get(env(n, EnvHold));
    s.decayMs = p.get(env(n, EnvDecay));
    s.sustain = (smoothedPrimed_ ? smoothed_[env(n, EnvSustain)] : p.get(env(n, EnvSustain))) * 0.01;
    s.releaseMs = p.get(env(n, EnvRelease));
    s.attackCurve = p.get(env(n, EnvAttackCurve)) * 0.01;
    s.decayCurve = p.get(env(n, EnvDecayCurve)) * 0.01;
    s.releaseCurve = p.get(env(n, EnvReleaseCurve)) * 0.01;
    s.mode = static_cast<dsp::CurvedEnvelope::Mode>(p.getInt(env(n, EnvMode)));
    return s;
}

void WavetableSynth::voiceStart(int index, int key, float velocity, int channel, std::int32_t, bool retrigger)
{
    auto& v = voice_[static_cast<size_t>(index)];
    const auto& p = params();
    // Still sounding (its envelope says so, whatever the block size): its filters keep their state.
    const bool wasSounding = v.envs[0].isActive() && retrigger;
    v.key = key;
    v.channel = channel & 15;
    v.velocity = velocity;
    v.releaseVelocity = 0.0f;
    v.seed = hash(++notes_ * 2654435761U);
    v.noteRandom = static_cast<float>(2.0 * (hash(v.seed + 7) >> 8) / 16777216.0 - 1.0);
    v.heldSamples = 0.0;
    for (int n = 0; n < numOscs; ++n)
    {
        auto& o = v.oscs[static_cast<size_t>(n)];
        o.setFineReads(quality_ == 2);
        o.start(hash(v.seed + static_cast<std::uint32_t>(n) * 31U), p.get(osc(n, OscPhase)) / 360.0f,
                p.get(osc(n, OscRand)) * 0.01f);
    }
    v.sub.reset();
    v.noise.reset(hash(v.seed + 99));
    for (auto& stage : v.decimators)
        for (auto& d : stage)
            d.reset();
    if (!wasSounding)
        for (auto& pair : v.filters)
            for (auto& f : pair)
                f.reset();
    for (int n = 0; n < numEnvs; ++n)
    {
        v.envs[static_cast<size_t>(n)].set(envelopeSettings(n));
        v.envs[static_cast<size_t>(n)].noteOn();
    }
    for (int n = 0; n < numLfos; ++n)
        v.lfos[static_cast<size_t>(n)].noteOn(p.get(lfo(n, LfoPhase)) / 360.0);
    v.lfoSmoothed = {};
    v.randomValue = {};
    v.lastOut = {};
    v.lastSub = v.lastNoise = 0.0f;
    for (int n = 0; n < numRandoms; ++n)
    {
        v.randoms[static_cast<size_t>(n)].reset(hash(v.seed + 500 + static_cast<std::uint32_t>(n)));
        v.randoms[static_cast<size_t>(n)].noteOn();
    }
    v.driftPitch.seed(hash(v.seed + 11));
    v.driftCutoff.seed(hash(v.seed + 13));
    // Glide: from the last note played, when there's a glide (always, or only between held notes).
    const double target = key;
    const bool glide = p.get(VoiceGlide) > 0.0f && (p.getInt(VoiceGlideMode) == 0 || wasSounding);
    v.glidePitch = glide ? lastPitch_ : target;
    v.glideTarget = target;
    v.glideRate =
        glide ? std::abs(target - v.glidePitch) / std::max(1.0, p.get(VoiceGlide) * 0.001 * sampleRate_)
              : 0.0;
    lastPitch_ = target;
    v.sounding = true;
    newest_ = index;
    for (auto& u : v.unison)
        u.count = -1; // set again at the first tick
    // Nothing from the voice's last note: its controls start where this note puts them.
    v.from = {};
    v.to = {};
    tickVoice(v, TransportInfo{}, true);
    v.from = v.to;
}

void WavetableSynth::voiceLegato(int index, int key, float velocity)
{
    auto& v = voice_[static_cast<size_t>(index)];
    const auto& p = params();
    v.velocity = velocity;
    v.glideTarget = key;
    const double ms = p.get(VoiceGlide);
    v.glideRate = ms > 0.0 ? std::abs(key - v.glidePitch) / std::max(1.0, ms * 0.001 * sampleRate_) : 1e9;
    v.key = key;
    lastPitch_ = key;
}

void WavetableSynth::voiceRelease(int index)
{
    auto& v = voice_[static_cast<size_t>(index)];
    for (auto& e : v.envs)
        e.noteOff();
}

void WavetableSynth::handleEvent(const Event& e) noexcept
{
    const auto ch = static_cast<size_t>(e.channel & 15);
    switch (e.type)
    {
    case EventType::NoteOn:
    case EventType::NoteOff:
    case EventType::AllNotesOff:
        voices_.handle(e, *this);
        break;
    case EventType::Controller:
        if (e.key == 1)
            modWheel_ = e.value;
        else if (e.key == 74)
            slide_[ch] = e.value;
        else
            voices_.handle(e, *this); // the sustain pedal
        break;
    case EventType::PitchBend:
        bend_[ch] = e.value;
        break;
    case EventType::ChannelPressure:
    case EventType::PolyPressure:
        pressure_[ch] = e.value;
        break;
    }
}

// ---- control ticks ---------------------------------------------------------------------------

float WavetableSynth::sourceValue(const Voice& v, int source) const noexcept
{
    const auto s = static_cast<Source>(source);
    if (s >= Source::AmpEnv && s <= Source::Env4)
        return v.envs[static_cast<size_t>(source - static_cast<int>(Source::AmpEnv))].level();
    if (s >= Source::Lfo1 && s < Source::Random1)
    {
        const int n = source - static_cast<int>(Source::Lfo1);
        const bool poly = params().getBool(lfo(n, LfoPoly));
        float value = poly ? v.lfoSmoothed[static_cast<size_t>(n)] : lfoValue_[static_cast<size_t>(n)];
        if (poly)
            if (const double fade = params().get(lfo(n, LfoFade)) * 0.001 * sampleRate_; fade > 1.0)
                value *= static_cast<float>(std::min(1.0, v.heldSamples / fade));
        return value;
    }
    if (s >= Source::Random1 && s < Source::Macro1)
    {
        const int n = source - static_cast<int>(Source::Random1);
        return params().getBool(random(n, RandomPoly)) ? v.randomValue[static_cast<size_t>(n)]
                                                       : randomValue_[static_cast<size_t>(n)];
    }
    if (s >= Source::Macro1 && s < Source::Velocity)
        return smoothed_[macro(source - static_cast<int>(Source::Macro1))] * 0.01f;
    switch (s)
    {
    case Source::Velocity:
        return v.velocity;
    case Source::ReleaseVelocity:
        return v.releaseVelocity;
    case Source::Note:
        return std::clamp((v.key - 60) / 60.0f, -1.0f, 1.0f);
    case Source::Pressure:
        return pressure_[static_cast<size_t>(v.channel)];
    case Source::Slide:
        return slide_[static_cast<size_t>(v.channel)];
    case Source::PitchBend:
        return bend_[static_cast<size_t>(v.channel)];
    case Source::ModWheel:
        return modWheel_;
    case Source::NoteRandom:
        return v.noteRandom;
    default:
        return 0.0f;
    }
}

void WavetableSynth::tick(const TransportInfo& transport, bool renderNow) noexcept
{
    const auto& p = params();
    const int interval = grid_.interval();
    volume_.setTarget(dsp::dbToGain(p.get(OutVolume), -70.0f), dsp::msToSamples(20.0, sampleRate_));
    voices_.setVoiceCount(p.getInt(VoiceCount), *this);
    voices_.setMode(static_cast<VoiceManager::Mode>(p.getInt(VoiceMode)), *this);

    // Continuous settings glide over about 20 ms, at the control rate.
    const double k = smoothedPrimed_ ? 1.0 - std::exp(-interval / (0.02 * sampleRate_)) : 1.0;
    for (size_t i = 0; i < smoothed_.size(); ++i)
    {
        const float target = p.get(i);
        if (p.spec(i).kind != ParamKind::Continuous)
            smoothed_[i] = target;
        else
            smoothed_[i] += static_cast<float>(k * (target - smoothed_[i]));
    }
    smoothedPrimed_ = true;

    const auto* a = current_.get();
    // Shared LFOs and random sources (those not per note).
    const double beatsPerBar = transport.timeSigNumerator * 4.0 / std::max(1, transport.timeSigDenominator);
    const auto cycles = [&](int sync, double hz) -> double
    {
        if (sync <= 0)
            return hz / sampleRate_;
        const double beats = kSyncBeats[sync] < 0.0 ? -kSyncBeats[sync] * beatsPerBar : kSyncBeats[sync];
        return transport.tempoBpm / 60.0 / beats / sampleRate_;
    };
    for (int n = 0; n < numLfos; ++n)
    {
        if (p.getBool(lfo(n, LfoPoly)) || a == nullptr)
            continue;
        const int sync = p.getInt(lfo(n, LfoSync));
        const auto mode = static_cast<dsp::ShapeLfo::Mode>(p.getInt(lfo(n, LfoMode)));
        double song = -1.0;
        if (sync > 0 && transport.playing && mode == dsp::ShapeLfo::Mode::Free)
        {
            const double beats = kSyncBeats[sync] < 0.0 ? -kSyncBeats[sync] * beatsPerBar : kSyncBeats[sync];
            song = transport.ppqPosition / beats + p.get(lfo(n, LfoPhase)) / 360.0;
        }
        // Shared, it has no note to restart with: it runs freely.
        const float raw = lfos_[static_cast<size_t>(n)].advance(
            a->lfoShapes[static_cast<size_t>(n)], dsp::ShapeLfo::Mode::Free, interval,
            cycles(sync, smoothed_[lfo(n, LfoRate)]), song);
        const double smooth = smoothed_[lfo(n, LfoSmooth)] * 0.01;
        const double tau = smooth * 0.25 / std::max(0.01, static_cast<double>(smoothed_[lfo(n, LfoRate)]));
        const double follow = tau > 1e-6 ? 1.0 - std::exp(-interval / (tau * sampleRate_)) : 1.0;
        lfoValue_[static_cast<size_t>(n)] +=
            static_cast<float>(follow * (raw - lfoValue_[static_cast<size_t>(n)]));
    }
    for (int n = 0; n < numRandoms; ++n)
        if (!p.getBool(random(n, RandomPoly)))
            randomValue_[static_cast<size_t>(n)] = randoms_[static_cast<size_t>(n)].advance(
                static_cast<dsp::RandomSource::Mode>(p.getInt(random(n, RandomMode))), interval,
                cycles(p.getInt(random(n, RandomSync)), smoothed_[random(n, RandomRate)]));

    for (auto& v : voice_)
        if (v.sounding)
        {
            v.from = v.to;
            tickVoice(v, transport, renderNow);
        }
}

void WavetableSynth::tickVoice(Voice& v, const TransportInfo& transport, bool renderNow) noexcept
{
    const auto& p = params();
    const auto& specs = tables().specs;
    const auto* a = current_.get();
    const int interval = grid_.interval();

    // The matrix: every route's source, bent, scaled, into its target (in its normalised range).
    std::array<float, NumTargets> mod{};
    for (int r = 0; r < numRoutes; ++r)
    {
        const int src = p.getInt(route(r, RouteSource)), dst = p.getInt(route(r, RouteTarget));
        const float amount = smoothed_[route(r, RouteAmount)] * 0.01f;
        if (src <= 0 || dst <= 0 || dst >= NumTargets || amount == 0.0f)
            continue;
        const float value = sourceValue(v, src);
        const float bent = std::copysign(
            std::pow(std::abs(value), std::exp2(-2.0f * smoothed_[route(r, RouteCurve)] * 0.01f)), value);
        float scale = 1.0f;
        if (const int aux = p.getInt(route(r, RouteScale)); aux > 0)
        {
            const float s = sourceValue(v, aux);
            const auto as = static_cast<Source>(aux);
            const bool bipolar = (as >= Source::Lfo1 && as < Source::Macro1) || as == Source::Note ||
                                 as == Source::PitchBend || as == Source::NoteRandom;
            scale = bipolar ? 0.5f * (s + 1.0f) : s;
        }
        mod[static_cast<size_t>(dst)] += bent * amount * scale;
    }
    // A setting where it is now: its smoothed value moved by its routes, within its range.
    const auto moved = [&](size_t param, int target) -> float
    {
        const auto& spec = specs[param];
        const float base = smoothed_[param];
        const float m = target > 0 ? mod[static_cast<size_t>(target)] : 0.0f;
        if (m == 0.0f)
            return base;
        return spec.fromNormalized(std::clamp(spec.toNormalized(base) + m, 0.0f, 1.0f));
    };

    // Pitch: the note (gliding), the bend, drift and pitch routes.
    const int ch = v.channel;
    if (v.glidePitch != v.glideTarget)
    {
        const double step = v.glideRate * interval;
        v.glidePitch = std::abs(v.glideTarget - v.glidePitch) <= step
                           ? v.glideTarget
                           : v.glidePitch + std::copysign(step, v.glideTarget - v.glidePitch);
    }
    const double drift = smoothed_[VoiceDrift] * 0.01;
    v.driftP = v.driftPitch.advance(interval, sampleRate_);
    v.driftC = v.driftCutoff.advance(interval, sampleRate_);
    const double pitch = v.glidePitch + bend_[static_cast<size_t>(ch)] * p.get(VoiceBend) +
                         drift * 0.15 * v.driftP + 24.0 * mod[TargetPitch];

    for (int n = 0; n < numOscs; ++n)
    {
        const auto i = static_cast<size_t>(n);
        auto& o = v.oscs[i];
        const bool on = p.getBool(osc(n, OscOn));
        const double oscPitch = pitch + 12.0 * p.get(osc(n, OscOct)) + p.get(osc(n, OscSemi)) +
                                moved(osc(n, OscFine), oscTarget(n, TFine)) * 0.01 +
                                24.0 * mod[static_cast<size_t>(oscTarget(n, TPitch))];
        const double increment = std::min(0.45, hzForPitch(oscPitch) / sampleRate_);
        v.to[CIncrement0 + i] = increment;
        const float levelDb = moved(osc(n, OscLevel), oscTarget(n, TLevel));
        v.to[CLevel0 + i] = on && levelDb > -70.0f ? dsp::dbToGain(levelDb) : 0.0f;
        const auto [l, r] = balance(moved(osc(n, OscPan), oscTarget(n, TPan)));
        v.to[CPanL0 + i] = l;
        v.to[CPanR0 + i] = r;
        v.to[CPos0 + i] = moved(osc(n, OscPos), oscTarget(n, TPosition)) * 0.01;
        v.to[CWarpA0 + i] = moved(osc(n, OscWarp1Amt), oscTarget(n, TWarp1)) * 0.01;
        v.to[CWarpB0 + i] = moved(osc(n, OscWarp2Amt), oscTarget(n, TWarp2)) * 0.01;

        // The unison, set again when it changed.
        dsp::WavetableOscillator::Unison u;
        u.count = p.getInt(osc(n, OscUni));
        u.detune = moved(osc(n, OscDetune), oscTarget(n, TDetune)) * 0.01f;
        u.mode = static_cast<dsp::WavetableOscillator::UnisonMode>(p.getInt(osc(n, OscUniMode)));
        u.stack = static_cast<dsp::WavetableOscillator::Stack>(p.getInt(osc(n, OscStack)));
        u.blend = moved(osc(n, OscBlend), oscTarget(n, TBlend)) * 0.01f;
        u.width = moved(osc(n, OscWidth), oscTarget(n, TWidth)) * 0.01f;
        u.positionSpread = moved(osc(n, OscSpread), oscTarget(n, TPositionSpread)) * 0.01f;
        u.warpSpread = moved(osc(n, OscWarpSpread), oscTarget(n, TWarpSpread)) * 0.01f;
        auto& was = v.unison[i];
        if (was.count != u.count || std::abs(was.detune - u.detune) > 1e-4f || was.mode != u.mode ||
            was.stack != u.stack || std::abs(was.blend - u.blend) > 1e-4f ||
            std::abs(was.width - u.width) > 1e-4f ||
            std::abs(was.positionSpread - u.positionSpread) > 1e-4f ||
            std::abs(was.warpSpread - u.warpSpread) > 1e-4f)
        {
            o.setUnison(u);
            was = u;
        }
    }

    // Sub and noise.
    v.to[CSubIncrement] = std::min(0.45, hzForPitch(pitch + 12.0 * p.get(SubOct)) / sampleRate_);
    const float subDb = moved(SubLevel, TargetSubLevel);
    v.to[CSubLevel] = p.getBool(SubOn) && subDb > -70.0f ? dsp::dbToGain(subDb) : 0.0f;
    std::tie(v.to[CSubPanL], v.to[CSubPanR]) = balance(moved(SubPan, TargetSubPan));
    const float noiseDb = moved(NoiseLevel, TargetNoiseLevel);
    v.to[CNoiseLevel] = p.getBool(NoiseOn) && noiseDb > -70.0f ? dsp::dbToGain(noiseDb) : 0.0f;
    v.to[CNoiseColor] = moved(NoiseColor, TargetNoiseColor) * 0.01;
    std::tie(v.to[CNoisePanL], v.to[CNoisePanR]) = balance(moved(NoisePan, TargetNoisePan));

    // A new cycle for each oscillator on the render grid (or the first), crossfading in over the
    // interval (in the oscillators' own samples).
    const int factor = oscillatorRateFactor();
    for (int n = 0; n < numOscs; ++n)
    {
        const auto i = static_cast<size_t>(n);
        auto& o = v.oscs[i];
        if (!p.getBool(osc(n, OscOn)) || a == nullptr || a->tables[i] == nullptr ||
            !(renderNow || !o.hasCycle()))
            continue;
        const double increment = v.to[CIncrement0 + i];
        std::array<dsp::CycleRenderer::WarpSetting, 2> warps{};
        // How much faster than its pitch FM and PM may read the cycle at moments: FM's peak ratio,
        // PM's deviation (2 pi times the amount, at the source's pitch over this one's).
        double fmReach = 1.0, pmReach = 0.0;
        for (int k = 0; k < 2; ++k)
        {
            const auto warp = static_cast<dsp::Warp>(p.getInt(osc(n, k == 0 ? OscWarp1 : OscWarp2)));
            const auto slot = (k == 0 ? CWarpA0 : CWarpB0) + i;
            warps[static_cast<size_t>(k)] = {dsp::isSampleWarp(warp) ? dsp::Warp::Off : warp,
                                             static_cast<float>(v.to[slot]),
                                             &a->remaps[static_cast<size_t>(n * 2 + k)]};
            const double amount = std::max(v.from[slot], v.to[slot]);
            if (warp == dsp::Warp::Fm)
                fmReach *= 1.0 + 4.0 * amount;
            else if (warp == dsp::Warp::Pm)
            {
                const int src = p.getInt(osc(n, k == 0 ? OscWarp1Src : OscWarp2Src));
                const double srcIncrement = src < numOscs ? v.to[CIncrement0 + static_cast<size_t>(src)] *
                                                                v.oscs[static_cast<size_t>(src)].widestRatio()
                                            : src == numOscs ? v.to[CSubIncrement]
                                                             : 0.0;
                pmReach += 2.0 * std::numbers::pi * amount * srcIncrement / std::max(1e-9, increment);
            }
        }
        // The highest pitch it may reach before the next render: two semitones' headroom.
        const double highest = std::max(v.from[CIncrement0 + i], increment) * 1.1225;
        o.render(renderer_, *a->tables[i], v.to[CPos0 + i], warps, highest, sampleRate_,
                 renderInterval_ * factor, fmReach + pmReach, factor);
    }

    // Filters: the cutoff from key tracking, the filter envelope, routes and drift.
    for (int n = 0; n < numFilters; ++n)
    {
        const auto i = static_cast<size_t>(n);
        const double octaves = smoothed_[filter(n, FilterKey)] * 0.01 * (v.glidePitch - 60.0) / 12.0 +
                               smoothed_[filter(n, FilterEnv)] * 0.01 * 5.0 * v.envs[1].level() +
                               5.0 * mod[static_cast<size_t>(filterTarget(n, TCutoff))] +
                               drift * 0.3 * v.driftC;
        v.to[CCutoff0 + i] =
            std::clamp(smoothed_[filter(n, FilterCutoff)] * std::exp2(octaves), 20.0, 0.45 * sampleRate_);
        v.to[CRes0 + i] = moved(filter(n, FilterRes), filterTarget(n, TResonance)) * 0.01;
        v.to[CDrive0 + i] = moved(filter(n, FilterDrive), filterTarget(n, TDrive)) * 0.01;
        v.to[CShape0 + i] = moved(filter(n, FilterShape), filterTarget(n, TShape)) * 0.01;
    }
    v.to[CRouting] = moved(FiltersRouting, TargetRouting) * 0.01;

    // Amp: velocity, routes, pan.
    const double velocity = 1.0 - smoothed_[AmpVelocity] * 0.01 * (1.0 - v.velocity);
    const double amp = std::max(0.0, velocity * (1.0 + mod[TargetAmp]));
    const auto [panL, panR] = balance(mod[TargetPan]);
    v.to[CAmpL] = amp * panL;
    v.to[CAmpR] = amp * panR;

    // Envelope settings (new times at the next segment).
    for (int n = 0; n < numEnvs; ++n)
        v.envs[static_cast<size_t>(n)].set(envelopeSettings(n));

    // Per-note LFOs and random sources, for the next tick (their rates moved by routes).
    const double beatsPerBar = transport.timeSigNumerator * 4.0 / std::max(1, transport.timeSigDenominator);
    const auto cycles = [&](int sync, double hz) -> double
    {
        if (sync <= 0)
            return hz / sampleRate_;
        const double beats = kSyncBeats[sync] < 0.0 ? -kSyncBeats[sync] * beatsPerBar : kSyncBeats[sync];
        return std::max(1.0, transport.tempoBpm) / 60.0 / beats / sampleRate_;
    };
    if (a != nullptr)
        for (int n = 0; n < numLfos; ++n)
        {
            if (!p.getBool(lfo(n, LfoPoly)))
                continue;
            const auto i = static_cast<size_t>(n);
            const double hz = moved(lfo(n, LfoRate), TargetLfoRate + n);
            const float raw = v.lfos[i].advance(a->lfoShapes[i],
                                                static_cast<dsp::ShapeLfo::Mode>(p.getInt(lfo(n, LfoMode))),
                                                interval, cycles(p.getInt(lfo(n, LfoSync)), hz));
            const double tau = smoothed_[lfo(n, LfoSmooth)] * 0.01 * 0.25 / std::max(0.01, hz);
            const double follow = tau > 1e-6 ? 1.0 - std::exp(-interval / (tau * sampleRate_)) : 1.0;
            v.lfoSmoothed[i] += static_cast<float>(follow * (raw - v.lfoSmoothed[i]));
        }
    for (int n = 0; n < numRandoms; ++n)
        if (p.getBool(random(n, RandomPoly)))
            v.randomValue[static_cast<size_t>(n)] = v.randoms[static_cast<size_t>(n)].advance(
                static_cast<dsp::RandomSource::Mode>(p.getInt(random(n, RandomMode))), interval,
                cycles(p.getInt(random(n, RandomSync)), moved(random(n, RandomRate), TargetRandomRate + n)));
    v.heldSamples += interval;
}

// ---- audio -----------------------------------------------------------------------------------

int WavetableSynth::oscillatorRateFactor() const noexcept
{
    const auto& p = params();
    for (int n = 0; n < numOscs; ++n)
        if (p.getBool(osc(n, OscOn)) &&
            (dsp::isSampleWarp(static_cast<dsp::Warp>(p.getInt(osc(n, OscWarp1)))) ||
             dsp::isSampleWarp(static_cast<dsp::Warp>(p.getInt(osc(n, OscWarp2))))))
            return quality_ == 2 ? 4 : quality_ == 1 ? 2 : 1;
    return 1;
}

void WavetableSynth::renderVoice(Voice& v, float* outL, float* outR, int from, int to, int phase) noexcept
{
    const auto& p = params();
    const int interval = grid_.interval();
    // What stays put across the stretch.
    std::array<int, numOscs> routes{};
    std::array<std::array<dsp::Warp, 2>, numOscs> sampleWarps{};
    std::array<std::array<int, 2>, numOscs> warpSources{};
    for (int n = 0; n < numOscs; ++n)
    {
        routes[static_cast<size_t>(n)] = p.getInt(osc(n, OscRoute));
        for (int k = 0; k < 2; ++k)
        {
            const auto w = static_cast<dsp::Warp>(p.getInt(osc(n, k == 0 ? OscWarp1 : OscWarp2)));
            sampleWarps[static_cast<size_t>(n)][static_cast<size_t>(k)] =
                dsp::isSampleWarp(w) ? w : dsp::Warp::Off;
            warpSources[static_cast<size_t>(n)][static_cast<size_t>(k)] =
                p.getInt(osc(n, k == 0 ? OscWarp1Src : OscWarp2Src));
        }
    }
    const int subRoute = p.getInt(SubRoute), noiseRoute = p.getInt(NoiseRoute);
    const auto subWave = subShape(p.getInt(SubShape));
    const auto noiseType = static_cast<dsp::ColouredNoise::Type>(p.getInt(NoiseType));
    std::array<bool, numFilters> filterOn{};
    std::array<dsp::VoiceFilter::Type, numFilters> filterType{};
    for (int n = 0; n < numFilters; ++n)
    {
        filterOn[static_cast<size_t>(n)] = p.getBool(filter(n, FilterOn));
        filterType[static_cast<size_t>(n)] =
            static_cast<dsp::VoiceFilter::Type>(p.getInt(filter(n, FilterType)));
    }

    // Frequency modulation and its kin, at Normal and High, are made at 2 or 4 times the rate and
    // brought back down (their sidebands would otherwise fold back as aliasing).
    const int factor = oscillatorRateFactor();

    for (int s = from; s < to; ++s)
    {
        const double t = static_cast<double>(phase + (s - from) + 1) / interval;
        const auto c = [&v, t](size_t k) { return v.from[k] + t * (v.to[k] - v.from[k]); };
        float envs[numEnvs];
        for (int n = 0; n < numEnvs; ++n)
            envs[n] = v.envs[static_cast<size_t>(n)].next();

        // The buses into filter 1 and 2, and around them; left and right; per step of the rate.
        std::array<std::array<double, 4>, 6> bus{}; // aL, aR, bL, bR, nL, nR
        const double routing = c(CRouting);
        for (int step = 0; step < factor; ++step)
        {
            const auto send = [&](int routeIndex, double l, double r)
            {
                const auto j = static_cast<size_t>(step);
                switch (routeIndex)
                {
                case 0:
                    bus[0][j] += l, bus[1][j] += r;
                    break;
                case 1:
                    bus[2][j] += l, bus[3][j] += r;
                    break;
                case 2:
                    bus[0][j] += l, bus[1][j] += r, bus[2][j] += routing * l, bus[3][j] += routing * r;
                    break;
                default:
                    bus[4][j] += l, bus[5][j] += r;
                    break;
                }
            };
            std::array<float, numOscs> out{};
            for (int n = 0; n < numOscs; ++n)
            {
                const auto i = static_cast<size_t>(n);
                const double level = c(CLevel0 + i);
                if (level <= 0.0 && !p.getBool(osc(n, OscOn)))
                    continue;
                // Warps from another source, sample by sample (an earlier one's now, a later one's last).
                double fm = 1.0, pm = 0.0, gain = 1.0;
                for (int k = 0; k < 2; ++k)
                {
                    const auto w = sampleWarps[i][static_cast<size_t>(k)];
                    if (w == dsp::Warp::Off)
                        continue;
                    const int src = warpSources[i][static_cast<size_t>(k)];
                    const double value = src < numOscs    ? (src < n ? out[static_cast<size_t>(src)]
                                                                     : v.lastOut[static_cast<size_t>(src)])
                                         : src == numOscs ? v.lastSub
                                                          : v.lastNoise;
                    const double amt = c((k == 0 ? CWarpA0 : CWarpB0) + i);
                    switch (w)
                    {
                    case dsp::Warp::Fm:
                        fm *= 1.0 + 4.0 * amt * value;
                        break;
                    case dsp::Warp::Pm:
                        pm += amt * value;
                        break;
                    case dsp::Warp::Rm:
                        gain *= 1.0 - amt + amt * value;
                        break;
                    case dsp::Warp::Am:
                        gain *= 1.0 - amt + amt * (0.5 + 0.5 * value);
                        break;
                    default:
                        break;
                    }
                }
                float l = 0.0f, r = 0.0f;
                const float mono = v.oscs[i].next(c(CIncrement0 + i) / factor, fm, pm, l, r);
                out[i] = static_cast<float>(mono * gain);
                const double g = level * gain;
                send(routes[i], l * g * c(CPanL0 + i), r * g * c(CPanR0 + i));
            }
            v.lastOut = out;
            // Sub and noise.
            const double subLevel = c(CSubLevel);
            if (subLevel > 0.0)
            {
                v.lastSub = v.sub.process(c(CSubIncrement) / factor, subWave);
                send(subRoute, v.lastSub * subLevel * c(CSubPanL), v.lastSub * subLevel * c(CSubPanR));
            }
            const double noiseLevel = c(CNoiseLevel);
            if (noiseLevel > 0.0)
            {
                v.lastNoise =
                    v.noise.next(noiseType, static_cast<float>(c(CNoiseColor)), sampleRate_ * factor);
                send(noiseRoute, v.lastNoise * noiseLevel * c(CNoisePanL),
                     v.lastNoise * noiseLevel * c(CNoisePanR));
            }
        }
        // Back down to the rate: one half-band per halving.
        double aL = bus[0][0], aR = bus[1][0], bL = bus[2][0], bR = bus[3][0], nL = bus[4][0], nR = bus[5][0];
        if (factor > 1)
        {
            std::array<double, 6> down{};
            for (size_t ch = 0; ch < 6; ++ch)
            {
                auto& stage = v.decimators[ch];
                if (factor == 2)
                    down[ch] =
                        stage[0].process(static_cast<float>(bus[ch][0]), static_cast<float>(bus[ch][1]));
                else
                {
                    const float x =
                        stage[0].process(static_cast<float>(bus[ch][0]), static_cast<float>(bus[ch][1]));
                    const float y =
                        stage[0].process(static_cast<float>(bus[ch][2]), static_cast<float>(bus[ch][3]));
                    down[ch] = stage[1].process(x, y);
                }
            }
            aL = down[0], aR = down[1], bL = down[2], bR = down[3], nL = down[4], nR = down[5];
        }
        // Filter 1 into filter 2, or side by side, or anywhere between.
        double yL = aL, yR = aR;
        if (filterOn[0])
        {
            yL = v.filters[0][0].process(static_cast<float>(aL), filterType[0], c(CCutoff0), c(CRes0),
                                         c(CDrive0), c(CShape0));
            yR = v.filters[0][1].process(static_cast<float>(aR), filterType[0], c(CCutoff0), c(CRes0),
                                         c(CDrive0), c(CShape0));
        }
        double zL = (1.0 - routing) * yL + bL, zR = (1.0 - routing) * yR + bR;
        if (filterOn[1])
        {
            zL = v.filters[1][0].process(static_cast<float>(zL), filterType[1], c(CCutoff0 + 1), c(CRes0 + 1),
                                         c(CDrive0 + 1), c(CShape0 + 1));
            zR = v.filters[1][1].process(static_cast<float>(zR), filterType[1], c(CCutoff0 + 1), c(CRes0 + 1),
                                         c(CDrive0 + 1), c(CShape0 + 1));
        }
        const double mixL = zL + routing * yL + nL, mixR = zR + routing * yR + nR;
        const double amp = envs[0];
        outL[s] += static_cast<float>(mixL * amp * c(CAmpL));
        outR[s] += static_cast<float>(mixR * amp * c(CAmpR));
        // Done at the sample its envelope ends, whatever the block size: the voice is free again.
        if (!v.envs[0].isActive())
        {
            v.sounding = false;
            voices_.voiceFinished(static_cast<int>(&v - voice_.data()));
            return;
        }
    }
}

void WavetableSynth::process(ProcessContext& ctx) noexcept
{
    const auto started = std::chrono::steady_clock::now();
    const int n = ctx.numSamples;
    if (n <= 0 || ctx.output.numChannels == 0)
        return;
    float* outL = ctx.output.channel(0);
    float* outR = ctx.output.numChannels > 1 ? ctx.output.channel(1) : nullptr;
    std::fill(outL, outL + n, 0.0f);
    if (outR != nullptr)
        std::fill(outR, outR + n, 0.0f);
    // A mono output plays the left channel: the right goes to a spare.
    if (outR == nullptr)
    {
        std::fill(spareRight_.begin(), spareRight_.end(), 0.0f);
        outR = spareRight_.data();
    }
    const bool stereo = ctx.output.numChannels > 1;

    // New tables and curves, at the block's start.
    if (auto fresh = assets_.takeNew(); fresh.isSet())
    {
        if (current_.isSet())
            assets_.retire(current_);
        current_ = fresh;
    }
    applyQuality();

    const EventList* events = ctx.events;
    size_t nextEvent = 0;
    const int interval = grid_.interval();
    grid_.forEachSegment(n,
                         [&](int offset, int length, bool isTick) noexcept
                         {
                             const int phase0 = static_cast<int>(grid_.position() % interval);
                             if (isTick)
                             {
                                 // The song's position at this tick, not the block's start: block sizes never
                                 // show.
                                 TransportInfo at = ctx.transport;
                                 at.samplePosition += offset;
                                 at.ppqPosition += offset * ctx.transport.tempoBpm / (60.0 * sampleRate_);
                                 tick(at, grid_.position() % renderInterval_ == 0);
                             }
                             int i = offset;
                             const int end = offset + length;
                             while (i < end)
                             {
                                 while (events != nullptr && nextEvent < events->size() &&
                                        (*events)[nextEvent].sampleOffset <= i)
                                     handleEvent((*events)[nextEvent++]);
                                 int stop = end;
                                 if (events != nullptr && nextEvent < events->size() &&
                                     (*events)[nextEvent].sampleOffset < end)
                                     stop = std::max(i + 1, (*events)[nextEvent].sampleOffset);
                                 for (auto& v : voice_)
                                 {
                                     if (!v.sounding)
                                         continue;
                                     renderVoice(v, outL, outR, i, stop, phase0 + (i - offset));
                                 }
                                 i = stop;
                             }
                         });
    while (events != nullptr && nextEvent < events->size())
        handleEvent((*events)[nextEvent++]);

    float peak = 0.0f;
    for (int s = 0; s < n; ++s)
    {
        const float g = volume_.next();
        outL[s] *= g;
        outR[s] *= g;
        peak = std::max({peak, std::abs(outL[s]), stereo ? std::abs(outR[s]) : 0.0f});
    }
    analysis().publishPeak(PeakOut, peak);
    analysis().publish(ActiveVoices, static_cast<float>(voices_.activeVoices()));
    if (newest_ >= 0 && voice_[static_cast<size_t>(newest_)].sounding)
    {
        const auto& v = voice_[static_cast<size_t>(newest_)];
        for (int k = 0; k < numOscs; ++k)
            analysis().publish(Osc1Position + k, static_cast<float>(v.to[CPos0 + static_cast<size_t>(k)]));
        for (int k = 0; k < numLfos; ++k)
            analysis().publish(Lfo1Phase + k,
                               static_cast<float>(params().getBool(lfo(k, LfoPoly))
                                                      ? v.lfos[static_cast<size_t>(k)].phase()
                                                      : lfos_[static_cast<size_t>(k)].phase()));
    }
    // The CPU it takes: this block's time over the block's own length, eased.
    const double took = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    cpuLoad_ += 0.1 * (took * sampleRate_ / n - cpuLoad_);
    analysis().publish(CpuLoad, static_cast<float>(cpuLoad_));
}

const DeviceFactory& wavetableFactory()
{
    static const DeviceFactory factory{
        .info = []() -> const DeviceInfo& { return tables().info; },
        .create = []() -> std::unique_ptr<Device> { return std::make_unique<WavetableSynth>(); },
    };
    return factory;
}
} // namespace fd::devices
