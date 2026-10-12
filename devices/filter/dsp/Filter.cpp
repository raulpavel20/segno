#include "Filter.h"

#include "fd/dsp/Math.h"

#include <algorithm>
#include <cmath>

namespace fd::devices
{
namespace
{
constexpr std::string_view kTypes[] = {"Low Pass", "High Pass", "Band Pass", "Notch",      "Morph",
                                       "DJ",       "Comb",      "Vowel",     "Notch + LP", "Resampling"};
constexpr std::string_view kCircuits[] = {"Clean", "Warm", "Acid", "Gritty"};
constexpr std::string_view kSlopes[] = {"12 dB", "24 dB"};
constexpr std::string_view kShapes[] = {"Sine",   "Triangle", "Saw Up", "Saw Down",
                                        "Square", "Random",   "Drift"};
constexpr std::string_view kStereo[] = {"Offset", "Spin"};

constexpr std::array<ParamSpec, Filter::NumParams> kSpecs{{
    {.id = "type",
     .name = "Type",
     .shortName = "Type",
     .kind = ParamKind::Choice,
     .min = 0.0f,
     .max = 9.0f,
     .def = 0.0f,
     .choices = kTypes},
    {.id = "circuit",
     .name = "Circuit",
     .shortName = "Circuit",
     .kind = ParamKind::Choice,
     .min = 0.0f,
     .max = 3.0f,
     .def = 0.0f,
     .choices = kCircuits},
    {.id = "slope",
     .name = "Slope",
     .shortName = "Slope",
     .kind = ParamKind::Choice,
     .min = 0.0f,
     .max = 1.0f,
     .def = 1.0f,
     .choices = kSlopes},
    {.id = "freq",
     .name = "Freq",
     .shortName = "Freq",
     .min = 20.0f,
     .max = 20000.0f,
     .def = 2000.0f,
     .skewCentre = 632.0f,
     .unit = Unit::Hertz,
     .smoothing = {SmoothingKind::Multiplicative, 20.0f},
     .decimals = 0},
    {.id = "res",
     .name = "Resonance",
     .shortName = "Res",
     .min = 0.0f,
     .max = 100.0f,
     .def = 25.0f,
     .unit = Unit::Percent,
     .decimals = 0},
    {.id = "sweep",
     .name = "Sweep",
     .shortName = "Sweep",
     .min = -100.0f,
     .max = 100.0f,
     .def = 0.0f,
     .unit = Unit::Percent,
     .decimals = 0},
    {.id = "vowel",
     .name = "Vowel",
     .shortName = "Vowel",
     .min = 0.0f,
     .max = 100.0f,
     .def = 0.0f,
     .unit = Unit::Percent,
     .decimals = 0},
    {.id = "morph",
     .name = "Morph",
     .shortName = "Morph",
     .min = 0.0f,
     .max = 100.0f,
     .def = 0.0f,
     .unit = Unit::Percent,
     .decimals = 0},
    {.id = "drive",
     .name = "Drive",
     .shortName = "Drive",
     .min = 0.0f,
     .max = 24.0f,
     .def = 0.0f,
     .unit = Unit::Decibels},
    {.id = "lfo",
     .name = "LFO",
     .shortName = "LFO",
     .kind = ParamKind::Toggle,
     .min = 0.0f,
     .max = 1.0f,
     .def = 0.0f},
    {.id = "lfo.rate",
     .name = "LFO Rate",
     .shortName = "Rate",
     .min = 0.01f,
     .max = 40.0f,
     .def = 1.0f,
     .skewCentre = 2.0f,
     .unit = Unit::Hertz,
     .smoothing = {SmoothingKind::Multiplicative, 20.0f},
     .decimals = 2},
    {.id = "lfo.sync",
     .name = "LFO Sync",
     .shortName = "Sync",
     .kind = ParamKind::Toggle,
     .min = 0.0f,
     .max = 1.0f,
     .def = 1.0f},
    {.id = "lfo.div",
     .name = "LFO Sync Rate",
     .shortName = "Rate",
     .kind = ParamKind::Choice,
     .min = 0.0f,
     .max = 16.0f,
     .def = 3.0f,
     .choices = dsp::lfoNoteValues},
    {.id = "lfo.shape",
     .name = "LFO Shape",
     .shortName = "Shape",
     .kind = ParamKind::Choice,
     .min = 0.0f,
     .max = 6.0f,
     .def = 0.0f,
     .choices = kShapes},
    {.id = "lfo.amount",
     .name = "LFO Amount",
     .shortName = "Amount",
     .min = 0.0f,
     .max = 100.0f,
     .def = 50.0f,
     .unit = Unit::Percent,
     .decimals = 0},
    {.id = "lfo.phase",
     .name = "LFO Phase",
     .shortName = "Phase",
     .min = 0.0f,
     .max = 360.0f,
     .def = 0.0f,
     .unit = Unit::Degrees,
     .decimals = 0},
    {.id = "lfo.stereo",
     .name = "LFO Stereo",
     .shortName = "Stereo",
     .kind = ParamKind::Choice,
     .min = 0.0f,
     .max = 1.0f,
     .def = 0.0f,
     .choices = kStereo},
    {.id = "lfo.offset",
     .name = "LFO Offset",
     .shortName = "Offset",
     .min = 0.0f,
     .max = 180.0f,
     .def = 0.0f,
     .unit = Unit::Degrees,
     .decimals = 0},
    {.id = "lfo.spin",
     .name = "LFO Spin",
     .shortName = "Spin",
     .min = 0.0f,
     .max = 100.0f,
     .def = 20.0f,
     .unit = Unit::Percent,
     .decimals = 0},
    {.id = "env",
     .name = "Envelope",
     .shortName = "Envelope",
     .kind = ParamKind::Toggle,
     .min = 0.0f,
     .max = 1.0f,
     .def = 0.0f},
    {.id = "env.amount",
     .name = "Env Amount",
     .shortName = "Amount",
     .min = -100.0f,
     .max = 100.0f,
     .def = 50.0f,
     .unit = Unit::Percent,
     .decimals = 0},
    {.id = "env.attack",
     .name = "Env Attack",
     .shortName = "Attack",
     .min = 1.0f,
     .max = 200.0f,
     .def = 10.0f,
     .skewCentre = 20.0f,
     .unit = Unit::Milliseconds,
     .decimals = 0},
    {.id = "env.release",
     .name = "Env Release",
     .shortName = "Release",
     .min = 10.0f,
     .max = 2000.0f,
     .def = 250.0f,
     .skewCentre = 250.0f,
     .unit = Unit::Milliseconds,
     .decimals = 0},
    {.id = "clip",
     .name = "Clip",
     .shortName = "Clip",
     .kind = ParamKind::Toggle,
     .min = 0.0f,
     .max = 1.0f,
     .def = 1.0f},
    {.id = "mix",
     .name = "Dry/Wet",
     .shortName = "Mix",
     .min = 0.0f,
     .max = 100.0f,
     .def = 100.0f,
     .unit = Unit::Percent,
     .decimals = 0},
    {.id = "output",
     .name = "Output",
     .shortName = "Output",
     .min = -24.0f,
     .max = 12.0f,
     .def = 0.0f,
     .unit = Unit::Decibels},
}};
// What each parameter does, in one sentence: the same as in SPEC.md (open question B22).
constexpr ParamDescription kDescriptions[] = {
    {"type",
     "The kind of filter: the classic passes and a notch, a morph between them, a DJ sweep, a comb, a vowel, "
     "or resampling."},
    {"circuit",
     "The filter's character: Clean is exact, Warm thick and smooth, Acid rubbery and squelchy, Gritty raw "
     "where it resonates."},
    {"slope", "How steeply the filter cuts: 12 dB per octave is gentle, 24 dB steeper and more dramatic."},
    {"freq", "Where the filter works: its cutoff, a comb's pitch, or where resampling starts to fold the "
             "highs back."},
    {"res",
     "How much the filter rings at its cutoff: more is sharper and more vocal, and near 100 % it whistles."},
    {"sweep", "The DJ filter: left of the centre a low pass closes on the highs, right of it a high pass on "
              "the lows; at "
              "the centre, nothing."},
    {"vowel", "Which vowel the filter speaks: a, e, i, o or u, and the sounds between them."},
    {"morph", "Morph: low to band to high pass. Comb: peaks to notches. Notch + LP: the notch nearer the "
              "cutoff. Vowel: a "
              "deep voice to a small one."},
    {"drive", "How hard the sound hits the filter: more is warmer and grittier, at about the same level."},
    {"lfo", "Moves the filter with a wave, freely or in time with the song."},
    {"lfo.rate", "How fast the LFO moves the filter, in cycles per second."},
    {"lfo.sync", "Moves the filter in time with the song: the rate becomes a note value."},
    {"lfo.div", "How fast the LFO moves the filter as a note value, one cycle per note, while Sync is on."},
    {"lfo.shape", "The LFO's wave: smooth, even, rising, falling, jumping, stepping at random or drifting."},
    {"lfo.amount", "How far the LFO moves the filter: at 100 %, five octaves each way."},
    {"lfo.phase",
     "Where the cycle starts against the beat, while Sync is on: 180° starts half a cycle late."},
    {"lfo.stereo", "How the two sides move: Offset moves them apart in step, Spin at different speeds."},
    {"lfo.offset", "How far apart the two sides move: together at 0°, in opposite directions at 180°."},
    {"lfo.spin", "How much faster the right side moves than the left, so the two drift in and out of step."},
    {"env", "Lets the sound's level move the filter: the input's, or the sidechain's when one is chosen."},
    {"env.amount", "How far, and which way, the level moves the filter: at 100 %, up to five octaves."},
    {"env.attack", "How quickly the filter follows the sound getting louder."},
    {"env.release", "How quickly the filter falls back once the sound gets quieter."},
    {"clip", "Softly rounds off the filtered sound's peaks, so resonance never goes over 0 dBFS. Quieter "
             "sound passes "
             "exactly."},
    {"mix", "Blends the original sound with the filtered one."},
    {"output", "The level after the filter."},
};
constexpr auto kParams = describe(kSpecs, kDescriptions);
static_assert(allDescribed(kParams), "every parameter needs a description (SPEC.md)");

constexpr MeterSpec kMeters[] = {
    {.slot = Filter::PeakInL, .id = "input_left", .name = "Input, left", .kind = MeterKind::Level},
    {.slot = Filter::PeakInR, .id = "input_right", .name = "Input, right", .kind = MeterKind::Level},
    {.slot = Filter::PeakOutL, .id = "output_left", .name = "Output, left", .kind = MeterKind::Level},
    {.slot = Filter::PeakOutR, .id = "output_right", .name = "Output, right", .kind = MeterKind::Level},
};

constexpr DeviceInfo kInfo{
    .id = "filter",
    .name = "Filter",
    .category = "EQ & Filter",
    .kind = DeviceKind::AudioEffect,
    .buses = {.inputChannels = 2, .outputChannels = 2, .sidechainChannels = 2},
    .params = kParams,
    .stateVersion = 1,
    .meters = kMeters,
};

using C = std::complex<double>;
using T = Filter::TypeKind;
using K = Filter::CircuitKind;

// ---- tuning (SPEC: DSP design); starting points, tuned by ear -------------------------------

constexpr float kEnvFloorDb = -48.0f;
constexpr double kLfoSmoothMs = 3.0, kGlideMs = 12.0;
constexpr double kHighestQ = 25.0;
constexpr double kButterworth = 0.70710678118654752; // a 2-pole's Q at Resonance 0
constexpr double kButterworth4a = 0.54119610014619698, kButterworth4b = 1.3065629648763766; // 4-pole
constexpr double kGrittyClip = 1.0;    // where Gritty's resonance clips
constexpr double kAcidLoopRatio = 0.3; // Acid's feedback high pass, against its cutoff
constexpr double kAcidBias = 0.3;      // Acid's saturation leans this way
constexpr double kAcidDcHz = 5.0;
constexpr double kDjLowestHz = 50.0, kDjHighestHz = 10000.0, kDjFlatHz = 10.0, kDjTopHz = 40000.0;
constexpr double kDjCentre = 0.02; // within this of the centre, the DJ filter fades to the dry sound
constexpr double kCombGain = 0.95;
constexpr double kNotchOctaves = 2.0; // Notch + LP: how far below the cutoff the notch starts
constexpr double kVowelMakeupDb = 15.0;

constexpr dsp::ModulationLfo::Shape kLfoShapes[] = {
    dsp::ModulationLfo::Shape::Sine,   dsp::ModulationLfo::Shape::Triangle,
    dsp::ModulationLfo::Shape::SawUp,  dsp::ModulationLfo::Shape::SawDown,
    dsp::ModulationLfo::Shape::Square, dsp::ModulationLfo::Shape::SampleAndHold,
    dsp::ModulationLfo::Shape::Wander};

/// Formants of a low voice (the first three: frequency, level, bandwidth), a e i o u, from
/// published averages.
struct VowelRow
{
    std::array<double, 3> hz, db, bw;
};
constexpr std::array<VowelRow, 5> kVowels{{
    {{600.0, 1040.0, 2250.0}, {0.0, -7.0, -9.0}, {60.0, 70.0, 110.0}},
    {{400.0, 1620.0, 2400.0}, {0.0, -12.0, -9.0}, {40.0, 80.0, 100.0}},
    {{250.0, 1750.0, 2600.0}, {0.0, -30.0, -16.0}, {60.0, 90.0, 100.0}},
    {{400.0, 750.0, 2400.0}, {0.0, -11.0, -21.0}, {40.0, 80.0, 100.0}},
    {{350.0, 600.0, 2400.0}, {0.0, -20.0, -32.0}, {40.0, 80.0, 100.0}},
}};

/// The 2-pole's Q at a Resonance (0..1), from `base` (no peak) to kHighestQ.
double qOf(double res, double base) noexcept
{
    return base * std::pow(kHighestQ / base, std::pow(std::clamp(res, 0.0, 1.0), 1.5));
}

/// How sharp the vowel's formants are against the table's, at a Resonance.
double vowelSharpness(double res) noexcept
{
    return 0.35 * std::pow(3.0 / 0.35, std::clamp(res, 0.0, 1.0));
}

/// A four-pole ladder's resonance at full Resonance, held just under where it would ring on
/// its own, so its ring falls 60 dB within 2 s at any cutoff (SPEC: Resonance at 100 %).
double ladderMaxK(double hz) noexcept
{
    const double margin = std::clamp(0.55 / std::max(hz, 1.0), 0.004, 0.2);
    return std::pow((1.0 - margin) / kButterworth, 4.0);
}

/// Acid's loop (four poles and the feedback's high pass, as a prototype at a cutoff of 1): the
/// frequency it rings at and the resonance where it would ring on its own.
struct AcidLoop
{
    double ring = 1.0, threshold = 4.0;
};
AcidLoop solveAcid()
{
    const auto phase = [](double w)
    { return (dsp::pi / 2.0 - std::atan(w / kAcidLoopRatio)) - 4.0 * std::atan(w); };
    double lo = 0.5, hi = 4.0;
    for (int i = 0; i < 80; ++i)
    {
        const double mid = 0.5 * (lo + hi);
        (phase(mid) > -dsp::pi ? lo : hi) = mid;
    }
    const double w = 0.5 * (lo + hi);
    const double gain = (w / std::hypot(w, kAcidLoopRatio)) * std::pow(1.0 / (1.0 + w * w), 2.0);
    return {.ring = w, .threshold = 1.0 / gain};
}
const AcidLoop kAcid = solveAcid();

double kneeOf(double x) noexcept
{
    const double a = std::abs(x);
    return a <= 0.5 ? x : std::copysign(0.5 + 0.5 * std::tanh((a - 0.5) / 0.5), x);
}

double hardClip(double x, double level) noexcept
{
    const double a = std::abs(x), edge = 0.8 * level, room = 0.2 * level;
    return a <= edge ? x : std::copysign(edge + room * std::tanh((a - edge) / room), x);
}

/// Warm's input stage: a gentle saturation, a slope of 1 at rest (0 dBFS loses 0.7 dB).
double warmSaturate(double x) noexcept
{
    return 2.0 * std::tanh(0.5 * x);
}

/// Acid's saturation: leaning one way (even harmonics), with a slope of 1 at rest.
double acidSaturate(double x) noexcept
{
    const double t = std::tanh(kAcidBias);
    return (std::tanh(x + kAcidBias) - t) / (1.0 - t * t);
}

/// What goes out with Clip off: exactly itself up to full scale, then rounding off towards
/// +12 dBFS (the Phaser & Flanger's).
float bounded(float x) noexcept
{
    const float a = std::abs(x);
    return a <= 1.0f ? x : std::copysign(1.0f + 3.0f * std::tanh((a - 1.0f) / 3.0f), x);
}

double morphBlend(double lp, double bp, double hp, double morph) noexcept
{
    const double m = std::clamp(morph, 0.0, 1.0);
    return m <= 0.5 ? lp + 2.0 * m * (bp - lp) : bp + (2.0 * m - 1.0) * (hp - bp);
}
C morphBlend(C lp, C bp, C hp, double morph) noexcept
{
    const double m = std::clamp(morph, 0.0, 1.0);
    return m <= 0.5 ? lp + 2.0 * m * (bp - lp) : bp + (2.0 * m - 1.0) * (hp - bp);
}

/// The kind of pass a type takes from a circuit.
T passOf(T type) noexcept
{
    return type == T::NotchLowPass ? T::LowPass : type;
}

/// A 2-pole stage's output for a type: low, high, band (its peak at 0 dB), notch or morph.
template <typename V> V svfSelect(T type, V lp, V bp, V hp, double k, double morph) noexcept
{
    switch (type)
    {
    case T::HighPass:
        return hp;
    case T::BandPass:
        return k * bp;
    case T::Notch:
        return lp + hp;
    case T::Morph:
        return morphBlend(lp, k * bp, hp, morph);
    default:
        return lp;
    }
}

/// A ladder's output for a type, from its stages' outputs (y[0] the input to the first stage).
/// Its resonance takes the lows down; the low pass gets some back, so they thin only a little
/// (Acid's feedback high pass keeps the lowest lows itself, so it gets less).
template <typename V>
V ladderSelect(T type, bool steep, const std::array<V, 5>& y, double k, bool acid, double morph) noexcept
{
    const double threshold = acid ? kAcid.threshold : 4.0, push = k * 4.0 / threshold;
    const double lowGain = 1.0 + (acid ? 0.3 : 0.5) * push;
    const double bandGain = 1.0 - k / threshold; // the band pass narrows rather than boosts
    V lp, bp, hp, notch;
    if (steep)
    {
        lp = lowGain * y[4];
        hp = y[0] - 4.0 * y[1] + 6.0 * y[2] - 4.0 * y[3] + y[4];
        bp = 4.0 * bandGain * (y[2] - 2.0 * y[3] + y[4]);
        notch = y[0] - 4.0 * y[1] + 8.0 * y[2] - 8.0 * y[3] + 4.0 * y[4];
    }
    else
    {
        lp = lowGain * y[2];
        hp = y[0] - 2.0 * y[1] + y[2];
        bp = 2.0 * bandGain * (y[1] - y[2]);
        notch = y[0] - 2.0 * y[1] + 2.0 * y[2];
    }
    switch (type)
    {
    case T::HighPass:
        return hp;
    case T::BandPass:
        return bp;
    case T::Notch:
        return notch;
    case T::Morph:
        return morphBlend(lp, bp, hp, morph);
    default:
        return lp;
    }
}

using Tuning = Filter::Tuning;
Tuning tune(K circuit, bool steep, double hz, double res, double rate2) noexcept
{
    const double top = 0.45 * rate2;
    Tuning t;
    if (circuit == K::Warm || circuit == K::Acid)
    {
        const bool acid = circuit == K::Acid;
        const double inner = std::clamp(acid ? hz / kAcid.ring : hz, 5.0, top);
        t.g = std::tan(dsp::pi * inner / rate2);
        t.gh = acid ? std::tan(dsp::pi * kAcidLoopRatio * inner / rate2) : 0.0;
        t.k = std::clamp(res, 0.0, 1.0) * ladderMaxK(hz) * (acid ? kAcid.threshold / 4.0 : 1.0);
    }
    else
    {
        t.g = std::tan(dsp::pi * std::clamp(hz, 5.0, top) / rate2);
        t.k1 = 1.0 / kButterworth4a;
        t.k2 = 1.0 / qOf(res, steep ? kButterworth4b : kButterworth);
    }
    return t;
}

/// A one-pole's prototype variable (s) at `hz` for a stage tuned with `g`, at the oversampled rate.
C sAt(double hz, double g, double rate2) noexcept
{
    return {0.0, std::tan(dsp::pi * std::min(hz, 0.499 * rate2) / rate2) / std::max(g, 1e-12)};
}

/// The circuit's small-signal response for a pass type (what runPasses does at low levels).
C passResponse(K circuit, bool steep, T type, double hz, double res, double morph, double at,
               double rate2) noexcept
{
    const auto t = tune(circuit, steep, hz, res, rate2);
    if (circuit == K::Warm || circuit == K::Acid)
    {
        const bool acid = circuit == K::Acid;
        const C s = sAt(at, t.g, rate2);
        const C g = 1.0 / (1.0 + s);
        C loop = g * g * g * g;
        C dcBlock{1.0, 0.0};
        if (acid)
        {
            const C sh = sAt(at, t.gh, rate2);
            loop *= sh / (1.0 + sh);
            const double r = std::exp(-dsp::twoPi * kAcidDcHz / rate2);
            const C z1 = std::polar(1.0, -dsp::twoPi * at / rate2);
            dcBlock = (1.0 - z1) / (1.0 - r * z1);
        }
        const C u = 1.0 / (1.0 + t.k * loop);
        const std::array<C, 5> y{u, u * g, u * g * g, u * g * g * g, u * g * g * g * g};
        return dcBlock * ladderSelect(type, steep, y, t.k, acid, morph);
    }
    const C s = sAt(at, t.g, rate2);
    const auto stage = [&](double k, C in)
    {
        const C d = s * s + k * s + 1.0;
        return svfSelect(type, in / d, in * s / d, in * s * s / d, k, morph);
    };
    return steep ? stage(t.k2, stage(t.k1, C{1.0, 0.0})) : stage(t.k2, C{1.0, 0.0});
}
} // namespace

Filter::Filter() : Device(kInfo) {}

// ---- pure functions ------------------------------------------------------------------------

size_t Filter::mainParam(TypeKind type) noexcept
{
    return type == TypeKind::Dj ? Sweep : type == TypeKind::Vowel ? Vowel : Freq;
}

double Filter::placeOf(TypeKind type, double value) noexcept
{
    if (type == TypeKind::Dj)
        return std::clamp((value + 100.0) / 200.0, 0.0, 1.0);
    if (type == TypeKind::Vowel)
        return std::clamp(value / 100.0, 0.0, 1.0);
    return std::clamp(std::log(std::max(value, lowestHz) / lowestHz) / std::log(highestHz / lowestHz), 0.0,
                      1.0);
}

double Filter::valueAt(TypeKind type, double place) noexcept
{
    const double p = std::clamp(place, 0.0, 1.0);
    if (type == TypeKind::Dj)
        return p * 200.0 - 100.0;
    if (type == TypeKind::Vowel)
        return p * 100.0;
    return lowestHz * std::pow(highestHz / lowestHz, p);
}

double Filter::modulated(double place, double lfoAmount, double lfo, double envAmount, double env) noexcept
{
    return std::clamp(place + 0.5 * (lfoAmount * lfo + envAmount * env), 0.0, 1.0);
}

Filter::DjFilter Filter::djFilter(double sweep, double sampleRate) noexcept
{
    const double s = std::clamp(sweep, -1.0, 1.0), a = std::abs(s);
    DjFilter f;
    f.highPass = s > 0.0;
    f.amount = std::min(1.0, a / kDjCentre);
    const double top = std::min(kDjTopHz, 0.45 * factor * sampleRate);
    f.hz =
        f.highPass ? kDjFlatHz * std::pow(kDjHighestHz / kDjFlatHz, a) : top * std::pow(kDjLowestHz / top, a);
    return f;
}

Filter::Formants Filter::formants(double vowel, double morph) noexcept
{
    const double x = std::clamp(vowel, 0.0, 1.0) * 4.0;
    const int i = std::min(3, static_cast<int>(x));
    const double t = x - i, shift = std::exp2(std::clamp(morph, 0.0, 1.0));
    const auto& a = kVowels[static_cast<size_t>(i)];
    const auto& b = kVowels[static_cast<size_t>(i + 1)];
    Formants f;
    for (size_t n = 0; n < 3; ++n)
    {
        f.hz[n] = a.hz[n] * std::pow(b.hz[n] / a.hz[n], t) * shift;
        f.level[n] = dsp::dbToGain(static_cast<float>(a.db[n] + t * (b.db[n] - a.db[n])));
        f.bandwidth[n] = (a.bw[n] + t * (b.bw[n] - a.bw[n])) * shift;
    }
    return f;
}

float Filter::softClip(float x) noexcept
{
    return static_cast<float>(kneeOf(x));
}

std::complex<double> Filter::response(const Settings& s, double place, double hz, double sampleRate) noexcept
{
    const double rate2 = factor * sampleRate;
    switch (s.type)
    {
    case TypeKind::Resampling:
        return {1.0, 0.0};
    case TypeKind::Comb:
    {
        const double pitch = std::clamp(valueAt(TypeKind::LowPass, place), lowestHz, combHighestHz);
        const double g = kCombGain * std::clamp(s.res, 0.0, 1.0);
        const C z = std::polar(1.0, -dsp::twoPi * hz / pitch);
        const C peaks = std::sqrt(1.0 - g * g) / (1.0 - g * z),
                notches = (1.0 - g * z) / std::sqrt(1.0 + g * g);
        return peaks + std::clamp(s.morph, 0.0, 1.0) * (notches - peaks);
    }
    case TypeKind::Vowel:
    {
        const auto f = formants(place, s.morph);
        const double sharp = vowelSharpness(s.res);
        C sum{0.0, 0.0};
        for (size_t n = 0; n < 3; ++n)
        {
            const double k = f.bandwidth[n] / (f.hz[n] * sharp);
            const C x = sAt(hz, std::tan(dsp::pi * std::min(f.hz[n], 0.45 * rate2) / rate2), rate2);
            sum += f.level[n] * k * x / (x * x + k * x + 1.0);
        }
        return dsp::dbToGain(static_cast<float>(kVowelMakeupDb)) * std::sqrt(sharp) * sum;
    }
    case TypeKind::Dj:
    {
        const double sweep = valueAt(TypeKind::Dj, place) / 100.0;
        const auto dj = djFilter(sweep, sampleRate);
        const double res = s.res * std::min(1.0, 2.0 * std::abs(sweep));
        const C h = passResponse(s.circuit, s.steep, dj.highPass ? TypeKind::HighPass : TypeKind::LowPass,
                                 dj.hz, res, s.morph, hz, rate2);
        return 1.0 + dj.amount * (h - 1.0);
    }
    default:
        break;
    }
    const double cutoff = valueAt(TypeKind::LowPass, place);
    C h = passResponse(s.circuit, s.steep, passOf(s.type), cutoff, s.res, s.morph, hz, rate2);
    if (s.type == TypeKind::NotchLowPass)
    {
        const double at = cutoff * std::exp2(-kNotchOctaves * (1.0 - std::clamp(s.morph, 0.0, 1.0)));
        const double k = 1.0 / qOf(s.res, kButterworth);
        const C x = sAt(hz, std::tan(dsp::pi * std::min(at, 0.45 * rate2) / rate2), rate2);
        h *= (x * x + 1.0) / (x * x + k * x + 1.0);
    }
    return h;
}

// ---- the circuits --------------------------------------------------------------------------

Filter::Svf::Out Filter::Svf::process(double x, double g, double k, double clip) noexcept
{
    const double a1 = 1.0 / (1.0 + g * (g + k)), a2 = g * a1;
    const double v3 = x - s2;
    double v1 = a1 * s1 + a2 * v3;
    if (clip > 0.0)
        v1 = hardClip(v1, clip);
    const double v2 = s2 + g * v1;
    s1 = 2.0 * v1 - s1;
    s2 = 2.0 * v2 - s2;
    return {.lp = v2, .bp = v1, .hp = x - k * v1 - v2};
}

std::array<double, 5> Filter::Ladder::process(double x, double g, double gh, double k, bool acid) noexcept
{
    const double G = g / (1.0 + g);
    const double G4 = G * G * G * G;
    // Zero-delay feedback: the last stage is G^4 u + S, with S from the stages' states.
    const double S = (G * G * G * s[0] + G * G * s[1] + G * s[2] + s[3]) / (1.0 + g);
    double loopA = G4, loopB = S;
    double Gh = 0.0;
    if (acid)
    {
        Gh = gh / (1.0 + gh);
        loopA = (1.0 - Gh) * G4;
        loopB = (1.0 - Gh) * (S - sh);
    }
    const double linear = (x - k * loopB) / (1.0 + k * loopA);
    const double u = acid ? acidSaturate(linear) : std::tanh(linear);
    std::array<double, 5> y{};
    y[0] = u;
    double in = u;
    for (size_t i = 0; i < 4; ++i)
    {
        const double v = (in - s[i]) * G;
        const double out = v + s[i];
        s[i] = out + v;
        in = out;
        y[i + 1] = out;
    }
    if (acid)
    {
        const double v = (y[4] - sh) * Gh;
        const double lp = v + sh;
        sh = lp + v;
    }
    return y;
}

void Filter::CombLine::prepare(int samples)
{
    int size = 1;
    while (size < samples + 8)
        size <<= 1;
    buffer.assign(static_cast<size_t>(size), 0.0f);
    mask = size - 1;
    write = 0;
}

void Filter::CombLine::reset() noexcept
{
    std::fill(buffer.begin(), buffer.end(), 0.0f);
    write = 0;
}

float Filter::CombLine::read(double delay) const noexcept
{
    // Cubic (Catmull-Rom) between the samples either side of `delay`.
    const double d = std::max(1.0, delay);
    const int i = static_cast<int>(d);
    const float t = static_cast<float>(d - i);
    const auto at = [this](int back) { return buffer[static_cast<size_t>((write - back) & mask)]; };
    const float y0 = at(i - 1), y1 = at(i), y2 = at(i + 1), y3 = at(i + 2);
    const float c1 = 0.5f * (y2 - y0), c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3,
                c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
    return ((c3 * t + c2) * t + c1) * t + y1;
}

void Filter::Core::reset() noexcept
{
    for (auto& s : svf)
        s.reset();
    ladder.reset();
    combIn.reset();
    combOut.reset();
    for (auto& s : vowel)
        s.reset();
    notch.reset();
    held = 0.0;
    holdPhase = 1.0;
    dcIn = dcOut = 0.0;
    tunedHz = tunedRes = -1.0;
    vowelPlace = vowelRes = vowelMorph = -1.0;
}

double Filter::runCore(Core& core, double x, double place, double res, double morph,
                       const DriveNow& drive) noexcept
{
    const auto& st = core.settings;
    // Clean's drive (and the types without a circuit): a soft saturation going in, level-matched.
    const double pre = x + drive.clean * (kneeOf(x * drive.gain) / drive.gain - x);
    switch (st.type)
    {
    case TypeKind::Resampling:
    {
        const double step = 2.0 * valueAt(TypeKind::LowPass, place) / rate2_;
        if (step >= 1.0)
            return pre;
        core.holdPhase += step;
        if (core.holdPhase >= 1.0)
        {
            core.holdPhase -= std::floor(core.holdPhase);
            core.held = pre;
        }
        return core.held;
    }
    case TypeKind::Comb:
    {
        const double pitch = std::clamp(valueAt(TypeKind::LowPass, place), lowestHz, combHighestHz);
        const double delay = rate2_ / pitch, g = kCombGain * std::clamp(res, 0.0, 1.0);
        const double peaks = pre + g * core.combOut.read(delay - 1.0);
        const double notches = pre - g * core.combIn.read(delay - 1.0);
        core.combOut.push(static_cast<float>(peaks));
        core.combIn.push(static_cast<float>(pre));
        const double a = peaks * std::sqrt(1.0 - g * g), b = notches / std::sqrt(1.0 + g * g);
        return a + std::clamp(morph, 0.0, 1.0) * (b - a);
    }
    case TypeKind::Vowel:
    {
        if (place != core.vowelPlace || res != core.vowelRes || morph != core.vowelMorph)
        {
            const auto f = formants(place, morph);
            const double sharp = vowelSharpness(res);
            for (size_t n = 0; n < 3; ++n)
            {
                core.vowelK[n] = f.bandwidth[n] / (f.hz[n] * sharp);
                core.vowelG[n] = std::tan(dsp::pi * std::min(f.hz[n], 0.45 * rate2_) / rate2_);
                core.vowelLevel[n] = f.level[n] * core.vowelK[n];
            }
            core.vowelGain = dsp::dbToGain(static_cast<float>(kVowelMakeupDb)) * std::sqrt(sharp);
            core.vowelPlace = place;
            core.vowelRes = res;
            core.vowelMorph = morph;
        }
        double sum = 0.0;
        for (size_t n = 0; n < 3; ++n)
            sum += core.vowelLevel[n] * core.vowel[n].process(pre, core.vowelG[n], core.vowelK[n], 0.0).bp;
        return core.vowelGain * sum;
    }
    default:
        break;
    }

    // The types built from a low, band and high pass, through the circuit.
    auto type = passOf(st.type);
    double hz = valueAt(TypeKind::LowPass, place), r = res;
    if (st.type == TypeKind::Dj)
    {
        const double sweep = valueAt(TypeKind::Dj, place) / 100.0;
        const auto dj = djFilter(sweep, sampleRate_);
        type = dj.highPass ? TypeKind::HighPass : TypeKind::LowPass;
        hz = dj.hz;
        r = res * std::min(1.0, 2.0 * std::abs(sweep));
    }
    if (hz != core.tunedHz || r != core.tunedRes)
    {
        core.tuning = tune(st.circuit, st.steep, hz, r, rate2_);
        core.tunedHz = hz;
        core.tunedRes = r;
    }
    const auto& t = core.tuning;
    double y = 0.0;
    switch (st.circuit)
    {
    case CircuitKind::Warm:
    case CircuitKind::Acid:
    {
        const bool acid = st.circuit == CircuitKind::Acid;
        const double in = acid ? x * drive.gain : warmSaturate(x * drive.gain);
        const auto taps = core.ladder.process(in, t.g, t.gh, t.k, acid);
        y = ladderSelect(type, st.steep, taps, t.k, acid, morph) / drive.gain;
        if (acid)
        {
            // Its saturation leans one way: the offset it leaves is taken out.
            const double r5 = std::exp(-dsp::twoPi * kAcidDcHz / rate2_);
            const double out = y - core.dcIn + r5 * core.dcOut;
            core.dcIn = y;
            core.dcOut = out;
            y = out;
        }
        break;
    }
    case CircuitKind::Clean:
    case CircuitKind::Gritty:
    {
        const bool gritty = st.circuit == CircuitKind::Gritty;
        double in = gritty ? hardClip(x * drive.gain, 1.0) : pre;
        if (st.steep)
        {
            const auto a = core.svf[0].process(in, t.g, t.k1, 0.0);
            in = svfSelect(type, a.lp, a.bp, a.hp, t.k1, morph);
        }
        const auto b = core.svf[1].process(in, t.g, t.k2, gritty ? kGrittyClip : 0.0);
        y = svfSelect(type, b.lp, b.bp, b.hp, t.k2, morph);
        if (gritty)
            y /= drive.gain;
        break;
    }
    }

    if (st.type == TypeKind::NotchLowPass)
    {
        const double at = hz * std::exp2(-kNotchOctaves * (1.0 - std::clamp(morph, 0.0, 1.0)));
        const double k = 1.0 / qOf(res, kButterworth);
        const auto n =
            core.notch.process(y, std::tan(dsp::pi * std::min(at, 0.45 * rate2_) / rate2_), k, 0.0);
        y = n.lp + n.hp;
    }
    return y; // the DJ filter's fade to the dry sound is at the device's rate (process: stage 3)
}

// ---- the engine ----------------------------------------------------------------------------

void Filter::prepare(const ProcessSpec& spec)
{
    sampleRate_ = spec.sampleRate > 0.0 ? spec.sampleRate : 48000.0;
    rate2_ = factor * sampleRate_;
    rampSamples_ = dsp::msToSamples(20.0, sampleRate_);
    fadeSamples_ = dsp::msToSamples(30.0, sampleRate_);
    lfoCoef_ = static_cast<float>(1.0 - std::exp(-1.0 / (kLfoSmoothMs * 0.001 * sampleRate_)));
    for (auto* g : {&phase_, &offset_, &rightJump_})
        g->setTime(kGlideMs, sampleRate_);
    oversampler_.prepare(grid_.interval(), maxChannels);
    oversampler_.setFactor(factor);
    for (int c = 0; c < maxChannels; ++c)
    {
        work_[static_cast<size_t>(c)].assign(static_cast<size_t>(grid_.interval()), 0.0f);
        dryLine_[static_cast<size_t>(c)].assign(static_cast<size_t>(oversampler_.latencySamples() + 1), 0.0f);
        for (auto& core : cores_[static_cast<size_t>(c)])
        {
            const int longest = static_cast<int>(std::ceil(rate2_ / lowestHz)) + 4;
            core.combIn.prepare(longest);
            core.combOut.prepare(longest);
        }
    }
    lfo_.prepare(sampleRate_, kGlideMs);
    detector_.prepare(sampleRate_);
    analysis().setSampleRate(sampleRate_);
    analysis().fifo(OutputFifo).prepare(16384);
    reset();
}

void Filter::reset() noexcept
{
    grid_.reset();
    oversampler_.reset();
    for (int c = 0; c < maxChannels; ++c)
    {
        for (auto& core : cores_[static_cast<size_t>(c)])
            core.reset();
        std::fill(dryLine_[static_cast<size_t>(c)].begin(), dryLine_[static_cast<size_t>(c)].end(), 0.0f);
        dryPos_[static_cast<size_t>(c)] = 0;
    }
    lfo_.reset();
    detector_.reset();
    envDb_ = kEnvFloorDb;
    snap();
    fresh_ = true;
}

int Filter::latencySamples() const noexcept
{
    return oversampler_.latencySamples();
}

Filter::Settings Filter::settingsNow() const noexcept
{
    const auto& p = params();
    return {.type = static_cast<TypeKind>(p.getInt(Type)),
            .circuit = static_cast<CircuitKind>(p.getInt(Circuit)),
            .steep = p.getInt(Slope) == 1,
            .res = p.get(Res) * 0.01,
            .morph = p.get(Morph) * 0.01};
}

void Filter::snap() noexcept
{
    const auto& p = params();
    active_ = 0;
    for (auto& side : cores_)
        side[0].settings = side[1].settings = settingsNow();
    fade_.reset(1.0f);
    stereo_ = static_cast<StereoKind>(p.getInt(LfoStereo));
    shape_ = p.getInt(LfoShape);
    updateTargets(/*immediate*/ true);
    for (auto* g : {&phase_, &offset_})
        g->snap(g->target());
    rightJump_.snap(0.0);
    // Locked to the song at once: nothing has played to glide from.
    updateSync(/*snap*/ true);
    rightPosition_ = lfo_.position() + offset_.current();
    const auto shape = kLfoShapes[std::clamp(shape_, 0, 6)];
    lfoValue_[0] = dsp::ModulationLfo::value(shape, lfo_.position() + phase_.current());
    lfoValue_[1] = dsp::ModulationLfo::value(shape, rightPosition_ + phase_.current());
}

void Filter::updateTargets(bool immediate) noexcept
{
    const auto& p = params();
    const int ramp = immediate ? 0 : rampSamples_;
    res_.setTarget(p.get(Res) * 0.01f, ramp);
    sweep_.setTarget(p.get(Sweep), ramp);
    vowel_.setTarget(p.get(Vowel), ramp);
    morph_.setTarget(p.get(Morph) * 0.01f, ramp);
    drive_.setTarget(p.get(Drive), ramp);
    lfoOn_.setTarget(p.getBool(Lfo) ? 1.0f : 0.0f, ramp);
    lfoAmount_.setTarget(p.get(LfoAmount) * 0.01f, ramp);
    spin_.setTarget(p.get(LfoSpin) * 0.01f, ramp);
    envOn_.setTarget(p.getBool(Env) ? 1.0f : 0.0f, ramp);
    envAmount_.setTarget(p.get(EnvAmount) * 0.01f, ramp);
    clip_.setTarget(p.getBool(Clip) ? 1.0f : 0.0f, ramp);
    mix_.setTarget(p.get(Mix) * 0.01f, ramp);
    output_.setTarget(dsp::dbToGain(p.get(Output)), ramp);
    freq_.setTarget(p.get(Freq), ramp);
    rate_.setTarget(p.get(LfoRate), ramp);
    offset_.setTarget(p.get(LfoOffset) / 360.0);
    // Phase: the nearer way round (0° and 360° are the same place).
    const double phase = p.get(LfoPhase) / 360.0, step = phase - phase_.target();
    phase_.setTarget(phase_.target() + step - std::round(step));
    if (immediate)
    {
        for (auto* s : {&res_, &sweep_, &vowel_, &morph_, &drive_, &lfoOn_, &lfoAmount_, &spin_, &envOn_,
                        &envAmount_, &clip_, &mix_, &output_})
            s->reset(s->target());
        freq_.reset(freq_.target());
        rate_.reset(rate_.target());
        phase_.snap(phase);
    }

    // A new type, circuit or slope: the old filter and the new run side by side and crossfade.
    const auto now = settingsNow();
    const auto& current = cores_[0][static_cast<size_t>(active_)].settings;
    if (now.type != current.type || now.circuit != current.circuit || now.steep != current.steep)
    {
        if (immediate)
            for (auto& side : cores_)
                side[static_cast<size_t>(active_)].settings = now;
        else
        {
            active_ = 1 - active_;
            for (auto& side : cores_)
            {
                side[static_cast<size_t>(active_)].reset();
                side[static_cast<size_t>(active_)].settings = now;
            }
            fade_.reset(0.0f);
            fade_.setTarget(1.0f, fadeSamples_);
        }
    }
    // Back from Spin to Offset: the right side glides to its place.
    const auto stereo = static_cast<StereoKind>(p.getInt(LfoStereo));
    if (stereo != stereo_ && !immediate && stereo == StereoKind::Offset)
    {
        const double jump = lfo_.position() + offset_.current() - rightPosition_;
        rightJump_.snap(-(jump - std::round(jump)));
        rightJump_.setTarget(0.0);
    }
    stereo_ = stereo;
    shape_ = p.getInt(LfoShape);

    // The envelope: rising or falling its whole range (48 dB) in Env Attack or Env Release.
    envRise_ = -kEnvFloorDb / static_cast<float>(std::max(1.0, p.get(EnvAttack) * 0.001 * sampleRate_));
    envFall_ = -kEnvFloorDb / static_cast<float>(std::max(1.0, p.get(EnvRelease) * 0.001 * sampleRate_));
}

void Filter::updateSync(bool snap) noexcept
{
    const auto& p = params();
    // A synced cycle at most maxCycleSeconds long, at a slow tempo.
    const double beats = p.getBool(LfoSync) ? std::min(dsp::lfoNoteBeats(p.getInt(LfoDiv), beatsPerBar_),
                                                       maxCycleSeconds * tempo_ / 60.0)
                                            : 0.0;
    lfo_.set(rate_.current(), beats, snap);
}

void Filter::process(ProcessContext& ctx) noexcept
{
    const int n = ctx.numSamples;
    if (n <= 0 || ctx.output.numChannels == 0)
        return;
    const int inChannels = std::min(ctx.input.numChannels, maxChannels);
    const int outChannels = std::min(ctx.output.numChannels, maxChannels);
    const int scChannels = std::min(ctx.sidechain.numChannels, maxChannels);
    tempo_ = ctx.transport.tempoBpm > 0.0 ? ctx.transport.tempoBpm : 120.0;
    beatsPerBar_ = ctx.transport.timeSigDenominator > 0
                       ? ctx.transport.timeSigNumerator * 4.0 / ctx.transport.timeSigDenominator
                       : 4.0;
    lfo_.beginBlock(ctx.transport.playing, ctx.transport.ppqPosition, tempo_);
    if (fresh_)
    {
        snap();
        fresh_ = false;
    }
    const int latency = oversampler_.latencySamples();

    float peakIn[maxChannels] = {}, peakOut[maxChannels] = {}, envelope = 0.0f;
    grid_.forEachSegment(
        n,
        [&](int offset, int length, bool isTick) noexcept
        {
            if (isTick)
            {
                updateTargets(false);
                updateSync(/*snap*/ false);
            }
            const auto shape = kLfoShapes[std::clamp(shape_, 0, 6)];
            const int old = 1 - active_;
            const auto typeNow = cores_[0][static_cast<size_t>(active_)].settings.type;
            const auto typeOld = cores_[0][static_cast<size_t>(old)].settings.type;
            float* work[maxChannels] = {work_[0].data(), work_[1].data()};
            std::array<float, 64> heard{};

            // Stage 1 (the device's rate): the modulation, each side's place, and the dry sound.
            for (int i = 0; i < length; ++i)
            {
                const auto at = static_cast<size_t>(i);
                resNow_[at] = res_.next();
                morphNow_[at] = morph_.next();
                fadeNow_[at] = fade_.next();
                driveNow_[at] = drive_.next();
                mixNow_[at] = mix_.next();
                outNow_[at] = output_.next();
                clipNow_[at] = clip_.next();
                const float lfoOn = lfoOn_.next(), lfoAmount = lfoAmount_.next(), spin = spin_.next(),
                            envOn = envOn_.next(), envAmount = envAmount_.next();
                const float sweep = sweep_.next(), vowel = vowel_.next(), freq = freq_.next();
                rate_.next();
                const double phase = phase_.next(), stereoOffset = offset_.next(),
                             rightJump = rightJump_.next();

                // The LFO, each side's, smoothed so the jumping shapes don't click.
                const double left = lfo_.next();
                if (stereo_ == StereoKind::Spin)
                    rightPosition_ += lfo_.rateHz() * (1.0 + spin) / sampleRate_;
                else
                    rightPosition_ = left + stereoOffset + rightJump;
                lfoValue_[0] += lfoCoef_ * (dsp::ModulationLfo::value(shape, left + phase) - lfoValue_[0]);
                lfoValue_[1] +=
                    lfoCoef_ * (dsp::ModulationLfo::value(shape, rightPosition_ + phase) - lfoValue_[1]);

                const int s = offset + i;
                const float in[maxChannels] = {ctx.input.channel(0)[s], inChannels > 1
                                                                            ? ctx.input.channel(1)[s]
                                                                            : ctx.input.channel(0)[s]};
                // The envelope: the sidechain's peak if one is connected, else the input's.
                const float key = scChannels > 0
                                      ? std::max(std::abs(ctx.sidechain.channel(0)[s]),
                                                 std::abs(ctx.sidechain.channel(scChannels - 1)[s]))
                                      : std::max(std::abs(in[0]), std::abs(in[1]));
                const float levelDb = detector_.processDb(key);
                envDb_ = std::max(kEnvFloorDb, envDb_ + std::clamp(levelDb - envDb_, -envFall_, envRise_));
                const float env = (envDb_ - kEnvFloorDb) / -kEnvFloorDb;
                envelope = envOn * env;

                const auto valueFor = [&](TypeKind type)
                {
                    return type == TypeKind::Dj ? sweep : type == TypeKind::Vowel ? vowel : freq;
                };
                // How much of a DJ filter sounds: none at its centre, where the dry sound stands in.
                const auto djAmount = [this](TypeKind type, double place)
                {
                    return type == TypeKind::Dj
                               ? djFilter(valueAt(TypeKind::Dj, place) / 100.0, sampleRate_).amount
                               : 1.0;
                };
                for (int c = 0; c < maxChannels; ++c)
                {
                    const auto cc = static_cast<size_t>(c);
                    const double moveLfo = lfoOn * lfoAmount, moveEnv = envOn * envAmount;
                    place_[at][cc] =
                        modulated(placeOf(typeNow, valueFor(typeNow)), moveLfo, lfoValue_[cc], moveEnv, env);
                    placeOld_[at][cc] =
                        modulated(placeOf(typeOld, valueFor(typeOld)), moveLfo, lfoValue_[cc], moveEnv, env);
                    const double now = djAmount(typeNow, place_[at][cc]),
                                 was = djAmount(typeOld, placeOld_[at][cc]);
                    djNow_[cc][at] = static_cast<float>(was + fadeNow_[at] * (now - was));
                    peakIn[c] = std::max(peakIn[c], std::abs(in[c]));
                    work[c][i] = in[c];
                    dryNow_[cc][at] = in[c];
                    lastPlace_[cc] = place_[at][cc];
                }
            }

            // Stage 2 (twice the rate): the filter, the old one crossfading out after a change.
            float* const* up = oversampler_.upsample(work, maxChannels, length);
            for (int c = 0; c < maxChannels; ++c)
            {
                auto& side = cores_[static_cast<size_t>(c)];
                for (int j = 0; j < length * factor; ++j)
                {
                    const auto at = static_cast<size_t>(j / factor);
                    const auto cc = static_cast<size_t>(c);
                    const double driveDb = driveNow_[at];
                    const DriveNow drive{.gain = dsp::dbToGain(static_cast<float>(driveDb)),
                                         .clean = std::clamp(driveDb / 3.0, 0.0, 1.0)};
                    const double x = up[c][j];
                    double y = runCore(side[static_cast<size_t>(active_)], x, place_[at][cc], resNow_[at],
                                       morphNow_[at], drive);
                    if (fadeNow_[at] < 1.0f)
                    {
                        const double was = runCore(side[static_cast<size_t>(old)], x, placeOld_[at][cc],
                                                   resNow_[at], morphNow_[at], drive);
                        y = was + fadeNow_[at] * (y - was);
                    }
                    up[c][j] = static_cast<float>(y);
                }
            }
            oversampler_.downsample(work, maxChannels, length);

            // Stage 3 (the device's rate): Clip, the mix with the dry sound, Output.
            for (int i = 0; i < length; ++i)
            {
                const auto at = static_cast<size_t>(i);
                float sum = 0.0f;
                for (int c = 0; c < maxChannels; ++c)
                {
                    const auto cc = static_cast<size_t>(c);
                    // The dry sound, held back by the latency.
                    auto& line = dryLine_[cc];
                    const int size = static_cast<int>(line.size());
                    int& pos = dryPos_[cc];
                    line[static_cast<size_t>(pos)] = dryNow_[cc][at];
                    const float dry = line[static_cast<size_t>((pos - latency + size) % size)];
                    pos = (pos + 1) % size;
                    const float w = work[c][i];
                    float wet = bounded(w) + clipNow_[at] * (softClip(w) - bounded(w));
                    wet = dry + djNow_[cc][at] * (wet - dry);
                    const float y = (dry + mixNow_[at] * (wet - dry)) * outNow_[at];
                    sum += y;
                    if (c < outChannels)
                    {
                        ctx.output.channel(c)[offset + i] = y;
                        peakOut[c] = std::max(peakOut[c], std::abs(y));
                    }
                }
                heard[at] = 0.5f * sum;
            }
            analysis().fifo(OutputFifo).push(heard.data(), static_cast<size_t>(length));
        });

    analysis().publishPeak(PeakInL, peakIn[0]);
    analysis().publishPeak(PeakInR, peakIn[1]);
    analysis().publishPeak(PeakOutL, peakOut[0]);
    analysis().publishPeak(PeakOutR, peakOut[1]);
    analysis().publish(MainLeft, static_cast<float>(lastPlace_[0]));
    analysis().publish(MainRight, static_cast<float>(lastPlace_[1]));
    analysis().publish(Envelope, envelope);
}

double Filter::tailSeconds() const noexcept
{
    const auto& p = params();
    const auto s = settingsNow();
    // The lowest the modulation can take the main control.
    const double reach = 0.5 * ((p.getBool(Lfo) ? p.get(LfoAmount) * 0.01 : 0.0) +
                                (p.getBool(Env) ? std::abs(p.get(EnvAmount)) * 0.01 : 0.0));
    const double lowest =
        std::max(lowestHz, valueAt(TypeKind::LowPass, placeOf(TypeKind::LowPass, p.get(Freq)) - reach));
    const double latency = latencySamples() / sampleRate_;
    double ring = 0.0; // seconds to fall 60 dB
    switch (s.type)
    {
    case TypeKind::Resampling:
        break;
    case TypeKind::Comb:
    {
        const double g = kCombGain * s.res;
        const double passes = g < 1e-3 ? 1.0 : 3.0 / std::log10(1.0 / g);
        ring = (passes + 1.0) / std::min(lowest, combHighestHz);
        break;
    }
    case TypeKind::Vowel:
        // The sharpest formant at the lowest place it can be.
        ring = 6.91 * 2.0 * (250.0 / 40.0) * vowelSharpness(s.res) / (dsp::twoPi * 250.0);
        break;
    default:
    {
        const double hz = s.type == TypeKind::Dj ? kDjLowestHz : lowest;
        if (s.circuit == CircuitKind::Warm || s.circuit == CircuitKind::Acid)
            ring = 2.0 * s.res;
        else
            ring = 6.91 * 2.0 * qOf(s.res, s.steep ? kButterworth4b : kButterworth) / (dsp::twoPi * hz);
        if (s.type == TypeKind::NotchLowPass)
            ring = std::max(ring, 6.91 * 2.0 * qOf(s.res, kButterworth) / (dsp::twoPi * hz / 4.0));
        break;
    }
    }
    return std::min(8.0, ring) + latency + 0.05;
}

const DeviceFactory& filterFactory()
{
    static const DeviceFactory factory{
        .info = []() -> const DeviceInfo& { return kInfo; },
        .create = []() -> std::unique_ptr<Device> { return std::make_unique<Filter>(); },
    };
    return factory;
}
} // namespace fd::devices
