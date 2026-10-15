#include "Tuner.h"

#include "fd/dsp/Math.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace fd::devices
{
namespace
{
using T = Tuner;

constexpr float kNone = std::numeric_limits<float>::quiet_NaN();

constexpr std::string_view kViews[] = {"Needle", "Strobe"};
constexpr std::string_view kNotes[] = {"All", "In key"};

constexpr auto kDisplay = ParamFlags::None; // display settings: never automated

constexpr std::array<ParamSpec, T::NumParams> kSpecs{{
    {.id = "reference",
     .name = "Reference",
     .shortName = "Reference",
     .min = 410.0f,
     .max = 480.0f,
     .def = 440.0f,
     .unit = Unit::Hertz,
     .smoothing = {SmoothingKind::None, 0.0f},
     .flags = kDisplay,
     .decimals = 1},
    {.id = "view",
     .name = "View",
     .shortName = "View",
     .kind = ParamKind::Choice,
     .min = 0.0f,
     .max = 1.0f,
     .def = 0.0f,
     .choices = kViews,
     .smoothing = {SmoothingKind::None, 0.0f},
     .flags = kDisplay},
    {.id = "notes",
     .name = "Notes",
     .shortName = "Notes",
     .kind = ParamKind::Choice,
     .min = 0.0f,
     .max = 1.0f,
     .def = 0.0f,
     .choices = kNotes,
     .smoothing = {SmoothingKind::None, 0.0f},
     .flags = kDisplay},
    {.id = "mute",
     .name = "Mute",
     .shortName = "Mute",
     .kind = ParamKind::Toggle,
     .min = 0.0f,
     .max = 1.0f,
     .def = 0.0f,
     .smoothing = {SmoothingKind::Linear, 10.0f}},
}};

// What each parameter does, in one sentence: the same as in SPEC.md (open question B22).
constexpr ParamDescription kDescriptions[] = {
    {"reference", "The pitch of the A above middle C, which every note is measured against."},
    {"view", "Needle shows how far off the note is on a scale; Strobe turns faster the further off it is."},
    {"notes", "Which notes it measures against: every note, or only the song's key's."},
    {"mute", "Silences what comes out, so you can tune without being heard."},
};
constexpr auto kParams = describe(kSpecs, kDescriptions);
static_assert(allDescribed(kParams), "every parameter needs a description (SPEC.md)");

// Its readings, by name (MCP measure_module).
constexpr MeterSpec kMeters[] = {
    {.slot = T::Frequency, .id = "frequency", .name = "Frequency", .kind = MeterKind::Hertz},
    {.slot = T::Note, .id = "note", .name = "Note", .kind = MeterKind::Pitch},
    {.slot = T::Cents, .id = "cents", .name = "Cents off the note", .kind = MeterKind::Cents},
    {.slot = T::Clarity, .id = "clarity", .name = "Clarity (how periodic)", .kind = MeterKind::Ratio},
    {.slot = T::Level, .id = "level", .name = "Level (dBFS)", .kind = MeterKind::Decibels},
};

constexpr DeviceInfo kInfo{
    .id = "tuner",
    .name = "Tuner",
    .category = "Utility",
    .kind = DeviceKind::AudioEffect,
    .buses = {.inputChannels = 2, .outputChannels = 2},
    .params = kParams,
    .stateVersion = 1,
    .meters = kMeters,
};

/// The middle of three.
float median(float a, float b, float c) noexcept
{
    return std::max(std::min(a, b), std::min(std::max(a, b), c));
}
} // namespace

Tuner::Tuner() : Device(kInfo)
{
    for (auto& h : history_)
        h.store(kNone, std::memory_order_relaxed);
    publishNone();
    analysis().publish(Level, -120.0f);
    analysis().publish(StrobePhase, 0.0f);
    analysis().publish(KeyRoot, -1.0f);
    analysis().publish(KeyNotes, 0.0f);
    analysis().publish(Since, 1e9f);
}

void Tuner::prepare(const ProcessSpec& spec)
{
    sampleRate_ = spec.sampleRate;
    fadeSamples_ = dsp::msToSamples(10.0, sampleRate_);
    detector_.prepare(sampleRate_);
    readingSeconds_.store(detector_.hopSeconds(), std::memory_order_relaxed);
    mono_.assign(static_cast<size_t>(std::max(1, spec.maxBlockSize)), 0.0f);
    reset();
}

void Tuner::reset() noexcept
{
    detector_.reset();
    gain_.reset(params().getBool(Mute) ? 0.0f : 1.0f);
    held_.reset();
    recentCount_ = 0;
    rawCents_ = kNone;
    since_ = 1e9;
    publishNone();
}

void Tuner::publishNone() noexcept
{
    auto& bus = analysis();
    for (const int s : {Frequency, Note, Cents, RawCents, Clarity})
        bus.publish(s, kNone);
}

int Tuner::nearestNote(double pitch, const KeyInfo* key) noexcept
{
    const int nearest = static_cast<int>(std::lround(pitch));
    if (key == nullptr || !key->known() || key->notes == 0)
        return nearest;
    // The key's nearest note below (or at) the pitch and above it; the lower one at a tie.
    int below = static_cast<int>(std::floor(pitch)), above = static_cast<int>(std::ceil(pitch));
    while (!key->contains(below))
        --below;
    while (!key->contains(above))
        ++above;
    return pitch - below <= above - pitch ? below : above;
}

void Tuner::onReading(const dsp::PitchDetector::Reading& reading) noexcept
{
    auto& bus = analysis();
    bus.publish(Level, reading.levelDb);
    const auto index = written_.load(std::memory_order_relaxed);
    if (!reading.valid)
    {
        history_[index % historySize].store(kNone, std::memory_order_relaxed);
        written_.store(index + 1, std::memory_order_release);
        publishNone();
        held_.reset();
        rawCents_ = kNone;
        return;
    }
    const double reference = params().get(Reference);
    const double pitch = 69.0 + 12.0 * std::log2(reading.hz / reference);
    history_[index % historySize].store(static_cast<float>(pitch), std::memory_order_relaxed);
    written_.store(index + 1, std::memory_order_release);
    since_ = 0.0;

    // The note: it changes only when another is nearer by more than 10 cents (past ±55 cents
    // between neighbours), so a note held on the boundary, vibrato and all, doesn't flicker.
    const KeyInfo* key = key_.known() ? &key_ : nullptr;
    const int candidate = nearestNote(pitch, key);
    if (!held_)
        held_ = candidate;
    else if (candidate != *held_ && std::abs(pitch - *held_) - std::abs(pitch - candidate) > 0.10)
    {
        held_ = candidate;
        recentCount_ = 0;
    }

    rawCents_ = static_cast<float>((pitch - *held_) * 100.0);
    recent_[static_cast<size_t>(recentCount_ % 3)] = rawCents_;
    ++recentCount_;
    const float steady = recentCount_ >= 3 ? median(recent_[0], recent_[1], recent_[2]) : rawCents_;
    bus.publish(Frequency, static_cast<float>(reading.hz));
    bus.publish(Note, static_cast<float>(*held_));
    bus.publish(Cents, steady);
    bus.publish(RawCents, rawCents_);
    bus.publish(Clarity, reading.clarity);
}

float Tuner::historyAt(std::uint32_t ago) const noexcept
{
    const auto written = written_.load(std::memory_order_acquire);
    if (ago >= written || ago >= static_cast<std::uint32_t>(historySize) - 1)
        return kNone;
    return history_[(written - 1 - ago) % historySize].load(std::memory_order_relaxed);
}

void Tuner::process(ProcessContext& ctx) noexcept
{
    const int n = ctx.numSamples;
    if (n <= 0 || ctx.output.numChannels == 0)
        return;
    auto& p = params();
    // The key, while In key and the song has one (TU5).
    key_ = p.get(Notes) >= 0.5f ? ctx.transport.key : KeyInfo{};
    auto& bus = analysis();
    bus.publish(KeyRoot, static_cast<float>(key_.known() ? key_.root : -1));
    bus.publish(KeyNotes, static_cast<float>(key_.known() ? key_.notes : 0));

    const int inputs = ctx.input.numChannels;
    const float* inL = inputs > 0 ? ctx.input.channel(0) : nullptr;
    const float* inR = inputs > 1 ? ctx.input.channel(1) : inL;
    const int count = std::min(n, static_cast<int>(mono_.size()));
    for (int i = 0; i < count; ++i)
        mono_[static_cast<size_t>(i)] =
            inL != nullptr ? 0.5f * (inL[i] + (inR != nullptr ? inR[i] : inL[i])) : 0.0f;

    // Through, unchanged, unless muted (a 10 ms fade either way).
    gain_.setTarget(p.getBool(Mute) ? 0.0f : 1.0f, fadeSamples_);
    for (int c = 0; c < ctx.output.numChannels; ++c)
    {
        const float* in = c < inputs ? ctx.input.channel(c) : inL;
        float* out = ctx.output.channel(c);
        auto g = gain_;
        for (int i = 0; i < n; ++i)
            out[i] = (in != nullptr ? in[i] : 0.0f) * g.next();
    }
    for (int i = 0; i < n; ++i)
        (void)gain_.next();

    detector_.push(mono_.data(), count, [this](const dsp::PitchDetector::Reading& r) { onReading(r); });

    const double seconds = n / sampleRate_;
    since_ += held_ ? 0.0 : seconds;
    if (std::isfinite(rawCents_))
    {
        // The strobe turns with how far off the note is: still when in tune.
        strobe_ += rawCents_ / 100.0 * strobeTurnsPerSemitone * seconds;
        strobe_ -= std::floor(strobe_);
    }
    bus.publish(StrobePhase, static_cast<float>(strobe_));
    bus.publish(Since, static_cast<float>(since_));
}

const DeviceFactory& tunerFactory()
{
    static const DeviceFactory factory{
        .info = []() -> const DeviceInfo& { return kInfo; },
        .create = []() -> std::unique_ptr<Device> { return std::make_unique<Tuner>(); },
    };
    return factory;
}
} // namespace fd::devices
