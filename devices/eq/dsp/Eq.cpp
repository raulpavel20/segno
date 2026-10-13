#include "Eq.h"

#include "fd/dsp/Loudness.h"
#include "fd/dsp/Math.h"

#include <algorithm>
#include <cmath>
#include <string>

namespace fd::devices
{
namespace
{
constexpr std::string_view kTypes[] = {"Bell", "Low Shelf", "High Shelf", "Low Cut",
                                       "High Cut", "Notch", "Band Pass", "Tilt"};
constexpr std::string_view kSlopes[] = {"6 dB", "12 dB", "18 dB", "24 dB", "36 dB", "48 dB", "72 dB", "96 dB"};
constexpr int kSlopeDb[] = {6, 12, 18, 24, 36, 48, 72, 96};
constexpr std::string_view kPlaces[] = {"Stereo", "Left", "Right", "Mid", "Side"};
constexpr std::string_view kAnalyzer[] = {"Off", "Pre", "Post", "Pre+Post"};
constexpr std::string_view kTriggers[] = {"Input", "Sidechain"};
constexpr std::string_view kPhases[] = {"Zero latency", "Linear phase"};
constexpr std::string_view kResolutions[] = {"Low", "Medium", "High"};

// What Auto Gain applies, by name (MCP measure_module).
constexpr MeterSpec kMeters[] = {
    {.slot = Eq::AutoGainDb,
     .id = "auto_gain",
     .name = "Auto Gain's correction",
     .kind = MeterKind::Decibels},
};

/// The parameter table is generated once (IDs like "band3.freq" need stable storage).
struct Tables
{
    std::array<std::string, Eq::NumParams> ids, names, shortNames;
    std::array<std::string_view, Eq::NumParams> descriptions; // as in SPEC.md (open question B22)
    std::array<ParamSpec, Eq::NumParams> specs{};
    std::array<std::string, Eq::numBands + 1> listenNames; // "Off", "Band 1" ... "Band 24"
    std::array<std::string_view, Eq::numBands + 1> listenChoices{};
    DeviceInfo info{};

    Tables()
    {
        // Defaults: bands 1-4 on and flat (transparent); the rest off, spread across the range.
        constexpr Eq::BandType defaultType[4] = {Eq::BandType::LowShelf, Eq::BandType::Bell, Eq::BandType::Bell,
                                                 Eq::BandType::HighShelf};
        constexpr float defaultFreq[4] = {100.0f, 400.0f, 2500.0f, 8000.0f};
        for (int b = 0; b < Eq::numBands; ++b)
        {
            const std::string prefix = "band" + std::to_string(b + 1) + ".";
            const std::string label = "Band " + std::to_string(b + 1) + " ";
            auto set = [&](Eq::BandParam p, const char* id, const char* name, const char* shortName,
                           std::string_view description) {
                const auto i = Eq::paramIndex(b, p);
                ids[i] = prefix + id;
                names[i] = label + name;
                shortNames[i] = shortName;
                descriptions[i] = description;
                return i;
            };
            const bool initial = b < 4;
            const float freq = initial ? defaultFreq[b]
                                       : static_cast<float>(30.0 * std::pow(16000.0 / 30.0, (b - 4) / 19.0));

            auto i = set(Eq::On, "on", "On", "On",
                    "Turns the band on or off.");
            specs[i] = {.kind = ParamKind::Toggle, .min = 0, .max = 1, .def = initial ? 1.0f : 0.0f};
            i = set(Eq::Type, "type", "Type", "Type",
                    "The band's filter shape: bell, shelf, cut, notch, band pass or tilt.");
            specs[i] = {.kind = ParamKind::Choice, .min = 0, .max = 7,
                        .def = static_cast<float>(initial ? static_cast<int>(defaultType[b]) : 0), .choices = kTypes};
            i = set(Eq::Freq, "freq", "Freq", "Freq",
                    "The band's centre or corner frequency.");
            specs[i] = {.min = 10.0f, .max = 30000.0f, .def = freq, .skewCentre = 632.0f, .unit = Unit::Hertz,
                        .smoothing = {SmoothingKind::Multiplicative, 20.0f}, .decimals = 0};
            i = set(Eq::Gain, "gain", "Gain", "Gain",
                    "How much the band boosts or cuts (bells, shelves and tilt).");
            specs[i] = {.min = -30.0f, .max = 30.0f, .def = 0.0f, .unit = Unit::Decibels};
            i = set(Eq::Q, "q", "Q", "Q",
                    "How narrow the band is: a higher Q affects a narrower range.");
            specs[i] = {.min = 0.1f, .max = 40.0f, .def = 0.71f, .skewCentre = 2.0f,
                        .smoothing = {SmoothingKind::Multiplicative, 20.0f}, .decimals = 2};
            i = set(Eq::Slope, "slope", "Slope", "Slope",
                    "How steeply the band cuts, in dB per octave (cut filters).");
            specs[i] = {.kind = ParamKind::Choice, .min = 0, .max = 7, .def = 1.0f, .choices = kSlopes};
            i = set(Eq::Place, "place", "Placement", "Place",
                    "Which part of the signal the band changes: both channels, left, right, mid or side.");
            specs[i] = {.kind = ParamKind::Choice, .min = 0, .max = 4, .def = 0.0f, .choices = kPlaces};
        }
        // Stage 2 (state version 2): each band's dynamics. Defaults leave the sound as it was.
        for (int b = 0; b < Eq::numBands; ++b)
        {
            const std::string prefix = "band" + std::to_string(b + 1) + ".";
            const std::string label = "Band " + std::to_string(b + 1) + " ";
            auto set = [&](Eq::DynParam p, const char* id, const char* name, const char* shortName,
                           std::string_view description, ParamSpec spec)
            {
                const auto i = Eq::dynIndex(b, p);
                ids[i] = prefix + id;
                names[i] = label + name;
                shortNames[i] = shortName;
                descriptions[i] = description;
                spec.sinceVersion = 2;
                specs[i] = spec;
            };
            set(Eq::Dyn, "dyn", "Dynamics", "Dynamics",
                "Lets the band move with the sound: it turns further up or down as its range gets louder.",
                {.kind = ParamKind::Toggle, .min = 0, .max = 1, .def = 0.0f});
            set(Eq::Range, "range", "Range", "Range",
                "How far the band moves when its range is loud: below zero it cuts, above zero it boosts.",
                {.min = -30.0f, .max = 30.0f, .def = -6.0f, .unit = Unit::Decibels});
            set(Eq::Threshold, "threshold", "Threshold", "Thresh",
                "How loud the band's range must get before it moves.",
                {.min = -60.0f, .max = 0.0f, .def = -24.0f, .unit = Unit::Decibels});
            set(Eq::AutoThreshold, "auto", "Auto Threshold", "Auto",
                "Sets the threshold from the sound itself, so the band moves on what stands out from its "
                "usual level.",
                {.kind = ParamKind::Toggle, .min = 0, .max = 1, .def = 1.0f});
            set(Eq::Attack, "attack", "Attack", "Attack",
                "How quickly the band moves when its range gets loud.",
                {.min = 0.5f,
                 .max = 200.0f,
                 .def = 5.0f,
                 .skewCentre = 10.0f,
                 .unit = Unit::Milliseconds,
                 .smoothing = {SmoothingKind::Multiplicative, 20.0f}});
            set(Eq::Release, "release", "Release", "Release",
                "How quickly the band moves back once its range gets quieter.",
                {.min = 10.0f,
                 .max = 2000.0f,
                 .def = 120.0f,
                 .skewCentre = 150.0f,
                 .unit = Unit::Milliseconds,
                 .smoothing = {SmoothingKind::Multiplicative, 20.0f},
                 .decimals = 0});
            set(Eq::Trigger, "trigger", "Trigger", "Trigger",
                "What the band listens to: its own range of the sound, or the same range of the sidechain.",
                {.kind = ParamKind::Choice, .min = 0, .max = 1, .def = 0.0f, .choices = kTriggers});
        }
        // Stage 2, step 2b (state version 3): spectral bands. Spectral changes the delay when it's
        // the first (ADR 0020), so it's never automated.
        for (int b = 0; b < Eq::numBands; ++b)
        {
            const std::string prefix = "band" + std::to_string(b + 1) + ".";
            const std::string label = "Band " + std::to_string(b + 1) + " ";
            auto set = [&](Eq::SpectralParam p, const char* id, const char* name,
                           std::string_view description, ParamSpec spec)
            {
                const auto i = Eq::spectralIndex(b, p);
                ids[i] = prefix + id;
                names[i] = label + name;
                shortNames[i] = name;
                descriptions[i] = description;
                spec.sinceVersion = 3;
                specs[i] = spec;
            };
            set(Eq::Spectral, "spectral", "Spectral",
                "Follows each frequency in the band on its own, so only what sticks out moves: "
                "tames resonances and harshness.",
                {.kind = ParamKind::Toggle,
                 .min = 0,
                 .max = 1,
                 .def = 0.0f,
                 .flags = ParamFlags::ChangesLatency});
            set(Eq::Detail, "detail", "Detail",
                "Spectral bands: higher finds narrow peaks and cuts them narrowly, lower treats broad "
                "build-ups.",
                {.min = 0.0f,
                 .max = 100.0f,
                 .def = 50.0f,
                 .unit = Unit::Percent,
                 .smoothing = {SmoothingKind::Linear, 50.0f},
                 .decimals = 0});
        }
        ids[Eq::Phase] = "phase";
        names[Eq::Phase] = "Processing";
        shortNames[Eq::Phase] = "Mode"; // the header's caption (the product owner, 2026-10-13)
        descriptions[Eq::Phase] =
            "Zero latency keeps the analog-style phase and adds no delay; Linear phase changes "
            "no phase but delays the sound.";
        specs[Eq::Phase] = {.kind = ParamKind::Choice,
                            .min = 0,
                            .max = 1,
                            .def = 0.0f,
                            .choices = kPhases,
                            .flags = ParamFlags::ChangesLatency,
                            .sinceVersion = 3};
        ids[Eq::Resolution] = "resolution";
        names[Eq::Resolution] = "Resolution";
        shortNames[Eq::Resolution] = "Quality";
        descriptions[Eq::Resolution] = "How fine the linear-phase and spectral processing is in the lows: "
                                       "higher is finer, with more delay.";
        specs[Eq::Resolution] = {.kind = ParamKind::Choice,
                                 .min = 0,
                                 .max = 2,
                                 .def = 1.0f,
                                 .choices = kResolutions,
                                 .flags = ParamFlags::ChangesLatency,
                                 .sinceVersion = 3};

        listenNames[0] = "Off";
        for (int b = 0; b < Eq::numBands; ++b)
            listenNames[static_cast<size_t>(b + 1)] = "Band " + std::to_string(b + 1);
        for (size_t k = 0; k < listenNames.size(); ++k)
            listenChoices[k] = listenNames[k];
        ids[Eq::AutoGain] = "autogain";
        names[Eq::AutoGain] = shortNames[Eq::AutoGain] = "Auto Gain";
        descriptions[Eq::AutoGain] =
            "Keeps the level about the same as bands boost or cut, so you judge the tone, not the loudness.";
        specs[Eq::AutoGain] = {.kind = ParamKind::Toggle, .min = 0, .max = 1, .def = 0.0f, .sinceVersion = 2};
        ids[Eq::Delta] = "delta";
        names[Eq::Delta] = shortNames[Eq::Delta] = "Delta";
        descriptions[Eq::Delta] =
            "Plays only what the dynamic and spectral bands change, to hear what they take away or add.";
        specs[Eq::Delta] = {.kind = ParamKind::Toggle,
                            .min = 0,
                            .max = 1,
                            .def = 0.0f,
                            .flags = ParamFlags::Transient,
                            .sinceVersion = 2};
        ids[Eq::Listen] = "listen";
        names[Eq::Listen] = shortNames[Eq::Listen] = "Listen";
        descriptions[Eq::Listen] = "Plays only a band's range, to find the frequency to fix.";
        specs[Eq::Listen] = {.kind = ParamKind::Choice,
                             .min = 0,
                             .max = static_cast<float>(Eq::numBands),
                             .def = 0.0f,
                             .choices = listenChoices,
                             .flags = ParamFlags::Transient,
                             .sinceVersion = 2};

        ids[Eq::Output] = "output";
        names[Eq::Output] = shortNames[Eq::Output] = "Output";
        descriptions[Eq::Output] = "The level after the EQ.";
        specs[Eq::Output] = {.min = -24.0f, .max = 24.0f, .def = 0.0f, .unit = Unit::Decibels};
        ids[Eq::Scale] = "scale";
        names[Eq::Scale] = "Gain Scale";
        shortNames[Eq::Scale] = "Scale";
        descriptions[Eq::Scale] = "Scales every band's boost or cut at once; 0 % makes the EQ flat.";
        specs[Eq::Scale] = {.min = 0.0f, .max = 200.0f, .def = 100.0f, .unit = Unit::Percent, .decimals = 0};
        ids[Eq::Analyzer] = "analyzer";
        names[Eq::Analyzer] = shortNames[Eq::Analyzer] = "Analyzer";
        descriptions[Eq::Analyzer] = "Which signal the spectrum behind the curve shows: before the EQ, after it, or both.";
        specs[Eq::Analyzer] = {.kind = ParamKind::Choice, .min = 0, .max = 3, .def = 2.0f, .choices = kAnalyzer,
                               .flags = ParamFlags::None};

        for (size_t i = 0; i < specs.size(); ++i)
        {
            specs[i].id = ids[i];
            specs[i].name = names[i];
            specs[i].shortName = shortNames[i];
            specs[i].description = descriptions[i];
        }
        // Stage 2: a sidechain, for bands triggered by another track and the masking display.
        info = DeviceInfo{.id = "eq",
                          .name = "EQ",
                          .category = "EQ & Filter",
                          .kind = DeviceKind::AudioEffect,
                          .buses = {.inputChannels = 2, .outputChannels = 2, .sidechainChannels = 2},
                          .params = specs,
                          .stateVersion = 3,
                          .meters = kMeters};
    }
};

const Tables& tables()
{
    static const Tables t;
    return t;
}

inline double processSection(const dsp::BiquadCoeffs& c, std::array<double, 2>& s, double x) noexcept
{
    const double y = c.b0 * x + s[0];
    s[0] = c.b1 * x - c.a1 * y + s[1];
    s[1] = c.b2 * x - c.a2 * y;
    return y;
}
} // namespace

int Eq::slopeDbPerOct(int slopeIndex) noexcept
{
    return kSlopeDb[std::clamp(slopeIndex, 0, 7)];
}

bool Eq::isFlat(const BandSettings& band) noexcept
{
    const bool gainType = band.type == BandType::Bell || band.type == BandType::LowShelf ||
                          band.type == BandType::HighShelf || band.type == BandType::Tilt;
    return gainType && std::abs(band.gainDb) < 1e-4;
}

Eq::Sections Eq::design(const BandSettings& band, double sampleRate) noexcept
{
    Sections s;
    using MT = dsp::MatchedType;
    switch (band.type)
    {
    case BandType::Bell:
        s.coeffs[0] = dsp::designMatched(MT::Bell, band.freq, band.gainDb, band.q, sampleRate);
        s.count = 1;
        break;
    case BandType::LowShelf:
        s.coeffs[0] = dsp::designMatched(MT::LowShelf, band.freq, band.gainDb, band.q, sampleRate);
        s.count = 1;
        break;
    case BandType::HighShelf:
        s.coeffs[0] = dsp::designMatched(MT::HighShelf, band.freq, band.gainDb, band.q, sampleRate);
        s.count = 1;
        break;
    case BandType::Tilt:
        s.coeffs[0] = dsp::designMatched(MT::Tilt, band.freq, band.gainDb, band.q, sampleRate);
        s.count = 1;
        break;
    case BandType::Notch:
        s.coeffs[0] = dsp::designMatched(MT::Notch, band.freq, 0.0, band.q, sampleRate);
        s.count = 1;
        break;
    case BandType::BandPass:
        s.coeffs[0] = dsp::designMatched(MT::Bandpass, band.freq, 0.0, band.q, sampleRate);
        s.count = 1;
        break;
    case BandType::LowCut:
    case BandType::HighCut:
    {
        const bool hp = band.type == BandType::LowCut;
        const int order = slopeDbPerOct(band.slopeIndex) / 6;
        if (order == 2)
        {
            // 12 dB/oct: one section with the band's Q (resonance).
            s.coeffs[0] = dsp::designMatched(hp ? MT::Highpass : MT::Lowpass, band.freq, 0.0, band.q, sampleRate);
            s.count = 1;
            break;
        }
        double qs[maxSections];
        const int n2 = dsp::butterworthQs(order, qs, maxSections);
        int k = 0;
        if (order % 2 == 1)
            s.coeffs[static_cast<size_t>(k++)] = dsp::designMatchedFirstOrder(hp, band.freq, sampleRate);
        for (int i = 0; i < n2 && k < maxSections; ++i)
            s.coeffs[static_cast<size_t>(k++)] =
                dsp::designMatched(hp ? MT::Highpass : MT::Lowpass, band.freq, 0.0, qs[i], sampleRate);
        s.count = k;
        break;
    }
    }
    return s;
}

bool Eq::canBeSpectral(BandType type) noexcept
{
    return type == BandType::Bell || type == BandType::LowShelf || type == BandType::HighShelf;
}

int Eq::engineOrder(int resolution, double sampleRate) noexcept
{
    // 4096, 8192 or 16384 points at 44.1 and 48 kHz: 53, 107 and 213 ms at 48 kHz.
    const int rate = sampleRate < 35000.0 ? -1 : sampleRate < 70000.0 ? 0 : sampleRate < 140000.0 ? 1 : 2;
    return 12 + std::clamp(resolution, 0, 2) + rate;
}

int Eq::latencyFor(bool engine, int resolution, double sampleRate) noexcept
{
    if (!engine)
        return 0;
    const int n = 1 << engineOrder(resolution, sampleRate);
    return n / 4 + (n - n / 4) / 2; // the engine's frame, and half its kernel (dsp::SpectralFilter)
}

bool Eq::usesEngine(const ParamStore& p) noexcept
{
    if (p.getInt(Phase) == static_cast<int>(Processing::LinearPhase))
        return true;
    for (int b = 0; b < numBands; ++b)
        if (p.getBool(spectralIndex(b, Spectral)))
            return true;
    return false;
}

int Eq::latencySamples() const noexcept
{
    return latencyFor(usesEngine(params()), params().getInt(Resolution), sampleRate_);
}

bool Eq::canMove(BandType type) noexcept
{
    return type == BandType::Bell || type == BandType::LowShelf || type == BandType::HighShelf ||
           type == BandType::Tilt;
}

std::optional<dsp::BiquadCoeffs> Eq::rangeFilter(BandType type, double freq, double q,
                                                 double sampleRate) noexcept
{
    using MT = dsp::MatchedType;
    const double f = std::clamp(freq, 10.0, 0.49 * sampleRate);
    switch (type)
    {
    case BandType::Bell:
    case BandType::Notch:
    case BandType::BandPass:
        return dsp::designMatched(MT::Bandpass, f, 0.0, std::clamp(q, 0.1, 40.0), sampleRate);
    case BandType::LowShelf:
    case BandType::LowCut:
        return dsp::designMatched(MT::Lowpass, f, 0.0, 0.7071, sampleRate);
    case BandType::HighShelf:
    case BandType::HighCut:
        return dsp::designMatched(MT::Highpass, f, 0.0, 0.7071, sampleRate);
    case BandType::Tilt:
        break;
    }
    return std::nullopt;
}

float Eq::dynamicGain(float overDb, float rangeDb) noexcept
{
    // A soft knee 6 dB wide around the threshold, then in proportion: all of Range 12 dB over.
    constexpr float knee = 3.0f, travel = 12.0f;
    float amount = 0.0f;
    if (overDb >= knee)
        amount = overDb / travel;
    else if (overDb > -knee)
        amount = (overDb + knee) * (overDb + knee) / (4.0f * knee * travel);
    return rangeDb * std::clamp(amount, 0.0f, 1.0f);
}

double Eq::loudnessChangeDb(std::span<const CurveBand> bands, double sampleRate) noexcept
{
    // Third-octave points from 20 Hz to 20 kHz: pink noise has the same power in each, weighted by
    // BS.1770's K-weighting. Left and right bands act on one side; mid and side bands on half the
    // power (an estimate: they depend on how alike the sides are).
    const auto k = dsp::kWeighting(sampleRate);
    std::array<Sections, numBands> designs{};
    const size_t n = std::min(bands.size(), designs.size());
    for (size_t i = 0; i < n; ++i)
        if (bands[i].on)
            designs[i] = design(bands[i].settings, sampleRate);
    double with = 0.0, without = 0.0;
    for (int step = -17; step <= 13; ++step)
    {
        const double hz = 1000.0 * std::pow(2.0, step / 3.0);
        if (hz >= 0.49 * sampleRate)
            break;
        const double weight =
            std::norm(dsp::biquadResponse(k[0], hz, sampleRate) * dsp::biquadResponse(k[1], hz, sampleRate));
        double left = 1.0, right = 1.0;
        for (size_t i = 0; i < n; ++i)
        {
            if (!bands[i].on || isFlat(bands[i].settings))
                continue;
            const double power = std::norm(response(designs[i], hz, sampleRate));
            switch (bands[i].place)
            {
            case Placement::Stereo:
                left *= power, right *= power;
                break;
            case Placement::Left:
                left *= power;
                break;
            case Placement::Right:
                right *= power;
                break;
            case Placement::Mid:
            case Placement::Side:
                left *= 0.5 * (power + 1.0), right *= 0.5 * (power + 1.0);
                break;
            }
        }
        with += weight * 0.5 * (left + right);
        without += weight;
    }
    return with > 0.0 && without > 0.0 ? 10.0 * std::log10(with / without) : 0.0;
}

std::complex<double> Eq::response(const Sections& s, double freqHz, double sampleRate) noexcept
{
    std::complex<double> h{1.0, 0.0};
    for (int i = 0; i < s.count; ++i)
        h *= dsp::biquadResponse(s.coeffs[static_cast<size_t>(i)], freqHz, sampleRate);
    return h;
}

Eq::Eq() : Device(tables().info) {}

void Eq::prepare(const ProcessSpec& spec)
{
    sampleRate_ = spec.sampleRate;
    rampSamples_ = dsp::msToSamples(20.0, sampleRate_);
    fadeSamples_ = dsp::msToSamples(5.0, sampleRate_);
    frameLength_ = std::max(1, dsp::msToSamples(dynamicsFrameMs, sampleRate_));
    for (auto& b : bands_)
        for (auto& d : b.detectors)
        {
            d.prepare(sampleRate_, 5.0);
            d.setMode(dsp::LevelDetector::Mode::Rms);
        }
    analysis().setSampleRate(sampleRate_); // the analyser's bins, in hertz
    analysis().fifo(PreFifo).prepare(16384);
    analysis().fifo(PostFifo).prepare(16384);
    analysis().fifo(SidechainFifo).prepare(16384);
    analysis().fifo(DynamicsFifo).prepare(static_cast<size_t>(dynamicsFrame) * 64);

    // The spectral engine, room for its largest size at this rate.
    const int maxOrder = engineOrder(2, sampleRate_);
    engine_.prepare(maxOrder);
    const auto maxN = static_cast<size_t>(1) << maxOrder;
    const auto maxBins = maxN / 2 + 1;
    spectralScratch_.prepare(static_cast<int>(maxBins));
    for (auto& b : bands_)
    {
        b.spectralDetector.prepare(static_cast<int>(maxBins));
        b.magnitude.assign(maxBins, 1.0f);
    }
    for (auto* v : {&cosW_, &cos2W_})
        v->assign(maxBins, 0.0);
    for (auto* v : {&rowA_, &rowB_, &rowC_, &rowD_, &staticLl_, &staticLr_, &staticRl_, &staticRr_})
        v->assign(maxBins, 0.0f);
    for (auto* v : {&detectL_, &detectR_, &sideL_, &sideR_})
        v->assign(maxBins, {});
    for (auto* v : {&inRingL_, &inRingR_, &scRingL_, &scRingR_, &delayStaticL_, &delayStaticR_,
                    &delayListenL_, &delayListenR_, &delayDeltaMix_, &delayListenMix_})
        v->assign(maxN, 0.0f);
    ringMask_ = static_cast<long long>(maxN) - 1;
    engineOrder_ = -1;
    reset();
}

void Eq::reset() noexcept
{
    grid_.reset();
    for (auto& b : bands_)
    {
        b.state = {};
        b.staticState = {};
        b.idle = true;
        b.restructurePending = false;
        b.designedFreq = b.staticFreq = -1.0f;
        b.triggerFreq = -1.0f; // redesigned for this rate
        b.triggerState = {};
        for (auto& d : b.detectors)
            d.reset();
        b.usualDb = -120.0f;
        b.usualSet = false;
        b.levelDb = -120.0f;
        b.dynGain.reset(0.0f);
        b.dynDb = b.designedDyn = 0.0f;
    }
    listenState_ = {};
    listening_ = -1;
    listenMix_.reset(0.0f);
    deltaMix_.reset(0.0f);
    autoGainStale_ = true;
    autoGainWait_ = 0;
    framePos_ = 0;
    applyEngine(/*force*/ true);
    tick(/*immediate*/ true);
}

void Eq::applyEngine(bool force) noexcept
{
    // Processing, Resolution and Spectral change the delay (ADR 0020): applied at a block's start,
    // starting afresh (the DAW restarts playback for them anyway).
    const auto& p = params();
    const bool on = usesEngine(p);
    const bool linear = on && p.getInt(Phase) == static_cast<int>(Processing::LinearPhase);
    const int order = engineOrder(p.getInt(Resolution), sampleRate_);
    if (cosW_.empty() || (!force && on == engineOn_ && linear == linear_ && (!on || order == engineOrder_)))
        return;
    engineOn_ = on;
    linear_ = linear;
    engineOrder_ = order;
    engine_.setOrder(order);
    const int n = engine_.fftSize();
    const int bins = engine_.bins();
    for (int k = 0; k < bins; ++k)
    {
        const double w = dsp::twoPi * k / n;
        cosW_[static_cast<size_t>(k)] = std::cos(w);
        cos2W_[static_cast<size_t>(k)] = std::cos(2.0 * w);
    }
    // The display's points: the bins within a twenty-fourth of an octave, or the nearest.
    const double binHz = sampleRate_ / n;
    for (int j = 0; j < spectralPoints; ++j)
    {
        const double hz = spectralPointHz(j);
        int lo = static_cast<int>(std::ceil(hz * std::exp2(-1.0 / 24.0) / binHz));
        int hi = static_cast<int>(std::floor(hz * std::exp2(1.0 / 24.0) / binHz));
        if (hi < lo)
            lo = hi = static_cast<int>(std::lround(hz / binHz));
        pointLo_[static_cast<size_t>(j)] = std::clamp(lo, 1, bins - 1);
        pointHi_[static_cast<size_t>(j)] = std::clamp(hi, 1, bins - 1);
    }
    for (auto& b : bands_)
    {
        b.state = {};
        b.staticState = {};
        b.idle = true;
        b.spectral = false;
        b.spectralDetector.reset();
        b.magnitudeOf = -1;
        b.respondedMix = -1.0f;
    }
    for (auto* v : {&inRingL_, &inRingR_, &scRingL_, &scRingR_, &delayStaticL_, &delayStaticR_,
                    &delayListenL_, &delayListenR_, &delayDeltaMix_, &delayListenMix_})
        std::fill(v->begin(), v->end(), 0.0f);
    ringPos_ = 0;
    spectralDisplay_.fill(0.0f);
    respondedDelta_ = -1.0;
    responseStale_ = true;
}

Eq::BandSettings Eq::settingsOf(const Band& b) const noexcept
{
    // The band where it is now: its gain, plus how far its dynamics moved it, both scaled.
    const float scale = params().get(Scale) * 0.01f;
    return {b.type, b.freq.current(), static_cast<double>((b.gain.current() + b.dynDb) * scale),
            b.q.current(), b.slopeIndex};
}

Eq::BandSettings Eq::staticSettingsOf(const Band& b) const noexcept
{
    const float scale = params().get(Scale) * 0.01f;
    return {b.type, b.freq.current(), static_cast<double>(b.gain.current() * scale), b.q.current(), b.slopeIndex};
}

void Eq::redesign(Band& b) noexcept
{
    const auto s = settingsOf(b);
    b.fromSections = b.sections;
    b.sections = design(s, sampleRate_);
    // Interpolate per sample over the next grid interval when only the values moved (same
    // structure); structural changes start fresh behind a fade.
    b.interpolating = b.fromSections.count == b.sections.count && b.designedFreq > 0.0f;
    b.flat = isFlat(s);
    b.designedFreq = b.freq.current();
    b.designedGain = b.gain.current();
    b.designedQ = b.q.current();
    b.designedDyn = b.dynDb;
    updateStatic(b);
    ++b.designs; // for the engine's response (linear phase)
    responseStale_ = true;
}

void Eq::updateStatic(Band& b) noexcept
{
    // Delta plays against the same band without its dynamics: designed only while Delta is on, and
    // only when it changes (not as the dynamics move).
    const float scale = params().get(Scale);
    const bool same = b.staticFreq == b.freq.current() && b.staticGain == b.gain.current() &&
                      b.staticQ == b.q.current() && b.staticScale == scale;
    if (b.dynDb == 0.0f)
        b.staticSections = b.sections;
    else if (same || (deltaMix_.current() <= 0.0f && deltaMix_.target() <= 0.0f))
        return;
    else
        b.staticSections = design(staticSettingsOf(b), sampleRate_);
    b.staticFreq = b.freq.current();
    b.staticGain = b.gain.current();
    b.staticQ = b.q.current();
    b.staticScale = scale;
}

void Eq::tickDynamics(Band& b, int i, bool immediate) noexcept
{
    const auto& p = params();
    const bool dynamic = p.getBool(paramIndex(i, On)) && p.getBool(dynIndex(i, Dyn)) && canMove(b.type);
    const auto trigger = static_cast<TriggerSource>(p.getInt(dynIndex(i, Trigger)));
    // Spectral: each frequency on its own, in the engine; the band itself stays at its Gain.
    const bool spectral =
        dynamic && engineOn_ && canBeSpectral(b.type) && p.getBool(spectralIndex(i, Spectral));
    if (spectral != b.spectral || (spectral && trigger != b.trigger))
    {
        b.spectralDetector.reset();
        responseStale_ = true;
    }
    b.spectral = spectral;
    if (dynamic != b.dynamic || trigger != b.trigger)
    {
        // A new source: its filter and level start afresh.
        b.triggerState = {};
        for (auto& d : b.detectors)
            d.reset();
        b.usualSet = false;
    }
    b.dynamic = dynamic;
    b.trigger = trigger;
    b.autoThreshold = p.getBool(dynIndex(i, AutoThreshold));
    // Its range as a filter, redesigned only as the band moves (designs aren't cheap).
    if (dynamic &&
        (b.type != b.triggerType || b.freq.current() != b.triggerFreq || b.q.current() != b.triggerQ))
    {
        b.triggerFilter = rangeFilter(b.type, b.freq.current(), b.q.current(), sampleRate_);
        b.triggerType = b.type;
        b.triggerFreq = b.freq.current();
        b.triggerQ = b.q.current();
    }

    // The gain moves on the control grid, with times for that rate.
    const double tickRate = sampleRate_ / grid_.interval();
    const float range = p.get(dynIndex(i, Range));
    const double attack = dsp::attackCoefficient(p.get(dynIndex(i, Attack)), tickRate);
    const double release = dsp::releaseCoefficient(p.get(dynIndex(i, Release)), tickRate);
    if (range < 0.0f) // a cut: down is the attack
        b.dynGain.setCoefficients(attack, release);
    else
        b.dynGain.setCoefficients(release, attack);

    float target = 0.0f;
    if (dynamic)
    {
        // Auto: the threshold rides 6 dB over the trigger's usual level: its average in dB over
        // about 2 s while it sounds (silence holds it), from the first level heard.
        if (b.levelDb > -80.0f)
        {
            const auto slow = static_cast<float>(1.0 - std::exp(-1.0 / (2.0 * tickRate)));
            b.usualDb = b.usualSet ? b.usualDb + slow * (b.levelDb - b.usualDb) : b.levelDb;
            b.usualSet = true;
        }
        const float threshold =
            b.autoThreshold ? std::max(b.usualDb + 6.0f, -60.0f) : p.get(dynIndex(i, Threshold));
        if (!spectral)
            target = dynamicGain(b.levelDb - threshold, range);
    }
    // Off, it glides back to rest at its release.
    if (immediate)
        b.dynGain.reset(target);
    b.dynDb = immediate ? target : b.dynGain.process(target);
    if (std::abs(b.dynDb) < 1e-3f && target == 0.0f)
        b.dynDb = 0.0f;
}

void Eq::updateAutoGain() noexcept
{
    const auto& p = params();
    const float scale = p.get(Scale) * 0.01f;
    std::array<CurveBand, numBands> curve{};
    for (int i = 0; i < numBands; ++i)
        curve[static_cast<size_t>(i)] = {
            .settings = {static_cast<BandType>(p.getInt(paramIndex(i, Type))), p.get(paramIndex(i, Freq)),
                         static_cast<double>(p.get(paramIndex(i, Gain)) * scale), p.get(paramIndex(i, Q)),
                         p.getInt(paramIndex(i, Slope))},
            .on = p.getBool(paramIndex(i, On)),
            .place = static_cast<Placement>(p.getInt(paramIndex(i, Place)))};
    autoGainDb_ = static_cast<float>(std::clamp(-loudnessChangeDb(curve, sampleRate_), -24.0, 24.0));
}

void Eq::tick(bool immediate) noexcept
{
    const auto& p = params();
    const int ramp = immediate ? 0 : rampSamples_;
    output_.setTarget(dsp::dbToGain(p.get(Output)), ramp);
    const float scale = p.get(Scale);

    // Delta and Listen fade in and out over 20 ms. Delta's still bands start from silence.
    const int fade20 = immediate ? 0 : rampSamples_;
    if (p.getBool(Delta) && deltaMix_.current() <= 0.0f && deltaMix_.target() <= 0.0f)
        for (auto& b : bands_)
            b.staticState = {};
    deltaMix_.setTarget(p.getBool(Delta) ? 1.0f : 0.0f, fade20);
    const int wanted = p.getInt(Listen) - 1;
    if (wanted != listening_)
    {
        if (listenMix_.current() <= 0.0f || immediate)
        {
            listening_ = wanted; // switch while silent
            listenState_ = {};
        }
        else
            listenMix_.setTarget(0.0f, fade20 / 2);
    }
    if (wanted == listening_)
        listenMix_.setTarget(listening_ >= 0 ? 1.0f : 0.0f, fade20);

    bool staticMoved = immediate || scale != lastScale_;
    for (int i = 0; i < numBands; ++i)
    {
        auto& b = bands_[static_cast<size_t>(i)];
        const auto type = static_cast<BandType>(p.getInt(paramIndex(i, Type)));
        const auto place = static_cast<Placement>(p.getInt(paramIndex(i, Place)));
        const int slope = p.getInt(paramIndex(i, Slope));
        const bool on = p.getBool(paramIndex(i, On));

        b.freq.setTarget(p.get(paramIndex(i, Freq)), ramp);
        b.gain.setTarget(p.get(paramIndex(i, Gain)), ramp);
        b.q.setTarget(p.get(paramIndex(i, Q)), ramp);
        staticMoved = staticMoved || b.freq.isSmoothing() || b.gain.isSmoothing() || b.q.isSmoothing() ||
                      type != b.type || place != b.place || slope != b.slopeIndex ||
                      (on ? 1.0f : 0.0f) != b.mix.target();

        tickDynamics(b, i, immediate);

        const bool structureChanged = type != b.type || place != b.place || slope != b.slopeIndex;
        if (immediate)
        {
            b.type = type;
            b.place = place;
            b.slopeIndex = slope;
            b.mix.reset(on ? 1.0f : 0.0f);
            redesign(b);
            b.interpolating = false;
            continue;
        }
        if (structureChanged && !b.restructurePending)
        {
            b.restructurePending = true; // fade out, swap structure, fade back in
            b.mix.setTarget(0.0f, fadeSamples_);
        }
        if (b.restructurePending && !b.mix.isSmoothing() && b.mix.current() == 0.0f)
        {
            b.type = type;
            b.place = place;
            b.slopeIndex = slope;
            b.state = {};
            b.staticState = {};
            b.restructurePending = false;
            b.designedFreq = -1.0f; // no interpolation from the old structure
            b.staticFreq = -1.0f;
            redesign(b);
        }
        if (!b.restructurePending)
            b.mix.setTarget(on ? 1.0f : 0.0f, fadeSamples_);

        // Coefficients follow the smoothed values (and the dynamics) on control ticks, only when
        // they moved: the dynamics by a twentieth of a dB at least, or back to rest.
        const bool active = b.mix.current() > 0.0f || b.mix.target() > 0.0f;
        const bool dynMoved =
            std::abs(b.dynDb - b.designedDyn) >= 0.05f || (b.dynDb == 0.0f) != (b.designedDyn == 0.0f);
        if (active && (b.freq.current() != b.designedFreq || b.gain.current() != b.designedGain ||
                       b.q.current() != b.designedQ || dynMoved || scale != lastScale_))
            redesign(b);
        else
        {
            b.interpolating = false;
            if (active && b.dynDb != 0.0f)
                updateStatic(b); // Delta just came on
        }
    }
    lastScale_ = scale;

    // Auto Gain: re-estimated from the static curve when it moved, at most every 16 ticks (it
    // glides over 50 ms anyway).
    autoGainStale_ = autoGainStale_ || staticMoved;
    if (autoGainWait_ > 0)
        --autoGainWait_;
    const bool autoGain = p.getBool(AutoGain);
    if (autoGain && autoGainStale_ && (autoGainWait_ == 0 || immediate))
    {
        updateAutoGain();
        autoGainStale_ = false;
        autoGainWait_ = 16;
    }
    autoGain_.setTarget(autoGain ? dsp::dbToGain(autoGainDb_) : 1.0f,
                        immediate ? 0 : dsp::msToSamples(50.0, sampleRate_));

    // Listen's filter: the band's range, where the band is now.
    if (listening_ >= 0)
    {
        const auto& b = bands_[static_cast<size_t>(listening_)];
        const auto f = rangeFilter(b.type, b.freq.current(), b.q.current(), sampleRate_);
        listenPass_ = !f.has_value();
        if (f)
            listenCoeffs_ = *f;
    }
}

void Eq::detect(Band& b, double l, double r) noexcept
{
    // The trigger: the band's range of its placement's signal.
    const auto filtered = [&b](int ch, double x)
    {
        return b.triggerFilter ? processSection(*b.triggerFilter, b.triggerState[static_cast<size_t>(ch)], x)
                               : x;
    };
    float rms = 0.0f;
    switch (b.place)
    {
    case Placement::Stereo:
        rms = std::max(b.detectors[0].process(static_cast<float>(filtered(0, l))).rms,
                       b.detectors[1].process(static_cast<float>(filtered(1, r))).rms);
        break;
    case Placement::Left:
        rms = b.detectors[0].process(static_cast<float>(filtered(0, l))).rms;
        break;
    case Placement::Right:
        rms = b.detectors[1].process(static_cast<float>(filtered(1, r))).rms;
        break;
    case Placement::Mid:
        rms = b.detectors[0].process(static_cast<float>(filtered(0, 0.5 * (l + r)))).rms;
        break;
    case Placement::Side:
        rms = b.detectors[1].process(static_cast<float>(filtered(1, 0.5 * (l - r)))).rms;
        break;
    }
    b.levelDb = rms > 1e-6f ? dsp::gainToDb(rms) : -120.0f;
}

void Eq::magnitudeOf(const Sections& s, float* out, int bins, bool power) const noexcept
{
    // |H|^2 of each section from cos w and cos 2w: no complex exponentials per bin.
    for (int k = 0; k < bins; ++k)
    {
        const double c1 = cosW_[static_cast<size_t>(k)], c2 = cos2W_[static_cast<size_t>(k)];
        double product = 1.0;
        for (int i = 0; i < s.count; ++i)
        {
            const auto& c = s.coeffs[static_cast<size_t>(i)];
            const double num = c.b0 * c.b0 + c.b1 * c.b1 + c.b2 * c.b2 +
                               2.0 * (c.b0 * c.b1 + c.b1 * c.b2) * c1 + 2.0 * c.b0 * c.b2 * c2;
            const double den =
                1.0 + c.a1 * c.a1 + c.a2 * c.a2 + 2.0 * (c.a1 + c.a1 * c.a2) * c1 + 2.0 * c.a2 * c2;
            product *= std::max(num, 0.0) / std::max(den, 1e-300);
        }
        out[k] = static_cast<float>(power ? product : std::sqrt(product));
    }
}

void Eq::applyPlaced(Placement place, const float* gain, float* ll, float* lr, float* rl, float* rr,
                     int bins) const noexcept
{
    // The band after what's there: its gains on its placement, as a 2x2 matrix per bin (left and
    // right out from left and right in), so left/right and mid/side bands share one frame.
    switch (place)
    {
    case Placement::Stereo:
        for (int k = 0; k < bins; ++k)
            ll[k] *= gain[k], lr[k] *= gain[k], rl[k] *= gain[k], rr[k] *= gain[k];
        break;
    case Placement::Left:
        for (int k = 0; k < bins; ++k)
            ll[k] *= gain[k], lr[k] *= gain[k];
        break;
    case Placement::Right:
        for (int k = 0; k < bins; ++k)
            rl[k] *= gain[k], rr[k] *= gain[k];
        break;
    case Placement::Mid:
    case Placement::Side:
        // Mid: l' = (g+1)/2 l + (g-1)/2 r; side: l' = (1+g)/2 l + (1-g)/2 r (and mirrored for r').
        for (int k = 0; k < bins; ++k)
        {
            const float a = 0.5f * (1.0f + gain[k]);
            const float c = place == Placement::Mid ? 0.5f * (gain[k] - 1.0f) : 0.5f * (1.0f - gain[k]);
            const float l0 = ll[k], l1 = lr[k], r0 = rl[k], r1 = rr[k];
            ll[k] = a * l0 + c * r0;
            lr[k] = a * l1 + c * r1;
            rl[k] = c * l0 + a * r0;
            rr[k] = c * l1 + a * r1;
        }
        break;
    }
}

bool Eq::respond(dsp::SpectralFilter::Frame& f) noexcept
{
    const auto& p = params();
    const int bins = f.bins;
    // Rebuilt when something moved: a band's design or fade, Delta, or a spectral band (always).
    const double delta = linear_ ? deltaMix_.current() : 0.0;
    bool stale = responseStale_ || delta != respondedDelta_;
    for (const auto& b : bands_)
        stale = stale || b.spectral || static_cast<float>(b.mix.current()) != b.respondedMix;
    if (!stale)
        return false;
    responseStale_ = false;
    respondedDelta_ = delta;

    std::fill_n(f.ll, bins, 1.0f);
    std::fill_n(f.rr, bins, 1.0f);
    std::fill_n(f.lr, bins, 0.0f);
    std::fill_n(f.rl, bins, 0.0f);
    if (delta > 0.0)
    {
        std::fill_n(staticLl_.data(), bins, 1.0f);
        std::fill_n(staticRr_.data(), bins, 1.0f);
        std::fill_n(staticLr_.data(), bins, 0.0f);
        std::fill_n(staticRl_.data(), bins, 0.0f);
    }
    spectralDisplay_.fill(0.0f);
    const float scale = p.get(Scale) * 0.01f;
    const float norm =
        16.0f / (static_cast<float>(engine_.frameLength()) * static_cast<float>(engine_.frameLength()));
    bool haveInput = false, haveSide = false; // in linear phase the engine hears the input itself
    const std::complex<float>* inputL = detectL_.data();
    const std::complex<float>* inputR = detectR_.data();

    for (int i = 0; i < numBands; ++i)
    {
        auto& b = bands_[static_cast<size_t>(i)];
        const auto mix = static_cast<float>(b.mix.current());
        b.respondedMix = mix;
        if (mix <= 0.0f)
            continue;
        float* gain = rowA_.data();
        bool shaped = false;
        // Linear phase: every band's magnitude, at its Gain and where its dynamics moved it.
        const bool flat = b.flat && b.dynDb == 0.0f;
        if (linear_ && !flat)
        {
            if (b.magnitudeOf != b.designs)
            {
                magnitudeOf(b.sections, b.magnitude.data(), bins);
                b.magnitudeOf = b.designs;
            }
            for (int k = 0; k < bins; ++k)
                gain[k] = 1.0f + mix * (b.magnitude[static_cast<size_t>(k)] - 1.0f);
            shaped = true;
        }
        // A spectral band: each frequency on its own, from its trigger's spectrum.
        if (b.spectral)
        {
            const bool fromSide = b.trigger == TriggerSource::Sidechain && sidechainConnected_;
            if (fromSide && !haveSide)
            {
                engine_.analyse(scRingL_.data(), scRingR_.data(), ringMask_, engine_.samplesIn(),
                                sideL_.data(), sideR_.data());
                haveSide = true;
            }
            if (!fromSide && !haveInput && linear_)
            {
                inputL = f.left();
                inputR = f.right();
                haveInput = true;
            }
            if (!fromSide && !haveInput)
            {
                engine_.analyse(inRingL_.data(), inRingR_.data(), ringMask_, engine_.samplesIn(),
                                detectL_.data(), detectR_.data());
                haveInput = true;
            }
            const auto* xl = fromSide ? sideL_.data() : inputL;
            const auto* xr = fromSide ? sideR_.data() : inputR;
            float* power = rowB_.data();
            for (int k = 0; k < bins; ++k)
            {
                switch (b.place)
                {
                case Placement::Stereo:
                    power[k] = std::max(std::norm(xl[k]), std::norm(xr[k]));
                    break;
                case Placement::Left:
                    power[k] = std::norm(xl[k]);
                    break;
                case Placement::Right:
                    power[k] = std::norm(xr[k]);
                    break;
                case Placement::Mid:
                    power[k] = std::norm(0.5f * (xl[k] + xr[k]));
                    break;
                case Placement::Side:
                    power[k] = std::norm(0.5f * (xl[k] - xr[k]));
                    break;
                }
                power[k] *= norm;
            }
            // Where it may act: its range, as its trigger hears it.
            float* weight = rowC_.data();
            if (b.triggerFilter)
                magnitudeOf({.coeffs = {*b.triggerFilter}, .count = 1}, weight, bins, /*power*/ true);
            else
                std::fill_n(weight, bins, 1.0f);
            const dsp::SpectralDynamics::Settings settings{.sampleRate = sampleRate_,
                                                           .fftSize = engine_.fftSize(),
                                                           .padding =
                                                               engine_.fftSize() / engine_.frameLength(),
                                                           .frameSeconds = engine_.hop() / sampleRate_,
                                                           .rangeDb = p.get(dynIndex(i, Range)),
                                                           .autoThreshold = b.autoThreshold,
                                                           .thresholdDb = p.get(dynIndex(i, Threshold)),
                                                           .detail = p.get(spectralIndex(i, Detail)) * 0.01f,
                                                           .attackMs = p.get(dynIndex(i, Attack)),
                                                           .releaseMs = p.get(dynIndex(i, Release))};
            float* moved = rowD_.data();
            b.spectralDetector.process(power, weight, bins, settings, spectralScratch_, moved);
            const float amount = scale * mix;
            const float toGain = amount * std::log(10.0f) / 20.0f; // dB to gain, through exp
            for (int k = 0; k < bins; ++k)
            {
                const float g = std::exp(toGain * moved[k]);
                gain[k] = shaped ? gain[k] * g : g;
            }
            shaped = true;
            // For the display: the most it moves near each point.
            const bool boost = settings.rangeDb > 0.0f;
            for (int j = 0; j < spectralPoints; ++j)
            {
                float most = 0.0f;
                for (int k = pointLo_[static_cast<size_t>(j)]; k <= pointHi_[static_cast<size_t>(j)]; ++k)
                    most = boost ? std::max(most, moved[k]) : std::min(most, moved[k]);
                spectralDisplay_[static_cast<size_t>(j)] += amount * most;
            }
        }
        if (shaped)
            applyPlaced(b.place, gain, f.ll, f.lr, f.rl, f.rr, bins);

        // Delta: the same bands held still (at their Gain, no dynamics, no spectral moves).
        if (delta > 0.0 && !flat)
        {
            const float* still = b.magnitude.data();
            if (b.dynDb != 0.0f)
            {
                magnitudeOf(b.staticSections, rowC_.data(), bins);
                still = rowC_.data();
            }
            for (int k = 0; k < bins; ++k)
                gain[k] = 1.0f + mix * (still[k] - 1.0f);
            applyPlaced(b.place, gain, staticLl_.data(), staticLr_.data(), staticRl_.data(), staticRr_.data(),
                        bins);
        }
    }
    if (delta > 0.0)
    {
        const auto d = static_cast<float>(delta);
        for (int k = 0; k < bins; ++k)
        {
            f.ll[k] -= d * staticLl_[static_cast<size_t>(k)];
            f.lr[k] -= d * staticLr_[static_cast<size_t>(k)];
            f.rl[k] -= d * staticRl_[static_cast<size_t>(k)];
            f.rr[k] -= d * staticRr_[static_cast<size_t>(k)];
        }
    }
    return true;
}

void Eq::process(ProcessContext& ctx) noexcept
{
    const int n = ctx.numSamples;
    if (n <= 0 || ctx.output.numChannels == 0)
        return;
    applyEngine(/*force*/ false);
    const int inChannels = std::min(ctx.input.numChannels, maxChannels);
    const int channels = std::min(ctx.output.numChannels, maxChannels);
    const int scChannels = std::min(ctx.sidechain.numChannels, maxChannels);
    sidechainConnected_ = scChannels > 0;

    const int interval = grid_.interval();
    grid_.forEachSegment(n, [&](int offset, int length, bool isTick) noexcept {
        // Phase within the grid interval, from the absolute position (block-size invariant).
        const int phase0 = static_cast<int>(grid_.position() % interval);
        if (isTick)
            tick(false);
        const bool delta = deltaMix_.current() > 0.0f || deltaMix_.target() > 0.0f;
        std::array<float, 64> pre{}, post{}, side{}; // analyzer taps, pushed once per segment
        // The segment, between the bands and the output: the engine (when on) runs in between.
        std::array<double, 64> wetL{}, wetR{}, stillL{}, stillR{}, gains{}, deltas{}, listens{}, heardL{},
            heardR{};
        for (int i = offset; i < offset + length; ++i)
        {
            const auto j = static_cast<size_t>(i - offset);
            const double t = static_cast<double>(phase0 + (i - offset) + 1) / interval;
            const double inL = ctx.input.channel(0)[i];
            const double inR = inChannels > 1 ? ctx.input.channel(1)[i] : inL;
            double l = inL, r = inR, sl = inL, sr = inR; // sl, sr: the same bands held still (Delta)
            pre[j] = static_cast<float>(0.5 * (l + r));
            double scL = inL, scR = inR; // no sidechain: bands triggered by it listen to the input
            if (scChannels > 0)
            {
                scL = ctx.sidechain.channel(0)[i];
                scR = scChannels > 1 ? ctx.sidechain.channel(1)[i] : scL;
                side[j] = static_cast<float>(0.5 * (scL + scR));
            }
            if (engineOn_)
            {
                // Kept for the spectral bands' detection, a frame at a time.
                const auto at = static_cast<size_t>((ringPos_ + static_cast<long long>(j)) & ringMask_);
                inRingL_[at] = static_cast<float>(inL);
                inRingR_[at] = static_cast<float>(inR);
                scRingL_[at] = static_cast<float>(scL);
                scRingR_[at] = static_cast<float>(scR);
            }

            for (auto& b : bands_)
            {
                b.freq.next();
                b.gain.next();
                b.q.next();
                const double mix = b.mix.next();
                if (b.dynamic)
                {
                    if (b.trigger == TriggerSource::Sidechain)
                        detect(b, scL, scR);
                    else
                        detect(b, inL, inR);
                }
                // Skip bands that are off, or flat and settled (never mid-ramp: a gain ramp can
                // pass exactly through 0 dB), or in the engine (linear phase). Entering idle clears
                // the state, so resuming is clean.
                const bool skip =
                    linear_ || mix <= 0.0 || (b.flat && !b.gain.isSmoothing() && b.dynDb == 0.0f);
                if (skip)
                {
                    if (!b.idle)
                    {
                        b.state = {};
                        b.staticState = {};
                        b.idle = true;
                    }
                    continue;
                }
                b.idle = false;
                // Coefficients ramp linearly from the previous tick's design to this tick's,
                // so large, fast changes stay smooth (no 32-sample staircase).
                std::array<dsp::BiquadCoeffs, maxSections> c;
                for (int s = 0; s < b.sections.count; ++s)
                {
                    const auto& to = b.sections.coeffs[static_cast<size_t>(s)];
                    if (!b.interpolating)
                    {
                        c[static_cast<size_t>(s)] = to;
                        continue;
                    }
                    const auto& from = b.fromSections.coeffs[static_cast<size_t>(s)];
                    c[static_cast<size_t>(s)] = {from.b0 + t * (to.b0 - from.b0), from.b1 + t * (to.b1 - from.b1),
                                                 from.b2 + t * (to.b2 - from.b2), from.a1 + t * (to.a1 - from.a1),
                                                 from.a2 + t * (to.a2 - from.a2)};
                }
                auto run = [&](int ch, double x) {
                    double y = x;
                    for (int s = 0; s < b.sections.count; ++s)
                        y = processSection(c[static_cast<size_t>(s)],
                                           b.state[static_cast<size_t>(ch)][static_cast<size_t>(s)], y);
                    return x + mix * (y - x);
                };
                auto runStatic = [&](int ch, double x)
                {
                    double y = x;
                    for (int s = 0; s < b.staticSections.count; ++s)
                        y = processSection(b.staticSections.coeffs[static_cast<size_t>(s)],
                                           b.staticState[static_cast<size_t>(ch)][static_cast<size_t>(s)], y);
                    return x + mix * (y - x);
                };
                const auto place = [&](auto&& f, double& left, double& right)
                {
                    switch (b.place)
                    {
                    case Placement::Stereo:
                        left = f(0, left);
                        right = f(1, right);
                        break;
                    case Placement::Left:
                        left = f(0, left);
                        break;
                    case Placement::Right:
                        right = f(1, right);
                        break;
                    case Placement::Mid:
                    case Placement::Side:
                    {
                        double m = 0.5 * (left + right), s = 0.5 * (left - right);
                        if (b.place == Placement::Mid)
                            m = f(0, m);
                        else
                            s = f(1, s);
                        left = m + s;
                        right = m - s;
                        break;
                    }
                    }
                };
                place(run, l, r);
                if (delta)
                    place(runStatic, sl, sr);
            }

            gains[j] = output_.next() * autoGain_.next();
            if (delta)
                deltas[j] = deltaMix_.next();
            // Listen: only a band's range of the input, on its placement.
            if (const double m = listenMix_.next(); m > 0.0 && listening_ >= 0)
            {
                listens[j] = m;
                const auto& lb = bands_[static_cast<size_t>(listening_)];
                const auto filter = [this](int ch, double x)
                {
                    return listenPass_
                               ? x
                               : processSection(listenCoeffs_, listenState_[static_cast<size_t>(ch)], x);
                };
                double hl = 0.0, hr = 0.0;
                switch (lb.place)
                {
                case Placement::Stereo:
                    hl = filter(0, inL), hr = filter(1, inR);
                    break;
                case Placement::Left:
                    hl = filter(0, inL);
                    break;
                case Placement::Right:
                    hr = filter(1, inR);
                    break;
                case Placement::Mid:
                    hl = hr = filter(0, 0.5 * (inL + inR));
                    break;
                case Placement::Side:
                    hl = filter(1, 0.5 * (inL - inR)), hr = -hl;
                    break;
                }
                heardL[j] = hl;
                heardR[j] = hr;
            }
            wetL[j] = l;
            wetR[j] = r;
            stillL[j] = sl;
            stillR[j] = sr;
        }

        if (engineOn_)
        {
            // The spectral engine: linear phase, or the spectral bands after the others.
            std::array<float, 64> el{}, er{};
            for (int j = 0; j < length; ++j)
            {
                el[static_cast<size_t>(j)] = static_cast<float>(wetL[static_cast<size_t>(j)]);
                er[static_cast<size_t>(j)] = static_cast<float>(wetR[static_cast<size_t>(j)]);
            }
            engine_.process(el.data(), er.data(), length,
                            [this](dsp::SpectralFilter::Frame& f) noexcept { return respond(f); });
            // Delta's still bands (zero latency) and Listen, and their fades, delayed to line up
            // with it.
            const long long latency = engine_.latencySamples();
            const auto delayed = [this, latency](std::vector<float>& line, long long at, double x)
            {
                line[static_cast<size_t>(at & ringMask_)] = static_cast<float>(x);
                return static_cast<double>(line[static_cast<size_t>((at - latency) & ringMask_)]);
            };
            for (int j = 0; j < length; ++j)
            {
                const auto i = static_cast<size_t>(j);
                const long long at = ringPos_ + j;
                wetL[i] = el[i];
                wetR[i] = er[i];
                stillL[i] = delayed(delayStaticL_, at, stillL[i]);
                stillR[i] = delayed(delayStaticR_, at, stillR[i]);
                heardL[i] = delayed(delayListenL_, at, heardL[i]);
                heardR[i] = delayed(delayListenR_, at, heardR[i]);
                deltas[i] = delayed(delayDeltaMix_, at, deltas[i]);
                listens[i] = delayed(delayListenMix_, at, listens[i]);
            }
            ringPos_ += length;
        }

        for (int i = offset; i < offset + length; ++i)
        {
            const auto j = static_cast<size_t>(i - offset);
            double l = wetL[j], r = wetR[j];
            const double g = gains[j];
            // Delta: only what the dynamics changed (in linear phase, the engine's response is).
            if (delta && !linear_)
            {
                l -= deltas[j] * stillL[j];
                r -= deltas[j] * stillR[j];
            }
            l *= g;
            r *= g;
            if (const double m = listens[j]; m > 0.0)
            {
                l += m * (heardL[j] - l);
                r += m * (heardR[j] - r);
            }
            ctx.output.channel(0)[i] = static_cast<float>(l);
            if (channels > 1)
                ctx.output.channel(1)[i] = static_cast<float>(r);
            post[j] = static_cast<float>(0.5 * (l + r));
        }
        analysis().fifo(PreFifo).push(pre.data(), static_cast<size_t>(length));
        analysis().fifo(PostFifo).push(post.data(), static_cast<size_t>(length));
        if (scChannels > 0)
            analysis().fifo(SidechainFifo).push(side.data(), static_cast<size_t>(length));
        // Every 10 ms: where each dynamic band is, and what its trigger hears.
        if ((framePos_ += length) >= frameLength_)
        {
            framePos_ -= frameLength_;
            std::array<float, dynamicsFrame> frame{};
            for (int k = 0; k < numBands; ++k)
            {
                const auto& b = bands_[static_cast<size_t>(k)];
                frame[static_cast<size_t>(k)] = b.dynDb;
                frame[static_cast<size_t>(numBands + k)] = b.dynamic ? b.levelDb : -120.0f;
            }
            std::copy(spectralDisplay_.begin(), spectralDisplay_.end(), frame.begin() + 2 * numBands);
            analysis().fifo(DynamicsFifo).pushFrame(frame.data(), frame.size());
        }
    });
    analysis().publish(AutoGainDb, params().getBool(AutoGain) ? autoGainDb_ : 0.0f);
}

const DeviceFactory& eqFactory()
{
    static const DeviceFactory factory{
        .info = []() -> const DeviceInfo& { return tables().info; },
        .create = []() -> std::unique_ptr<Device> { return std::make_unique<Eq>(); },
    };
    return factory;
}
} // namespace fd::devices
