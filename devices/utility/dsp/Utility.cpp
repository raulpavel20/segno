#include "Utility.h"

#include "fd/dsp/Math.h"

#include <algorithm>
#include <cmath>

namespace fd::devices
{
namespace
{
constexpr float kGainMinDb = -70.0f;

constexpr std::array<ParamSpec, Utility::NumParams> kParams{{
    {.id = "gain", .name = "Gain", .shortName = "Gain", .min = kGainMinDb, .max = 35.0f, .def = 0.0f,
     .skewCentre = 0.0f, .unit = Unit::Decibels,
     .flags = ParamFlags::Automatable | ParamFlags::MinusInfinityAtMin},
    {.id = "balance", .name = "Balance", .shortName = "Bal", .min = -1.0f, .max = 1.0f, .def = 0.0f,
     .unit = Unit::Pan},
    {.id = "width", .name = "Width", .shortName = "Width", .min = 0.0f, .max = 400.0f, .def = 100.0f,
     .skewCentre = 100.0f, .unit = Unit::Percent, .decimals = 0},
    {.id = "mono", .name = "Mono", .shortName = "Mono", .kind = ParamKind::Toggle, .min = 0.0f, .max = 1.0f},
    {.id = "invert.l", .name = "Invert L", .shortName = "Inv L", .kind = ParamKind::Toggle, .min = 0.0f,
     .max = 1.0f},
    {.id = "invert.r", .name = "Invert R", .shortName = "Inv R", .kind = ParamKind::Toggle, .min = 0.0f,
     .max = 1.0f},
    {.id = "dc", .name = "DC Filter", .shortName = "DC", .kind = ParamKind::Toggle, .min = 0.0f, .max = 1.0f},
    {.id = "mute", .name = "Mute", .shortName = "Mute", .kind = ParamKind::Toggle, .min = 0.0f, .max = 1.0f},
}};

constexpr DeviceInfo kInfo{
    .id = "utility",
    .name = "Utility",
    .category = "Utility",
    .kind = DeviceKind::AudioEffect,
    .buses = {.inputChannels = 2, .outputChannels = 2},
    .params = kParams,
    .stateVersion = 1,
};
} // namespace

const DeviceInfo& Utility::staticInfo() noexcept
{
    return kInfo;
}

Utility::Utility() : Device(kInfo) {}

void Utility::prepare(const ProcessSpec& spec)
{
    sampleRate_ = spec.sampleRate;
    rampSamples_ = dsp::msToSamples(20.0, sampleRate_);
    fadeSamples_ = dsp::msToSamples(10.0, sampleRate_);
    for (auto& b : dcBlockers_)
        b.prepare(sampleRate_, 5.0);
    reset();
}

void Utility::reset() noexcept
{
    grid_.reset();
    for (auto& b : dcBlockers_)
        b.reset();
    updateTargets(/*immediate*/ true);
}

void Utility::updateTargets(bool immediate) noexcept
{
    const auto& p = params();
    const bool mute = p.getBool(Mute);
    const float gainDb = p.get(Gain);
    const float gain = mute || gainDb <= kGainMinDb ? 0.0f : dsp::dbToGain(gainDb);
    const float width = p.getBool(Mono) ? 0.0f : p.get(Width) * 0.01f;

    const int ramp = immediate ? 0 : rampSamples_;
    const int fade = immediate ? 0 : fadeSamples_;
    gain_.setTarget(gain, ramp);
    balance_.setTarget(p.get(Balance), ramp);
    width_.setTarget(width, ramp);
    polarityL_.setTarget(p.getBool(InvertL) ? -1.0f : 1.0f, fade);
    polarityR_.setTarget(p.getBool(InvertR) ? -1.0f : 1.0f, fade);
    dcMix_.setTarget(p.getBool(DcFilter) ? 1.0f : 0.0f, fade);
}

void Utility::process(ProcessContext& ctx) noexcept
{
    const int n = ctx.numSamples;
    if (n <= 0 || ctx.output.numChannels == 0)
        return;

    const float* inL = ctx.input.numChannels > 0 ? ctx.input.channel(0) : nullptr;
    const float* inR = ctx.input.numChannels > 1 ? ctx.input.channel(1) : inL;
    float* outL = ctx.output.channel(0);
    float* outR = ctx.output.numChannels > 1 ? ctx.output.channel(1) : nullptr;

    float peakL = 0.0f;
    float peakR = 0.0f;

    grid_.forEachSegment(n, [&](int offset, int length, bool isTick) noexcept {
        if (isTick)
            updateTargets(/*immediate*/ false);

        for (int i = offset; i < offset + length; ++i)
        {
            float l = inL != nullptr ? inL[i] : 0.0f; // read before writing: buffers may alias
            float r = inR != nullptr ? inR[i] : 0.0f;

            const float dc = dcMix_.next();
            const float dl = dcBlockers_[0].process(l);
            const float dr = dcBlockers_[1].process(r);
            l += dc * (dl - l);
            r += dc * (dr - r);

            l *= polarityL_.next();
            r *= polarityR_.next();

            // At exactly 100 % the mid/side round trip is skipped, keeping Utility bit-transparent.
            const float w = width_.next();
            if (w != 1.0f)
            {
                const float mid = 0.5f * (l + r);
                const float side = 0.5f * (l - r) * w;
                l = mid + side;
                r = mid - side;
            }

            const float b = balance_.next();
            l *= b > 0.0f ? 1.0f - b : 1.0f;
            r *= b < 0.0f ? 1.0f + b : 1.0f;

            const float g = gain_.next();
            l *= g;
            r *= g;

            if (outR != nullptr)
            {
                outL[i] = l;
                outR[i] = r;
            }
            else
            {
                outL[i] = 0.5f * (l + r);
            }
            peakL = std::max(peakL, std::abs(l));
            peakR = std::max(peakR, std::abs(r));
        }
    });

    analysis().publishPeak(PeakOutL, peakL);
    analysis().publishPeak(PeakOutR, peakR);
}

const DeviceFactory& utilityFactory()
{
    static const DeviceFactory factory{
        .info = []() -> const DeviceInfo& { return kInfo; },
        .create = []() -> std::unique_ptr<Device> { return std::make_unique<Utility>(); },
    };
    return factory;
}
} // namespace fd::devices
