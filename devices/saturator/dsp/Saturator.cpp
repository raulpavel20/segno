#include "Saturator.h"

#include "fd/dsp/Math.h"

#include <algorithm>
#include <cmath>

namespace fd::devices
{
namespace
{
constexpr std::string_view kCurves[] = {"Soft", "Warm", "Tape", "Hard", "Fold", "Rectify"};
constexpr std::string_view kQuality[] = {"2x", "4x", "8x"};

constexpr std::array<ParamSpec, Saturator::NumParams> kSpecs{{
    {.id = "drive", .name = "Drive", .shortName = "Drive", .min = 0.0f, .max = 36.0f, .def = 0.0f,
     .unit = Unit::Decibels},
    {.id = "curve", .name = "Curve", .shortName = "Curve", .kind = ParamKind::Choice, .min = 0.0f, .max = 5.0f,
     .def = 0.0f, .choices = kCurves},
    {.id = "bias", .name = "Bias", .shortName = "Bias", .min = -1.0f, .max = 1.0f, .def = 0.0f, .decimals = 2},
    {.id = "color", .name = "Color", .shortName = "Color", .kind = ParamKind::Toggle, .min = 0.0f, .max = 1.0f},
    {.id = "color.freq", .name = "Color Freq", .shortName = "Freq", .min = 100.0f, .max = 10000.0f, .def = 1000.0f,
     .skewCentre = 1000.0f, .unit = Unit::Hertz, .smoothing = {SmoothingKind::Multiplicative, 20.0f}, .decimals = 0},
    {.id = "color.gain", .name = "Color Gain", .shortName = "Gain", .min = -12.0f, .max = 12.0f, .def = 0.0f,
     .unit = Unit::Decibels},
    {.id = "output", .name = "Output", .shortName = "Output", .min = -36.0f, .max = 12.0f, .def = 0.0f,
     .skewCentre = -6.0f, .unit = Unit::Decibels},
    {.id = "mix", .name = "Dry/Wet", .shortName = "Mix", .min = 0.0f, .max = 100.0f, .def = 100.0f,
     .unit = Unit::Percent, .decimals = 0},
    {.id = "quality", .name = "Quality", .shortName = "Quality", .kind = ParamKind::Choice, .min = 0.0f, .max = 2.0f,
     .def = 1.0f, .choices = kQuality, .flags = ParamFlags::None},
}};
// What each parameter does, in one sentence: the same as in SPEC.md (open question B22).
constexpr ParamDescription kDescriptions[] = {
    {"drive", "How hard the signal is pushed into the curve: more drive, more distortion."},
    {"curve", "The shape of the distortion, from gentle (Soft, Warm, Tape) to harsh (Hard, Fold, Rectify)."},
    {"bias", "Shifts the signal before the curve, adding even harmonics for an asymmetric, warmer tone."},
    {"color", "Shapes which frequencies saturate most: a bell boosts them before the curve and cuts them the same amount after."},
    {"color.freq", "The frequency Color makes saturate more, or less."},
    {"color.gain", "How much Color boosts before the curve; negative values make that range saturate less."},
    {"output", "The level after saturation, to match the loudness of the dry signal."},
    {"mix", "Blends the original signal with the saturated one."},
    {"quality", "How much the curve is oversampled: higher stays cleaner on bright sounds and uses more CPU."},
};
constexpr auto kParams = describe(kSpecs, kDescriptions);
static_assert(allDescribed(kParams), "every parameter needs a description (SPEC.md)");

// The meters its panel shows, by name (MCP measure_module).
constexpr MeterSpec kMeters[] = {
    {.slot = Saturator::PeakInL, .id = "input_left", .name = "Input, left", .kind = MeterKind::Level},
    {.slot = Saturator::PeakInR, .id = "input_right", .name = "Input, right", .kind = MeterKind::Level},
    {.slot = Saturator::PeakOutL, .id = "output_left", .name = "Output, left", .kind = MeterKind::Level},
    {.slot = Saturator::PeakOutR, .id = "output_right", .name = "Output, right", .kind = MeterKind::Level},
};

constexpr DeviceInfo kInfo{
    .id = "saturator",
    .name = "Saturator",
    .category = "Distortion & Character",
    .kind = DeviceKind::AudioEffect,
    .buses = {.inputChannels = 2, .outputChannels = 2},
    .params = kParams,
    .stateVersion = 1,
    .meters = kMeters,
};

constexpr double kColorQ = 0.7;
} // namespace

Saturator::Saturator() : Device(kInfo) {}

float Saturator::transfer(dsp::ShaperCurve curve, float driveDb, float bias, float x) noexcept
{
    return static_cast<float>(dsp::shape(curve, static_cast<double>(x) * dsp::dbToGain(driveDb) + bias));
}

void Saturator::prepare(const ProcessSpec& spec)
{
    sampleRate_ = spec.sampleRate;
    rampSamples_ = dsp::msToSamples(20.0, sampleRate_);
    fadeSamples_ = dsp::msToSamples(10.0, sampleRate_);
    oversampler_.prepare(grid_.interval(), maxChannels);
    for (auto& c : ch_)
    {
        c.dc.prepare(sampleRate_, 5.0);
        c.dryDelay.assign(static_cast<size_t>(oversampler_.latencySamples() + 1), 0.0f);
    }
    // Processing runs per control segment, so working buffers only need one grid interval.
    for (auto& w : work_)
        w.assign(static_cast<size_t>(grid_.interval()), 0.0f);
    for (auto& d : dry_)
        d.assign(static_cast<size_t>(grid_.interval()), 0.0f);
    reset();
}

void Saturator::reset() noexcept
{
    grid_.reset();
    factor_ = qualityFactor(params().getInt(Quality));
    oversampler_.setFactor(factor_);
    curve_ = static_cast<dsp::ShaperCurve>(params().getInt(Curve));
    curveFadeLeft_ = 0;
    for (auto& c : ch_)
    {
        // Shapers start primed at the current bias, i.e. in steady state for silence.
        c.shaper.setCurve(curve_);
        c.shaper.prime(params().get(Bias));
        c.nextShaper.setCurve(curve_);
        c.nextShaper.prime(params().get(Bias));
        c.colorPre.reset();
        c.colorPost.reset();
        c.dc.reset();
        std::fill(c.dryDelay.begin(), c.dryDelay.end(), 0.0f);
        c.dryPos = 0;
    }
    updateTargets(/*immediate*/ true);
    updateColorCoeffs();
}

void Saturator::updateTargets(bool immediate) noexcept
{
    const auto& p = params();
    const int ramp = immediate ? 0 : rampSamples_;
    drive_.setTarget(dsp::dbToGain(p.get(Drive)), ramp);
    bias_.setTarget(p.get(Bias), ramp);
    colorMix_.setTarget(p.getBool(Color) ? 1.0f : 0.0f, immediate ? 0 : fadeSamples_);
    colorFreq_.setTarget(p.get(ColorFreq), ramp);
    colorGain_.setTarget(p.get(ColorGain), ramp);
    output_.setTarget(dsp::dbToGain(p.get(Output)), ramp);
    mix_.setTarget(p.get(Mix) * 0.01f, ramp);

    const auto curve = static_cast<dsp::ShaperCurve>(p.getInt(Curve));
    if (curve != curve_ && curveFadeLeft_ == 0 && !immediate)
    {
        // Crossfade to the new curve at the oversampled rate.
        for (auto& c : ch_)
        {
            c.nextShaper.setCurve(curve);
            c.nextShaper.prime(c.shaper.lastInput());
        }
        curve_ = curve;
        curveFadeLength_ = std::max(1, fadeSamples_ * factor_);
        curveFadeLeft_ = curveFadeLength_;
    }
}

void Saturator::updateColorCoeffs() noexcept
{
    const double f = colorFreq_.current();
    const double g = colorGain_.current();
    const auto pre = dsp::designBiquad(dsp::BiquadType::Peak, sampleRate_, f, kColorQ, g);
    const auto post = dsp::designBiquad(dsp::BiquadType::Peak, sampleRate_, f, kColorQ, -g);
    for (auto& c : ch_)
    {
        c.colorPre.setCoeffs(pre);
        c.colorPost.setCoeffs(post);
    }
}

void Saturator::process(ProcessContext& ctx) noexcept
{
    const int n = ctx.numSamples;
    if (n <= 0 || ctx.output.numChannels == 0)
        return;
    const int inChannels = std::min(ctx.input.numChannels, maxChannels);
    const int channels = std::min(ctx.output.numChannels, maxChannels);

    // Quality is not automatable (ADR 0016); a change applies at a block boundary.
    if (const int f = qualityFactor(params().getInt(Quality)); f != factor_)
    {
        factor_ = f;
        oversampler_.setFactor(factor_);
    }

    float peakIn[maxChannels] = {}, peakOut[maxChannels] = {};
    const int latency = oversampler_.latencySamples();

    // The whole pipeline runs per control segment (at most one grid interval), so every stage
    // sees the same parameter timeline whatever the host block size.
    grid_.forEachSegment(n, [&](int offset, int length, bool isTick) noexcept {
        if (isTick)
        {
            updateTargets(false);
            updateColorCoeffs();
        }

        std::array<float, 64> outGain{}, mix{}, colorMix{}, bias{};
        float* work[maxChannels] = {work_[0].data(), work_[1].data()};

        // Stage 1 (base rate): drive and Color pre-emphasis. Keep the dry signal.
        for (int i = 0; i < length; ++i)
        {
            const float drive = drive_.next();
            bias[static_cast<size_t>(i)] = bias_.next();
            colorMix[static_cast<size_t>(i)] = colorMix_.next();
            outGain[static_cast<size_t>(i)] = output_.next();
            mix[static_cast<size_t>(i)] = mix_.next();
            colorFreq_.next();
            colorGain_.next();
            for (int c = 0; c < channels; ++c)
            {
                const float x = ctx.input.channel(std::min(c, inChannels - 1))[offset + i];
                peakIn[c] = std::max(peakIn[c], std::abs(x));
                dry_[static_cast<size_t>(c)][static_cast<size_t>(i)] = x;
                float d = x * drive;
                d += colorMix[static_cast<size_t>(i)] * (ch_[static_cast<size_t>(c)].colorPre.process(d) - d);
                work[c][i] = d;
            }
        }

        // Stage 2 (oversampled): bias, the shaper, and removal of the shaper's static output
        // shape(b), so zero input gives exactly zero output: y = shape(u + b) - shape(b).
        // Adding the bias here (not before upsampling) keeps the filters free of DC, so there
        // is no start-up thump. Curve changes crossfade between two shapers.
        float* const* up = oversampler_.upsample(work, channels, length);
        const int upN = length * factor_;
        int fadeLeft = curveFadeLeft_;
        for (int c = 0; c < channels; ++c)
        {
            auto& chan = ch_[static_cast<size_t>(c)];
            fadeLeft = curveFadeLeft_;
            float b = 0.0f, offsetA = 0.0f, offsetB = 0.0f;
            for (int j = 0; j < upN; ++j)
            {
                if (j % factor_ == 0)
                {
                    b = bias[static_cast<size_t>(j / factor_)];
                    offsetA = static_cast<float>(dsp::shape(chan.shaper.curve(), b));
                    offsetB = static_cast<float>(dsp::shape(chan.nextShaper.curve(), b));
                }
                float& u = up[c][j];
                const float x = u + b;
                if (fadeLeft > 0)
                {
                    const float t = 1.0f - static_cast<float>(fadeLeft) / static_cast<float>(curveFadeLength_);
                    const float ya = chan.shaper.process(x) - offsetA;
                    const float yb = chan.nextShaper.process(x) - offsetB;
                    u = ya + t * (yb - ya);
                    if (--fadeLeft == 0)
                    {
                        chan.shaper.setCurve(chan.nextShaper.curve());
                        chan.shaper.prime(chan.nextShaper.lastInput());
                        offsetA = offsetB;
                    }
                }
                else
                {
                    u = chan.shaper.process(x) - offsetA;
                }
            }
        }
        curveFadeLeft_ = fadeLeft;
        oversampler_.downsample(work, channels, length);

        // Stage 3 (base rate): DC blocker (signal-dependent DC from asymmetric curves), Color
        // de-emphasis, output gain, and dry/wet with the dry signal delayed by the latency.
        for (int i = 0; i < length; ++i)
        {
            for (int c = 0; c < channels; ++c)
            {
                auto& chan = ch_[static_cast<size_t>(c)];
                float w = chan.dc.process(work[c][i]);
                w += colorMix[static_cast<size_t>(i)] * (chan.colorPost.process(w) - w);
                w *= outGain[static_cast<size_t>(i)];

                const int size = static_cast<int>(chan.dryDelay.size());
                chan.dryDelay[static_cast<size_t>(chan.dryPos)] = dry_[static_cast<size_t>(c)][static_cast<size_t>(i)];
                const float dry = chan.dryDelay[static_cast<size_t>((chan.dryPos - latency + size) % size)];
                chan.dryPos = (chan.dryPos + 1) % size;

                const float y = dry + mix[static_cast<size_t>(i)] * (w - dry);
                ctx.output.channel(c)[offset + i] = y;
                peakOut[c] = std::max(peakOut[c], std::abs(y));
            }
        }
    });

    for (int c = 0; c < channels; ++c)
    {
        analysis().publishPeak(c == 0 ? PeakInL : PeakInR, peakIn[c]);
        analysis().publishPeak(c == 0 ? PeakOutL : PeakOutR, peakOut[c]);
    }
}

const DeviceFactory& saturatorFactory()
{
    static const DeviceFactory factory{
        .info = []() -> const DeviceInfo& { return kInfo; },
        .create = []() -> std::unique_ptr<Device> { return std::make_unique<Saturator>(); },
    };
    return factory;
}
} // namespace fd::devices
