#pragma once

#include <atomic>
#include <cstddef>
#include <vector>

namespace fd
{
/// Single-producer single-consumer lock-free FIFO of samples, from the audio thread to the UI
/// (spectra, scopes, waveform playheads). The producer never blocks: when the FIFO is full,
/// new samples are dropped (displays only need recent data). Capacity is a power of two and is
/// allocated in prepare(), never on the audio thread.
class SampleFifo
{
public:
    /// Not real-time. Capacity is rounded up to a power of two.
    void prepare(size_t capacity)
    {
        size_t c = 1;
        while (c < capacity)
            c <<= 1;
        buffer_.assign(c, 0.0f);
        mask_ = c - 1;
        write_.store(0, std::memory_order_relaxed);
        read_.store(0, std::memory_order_relaxed);
    }

    [[nodiscard]] size_t capacity() const noexcept { return buffer_.size(); }

    /// Audio thread. Returns the number of samples written.
    size_t push(const float* data, size_t n) noexcept
    {
        const size_t w = write_.load(std::memory_order_relaxed);
        const size_t r = read_.load(std::memory_order_acquire);
        const size_t free = buffer_.size() - (w - r);
        const size_t count = n < free ? n : free;
        for (size_t i = 0; i < count; ++i)
            buffer_[(w + i) & mask_] = data[i];
        write_.store(w + count, std::memory_order_release);
        return count;
    }

    /// Audio thread. Writes all `n` samples or none (a frame of several values must never be
    /// split when the FIFO is full). Returns whether it wrote them.
    bool pushFrame(const float* data, size_t n) noexcept
    {
        const size_t w = write_.load(std::memory_order_relaxed);
        if (buffer_.size() - (w - read_.load(std::memory_order_acquire)) < n)
            return false;
        return push(data, n) == n;
    }

    /// UI thread. Returns the number of samples read.
    size_t pop(float* out, size_t n) noexcept
    {
        const size_t r = read_.load(std::memory_order_relaxed);
        const size_t w = write_.load(std::memory_order_acquire);
        const size_t available = w - r;
        const size_t count = n < available ? n : available;
        for (size_t i = 0; i < count; ++i)
            out[i] = buffer_[(r + i) & mask_];
        read_.store(r + count, std::memory_order_release);
        return count;
    }

    [[nodiscard]] size_t available() const noexcept
    {
        return write_.load(std::memory_order_acquire) - read_.load(std::memory_order_relaxed);
    }

private:
    std::vector<float> buffer_;
    size_t mask_ = 0;
    alignas(64) std::atomic<size_t> write_{0};
    alignas(64) std::atomic<size_t> read_{0};
};
} // namespace fd
