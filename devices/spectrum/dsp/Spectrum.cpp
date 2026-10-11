#include "Spectrum.h"

#include "fd/audiofile/AudioFile.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <nlohmann/json.hpp>
#include <vector>

namespace fd::devices
{
namespace
{
constexpr auto kChoice = ParamKind::Choice;
constexpr std::string_view kChannels[] = {"L+R", "Left", "Right", "Mid", "Side"};
constexpr std::string_view kResolutions[] = {"2048", "4096", "8192", "16384"};
constexpr std::string_view kHolds[] = {"Off", "1 s", "3 s", "Infinite"};
constexpr std::string_view kRanges[] = {"60 dB", "90 dB", "120 dB"};
constexpr std::string_view kLabels[] = {"Off", "3", "6"};
constexpr std::string_view kSlopes[] = {"0 dB/oct", "1.5 dB/oct", "3 dB/oct", "4.5 dB/oct", "6 dB/oct"};
constexpr std::string_view kViews[] = {"Spectrum", "Spectrogram"};

// Display settings: never automatable (SPEC). View arrives in stage 2: hidden until then.
constexpr std::array<ParamSpec, Spectrum::NumParams> kSpecs{{
    {.id = "channel",
     .name = "Channel",
     .shortName = "Channel",
     .kind = kChoice,
     .min = 0.0f,
     .max = 4.0f,
     .def = 0.0f,
     .choices = kChannels,
     .flags = ParamFlags::None},
    {.id = "resolution",
     .name = "Resolution",
     .shortName = "Resolution",
     .kind = kChoice,
     .min = 0.0f,
     .max = 3.0f,
     .def = 2.0f,
     .choices = kResolutions,
     .flags = ParamFlags::None},
    {.id = "average",
     .name = "Averaging",
     .shortName = "Average",
     .min = 0.0f,
     .max = 5000.0f,
     .def = 300.0f,
     .skewCentre = 300.0f,
     .unit = Unit::Milliseconds,
     .flags = ParamFlags::None,
     .decimals = 0},
    {.id = "hold",
     .name = "Peak Hold",
     .shortName = "Hold",
     .kind = kChoice,
     .min = 0.0f,
     .max = 3.0f,
     .def = 2.0f,
     .choices = kHolds,
     .flags = ParamFlags::None},
    {.id = "slope",
     .name = "Slope",
     .shortName = "Slope",
     .kind = kChoice,
     .min = 0.0f,
     .max = 4.0f,
     .def = 2.0f,
     .choices = kSlopes,
     .flags = ParamFlags::None},
    {.id = "range",
     .name = "Range",
     .shortName = "Range",
     .kind = kChoice,
     .min = 0.0f,
     .max = 2.0f,
     .def = 1.0f,
     .choices = kRanges,
     .flags = ParamFlags::None},
    {.id = "side",
     .name = "Side Overlay",
     .shortName = "Side Overlay",
     .kind = ParamKind::Toggle,
     .min = 0.0f,
     .max = 1.0f,
     .flags = ParamFlags::None},
    {.id = "labels",
     .name = "Peak Labels",
     .shortName = "Labels",
     .kind = kChoice,
     .min = 0.0f,
     .max = 2.0f,
     .def = 1.0f,
     .choices = kLabels,
     .flags = ParamFlags::None},
    {.id = "view",
     .name = "View",
     .shortName = "View",
     .kind = kChoice,
     .min = 0.0f,
     .max = 1.0f,
     .def = 0.0f,
     .choices = kViews,
     .flags = ParamFlags::Hidden},
}};
// What each parameter does, in one sentence: the same as in SPEC.md (open question B22).
constexpr ParamDescription kDescriptions[] = {
    {"channel", "Which signal is shown: both channels, left, right, mid or side."},
    {"resolution",
     "How finely frequencies are resolved: higher shows more detail in the lows and reacts more slowly."},
    {"average", "How long the display averages, from instant to smooth."},
    {"hold", "How long peaks stay marked."},
    {"slope", "Tilts the display around 1 kHz, so a balanced mix looks flat."},
    {"range", "How many decibels the display spans from top to bottom."},
    {"side", "Draws the side signal under the main curve, to show what's stereo."},
    {"labels", "Marks the strongest peaks with their frequency and note."},
    {"view", "Shows the spectrum now, or a spectrogram of how it changed over time (stage 2)."},
};
constexpr auto kParams = describe(kSpecs, kDescriptions);
static_assert(allDescribed(kParams), "every parameter needs a description (SPEC.md)");

// The snapshot is a picture of this mix at one moment: presets leave it out (ADR 0044). The
// reference stays, so a preset can hold a target curve.
constexpr std::array<std::string_view, 2> kSessionKeys{"snapshot.curve", "snapshot.channel"};

constexpr DeviceInfo kInfo{
    .id = "spectrum",
    .name = "Spectrum",
    .category = "Utility",
    .kind = DeviceKind::AudioEffect,
    .buses = {.inputChannels = 2, .outputChannels = 2, .sidechainChannels = 2},
    .params = kParams,
    .stateVersion = 1,
    .sessionExtraKeys = kSessionKeys,
};

/// A curve as stored: dB rounded to hundredths, so projects stay small and readable.
nlohmann::json curveJson(const dsp::SpectrumCurve& curve)
{
    auto out = nlohmann::json::array();
    for (const float v : curve)
        out.push_back(std::round(v * 100.0f) / 100.0f);
    return out;
}

std::optional<dsp::SpectrumCurve> readCurve(const nlohmann::json& j)
{
    if (!j.is_array() || j.size() != dsp::spectrum::numBands)
        return std::nullopt;
    dsp::SpectrumCurve out{};
    for (size_t i = 0; i < out.size(); ++i)
    {
        if (!j[i].is_number())
            return std::nullopt;
        out[i] = std::max(dsp::spectrum::floorDb, j[i].get<float>());
    }
    return out;
}

std::string stringAt(const nlohmann::json& j, const char* key)
{
    return j.contains(key) && j[key].is_string() ? j[key].get<std::string>() : std::string();
}
} // namespace

const DeviceInfo& Spectrum::staticInfo() noexcept
{
    return kInfo;
}

Spectrum::Spectrum() : Device(kInfo) {}

void Spectrum::prepare(const ProcessSpec& spec)
{
    analysis().setSampleRate(spec.sampleRate);
    // Half a second of audio at least: the panel takes them every frame.
    const auto capacity = std::max<size_t>(32768, static_cast<size_t>(spec.sampleRate * 0.5));
    for (int f = 0; f < AnalysisBus::numFifos; ++f)
        analysis().fifo(f).prepare(capacity);
}

void Spectrum::process(ProcessContext& ctx) noexcept
{
    const int n = ctx.numSamples;
    if (n <= 0)
        return;
    const auto count = static_cast<size_t>(n);
    const float* inL = ctx.input.numChannels > 0 ? ctx.input.channel(0) : nullptr;
    const float* inR = ctx.input.numChannels > 1 ? ctx.input.channel(1) : inL;

    // The taps first: the output may be the input's buffers.
    if (inL != nullptr)
    {
        analysis().fifo(InputLeft).push(inL, count);
        analysis().fifo(InputRight).push(inR, count);
    }
    if (ctx.sidechain.numChannels > 0)
    {
        const float* scL = ctx.sidechain.channel(0);
        const float* scR = ctx.sidechain.numChannels > 1 ? ctx.sidechain.channel(1) : scL;
        analysis().fifo(CompareLeft).push(scL, count);
        analysis().fifo(CompareRight).push(scR, count);
    }

    // The audio passes unchanged.
    for (int c = 0; c < ctx.output.numChannels; ++c)
    {
        float* out = ctx.output.channel(c);
        const float* in = c == 0 ? inL : inR;
        if (in == nullptr)
            std::fill(out, out + n, 0.0f);
        else if (in != out)
            std::memcpy(out, in, count * sizeof(float));
    }
}

void Spectrum::writeExtraState(nlohmann::json& extra) const
{
    const std::lock_guard lock(stateMutex_);
    if (snapshot_)
    {
        extra["snapshot.curve"] = curveJson(snapshot_->curve);
        extra["snapshot.channel"] = snapshot_->channel;
    }
    if (reference_)
    {
        extra["reference.name"] = reference_->name;
        extra["reference.path"] = reference_->path;
        auto curves = nlohmann::json::array();
        for (const auto& c : reference_->curves)
            curves.push_back(curveJson(c));
        extra["reference.curves"] = std::move(curves);
    }
}

void Spectrum::readExtraState(const nlohmann::json& extra)
{
    std::optional<Snapshot> snapshot;
    if (extra.contains("snapshot.curve"))
        if (const auto curve = readCurve(extra["snapshot.curve"]))
        {
            const auto channel = extra.value("snapshot.channel", 0);
            snapshot = Snapshot{*curve, std::clamp(channel, 0, dsp::numSpectrumChannels - 1)};
        }
    std::optional<Reference> reference;
    if (extra.contains("reference.curves") && extra["reference.curves"].is_array() &&
        extra["reference.curves"].size() == dsp::numSpectrumChannels)
    {
        Reference r{stringAt(extra, "reference.name"), stringAt(extra, "reference.path"), {}};
        bool whole = true;
        for (size_t i = 0; i < r.curves.size() && whole; ++i)
            if (const auto curve = readCurve(extra["reference.curves"][i]))
                r.curves[i] = *curve;
            else
                whole = false;
        if (whole)
            reference = std::move(r);
    }
    const std::lock_guard lock(stateMutex_);
    snapshot_ = std::move(snapshot);
    reference_ = std::move(reference);
}

int Spectrum::fftOrder(float resolution) noexcept
{
    return 11 + std::clamp(static_cast<int>(std::lround(resolution)), 0, 3);
}

std::optional<double> Spectrum::holdSeconds(float hold) noexcept
{
    switch (std::clamp(static_cast<int>(std::lround(hold)), 0, 3))
    {
    case 0:
        return std::nullopt;
    case 1:
        return 1.0;
    case 2:
        return 3.0;
    default:
        return -1.0; // for good
    }
}

float Spectrum::rangeDb(float range) noexcept
{
    constexpr float ranges[] = {60.0f, 90.0f, 120.0f};
    return ranges[std::clamp(static_cast<int>(std::lround(range)), 0, 2)];
}

float Spectrum::slopeDb(float slope) noexcept
{
    return 1.5f * static_cast<float>(std::clamp(static_cast<int>(std::lround(slope)), 0, 4));
}

int Spectrum::labelCount(float labels) noexcept
{
    constexpr int counts[] = {0, 3, 6};
    return counts[std::clamp(static_cast<int>(std::lround(labels)), 0, 2)];
}

std::optional<Spectrum::Snapshot> Spectrum::snapshot() const
{
    const std::lock_guard lock(stateMutex_);
    return snapshot_;
}

std::optional<Spectrum::Reference> Spectrum::reference() const
{
    const std::lock_guard lock(stateMutex_);
    return reference_;
}

nlohmann::json Spectrum::snapshotPatch(const std::optional<Snapshot>& snapshot)
{
    if (!snapshot)
        return {{"snapshot.curve", nullptr}, {"snapshot.channel", nullptr}};
    return {{"snapshot.curve", curveJson(snapshot->curve)}, {"snapshot.channel", snapshot->channel}};
}

nlohmann::json Spectrum::referencePatch(const std::optional<Reference>& reference)
{
    if (!reference)
        return {{"reference.name", nullptr}, {"reference.path", nullptr}, {"reference.curves", nullptr}};
    auto curves = nlohmann::json::array();
    for (const auto& c : reference->curves)
        curves.push_back(curveJson(c));
    return {{"reference.name", reference->name},
            {"reference.path", reference->path},
            {"reference.curves", curves}};
}

std::optional<std::array<dsp::SpectrumCurve, dsp::numSpectrumChannels>>
analyseFile(const std::string& path, std::string* error, const std::atomic<bool>* cancel)
{
    using dsp::SpectrumChannel;
    // Both is the average of the left and right powers: made from those two below.
    constexpr SpectrumChannel analysed[] = {SpectrumChannel::Left, SpectrumChannel::Right,
                                            SpectrumChannel::Mid, SpectrumChannel::Side};
    std::array<dsp::SpectrumAnalyser, 4> analysers;
    std::vector<float> left, right;
    bool stopped = false;
    const auto info = audiofile::stream(
        path, 8192,
        [&](const audiofile::StreamInfo& format, const float* interleaved, int frames)
        {
            if (cancel != nullptr && cancel->load(std::memory_order_relaxed))
            {
                stopped = true;
                return false;
            }
            if (analysers[0].order() == 0)
                for (size_t i = 0; i < analysers.size(); ++i)
                {
                    analysers[i].prepare(13, format.sampleRate);
                    analysers[i].setChannel(analysed[i]);
                    analysers[i].setCumulative(true);
                }
            const int channels = std::max(1, format.numChannels);
            left.resize(static_cast<size_t>(frames));
            right.resize(static_cast<size_t>(frames));
            for (int f = 0; f < frames; ++f)
            {
                left[static_cast<size_t>(f)] = interleaved[f * channels];
                right[static_cast<size_t>(f)] = interleaved[f * channels + (channels > 1 ? 1 : 0)];
            }
            for (auto& a : analysers)
                a.push(left.data(), right.data(), static_cast<size_t>(frames));
            return true;
        },
        error);
    if (stopped || !info)
        return std::nullopt;
    if (!analysers[0].hasData())
    {
        if (error != nullptr)
            *error = "too short to analyse";
        return std::nullopt;
    }
    std::array<dsp::SpectrumCurve, dsp::numSpectrumChannels> out{};
    out[static_cast<size_t>(SpectrumChannel::Left)] = analysers[0].bands();
    out[static_cast<size_t>(SpectrumChannel::Right)] = analysers[1].bands();
    out[static_cast<size_t>(SpectrumChannel::Mid)] = analysers[2].bands();
    out[static_cast<size_t>(SpectrumChannel::Side)] = analysers[3].bands();
    auto& both = out[static_cast<size_t>(SpectrumChannel::Both)];
    for (size_t b = 0; b < both.size(); ++b)
    {
        const double power = 0.5 * (std::pow(10.0, out[1][b] / 10.0) + std::pow(10.0, out[2][b] / 10.0));
        both[b] = power > 1e-20
                      ? std::max(dsp::spectrum::floorDb, static_cast<float>(10.0 * std::log10(power)))
                      : dsp::spectrum::floorDb;
    }
    return out;
}

std::string frequencyText(double hz)
{
    constexpr ParamSpec spec{.id = "hz", .min = 0.0f, .max = 100000.0f, .unit = Unit::Hertz, .decimals = 0};
    return spec.toText(static_cast<float>(hz));
}

std::string noteText(double hz)
{
    constexpr ParamSpec spec{.id = "note", .min = 0.0f, .max = 127.0f, .unit = Unit::Note, .decimals = 0};
    const double midi = 69.0 + 12.0 * std::log2(std::max(hz, 1.0) / 440.0);
    return spec.toText(static_cast<float>(std::round(midi)));
}

std::string peakLabel(double hz)
{
    return frequencyText(hz) + " \xC2\xB7 " + noteText(hz); // a middle dot (UTF-8)
}

const DeviceFactory& spectrumFactory()
{
    static const DeviceFactory factory{
        .info = &Spectrum::staticInfo,
        .create = []() -> std::unique_ptr<Device> { return std::make_unique<Spectrum>(); },
    };
    return factory;
}
} // namespace fd::devices
