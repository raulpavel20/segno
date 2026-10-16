#include "GrainSource.h"

#include "Sfz.h"
#include "fd/device/Library.h"
#include "fd/dsp/Onsets.h"
#include "fd/dsp/PitchDetector.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <map>
#include <mutex>
#include <numbers>

namespace fd::devices
{
namespace
{
/// I0, the modified Bessel function of the first kind (the Kaiser window's).
double besselI0(double x) noexcept
{
    double sum = 1.0, term = 1.0;
    for (int k = 1; k < 64; ++k)
    {
        term *= (x / (2.0 * k)) * (x / (2.0 * k));
        sum += term;
        if (term < 1e-12 * sum)
            break;
    }
    return sum;
}

/// The odd phase of a half-band interpolator: what lies halfway between two samples, from the 32
/// either side (a Kaiser-windowed sinc, β 9: images under −90 dB above 0.295 of the doubled rate,
/// where a decimated level's content ends at 0.205).
struct HalfBand
{
    static constexpr int side = 32;
    std::array<float, 2 * side> taps{};
    HalfBand()
    {
        constexpr double beta = 9.0;
        double sum = 0.0;
        for (int j = 0; j < 2 * side; ++j)
        {
            const double t = j - side + 0.5; // the tap's distance from the point between samples
            const double u = t / (side + 0.5);
            const double w = besselI0(beta * std::sqrt(std::max(0.0, 1.0 - u * u))) / besselI0(beta);
            const double s = std::sin(std::numbers::pi * t) / (std::numbers::pi * t);
            taps[static_cast<size_t>(j)] = static_cast<float>(s * w);
            sum += s * w;
        }
        for (auto& c : taps)
            c = static_cast<float>(c / sum); // unity at DC
    }
};

/// `in` with `pad` zeros either side, so the filters below run without a test at the edges.
std::vector<float> padded(std::span<const float> in, int pad)
{
    std::vector<float> p(in.size() + 2 * static_cast<size_t>(pad), 0.0f);
    std::copy(in.begin(), in.end(), p.begin() + pad);
    return p;
}

/// `in` at twice its rate: its own samples, and the points between them.
std::vector<float> upsample(std::span<const float> in)
{
    static const HalfBand h;
    constexpr int side = HalfBand::side;
    const int n = static_cast<int>(in.size());
    const auto p = padded(in, side);
    std::vector<float> out(static_cast<size_t>(2 * n), 0.0f);
    for (int i = 0; i < n; ++i)
    {
        const float* x = p.data() + i + 1; // in[i - side + 1] … in[i + side]
        float y = 0.0f;
        for (int j = 0; j < 2 * side; ++j)
            y += h.taps[static_cast<size_t>(j)] * x[j];
        out[static_cast<size_t>(2 * i)] = in[static_cast<size_t>(i)];
        out[static_cast<size_t>(2 * i + 1)] = y;
    }
    return out;
}

/// A level halved: low-passed first, its pass band to 0.205 of its rate and its stop band from
/// 0.25 at about 80 dB (dsp::decimateByTwo's design: 129 taps, a Kaiser-windowed sinc, β 8), in
/// floats with its symmetric taps folded. The pyramid's build is most of a load.
std::vector<float> halve(std::span<const float> in)
{
    constexpr int half = 64;
    static const auto taps = []
    {
        constexpr double fc = 2.0 * 0.2275, beta = 8.0;
        std::array<double, half + 1> h{};
        double sum = 0.0;
        for (int k = 0; k <= half; ++k)
        {
            const double x = fc * k;
            const double s = k == 0 ? 1.0 : std::sin(std::numbers::pi * x) / (std::numbers::pi * x);
            const double u = static_cast<double>(k) / (half + 1);
            h[static_cast<size_t>(k)] = fc * s * besselI0(beta * std::sqrt(1.0 - u * u)) / besselI0(beta);
            sum += (k == 0 ? 1.0 : 2.0) * h[static_cast<size_t>(k)];
        }
        std::array<float, half + 1> f{};
        for (int k = 0; k <= half; ++k)
            f[static_cast<size_t>(k)] = static_cast<float>(h[static_cast<size_t>(k)] / sum);
        return f;
    }();
    const int n = static_cast<int>(in.size());
    const auto p = padded(in, half);
    std::vector<float> out(static_cast<size_t>((n + 1) / 2));
    for (int i = 0; i < static_cast<int>(out.size()); ++i)
    {
        const float* c = p.data() + half + 2 * i;
        float y = taps[0] * c[0];
        for (int k = 1; k <= half; ++k)
            y += taps[static_cast<size_t>(k)] * (c[-k] + c[k]);
        out[static_cast<size_t>(i)] = y;
    }
    return out;
}

/// Channels into one interleaved, padded level.
GrainSource::Level interleave(const std::vector<std::vector<float>>& channels, bool compact, double scale)
{
    GrainSource::Level l;
    const int c = static_cast<int>(channels.size());
    l.frames = channels.empty() ? 0 : static_cast<int>(channels[0].size());
    l.scale = scale;
    const auto total = static_cast<size_t>(l.frames + 2 * GrainSource::pad) * static_cast<size_t>(c);
    if (compact)
        l.i16.assign(total, 0);
    else
        l.f32.assign(total, 0.0f);
    for (int i = 0; i < l.frames; ++i)
        for (int k = 0; k < c; ++k)
        {
            const float x = channels[static_cast<size_t>(k)][static_cast<size_t>(i)];
            const auto at =
                static_cast<size_t>(i + GrainSource::pad) * static_cast<size_t>(c) + static_cast<size_t>(k);
            if (compact)
                l.i16[at] = static_cast<std::int16_t>(std::lround(std::clamp(x, -1.0f, 1.0f) * 32767.0f));
            else
                l.f32[at] = x;
        }
    return l;
}

/// The root a sound's pitch gives: the median of its steady readings, when most of it is pitched.
std::optional<double> rootFromSound(std::span<const float> mono, double sampleRate)
{
    if (mono.empty() || sampleRate <= 0.0)
        return std::nullopt;
    dsp::PitchDetector detector;
    detector.prepare(sampleRate);
    std::vector<double> hz;
    int readings = 0;
    const auto length = std::min(mono.size(), static_cast<size_t>(30.0 * sampleRate));
    detector.push(mono.data(), static_cast<int>(length),
                  [&](const dsp::PitchDetector::Reading& r)
                  {
                      if (r.levelDb > dsp::PitchDetector::gateDb)
                          ++readings;
                      if (r.valid && r.clarity >= 0.9f)
                          hz.push_back(r.hz);
                  });
    if (readings < 4 || static_cast<double>(hz.size()) < 0.4 * readings)
        return std::nullopt;
    std::nth_element(hz.begin(), hz.begin() + static_cast<long>(hz.size() / 2), hz.end());
    return 69.0 + 12.0 * std::log2(hz[hz.size() / 2] / 440.0);
}
} // namespace

std::optional<double> GrainSource::rootFromName(std::string_view name)
{
    // A note name with its octave standing alone: a letter, an optional # or b, a number, with
    // nothing alphanumeric either side ("Cello_C2", "pad F#3").
    for (size_t i = 0; i < name.size(); ++i)
    {
        const char c = static_cast<char>(std::toupper(static_cast<unsigned char>(name[i])));
        if (c < 'A' || c > 'G' || (i > 0 && std::isalnum(static_cast<unsigned char>(name[i - 1]))))
            continue;
        static constexpr int semis[] = {9, 11, 0, 2, 4, 5, 7};
        int note = semis[c - 'A'];
        size_t j = i + 1;
        if (j < name.size() && (name[j] == '#' || name[j] == 'b'))
            note += name[j++] == '#' ? 1 : -1;
        const bool negative = j < name.size() && name[j] == '-';
        if (negative)
            ++j;
        if (j >= name.size() || !std::isdigit(static_cast<unsigned char>(name[j])))
            continue;
        const int octave = name[j] - '0';
        ++j;
        if (j < name.size() && std::isalnum(static_cast<unsigned char>(name[j])))
            continue;
        return static_cast<double>(note + 12 * ((negative ? -octave : octave) + 1));
    }
    return std::nullopt;
}

audiofile::DecodedAudio GrainSource::chord(double seconds, double sampleRate)
{
    audiofile::DecodedAudio audio;
    audio.sampleRate = sampleRate;
    audio.rootKey = 45; // A2
    const auto frames = static_cast<size_t>(seconds * sampleRate);
    audio.channels.assign(2, std::vector<float>(frames, 0.0f));
    // A2, E3, A3, C#4, E4: each a few harmonics, the two sides a little apart in tuning, so the
    // chord beats slowly and moves across the field.
    constexpr double notes[] = {110.0, 164.81, 220.0, 277.18, 329.63};
    constexpr double twoPi = 6.283185307179586;
    for (size_t i = 0; i < frames; ++i)
    {
        const double t = static_cast<double>(i) / sampleRate;
        const double swell = 0.5 - 0.5 * std::cos(twoPi * t / seconds); // up and down over its length
        for (int c = 0; c < 2; ++c)
        {
            double x = 0.0;
            for (const double hz : notes)
                for (int h = 1; h <= 4; ++h)
                    x += std::sin(twoPi * hz * h * (1.0 + (c == 0 ? -0.0007 : 0.0007)) * t) / (h * h);
            audio.channels[static_cast<size_t>(c)][i] = static_cast<float>(
                0.08 * x * (0.25 + 0.75 * swell) * (1.0 + 0.3 * std::sin(twoPi * 0.4 * t + c)));
        }
    }
    return audio;
}

std::shared_ptr<const GrainSource> GrainSource::build(const audiofile::DecodedAudio& audio, std::string name,
                                                      std::string path, Options options)
{
    auto s = std::make_shared<GrainSource>();
    s->name = std::move(name);
    s->path = std::move(path);
    s->sampleRate = audio.sampleRate > 0.0 ? audio.sampleRate : 48000.0;
    s->numChannels = std::clamp(audio.numChannels(), 0, 2);
    const int longest = static_cast<int>(longestSeconds * s->sampleRate);
    s->frames = std::min(audio.numFrames(), longest);
    s->truncated = audio.numFrames() > longest;
    if (s->numChannels == 0 || s->frames == 0)
        return s;

    // Brought to full scale: a quiet file plays as loud as a loud one (Source n Gain is on top).
    std::vector<std::vector<float>> channels(static_cast<size_t>(s->numChannels));
    float peak = 0.0f;
    for (int c = 0; c < s->numChannels; ++c)
    {
        const auto& in = audio.channels[static_cast<size_t>(c)];
        channels[static_cast<size_t>(c)].assign(in.begin(), in.begin() + s->frames);
        for (const float x : channels[static_cast<size_t>(c)])
            peak = std::max(peak, std::abs(x));
    }
    s->normalise = peak > 1e-6f ? 1.0f / peak : 1.0f;
    for (auto& ch : channels)
        for (auto& x : ch)
            x *= s->normalise;

    // The overview and the level map.
    const int points = (s->frames + overviewBlock - 1) / overviewBlock;
    s->levelMap.assign(static_cast<size_t>(points), 0.0f);
    for (int c = 0; c < s->numChannels; ++c)
    {
        const auto& in = channels[static_cast<size_t>(c)];
        auto& lo = s->overviewMin[static_cast<size_t>(c)];
        auto& hi = s->overviewMax[static_cast<size_t>(c)];
        lo.assign(static_cast<size_t>(points), 0.0f);
        hi.assign(static_cast<size_t>(points), 0.0f);
        for (int p = 0; p < points; ++p)
        {
            const auto from = static_cast<size_t>(p * overviewBlock);
            const auto to = std::min(in.size(), from + overviewBlock);
            float mn = 0.0f, mx = 0.0f;
            double energy = 0.0;
            for (size_t i = from; i < to; ++i)
            {
                mn = std::min(mn, in[i]);
                mx = std::max(mx, in[i]);
                energy += static_cast<double>(in[i]) * in[i];
            }
            lo[static_cast<size_t>(p)] = mn;
            hi[static_cast<size_t>(p)] = mx;
            s->levelMap[static_cast<size_t>(p)] += static_cast<float>(
                energy / static_cast<double>(std::max<size_t>(1, to - from)) / s->numChannels);
        }
    }
    for (auto& v : s->levelMap)
        v = std::sqrt(v);

    // The pyramid: plain levels halved down; oversampled up to two minutes, unless compact.
    s->compact = options.compact || s->seconds() > oversampleSeconds;
    s->oversampled = !s->compact;
    std::vector<std::vector<float>> plain = channels;
    for (int k = 0; k < maxLevels; ++k)
    {
        const double scale = std::ldexp(1.0, -k) * (s->oversampled ? 2.0 : 1.0);
        if (s->oversampled)
        {
            std::vector<std::vector<float>> up(plain.size());
            for (size_t c = 0; c < plain.size(); ++c)
                up[c] = upsample(plain[c]);
            s->levels.push_back(interleave(up, false, scale));
        }
        else
            s->levels.push_back(interleave(plain, true, scale));
        if (plain[0].size() < 512)
            break;
        for (auto& c : plain)
            c = halve(c);
    }

    // What's found in it, on a mono mix at its own rate.
    std::vector<float> mono(static_cast<size_t>(s->frames), 0.0f);
    for (const auto& c : channels)
        for (size_t i = 0; i < mono.size(); ++i)
            mono[i] += c[i] / static_cast<float>(s->numChannels);
    for (int i = 1; i < s->frames; ++i)
        if (mono[static_cast<size_t>(i - 1)] < 0.0f && mono[static_cast<size_t>(i)] >= 0.0f)
            s->zeroCrossings.push_back(i);
    if (audio.rootKey)
    {
        s->root = static_cast<double>(*audio.rootKey);
        s->rootFound = true;
    }
    else if (const auto named = rootFromName(s->name))
    {
        s->root = named;
        s->rootFound = true;
    }
    if (options.analyse)
    {
        s->transients = dsp::findOnsets(mono, s->sampleRate, 0.5);
        if (const auto tempo = dsp::estimateTempo(mono, s->sampleRate))
            for (double t = tempo->firstBeat; t < s->seconds(); t += 60.0 / tempo->bpm)
                s->beats.push_back(t);
        if (!s->root)
            if ((s->root = rootFromSound(mono, s->sampleRate)))
                s->rootFound = true;
    }
    return s;
}

float GrainSource::levelAt(double frame) const noexcept
{
    if (levelMap.empty())
        return 0.0f;
    const auto i =
        static_cast<size_t>(std::clamp(frame / overviewBlock, 0.0, static_cast<double>(levelMap.size() - 1)));
    return levelMap[i];
}

size_t GrainSource::bytes() const noexcept
{
    size_t b = 0;
    for (const auto& l : levels)
        b += l.f32.size() * sizeof(float) + l.i16.size() * sizeof(std::int16_t);
    return b;
}

double GrainSource::nearest(std::span<const double> places, double at) noexcept
{
    if (places.empty())
        return at;
    const auto it = std::lower_bound(places.begin(), places.end(), at);
    if (it == places.begin())
        return *it;
    if (it == places.end())
        return *(it - 1);
    return at - *(it - 1) <= *it - at ? *(it - 1) : *it;
}

std::int32_t GrainSource::nearestZeroCrossing(double frame) const noexcept
{
    if (zeroCrossings.empty())
        return static_cast<std::int32_t>(frame);
    const auto at = static_cast<std::int32_t>(frame);
    const auto it = std::lower_bound(zeroCrossings.begin(), zeroCrossings.end(), at);
    if (it == zeroCrossings.begin())
        return *it;
    if (it == zeroCrossings.end())
        return *(it - 1);
    return at - *(it - 1) <= *it - at ? *(it - 1) : *it;
}

// ---- sets ----------------------------------------------------------------------------------------

const GrainSourceSet::Zone* GrainSourceSet::zoneFor(int key, int velocity) const noexcept
{
    const Zone* best = nullptr;
    int bestDistance = 1 << 30;
    for (const auto& z : zones)
    {
        if (z.audio == nullptr)
            continue;
        const int keyDistance = key < z.loKey ? z.loKey - key : key > z.hiKey ? key - z.hiKey : 0;
        const int velDistance = velocity < z.loVel   ? z.loVel - velocity
                                : velocity > z.hiVel ? velocity - z.hiVel
                                                     : 0;
        const int distance = keyDistance * 128 + velDistance;
        if (distance < bestDistance)
        {
            best = &z;
            bestDistance = distance;
        }
    }
    return best;
}

std::shared_ptr<const GrainSource> GrainSourceSet::shown() const noexcept
{
    const auto* z = zoneFor(60, 100);
    return z != nullptr ? z->audio : nullptr;
}

size_t GrainSourceSet::bytes() const noexcept
{
    size_t b = 0;
    for (const auto& z : zones)
        if (z.audio != nullptr)
            b += z.audio->bytes();
    return b;
}

std::shared_ptr<const GrainSourceSet> GrainSourceSet::one(std::shared_ptr<const GrainSource> audio,
                                                          std::string path)
{
    auto set = std::make_shared<GrainSourceSet>();
    set->name = audio != nullptr ? audio->name : std::string();
    set->path = std::move(path);
    Zone z;
    z.root = audio != nullptr && audio->root ? *audio->root : 60.0;
    z.audio = std::move(audio);
    set->zones.push_back(std::move(z));
    return set;
}

std::shared_ptr<const GrainSourceSet> GrainSourceSet::load(const std::string& stored, std::string* error)
{
    // One read a file, while any instance holds it.
    static std::mutex mutex;
    static std::map<std::string, std::weak_ptr<const GrainSourceSet>> cache;
    const auto file = library::resolve(stored);
    std::error_code ec;
    if (file.empty() || !std::filesystem::is_regular_file(file, ec))
    {
        if (error != nullptr)
            *error = library::isReference(stored)
                         ? library::missingMessage(stored)
                         : "Can't find " + std::filesystem::path(stored).filename().string();
        return nullptr;
    }
    const auto key = file.string();
    {
        const std::scoped_lock lock(mutex);
        if (auto held = cache[key].lock())
            return held;
    }
    auto set = std::make_shared<GrainSourceSet>();
    set->path = stored;
    set->name = file.stem().string();
    auto ext = file.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (ext == ".sfz")
    {
        const auto sfz = sfz::read(file, error);
        if (!sfz)
            return nullptr;
        set->multisample = true;
        // One sample each key range plays: the first in its round robin and random choice, the
        // velocity layer nearest a firm note (G13: a multisample's memory kept small).
        std::vector<const sfz::Region*> chosen;
        const auto distance = [](const sfz::Region& x)
        {
            return 100 < x.loVel ? x.loVel - 100 : 100 > x.hiVel ? 100 - x.hiVel : 0;
        };
        for (const auto& r : sfz->regions)
        {
            if (r.seqPosition != 1 || r.loRand > 0.0)
                continue;
            const auto same = std::find_if(chosen.begin(), chosen.end(), [&r](const sfz::Region* c)
                                           { return c->loKey == r.loKey && c->hiKey == r.hiKey; });
            if (same == chosen.end())
                chosen.push_back(&r);
            else if (distance(r) < distance(**same))
                *same = &r;
        }
        for (const auto* r : chosen)
        {
            auto audio = audiofile::decode(r->sample.string());
            if (!audio)
                continue;
            // Its part: from its offset to its end.
            const auto frames = static_cast<std::int64_t>(audio->numFrames());
            const auto from = static_cast<long>(std::clamp<std::int64_t>(r->offset, 0, frames));
            const auto to =
                static_cast<long>(r->end >= 0 ? std::clamp<std::int64_t>(r->end + 1, 0, frames) : frames);
            for (auto& c : audio->channels)
                c = std::vector<float>(c.begin() + from, c.begin() + std::max(from, to));
            Zone z;
            z.loKey = r->loKey;
            z.hiKey = r->hiKey; // every velocity: one layer was kept
            z.root = (r->rootFromSample && audio->rootKey ? *audio->rootKey : r->root) - r->transpose -
                     r->tune / 100.0;
            z.audio = GrainSource::build(*audio, r->sample.stem().string(), r->sample.string(),
                                         {.compact = true, .analyse = false});
            // Each zone's audio was brought to full scale: its level in the set is what the file
            // asks (its volume) over what that took.
            z.gain =
                static_cast<float>(std::pow(10.0, r->volume / 20.0) / std::max(1e-6f, z.audio->normalise));
            set->zones.push_back(std::move(z));
        }
        if (set->zones.empty())
        {
            if (error != nullptr)
                *error = "None of " + file.filename().string() + "'s samples could be read";
            return nullptr;
        }
        // The set brought to full scale as one: its loudest zone there, the rest as the file
        // balances them.
        float loudest = 0.0f;
        for (const auto& z : set->zones)
            loudest = std::max(loudest, z.gain);
        if (loudest > 0.0f)
            for (auto& z : set->zones)
                z.gain /= loudest;
    }
    else
    {
        const auto audio = audiofile::decode(key, error);
        if (!audio)
            return nullptr;
        auto built = GrainSource::build(*audio, set->name, stored);
        Zone z;
        z.root = built->root ? *built->root : 60.0;
        z.audio = std::move(built);
        set->zones.push_back(std::move(z));
    }
    const std::scoped_lock lock(mutex);
    cache[key] = set;
    return set;
}
} // namespace fd::devices
