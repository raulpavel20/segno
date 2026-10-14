// fd-library: builds Segno's Library (ADR 0055, docs/design/factory-library.md), the read-only
// files Segno ships besides its modules. JUCE-free (ADR 0006).
//
//   fd-library build <folder> [--recipe <recipe.json> --sources <cache>]
//       writes the Library into <folder>: its version and sources, the factory wavetables
//       (Wavetables/<Category>/<Name>.wav, each carrying its ID) and, given the recipe and the
//       sources it names (fetched and checked by cmake/FdLibrarySources.cmake), the sample
//       instruments (Samples/<Family>/<Instrument>/<Articulation>.sfz beside samples/*.flac)
//   fd-library pitch <file>...
//       the pitch each file sounds at, as a MIDI note: for checking a map's roots

#include "LibraryBuild.h"
#include "TableFiles.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>

namespace
{
namespace fs = std::filesystem;
namespace lb = fd::library_build;

/// The Library's version: what presets and projects that need it compare against.
constexpr const char* kVersion = "1.0.0";

int build(const fs::path& folder, const fs::path& recipeFile, const fs::path& sources)
{
    std::error_code ec;
    fs::create_directories(folder, ec);
    std::string error;
    if (!fd::devices::wavetable::writeFactoryTableFiles(folder / "Wavetables", &error))
    {
        std::cerr << "couldn't write the wavetables: " << error << "\n";
        return 1;
    }
    nlohmann::json library{{"segnoLibrary", 1}, {"version", kVersion}};
    if (!recipeFile.empty())
    {
        const auto recipe = lb::readRecipe(recipeFile, &error);
        if (!recipe)
        {
            std::cerr << error << "\n";
            return 1;
        }
        if (recipe->version != kVersion)
        {
            std::cerr << recipeFile.string() << " makes Library " << recipe->version << ", the tool "
                      << kVersion << "\n";
            return 1;
        }
        if (!lb::buildInstruments(*recipe, sources, folder, std::cout))
            return 1;
        nlohmann::json credits = nlohmann::json::array();
        for (const auto& s : recipe->sources)
            credits.push_back(
                {{"name", s.name}, {"url", s.url}, {"commit", s.commit}, {"license", s.license}});
        library["sources"] = std::move(credits);
        library["instruments"] = recipe->instruments.size();
        std::ofstream(folder / "README.txt")
            << "Segno's Library: the sounds and wavetables that come with Segno. Read-only: Segno never\n"
               "writes here, and presets and projects point into it by paths relative to it.\n\n"
               "The sample instruments are made from VSCO 2 Community Edition and the Versilian Community\n"
               "Sample Library, by Versilian Studios, both released under CC0 1.0. Thank you.\n"
               "The wavetables are Segno's own and the Adventure Kid Waveforms (CC0 1.0) by Kristoffer "
               "Ekstrand.\n";
    }
    std::ofstream(folder / "library.json") << library.dump(2) << "\n";
    std::cout << "the Library " << kVersion << " in " << folder.string() << "\n";
    return 0;
}

int pitch(int argc, char** argv)
{
    for (int i = 2; i < argc; ++i)
    {
        std::string error;
        const auto audio = fd::audiofile::decode(argv[i], &error);
        if (!audio)
        {
            std::cout << argv[i] << ": " << error << "\n";
            continue;
        }
        const auto p = lb::detectPitch(*audio);
        std::cout << argv[i] << ": " << (p ? std::to_string(*p) : std::string("no pitch")) << "\n";
    }
    return 0;
}
} // namespace

int main(int argc, char** argv)
{
    const std::string command = argc > 1 ? argv[1] : "";
    if (command == "build" && (argc == 3 || argc == 7))
    {
        fs::path recipe, sources;
        for (int i = 3; i + 1 < argc; i += 2)
        {
            const std::string flag = argv[i];
            if (flag == "--recipe")
                recipe = argv[i + 1];
            else if (flag == "--sources")
                sources = argv[i + 1];
        }
        if (argc == 7 && (recipe.empty() || sources.empty()))
            return std::cerr << "fd-library build <folder> [--recipe <recipe.json> --sources <cache>]\n", 2;
        return build(argv[2], recipe, sources);
    }
    if (command == "pitch" && argc > 2)
        return pitch(argc, argv);
    std::cerr << "usage: fd-library build <folder> [--recipe <recipe.json> --sources <cache>]\n"
                 "       fd-library pitch <file>...\n";
    return 2;
}
