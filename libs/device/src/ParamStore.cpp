#include "fd/device/ParamStore.h"

namespace fd
{
ParamStore::ParamStore(std::span<const ParamSpec> specs)
    : specs_(specs), values_(std::make_unique<std::atomic<float>[]>(specs.size()))
{
    resetToDefaults();
}

void ParamStore::resetToDefaults() noexcept
{
    for (size_t i = 0; i < specs_.size(); ++i)
        values_[i].store(specs_[i].sanitize(specs_[i].def), std::memory_order_relaxed);
}

std::optional<size_t> ParamStore::indexOf(std::string_view id) const noexcept
{
    for (size_t i = 0; i < specs_.size(); ++i)
        if (specs_[i].id == id)
            return i;
    return std::nullopt;
}
} // namespace fd
