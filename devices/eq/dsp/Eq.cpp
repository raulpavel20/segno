#include "Eq.h"

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

/// The parameter table is generated once (IDs like "band3.freq" need stable storage).
struct Tables
{
    std::array<std::string, Eq::NumParams> ids, names, shortNames;
    std::array<std::string_view, Eq::NumParams> descriptions; // as in SPEC.md (open question B22)
    std::array<ParamSpec, Eq::NumParams> specs{};
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
        info = DeviceInfo{.id = "eq", .name = "EQ", .category = "EQ & Filter", .kind = DeviceKind::AudioEffect,
                          .buses = {.inputChannels = 2, .outputChannels = 2}, .params = specs, .stateVersion = 1};
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
    analysis().fifo(PreFifo).prepare(16384);
    analysis().fifo(PostFifo).prepare(16384);
    reset();
}

void Eq::reset() noexcept
{
    grid_.reset();
    for (auto& b : bands_)
    {
        b.state = {};
        b.idle = true;
        b.restructurePending = false;
        b.designedFreq = -1.0f;
    }
    tick(/*immediate*/ true);
}

Eq::BandSettings Eq::settingsOf(const Band& b) const noexcept
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
}

void Eq::tick(bool immediate) noexcept
{
    const auto& p = params();
    output_.setTarget(dsp::dbToGain(p.get(Output)), immediate ? 0 : rampSamples_);
    const float scale = p.get(Scale);
    for (int i = 0; i < numBands; ++i)
    {
        auto& b = bands_[static_cast<size_t>(i)];
        const auto type = static_cast<BandType>(p.getInt(paramIndex(i, Type)));
        const auto place = static_cast<Placement>(p.getInt(paramIndex(i, Place)));
        const int slope = p.getInt(paramIndex(i, Slope));
        const bool on = p.getBool(paramIndex(i, On));

        const int ramp = immediate ? 0 : rampSamples_;
        b.freq.setTarget(p.get(paramIndex(i, Freq)), ramp);
        b.gain.setTarget(p.get(paramIndex(i, Gain)), ramp);
        b.q.setTarget(p.get(paramIndex(i, Q)), ramp);

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
            b.restructurePending = false;
            b.designedFreq = -1.0f; // no interpolation from the old structure
            redesign(b);
        }
        if (!b.restructurePending)
            b.mix.setTarget(on ? 1.0f : 0.0f, fadeSamples_);

        // Coefficients follow the smoothed values on control ticks (only when they moved).
        const bool active = b.mix.current() > 0.0f || b.mix.target() > 0.0f;
        if (active && (b.freq.current() != b.designedFreq || b.gain.current() != b.designedGain ||
                       b.q.current() != b.designedQ || scale != lastScale_))
            redesign(b);
        else
            b.interpolating = false;
    }
    lastScale_ = scale;
}

void Eq::process(ProcessContext& ctx) noexcept
{
    const int n = ctx.numSamples;
    if (n <= 0 || ctx.output.numChannels == 0)
        return;
    const int inChannels = std::min(ctx.input.numChannels, maxChannels);
    const int channels = std::min(ctx.output.numChannels, maxChannels);

    const int interval = grid_.interval();
    grid_.forEachSegment(n, [&](int offset, int length, bool isTick) noexcept {
        // Phase within the grid interval, from the absolute position (block-size invariant).
        const int phase0 = static_cast<int>(grid_.position() % interval);
        if (isTick)
            tick(false);
        std::array<float, 64> pre{}, post{}; // analyzer taps, pushed once per segment
        for (int i = offset; i < offset + length; ++i)
        {
            const double t = static_cast<double>(phase0 + (i - offset) + 1) / interval;
            double l = ctx.input.channel(0)[i];
            double r = inChannels > 1 ? ctx.input.channel(1)[i] : l;
            pre[static_cast<size_t>(i - offset)] = static_cast<float>(0.5 * (l + r));

            for (auto& b : bands_)
            {
                b.freq.next();
                b.gain.next();
                b.q.next();
                const double mix = b.mix.next();
                // Skip bands that are off, or flat and settled (never mid-ramp: a gain ramp can
                // pass exactly through 0 dB). Entering idle clears the state, so resuming is clean.
                const bool skip = mix <= 0.0 || (b.flat && !b.gain.isSmoothing());
                if (skip)
                {
                    if (!b.idle)
                    {
                        b.state = {};
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
                switch (b.place)
                {
                case Placement::Stereo:
                    l = run(0, l);
                    r = run(1, r);
                    break;
                case Placement::Left:
                    l = run(0, l);
                    break;
                case Placement::Right:
                    r = run(1, r);
                    break;
                case Placement::Mid:
                case Placement::Side:
                {
                    double m = 0.5 * (l + r), s = 0.5 * (l - r);
                    if (b.place == Placement::Mid)
                        m = run(0, m);
                    else
                        s = run(1, s);
                    l = m + s;
                    r = m - s;
                    break;
                }
                }
            }

            const double g = output_.next();
            l *= g;
            r *= g;
            ctx.output.channel(0)[i] = static_cast<float>(l);
            if (channels > 1)
                ctx.output.channel(1)[i] = static_cast<float>(r);
            post[static_cast<size_t>(i - offset)] = static_cast<float>(0.5 * (l + r));
        }
        analysis().fifo(PreFifo).push(pre.data(), static_cast<size_t>(length));
        analysis().fifo(PostFifo).push(post.data(), static_cast<size_t>(length));
    });
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
