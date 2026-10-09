#include "fd/dsp/Dynamics.h"

#include "fd/dsp/Math.h"

#include <algorithm>
#include <cmath>

namespace fd::dsp
{
float compressorCurve(float x, float threshold, float ratio, float knee) noexcept
{
    const float slope = 1.0f / std::max(1.0f, ratio); // infinity: 0, a limiter
    const float over = x - threshold;
    if (knee > 0.0f && std::abs(over) <= knee * 0.5f)
    {
        const float d = over + knee * 0.5f;
        return x + (slope - 1.0f) * d * d / (2.0f * knee);
    }
    return over <= 0.0f ? x : threshold + over * slope;
}

float expanderCurve(float x, float threshold, float ratio, float knee, float range) noexcept
{
    const float r = std::max(1.0f, ratio);
    const float under = x - threshold;
    float y = x;
    if (knee > 0.0f && std::abs(under) <= knee * 0.5f)
    {
        const float d = under - knee * 0.5f;
        y = x - (r - 1.0f) * d * d / (2.0f * knee);
    }
    else if (under < 0.0f)
        y = threshold + under * r;
    return std::max(y, x + std::min(0.0f, range));
}

namespace
{
double factor(double ms, double sampleRate, double stepsPerTimeConstant)
{
    if (!(ms > 0.0) || !(sampleRate > 0.0))
        return 0.0;
    const double tau = ms * 0.001 / stepsPerTimeConstant;
    return std::exp(-1.0 / (tau * sampleRate));
}
} // namespace

double attackCoefficient(double ms, double sampleRate) noexcept
{
    return factor(ms, sampleRate, std::log(9.0)); // 10 % -> 90 % takes ln 9 time constants
}

double releaseCoefficient(double ms, double sampleRate) noexcept
{
    return factor(ms, sampleRate, std::log(1000.0)); // to -60 dB takes ln 1000
}

// ---- GainSmoother ------------------------------------------------------------------------------

void GainSmoother::setCoefficients(double down, double up) noexcept
{
    down_ = down;
    up_ = up;
}

void GainSmoother::reset(float gainDb) noexcept
{
    y_ = std::max(gainDb, minusInfinityDb);
}

float GainSmoother::process(float targetDb) noexcept
{
    const double target = std::max(targetDb, minusInfinityDb);
    const double a = target < y_ ? down_ : up_;
    y_ = target + a * (y_ - target);
    return static_cast<float>(y_);
}

// ---- AutoReleaseSmoother ---------------------------------------------------------------------

void AutoReleaseSmoother::setCoefficients(double attack, double fastRelease, double slowRelease, double memory) noexcept
{
    attack_ = attack;
    fast_ = fastRelease;
    slow_ = slowRelease;
    memory_ = memory;
}

void AutoReleaseSmoother::reset(float gainDb) noexcept
{
    y_ = std::max(gainDb, minusInfinityDb);
    sustained_ = 0.0;
}

float AutoReleaseSmoother::process(float targetDb) noexcept
{
    constexpr double reducingBelowDb = -0.1;
    const double target = std::max(targetDb, minusInfinityDb);
    // How long reduction has lasted: builds up while reducing, fades with the slow release.
    const double reducing = y_ < reducingBelowDb ? 1.0 : 0.0;
    sustained_ = reducing + (reducing > 0.0 ? memory_ : slow_) * (sustained_ - reducing);
    const double release = fast_ + sustained_ * (slow_ - fast_);
    y_ = target + (target < y_ ? attack_ : release) * (y_ - target);
    return static_cast<float>(y_);
}

// ---- detection ---------------------------------------------------------------------------------

void LevelDetector::prepare(double sampleRate, double rmsWindowMs) noexcept
{
    rmsFactor_ = rmsWindowMs > 0.0 && sampleRate > 0.0 ? std::exp(-1.0 / (rmsWindowMs * 0.001 * sampleRate)) : 0.0;
    reset();
}

LevelDetector::Levels LevelDetector::process(float x) noexcept
{
    const double square = static_cast<double>(x) * x;
    meanSquare_ = square + rmsFactor_ * (meanSquare_ - square);
    return {std::abs(x), meanSquare_ > 1e-10 ? static_cast<float>(std::sqrt(meanSquare_)) : 0.0f};
}

float LevelDetector::processDb(float x) noexcept
{
    const auto levels = process(x);
    return gainToDb(mode_ == Mode::Peak ? levels.peak : levels.rms);
}

void linkLevels(float* levelsDb, int numChannels, float amount) noexcept
{
    if (numChannels < 2 || amount <= 0.0f)
        return;
    const float loudest = *std::max_element(levelsDb, levelsDb + numChannels);
    const float a = std::min(1.0f, amount);
    for (int c = 0; c < numChannels; ++c)
        levelsDb[c] += a * (loudest - levelsDb[c]);
}

// ---- lookahead ---------------------------------------------------------------------------------

void DelayLine::prepare(int maxDelaySamples)
{
    buffer_.assign(static_cast<size_t>(std::max(1, maxDelaySamples + 1)), 0.0f);
    delay_ = std::min(delay_, maxDelaySamples);
    reset();
}

void DelayLine::setDelay(int samples) noexcept
{
    delay_ = std::clamp(samples, 0, static_cast<int>(buffer_.size()) - 1);
    reset();
}

void DelayLine::reset() noexcept
{
    std::fill(buffer_.begin(), buffer_.end(), 0.0f);
    write_ = 0;
}

float DelayLine::process(float x) noexcept
{
    if (delay_ == 0)
        return x;
    const auto size = static_cast<int>(buffer_.size());
    buffer_[static_cast<size_t>(write_)] = x;
    int read = write_ - delay_;
    if (read < 0)
        read += size;
    write_ = write_ + 1 == size ? 0 : write_ + 1;
    return buffer_[static_cast<size_t>(read)];
}

void SlidingMinimum::prepare(int maxLength)
{
    ring_.assign(static_cast<size_t>(std::max(1, maxLength)), Entry{0.0f, 0});
    length_ = std::clamp(length_, 1, static_cast<int>(ring_.size()));
    reset();
}

void SlidingMinimum::setLength(int length) noexcept
{
    length_ = std::clamp(length, 1, static_cast<int>(ring_.size()));
    reset();
}

void SlidingMinimum::reset() noexcept
{
    head_ = count_ = 0;
    time_ = 0;
}

float SlidingMinimum::process(float x) noexcept
{
    // A monotonic queue of candidates in a ring: values increase from head to tail, and each
    // leaves once it is `length` samples old.
    const auto size = static_cast<int>(ring_.size());
    const auto at = [&](int i) -> Entry& { return ring_[static_cast<size_t>((head_ + i) % size)]; };
    while (count_ > 0 && at(0).time <= time_ - length_) // too old once this sample arrives
    {
        head_ = (head_ + 1) % size;
        --count_;
    }
    while (count_ > 0 && at(count_ - 1).value >= x)
        --count_;
    at(count_) = {x, time_};
    ++count_;
    ++time_;
    return at(0).value;
}

void MovingAverage::prepare(int maxLength)
{
    ring_.assign(static_cast<size_t>(std::max(1, maxLength)), 0.0);
    length_ = std::clamp(length_, 1, static_cast<int>(ring_.size()));
    reset();
}

void MovingAverage::setLength(int length, float value) noexcept
{
    length_ = std::clamp(length, 1, static_cast<int>(ring_.size()));
    reset(value);
}

void MovingAverage::reset(float value) noexcept
{
    std::fill(ring_.begin(), ring_.begin() + length_, static_cast<double>(value));
    sum_ = static_cast<double>(value) * length_;
    pos_ = 0;
}

float MovingAverage::process(float x) noexcept
{
    auto& slot = ring_[static_cast<size_t>(pos_)];
    sum_ += static_cast<double>(x) - slot;
    slot = x;
    pos_ = pos_ + 1 == length_ ? 0 : pos_ + 1;
    return static_cast<float>(sum_ / length_);
}
} // namespace fd::dsp
