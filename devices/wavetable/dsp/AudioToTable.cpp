#include "AudioToTable.h"

#include "fd/dsp/Math.h"
#include "fd/dsp/Wavetable.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <complex>

namespace fd::devices::wavetable
{
namespace
{
constexpr int N = dsp::Wavetable::frameSize;
constexpr double minPitch = 30.0, maxPitch = 2000.0;

/// The 4-term Blackman-Harris window (side lobes 92 dB down, its main lobe 4 bins either side).
double blackmanHarris(int n, int length)
{
    const double x = dsp::twoPi * n / std::max(1, length - 1);
    return 0.35875 - 0.48829 * std::cos(x) + 0.14128 * std::cos(2.0 * x) - 0.01168 * std::cos(3.0 * x);
}

/// Harmonic `h` of `pitch` in the windowed stretch of `length` samples from `start`: c, where the
/// stretch's harmonic is 2 Re(c e^(i 2 pi h pitch t)), t from the stretch's start.
std::complex<double> harmonicAt(const float* x, int start, int length, double sampleRate, double pitch, int h,
                                const std::vector<double>& window, double windowSum)
{
    const double step = -dsp::twoPi * h * pitch / sampleRate;
    const std::complex<double> rotate(std::cos(step), std::sin(step));
    std::complex<double> z(1.0, 0.0), sum;
    for (int n = 0; n < length; ++n)
    {
        sum += static_cast<double>(x[start + n]) * window[static_cast<size_t>(n)] * z;
        z *= rotate;
        if ((n & 255) == 255)
            z /= std::abs(z); // keep the phasor on the circle
    }
    return sum / windowSum;
}

/// YIN's period (in samples) for the stretch from `start`, or 0 if it isn't pitched.
double yinPeriod(const float* x, int length, int start, double sampleRate)
{
    const int minLag = static_cast<int>(sampleRate / maxPitch);
    const int maxLag = std::min(static_cast<int>(sampleRate / minPitch), (length - start) / 2);
    const int window = std::max(maxLag, 1024);
    if (maxLag <= minLag + 2 || start + window + maxLag > length)
        return 0.0;
    std::vector<double> d(static_cast<size_t>(maxLag) + 2, 0.0);
    for (int lag = 1; lag <= maxLag + 1; ++lag)
    {
        double sum = 0.0;
        for (int j = 0; j < window; ++j)
        {
            const double diff = static_cast<double>(x[start + j]) - x[start + j + lag];
            sum += diff * diff;
        }
        d[static_cast<size_t>(lag)] = sum;
    }
    // The cumulative mean normalised difference; the first dip under the threshold, to its bottom.
    std::vector<double> nd(d.size(), 1.0);
    double running = 0.0;
    for (size_t lag = 1; lag < d.size(); ++lag)
    {
        running += d[lag];
        nd[lag] = running > 0.0 ? d[lag] * static_cast<double>(lag) / running : 1.0;
    }
    int best = 0;
    for (int lag = minLag; lag <= maxLag; ++lag)
        if (nd[static_cast<size_t>(lag)] < 0.15)
        {
            while (lag + 1 <= maxLag && nd[static_cast<size_t>(lag) + 1] < nd[static_cast<size_t>(lag)])
                ++lag;
            best = lag;
            break;
        }
    if (best == 0)
        return 0.0;
    // Between samples: a parabola through the dip.
    const double a = nd[static_cast<size_t>(best) - 1], b = nd[static_cast<size_t>(best)],
                 c = nd[static_cast<size_t>(best) + 1];
    const double bend = a - 2.0 * b + c;
    return best + (bend > 1e-12 ? 0.5 * (a - c) / bend : 0.0);
}

/// `pitch` refined at `start` by how far the fundamental's phase moves over two periods.
double refinePitch(const float* x, int length, int start, double sampleRate, double pitch)
{
    for (int pass = 0; pass < 3; ++pass)
    {
        const double period = sampleRate / pitch;
        const int span = static_cast<int>(std::lround(4.0 * period));
        const int gap = static_cast<int>(std::lround(2.0 * period));
        if (start + gap + span > length)
            return pitch;
        std::vector<double> w(static_cast<size_t>(span));
        double sum = 0.0;
        for (int n = 0; n < span; ++n)
            sum += w[static_cast<size_t>(n)] = blackmanHarris(n, span);
        const auto first = harmonicAt(x, start, span, sampleRate, pitch, 1, w, sum);
        const auto second = harmonicAt(x, start + gap, span, sampleRate, pitch, 1, w, sum);
        if (std::abs(first) < 1e-9 || std::abs(second) < 1e-9)
            return pitch;
        // The fundamental's phase moves 2 pi f gap / rate from one stretch to the next: what it
        // moves beyond `pitch`'s share says how far off `pitch` is.
        double moved = std::arg(second) - std::arg(first) - dsp::twoPi * pitch * gap / sampleRate;
        moved -= dsp::twoPi * std::round(moved / dsp::twoPi);
        pitch += moved * sampleRate / (dsp::twoPi * gap);
    }
    return pitch;
}

std::vector<float> frameFrom(const std::vector<std::complex<double>>& c)
{
    // A cycle of N samples: sum over h of 2 Re(c[h] e^(i 2 pi h n / N)).
    std::vector<std::complex<float>> bins(static_cast<size_t>(N / 2 + 1));
    for (size_t h = 1; h < c.size() && h < static_cast<size_t>(N / 2); ++h)
        bins[h] = std::complex<float>(c[h]) * static_cast<float>(N);
    std::vector<float> out(static_cast<size_t>(N));
    dsp::RealFft(11).inverse(bins.data(), out.data());
    return out;
}
} // namespace

std::optional<double> findPitch(const float* audio, int length, double sampleRate)
{
    if (audio == nullptr || length <= 0 || sampleRate <= 0.0)
        return std::nullopt;
    double power = 0.0;
    for (int i = 0; i < length; ++i)
        power += static_cast<double>(audio[i]) * audio[i];
    if (power / length < 1e-8)
        return std::nullopt;
    std::vector<double> found;
    const int reach = static_cast<int>(2.2 * sampleRate / minPitch) + 1024;
    const int windows = 12;
    for (int k = 0; k < windows; ++k)
    {
        const int start =
            length > reach ? static_cast<int>(static_cast<double>(k) / (windows - 1) * (length - reach)) : 0;
        if (const double period = yinPeriod(audio, length, start, sampleRate); period > 0.0)
            found.push_back(refinePitch(audio, length, start, sampleRate, sampleRate / period));
        if (length <= reach)
            break;
    }
    if (found.empty())
        return std::nullopt;
    std::nth_element(found.begin(), found.begin() + static_cast<std::ptrdiff_t>(found.size() / 2),
                     found.end());
    return found[found.size() / 2];
}

std::optional<AudioFrames> framesFromAudio(const float* audio, int length, double sampleRate, AudioWay way,
                                           int count, std::string* error)
{
    const auto fail = [error](const std::string& why) -> std::optional<AudioFrames>
    {
        if (error != nullptr)
            *error = why;
        return std::nullopt;
    };
    count = std::clamp(count, 1, dsp::Wavetable::maxFrames);
    if (audio == nullptr || length <= 0)
        return fail("there's no audio");
    AudioFrames out;
    out.count = count;
    out.samples.reserve(static_cast<size_t>(count) * N);

    if (way == AudioWay::Slices)
    {
        for (int k = 0; k < count; ++k)
        {
            const int start =
                count > 1 && length > N
                    ? static_cast<int>(std::lround(static_cast<double>(k) / (count - 1) * (length - N)))
                    : 0;
            for (int i = 0; i < N; ++i)
                out.samples.push_back(start + i < length ? audio[start + i] : 0.0f);
        }
        return out;
    }

    const auto pitch = findPitch(audio, length, sampleRate);
    if (!pitch)
        return fail("no pitch was found: try Slices");
    out.pitch = *pitch;
    // Pitched: 6 periods a frame, levels and phases; Spectral: 16, levels alone.
    const int periods = way == AudioWay::Pitched ? 6 : 16;
    const int span = static_cast<int>(std::lround(periods * sampleRate / *pitch));
    if (span > length)
        return fail("the sound is shorter than " + std::to_string(periods) + " of its periods");
    std::vector<double> w(static_cast<size_t>(span));
    double windowSum = 0.0;
    for (int n = 0; n < span; ++n)
        windowSum += w[static_cast<size_t>(n)] = blackmanHarris(n, span);
    const int harmonics = std::min(N / 2 - 1, static_cast<int>(0.5 * sampleRate / *pitch * 0.98));
    for (int k = 0; k < count; ++k)
    {
        const int start =
            count > 1 ? static_cast<int>(std::lround(static_cast<double>(k) / (count - 1) * (length - span)))
                      : (length - span) / 2;
        // The pitch where this frame is taken (it may drift), then its harmonics there.
        const double here = refinePitch(audio, length, start, sampleRate, *pitch);
        std::vector<std::complex<double>> c(static_cast<size_t>(harmonics) + 1);
        for (int h = 1; h <= harmonics; ++h)
            c[static_cast<size_t>(h)] = harmonicAt(audio, start, span, sampleRate, here, h, w, windowSum);
        if (way == AudioWay::Pitched)
        {
            // The wave turned so its fundamental starts at zero going up, as every frame is.
            const double turn = -dsp::pi / 2.0 - std::arg(c[1]);
            for (int h = 1; h <= harmonics; ++h)
                c[static_cast<size_t>(h)] *= std::polar(1.0, turn * h);
        }
        else
            for (int h = 1; h <= harmonics; ++h)
                c[static_cast<size_t>(h)] = std::polar(std::abs(c[static_cast<size_t>(h)]), -dsp::pi / 2.0);
        const auto frame = frameFrom(c);
        out.samples.insert(out.samples.end(), frame.begin(), frame.end());
    }
    return out;
}

const char* wayName(AudioWay way) noexcept
{
    switch (way)
    {
    case AudioWay::Pitched:
        return "Pitched";
    case AudioWay::Spectral:
        return "Spectral";
    case AudioWay::Slices:
        return "Slices";
    }
    return "Pitched";
}

std::optional<AudioWay> wayFromName(const std::string& name) noexcept
{
    for (const auto way : {AudioWay::Pitched, AudioWay::Spectral, AudioWay::Slices})
    {
        std::string lower = wayName(way);
        std::transform(lower.begin(), lower.end(), lower.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (name == lower)
            return way;
    }
    return std::nullopt;
}

Recipe::Layer audioLayer(const AudioFrames& frames, AudioWay way, const std::string& from)
{
    Recipe::Layer layer;
    layer.kind = Recipe::SourceKind::Audio;
    // Frames taken by pitch move best by their harmonics; slices are as they are.
    layer.fill = way == AudioWay::Slices ? Recipe::Fill::Crossfade : Recipe::Fill::Spectral;
    layer.frames = frames.samples;
    layer.note = std::string(wayName(way)) + ", " + std::to_string(frames.count) +
                 (frames.count == 1 ? " frame" : " frames") + (from.empty() ? "" : " from " + from);
    return layer;
}

Recipe recipeFromAudio(const AudioFrames& frames, AudioWay way, const std::string& from)
{
    Recipe r;
    r.name = from.empty() ? "From audio" : from;
    r.frames = std::clamp(frames.count, 1, Recipe::maxFrames);
    r.layers.push_back(audioLayer(frames, way, from));
    return r;
}
} // namespace fd::devices::wavetable
