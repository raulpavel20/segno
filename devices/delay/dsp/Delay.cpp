#include "Delay.h"

#include "fd/dsp/Math.h"
#include "fd/dsp/Waveshapers.h"

#include <algorithm>
#include <cmath>

namespace fd::devices
{
namespace
{
constexpr std::string_view kModes[] = {"Clean", "Tape", "Analog"};
constexpr std::string_view kDivisions[] = {"1/64",  "1/32T", "1/32", "1/16T", "1/16",  "1/8T",
                                           "1/16D", "1/8",   "1/4T", "1/8D",  "1/4",   "1/2T",
                                           "1/4D",  "1/2",   "1/2D", "1 bar", "2 bars"};
// Each note value's length in beats (quarter notes); bars are counted apart.
constexpr double kDivisionBeats[] = {1.0 / 16, 1.0 / 12, 1.0 / 8, 1.0 / 6, 1.0 / 4, 1.0 / 3, 3.0 / 8, 1.0 / 2,
                                     2.0 / 3,  3.0 / 4,  1.0,     4.0 / 3, 3.0 / 2, 2.0,     3.0};
constexpr std::string_view kStereo[] = {"Stereo", "Ping-pong", "Mid-side"};
constexpr std::string_view kShapes[] = {"Sine", "Triangle", "Drift"};
constexpr std::string_view kTransitions[] = {"Repitch", "Fade"};

constexpr std::array<ParamSpec, Delay::NumParams> kSpecs{{
    {.id = "mode",
     .name = "Mode",
     .shortName = "Mode",
     .kind = ParamKind::Choice,
     .min = 0.0f,
     .max = 2.0f,
     .def = 0.0f,
     .choices = kModes},
    {.id = "sync",
     .name = "Sync",
     .shortName = "Sync",
     .kind = ParamKind::Toggle,
     .min = 0.0f,
     .max = 1.0f,
     .def = 1.0f},
    {.id = "time.left",
     .name = "Left",
     .shortName = "Left",
     .min = 1.0f,
     .max = 5000.0f,
     .def = 375.0f,
     .skewCentre = 250.0f,
     .unit = Unit::Milliseconds,
     .decimals = 0},
    {.id = "time.right",
     .name = "Right",
     .shortName = "Right",
     .min = 1.0f,
     .max = 5000.0f,
     .def = 375.0f,
     .skewCentre = 250.0f,
     .unit = Unit::Milliseconds,
     .decimals = 0},
    {.id = "div.left",
     .name = "Left Sync",
     .shortName = "Left",
     .kind = ParamKind::Choice,
     .min = 0.0f,
     .max = 16.0f,
     .def = 9.0f,
     .choices = kDivisions},
    {.id = "div.right",
     .name = "Right Sync",
     .shortName = "Right",
     .kind = ParamKind::Choice,
     .min = 0.0f,
     .max = 16.0f,
     .def = 9.0f,
     .choices = kDivisions},
    {.id = "link",
     .name = "Link",
     .shortName = "Link",
     .kind = ParamKind::Toggle,
     .min = 0.0f,
     .max = 1.0f,
     .def = 1.0f},
    {.id = "swing",
     .name = "Swing",
     .shortName = "Swing",
     .min = -33.0f,
     .max = 33.0f,
     .def = 0.0f,
     .unit = Unit::Percent,
     .decimals = 0},
    {.id = "stereo",
     .name = "Stereo",
     .shortName = "Stereo",
     .kind = ParamKind::Choice,
     .min = 0.0f,
     .max = 2.0f,
     .def = 0.0f,
     .choices = kStereo},
    {.id = "feedback",
     .name = "Feedback",
     .shortName = "Amount",
     .min = 0.0f,
     .max = 100.0f,
     .def = 35.0f,
     .unit = Unit::Percent,
     .decimals = 0},
    {.id = "freeze",
     .name = "Freeze",
     .shortName = "Freeze",
     .kind = ParamKind::Toggle,
     .min = 0.0f,
     .max = 1.0f},
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
     .def = 120.0f,
     .skewCentre = 200.0f,
     .unit = Unit::Hertz,
     .smoothing = {SmoothingKind::Multiplicative, 20.0f},
     .decimals = 0},
    {.id = "filter.high",
     .name = "High Cut",
     .shortName = "High Cut",
     .min = 500.0f,
     .max = 20000.0f,
     .def = 6000.0f,
     .skewCentre = 4000.0f,
     .unit = Unit::Hertz,
     .smoothing = {SmoothingKind::Multiplicative, 20.0f},
     .decimals = 0},
    {.id = "drive",
     .name = "Drive",
     .shortName = "Drive",
     .min = 0.0f,
     .max = 100.0f,
     .def = 0.0f,
     .unit = Unit::Percent,
     .decimals = 0},
    {.id = "wobble",
     .name = "Wobble",
     .shortName = "Wobble",
     .min = 0.0f,
     .max = 100.0f,
     .def = 0.0f,
     .unit = Unit::Percent,
     .decimals = 0},
    {.id = "mod.shape",
     .name = "Mod Shape",
     .shortName = "Shape",
     .kind = ParamKind::Choice,
     .min = 0.0f,
     .max = 2.0f,
     .def = 0.0f,
     .choices = kShapes},
    {.id = "mod.rate",
     .name = "Mod Rate",
     .shortName = "Rate",
     .min = 0.01f,
     .max = 10.0f,
     .def = 0.5f,
     .skewCentre = 0.5f,
     .unit = Unit::Hertz,
     .smoothing = {SmoothingKind::Multiplicative, 20.0f},
     .decimals = 2},
    {.id = "mod.time",
     .name = "Mod Time",
     .shortName = "Time",
     .min = 0.0f,
     .max = 100.0f,
     .def = 0.0f,
     .unit = Unit::Percent,
     .decimals = 0},
    {.id = "mod.filter",
     .name = "Mod Filter",
     .shortName = "Filter",
     .min = 0.0f,
     .max = 100.0f,
     .def = 0.0f,
     .unit = Unit::Percent,
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
    {.id = "transition",
     .name = "Time Change",
     .shortName = "Time Change",
     .kind = ParamKind::Choice,
     .min = 0.0f,
     .max = 1.0f,
     .def = 0.0f,
     .choices = kTransitions,
     .flags = ParamFlags::None},
    {.id = "mix",
     .name = "Dry/Wet",
     .shortName = "Mix",
     .min = 0.0f,
     .max = 100.0f,
     .def = 35.0f,
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
    {"mode", "The echoes' character: Clean is exact, Tape darkens and saturates like tape, Analog is darker "
             "and softer still."},
    {"sync", "Times in note values, following the song's tempo, instead of milliseconds."},
    {"time.left", "How long before the left side echoes, in milliseconds."},
    {"time.right", "How long before the right side echoes, in milliseconds."},
    {"div.left", "The left side's time as a note value, while Sync is on."},
    {"div.right", "The right side's time as a note value, while Sync is on."},
    {"link", "The right side takes the left side's time."},
    {"swing",
     "Moves the right side's synced time off the grid by a share of itself, for a swung, looser feel."},
    {"stereo", "How the sides echo: each on its own side, bouncing from side to side, or as mid and side."},
    {"feedback", "How much of each echo comes back as the next: more feedback, more repeats."},
    {"freeze", "Holds what's in the echoes, repeating it forever, and lets nothing new in."},
    {"filter", "Turns the feedback filter on: each repeat loses a little more of the lows and highs."},
    {"filter.low", "Below this, each repeat thins out."},
    {"filter.high", "Above this, each repeat darkens."},
    {"drive",
     "Saturates what feeds back, as the mode does: repeats thicken and stay bounded at any feedback."},
    {"wobble", "Slow and quick wavers in the echoes' time, like tape or a worn machine."},
    {"mod.shape", "The modulation's wave."},
    {"mod.rate", "How fast the modulation moves."},
    {"mod.time", "How far the modulation moves the echoes' time: a chorus-like shimmer."},
    {"mod.filter", "How far the modulation sweeps the feedback filter."},
    {"duck", "How much the echoes dip while you play; they come back up in the gaps."},
    {"duck.release", "How quickly the echoes come back up once you stop."},
    {"transition", "What a time change sounds like: Repitch glides, like tape speeding up or slowing down; "
                   "Fade crossfades."},
    {"mix", "Blends the original signal with the echoes."},
    {"output", "The level after the delay."},
};
constexpr auto kParams = describe(kSpecs, kDescriptions);
static_assert(allDescribed(kParams), "every parameter needs a description (SPEC.md)");

constexpr MeterSpec kMeters[] = {
    {.slot = Delay::PeakInL, .id = "input_left", .name = "Input, left", .kind = MeterKind::Level},
    {.slot = Delay::PeakInR, .id = "input_right", .name = "Input, right", .kind = MeterKind::Level},
    {.slot = Delay::PeakOutL, .id = "output_left", .name = "Output, left", .kind = MeterKind::Level},
    {.slot = Delay::PeakOutR, .id = "output_right", .name = "Output, right", .kind = MeterKind::Level},
    {.slot = Delay::DuckDb, .id = "ducking", .name = "Ducking", .kind = MeterKind::Reduction},
};

constexpr DeviceInfo kInfo{
    .id = "delay",
    .name = "Delay",
    .category = "Delay & Reverb",
    .kind = DeviceKind::AudioEffect,
    .buses = {.inputChannels = 2, .outputChannels = 2},
    .params = kParams,
    .stateVersion = 1,
    .meters = kMeters,
};

// The modes' tone in the loop (SPEC: DSP design).
constexpr double kTapeShelfHz = 100.0, kTapeShelfDb = 1.0, kTapeHighHz = 9000.0, kAnalogHighHz = 4500.0;
constexpr double kButterworthQ = 0.7071067811865476;
constexpr double kDuckFloorDb = -40.0, kDuckRangeDb = 30.0, kDuckMaxDb = 30.0;
constexpr double kModTimeShare = 0.02, kModTimeMaxSeconds = 0.005, kModFilterOctaves = 1.5;
constexpr double kWobbleShare = 0.006, kWowHz = 0.6, kFlutterHz = 6.0, kFlutterShare = 0.1;
constexpr double kMaxGlide = 0.9; // Repitch reads at 0.1x to 1.9x speed at most: no runaway swoops

dsp::BiquadCoeffs tapeShelf(double rate)
{
    return dsp::designBiquad(dsp::BiquadType::LowShelf, rate, kTapeShelfHz, kButterworthQ, kTapeShelfDb);
}
dsp::BiquadCoeffs lowpass(double rate, double hz)
{
    return dsp::designBiquad(dsp::BiquadType::Lowpass, rate, std::min(hz, 0.45 * rate), kButterworthQ);
}
} // namespace

Delay::Delay() : Device(kInfo) {}

// ---- pure functions ------------------------------------------------------------------------

double Delay::divisionBeats(int index, double beatsPerBar) noexcept
{
    index = std::clamp(index, 0, numDivisions - 1);
    if (index == numDivisions - 2)
        return beatsPerBar;
    if (index == numDivisions - 1)
        return 2.0 * beatsPerBar;
    return kDivisionBeats[index];
}

Delay::Times Delay::times(const TimeSettings& s, double tempoBpm, double beatsPerBar) noexcept
{
    const double beat = 60.0 / (tempoBpm > 0.0 ? tempoBpm : 120.0);
    Times t;
    if (s.sync)
    {
        t.left = divisionBeats(s.leftDiv, beatsPerBar) * beat;
        t.right = divisionBeats(s.link ? s.leftDiv : s.rightDiv, beatsPerBar) * beat;
        t.right *= 1.0 + std::clamp(s.swingPercent, -33.0, 33.0) / 100.0;
    }
    else
    {
        t.left = s.leftMs / 1000.0;
        t.right = (s.link ? s.leftMs : s.rightMs) / 1000.0;
    }
    t.left = std::clamp(t.left, 0.001, maxSeconds);
    t.right = std::clamp(t.right, 0.001, maxSeconds);
    return t;
}

double Delay::passGain(ModeKind mode, bool filter, double lowHz, double highHz, double sampleRate) noexcept
{
    constexpr double f = 1000.0;
    double g =
        filter ? dsp::FeedbackPath::magnitude(f, lowHz, std::min(highHz, 0.45 * sampleRate - 1.0), sampleRate)
               : 1.0;
    if (mode == ModeKind::Tape)
        g *= dsp::biquadMagnitude(tapeShelf(sampleRate), f, sampleRate) *
             dsp::biquadMagnitude(lowpass(sampleRate, kTapeHighHz), f, sampleRate);
    if (mode == ModeKind::Analog)
        g *= dsp::biquadMagnitude(lowpass(sampleRate, kAnalogHighHz), f, sampleRate);
    return g;
}

int Delay::repeats(const Times& t, StereoKind stereo, double feedback, double passGain, double horizon,
                   std::span<Repeat> out) noexcept
{
    const double step = std::clamp(feedback, 0.0, 1.0) * passGain;
    int n = 0;
    const auto add = [&](double at, int lane, double level)
    {
        if (n < static_cast<int>(out.size()) && at <= horizon && level >= 1e-3)
        {
            out[static_cast<size_t>(n++)] = {at, lane, level};
            return true;
        }
        return false;
    };
    if (stereo == StereoKind::PingPong)
    {
        // Into the left; each echo then crosses to the other side.
        double at = t.left, level = 1.0;
        for (int k = 0; add(at, k % 2, level); ++k)
        {
            at += k % 2 == 0 ? t.right : t.left;
            level *= step;
        }
        return n;
    }
    // Each side (or mid and side) on its own: interleaved by time.
    double atL = t.left, atR = t.right, levelL = 1.0, levelR = 1.0;
    for (;;)
    {
        const bool left = atL <= atR;
        if (!add(left ? atL : atR, left ? 0 : 1, left ? levelL : levelR))
        {
            // One lane ran out; finish the other.
            if (left)
                while (add(atR, 1, levelR))
                {
                    atR += t.right;
                    levelR *= step;
                }
            else
                while (add(atL, 0, levelL))
                {
                    atL += t.left;
                    levelL *= step;
                }
            return n;
        }
        if (left)
        {
            atL += t.left;
            levelL *= step;
        }
        else
        {
            atR += t.right;
            levelR *= step;
        }
    }
}

// ---- the engine ----------------------------------------------------------------------------

void Delay::prepare(const ProcessSpec& spec)
{
    sampleRate_ = spec.sampleRate > 0.0 ? spec.sampleRate : 48000.0;
    rampSamples_ = dsp::msToSamples(20.0, sampleRate_);
    modeSamples_ = dsp::msToSamples(30.0, sampleRate_);
    fadeSamples_ = dsp::msToSamples(50.0, sampleRate_);
    dipSamples_ = dsp::msToSamples(15.0, sampleRate_);
    glide_ = 1.0 - std::exp(-1.0 / (0.1 * sampleRate_));
    modCoef_ = static_cast<float>(1.0 - std::exp(-1.0 / (0.01 * sampleRate_)));
    duckAttack_ = static_cast<float>(1.0 - std::exp(-1.0 / (0.005 * sampleRate_)));
    // Room for the longest time with its modulation and wobble on top.
    const double maxDelay = maxSeconds * sampleRate_ * (1.0 + kWobbleShare + kModTimeShare) + 64.0;
    for (auto& c : ch_)
    {
        c.line.prepare(maxDelay);
        c.path.prepare(sampleRate_);
        c.tapeShelf.setCoeffs(tapeShelf(sampleRate_));
        c.tapeHigh.setCoeffs(lowpass(sampleRate_, kTapeHighHz));
        c.analogHigh.setCoeffs(lowpass(sampleRate_, kAnalogHighHz));
    }
    for (auto* lfo : {&mod_, &wow_, &flutter_})
        lfo->prepare(sampleRate_);
    wow_.setShape(dsp::ModulationLfo::Shape::Wander);
    wow_.setRate(kWowHz);
    flutter_.setShape(dsp::ModulationLfo::Shape::Sine);
    flutter_.setRate(kFlutterHz);
    mod_.setStereo(0.25);
    analysis().setSampleRate(sampleRate_);
    reset();
}

void Delay::reset() noexcept
{
    grid_.reset();
    for (auto& c : ch_)
    {
        c.line.reset();
        c.path.reset();
        c.tapeShelf.reset();
        c.tapeHigh.reset();
        c.analogHigh.reset();
        c.fadeLeft = 0;
    }
    for (auto* lfo : {&mod_, &wow_, &flutter_})
        lfo->reset(0.0);
    modL_ = modR_ = 0.0f;
    duckEnv_ = 0.0f;
    snap();
    fresh_ = true;
}

void Delay::snap() noexcept
{
    stereo_ = nextStereo_ = static_cast<StereoKind>(params().getInt(Stereo));
    dip_.reset(1.0f);
    updateTargets(/*immediate*/ true);
    for (auto& c : ch_)
    {
        c.delay = c.headA = c.headB = c.target;
        c.fadeLeft = 0;
    }
    updateFilters(0.0f);
}

void Delay::updateTargets(bool immediate) noexcept
{
    const auto& p = params();
    const int ramp = immediate ? 0 : rampSamples_;
    const int modeRamp = immediate ? 0 : modeSamples_;
    feedback_.setTarget(p.get(Feedback) * 0.01f, ramp);
    freeze_.setTarget(p.getBool(Freeze) ? 1.0f : 0.0f, ramp);
    filter_.setTarget(p.getBool(Filter) ? 1.0f : 0.0f, ramp);
    const auto mode = static_cast<ModeKind>(p.getInt(Mode));
    tape_.setTarget(mode == ModeKind::Tape ? 1.0f : 0.0f, modeRamp);
    analog_.setTarget(mode == ModeKind::Analog ? 1.0f : 0.0f, modeRamp);
    drive_.setTarget(p.get(Drive) * 0.01f, ramp);
    wobble_.setTarget(p.get(Wobble) * 0.01f, ramp);
    modTime_.setTarget(p.get(ModTime) * 0.01f, ramp);
    modFilter_.setTarget(p.get(ModFilter) * 0.01f, ramp);
    duck_.setTarget(p.get(Duck) * 0.01f, ramp);
    mix_.setTarget(p.get(Mix) * 0.01f, ramp);
    output_.setTarget(dsp::dbToGain(p.get(Output)), ramp);
    low_.setTarget(p.get(FilterLow), ramp);
    high_.setTarget(p.get(FilterHigh), ramp);
    if (immediate)
        for (auto* s : {&feedback_, &freeze_, &filter_, &tape_, &analog_, &drive_, &wobble_, &modTime_,
                        &modFilter_, &duck_, &mix_, &output_})
            s->reset(s->target());
    if (immediate)
    {
        low_.reset(low_.target());
        high_.reset(high_.target());
    }
    // The envelope falls linearly in dB: from full ducking, the echoes are back after Duck Release.
    const double releaseTau = p.get(DuckRelease) * 0.001 * (20.0 * std::log10(std::exp(1.0))) / kDuckRangeDb;
    duckRelease_ = static_cast<float>(1.0 - std::exp(-1.0 / (releaseTau * sampleRate_)));

    static constexpr dsp::ModulationLfo::Shape kLfo[] = {dsp::ModulationLfo::Shape::Sine,
                                                         dsp::ModulationLfo::Shape::Triangle,
                                                         dsp::ModulationLfo::Shape::Wander};
    mod_.setShape(kLfo[std::clamp(p.getInt(ModShape), 0, 2)]);
    mod_.setRate(p.get(ModRate));

    // A new stereo mode: the echoes dip, the routing changes at the bottom, they come back.
    const auto stereo = static_cast<StereoKind>(p.getInt(Stereo));
    if (stereo != nextStereo_ && !immediate)
    {
        nextStereo_ = stereo;
        dip_.setTarget(0.0f, dipSamples_);
    }

    const TimeSettings s{p.getBool(Sync),    p.get(TimeLeft), p.get(TimeRight), p.getInt(DivLeft),
                         p.getInt(DivRight), p.getBool(Link), p.get(Swing)};
    const auto t = times(s, tempo_, beatsPerBar_);
    // Read before the newest sample is written: one sample less (see process()).
    ch_[0].target =
        std::max(t.left * sampleRate_ - 1.0, static_cast<double>(dsp::ModulatedDelay::minimumDelay));
    ch_[1].target =
        std::max(t.right * sampleRate_ - 1.0, static_cast<double>(dsp::ModulatedDelay::minimumDelay));
}

void Delay::updateFilters(float modulation) noexcept
{
    const double octaves = kModFilterOctaves * modFilter_.current() * modulation;
    const double scale = std::exp2(octaves);
    const double nyquistish = 0.45 * sampleRate_ - 1.0;
    const double low = std::clamp(low_.current() * scale, 10.0, nyquistish);
    const double high = std::clamp(high_.current() * scale, 20.0, nyquistish);
    for (auto& c : ch_)
        c.path.set(low, high, 0.0);
}

float Delay::loop(Channel& c, float x, float tape, float analog, float drive, float filter) noexcept
{
    // The filter (always running, so turning it on has no transient), then the mode's tone.
    const float filtered = c.path.process(x);
    x += filter * (filtered - x);
    const float taped = c.tapeHigh.process(c.tapeShelf.process(x));
    x += tape * (taped - x);
    const float dark = c.analogHigh.process(x);
    x += analog * (dark - x);
    if (drive > 0.0f)
    {
        // Unity for small signals, bounded for big ones; Analog clips earlier, Tape with its curve.
        const float g = 1.0f + drive * (1.0f + analog);
        const auto gx = static_cast<double>(g * x);
        const float soft = static_cast<float>(dsp::shape(dsp::ShaperCurve::Soft, gx)) / g;
        const float tapeCurve = static_cast<float>(dsp::shape(dsp::ShaperCurve::Tape, gx)) / g;
        const float sat = soft + tape * (tapeCurve - soft);
        x += drive * (sat - x);
    }
    return std::clamp(x, -4.0f, 4.0f);
}

void Delay::process(ProcessContext& ctx) noexcept
{
    const int n = ctx.numSamples;
    if (n <= 0 || ctx.output.numChannels == 0)
        return;
    const int inChannels = std::min(ctx.input.numChannels, maxChannels);
    const int outChannels = std::min(ctx.output.numChannels, maxChannels);
    tempo_ = ctx.transport.tempoBpm > 0.0 ? ctx.transport.tempoBpm : 120.0;
    beatsPerBar_ = ctx.transport.timeSigDenominator > 0
                       ? ctx.transport.timeSigNumerator * 4.0 / ctx.transport.timeSigDenominator
                       : 4.0;
    if (fresh_)
    {
        // The first block since a reset: start at the song's tempo rather than glide to it.
        snap();
        fresh_ = false;
    }
    const bool fade = params().getInt(Transition) == 1;

    float peakIn[maxChannels] = {}, peakOut[maxChannels] = {}, duckMax = 0.0f;
    grid_.forEachSegment(
        n,
        [&](int offset, int length, bool isTick) noexcept
        {
            if (isTick)
            {
                updateTargets(false);
                updateFilters(modL_);
            }
            for (int i = 0; i < length; ++i)
            {
                const float fb = feedback_.next(), freeze = freeze_.next(), filter = filter_.next(),
                            tape = tape_.next(), analog = analog_.next(), drive = drive_.next(),
                            wobble = wobble_.next(), modTime = modTime_.next();
                modFilter_.next();
                const float duck = duck_.next(), mix = mix_.next(), out = output_.next();
                low_.next();
                high_.next();
                if (stereo_ != nextStereo_ && dip_.current() <= 0.0f)
                {
                    stereo_ = nextStereo_;
                    dip_.setTarget(1.0f, dipSamples_);
                }
                const float dip = dip_.next();

                // The modulation, smoothed so a shape change never steps; the wobble.
                const auto m = mod_.next();
                modL_ += modCoef_ * (m.left - modL_);
                modR_ += modCoef_ * (m.right - modR_);
                const float waver =
                    wow_.next().left + static_cast<float>(kFlutterShare) * flutter_.next().left;

                const int at = offset + i;
                const float inL = ctx.input.channel(0)[at];
                const float inR = inChannels > 1 ? ctx.input.channel(1)[at] : inL;
                peakIn[0] = std::max(peakIn[0], std::abs(inL));
                peakIn[1] = std::max(peakIn[1], std::abs(inR));
                const float level = std::max(std::abs(inL), std::abs(inR));
                duckEnv_ += (level > duckEnv_ ? duckAttack_ : duckRelease_) * (level - duckEnv_);

                // Read each line (before this sample is written), moving as the transition says.
                float wet[maxChannels];
                for (int c = 0; c < maxChannels; ++c)
                {
                    auto& h = ch_[static_cast<size_t>(c)];
                    double base;
                    if (fade)
                    {
                        if (h.fadeLeft == 0 && std::abs(h.target - h.headA) > 0.5)
                        {
                            h.headB = h.target;
                            h.fadeLeft = fadeSamples_;
                        }
                        if (h.fadeLeft == 0)
                            h.delay = h.headA; // where Repitch starts from, if it's chosen next
                        base = h.headA;
                    }
                    else
                    {
                        h.delay += std::clamp((h.target - h.delay) * glide_, -kMaxGlide, kMaxGlide);
                        h.headA = h.delay;
                        base = h.delay;
                    }
                    const double lfo = c == 0 ? modL_ : modR_;
                    const auto offsetFor = [&](double d)
                    {
                        return std::min(kModTimeShare * d, kModTimeMaxSeconds * sampleRate_) * modTime * lfo +
                               d * kWobbleShare * wobble * waver;
                    };
                    if (fade && h.fadeLeft > 0)
                    {
                        const double t = 1.0 - static_cast<double>(h.fadeLeft) / fadeSamples_;
                        const float a = h.line.read(h.headA + offsetFor(h.headA));
                        const float b = h.line.read(h.headB + offsetFor(h.headB));
                        wet[c] = static_cast<float>(std::cos(t * dsp::pi * 0.5) * a +
                                                    std::sin(t * dsp::pi * 0.5) * b);
                        if (--h.fadeLeft == 0)
                            h.headA = h.delay = h.headB;
                    }
                    else
                        wet[c] = h.line.read(base + offsetFor(base));
                }

                // Into the lines: the input, and what feeds back, routed as the stereo mode says.
                float into[maxChannels], back[maxChannels];
                switch (stereo_)
                {
                case StereoKind::Stereo:
                    into[0] = inL;
                    into[1] = inR;
                    back[0] = wet[0];
                    back[1] = wet[1];
                    break;
                case StereoKind::PingPong:
                    into[0] = 0.5f * (inL + inR);
                    into[1] = 0.0f;
                    back[0] = wet[1];
                    back[1] = wet[0];
                    break;
                case StereoKind::MidSide:
                    into[0] = 0.5f * (inL + inR);
                    into[1] = 0.5f * (inL - inR);
                    back[0] = wet[0];
                    back[1] = wet[1];
                    break;
                }
                const float gain = fb + freeze * (1.0f - fb);
                for (int c = 0; c < maxChannels; ++c)
                {
                    auto& h = ch_[static_cast<size_t>(c)];
                    const float shaped = loop(h, back[c], tape, analog, drive, filter);
                    // Frozen: what's held goes round unchanged, and nothing new comes in.
                    const float fed = shaped + freeze * (back[c] - shaped);
                    h.line.push(into[c] * (1.0f - freeze) + gain * fed);
                }

                // Out: mid and side back to left and right, ducked, mixed.
                float wetL = wet[0], wetR = wet[1];
                if (stereo_ == StereoKind::MidSide)
                {
                    wetL = wet[0] + wet[1];
                    wetR = wet[0] - wet[1];
                }
                float duckDb = 0.0f;
                if (duck > 0.0f)
                {
                    const float overDb = dsp::gainToDb(duckEnv_) - static_cast<float>(kDuckFloorDb);
                    duckDb = duck * static_cast<float>(kDuckMaxDb) *
                             std::clamp(overDb / static_cast<float>(kDuckRangeDb), 0.0f, 1.0f);
                }
                duckMax = std::max(duckMax, duckDb);
                const float wetGain = dsp::dbToGain(-duckDb) * dip;
                const float outL = ((1.0f - mix) * inL + mix * wetGain * wetL) * out;
                const float outR = ((1.0f - mix) * inR + mix * wetGain * wetR) * out;
                ctx.output.channel(0)[at] = outL;
                peakOut[0] = std::max(peakOut[0], std::abs(outL));
                if (outChannels > 1)
                {
                    ctx.output.channel(1)[at] = outR;
                    peakOut[1] = std::max(peakOut[1], std::abs(outR));
                }
            }
        });

    analysis().publishPeak(PeakInL, peakIn[0]);
    analysis().publishPeak(PeakInR, peakIn[1]);
    analysis().publishPeak(PeakOutL, peakOut[0]);
    analysis().publishPeak(PeakOutR, peakOut[1]);
    analysis().publishPeak(DuckDb, duckMax);
    analysis().publish(TempoBpm, static_cast<float>(tempo_));
    analysis().publish(BeatsPerBar, static_cast<float>(beatsPerBar_));
}

double Delay::tailSeconds() const noexcept
{
    const auto& p = params();
    if (p.getBool(Freeze))
        return 60.0;
    const TimeSettings s{p.getBool(Sync),    p.get(TimeLeft), p.get(TimeRight), p.getInt(DivLeft),
                         p.getInt(DivRight), p.getBool(Link), p.get(Swing)};
    const auto t = times(s, tempo_, beatsPerBar_);
    const double pass = static_cast<StereoKind>(p.getInt(Stereo)) == StereoKind::PingPong
                            ? t.left + t.right
                            : std::max(t.left, t.right);
    const double fb = p.get(Feedback) * 0.01;
    if (fb < 1e-3)
        return pass + 0.1;
    // Passes until the echoes are 60 dB down, and one more, at most a minute.
    const double passes = fb >= 1.0 ? 1e9 : 3.0 / std::log10(1.0 / fb);
    return std::min(60.0, (passes + 1.0) * pass + 0.1);
}

const DeviceFactory& delayFactory()
{
    static const DeviceFactory factory{
        .info = []() -> const DeviceInfo& { return kInfo; },
        .create = []() -> std::unique_ptr<Device> { return std::make_unique<Delay>(); },
    };
    return factory;
}
} // namespace fd::devices
