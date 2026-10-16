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
//   fd-library files <recipe.json>
//       every source file the recipe needs, a line each: <raw>/<commit>, <source>/<commit>, path,
//       SHA-256, tab-separated (cmake/FdLibrarySources.cmake fetches them)
//   fd-library measure <file>...
//       each file's channels, rate, length, peak, loudness and punch, a JSON line each (for balancing
//       kits, tools/library/make_recipe.py)

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

int files(const fs::path& recipePath)
{
    std::string error;
    const auto recipe = lb::readRecipe(recipePath, &error);
    if (!recipe)
        return std::cerr << error << "\n", 1;
    for (const auto& f : lb::sourceFiles(*recipe))
        std::cout << f.source->raw << "/" << f.source->commit << "\t" << f.source->id << "/"
                  << f.source->commit << "\t" << f.file << "\t" << f.sha256 << "\n";
    return 0;
}

int measure(int argc, char** argv)
{
    for (int i = 2; i < argc; ++i)
    {
        std::string error;
        const auto audio = fd::audiofile::decode(argv[i], &error);
        nlohmann::json j{{"file", argv[i]}};
        if (!audio || audio->numFrames() == 0)
        {
            j["error"] = audio ? std::string("empty") : error;
            std::cout << j.dump() << "\n";
            continue;
        }
        // The peak; the loudness of the first 100 ms from its onset (a hit's body) and of the whole,
        // RMS over the channels; where it falls 60 dB under its peak.
        const int frames = audio->numFrames();
        const int onset = lb::onsetOf(*audio);
        const int body = std::min(frames - onset, static_cast<int>(0.1 * audio->sampleRate));
        double peak = 0.0, bodySum = 0.0, sum = 0.0;
        for (const auto& ch : audio->channels)
            for (int f = 0; f < frames; ++f)
            {
                const double x = ch[static_cast<size_t>(f)];
                peak = std::max(peak, std::abs(x));
                sum += x * x;
                if (f >= onset && f < onset + body)
                    bodySum += x * x;
            }
        int last = 0;
        for (const auto& ch : audio->channels)
            for (int f = frames - 1; f > last; --f)
                if (std::abs(ch[static_cast<size_t>(f)]) > peak * 0.001)
                {
                    last = f;
                    break;
                }
        // Its punch: the loudest 25 ms, RMS, stepping 5 ms (a clap's energy is in its first 20 ms,
        // a kick's lasts: a window that long hears both as they're heard).
        const int window = std::max(1, static_cast<int>(0.025 * audio->sampleRate));
        const int step = std::max(1, static_cast<int>(0.005 * audio->sampleRate));
        double punch = 0.0;
        for (int from = 0; from < frames; from += step)
        {
            double s = 0.0;
            const int to = std::min(frames, from + window);
            for (const auto& ch : audio->channels)
                for (int f = from; f < to; ++f)
                    s += static_cast<double>(ch[static_cast<size_t>(f)]) * ch[static_cast<size_t>(f)];
            punch = std::max(punch, s / (window * static_cast<double>(audio->numChannels())));
        }
        const auto db = [](double v) { return v > 0.0 ? std::round(200.0 * std::log10(v)) / 10.0 : -200.0; };
        const double n = static_cast<double>(audio->numChannels());
        j["channels"] = audio->numChannels();
        j["rate"] = audio->sampleRate;
        j["bits"] = audio->bitsPerSample;
        j["seconds"] = static_cast<double>(frames) / audio->sampleRate;
        j["onset"] = static_cast<double>(onset) / audio->sampleRate;
        j["tail"] = static_cast<double>(last) / audio->sampleRate;
        j["peak"] = db(peak);
        j["body"] = db(std::sqrt(bodySum / std::max(1.0, body * n)));
        j["rms"] = db(std::sqrt(sum / std::max(1.0, frames * n)));
        j["punch"] = db(std::sqrt(punch));
        std::cout << j.dump() << "\n";
    }
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
        "       fd-library pitch <file>...\n"
        "       fd-library files <recipe.json>\n"
        "       fd-library measure <file>...\n";
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
    if (command == "files" && argc == 3)
        return files(argv[2]);
    if (command == "measure" && argc > 2)
        return measure(argc, argv);
    std::cerr << usage;
    return 2;
}
