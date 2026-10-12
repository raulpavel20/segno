#include "fd/dsp/Loudness.h"

#include "fd/dsp/Math.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace fd::dsp
{
namespace
{
constexpr float kMinusInf = -std::numeric_limits<float>::infinity();
constexpr float kNone = std::numeric_limits<float>::quiet_NaN();

double lufs(double meanSquare) noexcept
{
    return meanSquare > 0.0 ? -0.691 + 10.0 * std::log10(meanSquare) : -std::numeric_limits<double>::infinity();
}
} // namespace

double lufsOf(double meanSquare) noexcept
{
    return lufs(meanSquare);
}

std::array<BiquadCoeffs, 2> kWeighting(double sampleRate) noexcept
{
    // The analogue prototypes behind BS.1770's 48 kHz coefficients, so other rates match.
    std::array<BiquadCoeffs, 2> out{};
    {
        const double f0 = 1681.974450955533, gainDb = 3.999843853973347, q = 0.7071752369554196;
        const double k = std::tan(pi * f0 / sampleRate);
        const double vh = std::pow(10.0, gainDb / 20.0);
        const double vb = std::pow(vh, 0.4996667741545416);
        const double a0 = 1.0 + k / q + k * k;
        out[0] = {(vh + vb * k / q + k * k) / a0, 2.0 * (k * k - vh) / a0, (vh - vb * k / q + k * k) / a0,
                  2.0 * (k * k - 1.0) / a0, (1.0 - k / q + k * k) / a0};
    }
    {
        const double f0 = 38.13547087602444, q = 0.5003270373238773;
        const double k = std::tan(pi * f0 / sampleRate);
        const double a0 = 1.0 + k / q + k * k;
        out[1] = {1.0, -2.0, 1.0, 2.0 * (k * k - 1.0) / a0, (1.0 - k / q + k * k) / a0};
    }
    return out;
}

// ---- LoudnessHistogram ---------------------------------------------------------------------------

void LoudnessHistogram::prepare()
{
    energy_.assign(bins, 0.0);
    count_.assign(bins, 0);
    total_ = 0;
}

void LoudnessHistogram::clear() noexcept
{
    std::fill(energy_.begin(), energy_.end(), 0.0);
    std::fill(count_.begin(), count_.end(), 0);
    total_ = 0;
}

int LoudnessHistogram::binOf(double loudness) noexcept
{
    return std::clamp(static_cast<int>((loudness - floor) / step), 0, bins - 1);
}

void LoudnessHistogram::add(double meanSquare) noexcept
{
    const double loudness = lufs(meanSquare);
    if (!(loudness > floor) || energy_.empty())
        return;
    const auto bin = static_cast<size_t>(binOf(loudness));
    energy_[bin] += meanSquare;
    ++count_[bin];
    ++total_;
}

void LoudnessHistogram::remove(double meanSquare) noexcept
{
    const double loudness = lufs(meanSquare);
    if (!(loudness > floor) || energy_.empty())
        return;
    const auto bin = static_cast<size_t>(binOf(loudness));
    if (count_[bin] == 0)
        return;
    // An emptied bin is zeroed, so rounding never leaves energy behind.
    energy_[bin] = --count_[bin] == 0 ? 0.0 : std::max(0.0, energy_[bin] - meanSquare);
    --total_;
}

float LoudnessHistogram::gatedLoudness(double relativeGate) const noexcept
{
    if (total_ == 0)
        return kNone;
    double energy = 0.0;
    for (const double e : energy_)
        energy += e;
    const double gate = lufs(energy / static_cast<double>(total_)) + relativeGate;
    energy = 0.0;
    long long count = 0;
    for (int b = 0; b < bins; ++b)
        if (centreOf(b) > gate)
        {
            energy += energy_[static_cast<size_t>(b)];
            count += count_[static_cast<size_t>(b)];
        }
    return count > 0 ? static_cast<float>(lufs(energy / static_cast<double>(count))) : kNone;
}

float LoudnessHistogram::range() const noexcept
{
    if (total_ == 0)
        return kNone;
    // Tech 3342: the relative gate 20 LU under the absolute-gated mean; then the 10th and 95th
    // percentiles of what's left, as its reference implementation picks them from sorted values.
    double energy = 0.0;
    for (const double e : energy_)
        energy += e;
    const double gate = lufs(energy / static_cast<double>(total_)) - 20.0;
    const int first = std::clamp(static_cast<int>(std::ceil((gate - floor) / step - 0.5)), 0, bins);
    long long n = 0;
    for (int b = first; b < bins; ++b)
        n += count_[static_cast<size_t>(b)];
    if (n == 0)
        return kNone;
    const auto rankOf = [n](double percent)
    { return static_cast<long long>(std::lround(static_cast<double>(n - 1) * percent / 100.0)) + 1; };
    const long long lowRank = rankOf(10.0), highRank = rankOf(95.0);
    double low = 0.0, high = 0.0;
    long long seen = 0;
    for (int b = first; b < bins; ++b)
    {
        const long long before = seen;
        seen += count_[static_cast<size_t>(b)];
        if (before < lowRank && seen >= lowRank)
            low = centreOf(b);
        if (before < highRank && seen >= highRank)
        {
            high = centreOf(b);
            break;
        }
    }
    return static_cast<float>(high - low);
}

float LoudnessHistogram::loudest() const noexcept
{
    if (total_ == 0)
        return kNone;
    for (int b = bins - 1; b >= 0; --b)
        if (count_[static_cast<size_t>(b)] > 0)
            return static_cast<float>(centreOf(b));
    return kNone;
}

// ---- LoudnessMeter -----------------------------------------------------------------------------

void LoudnessMeter::prepare(double sampleRate, int numChannels)
{
    numChannels_ = std::clamp(numChannels, 1, maxChannels);
    const auto coeffs = kWeighting(sampleRate);
    for (auto& channel : filters_)
        for (size_t i = 0; i < channel.size(); ++i)
            channel[i].setCoeffs(coeffs[i]);
    stepLength_ = std::max(1, static_cast<int>(std::lround(sampleRate * 0.1)));
    blocks_.prepare();
    windows_.prepare();
    reset();
}

void LoudnessMeter::reset() noexcept
{
    for (auto& channel : filters_)
        for (auto& f : channel)
            f.reset();
    stepPos_ = 0;
    stepSum_ = 0.0;
    steps_.fill(0.0);
    stepsDone_ = stepWrite_ = 0;
    maxMomentary_ = maxShortTerm_ = kMinusInf;
    blocks_.clear();
    windows_.clear();
}

void LoudnessMeter::process(const float* const* channels, int numSamples) noexcept
{
    for (int i = 0; i < numSamples; ++i)
    {
        double sum = 0.0;
        for (int c = 0; c < numChannels_; ++c)
        {
            auto& f = filters_[static_cast<size_t>(c)];
            const double y = f[1].process(f[0].process(channels[c][i]));
            sum += y * y;
        }
        stepSum_ += sum;
        if (++stepPos_ == stepLength_)
            finishStep();
    }
}

void LoudnessMeter::finishStep() noexcept
{
    steps_[static_cast<size_t>(stepWrite_)] = stepSum_ / stepLength_;
    stepWrite_ = (stepWrite_ + 1) % blocksShortTerm;
    ++stepsDone_;
    stepSum_ = 0.0;
    stepPos_ = 0;

    if (stepsDone_ >= blocksMomentary)
    {
        // A 400 ms gating block (75 % overlap): kept by loudness for the integrated value.
        const double block = meanOfLast(blocksMomentary);
        maxMomentary_ = std::max(maxMomentary_, static_cast<float>(lufs(block)));
        blocks_.add(block);
    }
    if (stepsDone_ >= blocksShortTerm)
    {
        // A short-term value every step (10 Hz, as Tech 3342 asks): kept for the loudness range.
        const double window = meanOfLast(blocksShortTerm);
        maxShortTerm_ = std::max(maxShortTerm_, static_cast<float>(lufs(window)));
        windows_.add(window);
    }
}

double LoudnessMeter::meanOfLast(int steps) const noexcept
{
    double sum = 0.0;
    for (int i = 1; i <= steps; ++i)
        sum += steps_[static_cast<size_t>((stepWrite_ - i + blocksShortTerm) % blocksShortTerm)];
    return sum / steps;
}

float LoudnessMeter::momentary() const noexcept
{
    return stepsDone_ >= blocksMomentary ? static_cast<float>(lufs(meanOfLast(blocksMomentary))) : kMinusInf;
}

float LoudnessMeter::shortTerm() const noexcept
{
    return stepsDone_ >= blocksShortTerm ? static_cast<float>(lufs(meanOfLast(blocksShortTerm))) : kMinusInf;
}

float LoudnessMeter::integrated() const noexcept
{
    // Absolute gate (-70 LUFS): every block in the histogram. Relative gate: 10 LU below their
    // mean loudness.
    const float value = blocks_.gatedLoudness(-10.0);
    return std::isnan(value) ? kMinusInf : value;
}

// ---- LoudnessStore -------------------------------------------------------------------------------

namespace
{
/// NaN-aware: a beats b (NaN never beats anything; anything beats NaN).
bool larger(float a, float b) noexcept
{
    return !std::isnan(a) && (std::isnan(b) || a > b);
}
} // namespace

void LoudnessStore::Largest::update(float old, float now, bool replaced) noexcept
{
    if (!larger(value, now)) // now is at least as large
        value = std::isnan(now) ? value : now;
    else if (replaced && !larger(value, old)) // the largest got smaller
        stale = true;
}

float LoudnessStore::Largest::max(const std::atomic<float>* values, int extent) const noexcept
{
    if (stale)
    {
        float found = NAN;
        for (int i = 0; i < extent; ++i)
            if (const float v = values[i].load(std::memory_order_relaxed); larger(v, found))
                found = v;
        value = found;
        stale = false;
    }
    return value;
}

void LoudnessStore::prepare(int steps)
{
    capacity_ = std::max(0, steps);
    const auto make = [this](float initial)
    {
        auto array = std::make_unique<std::atomic<float>[]>(static_cast<size_t>(capacity_));
        for (int i = 0; i < capacity_; ++i)
            array[static_cast<size_t>(i)].store(initial, std::memory_order_relaxed);
        return array;
    };
    meanSquare_ = make(-1.0f);
    truePeak_ = make(0.0f);
    loudestMomentary_ = make(NAN);
    loudestShortTerm_ = make(NAN);
    blocks_.prepare();
    windows_.prepare();
    heard_ = 0;
    extent_.store(0, std::memory_order_relaxed);
    peaks_.reset(0.0f);
    momentaryMax_.reset(NAN);
    shortTermMax_.reset(NAN);
}

void LoudnessStore::clear() noexcept
{
    // Only as far as anything was heard: a song's worth, not the whole room.
    for (int i = 0; i < extent(); ++i)
    {
        const auto k = static_cast<size_t>(i);
        meanSquare_[k].store(-1.0f, std::memory_order_relaxed);
        truePeak_[k].store(0.0f, std::memory_order_relaxed);
        loudestMomentary_[k].store(NAN, std::memory_order_relaxed);
        loudestShortTerm_[k].store(NAN, std::memory_order_relaxed);
    }
    blocks_.clear();
    windows_.clear();
    heard_ = 0;
    extent_.store(0, std::memory_order_relaxed);
    peaks_.reset(0.0f);
    momentaryMax_.reset(NAN);
    shortTermMax_.reset(NAN);
}

bool LoudnessStore::heard(int index) const noexcept
{
    return meanSquareAt(index) >= 0.0f;
}

float LoudnessStore::meanSquareAt(int index) const noexcept
{
    if (index < 0 || index >= capacity_)
        return -1.0f;
    return meanSquare_[static_cast<size_t>(index)].load(std::memory_order_relaxed);
}

float LoudnessStore::truePeakAt(int index) const noexcept
{
    if (index < 0 || index >= capacity_)
        return 0.0f;
    return truePeak_[static_cast<size_t>(index)].load(std::memory_order_relaxed);
}

float LoudnessStore::loudestMomentaryAt(int index) const noexcept
{
    if (index < 0 || index >= capacity_)
        return NAN;
    return loudestMomentary_[static_cast<size_t>(index)].load(std::memory_order_relaxed);
}

float LoudnessStore::loudestShortTermAt(int index) const noexcept
{
    if (index < 0 || index >= capacity_)
        return NAN;
    return loudestShortTerm_[static_cast<size_t>(index)].load(std::memory_order_relaxed);
}

float LoudnessStore::maxMomentary() const noexcept
{
    const float coarse = blocks_.loudest(), fine = momentaryMax_.max(loudestMomentary_.get(), extent());
    return larger(fine, coarse) ? fine : coarse;
}

float LoudnessStore::maxShortTerm() const noexcept
{
    const float coarse = windows_.loudest(), fine = shortTermMax_.max(loudestShortTerm_.get(), extent());
    return larger(fine, coarse) ? fine : coarse;
}

double LoudnessStore::meanEndingAt(int end, int length) const noexcept
{
    if (end - length + 1 < 0 || end >= capacity_)
        return -1.0;
    double sum = 0.0;
    for (int i = end - length + 1; i <= end; ++i)
    {
        const float m = meanSquare_[static_cast<size_t>(i)].load(std::memory_order_relaxed);
        if (m < 0.0f)
            return -1.0;
        sum += m;
    }
    return sum / length;
}

float LoudnessStore::loudnessEndingAt(int end, int length) const noexcept
{
    const double mean = meanEndingAt(end, length);
    return mean < 0.0 ? kNone : static_cast<float>(lufs(mean));
}

void LoudnessStore::account(int index, bool add) noexcept
{
    // The blocks and windows that end at or after this step and still contain it.
    for (int end = index; end < index + stepsMomentary; ++end)
        if (const double mean = meanEndingAt(end, stepsMomentary); mean >= 0.0)
            add ? blocks_.add(mean) : blocks_.remove(mean);
    for (int end = index; end < index + stepsShortTerm; ++end)
        if (const double mean = meanEndingAt(end, stepsShortTerm); mean >= 0.0)
            add ? windows_.add(mean) : windows_.remove(mean);
}

void LoudnessStore::set(int index, double meanSquare, float truePeak, float loudestMomentary,
                        float loudestShortTerm) noexcept
{
    if (index < 0 || index >= capacity_)
        return;
    const auto i = static_cast<size_t>(index);
    const bool was = meanSquare_[i].load(std::memory_order_relaxed) >= 0.0f;
    if (was)
        account(index, false);
    meanSquare_[i].store(static_cast<float>(std::max(0.0, meanSquare)), std::memory_order_relaxed);
    account(index, true);
    const auto replace = [was](std::atomic<float>& slot, Largest& largest, float now)
    {
        const float old = slot.load(std::memory_order_relaxed);
        slot.store(now, std::memory_order_relaxed);
        largest.update(old, now, was);
    };
    replace(truePeak_[i], peaks_, truePeak);
    replace(loudestMomentary_[i], momentaryMax_, loudestMomentary);
    replace(loudestShortTerm_[i], shortTermMax_, loudestShortTerm);
    if (!was)
        ++heard_;
    if (index >= extent())
        extent_.store(index + 1, std::memory_order_relaxed);
}

// ---- TruePeakDetector ----------------------------------------------------------------------------

namespace
{
double besselI0(double x) noexcept
{
    double sum = 1.0, term = 1.0;
    for (int k = 1; k < 32; ++k)
    {
        term *= (x / (2.0 * k)) * (x / (2.0 * k));
        sum += term;
    }
    return sum;
}

/// The interpolation filter, phase by phase: a Kaiser-windowed sinc cutting at the input's
/// Nyquist frequency, each phase normalised to unity gain at DC.
struct Phases
{
    std::array<std::array<float, TruePeakDetector::tapsPerPhase>, TruePeakDetector::factor> taps{};

    Phases()
    {
        constexpr int f = TruePeakDetector::factor, n = TruePeakDetector::tapsPerPhase, length = f * n;
        constexpr double beta = 8.0;
        const double centre = (length - 1) / 2.0;
        for (int p = 0; p < f; ++p)
        {
            double sum = 0.0;
            std::array<double, n> h{};
            for (int j = 0; j < n; ++j)
            {
                const int k = p + j * f;
                const double t = (k - centre) / f;
                const double sinc = std::abs(t) < 1e-12 ? 1.0 : std::sin(pi * t) / (pi * t);
                const double w = (k - centre) / centre;
                h[static_cast<size_t>(j)] = sinc * besselI0(beta * std::sqrt(std::max(0.0, 1.0 - w * w))) / besselI0(beta);
                sum += h[static_cast<size_t>(j)];
            }
            for (int j = 0; j < n; ++j)
                taps[static_cast<size_t>(p)][static_cast<size_t>(j)] = static_cast<float>(h[static_cast<size_t>(j)] / sum);
        }
    }
};

const Phases& phases()
{
    static const Phases p;
    return p;
}
} // namespace

void TruePeakDetector::prepare(int numChannels)
{
    (void)phases(); // the table, before the audio thread needs it
    history_.assign(static_cast<size_t>(std::clamp(numChannels, 1, maxChannels)), {});
    pos_.assign(history_.size(), 0);
    reset();
}

void TruePeakDetector::reset() noexcept
{
    for (auto& h : history_)
        h.fill(0.0f);
    std::fill(pos_.begin(), pos_.end(), 0);
    peak_ = 0.0f;
}

float TruePeakDetector::processSample(int channel, float x) noexcept
{
    auto& h = history_[static_cast<size_t>(channel)];
    auto& pos = pos_[static_cast<size_t>(channel)];
    pos = pos == 0 ? tapsPerPhase - 1 : pos - 1;
    h[static_cast<size_t>(pos)] = x; // h[pos + j] is x[n - j]
    const auto& table = phases().taps;
    float largest = 0.0f;
    for (const auto& taps : table)
    {
        float y = 0.0f;
        for (int j = 0; j < tapsPerPhase; ++j)
            y += taps[static_cast<size_t>(j)] * h[static_cast<size_t>((pos + j) % tapsPerPhase)];
        largest = std::max(largest, std::abs(y));
    }
    return largest;
}

void TruePeakDetector::process(const float* const* channels, int numChannels, int numSamples) noexcept
{
    const int n = std::min(numChannels, static_cast<int>(history_.size()));
    for (int c = 0; c < n; ++c)
        for (int i = 0; i < numSamples; ++i)
            peak_ = std::max(peak_, processSample(c, channels[c][i]));
}
} // namespace fd::dsp
