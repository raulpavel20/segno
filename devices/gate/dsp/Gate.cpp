#include "Gate.h"

#include "fd/dsp/Math.h"

#include <algorithm>
#include <cmath>

namespace fd::devices
{
namespace
{
using P = Gate;

constexpr std::string_view kModes[] = {"Gate", "Expander"};
constexpr std::string_view kLookahead[] = {"Off", "1 ms", "10 ms"};
constexpr std::string_view kScFilters[] = {"Off", "High Pass", "Low Pass", "Band Pass", "Bell"};

constexpr auto kToggle = ParamKind::Toggle;
constexpr auto kChoice = ParamKind::Choice;
constexpr float kRangeMinDb = -80.0f; // the bottom of Range's travel: silence

constexpr std::array<ParamSpec, P::NumParams> kSpecs{{
    {.id = "threshold", .name = "Threshold", .shortName = "Thresh", .min = -80.0f, .max = 0.0f, .def = -40.0f,
     .unit = Unit::Decibels},
    {.id = "return", .name = "Return", .shortName = "Return", .min = 0.0f, .max = 24.0f, .def = 3.0f,
     .unit = Unit::Decibels},
    {.id = "mode", .name = "Mode", .shortName = "Mode", .kind = kChoice, .min = 0.0f, .max = 1.0f, .def = 0.0f,
     .choices = kModes},
    {.id = "ratio", .name = "Ratio", .shortName = "Ratio", .min = 1.0f, .max = 10.0f, .def = 2.0f, .skewCentre = 2.0f,
     .unit = Unit::Ratio},
    {.id = "range", .name = "Range", .shortName = "Range", .min = kRangeMinDb, .max = 0.0f, .def = kRangeMinDb,
     .skewCentre = -20.0f, .unit = Unit::Decibels,
     .flags = ParamFlags::Automatable | ParamFlags::MinusInfinityAtMin},
    {.id = "attack", .name = "Attack", .shortName = "Attack", .min = 0.02f, .max = 150.0f, .def = 0.1f,
     .skewCentre = 1.0f, .unit = Unit::Milliseconds, .smoothing = {SmoothingKind::Multiplicative, 20.0f},
     .decimals = 2},
    {.id = "hold", .name = "Hold", .shortName = "Hold", .min = 0.0f, .max = 1000.0f, .def = 20.0f, .skewCentre = 50.0f,
     .unit = Unit::Milliseconds, .decimals = 0},
    {.id = "release", .name = "Release", .shortName = "Release", .min = 1.0f, .max = 3000.0f, .def = 100.0f,
     .skewCentre = 100.0f, .unit = Unit::Milliseconds, .smoothing = {SmoothingKind::Multiplicative, 20.0f},
     .decimals = 0},
    {.id = "flip", .name = "Flip", .shortName = "Flip", .kind = kToggle, .min = 0.0f, .max = 1.0f},
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
}};

// What each parameter does, in one sentence: the same as in SPEC.md (open question B22).
constexpr ParamDescription kDescriptions[] = {
    {"threshold", "The level above which the gate opens."},
    {"return", "How far below the threshold the level must fall before the gate closes, so it doesn't chatter."},
    {"mode", "Gate cuts the sound below the threshold; Expander turns it down gradually."},
    {"ratio", "How strongly the expander turns down what falls below the threshold."},
    {"range", "How far the sound is turned down while the gate is closed; -inf is silence."},
    {"attack", "How quickly the gate opens."},
    {"hold", "How long the gate stays open after the level falls below the threshold."},
    {"release", "How quickly the gate closes after the hold."},
    {"flip", "Inverts the gate: it turns down what's above the threshold and lets the rest through."},
    {"lookahead", "Delays the audio a little so the gate opens before a hit, keeping its attack."},
    {"sc.filter", "Filters what the gate listens to, for example so it ignores the bass."},
    {"sc.freq", "The sidechain filter's frequency."},
    {"sc.q", "How narrow the sidechain filter is."},
    {"sc.gain", "How much the sidechain bell boosts or cuts."},
    {"sc.listen", "Plays what the gate listens to, to set the sidechain filter by ear."},
};
constexpr auto kParams = describe(kSpecs, kDescriptions);
static_assert(allDescribed(kParams), "every parameter needs a description (SPEC.md)");

constexpr DeviceInfo kInfo{
    .id = "gate",
    .name = "Gate",
    .category = "Dynamics",
    .kind = DeviceKind::AudioEffect,
    .buses = {.inputChannels = 2, .outputChannels = 2, .sidechainChannels = 2},
    .params = kParams,
    .stateVersion = 1,
};

constexpr double kMaxLookaheadMs = 10.0;
/// Gains below this are silence: the dB smoother only approaches its floor.
constexpr float kSilentBelowDb = dsp::minusInfinityDb + 1.0f;
} // namespace

Gate::Gate() : Device(kInfo) {}

double Gate::lookaheadMs(int setting) noexcept
{
    return setting == 1 ? 1.0 : setting == 2 ? kMaxLookaheadMs : 0.0;
}

float Gate::rangeDb(float rangeParam) noexcept
{
    return rangeParam <= kRangeMinDb ? dsp::minusInfinityDb : rangeParam;
}

float Gate::expanderGain(float levelDb, float thresholdDb, float ratio, float rangeParam) noexcept
{
    return dsp::expanderCurve(levelDb, thresholdDb, ratio, 0.0f, rangeDb(rangeParam)) - levelDb;
}

int Gate::latencySamples() const noexcept
{
    return static_cast<int>(std::lround(lookaheadMs(params().getInt(Lookahead)) * 0.001 * sampleRate_));
}

void Gate::prepare(const ProcessSpec& spec)
{
    sampleRate_ = spec.sampleRate;
    rampSamples_ = dsp::msToSamples(20.0, sampleRate_);
    switchSamples_ = dsp::msToSamples(10.0, sampleRate_);
    windowSamples_ = std::max(1, dsp::msToSamples(detectorWindowMs, sampleRate_));
    historyStep_ = std::max(1, dsp::msToSamples(historyStepMs, sampleRate_));
    window_.prepare(windowSamples_);
    window_.setLength(windowSamples_);
    const int maxDelay = static_cast<int>(std::lround(kMaxLookaheadMs * 0.001 * sampleRate_));
    for (auto& a : audio_)
        a.prepare(maxDelay);
    scFilter_.prepare(sampleRate_, switchSamples_);
    analysis().fifo(historyFifo).prepare(4096);
    reset();
}

void Gate::reset() noexcept
{
    grid_.reset();
    window_.reset();
    applyLookahead();
    updateTargets(/*immediate*/ true);
    updateTimes(/*immediate*/ true);
    updateSidechainFilter(/*immediate*/ true);
    // Closed, as after silence.
    const float closed = rangeDb(params().get(Range));
    gain_.reset(closed);
    open_ = false;
    holdLeft_ = 0;
    expanderHeld_ = closed;
    expanderHoldLeft_ = 0;
    historyPos_ = 0;
    historyLevel_ = historyOut_ = historyOpen_ = 0.0f;
}

void Gate::applyLookahead() noexcept
{
    appliedLookahead_ = params().getInt(Lookahead);
    for (auto& a : audio_)
        a.setDelay(latencySamples());
}

void Gate::updateTargets(bool immediate) noexcept
{
    const auto& p = params();
    const int ramp = immediate ? 0 : rampSamples_;
    const int fade = immediate ? 0 : switchSamples_;
    threshold_.setTarget(p.get(Threshold), ramp);
    return_.setTarget(p.get(Return), ramp);
    ratio_.setTarget(p.get(Ratio), ramp);
    range_.setTarget(rangeDb(p.get(Range)), ramp);
    expander_.setTarget(p.getInt(Mode) == static_cast<int>(ModeType::Expander) ? 1.0f : 0.0f, fade);
    flip_.setTarget(p.getBool(Flip) ? 1.0f : 0.0f, fade);
    listen_.setTarget(p.getBool(ScListen) ? 1.0f : 0.0f, fade);
}

void Gate::updateTimes(bool immediate) noexcept
{
    // The times glide (in ticks) rather than jump.
    const auto& p = params();
    const int ticks = immediate ? 0 : std::max(1, rampSamples_ / grid_.interval());
    attackMs_.setTarget(p.get(Attack), ticks);
    releaseMs_.setTarget(p.get(Release), ticks);
    holdMs_.setTarget(p.get(Hold), ticks);
    // Opening is the gain going up (attack); closing, going down (release).
    gain_.setCoefficients(dsp::releaseCoefficient(releaseMs_.next(), sampleRate_),
                          dsp::attackCoefficient(attackMs_.next(), sampleRate_));
    // The detector's window already holds the level that long.
    holdArm_ = std::max(0, static_cast<int>(std::lround(holdMs_.next() * 0.001 * sampleRate_)) - windowSamples_);
}

void Gate::updateSidechainFilter(bool immediate) noexcept
{
    const auto& p = params();
    const auto type = static_cast<ScFilterType>(p.getInt(ScFilter));
    if (immediate)
        scFilter_.reset(type, p.get(ScFreq), p.get(ScQ), p.get(ScGain));
    else
        scFilter_.update(type, p.get(ScFreq), p.get(ScQ), p.get(ScGain), rampSamples_ / grid_.interval());
}

void Gate::process(ProcessContext& ctx) noexcept
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

    float peakIn[maxChannels] = {}, peakOut[maxChannels] = {};
    float detectorPeak = 0.0f, maxReduction = 0.0f, anyOpen = 0.0f;

    grid_.forEachSegment(n, [&](int offset, int length, bool isTick) noexcept {
        if (isTick)
        {
            updateTargets(false);
            updateTimes(false);
            updateSidechainFilter(false);
        }
        for (int i = offset; i < offset + length; ++i)
        {
            const float threshold = threshold_.next();
            const float ret = return_.next();
            const float ratio = ratio_.next();
            const float range = range_.next();
            const float expander = expander_.next();
            const float flip = flip_.next();
            const float listen = listen_.next();

            // The detector: the sidechain if one is connected, else the input, through the
            // sidechain filter; one level for every channel (stereo-linked), held over the window.
            float heard[maxChannels] = {};
            for (int c = 0; c < channels; ++c)
                heard[c] = scChannels > 0 ? ctx.sidechain.channel(std::min(c, scChannels - 1))[i]
                                          : ctx.input.channel(std::min(c, inChannels - 1))[i];
            scFilter_.process(heard, channels);
            float peak = 0.0f;
            for (int c = 0; c < channels; ++c)
                peak = std::max(peak, std::abs(heard[c]));
            const float windowPeak = -window_.process(-peak);
            const float level = dsp::gainToDb(windowPeak);
            detectorPeak = std::max(detectorPeak, windowPeak);

            // Gate: opens at the threshold, closes below threshold - return once the hold is over.
            if (level >= threshold)
            {
                open_ = true;
                holdLeft_ = holdArm_;
            }
            else if (open_)
            {
                if (level >= threshold - ret)
                    holdLeft_ = holdArm_;
                else if (holdLeft_ > 0)
                    --holdLeft_;
                else
                    open_ = false;
            }
            const float gateTarget = open_ ? 0.0f : range;

            // Expander: the static curve, its target held for the hold time when it falls.
            const float expanderTarget = dsp::expanderCurve(level, threshold, ratio, 0.0f, range) - level;
            if (expanderTarget >= expanderHeld_)
            {
                expanderHeld_ = expanderTarget;
                expanderHoldLeft_ = holdArm_;
            }
            else if (expanderHoldLeft_ > 0)
                --expanderHoldLeft_;
            else
                expanderHeld_ = expanderTarget;

            const float gainDb = gain_.process(gateTarget + expander * (expanderHeld_ - gateTarget));
            const float g = gainDb < kSilentBelowDb ? 0.0f : dsp::dbToGain(gainDb);
            const float applied = g + flip * (1.0f - 2.0f * g); // Flip: what the gate takes away
            maxReduction = std::max(maxReduction, -gainDb);
            anyOpen = open_ ? 1.0f : anyOpen;

            float loudest = 0.0f;
            for (int c = 0; c < channels; ++c)
            {
                const float x = ctx.input.channel(std::min(c, inChannels - 1))[i];
                peakIn[c] = std::max(peakIn[c], std::abs(x));
                float y = audio_[static_cast<size_t>(c)].process(x) * applied;
                y += listen * (heard[c] - y); // Listen: the detector's signal replaces the output
                ctx.output.channel(c)[i] = y;
                peakOut[c] = std::max(peakOut[c], std::abs(y));
                loudest = std::max(loudest, std::abs(y));
            }

            // History for the display: one frame per step.
            historyLevel_ = std::max(historyLevel_, windowPeak);
            historyOut_ = std::max(historyOut_, loudest);
            historyOpen_ = open_ ? 1.0f : historyOpen_;
            if (++historyPos_ >= historyStep_)
            {
                const float frame[3] = {dsp::gainToDb(historyLevel_), dsp::gainToDb(historyOut_), historyOpen_};
                analysis().fifo(historyFifo).pushFrame(frame, 3);
                historyPos_ = 0;
                historyLevel_ = historyOut_ = historyOpen_ = 0.0f;
            }
        }
    });

    // Mono: both meter channels show the one channel.
    analysis().publishPeak(PeakInL, peakIn[0]);
    analysis().publishPeak(PeakInR, channels > 1 ? peakIn[1] : peakIn[0]);
    analysis().publishPeak(PeakOutL, peakOut[0]);
    analysis().publishPeak(PeakOutR, channels > 1 ? peakOut[1] : peakOut[0]);
    analysis().publishPeak(DetectorLevel, detectorPeak);
    analysis().publishPeak(GainReduction, maxReduction);
    analysis().publishPeak(Open, anyOpen);
}

const DeviceFactory& gateFactory()
{
    static const DeviceFactory factory{
        .info = []() -> const DeviceInfo& { return kInfo; },
        .create = []() -> std::unique_ptr<Device> { return std::make_unique<Gate>(); },
    };
    return factory;
}
} // namespace fd::devices
