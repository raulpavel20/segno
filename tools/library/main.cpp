// fd-library: builds Segno's Library (ADR 0055, docs/design/factory-library.md), the read-only
// files Segno ships besides its modules. JUCE-free (ADR 0006).
//
//   fd-library build <folder> [--recipe <recipe.json> --sources <cache>] [--racks <racks.json>]
//       writes the Library into <folder>: its version and sources, the factory wavetables
//       (Wavetables/<Category>/<Name>.wav, each carrying its ID), given the recipe and the
//       sources it names (fetched and checked by cmake/FdLibrarySources.cmake), the sample
//       instruments (Samples/<Family>/<Instrument>/<Articulation>.sfz beside samples/*.flac),
//       and given the rack recipes, the factory racks (Racks/<Folder>/<Name>.segnorack)
//   fd-library pitch <file>...
//       the pitch each file sounds at, as a MIDI note: for checking a map's roots

#include "LibraryBuild.h"
#include "RackRecipes.h"
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

/// The factory racks (racks.md §18.8), made from their recipes into Racks/<Folder>/.
bool writeRacks(const fs::path& folder, const fs::path& recipesFile, std::size_t* count)
{
    std::ifstream in(recipesFile);
    const auto recipes = nlohmann::json::parse(in, nullptr, false);
    const auto made = lb::racksFromRecipes(recipes);
    for (const auto& problem : made.problems)
        std::cerr << recipesFile.string() << ": " << problem << "\n";
    if (!made.problems.empty())
        return false;
    std::error_code ec;
    fs::remove_all(folder / "Racks", ec); // racks no recipe makes any more go
    for (const auto& rack : made.racks)
    {
        const auto file = folder / "Racks" / rack.folder / (rack.name + lb::rackFileExtension);
        fs::create_directories(file.parent_path(), ec);
        std::ofstream(file, std::ios::binary | std::ios::trunc) << rack.text;
    }
    *count = made.racks.size();
    std::cout << made.racks.size() << " factory racks\n";
    return true;
}

int build(const fs::path& folder, const fs::path& recipeFile, const fs::path& sources,
          const fs::path& racksFile)
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
    if (!racksFile.empty())
    {
        std::size_t racks = 0;
        if (!writeRacks(folder, racksFile, &racks))
            return 1;
        library["racks"] = racks;
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
    constexpr const char* usage =
        "usage: fd-library build <folder> [--recipe <recipe.json> --sources <cache>] [--racks <racks.json>]\n"
        "       fd-library pitch <file>...\n";
    if (command == "build" && argc >= 3 && argc % 2 == 1)
    {
        fs::path recipe, sources, racks;
        for (int i = 3; i + 1 < argc; i += 2)
        {
            const std::string flag = argv[i];
            if (flag == "--recipe")
                recipe = argv[i + 1];
            else if (flag == "--sources")
                sources = argv[i + 1];
            else if (flag == "--racks")
                racks = argv[i + 1];
            else
                return std::cerr << usage, 2;
        }
        if (recipe.empty() != sources.empty())
            return std::cerr << usage, 2;
        return build(argv[2], recipe, sources, racks);
    }
    if (command == "pitch" && argc > 2)
        return pitch(argc, argv);
    std::cerr << usage;
    return 2;
}
