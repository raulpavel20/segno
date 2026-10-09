#include "Limiter.h"

#include "fd/dsp/Math.h"

#include <algorithm>
#include <cmath>

namespace fd::devices
{
namespace
{
using P = Limiter;

constexpr std::string_view kModes[] = {"Standard", "Maximise"};
constexpr std::string_view kLinks[] = {"Stereo", "L/R", "M/S"};
constexpr std::string_view kLookahead[] = {"1.5 ms", "3 ms", "6 ms"};

constexpr auto kToggle = ParamKind::Toggle;
constexpr auto kChoice = ParamKind::Choice;

constexpr std::array<ParamSpec, P::NumParams> kSpecs{{
    {.id = "gain", .name = "Gain", .shortName = "Gain", .min = 0.0f, .max = 24.0f, .def = 0.0f,
     .unit = Unit::Decibels},
    {.id = "ceiling", .name = "Ceiling", .shortName = "Ceiling", .min = -24.0f, .max = 0.0f, .def = -0.3f,
     .unit = Unit::Decibels},
    {.id = "release", .name = "Release", .shortName = "Release", .min = 1.0f, .max = 1000.0f, .def = 80.0f,
     .skewCentre = 80.0f, .unit = Unit::Milliseconds, .smoothing = {SmoothingKind::Multiplicative, 20.0f},
     .decimals = 0},
    {.id = "release.auto", .name = "Auto Release", .shortName = "Auto Release", .kind = kToggle, .min = 0.0f,
     .max = 1.0f, .def = 1.0f},
    {.id = "mode", .name = "Mode", .shortName = "Mode", .kind = kChoice, .min = 0.0f, .max = 1.0f, .def = 0.0f,
     .choices = kModes},
    {.id = "truepeak", .name = "True Peak", .shortName = "True Peak", .kind = kToggle, .min = 0.0f, .max = 1.0f,
     .def = 1.0f, .flags = ParamFlags::ChangesLatency},
    {.id = "link", .name = "Channels", .shortName = "Channels", .kind = kChoice, .min = 0.0f, .max = 2.0f,
     .def = 0.0f, .choices = kLinks},
    {.id = "lookahead", .name = "Lookahead", .shortName = "Lookahead", .kind = kChoice, .min = 0.0f, .max = 2.0f,
     .def = 1.0f, .choices = kLookahead, .flags = ParamFlags::ChangesLatency},
}};

// What each parameter does, in one sentence: the same as in SPEC.md (open question B22).
constexpr ParamDescription kDescriptions[] = {
    {"gain", "Drives the signal into the limiter, making it louder."},
    {"ceiling", "The highest level the output reaches."},
    {"release", "How quickly the limiter lets go after a peak."},
    {"release.auto", "Adapts the release to the material, for loudness without pumping."},
    {"mode", "Standard: lowering the Ceiling limits harder; Maximise: the Ceiling moves the whole output, the "
             "limiting stays the same."},
    {"truepeak", "Also catches peaks between samples, so the output stays under the ceiling after conversion."},
    {"link", "Whether both channels are limited together, separately, or as mid and side."},
    {"lookahead", "How far ahead the limiter looks: longer is smoother, shorter adds less delay."},
};
constexpr auto kParams = describe(kSpecs, kDescriptions);
static_assert(allDescribed(kParams), "every parameter needs a description (SPEC.md)");

constexpr DeviceInfo kInfo{
    .id = "limiter",
    .name = "Limiter",
    .category = "Dynamics",
    .kind = DeviceKind::AudioEffect,
    .buses = {.inputChannels = 2, .outputChannels = 2},
    .params = kParams,
    .stateVersion = 1,
};

constexpr double kMaxLookaheadMs = 6.0;
constexpr int kDetectorLatency = dsp::TruePeakDetector::latencySamples();
constexpr double kAutoMemoryMs = 400.0; // how long limiting must last to count as sustained
constexpr double kAutoFastFactor = 0.25; // auto release: from a quarter of the Release setting...
constexpr double kAutoSlowFactor = 4.0;  // ...to four times it

/// The gain that brings a peak down to the threshold (1 when it's under).
float needed(float peak, float threshold) noexcept
{
    return peak > threshold ? threshold / peak : 1.0f;
}
} // namespace

Limiter::Limiter() : Device(kInfo) {}

double Limiter::lookaheadMs(int setting) noexcept
{
    return setting <= 0 ? 1.5 : setting == 1 ? 3.0 : kMaxLookaheadMs;
}

int Limiter::latencySamples() const noexcept
{
    const auto& p = params();
    return static_cast<int>(std::lround(lookaheadMs(p.getInt(Lookahead)) * 0.001 * sampleRate_)) +
           (p.getBool(TruePeak) ? kDetectorLatency : 0);
}

void Limiter::prepare(const ProcessSpec& spec)
{
    sampleRate_ = spec.sampleRate;
    rampSamples_ = dsp::msToSamples(20.0, sampleRate_);
    switchSamples_ = dsp::msToSamples(10.0, sampleRate_);
    holdSamples_ = dsp::msToSamples(holdMs, sampleRate_);
    historyStep_ = std::max(1, dsp::msToSamples(historyStepMs, sampleRate_));
    const int maxLookahead = static_cast<int>(std::lround(kMaxLookaheadMs * 0.001 * sampleRate_));
    for (auto& path : paths_)
    {
        path.hold.prepare(maxLookahead + 1 + holdSamples_);
        path.ramp.prepare(maxLookahead + 1);
    }
    for (auto& a : audio_)
        a.prepare(maxLookahead + kDetectorLatency);
    makeupDelay_.prepare(maxLookahead + kDetectorLatency);
    ceilingDelay_.prepare(maxLookahead + kDetectorLatency);
    for (auto& d : detectorAlign_)
        d.prepare(kDetectorLatency);
    detector_.prepare(4);
    outputPeaks_.prepare(maxChannels);
    loudness_.prepare(sampleRate_, maxChannels);
    analysis().fifo(historyFifo).prepare(4096);
    appliedLookahead_ = -1;
    reset();
}

void Limiter::reset() noexcept
{
    grid_.reset();
    applyLatency();
    updateTargets(/*immediate*/ true);
    updateTimes(/*immediate*/ true);
    updateModes();
    outputPeaks_.reset();
    loudness_.reset();
    truePeakMax_ = 0.0f;
    loudnessUntilPublish_ = 0;
    historyPos_ = 0;
    historyIn_ = historyOut_ = historyReduction_ = 0.0f;
}

void Limiter::applyLatency() noexcept
{
    // Lookahead and True Peak are configuration settings (ADR 0020): they apply at a block
    // boundary and restart the gain computers.
    const auto& p = params();
    appliedLookahead_ = p.getInt(Lookahead);
    appliedTruePeak_ = p.getBool(TruePeak);
    lookaheadSamples_ = static_cast<int>(std::lround(lookaheadMs(appliedLookahead_) * 0.001 * sampleRate_));
    for (auto& path : paths_)
    {
        path.hold.setLength(lookaheadSamples_ + 1 + holdSamples_);
        path.ramp.setLength(lookaheadSamples_ + 1, 1.0f);
        path.release.reset(0.0f);
        path.autoRelease.reset(0.0f);
    }
    for (auto& a : audio_)
        a.setDelay(latencySamples());
    makeupDelay_.setDelay(latencySamples());
    ceilingDelay_.setDelay(latencySamples());
    for (auto& d : detectorAlign_)
        d.setDelay(kDetectorLatency);
    detector_.reset();
}

void Limiter::updateTargets(bool immediate) noexcept
{
    const auto& p = params();
    const int ramp = immediate ? 0 : rampSamples_;
    const int fade = immediate ? 0 : switchSamples_;
    gainDb_.setTarget(p.get(Gain), ramp);
    ceilingDb_.setTarget(p.get(Ceiling), ramp);
    maximise_.setTarget(p.getInt(Mode) == static_cast<int>(ModeType::Maximise) ? 1.0f : 0.0f, fade);
    const auto link = static_cast<LinkType>(p.getInt(Link));
    linked_.setTarget(link == LinkType::Stereo ? 1.0f : 0.0f, fade);
    leftRight_.setTarget(link == LinkType::LeftRight ? 1.0f : 0.0f, fade);
    midSide_.setTarget(link == LinkType::MidSide ? 1.0f : 0.0f, fade);
}

void Limiter::updateTimes(bool immediate) noexcept
{
    const int ticks = immediate ? 0 : std::max(1, rampSamples_ / grid_.interval());
    releaseMs_.setTarget(params().get(Release), ticks);
    const double release = releaseMs_.next();
    // The ramp brings the gain down; the smoothers only let it back up.
    const double fixed = dsp::releaseCoefficient(release, sampleRate_);
    const double fast = dsp::releaseCoefficient(release * kAutoFastFactor, sampleRate_);
    const double slow = dsp::releaseCoefficient(release * kAutoSlowFactor, sampleRate_);
    const double memory = dsp::attackCoefficient(kAutoMemoryMs, sampleRate_);
    for (auto& path : paths_)
    {
        path.release.setCoefficients(0.0, fixed);
        path.autoRelease.setCoefficients(0.0, fast, slow, memory);
    }
}

void Limiter::updateModes() noexcept
{
    const bool autoRelease = params().getBool(ReleaseAuto);
    if (autoRelease == autoRelease_)
        return;
    // Hand the current gain over, so switching release modes never jumps.
    for (auto& path : paths_)
    {
        if (autoRelease)
            path.autoRelease.reset(path.release.value());
        else
            path.release.reset(path.autoRelease.value());
    }
    autoRelease_ = autoRelease;
}

float Limiter::gainFor(GainPath& path, float neededGain) noexcept
{
    const float ramped = path.ramp.process(path.hold.process(neededGain));
    const float db = dsp::gainToDb(std::min(ramped, 1.0f));
    const float released = autoRelease_ ? path.autoRelease.process(db) : path.release.process(db);
    return dsp::dbToGain(released);
}

void Limiter::process(ProcessContext& ctx) noexcept
{
    const int n = ctx.numSamples;
    if (n <= 0 || ctx.output.numChannels == 0)
        return;
    const int inChannels = std::max(1, std::min(ctx.input.numChannels, maxChannels));
    const int channels = std::min(ctx.output.numChannels, maxChannels);

    if (params().getInt(Lookahead) != appliedLookahead_ || params().getBool(TruePeak) != appliedTruePeak_)
        applyLatency();
    if (analysis().takeRequest(ResetLoudness))
    {
        loudness_.reset();
        truePeakMax_ = 0.0f;
    }
    const bool truePeak = appliedTruePeak_;

    float peakOut[maxChannels] = {};
    float maxReduction = 0.0f;

    grid_.forEachSegment(n, [&](int offset, int length, bool isTick) noexcept {
        if (isTick)
        {
            updateTargets(false);
            updateTimes(false);
            updateModes();
        }
        for (int i = offset; i < offset + length; ++i)
        {
            const float drive = dsp::dbToGain(gainDb_.next());
            const float ceilingDb = ceilingDb_.next();
            const float maximise = maximise_.next();
            const float wLinked = linked_.next(), wLeftRight = leftRight_.next(), wMidSide = midSide_.next();
            // Standard limits at the ceiling; Maximise limits at full scale and then moves the
            // whole output down to the ceiling. Either way the output stops at the ceiling. The
            // makeup and the ceiling apply to the delayed audio, so they are delayed with it.
            const float threshold = dsp::dbToGain(ceilingDb * (1.0f - maximise));
            const float makeup = dsp::dbToGain(makeupDelay_.process(ceilingDb * maximise));
            const float ceiling = dsp::dbToGain(ceilingDelay_.process(ceilingDb));

            const float l = ctx.input.channel(0)[i] * drive;
            const float r = ctx.input.channel(inChannels - 1)[i] * drive;
            const float mid = 0.5f * (l + r), side = 0.5f * (l - r);

            // Peaks: the samples, and with True Peak what lies between them (as late as the
            // interpolator, which the audio delay includes).
            const float signal[4] = {l, r, mid, side};
            float peak[4];
            for (int k = 0; k < 4; ++k)
                peak[k] = truePeak ? std::max(std::abs(detectorAlign_[static_cast<size_t>(k)].process(signal[k])),
                                              detector_.processSample(k, signal[k]))
                                   : std::abs(signal[k]);

            // What each gain computer needs. M/S: the mid is limited to the threshold and the
            // side takes the room left, so the decoded channels stay under it too.
            const float needL = needed(peak[0], threshold), needR = needed(peak[1], threshold);
            const float needMid = needed(peak[2], threshold);
            const float room = std::max(0.0f, threshold - needMid * peak[2]);
            const float needSide = peak[3] > room ? room / peak[3] : 1.0f;
            const float gLinked = gainFor(paths_[Linked], std::min(needL, needR));
            const float gL = gainFor(paths_[Left], needL);
            const float gR = gainFor(paths_[Right], needR);
            const float gMid = gainFor(paths_[Mid], needMid);
            const float gSide = gainFor(paths_[Side], needSide);

            const float a0 = audio_[0].process(l), a1 = audio_[1].process(r);
            const float aMid = 0.5f * (a0 + a1), aSide = 0.5f * (a0 - a1);
            const float y[maxChannels] = {
                (wLinked * a0 * gLinked + wLeftRight * a0 * gL + wMidSide * (aMid * gMid + aSide * gSide)) * makeup,
                (wLinked * a1 * gLinked + wLeftRight * a1 * gR + wMidSide * (aMid * gMid - aSide * gSide)) * makeup};

            float loudest = 0.0f;
            for (int c = 0; c < maxChannels; ++c)
            {
                // A last guard for what smoothing can't foresee (the ceiling moving).
                const float out = std::clamp(y[c], -ceiling, ceiling);
                if (c < channels)
                    ctx.output.channel(c)[i] = out;
                const float tp = std::max(std::abs(out), outputPeaks_.processSample(c, out));
                peakOut[c] = std::max(peakOut[c], tp);
                loudest = std::max(loudest, std::abs(out));
            }

            const float effective = wLinked * gLinked + wLeftRight * std::min(gL, gR) + wMidSide * gMid;
            const float reduction = -dsp::gainToDb(effective);
            maxReduction = std::max(maxReduction, reduction);

            // History for the display: one frame per step.
            historyIn_ = std::max({historyIn_, std::abs(l), std::abs(r)});
            historyOut_ = std::max(historyOut_, loudest);
            historyReduction_ = std::max(historyReduction_, reduction);
            if (++historyPos_ >= historyStep_)
            {
                const float frame[3] = {dsp::gainToDb(historyIn_), dsp::gainToDb(historyOut_), historyReduction_};
                analysis().fifo(historyFifo).pushFrame(frame, 3);
                historyPos_ = 0;
                historyIn_ = historyOut_ = historyReduction_ = 0.0f;
            }
        }
    });

    // Loudness of the output (mono: as one channel heard on both sides).
    const float* out[maxChannels] = {ctx.output.channel(0), ctx.output.channel(channels - 1)};
    loudness_.process(out, n);
    truePeakMax_ = std::max({truePeakMax_, peakOut[0], peakOut[1]});
    analysis().publish(ShortTermLufs, loudness_.shortTerm());
    if ((loudnessUntilPublish_ -= n) <= 0) // the integrated value needs a scan: every 100 ms
    {
        analysis().publish(IntegratedLufs, loudness_.integrated());
        loudnessUntilPublish_ = dsp::msToSamples(100.0, sampleRate_);
    }
    analysis().publish(TruePeakMax, dsp::gainToDb(truePeakMax_));
    analysis().publishPeak(PeakOutL, peakOut[0]);
    analysis().publishPeak(PeakOutR, channels > 1 ? peakOut[1] : peakOut[0]);
    analysis().publishPeak(GainReduction, maxReduction);
}

const DeviceFactory& limiterFactory()
{
    static const DeviceFactory factory{
        .info = []() -> const DeviceInfo& { return kInfo; },
        .create = []() -> std::unique_ptr<Device> { return std::make_unique<Limiter>(); },
    };
    return factory;
}
} // namespace fd::devices
