#include "Reverb.h"

#include "fd/dsp/Math.h"

#include <algorithm>
#include <cmath>
#include <complex>

namespace fd::devices
{
namespace
{
constexpr std::string_view kModes[] = {"Room", "Hall", "Plate", "Space"};

constexpr std::array<ParamSpec, Reverb::NumParams> kSpecs{{
    {.id = "mode",
     .name = "Mode",
     .shortName = "Mode",
     .kind = ParamKind::Choice,
     .min = 0.0f,
     .max = 3.0f,
     .def = 1.0f,
     .choices = kModes},
    {.id = "decay",
     .name = "Decay",
     .shortName = "Decay",
     .min = 0.1f,
     .max = 60.0f,
     .def = 2.5f,
     .skewCentre = 2.0f,
     .unit = Unit::Seconds,
     .smoothing = {SmoothingKind::Multiplicative, 50.0f},
     .decimals = 2},
    {.id = "size",
     .name = "Size",
     .shortName = "Size",
     .min = 0.0f,
     .max = 100.0f,
     .def = 50.0f,
     .unit = Unit::Percent,
     .decimals = 0},
    {.id = "predelay",
     .name = "Predelay",
     .shortName = "Predelay",
     .min = 0.0f,
     .max = 500.0f,
     .def = 15.0f,
     .skewCentre = 50.0f,
     .unit = Unit::Milliseconds,
     .decimals = 1},
    {.id = "diffusion",
     .name = "Diffusion",
     .shortName = "Diffusion",
     .min = 0.0f,
     .max = 100.0f,
     .def = 80.0f,
     .unit = Unit::Percent,
     .decimals = 0},
    {.id = "early",
     .name = "Early",
     .shortName = "Early",
     .min = 0.0f,
     .max = 100.0f,
     .def = 50.0f,
     .unit = Unit::Percent,
     .decimals = 0},
    {.id = "motion",
     .name = "Motion",
     .shortName = "Motion",
     .min = 0.0f,
     .max = 100.0f,
     .def = 25.0f,
     .unit = Unit::Percent,
     .decimals = 0},
    {.id = "width",
     .name = "Width",
     .shortName = "Width",
     .min = 0.0f,
     .max = 200.0f,
     .def = 100.0f,
     .unit = Unit::Percent,
     .decimals = 0},
    {.id = "freeze",
     .name = "Freeze",
     .shortName = "Freeze",
     .kind = ParamKind::Toggle,
     .min = 0.0f,
     .max = 1.0f,
     .def = 0.0f},
    {.id = "low.mult",
     .name = "Low Decay",
     .shortName = "Low",
     .min = 25.0f,
     .max = 400.0f,
     .def = 120.0f,
     .skewCentre = 100.0f,
     .unit = Unit::Percent,
     .decimals = 0},
    {.id = "low.freq",
     .name = "Low Freq",
     .shortName = "Freq",
     .min = 50.0f,
     .max = 1000.0f,
     .def = 250.0f,
     .skewCentre = 250.0f,
     .unit = Unit::Hertz,
     .smoothing = {SmoothingKind::Multiplicative, 50.0f},
     .decimals = 0},
    {.id = "high.mult",
     .name = "High Decay",
     .shortName = "High",
     .min = 10.0f,
     .max = 200.0f,
     .def = 60.0f,
     .skewCentre = 60.0f,
     .unit = Unit::Percent,
     .decimals = 0},
    {.id = "high.freq",
     .name = "High Freq",
     .shortName = "Freq",
     .min = 1000.0f,
     .max = 16000.0f,
     .def = 4000.0f,
     .skewCentre = 4000.0f,
     .unit = Unit::Hertz,
     .smoothing = {SmoothingKind::Multiplicative, 50.0f},
     .decimals = 0},
    {.id = "filter",
     .name = "Filter",
     .shortName = "Filter",
     .kind = ParamKind::Toggle,
     .min = 0.0f,
     .max = 1.0f,
     .def = 1.0f},
    {.id = "filter.low",
     .name = "Low Cut",
     .shortName = "Low Cut",
     .min = 20.0f,
     .max = 2000.0f,
     .def = 100.0f,
     .skewCentre = 200.0f,
     .unit = Unit::Hertz,
     .smoothing = {SmoothingKind::Multiplicative, 20.0f},
     .decimals = 0},
    {.id = "filter.high",
     .name = "High Cut",
     .shortName = "High Cut",
     .min = 1000.0f,
     .max = 20000.0f,
     .def = 12000.0f,
     .skewCentre = 6000.0f,
     .unit = Unit::Hertz,
     .smoothing = {SmoothingKind::Multiplicative, 20.0f},
     .decimals = 0},
    {.id = "duck",
     .name = "Ducking",
     .shortName = "Ducking",
     .min = 0.0f,
     .max = 100.0f,
     .def = 0.0f,
     .unit = Unit::Percent,
     .decimals = 0},
    {.id = "duck.release",
     .name = "Duck Release",
     .shortName = "Release",
     .min = 50.0f,
     .max = 2000.0f,
     .def = 300.0f,
     .skewCentre = 300.0f,
     .unit = Unit::Milliseconds,
     .decimals = 0},
    {.id = "mix",
     .name = "Dry/Wet",
     .shortName = "Mix",
     .min = 0.0f,
     .max = 100.0f,
     .def = 30.0f,
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
    {"mode", "The kind of space: Room is close and quick, Hall big and slow to build, Plate dense and "
             "bright, Space vast and moving."},
    {"decay", "How long the reverb rings in the middle frequencies before falling 60 dB."},
    {"size", "How big the space is: small is close and quick to build, big is distant and spacious."},
    {"predelay", "How long before the reverb starts, after the sound."},
    {"diffusion",
     "How quickly the echoes blur into a smooth wash: low is grainy and echoey, high is smooth at once."},
    {"early", "How loud the first reflections are against the tail: more is closer and clearer."},
    {"motion",
     "How much the tail moves and shimmers: none is still and exact, more is lusher and less metallic."},
    {"width", "The stereo width of the reverb, from mono at 0 % to extra wide at 200 %."},
    {"freeze", "Holds the reverb as it is, ringing forever, and lets nothing new in."},
    {"low.mult", "How long the lows ring, as a share of Decay: over 100 % they ring longer, under it they "
                 "thin out sooner."},
    {"low.freq", "Below this frequency, the reverb rings for Low Decay."},
    {"high.mult", "How long the highs ring, as a share of Decay: under 100 % the tail darkens as it fades, "
                  "like a real room."},
    {"high.freq", "Above this frequency, the reverb rings for High Decay."},
    {"filter", "Filters what goes into the reverb: less low rumble and hiss to ring."},
    {"filter.low", "Below this, the sound doesn't reach the reverb."},
    {"filter.high", "Above this, the sound doesn't reach the reverb."},
    {"duck", "How much the reverb dips while you play; it swells back up in the gaps."},
    {"duck.release", "How quickly the reverb comes back up once you stop."},
    {"mix", "Blends the original sound with the reverb; on a return track, set it to 100 %."},
    {"output", "The level after the reverb."},
};
constexpr auto kParams = describe(kSpecs, kDescriptions);
static_assert(allDescribed(kParams), "every parameter needs a description (SPEC.md)");

constexpr MeterSpec kMeters[] = {
    {.slot = Reverb::PeakInL, .id = "input_left", .name = "Input, left", .kind = MeterKind::Level},
    {.slot = Reverb::PeakInR, .id = "input_right", .name = "Input, right", .kind = MeterKind::Level},
    {.slot = Reverb::PeakOutL, .id = "output_left", .name = "Output, left", .kind = MeterKind::Level},
    {.slot = Reverb::PeakOutR, .id = "output_right", .name = "Output, right", .kind = MeterKind::Level},
    {.slot = Reverb::DuckDb, .id = "ducking", .name = "Ducking", .kind = MeterKind::Reduction},
};

constexpr DeviceInfo kInfo{
    .id = "reverb",
    .name = "Reverb",
    .category = "Delay & Reverb",
    .kind = DeviceKind::AudioEffect,
    .buses = {.inputChannels = 2, .outputChannels = 2},
    .params = kParams,
    .stateVersion = 1,
    .meters = kMeters,
};

// The spaces (SPEC: Ranges and limits, DSP design). Starting points, tuned by ear.
constexpr double kSpeedOfSound = 343.0;
struct ModeShape
{
    double fewestMetres, mostMetres; // the space's size from Size 0 to 100 % (0: a plate)
    double earlySpan;                // the early reflections' span, in mean lines
    double earlyCurve;               // how the taps bunch: over 1 early and close, under 1 spread
    double motionMs, motionHz;       // Motion's depth at 100 % and its base rate
    double diffuserScale;            // the input diffusers' lengths, against the shortest
    double diffuserGain;             // their gain at Diffusion 100 %
};
constexpr ModeShape kSpaces[] = {
    {3.0, 20.0, 2.0, 1.3, 0.3, 0.9, 1.0, 0.75},   // Room
    {15.0, 60.0, 1.5, 0.9, 0.6, 0.6, 1.3, 0.75},  // Hall
    {0.0, 0.0, 0.0, 1.0, 0.4, 1.0, 0.8, 0.8},     // Plate
    {40.0, 150.0, 1.2, 0.8, 1.2, 0.3, 1.5, 0.75}, // Space
};
constexpr double kLargestDiffuser = 3.0;
constexpr double kPlateShortest = 0.008, kPlateLongest = 0.025; // a plate's mean line (s)
constexpr double kLineRatios[Reverb::lines] = {0.617, 0.709, 0.793, 0.881, 0.997, 1.103, 1.229, 1.373};
constexpr double kLoopAllpassMs[Reverb::lines] = {0.31, 0.43, 0.53, 0.67, 0.79, 0.89, 1.03, 1.13};
constexpr double kLfoRatios[Reverb::lines] = {1.00, 1.13, 0.87, 1.31, 0.79, 1.19, 0.93, 1.07};
constexpr double kDiffuserMs[Reverb::maxChannels][Reverb::inputDiffusers] = {{1.37, 2.11, 3.29, 4.91},
                                                                             {1.53, 2.29, 3.07, 5.23}};
constexpr double kReferenceLine = 0.03;                // the mean line the input's level is set for (s)
constexpr float kEarlyLevel = 0.3f, kTailLevel = 0.4f; // the wet's parts, tuned by ear
constexpr double kGlideMs = 30.0;                      // each of Size's and Predelay's glides' two poles
constexpr double kDuckFloorDb = -40.0, kDuckRangeDb = 30.0, kDuckMaxDb = 30.0;

/// A repeatable fraction (0..1) for a tap: the early pattern is the same every time.
double scatter(double seed) noexcept
{
    const double v = std::sin(seed * 12.9898) * 43758.5453;
    return v - std::floor(v);
}

/// A first-order TPT low-pass's coefficients as a biquad (dsp::OnePoleLowpass), to draw it.
dsp::BiquadCoeffs onePole(double hz, double sampleRate)
{
    const double fc = std::clamp(hz, 1.0, 0.49 * sampleRate);
    const double g = std::tan(dsp::pi * fc / sampleRate);
    return {.b0 = g / (1.0 + g), .b1 = g / (1.0 + g), .b2 = 0.0, .a1 = (g - 1.0) / (1.0 + g), .a2 = 0.0};
}
} // namespace

Reverb::Reverb() : Device(kInfo) {}

// ---- pure functions ------------------------------------------------------------------------

double Reverb::metres(ModeKind mode, double size) noexcept
{
    const auto& m = kSpaces[static_cast<int>(mode)];
    if (m.mostMetres <= 0.0)
        return 0.0;
    return m.fewestMetres * std::pow(m.mostMetres / m.fewestMetres, std::clamp(size, 0.0, 1.0));
}

double Reverb::meanLine(ModeKind mode, double size) noexcept
{
    if (mode == ModeKind::Plate)
        return kPlateShortest * std::pow(kPlateLongest / kPlateShortest, std::clamp(size, 0.0, 1.0));
    return 0.5 * metres(mode, size) / kSpeedOfSound;
}

double Reverb::lineRatio(int i) noexcept
{
    return kLineRatios[std::clamp(i, 0, lines - 1)];
}

double Reverb::loopAllpass(int i) noexcept
{
    return kLoopAllpassMs[std::clamp(i, 0, lines - 1)] * 0.001;
}

double Reverb::bandDecay(double decay, double mult) noexcept
{
    return std::clamp(decay * mult, shortestDecay, longestDecay);
}

Reverb::BandGains Reverb::bandGains(const Bands& b, double loopSeconds) noexcept
{
    const auto gain = [&](double seconds) { return std::pow(10.0, -3.0 * loopSeconds / seconds); };
    return {.low = gain(bandDecay(b.decay, b.lowMult)),
            .mid = gain(bandDecay(b.decay, 1.0)),
            .high = gain(bandDecay(b.decay, b.highMult))};
}

double Reverb::decayAt(const Bands& b, double hz, double loopSeconds, double sampleRate) noexcept
{
    // The band filter: low = LP(x); mid = LP'(x - low); high = the rest.
    const auto g = bandGains(b, loopSeconds);
    const auto lowSplit = dsp::biquadResponse(onePole(b.lowHz, sampleRate), hz, sampleRate);
    const auto highSplit = dsp::biquadResponse(onePole(b.highHz, sampleRate), hz, sampleRate);
    const auto rest = 1.0 - lowSplit;
    const auto h = g.low * lowSplit + g.mid * rest * highSplit + g.high * rest * (1.0 - highSplit);
    const double level = std::abs(h);
    if (level >= 1.0)
        return longestDecay;
    return std::clamp(-3.0 * loopSeconds / std::log10(level), shortestDecay, longestDecay);
}

Reverb::Tap Reverb::earlyTap(ModeKind mode, int side, int i) noexcept
{
    if (i <= 0)
        return {.at = 0.0, .gain = side == 0 ? 1.0 : 0.85};
    const auto& m = kSpaces[static_cast<int>(mode)];
    const double seed = 1.0 + i + 17.0 * side + 101.0 * static_cast<int>(mode);
    const double place = std::pow(static_cast<double>(i) / (earlyTaps - 1), m.earlyCurve);
    const double at = std::clamp(place * (0.85 + 0.3 * scatter(seed)), 0.02, 1.0);
    const double sign = scatter(seed + 0.5) < 0.5 ? -1.0 : 1.0;
    return {.at = at, .gain = sign * 0.9 * (1.0 - 0.8 * at)};
}

double Reverb::earlySpan(ModeKind mode, double size) noexcept
{
    return kSpaces[static_cast<int>(mode)].earlySpan * meanLine(mode, size);
}

// ---- the engine ----------------------------------------------------------------------------

void Reverb::TapLine::prepare(int samples)
{
    int size = 1;
    while (size < samples + 2)
        size <<= 1;
    buffer.assign(static_cast<size_t>(size), 0.0f);
    mask = size - 1;
    write = 0;
}

void Reverb::TapLine::reset() noexcept
{
    std::fill(buffer.begin(), buffer.end(), 0.0f);
    write = 0;
}

float Reverb::TapLine::read(double delay) const noexcept
{
    const double d = std::clamp(delay, 0.0, static_cast<double>(mask - 1));
    const auto whole = static_cast<int>(d);
    const auto frac = static_cast<float>(d - whole);
    const float a = at(whole), b = at(whole + 1);
    return a + frac * (b - a);
}

void Reverb::prepare(const ProcessSpec& spec)
{
    sampleRate_ = spec.sampleRate > 0.0 ? spec.sampleRate : 48000.0;
    rampSamples_ = dsp::msToSamples(20.0, sampleRate_);
    slowSamples_ = dsp::msToSamples(50.0, sampleRate_);
    dipSamples_ = dsp::msToSamples(20.0, sampleRate_);
    duckAttack_ = static_cast<float>(1.0 - std::exp(-1.0 / (0.005 * sampleRate_)));
    for (auto* g : {&size_, &predelay_})
        g->setTime(kGlideMs, sampleRate_);
    // The longest line, with Motion on top; the predelay and the longest early span after it.
    const double longestLine = meanLine(ModeKind::Space, 1.0) * kLineRatios[lines - 1] + 0.002;
    const double longestInput = 0.5 + earlySpan(ModeKind::Space, 1.0) + 0.01;
    for (int i = 0; i < lines; ++i)
    {
        auto& l = lines_[static_cast<size_t>(i)];
        l.delay.prepare(longestLine * sampleRate_ + 64.0);
        l.allpass.length =
            std::max(1, static_cast<int>(std::lround(kLoopAllpassMs[i] * 0.001 * sampleRate_)));
        l.allpass.line.prepare(l.allpass.length + 1);
        l.lfo.prepare(sampleRate_);
    }
    for (int c = 0; c < maxChannels; ++c)
    {
        auto& s = sides_[static_cast<size_t>(c)];
        s.input.prepare(static_cast<int>(longestInput * sampleRate_) + 4);
        for (int k = 0; k < inputDiffusers; ++k)
            s.diffusers[static_cast<size_t>(k)].line.prepare(
                static_cast<int>(std::lround(kLargestDiffuser * kDiffuserMs[c][k] * 0.001 * sampleRate_)) +
                2);
        s.filter.prepare(sampleRate_);
    }
    analysis().setSampleRate(sampleRate_);
    reset();
}

void Reverb::clearNetwork() noexcept
{
    // The input diffusers' lengths are the mode's: set while everything is clear, so it can't click.
    for (int c = 0; c < maxChannels; ++c)
        for (int k = 0; k < inputDiffusers; ++k)
            sides_[static_cast<size_t>(c)].diffusers[static_cast<size_t>(k)].length =
                std::max(1, static_cast<int>(std::lround(kSpaces[static_cast<int>(mode_)].diffuserScale *
                                                         kDiffuserMs[c][k] * 0.001 * sampleRate_)));
    for (auto& l : lines_)
    {
        l.delay.reset();
        l.allpass.line.reset();
        l.lowSplit.reset();
        l.highSplit.reset();
    }
    for (auto& s : sides_)
    {
        s.input.reset();
        for (auto& d : s.diffusers)
            d.line.reset();
    }
}

void Reverb::reset() noexcept
{
    grid_.reset();
    clearNetwork();
    for (auto& s : sides_)
        s.filter.reset();
    for (int i = 0; i < lines; ++i)
        lines_[static_cast<size_t>(i)].lfo.reset(static_cast<double>(i) / lines);
    duckEnv_ = 0.0f;
    snap();
    fresh_ = true;
}

void Reverb::snap() noexcept
{
    mode_ = nextMode_ = static_cast<ModeKind>(params().getInt(Mode));
    clearNetwork();
    dip_.reset(1.0f);
    updateTargets(/*immediate*/ true);
    size_.snap(size_.target());
    predelay_.snap(predelay_.target());
    const double mean = meanLine(mode_, size_.current()) * sampleRate_;
    for (int i = 0; i < lines; ++i)
        lines_[static_cast<size_t>(i)].length = mean * kLineRatios[i];
    updateBands();
}

void Reverb::updateTargets(bool immediate) noexcept
{
    const auto& p = params();
    const int ramp = immediate ? 0 : rampSamples_, slow = immediate ? 0 : slowSamples_;
    diffusion_.setTarget(p.get(Diffusion) * 0.01f, ramp);
    early_.setTarget(p.get(Early) * 0.01f, ramp);
    motion_.setTarget(p.get(Motion) * 0.01f, ramp);
    width_.setTarget(p.get(Width) * 0.01f, ramp);
    freeze_.setTarget(p.getBool(Freeze) ? 1.0f : 0.0f, slow);
    lowMult_.setTarget(p.get(LowMult) * 0.01f, slow);
    highMult_.setTarget(p.get(HighMult) * 0.01f, slow);
    filter_.setTarget(p.getBool(Filter) ? 1.0f : 0.0f, ramp);
    duck_.setTarget(p.get(Duck) * 0.01f, ramp);
    mix_.setTarget(p.get(Mix) * 0.01f, ramp);
    output_.setTarget(dsp::dbToGain(p.get(Output)), ramp);
    decay_.setTarget(p.get(Decay), slow);
    lowFreq_.setTarget(p.get(LowFreq), slow);
    highFreq_.setTarget(p.get(HighFreq), slow);
    filterLow_.setTarget(p.get(FilterLow), ramp);
    filterHigh_.setTarget(p.get(FilterHigh), ramp);
    size_.setTarget(p.get(Size) * 0.01);
    predelay_.setTarget(p.get(Predelay) * 0.001 * sampleRate_);
    if (immediate)
    {
        for (auto* s : {&diffusion_, &early_, &motion_, &width_, &freeze_, &lowMult_, &highMult_, &filter_,
                        &duck_, &mix_, &output_})
            s->reset(s->target());
        for (auto* s : {&decay_, &lowFreq_, &highFreq_, &filterLow_, &filterHigh_})
            s->reset(s->target());
    }

    // A new mode: the reverb dips out, the network is cleared and retuned, and it comes back.
    const auto mode = static_cast<ModeKind>(p.getInt(Mode));
    if (mode != nextMode_ && !immediate)
    {
        nextMode_ = mode;
        dip_.setTarget(0.0f, dipSamples_);
    }

    // The ducking detector falls linearly in dB: from full ducking, back after Duck Release.
    const double releaseTau = p.get(DuckRelease) * 0.001 * (20.0 * std::log10(std::exp(1.0))) / kDuckRangeDb;
    duckRelease_ = static_cast<float>(1.0 - std::exp(-1.0 / (releaseTau * sampleRate_)));

    const double nyquistish = 0.45 * sampleRate_ - 1.0;
    for (auto& s : sides_)
        s.filter.set(std::min<double>(filterLow_.current(), nyquistish),
                     std::min<double>(filterHigh_.current(), nyquistish), 0.0);
    const auto& shape = kSpaces[static_cast<int>(mode_)];
    for (int i = 0; i < lines; ++i)
        lines_[static_cast<size_t>(i)].lfo.setRate(shape.motionHz * kLfoRatios[i]);
}

void Reverb::updateBands() noexcept
{
    const Bands b{.decay = decay_.current(),
                  .lowMult = lowMult_.current(),
                  .lowHz = lowFreq_.current(),
                  .highMult = highMult_.current(),
                  .highHz = highFreq_.current()};
    const double freeze = freeze_.current();
    for (int i = 0; i < lines; ++i)
    {
        auto& l = lines_[static_cast<size_t>(i)];
        const double loop = l.length / sampleRate_ + loopAllpass(i);
        auto g = bandGains(b, loop);
        // Frozen, nothing is lost.
        g.low += freeze * (1.0 - g.low);
        g.mid += freeze * (1.0 - g.mid);
        g.high += freeze * (1.0 - g.high);
        l.gains = g;
        l.lowSplit.setCutoff(b.lowHz, sampleRate_);
        l.highSplit.setCutoff(b.highHz, sampleRate_);
    }
}

void Reverb::process(ProcessContext& ctx) noexcept
{
    const int n = ctx.numSamples;
    if (n <= 0 || ctx.output.numChannels == 0)
        return;
    const int inChannels = std::min(ctx.input.numChannels, maxChannels);
    const int outChannels = std::min(ctx.output.numChannels, maxChannels);
    if (fresh_)
    {
        snap();
        fresh_ = false;
    }

    float peakIn[maxChannels] = {}, peakOut[maxChannels] = {}, duckMax = 0.0f;
    grid_.forEachSegment(
        n,
        [&](int offset, int length, bool isTick) noexcept
        {
            if (isTick)
            {
                updateTargets(false);
                updateBands();
            }
            for (int i = 0; i < length; ++i)
            {
                const float diffusion = diffusion_.next(), early = early_.next(), motion = motion_.next(),
                            width = width_.next(), freeze = freeze_.next(), filter = filter_.next(),
                            duck = duck_.next(), mix = mix_.next(), out = output_.next();
                lowMult_.next();
                highMult_.next();
                decay_.next();
                lowFreq_.next();
                highFreq_.next();
                filterLow_.next();
                filterHigh_.next();
                const double size = size_.next(), predelay = predelay_.next();

                // A new mode, once the reverb has dipped out.
                if (mode_ != nextMode_ && dip_.current() <= 0.0f)
                {
                    mode_ = nextMode_;
                    clearNetwork();
                    dip_.setTarget(1.0f, dipSamples_);
                }
                const float dip = dip_.next();
                const auto& shape = kSpaces[static_cast<int>(mode_)];

                const int at = offset + i;
                const float in[maxChannels] = {ctx.input.channel(0)[at], inChannels > 1
                                                                             ? ctx.input.channel(1)[at]
                                                                             : ctx.input.channel(0)[at]};
                const float level = std::max(std::abs(in[0]), std::abs(in[1]));
                duckEnv_ += (level > duckEnv_ ? duckAttack_ : duckRelease_) * (level - duckEnv_);

                // The space at this Size: the lines' lengths, the early span, the input's level.
                const double mean = meanLine(mode_, size);
                const double span = earlySpan(mode_, size) * sampleRate_;
                const auto inGain = static_cast<float>(0.5 * std::sqrt(mean / kReferenceLine));
                const float earlyOn = mode_ == ModeKind::Plate ? 0.0f : 1.0f;

                // Into the reverb: filtered, held back by Predelay; the early reflections; diffused.
                float earlyOut[maxChannels], diffused[maxChannels];
                for (int c = 0; c < maxChannels; ++c)
                {
                    auto& s = sides_[static_cast<size_t>(c)];
                    peakIn[c] = std::max(peakIn[c], std::abs(in[c]));
                    const float filtered = in[c] + filter * (s.filter.process(in[c]) - in[c]);
                    s.input.push(filtered * (1.0f - freeze));
                    float e = 0.0f;
                    if (earlyOn > 0.0f)
                        for (int k = 0; k < earlyTaps; ++k)
                        {
                            const auto tap = earlyTap(mode_, c, k);
                            e += static_cast<float>(tap.gain) * s.input.read(predelay + tap.at * span);
                        }
                    earlyOut[c] = e * kEarlyLevel;
                    float d = s.input.read(predelay) + 0.5f * earlyOut[c];
                    const auto g = static_cast<float>(shape.diffuserGain) * diffusion;
                    for (auto& diffuser : s.diffusers)
                        d = diffuser.process(d, g);
                    diffused[c] = d;
                }

                // The network: each line read (moving with Motion), through its all-pass and its
                // band decay, mixed back through the Householder matrix.
                float z[lines];
                float sum = 0.0f;
                const double depth = motion * shape.motionMs * 0.001 * sampleRate_;
                const float loopGain = 0.6f * diffusion;
                for (int k = 0; k < lines; ++k)
                {
                    auto& l = lines_[static_cast<size_t>(k)];
                    l.length = mean * kLineRatios[k] * sampleRate_;
                    const double wobble = depth * (1.0 + l.lfo.next().left) * 0.5;
                    // Read before this sample is written: one sample less.
                    const float r = l.delay.read(l.length - 1.0 + wobble);
                    const float a = l.allpass.process(r, loopGain);
                    const float low = l.lowSplit.process(a);
                    const float rest = a - low;
                    const float mid = l.highSplit.process(rest);
                    const float high = rest - mid;
                    z[k] = static_cast<float>(l.gains.low) * low + static_cast<float>(l.gains.mid) * mid +
                           static_cast<float>(l.gains.high) * high;
                    sum += z[k];
                }
                const float householder = 2.0f / lines * sum;
                for (int k = 0; k < lines; ++k)
                {
                    const float input = diffused[k % 2] * inGain * (k < lines / 2 ? 1.0f : -1.0f);
                    lines_[static_cast<size_t>(k)].delay.push(
                        std::clamp(input + z[k] - householder, -8.0f, 8.0f));
                }
                const float tail[maxChannels] = {kTailLevel * (z[0] - z[2] + z[4] - z[6]),
                                                 kTailLevel * (z[1] - z[3] + z[5] - z[7])};

                // The wet: the early reflections at Early's level and the tail, widened, ducked.
                float wet[maxChannels];
                for (int c = 0; c < maxChannels; ++c)
                    wet[c] = earlyOn * early * earlyOut[c] + tail[c];
                const float mid = 0.5f * (wet[0] + wet[1]), side = 0.5f * (wet[0] - wet[1]) * width;
                float duckDb = 0.0f;
                if (duck > 0.0f)
                {
                    const float overDb = dsp::gainToDb(duckEnv_) - static_cast<float>(kDuckFloorDb);
                    duckDb = duck * static_cast<float>(kDuckMaxDb) *
                             std::clamp(overDb / static_cast<float>(kDuckRangeDb), 0.0f, 1.0f);
                }
                duckMax = std::max(duckMax, duckDb);
                const float wetGain = dsp::dbToGain(-duckDb) * dip;
                const float wide[maxChannels] = {mid + side, mid - side};
                for (int c = 0; c < outChannels; ++c)
                {
                    const float y = ((1.0f - mix) * in[c] + mix * wetGain * wide[c]) * out;
                    ctx.output.channel(c)[at] = y;
                    peakOut[c] = std::max(peakOut[c], std::abs(y));
                }
            }
        });

    analysis().publishPeak(PeakInL, peakIn[0]);
    analysis().publishPeak(PeakInR, peakIn[1]);
    analysis().publishPeak(PeakOutL, peakOut[0]);
    analysis().publishPeak(PeakOutR, peakOut[1]);
    analysis().publishPeak(DuckDb, duckMax);
}

double Reverb::tailSeconds() const noexcept
{
    const auto& p = params();
    if (p.getBool(Freeze))
        return longestDecay;
    const auto mode = static_cast<ModeKind>(p.getInt(Mode));
    const double decay = p.get(Decay);
    const double longest = std::max({bandDecay(decay, 1.0), bandDecay(decay, p.get(LowMult) * 0.01),
                                     bandDecay(decay, p.get(HighMult) * 0.01)});
    return std::min(longestDecay,
                    longest + p.get(Predelay) * 0.001 + earlySpan(mode, p.get(Size) * 0.01) + 0.05);
}

const DeviceFactory& reverbFactory()
{
    static const DeviceFactory factory{
        .info = []() -> const DeviceInfo& { return kInfo; },
        .create = []() -> std::unique_ptr<Device> { return std::make_unique<Reverb>(); },
    };
    return factory;
}
} // namespace fd::devices
