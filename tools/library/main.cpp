// fd-library: builds Segno's Library (ADR 0055, docs/design/factory-library.md), the read-only
// files Segno ships besides its modules. JUCE-free (ADR 0006).
//
//   fd-library build <folder>    writes the Library into <folder>: its version, the factory
//                                wavetables (Wavetables/<Category>/<Name>.wav, each carrying its ID)
//
// The sample instruments come with the Sampler's presets (factory-library.md §6).

#include "TableFiles.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>

namespace
{
namespace fs = std::filesystem;

/// The Library's version: what presets and projects that need it compare against.
constexpr const char* kVersion = "1.0.0";

int build(const fs::path& folder)
{
    std::error_code ec;
    fs::create_directories(folder, ec);
    std::string error;
    if (!fd::devices::wavetable::writeFactoryTableFiles(folder / "Wavetables", &error))
    {
        std::cerr << "couldn't write the wavetables: " << error << "\n";
        return 1;
    }
    std::ofstream(folder / "library.json")
        << nlohmann::json{{"segnoLibrary", 1}, {"version", kVersion}}.dump(2) << "\n";
    std::cout << "the Library " << kVersion << " in " << folder.string() << "\n";
    return 0;
}
} // namespace

int main(int argc, char** argv)
{
    if (argc == 3 && std::string(argv[1]) == "build")
        return build(argv[2]);
    std::cerr << "usage: fd-library build <folder>\n";
    return 2;
}
