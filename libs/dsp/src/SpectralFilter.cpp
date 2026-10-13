#include "fd/dsp/SpectralFilter.h"

#include "fd/dsp/Math.h"

#include <algorithm>
#include <cmath>

namespace fd::dsp
{
namespace
{
/// In place: the inverse transform, through the forward one (conjugate in and out, scale).
void inverse(const Fft& fft, std::complex<float>* data) noexcept
{
    const int n = fft.size();
    for (int i = 0; i < n; ++i)
        data[i] = std::conj(data[i]);
    fft.forward(data);
    const float scale = 1.0f / static_cast<float>(n);
    for (int i = 0; i < n; ++i)
        data[i] = std::conj(data[i]) * scale;
}
} // namespace

void SpectralFilter::prepare(int maxOrder)
{
    maxOrder_ = std::max(4, maxOrder);
    ffts_.clear();
    for (int order = 0; order <= maxOrder_; ++order)
        ffts_.emplace_back(std::max(1, order));
    const auto maxN = static_cast<size_t>(1) << maxOrder_;
    window_.assign(maxN, 0.0f);
    taper_.assign(maxN, 0.0f);
    inL_.assign(maxN, 0.0f);
    inR_.assign(maxN, 0.0f);
    outL_.assign(2 * maxN, 0.0f);
    outR_.assign(2 * maxN, 0.0f);
    work_.assign(maxN, {});
    spectrumL_.assign(maxN / 2 + 1, {});
    spectrumR_.assign(maxN / 2 + 1, {});
    for (auto* v : {&ll_, &lr_, &rl_, &rr_})
        v->assign(maxN / 2 + 1, 0.0f);
    for (auto* k : {&kll_, &klr_, &krl_, &krr_})
        k->assign(maxN, {});
    setOrder(maxOrder_);
}

void SpectralFilter::setOrder(int order) noexcept
{
    order = std::clamp(order, 4, maxOrder_);
    n_ = 1 << order;
    m_ = n_ / 4;
    l_ = n_ - m_;
    fft_ = &ffts_[static_cast<size_t>(order)];
    ringMask_ = n_ - 1;
    outMask_ = 2LL * n_ - 1;
    // A periodic Hann over the frame: half-overlapping copies add to exactly one.
    for (int i = 0; i < m_; ++i)
        window_[static_cast<size_t>(i)] = static_cast<float>(0.5 - 0.5 * std::cos(twoPi * i / m_));
    // The kernel's taper: flat, its outer eighths falling to zero (Tukey), so a kernel that fits
    // stays exact and a longer one is cut off gently.
    const int edge = l_ / 8;
    for (int i = 0; i < l_; ++i)
    {
        const int fromEnd = std::min(i, l_ - 1 - i);
        taper_[static_cast<size_t>(i)] =
            fromEnd >= edge ? 1.0f : static_cast<float>(0.5 - 0.5 * std::cos(pi * (fromEnd + 0.5) / edge));
    }
    reset();
}

void SpectralFilter::reset() noexcept
{
    std::fill(inL_.begin(), inL_.end(), 0.0f);
    std::fill(inR_.begin(), inR_.end(), 0.0f);
    std::fill(outL_.begin(), outL_.end(), 0.0f);
    std::fill(outR_.begin(), outR_.end(), 0.0f);
    written_ = 0;
    sinceFrame_ = 0;
    std::fill(ll_.begin(), ll_.end(), 1.0f);
    std::fill(rr_.begin(), rr_.end(), 1.0f);
    std::fill(lr_.begin(), lr_.end(), 0.0f);
    std::fill(rl_.begin(), rl_.end(), 0.0f);
    kernelsValid_ = false;
}

void SpectralFilter::analyse(const float* left, const float* right, long long mask, long long end,
                             std::complex<float>* outLeft, std::complex<float>* outRight) noexcept
{
    // Both channels in one transform: left real, right imaginary; then split by symmetry.
    const long long start = end - m_;
    for (int i = 0; i < n_; ++i)
    {
        if (i < m_)
        {
            const auto at = static_cast<size_t>((start + i) & mask);
            const float w = window_[static_cast<size_t>(i)];
            work_[static_cast<size_t>(i)] = {left[at] * w, right[at] * w};
        }
        else
            work_[static_cast<size_t>(i)] = {};
    }
    fft_->forward(work_.data());
    for (int k = 0; k <= n_ / 2; ++k)
    {
        const auto z = work_[static_cast<size_t>(k)];
        const auto mirror = std::conj(work_[static_cast<size_t>((n_ - k) & (n_ - 1))]);
        outLeft[k] = 0.5f * (z + mirror);
        outRight[k] = std::complex<float>(0.0f, -0.5f) * (z - mirror);
    }
}

void SpectralFilter::kernelOf(const float* gains, std::vector<std::complex<float>>& kernel) noexcept
{
    // The zero-phase impulse response of the gains (real and even: its transform is real)...
    for (int k = 0; k <= n_ / 2; ++k)
    {
        work_[static_cast<size_t>(k)] = {gains[k], 0.0f};
        if (k > 0 && k < n_ / 2)
            work_[static_cast<size_t>(n_ - k)] = {gains[k], 0.0f};
    }
    inverse(*fft_, work_.data());
    // ...tapered to L samples around zero and delayed by L/2: linear phase, causal.
    for (int i = 0; i < n_; ++i)
        kernel[static_cast<size_t>(i)] = {};
    const int half = l_ / 2;
    for (int i = 0; i < l_; ++i)
    {
        const int lag = i - half; // the response at this lag sits at index lag mod N
        const float h = work_[static_cast<size_t>(lag & (n_ - 1))].real();
        kernel[static_cast<size_t>(i)] = {h * taper_[static_cast<size_t>(i)], 0.0f};
    }
    fft_->forward(kernel.data());
}

void SpectralFilter::designKernels() noexcept
{
    const int b = bins();
    diagonal_ = std::all_of(lr_.begin(), lr_.begin() + b, [](float g) { return g == 0.0f; }) &&
                std::all_of(rl_.begin(), rl_.begin() + b, [](float g) { return g == 0.0f; });
    same_ = diagonal_ && std::equal(ll_.begin(), ll_.begin() + b, rr_.begin());
    identity_ = same_ && std::all_of(ll_.begin(), ll_.begin() + b, [](float g) { return g == 1.0f; });
    kernelsValid_ = true;
    if (identity_)
        return;
    kernelOf(ll_.data(), kll_);
    if (!same_)
        kernelOf(rr_.data(), krr_);
    if (!diagonal_)
    {
        kernelOf(lr_.data(), klr_);
        kernelOf(rl_.data(), krl_);
    }
    kernelsValid_ = true;
}

void SpectralFilter::convolve() noexcept
{
    const long long start = written_ - m_;
    if (identity_)
    {
        // The windowed frame, L/2 later: what the identity's kernel does, without rounding.
        for (int i = 0; i < m_; ++i)
        {
            const auto in = static_cast<size_t>((start + i) & ringMask_);
            const auto at = static_cast<size_t>((start + i + l_ / 2) & outMask_);
            const float w = window_[static_cast<size_t>(i)];
            outL_[at] += inL_[in] * w;
            outR_[at] += inR_[in] * w;
        }
        return;
    }
    spectra();
    const auto& krr = same_ ? kll_ : krr_;
    for (int k = 0; k < n_; ++k)
    {
        // The full spectra from the half kept (real signals: conjugate symmetry).
        const bool upper = k > n_ / 2;
        const auto at = static_cast<size_t>(upper ? n_ - k : k);
        const auto xl = upper ? std::conj(spectrumL_[at]) : spectrumL_[at];
        const auto xr = upper ? std::conj(spectrumR_[at]) : spectrumR_[at];
        const auto i = static_cast<size_t>(k);
        auto yl = kll_[i] * xl, yr = krr[i] * xr;
        if (!diagonal_)
        {
            yl += klr_[i] * xr;
            yr += krl_[i] * xl;
        }
        work_[i] = yl + std::complex<float>(0.0f, 1.0f) * yr;
    }
    inverse(*fft_, work_.data());
    for (int i = 0; i < n_; ++i)
    {
        const auto at = static_cast<size_t>((start + i) & outMask_);
        outL_[at] += work_[static_cast<size_t>(i)].real();
        outR_[at] += work_[static_cast<size_t>(i)].imag();
    }
}
} // namespace fd::dsp
