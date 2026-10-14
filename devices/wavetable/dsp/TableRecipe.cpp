#include "TableRecipe.h"

#include "FactoryTables.h"
#include "Formula.h"
#include "TableFiles.h"
#include "fd/dsp/Math.h"
#include "fd/dsp/VoiceModules.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdint>

namespace fd::devices::wavetable
{
namespace
{
constexpr int N = Recipe::frameSize;
constexpr int H = N / 2 - 1; // the harmonics a frame keeps (the Nyquist bin is dropped)

using Frame = std::vector<float>;
using Spectrum = std::vector<std::complex<float>>; // harmonics 0 ... N / 2, a cycle is sum of 2 Re(c e^(...))

dsp::RealFft& fft()
{
    thread_local dsp::RealFft f(11);
    return f;
}

Spectrum toSpectrum(const Frame& x)
{
    Spectrum c(static_cast<size_t>(N / 2 + 1));
    fft().forward(x.data(), c.data());
    for (auto& v : c)
        v /= static_cast<float>(N);
    c[0] = {};
    c[static_cast<size_t>(N / 2)] = {};
    return c;
}

Frame fromSpectrum(Spectrum c)
{
    for (auto& v : c)
        v *= static_cast<float>(N);
    c[0] = {};
    c[static_cast<size_t>(N / 2)] = {};
    Frame x(static_cast<size_t>(N));
    fft().inverse(c.data(), x.data());
    return x;
}

/// Two spectra moved between at `t`: each harmonic's level and phase (the shorter way round).
Spectrum spectralMix(const Spectrum& a, const Spectrum& b, float t)
{
    Spectrum c(a.size());
    for (size_t h = 1; h < a.size(); ++h)
    {
        const float la = std::abs(a[h]), lb = std::abs(b[h]);
        const float pa = la > 1e-12f ? std::arg(a[h]) : (lb > 1e-12f ? std::arg(b[h]) : 0.0f);
        const float pb = lb > 1e-12f ? std::arg(b[h]) : pa;
        float d = pb - pa;
        d -= static_cast<float>(dsp::twoPi) * std::round(d / static_cast<float>(dsp::twoPi));
        c[h] = std::polar(la + t * (lb - la), pa + t * d);
    }
    return c;
}

Frame crossfade(const Frame& a, const Frame& b, float t)
{
    Frame c(a.size());
    for (size_t i = 0; i < a.size(); ++i)
        c[i] = a[i] + t * (b[i] - a[i]);
    return c;
}

Frame mix(const Frame& a, const Frame& b, float t, Recipe::Fill fill)
{
    if (t <= 0.0f)
        return a;
    if (t >= 1.0f)
        return b;
    return fill == Recipe::Fill::Spectral ? fromSpectrum(spectralMix(toSpectrum(a), toSpectrum(b), t))
                                          : crossfade(a, b, t);
}

} // namespace

double drawnAt(const std::vector<std::array<float, 3>>& points, double x)
{
    if (points.empty())
        return 0.0;
    if (x <= points.front()[0])
        return points.front()[1];
    if (x >= points.back()[0])
        return points.back()[1];
    for (size_t k = 0; k + 1 < points.size(); ++k)
        if (x >= points[k][0] && x <= points[k + 1][0])
        {
            const double span = std::max(1e-9, static_cast<double>(points[k + 1][0] - points[k][0]));
            const double bent = dsp::CurvedEnvelope::shape((x - points[k][0]) / span, points[k][2]);
            return points[k][1] + (points[k + 1][1] - points[k][1]) * bent;
        }
    return 0.0;
}

namespace
{
Frame drawnWave(const Recipe::Keyframe& k)
{
    Frame x(static_cast<size_t>(N));
    for (int i = 0; i < N; ++i)
        x[static_cast<size_t>(i)] = static_cast<float>(drawnAt(k.points, static_cast<double>(i) / N));
    return x;
}

/// Harmonic levels and phases: harmonic h is level sin(2 pi (h x + phase)).
Frame harmonicWave(const std::vector<float>& levels, const std::vector<float>& phases)
{
    Spectrum c(static_cast<size_t>(N / 2 + 1));
    for (size_t i = 0; i < levels.size() && static_cast<int>(i) < H; ++i)
    {
        const double phase = i < phases.size() ? phases[i] : 0.0;
        c[i + 1] = std::polar(0.5f * levels[i], static_cast<float>(dsp::twoPi * phase - dsp::pi / 2.0));
    }
    return fromSpectrum(std::move(c));
}

/// A source's frames (as they came) spread over the recipe's `count` frames, filled between.
std::vector<Frame> spread(const std::vector<Frame>& source, int count, Recipe::Fill fill)
{
    std::vector<Frame> out;
    out.reserve(static_cast<size_t>(count));
    const int m = static_cast<int>(source.size());
    for (int f = 0; f < count; ++f)
    {
        const double at = count > 1 && m > 1 ? static_cast<double>(f) / (count - 1) * (m - 1) : 0.0;
        const int i0 = static_cast<int>(at), i1 = std::min(i0 + 1, m - 1);
        out.push_back(mix(source[static_cast<size_t>(i0)], source[static_cast<size_t>(i1)],
                          static_cast<float>(at - i0), fill));
    }
    return out;
}

/// A keyframed layer's frames: each keyframe's wave, the frames between filled.
std::vector<Frame> keyframed(const Recipe::Layer& layer, int count)
{
    std::vector<std::pair<int, Frame>> keys;
    for (const auto& k : layer.keyframes)
        keys.emplace_back(std::clamp(k.frame, 0, count - 1), layer.kind == Recipe::SourceKind::Draw
                                                                 ? drawnWave(k)
                                                                 : harmonicWave(k.levels, k.phases));
    std::vector<Frame> out(static_cast<size_t>(count), Frame(static_cast<size_t>(N)));
    if (keys.empty())
        return out;
    std::sort(keys.begin(), keys.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    for (int f = 0; f < count; ++f)
    {
        size_t k = 0;
        while (k + 1 < keys.size() && keys[k + 1].first <= f)
            ++k;
        if (f <= keys.front().first || k + 1 >= keys.size())
            out[static_cast<size_t>(f)] = keys[f <= keys.front().first ? 0 : k].second;
        else
        {
            const auto& [f0, a] = keys[k];
            const auto& [f1, b] = keys[k + 1];
            out[static_cast<size_t>(f)] =
                mix(a, b, static_cast<float>(f - f0) / static_cast<float>(f1 - f0), layer.fill);
        }
    }
    return out;
}

std::vector<Frame> framesOf(const std::vector<float>& samples)
{
    std::vector<Frame> out;
    for (size_t at = 0; at + N <= samples.size(); at += N)
        out.emplace_back(samples.begin() + static_cast<std::ptrdiff_t>(at),
                         samples.begin() + static_cast<std::ptrdiff_t>(at + N));
    return out;
}

/// A layer's frames, `count` of them.
std::vector<Frame> layerFrames(const Recipe::Layer& layer, int count)
{
    using K = Recipe::SourceKind;
    switch (layer.kind)
    {
    case K::Draw:
    case K::Harmonics:
        return keyframed(layer, count);
    case K::Formula:
    {
        std::vector<Frame> out(static_cast<size_t>(count), Frame(static_cast<size_t>(N)));
        const auto f = Formula::parse(layer.formula);
        if (!f)
            return out;
        for (int k = 0; k < count; ++k)
        {
            const double t = count > 1 ? static_cast<double>(k) / (count - 1) : 0.0;
            for (int i = 0; i < N; ++i)
                out[static_cast<size_t>(k)][static_cast<size_t>(i)] =
                    static_cast<float>(f->evaluate(static_cast<double>(i) / N, t));
        }
        return out;
    }
    case K::Table:
        if (!layer.tableId.empty())
            if (const auto table = factoryTable(layer.tableId))
            {
                std::vector<Frame> source;
                for (int f = 0; f < table->frames(); ++f)
                    source.emplace_back(table->samples(f), table->samples(f) + N);
                return spread(source, count, layer.fill);
            }
        [[fallthrough]];
    case K::Audio:
    {
        const auto source = framesOf(layer.frames);
        return source.empty() ? std::vector<Frame>(static_cast<size_t>(count), Frame(static_cast<size_t>(N)))
                              : spread(source, count, layer.fill);
    }
    case K::Image:
    {
        // Rows of brightness (0–1): as the wave itself, or as the levels of the first 256 harmonics.
        auto rows = framesOf(layer.frames);
        if (rows.empty())
            return std::vector<Frame>(static_cast<size_t>(count), Frame(static_cast<size_t>(N)));
        for (auto& row : rows)
        {
            if (layer.imageAsHarmonics)
            {
                std::vector<float> levels(256), phases(256, 0.0f);
                for (size_t h = 0; h < levels.size(); ++h)
                {
                    const float b = row[h * static_cast<size_t>(N) / levels.size()];
                    levels[h] = b * b / static_cast<float>(h + 1);
                }
                row = harmonicWave(levels, phases);
            }
            else
                for (auto& v : row)
                    v = 2.0f * v - 1.0f;
        }
        return spread(rows, count, layer.fill);
    }
    }
    return {};
}

/// A gentle (12 dB a doubling) response at harmonic h around `cutoff`: low, high or band pass.
double response(int type, double h, double cutoff)
{
    const double r = h / std::max(cutoff, 1e-9);
    const double low = 1.0 / std::sqrt(1.0 + r * r * r * r);
    if (type == 0)
        return low;
    const double high = r * r / std::sqrt(1.0 + r * r * r * r);
    return type == 1 ? high : 2.0 * low * high;
}

void applyChange(const Recipe::Change& change, std::vector<Frame>& frames)
{
    using C = Recipe::ChangeKind;
    std::vector<std::complex<float>> scratch(static_cast<size_t>(N / 2 + 1));
    if (change.kind == C::Warp && change.warp == dsp::Warp::FrameSkew)
    {
        // Each harmonic read from another frame: a pass over the whole table.
        std::vector<Spectrum> spectra;
        for (const auto& f : frames)
            spectra.push_back(toSpectrum(f));
        const int count = static_cast<int>(frames.size());
        for (int f = 0; f < count; ++f)
        {
            Spectrum c(static_cast<size_t>(N / 2 + 1));
            const double position = count > 1 ? static_cast<double>(f) / (count - 1) : 0.0;
            for (int h = 1; h <= H; ++h)
            {
                const double at =
                    std::clamp(position + dsp::frameSkewOffset(h, H, change.amountAt(f)), 0.0, 1.0) *
                    (count - 1);
                const int i0 = static_cast<int>(at), i1 = std::min(i0 + 1, count - 1);
                const auto t = static_cast<float>(at - i0);
                const auto a = spectra[static_cast<size_t>(i0)][static_cast<size_t>(h)];
                const auto b = spectra[static_cast<size_t>(i1)][static_cast<size_t>(h)];
                c[static_cast<size_t>(h)] = a + t * (b - a);
            }
            frames[static_cast<size_t>(f)] = fromSpectrum(std::move(c));
        }
        return;
    }
    for (int f = 0; f < static_cast<int>(frames.size()); ++f)
    {
        auto& x = frames[static_cast<size_t>(f)];
        const float a = std::clamp(change.amountAt(f), 0.0f, 1.0f);
        if (a <= 0.0f)
            continue;
        switch (change.kind)
        {
        case C::Normalise:
        {
            float peak = 0.0f;
            for (const float v : x)
                peak = std::max(peak, std::abs(v));
            if (peak > 1e-9f)
                for (auto& v : x)
                    v *= 1.0f + a * (1.0f / peak - 1.0f);
            break;
        }
        case C::RemoveDc:
        {
            double mean = 0.0;
            for (const float v : x)
                mean += v;
            mean /= N;
            for (auto& v : x)
                v -= static_cast<float>(a * mean);
            break;
        }
        case C::PhaseAlign:
        {
            // The whole wave turned, so the fundamental starts at zero going up.
            auto c = toSpectrum(x);
            if (std::abs(c[1]) > 1e-9f)
            {
                const float turn = a * (static_cast<float>(-dsp::pi / 2.0) - std::arg(c[1]));
                for (int h = 1; h <= H; ++h)
                    c[static_cast<size_t>(h)] *= std::polar(1.0f, turn * static_cast<float>(h));
                x = fromSpectrum(std::move(c));
            }
            break;
        }
        case C::Smooth:
        case C::Filter:
        {
            auto c = toSpectrum(x);
            const int type = change.kind == C::Smooth ? 0 : change.filterType;
            // The cutoff from the top (or the bottom, for a high pass) towards the other end.
            const double cutoff = type == 0 ? std::pow(2048.0, 1.0 - a) : std::pow(1024.0, a);
            for (int h = 1; h <= H; ++h)
                c[static_cast<size_t>(h)] *= static_cast<float>(response(type, h, cutoff));
            x = fromSpectrum(std::move(c));
            break;
        }
        case C::Fold:
        case C::Mirror:
        {
            Frame y(x.size());
            dsp::applyCycleWarp(change.kind == C::Fold ? dsp::Warp::Fold : dsp::Warp::Mirror, a, x.data(), N,
                                y.data());
            x = std::move(y);
            break;
        }
        case C::Reverse:
        {
            Frame y(x.rbegin(), x.rend());
            x = crossfade(x, y, a);
            break;
        }
        case C::Warp:
        {
            auto c = toSpectrum(x);
            dsp::applySpectralWarp(change.warp, a, c.data(), H, 1, scratch.data());
            x = fromSpectrum(std::move(c));
            break;
        }
        }
    }
}

// ---- JSON names ------------------------------------------------------------------------------

constexpr const char* kSourceNames[] = {"draw", "harmonics", "formula", "audio", "image", "table"};
constexpr const char* kChangeNames[] = {"normalise", "remove-dc", "phase-align", "smooth", "fold",
                                        "filter",    "reverse",   "mirror",      "warp"};
constexpr const char* kFilterNames[] = {"low", "high", "band"};
constexpr const char* kWarpNames[] = {"formant-shift",  "harmonic-stretch",  "inharmonic-stretch",
                                      "smear",          "spectral-low-pass", "spectral-high-pass",
                                      "phase-disperse", "frame-skew",        "random-harmonics",
                                      "shepard"};

template <size_t Count> int indexOf(const char* const (&names)[Count], const std::string& name)
{
    for (size_t i = 0; i < Count; ++i)
        if (name == names[i])
            return static_cast<int>(i);
    return -1;
}

std::string framesToBase64(const std::vector<float>& frames)
{
    std::vector<unsigned char> bytes;
    bytes.reserve(frames.size() * 2);
    for (const float v : frames)
    {
        const auto s = static_cast<std::int16_t>(std::lround(std::clamp(v, -1.0f, 1.0f) * 32767.0f));
        bytes.push_back(static_cast<unsigned char>(s & 0xFF));
        bytes.push_back(static_cast<unsigned char>((s >> 8) & 0xFF));
    }
    return toBase64(bytes);
}

std::vector<float> framesFromBase64(const std::string& text)
{
    const auto bytes = fromBase64(text);
    std::vector<float> out(bytes.size() / 2);
    for (size_t i = 0; i < out.size(); ++i)
        out[i] =
            static_cast<float>(static_cast<std::int16_t>(bytes[2 * i] | (bytes[2 * i + 1] << 8))) / 32767.0f;
    return out;
}
} // namespace

float Recipe::Change::amountAt(int frame) const noexcept
{
    if (amounts.empty())
        return 1.0f;
    if (frame <= amounts.front().first)
        return amounts.front().second;
    if (frame >= amounts.back().first)
        return amounts.back().second;
    for (size_t k = 0; k + 1 < amounts.size(); ++k)
        if (frame >= amounts[k].first && frame <= amounts[k + 1].first)
        {
            const float t = static_cast<float>(frame - amounts[k].first) /
                            static_cast<float>(std::max(1, amounts[k + 1].first - amounts[k].first));
            return amounts[k].second + t * (amounts[k + 1].second - amounts[k].second);
        }
    return amounts.back().second;
}

dsp::Wavetable bake(const Recipe& recipe)
{
    const int count = std::clamp(recipe.frames, 1, Recipe::maxFrames);
    std::vector<Frame> frames(static_cast<size_t>(count), Frame(static_cast<size_t>(N)));
    for (const auto& layer : recipe.layers)
    {
        if (!layer.on || layer.level <= 0.0f)
            continue;
        const auto made = layerFrames(layer, count);
        for (int f = 0; f < count; ++f)
            for (int i = 0; i < N; ++i)
                frames[static_cast<size_t>(f)][static_cast<size_t>(i)] +=
                    layer.level * made[static_cast<size_t>(f)][static_cast<size_t>(i)];
    }
    for (const auto& change : recipe.changes)
        if (change.on)
            applyChange(change, frames);
    std::vector<float> samples;
    samples.reserve(static_cast<size_t>(count) * N);
    for (const auto& f : frames)
        samples.insert(samples.end(), f.begin(), f.end());
    auto table = dsp::Wavetable::fromCycles(samples.data(), N, count, true);
    table.name = recipe.name;
    return table;
}

Recipe recipeFromTable(const dsp::Wavetable& table, const std::string& factoryId)
{
    Recipe r;
    r.name = table.name;
    r.frames = table.frames();
    Recipe::Layer layer;
    layer.kind = Recipe::SourceKind::Table;
    if (!factoryId.empty() && factoryTable(factoryId) != nullptr)
        layer.tableId = factoryId;
    else
        for (int f = 0; f < table.frames(); ++f)
            layer.frames.insert(layer.frames.end(), table.samples(f), table.samples(f) + N);
    layer.note = table.name;
    r.layers.push_back(std::move(layer));
    return r;
}

Recipe::Layer newLayer(Recipe::SourceKind kind)
{
    using K = Recipe::SourceKind;
    Recipe::Layer layer;
    layer.kind = kind;
    switch (kind)
    {
    case K::Draw:
    {
        Recipe::Keyframe k;
        k.points = {{0.0f, 0.0f, 0.0f}, {0.25f, 1.0f, 0.0f}, {0.75f, -1.0f, 0.0f}, {1.0f, 0.0f, 0.0f}};
        layer.keyframes.push_back(std::move(k));
        break;
    }
    case K::Harmonics:
    {
        Recipe::Keyframe k;
        k.levels.assign(Recipe::numHarmonicBars, 0.0f);
        k.phases.assign(Recipe::numHarmonicBars, 0.0f);
        k.levels[0] = 1.0f;
        layer.keyframes.push_back(std::move(k));
        break;
    }
    case K::Formula:
        layer.formula = "sin(2*pi*x)";
        break;
    case K::Table:
        layer.tableId = std::string(defaultTableId);
        break;
    case K::Audio:
    case K::Image:
        break;
    }
    return layer;
}

Recipe::Keyframe newKeyframe(const Recipe::Layer& layer, int frame, int count)
{
    Recipe::Keyframe k;
    k.frame = frame;
    if (layer.kind == Recipe::SourceKind::Harmonics)
    {
        const auto made = layerFrames(layer, std::max(1, count));
        const auto c = toSpectrum(made[static_cast<size_t>(std::clamp(frame, 0, std::max(1, count) - 1))]);
        k.levels.resize(Recipe::numHarmonicBars);
        k.phases.resize(Recipe::numHarmonicBars);
        for (int h = 1; h <= Recipe::numHarmonicBars; ++h)
        {
            const auto v = c[static_cast<size_t>(h)];
            k.levels[static_cast<size_t>(h - 1)] = std::min(1.0f, 2.0f * std::abs(v));
            // harmonicWave's angle is 2 pi phase - pi / 2.
            const double phase = (std::arg(v) + dsp::pi / 2.0) / dsp::twoPi;
            k.phases[static_cast<size_t>(h - 1)] = static_cast<float>(phase - std::floor(phase));
        }
        return k;
    }
    // The drawing before it, or the first.
    const Recipe::Keyframe* from = nullptr;
    for (const auto& other : layer.keyframes)
        if (other.frame <= frame && (from == nullptr || other.frame > from->frame))
            from = &other;
    if (from == nullptr && !layer.keyframes.empty())
        from = &*std::min_element(layer.keyframes.begin(), layer.keyframes.end(),
                                  [](const auto& a, const auto& b) { return a.frame < b.frame; });
    if (from != nullptr)
        k.points = from->points;
    return k;
}

Recipe::Layer imageLayer(const float* brightness, int width, int height, bool asHarmonics,
                         const std::string& from)
{
    Recipe::Layer layer;
    layer.kind = Recipe::SourceKind::Image;
    layer.imageAsHarmonics = asHarmonics;
    layer.note = from;
    if (brightness == nullptr || width < 1 || height < 1)
        return layer;
    const int rows = std::min(height, Recipe::maxFrames);
    layer.frames.reserve(static_cast<size_t>(rows) * N);
    for (int r = 0; r < rows; ++r)
    {
        // Rows spread from the top to the bottom; each read across, between its pixels.
        const int y =
            rows > 1 ? static_cast<int>(std::lround(static_cast<double>(r) * (height - 1) / (rows - 1))) : 0;
        const float* row = brightness + static_cast<size_t>(y) * static_cast<size_t>(width);
        for (int i = 0; i < N; ++i)
        {
            const double at = std::clamp((i + 0.5) / N * width - 0.5, 0.0, width - 1.0);
            const int i0 = static_cast<int>(at), i1 = std::min(i0 + 1, width - 1);
            const double t = at - i0;
            layer.frames.push_back(
                std::clamp(static_cast<float>(row[i0] + t * (row[i1] - row[i0])), 0.0f, 1.0f));
        }
    }
    return layer;
}

std::string formulaError(const std::string& text)
{
    std::string error;
    return Formula::parse(text, &error) ? std::string() : error;
}

nlohmann::json toJson(const Recipe& r)
{
    nlohmann::json j;
    j["name"] = r.name;
    j["frames"] = r.frames;
    j["layers"] = nlohmann::json::array();
    for (const auto& l : r.layers)
    {
        nlohmann::json layer{{"kind", kSourceNames[static_cast<size_t>(l.kind)]},
                             {"on", l.on},
                             {"level", l.level},
                             {"fill", l.fill == Recipe::Fill::Spectral ? "spectral" : "crossfade"}};
        if (!l.keyframes.empty())
        {
            layer["keyframes"] = nlohmann::json::array();
            for (const auto& k : l.keyframes)
            {
                nlohmann::json key{{"frame", k.frame}};
                if (l.kind == Recipe::SourceKind::Draw)
                    key["points"] = k.points;
                else
                {
                    key["levels"] = k.levels;
                    key["phases"] = k.phases;
                }
                layer["keyframes"].push_back(std::move(key));
            }
        }
        if (l.kind == Recipe::SourceKind::Formula)
            layer["formula"] = l.formula;
        if (!l.tableId.empty())
            layer["table"] = l.tableId;
        if (!l.frames.empty())
            layer["data"] = framesToBase64(l.frames);
        if (l.kind == Recipe::SourceKind::Image)
            layer["asHarmonics"] = l.imageAsHarmonics;
        if (!l.note.empty())
            layer["note"] = l.note;
        j["layers"].push_back(std::move(layer));
    }
    j["changes"] = nlohmann::json::array();
    for (const auto& c : r.changes)
    {
        nlohmann::json change{{"kind", kChangeNames[static_cast<size_t>(c.kind)]}, {"on", c.on}};
        if (c.kind == Recipe::ChangeKind::Warp)
            change["warp"] =
                kWarpNames[static_cast<size_t>(c.warp) - static_cast<size_t>(dsp::Warp::FormantShift)];
        if (c.kind == Recipe::ChangeKind::Filter)
            change["filter"] = kFilterNames[std::clamp(c.filterType, 0, 2)];
        change["amounts"] = c.amounts;
        j["changes"].push_back(std::move(change));
    }
    return j;
}

std::optional<Recipe> recipeFromJson(const nlohmann::json& j, std::string* error)
{
    const auto fail = [error](const std::string& why) -> std::optional<Recipe>
    {
        if (error != nullptr)
            *error = why;
        return std::nullopt;
    };
    if (!j.is_object())
        return fail("a recipe is an object");
    Recipe r;
    r.name = j.value("name", std::string());
    r.frames = j.value("frames", 64);
    if (r.frames < 1 || r.frames > Recipe::maxFrames)
        return fail("frames must be 1 to " + std::to_string(Recipe::maxFrames));
    const auto layers = j.value("layers", nlohmann::json::array());
    if (!layers.is_array() || layers.size() > static_cast<size_t>(Recipe::maxLayers))
        return fail("up to " + std::to_string(Recipe::maxLayers) + " layers");
    for (const auto& lj : layers)
    {
        Recipe::Layer l;
        const int kind = indexOf(kSourceNames, lj.value("kind", std::string()));
        if (kind < 0)
            return fail("a layer's kind is draw, harmonics, formula, audio, image or table");
        l.kind = static_cast<Recipe::SourceKind>(kind);
        l.on = lj.value("on", true);
        l.level = std::clamp(lj.value("level", 1.0f), 0.0f, 1.0f);
        l.fill = lj.value("fill", std::string("crossfade")) == "spectral" ? Recipe::Fill::Spectral
                                                                          : Recipe::Fill::Crossfade;
        for (const auto& kj : lj.value("keyframes", nlohmann::json::array()))
        {
            Recipe::Keyframe k;
            k.frame = kj.value("frame", 0);
            if (k.frame < 0 || k.frame >= r.frames)
                return fail("a keyframe's frame is outside the table");
            for (const auto& p : kj.value("points", nlohmann::json::array()))
            {
                if (!p.is_array() || p.size() != 3)
                    return fail("a point is [x, y, bend]");
                k.points.push_back({std::clamp(p[0].get<float>(), 0.0f, 1.0f),
                                    std::clamp(p[1].get<float>(), -1.0f, 1.0f),
                                    std::clamp(p[2].get<float>(), -1.0f, 1.0f)});
            }
            std::sort(k.points.begin(), k.points.end(),
                      [](const auto& a, const auto& b) { return a[0] < b[0]; });
            k.levels = kj.value("levels", std::vector<float>{});
            k.phases = kj.value("phases", std::vector<float>{});
            if (k.levels.size() > static_cast<size_t>(Recipe::numHarmonicBars) ||
                k.phases.size() > static_cast<size_t>(Recipe::numHarmonicBars))
                return fail("up to " + std::to_string(Recipe::numHarmonicBars) + " harmonics");
            l.keyframes.push_back(std::move(k));
        }
        std::sort(l.keyframes.begin(), l.keyframes.end(),
                  [](const auto& a, const auto& b) { return a.frame < b.frame; });
        l.formula = lj.value("formula", std::string());
        if (l.kind == Recipe::SourceKind::Formula)
            if (const auto why = formulaError(l.formula); !why.empty())
                return fail("the formula: " + why);
        l.tableId = lj.value("table", std::string());
        if (!l.tableId.empty() && factoryTable(l.tableId) == nullptr)
            return fail("there's no factory table \"" + l.tableId + "\"");
        if (lj.contains("data"))
        {
            l.frames = framesFromBase64(lj.value("data", std::string()));
            if (l.frames.size() % static_cast<size_t>(Recipe::frameSize) != 0 ||
                l.frames.size() > static_cast<size_t>(Recipe::maxFrames) * Recipe::frameSize)
                return fail("a layer's data is whole frames of 2 048 samples, at most 256");
        }
        l.imageAsHarmonics = lj.value("asHarmonics", false);
        l.note = lj.value("note", std::string());
        r.layers.push_back(std::move(l));
    }
    for (const auto& cj : j.value("changes", nlohmann::json::array()))
    {
        Recipe::Change c;
        const int kind = indexOf(kChangeNames, cj.value("kind", std::string()));
        if (kind < 0)
            return fail("a change's kind isn't known");
        c.kind = static_cast<Recipe::ChangeKind>(kind);
        c.on = cj.value("on", true);
        if (c.kind == Recipe::ChangeKind::Warp)
        {
            const int w = indexOf(kWarpNames, cj.value("warp", std::string()));
            if (w < 0)
                return fail("a warp change names one of the spectral warps");
            c.warp = static_cast<dsp::Warp>(static_cast<int>(dsp::Warp::FormantShift) + w);
        }
        if (c.kind == Recipe::ChangeKind::Filter)
            c.filterType = std::max(0, indexOf(kFilterNames, cj.value("filter", std::string("low"))));
        for (const auto& a : cj.value("amounts", nlohmann::json::array()))
        {
            if (!a.is_array() || a.size() != 2)
                return fail("an amount is [frame, amount]");
            c.amounts.emplace_back(std::clamp(a[0].get<int>(), 0, r.frames - 1),
                                   std::clamp(a[1].get<float>(), 0.0f, 1.0f));
        }
        std::sort(c.amounts.begin(), c.amounts.end());
        r.changes.push_back(std::move(c));
    }
    return r;
}
} // namespace fd::devices::wavetable
