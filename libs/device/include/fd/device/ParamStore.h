#pragma once

#include "fd/device/Param.h"

#include <atomic>
#include <memory>
#include <optional>
#include <span>
#include <string_view>

namespace fd
{
/// Current plain values of a device's parameters, written by the host adapter and read by
/// the device on the audio thread. Lock-free (relaxed atomics); the device never writes it.
class ParamStore
{
public:
    explicit ParamStore(std::span<const ParamSpec> specs);

    [[nodiscard]] size_t size() const noexcept { return specs_.size(); }
    [[nodiscard]] std::span<const ParamSpec> specs() const noexcept { return specs_; }
    [[nodiscard]] const ParamSpec& spec(size_t index) const noexcept { return specs_[index]; }

    [[nodiscard]] float get(size_t index) const noexcept { return values_[index].load(std::memory_order_relaxed); }
    [[nodiscard]] bool getBool(size_t index) const noexcept { return get(index) >= 0.5f; }
    [[nodiscard]] int getInt(size_t index) const noexcept { return static_cast<int>(get(index)); }
    [[nodiscard]] float getNormalized(size_t index) const noexcept { return specs_[index].toNormalized(get(index)); }

    /// Host side. Values are sanitised (clamped, stepped kinds rounded).
    void set(size_t index, float plain) noexcept
    {
        values_[index].store(specs_[index].sanitize(plain), std::memory_order_relaxed);
    }
    void setNormalized(size_t index, float normalized) noexcept
    {
        values_[index].store(specs_[index].fromNormalized(normalized), std::memory_order_relaxed);
    }

    void resetToDefaults() noexcept;
    [[nodiscard]] std::optional<size_t> indexOf(std::string_view id) const noexcept;

private:
    std::span<const ParamSpec> specs_;
    std::unique_ptr<std::atomic<float>[]> values_;
};
} // namespace fd
