#pragma once

#include <cstddef>
#include <span>
#include <string_view>

namespace fd::devices::wavetable
{
/// One single cycle of AKWF-FREE (CC0), as its WAV file's bytes.
struct AkwfCycle
{
    const unsigned char* wav;
    std::size_t size;
};

/// A factory table made of AKWF cycles (devices/wavetable/tables/akwf.json): its cycles in order,
/// morphed over its frames.
struct AkwfTable
{
    std::string_view id, name, category;
    int frames;
    std::span<const AkwfCycle> cycles;
};

/// Every AKWF table, from the source cmake/FdAkwf.cmake generates.
[[nodiscard]] std::span<const AkwfTable> akwfTables();
} // namespace fd::devices::wavetable
