#include "Compressor.h"

#include "fd/dsp/Math.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace fd::devices
{
namespace
{
using P = Compressor;

constexpr std::string_view kCharacters[] = {"Clean", "Glue"};
constexpr std::string_view kDetectors[] = {"Peak", "RMS"};
constexpr std::string_view kLookahead[] = {"Off", "1 ms", "10 ms"};
constexpr std::string_view kScFilters[] = {"Off", "High Pass", "Low Pass", "Band Pass", "Bell"};

constexpr auto kToggle = ParamKind::Toggle;
constexpr auto kChoice = ParamKind::Choice;

constexpr std::array<ParamSpec, P::NumParams> kSpecs{{
    {.id = "threshold", .name = "Threshold", .shortName = "Thresh", .min = -60.0f, .max = 0.0f, .def = -18.0f,
     .unit = Unit::Decibels},
    {.id = "ratio", .name = "Ratio", .shortName = "Ratio", .min = 1.0f, .max = Compressor::infiniteRatio, .def = 4.0f,
     .skewCentre = 4.0f, .unit = Unit::Ratio, .flags = ParamFlags::Automatable | ParamFlags::InfinityAtMax},
    {.id = "knee", .name = "Knee", .shortName = "Knee", .min = 0.0f, .max = 24.0f, .def = 6.0f, .unit = Unit::Decibels},
    {.id = "attack", .name = "Attack", .shortName = "Attack", .min = 0.01f, .max = 300.0f, .def = 10.0f,
     .skewCentre = 10.0f, .unit = Unit::Milliseconds, .smoothing = {SmoothingKind::Multiplicative, 20.0f},
     .decimals = 2},
    {.id = "release", .name = "Release", .shortName = "Release", .min = 1.0f, .max = 3000.0f, .def = 150.0f,
     .skewCentre = 150.0f, .unit = Unit::Milliseconds, .smoothing = {SmoothingKind::Multiplicative, 20.0f},
     .decimals = 0},
    {.id = "release.auto", .name = "Auto Release", .shortName = "Auto Release", .kind = kToggle, .min = 0.0f,
     .max = 1.0f},
    {.id = "makeup", .name = "Makeup", .shortName = "Makeup", .min = -24.0f, .max = 36.0f, .def = 0.0f,
     .unit = Unit::Decibels},
    {.id = "makeup.auto", .name = "Auto Makeup", .shortName = "Auto Makeup", .kind = kToggle, .min = 0.0f, .max = 1.0f},
    {.id = "character", .name = "Character", .shortName = "Character", .kind = kChoice, .min = 0.0f, .max = 1.0f,
     .def = 0.0f, .choices = kCharacters},
    {.id = "detector", .name = "Detector", .shortName = "Detector", .kind = kChoice, .min = 0.0f, .max = 1.0f,
     .def = 0.0f, .choices = kDetectors},
    {.id = "link", .name = "Stereo Link", .shortName = "Link", .min = 0.0f, .max = 100.0f, .def = 100.0f,
     .unit = Unit::Percent, .decimals = 0},
    {.id = "lookahead", .name = "Lookahead", .shortName = "Lookahead", .kind = kChoice, .min = 0.0f, .max = 2.0f,
     .def = 0.0f, .choices = kLookahead, .flags = ParamFlags::ChangesLatency},
    {.id = "sc.filter", .name = "Sidechain Filter", .shortName = "Filter", .kind = kChoice, .min = 0.0f, .max = 4.0f,
     .def = 0.0f, .choices = kScFilters},
    {.id = "sc.freq", .name = "Sidechain Freq", .shortName = "Freq", .min = 20.0f, .max = 20000.0f, .def = 100.0f,
     .skewCentre = 632.0f, .unit = Unit::Hertz, .smoothing = {SmoothingKind::Multiplicative, 20.0f}, .decimals = 0},
    {.id = "sc.q", .name = "Sidechain Q", .shortName = "Q", .min = 0.1f, .max = 10.0f, .def = 0.71f,
     .skewCentre = 1.0f, .smoothing = {SmoothingKind::Multiplicative, 20.0f}, .decimals = 2},
    {.id = "sc.gain", .name = "Sidechain Gain", .shortName = "Gain", .min = -24.0f, .max = 24.0f, .def = 0.0f,
     .unit = Unit::Decibels},
    {.id = "sc.listen", .name = "Listen", .shortName = "Listen", .kind = kToggle, .min = 0.0f, .max = 1.0f},
    {.id = "mix", .name = "Mix", .shortName = "Mix", .min = 0.0f, .max = 100.0f, .def = 100.0f, .unit = Unit::Percent,
     .decimals = 0},
    {.id = "output", .name = "Output", .shortName = "Output", .min = -24.0f, .max = 24.0f, .def = 0.0f,
     .unit = Unit::Decibels},
}};

// What each parameter does, in one sentence: the same as in SPEC.md (open question B22).
constexpr ParamDescription kDescriptions[] = {
    {"threshold", "The level above which the compressor starts turning the sound down."},
    {"ratio", "How strongly it turns down what goes over the threshold: at 4:1, 4 dB over comes out 1 dB over."},
    {"knee", "How gradually compression starts around the threshold: wider is gentler."},
    {"attack", "How quickly it reacts when the level goes over the threshold."},
    {"release", "How quickly it lets go once the level falls back."},
    {"release.auto", "Adapts the release to the material: fast after short peaks, slower under sustained loudness."},
    {"makeup", "Gain after compression, to bring the level back up."},
    {"makeup.auto", "Sets the makeup gain from the threshold and ratio, so the level stays about the same."},
    {"character", "Clean is transparent; Glue reacts more gently, for holding busses and mixes together."},
    {"detector", "Whether it reacts to peaks or to the average level (RMS)."},
    {"link", "How much the two channels share their gain reduction: 100 % keeps the stereo image steady."},
    {"lookahead", "Delays the audio a little so the compressor catches fast peaks in time."},
    {"sc.filter", "Filters what the compressor listens to, for example so it ignores the bass."},
    {"sc.freq", "The sidechain filter's frequency."},
    {"sc.q", "How narrow the sidechain filter is."},
    {"sc.gain", "How much the sidechain bell boosts or cuts."},
    {"sc.listen", "Plays what the compressor listens to, to set the sidechain filter by ear."},
    {"mix", "Blends the uncompressed signal with the compressed one (parallel compression)."},
    {"output", "The level after the compressor."},
};
constexpr auto kParams = describe(kSpecs, kDescriptions);
static_assert(allDescribed(kParams), "every parameter needs a description (SPEC.md)");

// The meters its panel shows, by name (MCP measure_module).
constexpr MeterSpec kMeters[] = {
    {.slot = Compressor::PeakInL, .id = "input_left", .name = "Input, left", .kind = MeterKind::Level},
    {.slot = Compressor::PeakInR, .id = "input_right", .name = "Input, right", .kind = MeterKind::Level},
    {.slot = Compressor::PeakOutL, .id = "output_left", .name = "Output, left", .kind = MeterKind::Level},
    {.slot = Compressor::PeakOutR, .id = "output_right", .name = "Output, right", .kind = MeterKind::Level},
    {.slot = Compressor::GainReduction, .id = "gain_reduction", .name = "Gain reduction", .kind = MeterKind::Reduction},
    {.slot = Compressor::DetectorLevel, .id = "detector", .name = "What the detector hears", .kind = MeterKind::Level},
};

constexpr DeviceInfo kInfo{
    .id = "compressor",
    .name = "Compressor",
    .category = "Dynamics",
    .kind = DeviceKind::AudioEffect,
    .buses = {.inputChannels = 2, .outputChannels = 2, .sidechainChannels = 2},
    .params = kParams,
    .stateVersion = 1,
    .meters = kMeters,
};

constexpr double kMaxLookaheadMs = 10.0;
constexpr double kAutoMemoryMs = 500.0;    // how long reduction must last to count as sustained
constexpr double kAutoFastFactor = 0.5;    // auto release: from half the Release setting...
constexpr double kAutoSlowFactor = 5.0;    // ...to five times it
constexpr float kGlueClipKnee = 0.8f;      // Glue's output soft-clip starts here

float glueClip(float x) noexcept
{
    const float a = std::abs(x);
    if (a <= kGlueClipKnee)
        return x;
    const float room = 1.0f - kGlueClipKnee;
    return std::copysign(kGlueClipKnee + room * std::tanh((a - kGlueClipKnee) / room), x);
}

float slopeFor(float ratio) noexcept
{
    return ratio >= Compressor::infiniteRatio ? 0.0f : 1.0f / std::max(1.0f, ratio);
}
} // namespace

Compressor::Compressor() : Device(kInfo) {}

double Compressor::lookaheadMs(int setting) noexcept
{
    return setting == 1 ? 1.0 : setting == 2 ? kMaxLookaheadMs : 0.0;
}

float Compressor::curve(float inputDb, float thresholdDb, float ratio, float kneeDb) noexcept
{
    const float slope = slopeFor(ratio);
    return dsp::compressorCurve(inputDb, thresholdDb,
                                slope > 0.0f ? 1.0f / slope : std::numeric_limits<float>::infinity(), kneeDb);
}

float Compressor::autoMakeup(float thresholdDb, float ratio, float kneeDb) noexcept
{
    return -18.0f - curve(-18.0f, thresholdDb, ratio, kneeDb);
}

int Compressor::latencySamples() const noexcept
{
    return static_cast<int>(std::lround(lookaheadMs(params().getInt(Lookahead)) * 0.001 * sampleRate_));
}

void Compressor::prepare(const ProcessSpec& spec)
{
    sampleRate_ = spec.sampleRate;
    rampSamples_ = dsp::msToSamples(20.0, sampleRate_);
    switchSamples_ = dsp::msToSamples(10.0, sampleRate_);
    historyStep_ = std::max(1, dsp::msToSamples(historyStepMs, sampleRate_));
    const int maxDelay = static_cast<int>(std::lround(kMaxLookaheadMs * 0.001 * sampleRate_));
    for (auto& c : ch_)
    {
        c.detector.prepare(sampleRate_, 10.0);
        c.audio.prepare(maxDelay);
    }
    scFilter_.prepare(sampleRate_, switchSamples_);
    analysis().fifo(historyFifo).prepare(4096);
    reset();
}

void Compressor::reset() noexcept
{
    grid_.reset();
    for (auto& c : ch_)
    {
        c.detector.reset();
        c.gain.reset(0.0f);
        c.autoGain.reset(0.0f);
        c.feedback = 0.0f;
    }
    applyLookahead();
    updateTargets(/*immediate*/ true);
    updateTimes(/*immediate*/ true);
    updateModes(/*immediate*/ true);
    updateSidechainFilter(/*immediate*/ true);
    historyPos_ = 0;
    historyIn_ = historyOut_ = historyReduction_ = 0.0f;
}

void Compressor::applyLookahead() noexcept
{
    appliedLookahead_ = params().getInt(Lookahead);
    for (auto& c : ch_)
        c.audio.setDelay(latencySamples());
}

void Compressor::updateTargets(bool immediate) noexcept
{
    const auto& p = params();
    const int ramp = immediate ? 0 : rampSamples_;
    threshold_.setTarget(p.get(Threshold), ramp);
    slope_.setTarget(slopeFor(p.get(Ratio)), ramp);
    knee_.setTarget(p.get(Knee), ramp);
    makeup_.setTarget(p.get(Makeup), ramp);
    output_.setTarget(dsp::dbToGain(p.get(Output)), ramp);
    mix_.setTarget(p.get(Mix) * 0.01f, ramp);
    autoMakeup_.setTarget(p.getBool(MakeupAuto) ? 1.0f : 0.0f, ramp);
    listen_.setTarget(p.getBool(ScListen) ? 1.0f : 0.0f, immediate ? 0 : switchSamples_);
}

void Compressor::updateTimes(bool immediate) noexcept
{
    // The times glide (in ticks) rather than jump, so a fast attack never snaps the gain.
    const auto& p = params();
    const int ticks = immediate ? 0 : std::max(1, rampSamples_ / grid_.interval());
    attackMs_.setTarget(p.get(Attack), ticks);
    releaseMs_.setTarget(p.get(Release), ticks);
    const double attack = dsp::attackCoefficient(attackMs_.next(), sampleRate_);
    const double release = releaseMs_.next();
    const double fixed = dsp::releaseCoefficient(release, sampleRate_);
    const double fast = dsp::releaseCoefficient(release * kAutoFastFactor, sampleRate_);
    const double slow = dsp::releaseCoefficient(release * kAutoSlowFactor, sampleRate_);
    const double memory = dsp::attackCoefficient(kAutoMemoryMs, sampleRate_);
    for (auto& c : ch_)
    {
        c.gain.setCoefficients(attack, fixed);
        c.autoGain.setCoefficients(attack, fast, slow, memory);
    }
}

void Compressor::updateModes(bool immediate) noexcept
{
    // Character and Detector crossfade (10 ms) rather than switch.
    const auto& p = params();
    const int fade = immediate ? 0 : switchSamples_;
    glue_.setTarget(p.getInt(Character) == static_cast<int>(CharacterType::Glue) ? 1.0f : 0.0f, fade);
    rms_.setTarget(p.getInt(Detector) == 1 ? 1.0f : 0.0f, fade);
    const bool autoRelease = p.getBool(ReleaseAuto);
    if (autoRelease != autoRelease_)
    {
        // Hand the current gain over, so switching release modes never jumps.
        for (auto& c : ch_)
        {
            if (autoRelease)
                c.autoGain.reset(c.gain.value());
            else
                c.gain.reset(c.autoGain.value());
        }
        autoRelease_ = autoRelease;
    }
}

void Compressor::updateSidechainFilter(bool immediate) noexcept
{
    const auto& p = params();
    const auto type = static_cast<ScFilterType>(p.getInt(ScFilter));
    if (immediate)
        scFilter_.reset(type, p.get(ScFreq), p.get(ScQ), p.get(ScGain));
    else
        scFilter_.update(type, p.get(ScFreq), p.get(ScQ), p.get(ScGain), rampSamples_ / grid_.interval());
}

void Compressor::process(ProcessContext& ctx) noexcept
{
    const int n = ctx.numSamples;
    if (n <= 0 || ctx.output.numChannels == 0)
        return;
    const int inChannels = std::max(1, std::min(ctx.input.numChannels, maxChannels));
    const int channels = std::min(ctx.output.numChannels, maxChannels);
    const int scChannels = std::min(ctx.sidechain.numChannels, maxChannels);

    // Lookahead is a configuration setting (ADR 0020): it applies at a block boundary.
    if (params().getInt(Lookahead) != appliedLookahead_)
        applyLookahead();

    const float link = params().get(Link) * 0.01f;

    float peakIn[maxChannels] = {}, peakOut[maxChannels] = {};
    float detectorLevel = dsp::minusInfinityDb, maxReduction = 0.0f;

    grid_.forEachSegment(n, [&](int offset, int length, bool isTick) noexcept {
        if (isTick)
        {
            updateTargets(false);
            updateTimes(false);
            updateModes(false);
            updateSidechainFilter(false);
        }
        for (int i = offset; i < offset + length; ++i)
        {
            const float threshold = threshold_.next();
            const float slope = slope_.next();
            const float knee = knee_.next();
            const float makeupDb = makeup_.next();
            const float outGain = output_.next();
            const float mix = mix_.next();
            const float listen = listen_.next();
            const float glue = glue_.next();
            const float rms = rms_.next();
            const float ratio = slope > 0.0f ? 1.0f / slope : std::numeric_limits<float>::infinity();
            const float autoMakeup = autoMakeup_.next();
            const float makeup =
                autoMakeup > 0.0f
                    ? makeupDb + autoMakeup * (-18.0f - dsp::compressorCurve(-18.0f, threshold, ratio, knee) - makeupDb)
                    : makeupDb;

            // The detector: the sidechain if one is connected, else the input (Glue: the output),
            // through the sidechain filter.
            float levels[maxChannels] = {}, heard[maxChannels] = {};
            for (int c = 0; c < channels; ++c)
            {
                const float x = ctx.input.channel(std::min(c, inChannels - 1))[i];
                heard[c] = scChannels > 0 ? ctx.sidechain.channel(std::min(c, scChannels - 1))[i]
                                          : x + glue * (ch_[static_cast<size_t>(c)].feedback - x);
            }
            scFilter_.process(heard, channels);
            for (int c = 0; c < channels; ++c)
            {
                auto& ch = ch_[static_cast<size_t>(c)];
                const auto level = ch.detector.process(heard[c]);
                levels[c] = dsp::gainToDb(level.peak + rms * (level.rms - level.peak));
                detectorLevel = std::max(detectorLevel, levels[c]);
            }
            dsp::linkLevels(levels, channels, link);

            for (int c = 0; c < channels; ++c)
            {
                auto& ch = ch_[static_cast<size_t>(c)];
                const float x = ctx.input.channel(std::min(c, inChannels - 1))[i];
                peakIn[c] = std::max(peakIn[c], std::abs(x));
                const float target = dsp::compressorCurve(levels[c], threshold, ratio, knee) - levels[c];
                const float gainDb = autoRelease_ ? ch.autoGain.process(target) : ch.gain.process(target);
                maxReduction = std::max(maxReduction, -gainDb);

                const float dry = ch.audio.process(x); // the lookahead delay, for both paths
                float wet = dry * dsp::dbToGain(gainDb);
                ch.feedback = wet;
                wet *= dsp::dbToGain(makeup);
                if (glue > 0.0f)
                    wet += glue * (glueClip(wet) - wet);
                wet += listen * (heard[c] - wet); // Listen: the detector's signal replaces the wet path

                wet *= outGain;
                const float y = dry + mix * (wet - dry);
                ctx.output.channel(c)[i] = y;
                peakOut[c] = std::max(peakOut[c], std::abs(y));
                historyIn_ = std::max(historyIn_, std::abs(x));
                historyOut_ = std::max(historyOut_, std::abs(y));
                historyReduction_ = std::max(historyReduction_, -gainDb);
            }

            // History for the display: one frame per step.
            if (++historyPos_ >= historyStep_)
            {
                const float frame[3] = {dsp::gainToDb(historyIn_), dsp::gainToDb(historyOut_), historyReduction_};
                analysis().fifo(historyFifo).pushFrame(frame, 3);
                historyPos_ = 0;
                historyIn_ = historyOut_ = historyReduction_ = 0.0f;
            }
        }
    });

    // Mono: both meter channels show the one channel.
    analysis().publishPeak(PeakInL, peakIn[0]);
    analysis().publishPeak(PeakInR, channels > 1 ? peakIn[1] : peakIn[0]);
    analysis().publishPeak(PeakOutL, peakOut[0]);
    analysis().publishPeak(PeakOutR, channels > 1 ? peakOut[1] : peakOut[0]);
    analysis().publishPeak(GainReduction, maxReduction);
    analysis().publishPeak(DetectorLevel, dsp::dbToGain(detectorLevel));
}

const DeviceFactory& compressorFactory()
{
    static const DeviceFactory factory{
        .info = []() -> const DeviceInfo& { return kInfo; },
        .create = []() -> std::unique_ptr<Device> { return std::make_unique<Compressor>(); },
    };
    return factory;
}
} // namespace fd::devices
