#pragma once

#include <array>
#include <atomic>
#include <cassert>
#include <cstddef>
#include <memory>

namespace fd
{
/// Hands immutable heavy data (decoded samples, wavetables) from a loader to the audio thread
/// (ADR 0017). Wait-free on both sides; every allocation and deallocation happens on the loader
/// side, never on the audio thread.
///
/// Loader side (one thread at a time, not real-time): publish(), latest(), collect().
/// Audio side (the audio thread only): takeNew(), retire().
///
/// The audio side holds at most `maxHeld` references at once (e.g. the current sample plus an
/// old one whose voices are still fading out). takeNew() returns nothing while that many are
/// held; publishing again before the audio thread takes the data replaces it.
template <typename T>
class AssetHandoff
{
    struct Node
    {
        std::shared_ptr<const T> data;
    };

public:
    static constexpr size_t maxHeld = 4;

    /// A reference held by the audio thread. Trivially copyable; empty when `get()` is null.
    class Ref
    {
    public:
        Ref() = default;
        [[nodiscard]] const T* get() const noexcept { return node_ != nullptr ? node_->data.get() : nullptr; }
        [[nodiscard]] const T* operator->() const noexcept { return get(); }
        [[nodiscard]] explicit operator bool() const noexcept { return get() != nullptr; }
        [[nodiscard]] bool isSet() const noexcept { return node_ != nullptr; } // also true for published null
        friend bool operator==(const Ref& a, const Ref& b) noexcept { return a.node_ == b.node_; }

    private:
        friend class AssetHandoff;
        explicit Ref(Node* n) noexcept : node_(n) {}
        Node* node_ = nullptr;
    };

    AssetHandoff() = default;
    AssetHandoff(const AssetHandoff&) = delete;
    AssetHandoff& operator=(const AssetHandoff&) = delete;

    /// The audio thread must have stopped (device destruction).
    ~AssetHandoff()
    {
        delete pending_.exchange(nullptr);
        for (Node* n : held_)
            delete n;
        for (Node* n : deferred_)
            delete n;
        collect();
    }

    // ---- loader side --------------------------------------------------------------------

    /// Hands `data` to the audio thread (null clears it). Replaces data not yet taken.
    void publish(std::shared_ptr<const T> data)
    {
        latest_ = data;
        delete pending_.exchange(new Node{std::move(data)}, std::memory_order_acq_rel);
        collect();
    }

    /// The most recently published data (what the audio thread is about to use).
    [[nodiscard]] const std::shared_ptr<const T>& latest() const noexcept { return latest_; }

    /// Frees data the audio thread has retired.
    void collect() noexcept
    {
        size_t r = retireRead_.load(std::memory_order_relaxed);
        const size_t w = retireWrite_.load(std::memory_order_acquire);
        for (; r != w; ++r)
            delete retired_[r % retired_.size()];
        retireRead_.store(r, std::memory_order_release);
    }

    // ---- audio side ---------------------------------------------------------------------

    /// Newly published data since the last call, or an unset Ref. Wait-free, no allocation.
    [[nodiscard]] Ref takeNew() noexcept
    {
        flushDeferred();
        size_t slot = maxHeld;
        for (size_t i = 0; i < maxHeld; ++i)
            if (held_[i] == nullptr)
            {
                slot = i;
                break;
            }
        if (slot == maxHeld || pending_.load(std::memory_order_relaxed) == nullptr)
            return {};
        Node* n = pending_.exchange(nullptr, std::memory_order_acq_rel);
        held_[slot] = n;
        return Ref(n);
    }

    /// Gives back a reference no longer used by the audio thread. Wait-free, no deallocation.
    void retire(Ref ref) noexcept
    {
        if (ref.node_ == nullptr)
            return;
        for (Node*& h : held_)
            if (h == ref.node_)
                h = nullptr;
        if (!pushRetired(ref.node_))
            for (Node*& d : deferred_)
                if (d == nullptr)
                {
                    d = ref.node_;
                    return;
                }
        // Unreachable in practice (the loader collects on every publish); if both rings are
        // full the node is leaked rather than freed on the audio thread.
    }

private:
    bool pushRetired(Node* n) noexcept
    {
        const size_t w = retireWrite_.load(std::memory_order_relaxed);
        if (w - retireRead_.load(std::memory_order_acquire) >= retired_.size())
            return false;
        retired_[w % retired_.size()] = n;
        retireWrite_.store(w + 1, std::memory_order_release);
        return true;
    }

    void flushDeferred() noexcept
    {
        for (Node*& d : deferred_)
            if (d != nullptr && pushRetired(d))
                d = nullptr;
    }

    std::shared_ptr<const T> latest_;           // loader side
    std::atomic<Node*> pending_{nullptr};       // loader -> audio
    std::array<Node*, maxHeld> held_{};         // audio side
    std::array<Node*, 8> deferred_{};           // audio side, when the ring is full
    std::array<Node*, 16> retired_{};           // audio -> loader ring
    std::atomic<size_t> retireWrite_{0}, retireRead_{0};
};
} // namespace fd
