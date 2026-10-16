#include "fd/dsp/Onsets.h"

#include "fd/dsp/Fft.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <limits>
#include <numeric>

namespace fd::dsp
{
namespace
{
/// The FFT order whose frame lasts at least `seconds`.
int orderFor(double sampleRate, double seconds)
{
    int order = 6;
    while ((1 << order) < sampleRate * seconds && order < 16)
        ++order;
    return order;
}

/// Magnitude frames of a signal, frame k centred on sample k × hop (the signal padded with
/// silence), each bins wide; log-compressed (log(1 + 1000 m): a hit 30 dB down still shows) when
/// asked.
struct Spectrogram
{
    std::vector<float> data;
    int bins = 0, frames = 0, hop = 0, length = 0;
    [[nodiscard]] const float* row(int k) const
    {
        return data.data() + static_cast<size_t>(k) * static_cast<size_t>(bins);
    }
};

Spectrogram spectrogram(std::span<const float> x, double sampleRate, double frameSeconds, int hopDivisor,
                        bool log)
{
    Fft fft(orderFor(sampleRate, frameSeconds));
    const int n = fft.size();
    Spectrogram s;
    s.length = n;
    s.hop = n / hopDivisor;
    s.bins = n / 2 + 1;
    s.frames = static_cast<int>(x.size()) / s.hop + 1;
    s.data.resize(static_cast<size_t>(s.frames) * static_cast<size_t>(s.bins));
    std::vector<float> block(static_cast<size_t>(n)), mags(static_cast<size_t>(s.bins));
    std::vector<std::complex<float>> scratch;
    const auto size = static_cast<long>(x.size());
    for (int k = 0; k < s.frames; ++k)
    {
        const long first = static_cast<long>(k) * s.hop - n / 2;
        for (int i = 0; i < n; ++i)
        {
            const long j = first + i;
            block[static_cast<size_t>(i)] = j >= 0 && j < size ? x[static_cast<size_t>(j)] : 0.0f;
        }
        fft.magnitudes(block.data(), mags.data(), scratch);
        float* row = s.data.data() + static_cast<size_t>(k) * static_cast<size_t>(s.bins);
        for (int b = 0; b < s.bins; ++b)
            row[b] = log ? std::log1p(1000.0f * mags[static_cast<size_t>(b)]) : mags[static_cast<size_t>(b)];
    }
    return s;
}

/// Where an attack found near `centre` seconds starts, to the millisecond: where the signal's
/// energy (in 1 ms windows) leaves the level before it, 30 dB under the attack's peak at most.
double attackStart(std::span<const float> x, double sampleRate, double centre, double frameSeconds)
{
    const int window = std::max(8, static_cast<int>(std::lround(0.001 * sampleRate)));
    const int step = std::max(1, window / 4);
    const auto size = static_cast<long>(x.size());
    // Log magnitudes see an attack as soon as it enters a frame: the flux peaks up to a frame
    // before it.
    // Before the signal's start is silence, so a sound that starts at once is placed there.
    const long a =
        std::max(-2L * window, static_cast<long>((centre - 0.5 * frameSeconds) * sampleRate) - window);
    const long b = std::min(size, static_cast<long>((centre + frameSeconds) * sampleRate) + window);
    std::vector<double> level;
    for (long i = a; i + window <= b; i += step)
    {
        double e = 0.0;
        for (long j = std::max(0L, i); j < i + window; ++j)
            e += static_cast<double>(x[static_cast<size_t>(j)]) * x[static_cast<size_t>(j)];
        level.push_back(10.0 * std::log10(e / window + 1e-12));
    }
    if (level.size() < 6)
        return centre;
    // The steepest rise over a millisecond.
    const int span = std::max(1, window / step);
    int steepest = 0;
    double rise = -std::numeric_limits<double>::infinity();
    for (int j = 0; j + span < static_cast<int>(level.size()); ++j)
        if (const double r = level[static_cast<size_t>(j + span)] - level[static_cast<size_t>(j)]; r > rise)
        {
            rise = r;
            steepest = j;
        }
    // The level before it (its lowest in the 10 ms before), and the attack's peak (5 ms after).
    const int before = std::max(0, steepest - static_cast<int>(0.010 * sampleRate) / step);
    const int after = std::min(static_cast<int>(level.size()) - 1,
                               steepest + span + static_cast<int>(0.005 * sampleRate) / step);
    const double floor = *std::min_element(level.begin() + before, level.begin() + steepest + 1);
    const double peak = *std::max_element(level.begin() + steepest, level.begin() + after + 1);
    if (peak - floor < 6.0)
        return centre; // no attack to place
    const double threshold = std::max(floor + 6.0, peak - 30.0);
    int j = before;
    while (j < after && level[static_cast<size_t>(j)] <= threshold)
        ++j;
    // A window's level covers its whole millisecond: the attack is at its end.
    return static_cast<double>(a + static_cast<long>(j) * step + window) / sampleRate -
           0.25 * window / sampleRate;
}

/// Linear interpolation into a series (0 outside it).
double at(const std::vector<double>& v, double index)
{
    if (index < 0.0 || index > static_cast<double>(v.size() - 1))
        return 0.0;
    const auto i = static_cast<size_t>(index);
    if (i + 1 >= v.size())
        return v[i];
    const double f = index - static_cast<double>(i);
    return v[i] + f * (v[i + 1] - v[i]);
}

/// The onset envelope of the bins below `topHz` (all of them: 0): of log magnitudes, or of
/// magnitudes, where how loud a hit is matters.
OnsetEnvelope envelope(std::span<const float> mono, double sampleRate, double topHz, bool log = true)
{
    OnsetEnvelope e;
    if (mono.empty() || !(sampleRate > 0.0))
        return e;
    const auto s = spectrogram(mono, sampleRate, 0.023, 4, log);
    const int bins =
        topHz > 0.0 ? std::clamp(static_cast<int>(topHz / sampleRate * 2.0 * (s.bins - 1)) + 1, 2, s.bins)
                    : s.bins;
    e.hop = s.hop / sampleRate;
    e.flux.assign(static_cast<size_t>(s.frames), 0.0f);
    // Where a frame reaches past the signal's end, the end cuts it off: that's no start.
    const long ends = (static_cast<long>(mono.size()) - s.length / 2) / s.hop;
    for (int k = 0; k < s.frames && (k == 0 || k <= ends); ++k)
    {
        const float* now = s.row(k);
        const float* prev = k > 0 ? s.row(k - 1) : nullptr;
        float sum = 0.0f;
        for (int b = 0; b < bins; ++b)
            sum += std::max(0.0f, now[b] - (prev != nullptr ? prev[b] : 0.0f));
        e.flux[static_cast<size_t>(k)] = sum / static_cast<float>(bins);
    }
    return e;
}
} // namespace

OnsetEnvelope onsetEnvelope(std::span<const float> mono, double sampleRate)
{
    return envelope(mono, sampleRate, 0.0);
}

namespace
{
/// What a sensitivity asks of a candidate: how far above its surroundings, and how long after the
/// last one taken.
double marginFor(double sensitivity)
{
    return 0.01 + 0.3 * std::pow(1.0 - std::clamp(sensitivity, 0.0, 1.0), 1.5);
}
double gapFor(double sensitivity)
{
    return 0.02 + 0.04 * (1.0 - std::clamp(sensitivity, 0.0, 1.0));
}
} // namespace

std::vector<OnsetCandidate> onsetCandidates(std::span<const float> mono, double sampleRate)
{
    const auto env = onsetEnvelope(mono, sampleRate);
    const auto& f = env.flux;
    const int n = static_cast<int>(f.size());
    std::vector<OnsetCandidate> out;
    if (n < 3)
        return out;
    const float top = *std::max_element(f.begin(), f.end());
    if (!(top > 1e-6f))
        return out;
    // Above the local mean (±100 ms) by the least margin any sensitivity asks; the highest within
    // ±15 ms.
    const int half = std::max(1, static_cast<int>(std::lround(0.1 / env.hop)));
    const int peakHalf = std::max(1, static_cast<int>(std::lround(0.015 / env.hop)));
    const double least = marginFor(1.0);
    std::vector<double> sums(static_cast<size_t>(n) + 1, 0.0);
    for (int k = 0; k < n; ++k)
        sums[static_cast<size_t>(k) + 1] = sums[static_cast<size_t>(k)] + f[static_cast<size_t>(k)] / top;
    for (int k = 0; k < n; ++k)
    {
        const double v = f[static_cast<size_t>(k)] / top;
        const int a = std::max(0, k - half), b = std::min(n, k + half + 1);
        const double mean = (sums[static_cast<size_t>(b)] - sums[static_cast<size_t>(a)]) / (b - a);
        if (v < mean + least)
            continue;
        bool highest = true;
        for (int j = std::max(0, k - peakHalf); j <= std::min(n - 1, k + peakHalf) && highest; ++j)
            highest = j == k || f[static_cast<size_t>(j)] < f[static_cast<size_t>(k)] ||
                      (f[static_cast<size_t>(j)] == f[static_cast<size_t>(k)] && j > k);
        if (!highest)
            continue;
        const double time = std::max(0.0, attackStart(mono, sampleRate, k * env.hop, 4.0 * env.hop));
        const auto strength = static_cast<float>(v - mean);
        // Refined, two may land on the same attack: the stronger stands for both.
        if (!out.empty() && time - out.back().time < 0.005)
        {
            out.back().strength = std::max(out.back().strength, strength);
            continue;
        }
        out.push_back({time, strength});
    }
    return out;
}

std::vector<double> onsetsAt(const std::vector<OnsetCandidate>& candidates, double sensitivity)
{
    const double margin = marginFor(sensitivity), gap = gapFor(sensitivity);
    std::vector<double> out;
    for (const auto& c : candidates)
        if (c.strength >= margin && (out.empty() || c.time - out.back() >= gap))
            out.push_back(c.time);
    return out;
}

std::vector<double> findOnsets(std::span<const float> mono, double sampleRate, double sensitivity)
{
    return onsetsAt(onsetCandidates(mono, sampleRate), sensitivity);
}

std::optional<TempoEstimate> estimateTempo(std::span<const float> mono, double sampleRate, double minBpm,
                                           double maxBpm)
{
    // Two minutes say all a tempo can.
    const auto length = std::min(mono.size(), static_cast<size_t>(120.0 * sampleRate));
    const auto env = onsetEnvelope(mono.first(length), sampleRate);
    const double seconds = static_cast<double>(length) / sampleRate;
    if (seconds < 1.0 || env.flux.size() < 16 || !(maxBpm > minBpm))
        return std::nullopt;
    // What starts, against its surroundings (±250 ms), half-wave rectified; and the same blurred
    // over ±16 ms, so a tempo a little off still meets its beats.
    const int n = static_cast<int>(env.flux.size());
    const int half = std::max(1, static_cast<int>(std::lround(0.25 / env.hop)));
    std::vector<double> sums(static_cast<size_t>(n) + 1, 0.0), o(static_cast<size_t>(n), 0.0),
        blurred(static_cast<size_t>(n), 0.0);
    for (int k = 0; k < n; ++k)
        sums[static_cast<size_t>(k) + 1] = sums[static_cast<size_t>(k)] + env.flux[static_cast<size_t>(k)];
    for (int k = 0; k < n; ++k)
    {
        const int a = std::max(0, k - half), b = std::min(n, k + half + 1);
        o[static_cast<size_t>(k)] =
            std::max(0.0, env.flux[static_cast<size_t>(k)] -
                              (sums[static_cast<size_t>(b)] - sums[static_cast<size_t>(a)]) / (b - a));
    }
    const int blur = std::max(1, static_cast<int>(std::lround(0.016 / env.hop)));
    for (int k = 0; k < n; ++k)
    {
        double sum = 0.0, weights = 0.0;
        for (int j = -blur; j <= blur; ++j)
            if (k + j >= 0 && k + j < n)
            {
                const double w = blur + 1 - std::abs(j);
                sum += w * o[static_cast<size_t>(k + j)];
                weights += w;
            }
        blurred[static_cast<size_t>(k)] = sum / weights;
    }
    const double mean = std::accumulate(o.begin(), o.end(), 0.0) / n;
    if (!(mean > 0.0))
        return std::nullopt;
    // A tempo's score: at its best phase, what starts on its beats above the average (a beat
    // where nothing starts counts against it, so double tempo loses; a beat missed is lost, so
    // half tempo does).
    const auto score = [&](double bpm, double* phase)
    {
        const double period = 60.0 / bpm / env.hop;
        const int phases = std::max(8, static_cast<int>(std::ceil(period)));
        double best = -std::numeric_limits<double>::infinity();
        for (int p = 0; p < phases; ++p)
        {
            const double start = p * period / phases;
            double sum = 0.0;
            for (double t = start; t < n - 1; t += period)
                sum += at(blurred, t) - mean;
            if (sum > best)
            {
                best = sum;
                if (phase != nullptr)
                    *phase = start;
            }
        }
        return best / seconds;
    };
    // Candidates from how the onsets repeat (their autocorrelation, which no drift spoils), with
    // the tempos an octave or a fifth from each.
    const int lagMin = std::max(1, static_cast<int>(std::floor(60.0 / maxBpm / env.hop)));
    const int lagMax = std::min(n - 2, static_cast<int>(std::ceil(60.0 / minBpm / env.hop)));
    if (lagMax <= lagMin + 2)
        return std::nullopt;
    std::vector<double> ac(static_cast<size_t>(lagMax) + 2, 0.0);
    for (int lag = lagMin - 1; lag <= lagMax + 1; ++lag)
    {
        double sum = 0.0;
        for (int k = 0; k + lag < n; ++k)
            sum += o[static_cast<size_t>(k)] * o[static_cast<size_t>(k + lag)];
        ac[static_cast<size_t>(std::max(0, lag))] = sum / (n - lag);
    }
    std::vector<std::pair<double, double>> peaks; // (strength, bpm)
    for (int lag = lagMin; lag <= lagMax; ++lag)
    {
        const double y0 = ac[static_cast<size_t>(lag - 1)], y1 = ac[static_cast<size_t>(lag)],
                     y2 = ac[static_cast<size_t>(lag + 1)];
        if (y1 <= 0.0 || y1 < y0 || y1 < y2)
            continue;
        const double curve = y0 - 2.0 * y1 + y2;
        const double offset = curve < 0.0 ? 0.5 * (y0 - y2) / curve : 0.0;
        peaks.emplace_back(y1, 60.0 / ((lag + offset) * env.hop));
    }
    if (peaks.empty())
        return std::nullopt;
    std::sort(peaks.begin(), peaks.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
    peaks.resize(std::min<size_t>(peaks.size(), 4));
    double best = 0.0, bestScore = -std::numeric_limits<double>::infinity(), total = 0.0;
    int count = 0;
    for (const auto& peak : peaks)
        for (const double factor : {1.0, 2.0, 0.5, 1.5, 2.0 / 3.0})
        {
            const double around = peak.second * factor;
            if (around < minBpm * 0.99 || around > maxBpm * 1.01)
                continue;
            for (double bpm = around * 0.99; bpm <= around * 1.01; bpm += 0.1)
            {
                if (bpm < minBpm || bpm > maxBpm)
                    continue;
                const double sc = score(bpm, nullptr);
                total += sc;
                ++count;
                if (sc > bestScore)
                {
                    bestScore = sc;
                    best = bpm;
                }
            }
        }
    if (!(bestScore > 0.0) || count == 0)
        return std::nullopt;
    // Finer, twice over.
    for (const double step : {0.01, 0.002})
    {
        const double centre = best;
        for (double bpm = centre - 10.0 * step; bpm <= centre + 10.0 * step; bpm += step)
            if (bpm >= minBpm && bpm <= maxBpm)
                if (const double sc = score(bpm, nullptr); sc > bestScore)
                {
                    bestScore = sc;
                    best = bpm;
                }
    }
    // A loop is a whole number of bars: a tempo that makes it one, close by and nearly as good.
    if (seconds < 32.0)
    {
        double snapped = best, nearest = std::numeric_limits<double>::infinity();
        for (int bars = 1; bars <= 64; ++bars)
        {
            const double bpm = 240.0 * bars / seconds;
            if (bpm < minBpm || bpm > maxBpm || std::abs(bpm - best) / best > 0.015 ||
                std::abs(bpm - best) >= nearest)
                continue;
            if (score(bpm, nullptr) >= 0.75 * bestScore)
            {
                snapped = bpm;
                nearest = std::abs(bpm - best);
            }
        }
        best = snapped;
    }
    TempoEstimate t;
    t.bpm = best;
    double phase = 0.0;
    bestScore = score(best, &phase);
    const double period = 60.0 / best / env.hop;
    // The strongest a series is near a moment (±25 ms): the envelopes peak a little apart.
    const int near = std::max(1, static_cast<int>(std::lround(0.025 / env.hop)));
    const auto around = [&](const std::vector<double>& v, double k)
    {
        double out = 0.0;
        for (int j = -near; j <= near; ++j)
            out = std::max(out, at(v, k + j));
        return out;
    };
    // A tempo is a beat that repeats: a hit alone (a one-shot, a held tone's start) has none, and
    // two or three say less than a bar of them do.
    std::vector<double> onBeats;
    for (double k = phase; k < n - 1; k += period)
        onBeats.push_back(around(o, k));
    const double loudest = onBeats.empty() ? 0.0 : *std::max_element(onBeats.begin(), onBeats.end());
    const auto hits = std::count_if(onBeats.begin(), onBeats.end(),
                                    [loudest](double v) { return loudest > 0.0 && v > 0.25 * loudest; });
    if (hits < 2)
        return std::nullopt;
    // The bar's first beat: the one of four where the low end starts most, bar after bar (the
    // kick's beat, more than the snare's).
    const auto low = envelope(mono.first(length), sampleRate, 200.0, false);
    std::vector<double> lows(low.flux.begin(), low.flux.end());
    double strongest = -std::numeric_limits<double>::infinity(), downbeat = phase;
    for (int j = 0; j < 4; ++j)
    {
        double sum = 0.0;
        for (double k = phase + j * period; k < n - 1; k += 4.0 * period)
            sum += around(lows, k) + 0.25 * around(o, k);
        if (sum > strongest)
        {
            strongest = sum;
            downbeat = phase + j * period;
        }
    }
    // The first of each, from the start, at its attack.
    while (downbeat - 4.0 * period > -near)
        downbeat -= 4.0 * period;
    double beat = downbeat;
    while (beat - period > -near)
        beat -= period;
    const auto refined = [&](double k)
    {
        return std::max(
            0.0, attackStart(mono.first(length), sampleRate, std::max(0.0, k * env.hop), 4.0 * env.hop));
    };
    t.firstDownbeat = refined(downbeat);
    t.firstBeat = refined(beat);
    const double average = total / count;
    t.confidence = std::clamp(1.0 - std::max(0.0, average) / bestScore, 0.0, 1.0) *
                   std::min(1.0, static_cast<double>(hits - 1) / 3.0);
    return t;
}

double percussiveness(std::span<const float> mono, double sampleRate)
{
    const auto length = std::min(mono.size(), static_cast<size_t>(20.0 * sampleRate));
    if (length == 0 || !(sampleRate > 0.0))
        return 0.0;
    const auto s = spectrogram(mono.first(length), sampleRate, 0.04, 2, false);
    // Held tones are steady across time; hits are broadband in a frame. A median across time
    // keeps the first, across frequency the second.
    constexpr int kAcross = 6;
    double harmonic = 0.0, percussive = 0.0;
    std::vector<float> window;
    window.reserve(2 * kAcross + 1);
    const int bins =
        std::min(s.bins, static_cast<int>(std::lround(s.bins * std::min(1.0, 8000.0 / (0.5 * sampleRate)))));
    for (int k = 0; k < s.frames; ++k)
        for (int b = 1; b < bins; ++b)
        {
            window.clear();
            for (int j = std::max(0, k - kAcross); j <= std::min(s.frames - 1, k + kAcross); ++j)
                window.push_back(s.row(j)[b]);
            std::nth_element(window.begin(), window.begin() + static_cast<long>(window.size() / 2),
                             window.end());
            const double h = window[window.size() / 2];
            window.clear();
            for (int j = std::max(1, b - kAcross); j <= std::min(bins - 1, b + kAcross); ++j)
                window.push_back(s.row(k)[j]);
            std::nth_element(window.begin(), window.begin() + static_cast<long>(window.size() / 2),
                             window.end());
            const double p = window[window.size() / 2];
            const double m = s.row(k)[b];
            const double total = h * h + p * p;
            if (!(total > 0.0))
                continue;
            harmonic += m * m * (h * h / total);
            percussive += m * m * (p * p / total);
        }
    return harmonic + percussive > 0.0 ? percussive / (harmonic + percussive) : 0.0;
}
} // namespace fd::dsp
