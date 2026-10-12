#include "Chorus.h"

#include "fd/dsp/Math.h"

#include <algorithm>
#include <cmath>

namespace fd::devices
{
namespace
{
constexpr std::string_view kModes[] = {"Chorus", "Ensemble", "Vibrato"};
constexpr std::array<ParamSpec, Chorus::NumParams> kSpecs{{
    {.id = "mode",
     .name = "Mode",
     .shortName = "Mode",
     .kind = ParamKind::Choice,
     .min = 0.0f,
     .max = 2.0f,
     .def = 0.0f,
     .choices = kModes},
    {.id = "rate",
     .name = "Rate",
     .shortName = "Rate",
     .min = 0.01f,
     .max = 20.0f,
     .def = 0.8f,
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
     .def = 5.0f,
     .choices = dsp::lfoNoteValues},
    {.id = "depth",
     .name = "Depth",
     .shortName = "Depth",
     .min = 0.0f,
     .max = 100.0f,
     .def = 50.0f,
     .unit = Unit::Percent,
     .decimals = 0},
    {.id = "shape",
     .name = "Shape",
     .shortName = "Shape",
     .min = 0.0f,
     .max = 100.0f,
     .def = 0.0f,
     .unit = Unit::Percent,
     .decimals = 0},
    {.id = "offset",
     .name = "Offset",
     .shortName = "Offset",
     .min = 0.0f,
     .max = 180.0f,
     .def = 180.0f,
     .unit = Unit::Degrees,
     .decimals = 0},
    {.id = "delay",
     .name = "Delay",
     .shortName = "Delay",
     .min = 1.0f,
     .max = 30.0f,
     .def = 6.0f,
     .skewCentre = 8.0f,
     .unit = Unit::Milliseconds,
     .decimals = 1},
    {.id = "width",
     .name = "Width",
     .shortName = "Width",
     .min = 0.0f,
     .max = 200.0f,
     .def = 100.0f,
     .unit = Unit::Percent,
     .decimals = 0},
    {.id = "warmth",
     .name = "Warmth",
     .shortName = "Warmth",
     .min = 0.0f,
     .max = 100.0f,
     .def = 0.0f,
     .unit = Unit::Percent,
     .decimals = 0},
    {.id = "feedback",
     .name = "Feedback",
     .shortName = "Feedback",
     .min = 0.0f,
     .max = 95.0f,
     .def = 0.0f,
     .unit = Unit::Percent,
     .decimals = 0},
    {.id = "invert",
     .name = "Invert",
     .shortName = "Invert",
     .kind = ParamKind::Toggle,
     .min = 0.0f,
     .max = 1.0f,
     .def = 0.0f},
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
     "The kind of effect: Chorus thickens with two voices, Ensemble is lusher with three, and Vibrato "
     "bends the pitch alone."},
    {"rate", "How fast the voices move, in cycles per second."},
    {"sync", "Moves the voices in time with the song: the rate becomes a note value."},
    {"div", "How fast the voices move as a note value, one cycle per note, while Sync is on."},
    {"depth", "How far the voices' delay moves: more depth, more pitch movement and a deeper effect."},
    {"shape",
     "The movement's wave, from a smooth sine at 0 % to a triangle at 100 %, which swaps between two "
     "steady pitches."},
    {"offset",
     "How far apart the two sides move: together at 0°, and in opposite directions at 180°, the widest."},
    {"delay", "How far the voices trail the dry sound at their closest; Depth sweeps them later from there."},
    {"width", "The stereo width of the voices, from mono at 0 % to extra wide at 200 %."},
    {"warmth", "Darkens and gently saturates the voices, like an old analog chorus, more so on each pass of "
               "the feedback."},
    {"feedback", "How much of the voices goes back through them, for a resonant, metallic edge."},
    {"invert", "Flips the feedback's polarity, for a hollower sound."},
    {"lowcut", "Keeps the lows out of the effect, so they stay solid and centred."},
    {"lowcut.freq", "Below this frequency, the sound passes through untouched."},
    {"mix", "Blends the original sound with the voices; Vibrato is always all voices."},
    {"output", "The level after the chorus."},
};
constexpr auto kParams = describe(kSpecs, kDescriptions);
static_assert(allDescribed(kParams), "every parameter needs a description (SPEC.md)");

constexpr MeterSpec kMeters[] = {
    {.slot = Chorus::PeakInL, .id = "input_left", .name = "Input, left", .kind = MeterKind::Level},
    {.slot = Chorus::PeakInR, .id = "input_right", .name = "Input, right", .kind = MeterKind::Level},
    {.slot = Chorus::PeakOutL, .id = "output_left", .name = "Output, left", .kind = MeterKind::Level},
    {.slot = Chorus::PeakOutR, .id = "output_right", .name = "Output, right", .kind = MeterKind::Level},
};

constexpr DeviceInfo kInfo{
    .id = "chorus",
    .name = "Chorus",
    .category = "Modulation",
    .kind = DeviceKind::AudioEffect,
    .buses = {.inputChannels = 2, .outputChannels = 2},
    .params = kParams,
    .stateVersion = 1,
    .meters = kMeters,
};

// The voices (SPEC: DSP design). Starting points, tuned by ear.
constexpr double kChorusSweepMs = 8.0, kVibratoSweepMs = 16.0;
constexpr double kShimmerRatio = 7.0, kShimmerDepth = 0.2;
constexpr double kLongestMs = 60.0;
// Warmth: the tone stage's high cut falls from 20 kHz to 4 kHz and its saturation rises from 0 to
// 1; it fades into the path over the first 5 %.
constexpr double kWarmthTopHz = 20000.0, kWarmthBottomHz = 4000.0;
constexpr float kWarmthFadeIn = 0.05f;
constexpr double kGlideMs = 12.0; // each of a glide's two poles
} // namespace

Chorus::Chorus() : Device(kInfo) {}

// ---- pure functions ------------------------------------------------------------------------

double Chorus::divisionBeats(int index, double beatsPerBar) noexcept
{
    return dsp::lfoNoteBeats(index, beatsPerBar);
}

int Chorus::voiceCount(ModeKind mode) noexcept
{
    return mode == ModeKind::Ensemble ? maxVoices : 1;
}

double Chorus::sweepMs(ModeKind mode, double depth) noexcept
{
    const double d = std::clamp(depth, 0.0, 1.0);
    return (mode == ModeKind::Vibrato ? kVibratoSweepMs : kChorusSweepMs) * d * d;
}

double Chorus::baseMs(ModeKind mode, double delayMs, double sampleRate) noexcept
{
    // Read before the newest sample is written: the shortest read is one sample more (process()).
    if (mode == ModeKind::Vibrato)
        return (dsp::ModulatedDelay::minimumDelay + 1) * 1000.0 / sampleRate;
    return delayMs;
}

double Chorus::voiceValue(const Voicing& v, int side, int voice, double position) noexcept
{
    using Lfo = dsp::ModulationLfo;
    const double p = position + side * v.offset +
                     (v.mode == ModeKind::Ensemble ? voice / static_cast<double>(maxVoices) : 0.0);
    const double sine = Lfo::value(Lfo::Shape::Sine, p);
    double value = sine + std::clamp(v.shape, 0.0, 1.0) * (Lfo::value(Lfo::Shape::Triangle, p) - sine);
    if (v.mode == ModeKind::Ensemble)
        value =
            (value + kShimmerDepth * Lfo::value(Lfo::Shape::Sine, kShimmerRatio * p)) / (1.0 + kShimmerDepth);
    return value;
}

double Chorus::voiceDelayMs(const Voicing& v, double sampleRate, int side, int voice,
                            double position) noexcept
{
    return baseMs(v.mode, v.delayMs, sampleRate) +
           sweepMs(v.mode, v.depth) * 0.5 * (1.0 + voiceValue(v, side, voice, position));
}

double Chorus::pitchSwing(const Voicing& v, double rateHz, double sampleRate) noexcept
{
    // A voice plays its input at 1 - d'(t) times its speed, d its delay in seconds: the largest
    // bend over a cycle, either way. Every voice has the same movement, shifted.
    constexpr int steps = 4096;
    double worst = 0.0, last = voiceDelayMs(v, sampleRate, 0, 0, 0.0);
    for (int i = 1; i <= steps; ++i)
    {
        const double d = voiceDelayMs(v, sampleRate, 0, 0, static_cast<double>(i) / steps);
        const double speed = 1.0 - (d - last) / 1000.0 * steps * rateHz;
        last = d;
        worst = std::max(worst, std::abs(1200.0 * std::log2(std::max(speed, 0.01))));
    }
    return worst;
}

// ---- the engine ----------------------------------------------------------------------------

void Chorus::prepare(const ProcessSpec& spec)
{
    sampleRate_ = spec.sampleRate > 0.0 ? spec.sampleRate : 48000.0;
    rampSamples_ = dsp::msToSamples(20.0, sampleRate_);
    modeSamples_ = dsp::msToSamples(50.0, sampleRate_);
    for (auto* g : {&depth_, &delay_, &offset_})
        g->setTime(kGlideMs, sampleRate_);
    for (auto& c : ch_)
    {
        c.line.prepare(kLongestMs * 0.001 * sampleRate_ + 64.0);
        c.tone.prepare(sampleRate_);
        c.split.prepare(sampleRate_);
    }
    lfo_.prepare(sampleRate_, kGlideMs);
    analysis().setSampleRate(sampleRate_);
    reset();
}

void Chorus::reset() noexcept
{
    grid_.reset();
    for (auto& c : ch_)
    {
        c.line.reset();
        c.tone.reset();
        c.split.reset();
    }
    lfo_.reset();
    snap();
    fresh_ = true;
}

void Chorus::snap() noexcept
{
    mode_ = previous_ = static_cast<ModeKind>(params().getInt(Mode));
    modeFade_.reset(1.0f);
    updateTargets(/*immediate*/ true);
    for (auto* g : {&depth_, &delay_, &offset_})
        g->snap(g->target());
    // Locked to the song at once: nothing has played to glide from.
    updateSync(/*snap*/ true);
}

void Chorus::updateTargets(bool immediate) noexcept
{
    const auto& p = params();
    const int ramp = immediate ? 0 : rampSamples_;
    shape_.setTarget(p.get(Shape) * 0.01f, ramp);
    width_.setTarget(p.get(Width) * 0.01f, ramp);
    warmth_.setTarget(p.get(Warmth) * 0.01f, ramp);
    feedback_.setTarget(p.get(Feedback) * 0.01f, ramp);
    invert_.setTarget(p.getBool(Invert) ? 1.0f : 0.0f, ramp);
    lowOn_.setTarget(p.getBool(LowCut) ? 1.0f : 0.0f, ramp);
    mix_.setTarget(p.get(Mix) * 0.01f, ramp);
    output_.setTarget(dsp::dbToGain(p.get(Output)), ramp);
    rate_.setTarget(p.get(Rate), ramp);
    lowFreq_.setTarget(p.get(LowCutFreq), ramp);
    depth_.setTarget(p.get(Depth) * 0.01);
    delay_.setTarget(p.get(DelayTime));
    offset_.setTarget(p.get(Offset) / 360.0);
    if (immediate)
    {
        for (auto* s : {&shape_, &width_, &warmth_, &feedback_, &invert_, &lowOn_, &mix_, &output_})
            s->reset(s->target());
        rate_.reset(rate_.target());
        lowFreq_.reset(lowFreq_.target());
    }

    // A new mode: the one it leaves and the new one play side by side, crossfading.
    const auto mode = static_cast<ModeKind>(p.getInt(Mode));
    if (mode != mode_ && !immediate)
    {
        previous_ = mode_;
        mode_ = mode;
        modeFade_.reset(0.0f);
        modeFade_.setTarget(1.0f, modeSamples_);
    }

    // The tone stage and the split follow their settings on each tick.
    const double warmth = warmth_.current();
    const double highCut = kWarmthTopHz * std::pow(kWarmthBottomHz / kWarmthTopHz, warmth);
    for (auto& c : ch_)
    {
        c.tone.set(0.0, highCut, warmth);
        c.split.setFrequency(lowFreq_.current());
    }
}

void Chorus::updateSync(bool snap) noexcept
{
    const auto& p = params();
    // A synced cycle at most maxCycleSeconds long, at a slow tempo.
    const double beats = p.getBool(Sync) ? std::min(divisionBeats(p.getInt(Div), beatsPerBar_),
                                                    maxCycleSeconds * tempo_ / 60.0)
                                         : 0.0;
    lfo_.set(rate_.current(), beats, snap);
}

Chorus::Gains Chorus::gainsFor(ModeKind mode, float mix, float width, float feedback) const noexcept
{
    if (mode == ModeKind::Vibrato)
        return {.dry = 0.0f, .wet = 1.0f, .width = 1.0f, .feedback = 0.0f};
    // Equal power, exact at the ends.
    const float angle = std::clamp(mix, 0.0f, 1.0f) * static_cast<float>(dsp::pi) * 0.5f;
    return {.dry = mix <= 0.0f   ? 1.0f
                   : mix >= 1.0f ? 0.0f
                                 : std::cos(angle),
            .wet = mix <= 0.0f   ? 0.0f
                   : mix >= 1.0f ? 1.0f
                                 : std::sin(angle),
            .width = width,
            .feedback = feedback / std::sqrt(static_cast<float>(voiceCount(mode)))};
}

void Chorus::voices(ModeKind mode, double position, double depth, double delayMs, double shape, double offset,
                    float* sum) const noexcept
{
    const Voicing v{.mode = mode, .depth = depth, .delayMs = delayMs, .shape = shape, .offset = offset};
    const int n = voiceCount(mode);
    const float scale = 1.0f / std::sqrt(static_cast<float>(n));
    for (int side = 0; side < maxChannels; ++side)
    {
        float s = 0.0f;
        for (int k = 0; k < n; ++k)
        {
            // Read before this sample is written: one sample less.
            const double d = voiceDelayMs(v, sampleRate_, side, k, position) * 0.001 * sampleRate_ - 1.0;
            s += ch_[static_cast<size_t>(side)].line.read(d);
        }
        sum[side] = s * scale;
    }
}

void Chorus::process(ProcessContext& ctx) noexcept
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
    if (fresh_)
    {
        snap();
        fresh_ = false;
    }

    float peakIn[maxChannels] = {}, peakOut[maxChannels] = {};
    grid_.forEachSegment(
        n,
        [&](int offset, int length, bool isTick) noexcept
        {
            if (isTick)
            {
                updateTargets(false);
                updateSync(/*snap*/ false);
            }
            for (int i = 0; i < length; ++i)
            {
                const float shape = shape_.next(), width = width_.next(), warmth = warmth_.next(),
                            feedback = feedback_.next(), invert = invert_.next(), lowOn = lowOn_.next(),
                            mix = mix_.next(), out = output_.next(), fade = modeFade_.next();
                rate_.next();
                lowFreq_.next();
                const double depth = depth_.next(), delayMs = delay_.next(), stereo = offset_.next();
                const double position = lfo_.next();

                const int at = offset + i;
                const float in[maxChannels] = {ctx.input.channel(0)[at], inChannels > 1
                                                                             ? ctx.input.channel(1)[at]
                                                                             : ctx.input.channel(0)[at]};

                // The Low Cut: the band above feeds the voices; the band below stands in for the dry
                // sound the mix takes away, so the lows keep their level and never move.
                float lows[maxChannels], highs[maxChannels];
                for (int c = 0; c < maxChannels; ++c)
                {
                    peakIn[c] = std::max(peakIn[c], std::abs(in[c]));
                    auto& h = ch_[static_cast<size_t>(c)];
                    const auto bands = h.split.process(in[c]);
                    lows[c] = lowOn * bands.low;
                    highs[c] = in[c] + lowOn * (bands.high - in[c]);
                }

                // The voices of the mode, and of the one it's leaving while they crossfade.
                float sum[maxChannels];
                voices(mode_, position, depth, delayMs, shape, stereo, sum);
                auto g = gainsFor(mode_, mix, width, feedback);
                if (fade < 1.0f)
                {
                    float before[maxChannels];
                    voices(previous_, position, depth, delayMs, shape, stereo, before);
                    const auto h = gainsFor(previous_, mix, width, feedback);
                    for (int c = 0; c < maxChannels; ++c)
                        sum[c] = before[c] + fade * (sum[c] - before[c]);
                    g = {.dry = h.dry + fade * (g.dry - h.dry),
                         .wet = h.wet + fade * (g.wet - h.wet),
                         .width = h.width + fade * (g.width - h.width),
                         .feedback = h.feedback + fade * (g.feedback - h.feedback)};
                }

                // Warmth: the tone stage, faded in at the bottom.
                const float blend = std::min(1.0f, warmth / kWarmthFadeIn);
                const float fed = g.feedback * (1.0f - 2.0f * invert);
                float wet[maxChannels];
                for (int c = 0; c < maxChannels; ++c)
                {
                    auto& h = ch_[static_cast<size_t>(c)];
                    const float toned = h.tone.process(sum[c]);
                    wet[c] = sum[c] + blend * (toned - sum[c]);
                    h.line.push(highs[c] + std::clamp(fed * wet[c], -4.0f, 4.0f));
                }

                // Width on the voices, then the mix: the dry sound whole, the lows where it's taken away.
                const float mid = 0.5f * (wet[0] + wet[1]), side = 0.5f * (wet[0] - wet[1]) * g.width;
                const float wide[maxChannels] = {mid + side, mid - side};
                for (int c = 0; c < outChannels; ++c)
                {
                    const float y = (g.dry * in[c] + (1.0f - g.dry) * lows[c] + g.wet * wide[c]) * out;
                    ctx.output.channel(c)[at] = y;
                    peakOut[c] = std::max(peakOut[c], std::abs(y));
                }
            }
        });

    analysis().publishPeak(PeakInL, peakIn[0]);
    analysis().publishPeak(PeakInR, peakIn[1]);
    analysis().publishPeak(PeakOutL, peakOut[0]);
    analysis().publishPeak(PeakOutR, peakOut[1]);
    const double position = lfo_.position();
    analysis().publish(LfoPosition, static_cast<float>(position - std::floor(position)));
    analysis().publish(RateHz, static_cast<float>(lfo_.rateHz()));
}

double Chorus::tailSeconds() const noexcept
{
    const auto& p = params();
    const auto mode = static_cast<ModeKind>(p.getInt(Mode));
    // Each pass is at most the longest delay, and the loop gives back at most Feedback.
    const double longest =
        (baseMs(mode, p.get(DelayTime), sampleRate_) + sweepMs(mode, p.get(Depth) * 0.01)) * 0.001;
    const double fb = mode == ModeKind::Vibrato ? 0.0 : p.get(Feedback) * 0.01;
    if (fb < 1e-3)
        return longest + 0.01;
    const double passes = 3.0 / std::log10(1.0 / fb);
    return std::min(10.0, (passes + 1.0) * longest + 0.01);
}

const DeviceFactory& chorusFactory()
{
    static const DeviceFactory factory{
        .info = []() -> const DeviceInfo& { return kInfo; },
        .create = []() -> std::unique_ptr<Device> { return std::make_unique<Chorus>(); },
    };
    return factory;
}
} // namespace fd::devices
