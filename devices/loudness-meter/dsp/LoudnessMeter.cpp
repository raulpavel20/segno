#include "LoudnessMeter.h"

#include "fd/dsp/Math.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <nlohmann/json.hpp>

namespace fd::devices
{
namespace
{
using P = LoudnessMeter;

constexpr float kNone = std::numeric_limits<float>::quiet_NaN();

constexpr std::string_view kMeasures[] = {"Song", "Running"};
constexpr std::string_view kScales[] = {"+9", "+18"};
constexpr std::string_view kUnits[] = {"LUFS", "LU"};

constexpr auto kChoice = ParamKind::Choice;
constexpr auto kDisplay = ParamFlags::None; // display settings: never automated

constexpr std::array<ParamSpec, P::NumParams> kSpecs{{
    {.id = "target",
     .name = "Target",
     .shortName = "Target",
     .min = -36.0f,
     .max = -5.0f,
     .def = -14.0f,
     .unit = Unit::Lufs,
     .smoothing = {SmoothingKind::None, 0.0f},
     .flags = kDisplay},
    {.id = "ceiling",
     .name = "Peak Ceiling",
     .shortName = "Ceiling",
     .min = -9.0f,
     .max = 0.0f,
     .def = -1.0f,
     .unit = Unit::Dbtp,
     .smoothing = {SmoothingKind::None, 0.0f},
     .flags = kDisplay},
    {.id = "measure",
     .name = "Measure",
     .shortName = "Measure",
     .kind = kChoice,
     .min = 0.0f,
     .max = 1.0f,
     .def = 0.0f,
     .choices = kMeasures,
     .smoothing = {SmoothingKind::None, 0.0f},
     .flags = kDisplay},
    {.id = "scale",
     .name = "Scale",
     .shortName = "Scale",
     .kind = kChoice,
     .min = 0.0f,
     .max = 1.0f,
     .def = 0.0f,
     .choices = kScales,
     .smoothing = {SmoothingKind::None, 0.0f},
     .flags = kDisplay},
    {.id = "units",
     .name = "Units",
     .shortName = "Units",
     .kind = kChoice,
     .min = 0.0f,
     .max = 1.0f,
     .def = 0.0f,
     .choices = kUnits,
     .smoothing = {SmoothingKind::None, 0.0f},
     .flags = kDisplay},
}};

// What each parameter does, in one sentence: the same as in SPEC.md (open question B22).
constexpr ParamDescription kDescriptions[] = {
    {"target",
     "The loudness you're aiming for: readings are coloured against it, and the scale is centred on it."},
    {"ceiling", "The highest true peak you allow: peaks above it are marked."},
    {"measure",
     "Song counts each moment of the song once, the last time it played; Running counts everything as "
     "it plays."},
    {"scale", "How much the loudness bars and the history show around the target: 9 LU above it, or 18."},
    {"units", "Shows loudness as LUFS, or as LU above or below the target."},
};
constexpr auto kParams = describe(kSpecs, kDescriptions);
static_assert(allDescribed(kParams), "every parameter needs a description (SPEC.md)");

// Its readings, by name (MCP measure_module).
constexpr MeterSpec kMeters[] = {
    {.slot = P::Momentary, .id = "momentary", .name = "Momentary loudness", .kind = MeterKind::Loudness},
    {.slot = P::ShortTerm, .id = "short_term", .name = "Short-term loudness", .kind = MeterKind::Loudness},
    {.slot = P::Integrated, .id = "integrated", .name = "Integrated loudness", .kind = MeterKind::Loudness},
    {.slot = P::Range, .id = "range", .name = "Loudness range", .kind = MeterKind::LoudnessUnits},
    {.slot = P::MaxMomentary,
     .id = "max_momentary",
     .name = "Loudest momentary",
     .kind = MeterKind::Loudness},
    {.slot = P::MaxShortTerm,
     .id = "max_short_term",
     .name = "Loudest short-term",
     .kind = MeterKind::Loudness},
    {.slot = P::TruePeakMax,
     .id = "true_peak_max",
     .name = "True-peak maximum (dBTP)",
     .kind = MeterKind::Decibels},
    {.slot = P::PeakToLoudness,
     .id = "peak_to_loudness",
     .name = "Peak to loudness",
     .kind = MeterKind::LoudnessUnits},
    {.slot = P::Normalised,
     .id = "normalised",
     .name = "Normalised: the gain at the target loudness",
     .kind = MeterKind::Decibels},
    {.slot = P::Correlation,
     .id = "correlation",
     .name = "Stereo correlation",
     .kind = MeterKind::Correlation},
    {.slot = P::CorrelationMin,
     .id = "lowest_correlation",
     .name = "Lowest correlation since reset",
     .kind = MeterKind::Correlation},
    {.slot = P::Balance, .id = "balance", .name = "Balance (+ leans left)", .kind = MeterKind::LoudnessUnits},
    {.slot = P::Counted,
     .id = "counted_seconds",
     .name = "Seconds of song counted",
     .kind = MeterKind::Count},
    {.slot = P::Counting,
     .id = "counting",
     .name = "Counting: 0 yes, 1 paused, 2 waiting for the song to play, 3 past 3 hours",
     .kind = MeterKind::Count},
};

// The measurement is this session's, not the sound's: presets leave it out (ADR 0044).
constexpr std::array<std::string_view, 1> kSessionKeys{LoudnessMeter::measurementKey};

constexpr DeviceInfo kInfo{
    .id = "loudness-meter",
    .name = "Loudness Meter",
    .category = "Utility",
    .kind = DeviceKind::AudioEffect,
    .buses = {.inputChannels = 2, .outputChannels = 2},
    .params = kParams,
    .stateVersion = 1,
    .meters = kMeters,
    .sessionExtraKeys = kSessionKeys,
};

constexpr double kSilence = 1e-7; // -70 dBFS as a mean square: no stereo reading below it

float lufs(double meanSquare) noexcept
{
    return static_cast<float>(dsp::lufsOf(meanSquare));
}
} // namespace

LoudnessMeter::LoudnessMeter() : Device(kInfo) {}

float LoudnessMeter::normalisedGain(float integrated, float truePeakDb, float target, float ceiling) noexcept
{
    if (!std::isfinite(integrated))
        return kNone;
    const float toTarget = target - integrated;
    if (toTarget <= 0.0f || !std::isfinite(truePeakDb))
        return toTarget;
    return std::min(toTarget, std::max(0.0f, ceiling - truePeakDb)); // turned up as far as the peaks allow
}

const dsp::LoudnessStore& LoudnessMeter::store() const noexcept
{
    return stores_[static_cast<size_t>(active_.load())];
}

LoudnessMeter::MeasureMode LoudnessMeter::mode() const noexcept
{
    return params().getInt(Measure) == 1 ? MeasureMode::Running : MeasureMode::Song;
}

LoudnessMeter::MeasureMode LoudnessMeter::countedMode() const noexcept
{
    return appliedMode_ == 1 ? MeasureMode::Running : MeasureMode::Song;
}

void LoudnessMeter::prepare(const ProcessSpec& spec)
{
    sampleRate_ = spec.sampleRate;
    stepLength_ = std::max(slidesPerStep, static_cast<int>(std::lround(sampleRate_ * stepSeconds)));
    sliceLength_ = std::max(1, static_cast<int>(std::lround(sampleRate_ * stepSeconds / slidesPerStep)));
    const auto coeffs = dsp::kWeighting(sampleRate_);
    for (auto& channel : kWeighting_)
        for (size_t i = 0; i < channel.size(); ++i)
            channel[i].setCoeffs(coeffs[i]);
    truePeak_.prepare(2);
    // The count is in steps of song time, so it survives a change of rate: allocated once.
    if (!prepared_)
    {
        for (auto& s : stores_)
            s.prepare(storeSteps);
        prepared_ = true;
    }
    {
        const std::lock_guard lock(loadMutex_);
        if (pending_)
        {
            auto& counting = active();
            counting.clear();
            fill(counting, *pending_);
            appliedMode_ = static_cast<int>(pending_->mode);
            pending_.reset();
            readingsStale_ = true;
        }
    }
    reset();
}

void LoudnessMeter::reset() noexcept
{
    for (auto& channel : kWeighting_)
        for (auto& f : channel)
            f.reset();
    truePeak_.reset();
    live_.fill({});
    liveNow_ = {};
    livePos_ = liveWrite_ = liveDone_ = 0;
    momentary_ = shortTerm_ = balance_ = correlation_ = kNone;
    partial_ = {};
    slidesInARow_ = 0;
    nextCounted_ = -1;
    expected_ = -1;
    readingsStale_ = true;
}

void LoudnessMeter::clearCount() noexcept
{
    active().clear();
    partial_ = {};
    running_ = 0;
    correlationMin_ = kNone;
    readingsStale_ = true;
}

void LoudnessMeter::count(std::int64_t position, double energy, float peak) noexcept
{
    if (position < 0)
    {
        partial_.valid = false; // before the song's start: a count-in
        return;
    }
    const auto index = position / stepLength_;
    const auto offset = static_cast<int>(position % stepLength_);
    if (index >= storeSteps)
    {
        full_ = true;
        partial_.valid = false;
        return;
    }
    if (position != nextCounted_)
        slidesInARow_ = 0; // a jump: the windows start again
    nextCounted_ = position + 1;
    if (offset == 0)
        partial_ = {.index = static_cast<int>(index), .valid = true};
    if (!partial_.valid || partial_.index != index)
        return;
    partial_.energy += energy;
    partial_.peak = std::max(partial_.peak, peak);
    partial_.slideEnergy += energy;
    ++partial_.slideSamples;
    // Every 10 ms: the momentary and short-term windows ending here, for the maximums.
    const auto slideEnd = [this](int slide)
    { return static_cast<int>(static_cast<std::int64_t>(slide) * stepLength_ / slidesPerStep); };
    if (offset + 1 == slideEnd(partial_.slide + 1))
    {
        slides_[static_cast<size_t>(slideWrite_)] = partial_.slideEnergy / partial_.slideSamples;
        slideWrite_ = (slideWrite_ + 1) % slidesShortTerm;
        ++slidesInARow_;
        const auto loudness = [this](int length)
        {
            double sum = 0.0;
            for (int i = 1; i <= length; ++i)
                sum += slides_[static_cast<size_t>((slideWrite_ - i + slidesShortTerm) % slidesShortTerm)];
            return lufs(sum / length);
        };
        const auto raise = [](float& held, float value)
        { held = std::isnan(held) ? value : std::max(held, value); };
        if (slidesInARow_ >= slidesMomentary)
            raise(partial_.loudestMomentary, loudness(slidesMomentary));
        if (slidesInARow_ >= slidesShortTerm)
            raise(partial_.loudestShortTerm, loudness(slidesShortTerm));
        ++partial_.slide;
        partial_.slideEnergy = 0.0;
        partial_.slideSamples = 0;
    }
    // Whole steps only: one cut by a jump, a loop's wrap or a stop is dropped.
    if (++partial_.samples == stepLength_)
    {
        active().set(partial_.index, partial_.energy / stepLength_, partial_.peak, partial_.loudestMomentary,
                     partial_.loudestShortTerm);
        partial_.valid = false;
        readingsStale_ = true;
    }
}

void LoudnessMeter::process(ProcessContext& ctx) noexcept
{
    const int n = ctx.numSamples;
    if (n <= 0)
        return;
    const int inChannels = std::min(ctx.input.numChannels, 2);
    // The audio passes unchanged.
    for (int c = 0; c < ctx.output.numChannels; ++c)
        if (inChannels > 0 && ctx.output.channel(c) != ctx.input.channel(std::min(c, inChannels - 1)))
            std::copy_n(ctx.input.channel(std::min(c, inChannels - 1)), n, ctx.output.channel(c));
    if (inChannels == 0)
        return;

    // A loaded measurement takes over (when it's of the mode set now).
    if (swapReady_.load(std::memory_order_acquire))
    {
        if (static_cast<int>(loadedMode_) == static_cast<int>(mode()))
        {
            active_.store(1 - active_.load());
            appliedMode_ = static_cast<int>(loadedMode_);
            partial_ = {};
            readingsStale_ = true;
        }
        swapReady_.store(false, std::memory_order_release);
    }
    // Song and Running count different things: switching starts again.
    if (const int m = static_cast<int>(mode()); m != appliedMode_)
    {
        if (appliedMode_ >= 0)
            clearCount();
        appliedMode_ = m;
    }
    if (analysis().takeRequest(Reset))
        clearCount();
    if (analysis().takeRequest(Pause))
        paused_ = true;
    if (analysis().takeRequest(Resume))
        paused_ = false;

    const bool song = mode() == MeasureMode::Song;
    const bool counting = !paused_ && (!song || ctx.transport.playing);
    waiting_ = song && !ctx.transport.playing;
    std::int64_t position = song ? ctx.transport.samplePosition : running_;
    if (!counting || position != expected_)
        partial_.valid = false; // stopped, paused, or the song jumped: the step under way is dropped
    expected_ = counting ? position + n : -1;
    lastPosition_ = song ? ctx.transport.samplePosition : running_;
    full_ = false;

    const float* left = ctx.input.channel(0);
    const float* right = ctx.input.channel(inChannels - 1); // mono: heard on both sides
    for (int i = 0; i < n; ++i)
    {
        const float l = left[i], r = right[i];
        auto& kl = kWeighting_[0];
        auto& kr = kWeighting_[1];
        const double yl = kl[1].process(kl[0].process(l));
        const double yr = kr[1].process(kr[0].process(r));
        const double energy = yl * yl + yr * yr;
        const float peak = std::max(
            {std::abs(l), std::abs(r), truePeak_.processSample(0, l), truePeak_.processSample(1, r)});

        liveNow_.energy += energy;
        liveNow_.left += yl * yl;
        liveNow_.right += yr * yr;
        liveNow_.lr += static_cast<double>(l) * r;
        liveNow_.ll += static_cast<double>(l) * l;
        liveNow_.rr += static_cast<double>(r) * r;
        if (++livePos_ == sliceLength_)
        {
            finishLiveStep();
            if (counting && std::isfinite(correlation_))
                correlationMin_ =
                    std::isnan(correlationMin_) ? correlation_ : std::min(correlationMin_, correlation_);
        }
        if (counting)
            count(position + i, energy, peak);
    }
    if (counting && !song)
        running_ += n;

    if (readingsStale_)
        updateReadings();
    publish();
}

void LoudnessMeter::finishLiveStep() noexcept
{
    const double length = sliceLength_;
    live_[static_cast<size_t>(liveWrite_)] = {liveNow_.energy / length,
                                              liveNow_.left / length,
                                              liveNow_.right / length,
                                              liveNow_.lr,
                                              liveNow_.ll,
                                              liveNow_.rr};
    liveWrite_ = (liveWrite_ + 1) % slidesShortTerm;
    ++liveDone_;
    liveNow_ = {};
    livePos_ = 0;

    const auto last = [this](int slices)
    {
        Slice sum;
        for (int i = 1; i <= slices; ++i)
        {
            const auto& s = live_[static_cast<size_t>((liveWrite_ - i + slidesShortTerm) % slidesShortTerm)];
            sum.energy += s.energy;
            sum.left += s.left;
            sum.right += s.right;
            sum.lr += s.lr;
            sum.ll += s.ll;
            sum.rr += s.rr;
        }
        return sum;
    };
    constexpr int m = slidesMomentary, s = slidesShortTerm;
    if (liveDone_ >= m)
    {
        const auto block = last(m);
        momentary_ = lufs(block.energy / m);
        // Correlation over 400 ms, while both sides are above -70 dBFS.
        const double samples = static_cast<double>(m) * sliceLength_;
        correlation_ =
            block.ll / samples > kSilence && block.rr / samples > kSilence
                ? static_cast<float>(std::clamp(block.lr / std::sqrt(block.ll * block.rr), -1.0, 1.0))
                : kNone;
    }
    if (liveDone_ >= s)
    {
        const auto window = last(s);
        shortTerm_ = lufs(window.energy / s);
        const float l = lufs(window.left / s), r = lufs(window.right / s);
        balance_ = l > -70.0f && r > -70.0f ? l - r : kNone;
    }
}

void LoudnessMeter::updateReadings() noexcept
{
    const auto& s = active();
    integrated_ = s.integrated();
    range_ = s.range();
    maxMomentary_ = s.maxMomentary();
    maxShortTerm_ = s.maxShortTerm();
    const float peak = s.truePeakMax();
    peakMaxDb_ = peak > 0.0f ? dsp::gainToDb(peak) : kNone;
    readingsStale_ = false;
}

void LoudnessMeter::publish() noexcept
{
    auto& bus = analysis();
    const auto& p = params();
    bus.publish(Momentary, momentary_);
    bus.publish(ShortTerm, shortTerm_);
    bus.publish(Integrated, integrated_);
    bus.publish(Range, range_);
    bus.publish(MaxMomentary, maxMomentary_);
    bus.publish(MaxShortTerm, maxShortTerm_);
    bus.publish(TruePeakMax, peakMaxDb_);
    bus.publish(PeakToLoudness, std::isfinite(integrated_) ? peakMaxDb_ - integrated_ : kNone);
    bus.publish(Normalised, normalisedGain(integrated_, peakMaxDb_, p.get(Target), p.get(Ceiling)));
    bus.publish(Correlation, correlation_);
    bus.publish(CorrelationMin, correlationMin_);
    bus.publish(Balance, balance_);
    bus.publish(Counted, static_cast<float>(active().heardCount() * stepSeconds));
    const auto state = paused_    ? CountingState::Paused
                       : full_    ? CountingState::Full
                       : waiting_ ? CountingState::WaitingForSong
                                  : CountingState::Counting;
    bus.publish(Counting, static_cast<float>(state));
    bus.publish(Position, lastPosition_ >= 0 ? static_cast<float>(lastPosition_ / stepLength_) : -1.0f);
}

// ---- kept with the project ----------------------------------------------------------------------

void LoudnessMeter::writeExtraState(nlohmann::json& extra) const
{
    // Runs of heard steps: [first step, loudness, true peak, loudest momentary, loudest short-term],
    // each a list of hundredths (of a LUFS, a dBTP), null for none; about 60 KB for 5 minutes.
    const auto& s = store();
    if (s.heardCount() == 0)
        return;
    const auto hundredths = [](float v)
    { return std::isfinite(v) ? nlohmann::json(std::lround(v * 100.0f)) : nlohmann::json(nullptr); };
    auto runs = nlohmann::json::array();
    nlohmann::json run;
    int runEnd = -1;
    for (int i = 0; i < s.extent(); ++i)
    {
        const float ms = s.meanSquareAt(i);
        if (ms < 0.0f)
            continue;
        if (i != runEnd)
        {
            if (!run.is_null())
                runs.push_back(std::move(run));
            run = nlohmann::json::array({i, nlohmann::json::array(), nlohmann::json::array(),
                                         nlohmann::json::array(), nlohmann::json::array()});
        }
        const float tp = s.truePeakAt(i);
        run[1].push_back(hundredths(ms > 0.0f ? lufs(ms) : kNone));
        run[2].push_back(hundredths(tp > 0.0f ? dsp::gainToDb(tp) : kNone));
        run[3].push_back(hundredths(s.loudestMomentaryAt(i)));
        run[4].push_back(hundredths(s.loudestShortTermAt(i)));
        runEnd = i + 1;
    }
    if (!run.is_null())
        runs.push_back(std::move(run));
    extra[std::string(measurementKey)] = {
        {"mode", countedMode() == MeasureMode::Running ? "running" : "song"},
        {"step", stepSeconds},
        {"runs", runs}};
}

void LoudnessMeter::readExtraState(const nlohmann::json& extra)
{
    const auto key = std::string(measurementKey);
    if (!extra.contains(key))
        return; // a state without one leaves what's counted alone
    Loaded loaded;
    if (extra[key].is_object() && extra[key].value("step", 0.0) == stepSeconds)
    {
        const auto& m = extra[key];
        loaded.mode =
            m.value("mode", std::string("song")) == "running" ? MeasureMode::Running : MeasureMode::Song;
        const auto value = [](const nlohmann::json& v)
        { return v.is_number() ? static_cast<float>(v.get<double>() / 100.0) : kNone; };
        if (m.contains("runs") && m["runs"].is_array())
            for (const auto& run : m["runs"])
            {
                if (!run.is_array() || run.size() != 5 || !run[0].is_number_integer())
                    continue;
                const auto n = run[1].size();
                bool whole = true;
                for (size_t k = 1; k < 5; ++k)
                    whole = whole && run[k].is_array() && run[k].size() == n;
                if (!whole)
                    continue;
                const int start = run[0].get<int>();
                for (size_t k = 0; k < n; ++k)
                {
                    const float loudness = value(run[1][k]), peak = value(run[2][k]);
                    loaded.steps.push_back({.index = start + static_cast<int>(k),
                                            .meanSquare = std::isfinite(loudness)
                                                              ? std::pow(10.0, (loudness + 0.691) / 10.0)
                                                              : 0.0,
                                            .truePeak = std::isfinite(peak) ? dsp::dbToGain(peak) : 0.0f,
                                            .loudestMomentary = value(run[3][k]),
                                            .loudestShortTerm = value(run[4][k])});
                }
            }
    }
    const std::lock_guard lock(loadMutex_);
    if (!prepared_)
    {
        pending_ = std::move(loaded); // counted when it's prepared
        return;
    }
    // Into the store the audio thread isn't counting into; it takes over at its next block.
    if (swapReady_.load(std::memory_order_acquire))
        return; // one is waiting already
    auto& spare = stores_[static_cast<size_t>(1 - active_.load())];
    spare.clear();
    fill(spare, loaded);
    loadedMode_ = loaded.mode;
    swapReady_.store(true, std::memory_order_release);
}

void LoudnessMeter::fill(dsp::LoudnessStore& store, const Loaded& loaded)
{
    for (const auto& s : loaded.steps)
        store.set(s.index, s.meanSquare, s.truePeak, s.loudestMomentary, s.loudestShortTerm);
}

const DeviceFactory& loudnessMeterFactory()
{
    static const DeviceFactory factory{
        .info = []() -> const DeviceInfo& { return kInfo; },
        .create = []() -> std::unique_ptr<Device> { return std::make_unique<LoudnessMeter>(); },
    };
    return factory;
}
} // namespace fd::devices
