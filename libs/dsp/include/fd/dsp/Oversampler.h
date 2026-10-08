#pragma once

#include <array>
#include <vector>

namespace fd::dsp
{
/// One linear-phase FIR half-band stage (2× up or down), Kaiser-windowed sinc.
/// Length N = 4k + 3, so every other tap except the centre is zero.
class HalfbandStage
{
public:
    void design(int numTaps, double attenuationDb);
    void reset() noexcept;

    /// 2× upsampling: `in` has n samples, `out` receives 2n.
    void upsample(const float* in, float* out, int n) noexcept;
    /// 2× downsampling: `in` has 2n samples, `out` receives n.
    void downsample(const float* in, float* out, int n) noexcept;

    [[nodiscard]] int numTaps() const noexcept { return static_cast<int>(taps_.size()); }
    [[nodiscard]] const std::vector<double>& taps() const noexcept { return taps_; }

private:
    std::vector<double> taps_;
    // Polyphase taps for upsampling: phase p uses taps[2j + p] on x[m - j].
    std::array<std::vector<std::pair<int, double>>, 2> upPhases_;
    std::vector<std::pair<int, double>> downTaps_; // non-zero taps (index, value)
    std::vector<float> upHistory_, downHistory_;
    int upPos_ = 0, downPos_ = 0, upMask_ = 0, downMask_ = 0;
};

/// Multi-stage oversampler (2×, 4×, 8×) with linear-phase FIR half-bands.
///
/// The reported latency is the same for every factor (shorter chains are padded), and is a
/// whole number of base-rate samples, so a dry path can be aligned exactly and hosts never see
/// latency change (latency policy, ADR 0016).
class Oversampler
{
public:
    static constexpr int maxFactor = 8;

    /// May allocate. Resets state.
    void prepare(int maxBlockSize, int numChannels);
    /// 2, 4 or 8. Resets the filter state (no allocation).
    void setFactor(int factor) noexcept;
    void reset() noexcept;

    [[nodiscard]] int factor() const noexcept { return factor_; }
    /// Constant round-trip latency in base-rate samples (same for every factor).
    [[nodiscard]] int latencySamples() const noexcept { return latency_; }

    /// Upsamples n samples per channel; returns the oversampled channel buffers (n × factor each).
    float* const* upsample(const float* const* in, int numChannels, int n) noexcept;
    /// Downsamples the oversampled buffers back into `out` (n samples per channel).
    void downsample(float* const* out, int numChannels, int n) noexcept;

private:
    struct Channel
    {
        std::array<HalfbandStage, 3> up, down;
        std::vector<float> fineDelay; // integer padding at the oversampled rate
        int fineDelayPos = 0;
        std::vector<float> baseDelay; // padding at the base rate, to the constant latency
        int baseDelayPos = 0;
    };

    std::vector<Channel> channels_;
    std::vector<std::vector<float>> bufA_, bufB_;
    std::vector<float*> outPtrs_;
    int factor_ = 4;
    int maxBlock_ = 0;
    int latency_ = 0;
    std::array<int, 4> fineDelayFor_{};  // indexed by stage count (1..3)
    std::array<int, 4> baseDelayFor_{};
};
} // namespace fd::dsp
