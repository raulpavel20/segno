#include "PhaserFlanger.h"

#include "fd/dsp/Math.h"

#include <algorithm>
#include <cmath>
#include <complex>

namespace fd::devices
{
namespace
{
constexpr std::string_view kModes[] = {"Phaser", "Flanger", "Doubler"};
constexpr std::string_view kShapes[] = {"Sine",   "Triangle", "Saw Up", "Saw Down",
                                        "Square", "Random",   "Drift"};
constexpr std::string_view kStereo[] = {"Offset", "Spin"};

constexpr std::array<ParamSpec, PhaserFlanger::NumParams> kSpecs{{
    {.id = "mode",
     .name = "Mode",
     .shortName = "Mode",
     .kind = ParamKind::Choice,
     .min = 0.0f,
     .max = 2.0f,
     .def = 0.0f,
     .choices = kModes},
    {.id = "phaser.center",
     .name = "Center",
     .shortName = "Center",
     .min = 50.0f,
     .max = 15000.0f,
     .def = 800.0f,
     .skewCentre = 1000.0f,
     .unit = Unit::Hertz,
     .smoothing = {SmoothingKind::Multiplicative, 20.0f},
     .decimals = 0},
    {.id = "phaser.notches",
     .name = "Notches",
     .shortName = "Notches",
     .kind = ParamKind::Discrete,
     .min = 1.0f,
     .max = 12.0f,
     .def = 2.0f,
     .decimals = 0},
    {.id = "phaser.spread",
     .name = "Spread",
     .shortName = "Spread",
     .min = 0.0f,
     .max = 100.0f,
     .def = 50.0f,
     .unit = Unit::Percent,
     .decimals = 0},
    {.id = "flanger.time",
     .name = "Flanger Time",
     .shortName = "Time",
     .min = 0.1f,
     .max = 10.0f,
     .def = 2.0f,
     .skewCentre = 2.0f,
     .unit = Unit::Milliseconds,
     .decimals = 2},
    {.id = "flanger.zero",
     .name = "Through Zero",
     .shortName = "Through Zero",
     .kind = ParamKind::Toggle,
     .min = 0.0f,
     .max = 1.0f,
     .def = 0.0f,
     .flags = ParamFlags::ChangesLatency},
    {.id = "doubler.time",
     .name = "Doubler Time",
     .shortName = "Time",
     .min = 5.0f,
     .max = 60.0f,
     .def = 20.0f,
     .skewCentre = 20.0f,
     .unit = Unit::Milliseconds,
     .decimals = 1},
    {.id = "feedback",
     .name = "Feedback",
     .shortName = "Feedback",
     .min = 0.0f,
     .max = 95.0f,
     .def = 30.0f,
     .unit = Unit::Percent,
     .decimals = 0},
    {.id = "invert",
     .name = "Invert",
     .shortName = "Invert",
     .kind = ParamKind::Toggle,
     .min = 0.0f,
     .max = 1.0f,
     .def = 0.0f},
    {.id = "warmth",
     .name = "Warmth",
     .shortName = "Warmth",
     .min = 0.0f,
     .max = 100.0f,
     .def = 0.0f,
     .unit = Unit::Percent,
     .decimals = 0},
    {.id = "rate",
     .name = "Rate",
     .shortName = "Rate",
     .min = 0.01f,
     .max = 20.0f,
     .def = 0.4f,
     .skewCentre = 1.0f,
     .unit = Unit::Hertz,
     .smoothing = {SmoothingKind::Multiplicative, 20.0f},
     .decimals = 2},
    {.id = "sync",
     .name = "Sync",
     .shortName = "Sync",
     .kind = ParamKind::Toggle,
     .min = 0.0f,
     .max = 1.0f,
     .def = 0.0f},
    {.id = "div",
     .name = "Sync Rate",
     .shortName = "Rate",
     .kind = ParamKind::Choice,
     .min = 0.0f,
     .max = 16.0f,
     .def = 3.0f,
     .choices = dsp::lfoNoteValues},
    {.id = "shape",
     .name = "Shape",
     .shortName = "Shape",
     .kind = ParamKind::Choice,
     .min = 0.0f,
     .max = 6.0f,
     .def = 1.0f,
     .choices = kShapes},
    {.id = "amount",
     .name = "Amount",
     .shortName = "Amount",
     .min = 0.0f,
     .max = 100.0f,
     .def = 50.0f,
     .unit = Unit::Percent,
     .decimals = 0},
    {.id = "stereo",
     .name = "Stereo",
     .shortName = "Stereo",
     .kind = ParamKind::Choice,
     .min = 0.0f,
     .max = 1.0f,
     .def = 0.0f,
     .choices = kStereo},
    {.id = "offset",
     .name = "Offset",
     .shortName = "Offset",
     .min = 0.0f,
     .max = 180.0f,
     .def = 90.0f,
     .unit = Unit::Degrees,
     .decimals = 0},
    {.id = "spin",
     .name = "Spin",
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
    {.id = "lowcut",
     .name = "Low Cut",
     .shortName = "Low Cut",
     .kind = ParamKind::Toggle,
     .min = 0.0f,
     .max = 1.0f,
     .def = 1.0f},
    {.id = "lowcut.freq",
     .name = "Low Cut Freq",
     .shortName = "Freq",
     .min = 20.0f,
     .max = 2000.0f,
     .def = 120.0f,
     .skewCentre = 200.0f,
     .unit = Unit::Hertz,
     .smoothing = {SmoothingKind::Multiplicative, 20.0f},
     .decimals = 0},
    {.id = "mix",
     .name = "Dry/Wet",
     .shortName = "Mix",
     .min = 0.0f,
     .max = 100.0f,
     .def = 50.0f,
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
    {"mode",
     "The kind of effect: Phaser sweeps notches through the sound, Flanger a comb of them, Doubler adds "
     "a moving second take."},
    {"phaser.center", "Where the phaser's notches sit, in the middle of their sweep."},
    {"phaser.notches", "How many notches the phaser sweeps: more is deeper and more intense."},
    {"phaser.spread", "How far apart the phaser's notches are: bunched close together, or spaced wide."},
    {"flanger.time", "The flanger's longest delay; the sweep shortens it from there, towards zero."},
    {"flanger.zero",
     "Holds the dry sound back 1 ms, so the flanger's sweep passes through it, for the deep jet "
     "of tape flanging."},
    {"doubler.time", "How far behind the dry sound the second take plays."},
    {"feedback", "How much of the effect goes back through it, for a resonant, metallic edge."},
    {"invert", "Flips the feedback's polarity, for a hollower sound."},
    {"warmth",
     "Darkens and gently saturates the effect, like an old analog pedal, more so on each pass of the "
     "feedback."},
    {"rate", "How fast the sweep moves, in cycles per second."},
    {"sync", "Moves the sweep in time with the song: the rate becomes a note value."},
    {"div", "How fast the sweep moves as a note value, one cycle per note, while Sync is on."},
    {"shape", "The sweep's wave: smooth, even, rising, falling, jumping, stepping at random or drifting."},
    {"amount", "How far the sweep moves: further is a deeper, more obvious effect."},
    {"stereo", "How the two sides differ: Offset sweeps them apart in step, Spin at different speeds."},
    {"offset", "How far apart the two sides sweep: together at 0°, in opposite directions at 180°."},
    {"spin", "How much faster the right side sweeps than the left, so the two drift in and out of step."},
    {"env", "Lets the input's level move the sweep: playing louder pushes it further."},
    {"env.amount", "How far, and which way, the input's level moves the sweep."},
    {"env.attack", "How quickly the sweep follows the input getting louder."},
    {"env.release", "How quickly the sweep falls back once the input gets quieter."},
    {"lowcut", "Keeps the lows out of the effect, so they stay solid and centred."},
    {"lowcut.freq", "Below this frequency, the sound passes through untouched."},
    {"mix", "Blends the original sound with the effect; 50 % gives the deepest notches."},
    {"output", "The level after the effect."},
};
constexpr auto kParams = describe(kSpecs, kDescriptions);
static_assert(allDescribed(kParams), "every parameter needs a description (SPEC.md)");

constexpr MeterSpec kMeters[] = {
    {.slot = PhaserFlanger::PeakInL, .id = "input_left", .name = "Input, left", .kind = MeterKind::Level},
    {.slot = PhaserFlanger::PeakInR, .id = "input_right", .name = "Input, right", .kind = MeterKind::Level},
    {.slot = PhaserFlanger::PeakOutL, .id = "output_left", .name = "Output, left", .kind = MeterKind::Level},
    {.slot = PhaserFlanger::PeakOutR,
     .id = "output_right",
     .name = "Output, right",
     .kind = MeterKind::Level},
};

constexpr DeviceInfo kInfo{
    .id = "phaser-flanger",
    .name = "Phaser & Flanger",
    .category = "Modulation",
    .kind = DeviceKind::AudioEffect,
    .buses = {.inputChannels = 2, .outputChannels = 2},
    .params = kParams,
    .stateVersion = 1,
    .meters = kMeters,
};

// The sweep (SPEC: Ranges and limits). Starting points, tuned by ear.
constexpr double kPhaserOctaves = 3.0, kPhaserLowestHz = 20.0, kPhaserHighestHz = 18000.0;
constexpr double kDoublerSweepMs = 5.0, kDoublerShortestMs = 1.0;
constexpr double kQBunched = 4.0, kQWide = 0.3;
constexpr double kLongestMs = 90.0; // the Doubler's longest, its sweep and Through Zero, with room
constexpr float kEnvFloorDb = -48.0f;
constexpr double kLfoSmoothMs = 3.0, kGlideMs = 12.0;
// Warmth: as the Chorus's.
constexpr double kWarmthTopHz = 20000.0, kWarmthBottomHz = 4000.0;
constexpr float kWarmthFadeIn = 0.05f;

constexpr dsp::ModulationLfo::Shape kLfoShapes[] = {
    dsp::ModulationLfo::Shape::Sine,   dsp::ModulationLfo::Shape::Triangle,
    dsp::ModulationLfo::Shape::SawUp,  dsp::ModulationLfo::Shape::SawDown,
    dsp::ModulationLfo::Shape::Square, dsp::ModulationLfo::Shape::SampleAndHold,
    dsp::ModulationLfo::Shape::Wander};

/// What goes round a loop: exactly itself up to full scale, then rounding off smoothly towards
/// +12 dBFS, so a ringing resonance saturates instead of clipping hard.
float bounded(float x) noexcept
{
    const float a = std::abs(x);
    return a <= 1.0f ? x : std::copysign(1.0f + 3.0f * std::tanh((a - 1.0f) / 3.0f), x);
}

int zeroSamples(double sampleRate)
{
    return static_cast<int>(std::lround(PhaserFlanger::zeroMs * 0.001 * sampleRate));
}
} // namespace

PhaserFlanger::PhaserFlanger() : Device(kInfo) {}

// ---- pure functions ------------------------------------------------------------------------

double PhaserFlanger::sweep(const Sweep& s, double lfo, double env) noexcept
{
    const double by = s.envAmount * env;
    switch (s.mode)
    {
    case ModeKind::Phaser:
        return std::clamp(s.centerHz * std::exp2(kPhaserOctaves * (s.amount * lfo + by)), kPhaserLowestHz,
                          kPhaserHighestHz);
    case ModeKind::Flanger:
        return std::clamp(s.flangerMs * (1.0 - s.amount * (1.0 - lfo) * 0.5 - by), 0.0, 2.0 * s.flangerMs);
    case ModeKind::Doubler:
        return std::max(kDoublerShortestMs, s.doublerMs + kDoublerSweepMs * (s.amount * lfo + by));
    }
    return 0.0;
}

double PhaserFlanger::allpassQ(double spread) noexcept
{
    return kQBunched * std::pow(kQWide / kQBunched, std::clamp(spread, 0.0, 1.0));
}

PhaserFlanger::Gains PhaserFlanger::gains(float mix) noexcept
{
    const float angle = std::clamp(mix, 0.0f, 1.0f) * static_cast<float>(dsp::pi) * 0.5f;
    return {.dry = mix <= 0.0f   ? 1.0f
                   : mix >= 1.0f ? 0.0f
                                 : std::cos(angle),
            .wet = mix <= 0.0f   ? 0.0f
                   : mix >= 1.0f ? 1.0f
                                 : std::sin(angle)};
}

double PhaserFlanger::response(const Response& r, double at, double hz, double sampleRate) noexcept
{
    using C = std::complex<double>;
    const auto g = gains(static_cast<float>(r.mix));
    const double fb = std::clamp(r.feedback, 0.0, 0.95) * (r.invert ? -1.0 : 1.0);
    const double w = dsp::twoPi * hz / sampleRate;
    C dry{1.0, 0.0}, wet;
    if (r.mode == ModeKind::Phaser)
    {
        // The chain, fed back through a sample: y = H x + fb H z^-1 y.
        const auto c = dsp::designBiquad(dsp::BiquadType::Allpass, sampleRate,
                                         std::min(at, 0.45 * sampleRate), allpassQ(r.spread));
        const C one = dsp::biquadResponse(c, hz, sampleRate);
        C h{1.0, 0.0};
        for (int k = 0; k < std::clamp(r.notches, 1, maxNotches); ++k)
            h *= one;
        wet = h / (1.0 - fb * std::polar(1.0, -w) * h);
    }
    else
    {
        // The line, read before each sample is written: never under its shortest.
        const double samples =
            std::max(at * 0.001 * sampleRate, static_cast<double>(dsp::ModulatedDelay::minimumDelay + 1));
        const C z = std::polar(1.0, -w * samples);
        wet = z / (1.0 - fb * z);
        if (r.zero && r.mode == ModeKind::Flanger)
            dry = std::polar(1.0, -w * zeroSamples(sampleRate));
    }
    return std::abs(static_cast<double>(g.dry) * dry + static_cast<double>(g.wet) * wet);
}

// ---- the engine ----------------------------------------------------------------------------

void PhaserFlanger::prepare(const ProcessSpec& spec)
{
    sampleRate_ = spec.sampleRate > 0.0 ? spec.sampleRate : 48000.0;
    rampSamples_ = dsp::msToSamples(20.0, sampleRate_);
    modeSamples_ = dsp::msToSamples(50.0, sampleRate_);
    notchSamples_ = dsp::msToSamples(30.0, sampleRate_);
    lfoCoef_ = static_cast<float>(1.0 - std::exp(-1.0 / (kLfoSmoothMs * 0.001 * sampleRate_)));
    for (auto* g : {&flangerTime_, &doublerTime_, &offset_, &rightJump_})
        g->setTime(kGlideMs, sampleRate_);
    for (auto& c : ch_)
    {
        c.split.prepare(sampleRate_);
        c.line.prepare(kLongestMs * 0.001 * sampleRate_ + 64.0);
        c.tonePhaser.prepare(sampleRate_);
        c.toneLine.prepare(sampleRate_);
        for (auto* d : {&c.dry, &c.lows, &c.phaserIn})
            d->prepare(zeroSamples(sampleRate_));
    }
    lfo_.prepare(sampleRate_, kGlideMs);
    detector_.prepare(sampleRate_);
    analysis().setSampleRate(sampleRate_);
    reset();
}

void PhaserFlanger::reset() noexcept
{
    grid_.reset();
    for (auto& c : ch_)
    {
        c.split.reset();
        for (auto& a : c.allpass)
            a.reset();
        c.line.reset();
        c.tonePhaser.reset();
        c.toneLine.reset();
        c.backPhaser = c.backLine = 0.0f;
    }
    lfo_.reset();
    detector_.reset();
    envDb_ = kEnvFloorDb;
    applyZero();
    snap();
    fresh_ = true;
}

void PhaserFlanger::applyZero() noexcept
{
    zero_ = params().getBool(FlangerZero);
    for (auto& c : ch_)
        for (auto* d : {&c.dry, &c.lows, &c.phaserIn})
            d->setDelay(zero_ ? zeroSamples(sampleRate_) : 0);
}

int PhaserFlanger::latencySamples() const noexcept
{
    return params().getBool(FlangerZero) ? zeroSamples(sampleRate_) : 0;
}

void PhaserFlanger::snap() noexcept
{
    const auto& p = params();
    mode_ = previous_ = static_cast<ModeKind>(p.getInt(Mode));
    modeFade_.reset(1.0f);
    notches_ = previousNotches_ = p.getInt(PhaserNotches);
    notchFade_.reset(1.0f);
    stereo_ = static_cast<StereoKind>(p.getInt(Stereo));
    updateTargets(/*immediate*/ true);
    for (auto* g : {&flangerTime_, &doublerTime_, &offset_})
        g->snap(g->target());
    rightJump_.snap(0.0);
    // Locked to the song at once: nothing has played to glide from.
    updateSync(/*snap*/ true);
    rightPosition_ = lfo_.position() + offset_.current();
    const auto shape = kLfoShapes[std::clamp(p.getInt(Shape), 0, 6)];
    ch_[0].lfo = dsp::ModulationLfo::value(shape, lfo_.position());
    ch_[1].lfo = dsp::ModulationLfo::value(shape, rightPosition_);
}

void PhaserFlanger::updateTargets(bool immediate) noexcept
{
    const auto& p = params();
    const int ramp = immediate ? 0 : rampSamples_;
    spread_.setTarget(p.get(PhaserSpread) * 0.01f, ramp);
    feedback_.setTarget(p.get(Feedback) * 0.01f, ramp);
    invert_.setTarget(p.getBool(Invert) ? 1.0f : 0.0f, ramp);
    warmth_.setTarget(p.get(Warmth) * 0.01f, ramp);
    amount_.setTarget(p.get(Amount) * 0.01f, ramp);
    spin_.setTarget(p.get(Spin) * 0.01f, ramp);
    envOn_.setTarget(p.getBool(Env) ? 1.0f : 0.0f, ramp);
    envAmount_.setTarget(p.get(EnvAmount) * 0.01f, ramp);
    lowOn_.setTarget(p.getBool(LowCut) ? 1.0f : 0.0f, ramp);
    mix_.setTarget(p.get(Mix) * 0.01f, ramp);
    output_.setTarget(dsp::dbToGain(p.get(Output)), ramp);
    center_.setTarget(p.get(PhaserCenter), ramp);
    rate_.setTarget(p.get(Rate), ramp);
    lowFreq_.setTarget(p.get(LowCutFreq), ramp);
    flangerTime_.setTarget(p.get(FlangerTime));
    doublerTime_.setTarget(p.get(DoublerTime));
    offset_.setTarget(p.get(Offset) / 360.0);
    if (immediate)
    {
        for (auto* s : {&spread_, &feedback_, &invert_, &warmth_, &amount_, &spin_, &envOn_, &envAmount_,
                        &lowOn_, &mix_, &output_})
            s->reset(s->target());
        for (auto* s : {&center_, &rate_, &lowFreq_})
            s->reset(s->target());
    }

    // A new mode, or a new number of notches: the old and the new crossfade.
    const auto mode = static_cast<ModeKind>(p.getInt(Mode));
    if (mode != mode_ && !immediate)
    {
        previous_ = mode_;
        mode_ = mode;
        modeFade_.reset(0.0f);
        modeFade_.setTarget(1.0f, modeSamples_);
    }
    const int notches = p.getInt(PhaserNotches);
    if (notches != notches_ && !immediate)
    {
        previousNotches_ = notches_;
        notches_ = notches;
        notchFade_.reset(0.0f);
        notchFade_.setTarget(1.0f, notchSamples_);
    }
    // Back from Spin to Offset: the right side glides to its place.
    const auto stereo = static_cast<StereoKind>(p.getInt(Stereo));
    if (stereo != stereo_ && !immediate && stereo == StereoKind::Offset)
    {
        const double jump = lfo_.position() + offset_.current() - rightPosition_;
        rightJump_.snap(-(jump - std::round(jump)));
        rightJump_.setTarget(0.0);
    }
    stereo_ = stereo;

    // The envelope: rising or falling its whole range (48 dB) in Env Attack or Env Release.
    envRise_ = -kEnvFloorDb / static_cast<float>(std::max(1.0, p.get(EnvAttack) * 0.001 * sampleRate_));
    envFall_ = -kEnvFloorDb / static_cast<float>(std::max(1.0, p.get(EnvRelease) * 0.001 * sampleRate_));

    // The tone stages and the split follow their settings on each tick.
    const double warmth = warmth_.current();
    const double highCut = kWarmthTopHz * std::pow(kWarmthBottomHz / kWarmthTopHz, warmth);
    for (auto& c : ch_)
    {
        c.tonePhaser.set(0.0, highCut, warmth);
        c.toneLine.set(0.0, highCut, warmth);
        c.split.setFrequency(lowFreq_.current());
    }
}

void PhaserFlanger::updateSync(bool snap) noexcept
{
    const auto& p = params();
    // A synced cycle at most maxCycleSeconds long, at a slow tempo.
    const double beats = p.getBool(Sync) ? std::min(dsp::lfoNoteBeats(p.getInt(Div), beatsPerBar_),
                                                    maxCycleSeconds * tempo_ / 60.0)
                                         : 0.0;
    lfo_.set(rate_.current(), beats, snap);
}

void PhaserFlanger::process(ProcessContext& ctx) noexcept
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
    lfo_.beginBlock(ctx.transport.playing, ctx.transport.ppqPosition, tempo_);
    if (params().getBool(FlangerZero) != zero_)
        applyZero(); // a latency change: the host restarts around it
    if (fresh_)
    {
        snap();
        fresh_ = false;
    }
    const double zeroLine = zero_ ? zeroMs : 0.0;
    auto shape = kLfoShapes[std::clamp(params().getInt(Shape), 0, 6)];

    float peakIn[maxChannels] = {}, peakOut[maxChannels] = {}, envelope = 0.0f;
    grid_.forEachSegment(
        n,
        [&](int offset, int length, bool isTick) noexcept
        {
            if (isTick)
            {
                updateTargets(false);
                updateSync(/*snap*/ false);
                shape = kLfoShapes[std::clamp(params().getInt(Shape), 0, 6)];
            }
            for (int i = 0; i < length; ++i)
            {
                const float spread = spread_.next(), feedback = feedback_.next(), invert = invert_.next(),
                            warmth = warmth_.next(), amount = amount_.next(), spin = spin_.next(),
                            envOn = envOn_.next(), envAmount = envAmount_.next(), lowOn = lowOn_.next(),
                            mix = mix_.next(), out = output_.next(), fade = modeFade_.next(),
                            notchFade = notchFade_.next();
                const float centerHz = center_.next();
                rate_.next();
                lowFreq_.next();
                const double flangerMs = flangerTime_.next(), doublerMs = doublerTime_.next(),
                             stereoOffset = offset_.next(), rightJump = rightJump_.next();

                // The LFO, each side's, smoothed so the jumping shapes don't click.
                const double left = lfo_.next();
                if (stereo_ == StereoKind::Spin)
                    rightPosition_ += lfo_.rateHz() * (1.0 + spin) / sampleRate_;
                else
                    rightPosition_ = left + stereoOffset + rightJump;
                ch_[0].lfo += lfoCoef_ * (dsp::ModulationLfo::value(shape, left) - ch_[0].lfo);
                ch_[1].lfo += lfoCoef_ * (dsp::ModulationLfo::value(shape, rightPosition_) - ch_[1].lfo);

                const int at = offset + i;
                const float in[maxChannels] = {ctx.input.channel(0)[at], inChannels > 1
                                                                             ? ctx.input.channel(1)[at]
                                                                             : ctx.input.channel(0)[at]};

                // The envelope: the input's peak, moving its whole range in Env Attack or Release.
                const float levelDb = detector_.processDb(std::max(std::abs(in[0]), std::abs(in[1])));
                envDb_ = std::max(kEnvFloorDb, envDb_ + std::clamp(levelDb - envDb_, -envFall_, envRise_));
                envelope = envOn * (envDb_ - kEnvFloorDb) / -kEnvFloorDb;

                // How much each mode sounds: the one it's in, and the one it's leaving.
                float weight[3] = {};
                weight[static_cast<int>(mode_)] += fade;
                weight[static_cast<int>(previous_)] += 1.0f - fade;
                const float wPhaser = weight[0], wFlanger = weight[1], wDoubler = weight[2];
                const float wLine = wFlanger + wDoubler;

                const Sweep sw{.mode = ModeKind::Phaser,
                               .centerHz = centerHz,
                               .flangerMs = flangerMs,
                               .doublerMs = doublerMs,
                               .amount = amount,
                               .envAmount = envAmount};
                const auto q = allpassQ(spread);
                const float fed = feedback * (1.0f - 2.0f * invert);
                const float blend = std::min(1.0f, warmth / kWarmthFadeIn);
                const auto g = gains(mix);

                for (int c = 0; c < maxChannels; ++c)
                {
                    auto& h = ch_[static_cast<size_t>(c)];
                    peakIn[c] = std::max(peakIn[c], std::abs(in[c]));
                    // The Low Cut: the band above feeds the effect; the band below stands in for the
                    // dry sound the mix takes away. Through Zero holds the dry sound back.
                    const auto bands = h.split.process(in[c]);
                    const float lows = h.lows.process(lowOn * bands.low);
                    const float highs = in[c] + lowOn * (bands.high - in[c]);
                    const float dry = h.dry.process(in[c]);

                    // The Phaser: the chain always runs, so it's ready when it's heard.
                    auto phaser = sw;
                    const double hz = sweep(phaser, h.lfo, envelope);
                    const auto coeffs = dsp::designBiquad(dsp::BiquadType::Allpass, sampleRate_,
                                                          std::min(hz, 0.45 * sampleRate_), q);
                    float y = h.phaserIn.process(highs) + fed * wPhaser * h.backPhaser;
                    float taps[maxNotches + 1];
                    taps[0] = y;
                    for (int k = 0; k < maxNotches; ++k)
                    {
                        h.allpass[static_cast<size_t>(k)].setCoeffs(coeffs);
                        y = h.allpass[static_cast<size_t>(k)].process(y);
                        taps[k + 1] = y;
                    }
                    const float chain =
                        taps[previousNotches_] + notchFade * (taps[notches_] - taps[previousNotches_]);
                    const float tonedPhaser = chain + blend * (h.tonePhaser.process(chain) - chain);
                    h.backPhaser = bounded(tonedPhaser);

                    // The Flanger and the Doubler: the line, read where each sounds.
                    float read = 0.0f;
                    if (wLine > 0.0f)
                    {
                        auto flanger = sw, doubler = sw;
                        flanger.mode = ModeKind::Flanger;
                        doubler.mode = ModeKind::Doubler;
                        float sum = 0.0f;
                        // Read before this sample is written: one sample less.
                        if (wFlanger > 0.0f)
                            sum += wFlanger *
                                   h.line.read(sweep(flanger, h.lfo, envelope) * 0.001 * sampleRate_ - 1.0);
                        if (wDoubler > 0.0f)
                            sum += wDoubler * h.line.read((sweep(doubler, h.lfo, envelope) + zeroLine) *
                                                              0.001 * sampleRate_ -
                                                          1.0);
                        read = sum / wLine;
                    }
                    const float tonedLine = read + blend * (h.toneLine.process(read) - read);
                    h.line.push(highs + bounded(fed * wLine * tonedLine));

                    const float wet = wPhaser * tonedPhaser + wLine * tonedLine;
                    const float yOut = (g.dry * dry + (1.0f - g.dry) * lows + g.wet * wet) * out;
                    if (c < outChannels)
                    {
                        ctx.output.channel(c)[at] = yOut;
                        peakOut[c] = std::max(peakOut[c], std::abs(yOut));
                    }
                }
            }
        });

    analysis().publishPeak(PeakInL, peakIn[0]);
    analysis().publishPeak(PeakInR, peakIn[1]);
    analysis().publishPeak(PeakOutL, peakOut[0]);
    analysis().publishPeak(PeakOutR, peakOut[1]);
    const double position = lfo_.position();
    analysis().publish(LfoPosition, static_cast<float>(position - std::floor(position)));
    analysis().publish(LfoLeft, ch_[0].lfo);
    analysis().publish(LfoRight, ch_[1].lfo);
    analysis().publish(Envelope, envelope);
}

double PhaserFlanger::tailSeconds() const noexcept
{
    const auto& p = params();
    const auto mode = static_cast<ModeKind>(p.getInt(Mode));
    const Sweep s{.mode = mode,
                  .centerHz = p.get(PhaserCenter),
                  .flangerMs = p.get(FlangerTime),
                  .doublerMs = p.get(DoublerTime),
                  .amount = p.get(Amount) * 0.01,
                  .envAmount = p.getBool(Env) ? p.get(EnvAmount) * 0.01 : 0.0};
    // A pass through the loop: the line's longest delay, or the chain's delay at its lowest notch
    // (each all-pass delays its centre by 2Q / (pi f)).
    double pass = 0.0;
    if (mode == ModeKind::Phaser)
    {
        const double lowest =
            std::min({sweep(s, -1.0, 0.0), sweep(s, -1.0, 1.0), sweep(s, 1.0, 0.0), sweep(s, 1.0, 1.0)});
        pass = p.getInt(PhaserNotches) * 2.0 * allpassQ(p.get(PhaserSpread) * 0.01) / (dsp::pi * lowest);
    }
    else
        pass = (std::max({sweep(s, 1.0, 0.0), sweep(s, 1.0, 1.0), sweep(s, -1.0, 1.0)}) + zeroMs) * 0.001;
    const double fb = p.get(Feedback) * 0.01;
    if (fb < 1e-3)
        return pass + 0.01;
    const double passes = 3.0 / std::log10(1.0 / fb);
    return std::min(10.0, (passes + 1.0) * pass + 0.01);
}

const DeviceFactory& phaserFlangerFactory()
{
    static const DeviceFactory factory{
        .info = []() -> const DeviceInfo& { return kInfo; },
        .create = []() -> std::unique_ptr<Device> { return std::make_unique<PhaserFlanger>(); },
    };
    return factory;
}
} // namespace fd::devices
