#include "fd/dsp/Spectrum.h"

#include <algorithm>
#include <cmath>

namespace fd::dsp
{
namespace spectrum
{
double bandHz(int band) noexcept
{
    return lowestHz * std::exp2(static_cast<double>(band) / bandsPerOctave);
}

double bandAt(double hz) noexcept
{
    return bandsPerOctave * std::log2(std::max(hz, 1e-6) / lowestHz);
}

float tiltDb(double hz, float slope) noexcept
{
    return slope * static_cast<float>(std::log2(std::max(hz, 1e-6) / 1000.0));
}
} // namespace spectrum

namespace
{
using spectrum::floorDb;
using spectrum::numBands;

constexpr double kReferenceBinHz = 48000.0 / 8192.0; // bands read as densities in a bin this wide
constexpr double kHalfBand = 1.0293022366434921;     // 2^(1/24): a band's edges around its centre

float toDb(double power) noexcept
{
    return power > 1e-20 ? std::max(floorDb, static_cast<float>(10.0 * std::log10(power))) : floorDb;
}
} // namespace

void SpectrumAnalyser::prepare(int order, double sampleRate)
{
    order_ = std::clamp(order, 11, 14);
    size_ = 1 << order_;
    hop_ = hopAsked_ > 0 ? std::min(hopAsked_, size_) : size_ / 4;
    sampleRate_ = sampleRate > 0.0 ? sampleRate : 48000.0;
    fft_.prepare(order_);
    left_.assign(static_cast<size_t>(size_), 0.0f);
    right_.assign(static_cast<size_t>(size_), 0.0f);
    frame_.assign(static_cast<size_t>(size_), 0.0f);
    mags_.assign(static_cast<size_t>(size_ / 2 + 1), 0.0f);
    power_.assign(mags_.size(), 0.0);
    now_.assign(mags_.size(), 0.0);
    other_.assign(mags_.size(), 0.0);
    reset();
}

void SpectrumAnalyser::setHop(int samples) noexcept
{
    hopAsked_ = std::max(0, samples);
    if (size_ > 0)
        hop_ = hopAsked_ > 0 ? std::min(hopAsked_, size_) : size_ / 4;
}

void SpectrumAnalyser::setChannel(SpectrumChannel channel) noexcept
{
    if (channel == channel_)
        return;
    channel_ = channel;
    frames_ = 0; // a different signal: start the average again
}

void SpectrumAnalyser::reset() noexcept
{
    std::fill(left_.begin(), left_.end(), 0.0f);
    std::fill(right_.begin(), right_.end(), 0.0f);
    std::fill(power_.begin(), power_.end(), 0.0);
    write_ = filled_ = sinceHop_ = 0;
    frames_ = 0;
}

bool SpectrumAnalyser::push(const float* left, const float* right, std::size_t n)
{
    if (size_ == 0 || left == nullptr)
        return false;
    bool transformed = false;
    const int mask = size_ - 1;
    for (std::size_t i = 0; i < n; ++i)
    {
        left_[static_cast<size_t>(write_)] = left[i];
        right_[static_cast<size_t>(write_)] = right != nullptr ? right[i] : left[i];
        write_ = (write_ + 1) & mask;
        filled_ = std::min(filled_ + 1, size_);
        if (++sinceHop_ >= hop_ && filled_ == size_)
        {
            sinceHop_ = 0;
            transform();
            transformed = true;
            if (onTransform)
                onTransform();
        }
    }
    return transformed;
}

void SpectrumAnalyser::binPower(const std::vector<float>& frame, std::vector<double>& out)
{
    fft_.magnitudes(frame.data(), mags_.data(), scratch_);
    for (size_t k = 0; k < mags_.size(); ++k)
        out[k] = static_cast<double>(mags_[k]) * mags_[k];
}

void SpectrumAnalyser::transform()
{
    const int mask = size_ - 1;
    auto gather = [&](auto sample)
    {
        for (int i = 0; i < size_; ++i)
        {
            const auto at = static_cast<size_t>((write_ + i) & mask); // oldest first
            frame_[static_cast<size_t>(i)] = sample(left_[at], right_[at]);
        }
    };
    switch (channel_)
    {
    case SpectrumChannel::Both:
        gather([](float l, float) { return l; });
        binPower(frame_, now_);
        gather([](float, float r) { return r; });
        binPower(frame_, other_);
        for (size_t k = 0; k < now_.size(); ++k)
            now_[k] = 0.5 * (now_[k] + other_[k]);
        break;
    case SpectrumChannel::Left:
        gather([](float l, float) { return l; });
        binPower(frame_, now_);
        break;
    case SpectrumChannel::Right:
        gather([](float, float r) { return r; });
        binPower(frame_, now_);
        break;
    case SpectrumChannel::Mid:
        gather([](float l, float r) { return 0.5f * (l + r); });
        binPower(frame_, now_);
        break;
    case SpectrumChannel::Side:
        gather([](float l, float r) { return 0.5f * (l - r); });
        binPower(frame_, now_);
        break;
    }

    ++frames_;
    if (frames_ == 1)
    {
        power_ = now_;
        return;
    }
    // Cumulative: the running mean. Otherwise exponential, with the averaging time per hop.
    const double hopSeconds = hop_ / sampleRate_;
    const double weight = cumulative_           ? 1.0 / static_cast<double>(frames_)
                          : averagingMs_ <= 0.0 ? 1.0
                                                : 1.0 - std::exp(-hopSeconds / (averagingMs_ * 0.001));
    for (size_t k = 0; k < power_.size(); ++k)
        power_[k] += weight * (now_[k] - power_[k]);
}

SpectrumCurve SpectrumAnalyser::bands() const
{
    return bandsOf(power_);
}

SpectrumCurve SpectrumAnalyser::currentBands() const
{
    return bandsOf(now_);
}

void SpectrumAnalyser::lineAt(std::span<const double> edgesHz, std::span<float> out) const
{
    const size_t spans = edgesHz.empty() ? 0 : edgesHz.size() - 1;
    for (size_t i = 0; i < std::min(spans, out.size()); ++i)
        out[i] = floorDb;
    if (frames_ == 0 || spans == 0)
        return;
    const double binHz = sampleRate_ / size_;
    const int lastBin = size_ / 2 - 1;
    for (size_t i = 0; i < std::min(spans, out.size()); ++i)
    {
        const double lo = edgesHz[i] / binHz, hi = edgesHz[i + 1] / binHz;
        if (edgesHz[i] >= sampleRate_ * 0.5)
            break;
        const int from = std::max(1, static_cast<int>(std::ceil(lo)));
        const int to = std::min(lastBin, static_cast<int>(std::ceil(hi)) - 1);
        double power = 0.0;
        if (to >= from) // the loudest bin in the span: a sine reads its own level
            for (int k = from; k <= to; ++k)
                power = std::max(power, now_[static_cast<size_t>(k)]);
        else // narrower than a bin: between the two around its middle
        {
            const double x = std::clamp(0.5 * (lo + hi), 1.0, static_cast<double>(lastBin));
            const auto k = static_cast<size_t>(x);
            const double t = x - static_cast<double>(k);
            power = now_[k] * (1.0 - t) + now_[std::min(k + 1, static_cast<size_t>(lastBin))] * t;
        }
        out[i] = toDb(power);
    }
}

SpectrumCurve SpectrumAnalyser::bandsOf(const std::vector<double>& source) const
{
    SpectrumCurve out;
    out.fill(floorDb);
    if (frames_ == 0)
        return out;
    const double binHz = sampleRate_ / size_;
    const double scale = kReferenceBinHz / binHz;
    const int lastBin = size_ / 2 - 1; // the Nyquist bin is left out
    for (int b = 0; b < numBands; ++b)
    {
        const double centre = spectrum::bandHz(b);
        if (centre * kHalfBand >= sampleRate_ * 0.5)
            break;
        // The bins whose centres fall in the band; a band narrower than a bin is interpolated.
        const int from = std::max(1, static_cast<int>(std::ceil(centre / kHalfBand / binHz)));
        const int to = std::min(lastBin, static_cast<int>(std::ceil(centre * kHalfBand / binHz)) - 1);
        double power = 0.0;
        if (to >= from)
        {
            for (int k = from; k <= to; ++k)
                power += source[static_cast<size_t>(k)];
            power /= to - from + 1;
        }
        else
        {
            const double x = std::clamp(centre / binHz, 1.0, static_cast<double>(lastBin));
            const auto k = static_cast<size_t>(x);
            const double t = x - static_cast<double>(k);
            power = source[k] * (1.0 - t) + source[std::min(k + 1, static_cast<size_t>(lastBin))] * t;
        }
        out[static_cast<size_t>(b)] = toDb(power * scale);
    }
    return out;
}

SpectrumAnalyser::Peak SpectrumAnalyser::peakNear(double hz) const
{
    if (frames_ == 0 || hz <= 0.0)
        return {};
    const double binHz = sampleRate_ / size_;
    const int lastBin = size_ / 2 - 1;
    const double reach = std::max(2.0, hz * (kHalfBand - 1.0) / binHz);
    const int from = std::clamp(static_cast<int>(std::floor(hz / binHz - reach)), 1, lastBin);
    const int to = std::clamp(static_cast<int>(std::ceil(hz / binHz + reach)), 1, lastBin);
    int best = from;
    for (int k = from; k <= to; ++k)
        if (power_[static_cast<size_t>(k)] > power_[static_cast<size_t>(best)])
            best = k;
    // A parabola through the loudest bin and its neighbours, in dB: the sine's frequency and level
    // between bins.
    const float mid = toDb(power_[static_cast<size_t>(best)]);
    if (best <= 1 || best >= lastBin)
        return {best * binHz, mid};
    const float before = toDb(power_[static_cast<size_t>(best - 1)]);
    const float after = toDb(power_[static_cast<size_t>(best + 1)]);
    const float curvature = before - 2.0f * mid + after;
    const float offset =
        curvature < 0.0f ? std::clamp(0.5f * (before - after) / curvature, -0.5f, 0.5f) : 0.0f;
    return {(best + offset) * binHz, mid - 0.25f * (before - after) * offset};
}

void SpectrumPeakHold::update(const SpectrumCurve& now, double elapsedSeconds, double holdSeconds) noexcept
{
    constexpr float kFallDbPerSecond = 20.0f;
    if (!any_)
    {
        held_ = now;
        age_.fill(0.0f);
        any_ = true;
        return;
    }
    const auto dt = static_cast<float>(elapsedSeconds);
    for (size_t b = 0; b < held_.size(); ++b)
    {
        if (now[b] >= held_[b])
        {
            held_[b] = now[b];
            age_[b] = 0.0f;
            continue;
        }
        age_[b] += dt;
        if (holdSeconds >= 0.0 && age_[b] > holdSeconds)
            held_[b] = std::max(now[b], held_[b] - kFallDbPerSecond * dt);
    }
}

std::array<bool, numBands> spectrumCollisions(const SpectrumCurve& a, const SpectrumCurve& b, float slope,
                                              float withinDb)
{
    std::array<bool, numBands> out{};
    const int from = static_cast<int>(std::ceil(spectrum::bandAt(30.0)));
    const int to = static_cast<int>(std::floor(spectrum::bandAt(16000.0)));
    auto tilted = [slope](const SpectrumCurve& c, int band)
    { return c[static_cast<size_t>(band)] + spectrum::tiltDb(spectrum::bandHz(band), slope); };
    // Silence is judged untilted: the tilt lifts the floor itself in the highs.
    float loudestA = floorDb, loudestB = floorDb, rawA = floorDb, rawB = floorDb;
    for (int i = from; i <= to; ++i)
    {
        loudestA = std::max(loudestA, tilted(a, i));
        loudestB = std::max(loudestB, tilted(b, i));
        rawA = std::max(rawA, a[static_cast<size_t>(i)]);
        rawB = std::max(rawB, b[static_cast<size_t>(i)]);
    }
    if (rawA <= floorDb || rawB <= floorDb)
        return out; // one of them has nothing
    for (int i = from; i <= to; ++i)
        out[static_cast<size_t>(i)] =
            tilted(a, i) >= loudestA - withinDb && tilted(b, i) >= loudestB - withinDb &&
            a[static_cast<size_t>(i)] > floorDb && b[static_cast<size_t>(i)] > floorDb;
    return out;
}

std::vector<int> spectrumPeaks(const SpectrumCurve& curve, float slope, int count, float floorDbShown)
{
    constexpr int kThird = spectrum::bandsPerOctave / 3; // a third of an octave, in bands
    constexpr float kStandsOut = 6.0f;
    SpectrumCurve t;
    for (int i = 0; i < numBands; ++i)
        t[static_cast<size_t>(i)] =
            curve[static_cast<size_t>(i)] + spectrum::tiltDb(spectrum::bandHz(i), slope);
    std::vector<int> candidates;
    for (int i = 1; i + 1 < numBands; ++i)
    {
        const float v = t[static_cast<size_t>(i)];
        if (curve[static_cast<size_t>(i)] <= floorDb || v <= floorDbShown)
            continue;
        if (v < t[static_cast<size_t>(i - 1)] || v <= t[static_cast<size_t>(i + 1)])
            continue; // not a local maximum (a plateau counts once, at its right end)
        float lowestLeft = v, lowestRight = v;
        for (int j = std::max(0, i - kThird); j < i; ++j)
            lowestLeft = std::min(lowestLeft, t[static_cast<size_t>(j)]);
        for (int j = i + 1; j <= std::min(numBands - 1, i + kThird); ++j)
            lowestRight = std::min(lowestRight, t[static_cast<size_t>(j)]);
        if (v - lowestLeft >= kStandsOut && v - lowestRight >= kStandsOut)
            candidates.push_back(i);
    }
    std::stable_sort(candidates.begin(), candidates.end(),
                     [&t](int x, int y) { return t[static_cast<size_t>(x)] > t[static_cast<size_t>(y)]; });
    std::vector<int> out;
    for (const int c : candidates)
    {
        if (static_cast<int>(out.size()) >= count)
            break;
        if (std::none_of(out.begin(), out.end(), [c](int o) { return std::abs(o - c) < kThird; }))
            out.push_back(c);
    }
    return out;
}

float spectrumMatchOffset(const SpectrumCurve& curve, const SpectrumCurve& target)
{
    const int from = static_cast<int>(std::ceil(spectrum::bandAt(50.0)));
    const int to = static_cast<int>(std::floor(spectrum::bandAt(15000.0)));
    double sum = 0.0;
    int n = 0;
    for (int i = from; i <= to; ++i)
    {
        const float c = curve[static_cast<size_t>(i)], g = target[static_cast<size_t>(i)];
        if (c <= floorDb || g <= floorDb)
            continue;
        sum += g - c;
        ++n;
    }
    return n > 0 ? static_cast<float>(sum / n) : 0.0f;
}
// ---- the spectrogram's history ------------------------------------------------------------

void SpectrogramHistory::prepare(int width, int rows, double secondsPerRow)
{
    width_ = std::max(0, width);
    rows_ = std::max(0, rows);
    secondsPerRow_ = std::max(1.0e-4, secondsPerRow);
    levels_.assign(static_cast<size_t>(width_) * static_cast<size_t>(rows_), floorDb);
    collides_.assign(levels_.size(), 0);
    clear();
}

void SpectrogramHistory::clear() noexcept
{
    std::fill(levels_.begin(), levels_.end(), floorDb);
    std::fill(collides_.begin(), collides_.end(), std::uint8_t{0});
    head_ = 0;
    filled_ = 0;
    rowTime_ = 0.0;
    started_ = 0;
    ++version_;
}

size_t SpectrogramHistory::slot(int age) const noexcept
{
    return (head_ + static_cast<size_t>(age)) % static_cast<size_t>(rows_);
}

void SpectrogramHistory::add(std::span<const float> levels, std::span<const std::uint8_t> collides,
                             double seconds)
{
    if (width_ == 0 || rows_ == 0)
        return;
    // Its time is up: the next row starts, the oldest one goes.
    if (filled_ == 0 || rowTime_ >= secondsPerRow_)
    {
        if (filled_ > 0)
            head_ = (head_ + static_cast<size_t>(rows_) - 1) % static_cast<size_t>(rows_);
        const auto row = head_ * static_cast<size_t>(width_);
        std::fill_n(levels_.begin() + static_cast<std::ptrdiff_t>(row), width_, floorDb);
        std::fill_n(collides_.begin() + static_cast<std::ptrdiff_t>(row), width_, std::uint8_t{0});
        filled_ = std::min(filled_ + 1, rows_);
        rowTime_ = 0.0;
        ++started_;
    }
    const auto row = head_ * static_cast<size_t>(width_);
    const auto n = std::min(levels.size(), static_cast<size_t>(width_));
    for (size_t i = 0; i < n; ++i)
        levels_[row + i] = std::max(levels_[row + i], levels[i]);
    for (size_t i = 0; i < std::min(collides.size(), static_cast<size_t>(width_)); ++i)
        collides_[row + i] |= collides[i];
    rowTime_ += seconds;
    ++version_;
}

std::span<const float> SpectrogramHistory::levels(int age) const
{
    if (age < 0 || age >= rows_)
        return {};
    return {levels_.data() + slot(age) * static_cast<size_t>(width_), static_cast<size_t>(width_)};
}

std::span<const std::uint8_t> SpectrogramHistory::collisions(int age) const
{
    if (age < 0 || age >= rows_)
        return {};
    return {collides_.data() + slot(age) * static_cast<size_t>(width_), static_cast<size_t>(width_)};
}
} // namespace fd::dsp
