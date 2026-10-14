#include "fd/device/Library.h"

#include <cstdlib>
#include <fstream>
#include <mutex>
#include <nlohmann/json.hpp>

namespace fd::library
{
namespace
{
namespace fs = std::filesystem;

std::mutex& mutex()
{
    static std::mutex m;
    return m;
}

fs::path& hostChoice()
{
    static fs::path p;
    return p;
}

fs::path fromEnvironment(const char* name)
{
    const char* v = std::getenv(name);
    return v != nullptr && *v != '\0' ? fs::path(v) : fs::path();
}

bool holdsLibrary(const fs::path& folder)
{
    std::error_code ec;
    return !folder.empty() && fs::is_regular_file(folder / "library.json", ec);
}
} // namespace

std::vector<fs::path> standardPlaces()
{
    std::vector<fs::path> out;
#if defined(_WIN32)
    if (const auto local = fromEnvironment("LOCALAPPDATA"); !local.empty())
        out.push_back(local / "Segno" / "Library");
    if (const auto all = fromEnvironment("PROGRAMDATA"); !all.empty())
        out.push_back(all / "Segno" / "Library");
#elif defined(__APPLE__)
    if (const auto home = fromEnvironment("HOME"); !home.empty())
        out.push_back(home / "Library" / "Application Support" / "Segno" / "Library");
    out.emplace_back("/Library/Application Support/Segno/Library");
#else
    if (const auto data = fromEnvironment("XDG_DATA_HOME"); !data.empty())
        out.push_back(data / "Segno" / "Library");
    else if (const auto home = fromEnvironment("HOME"); !home.empty())
        out.push_back(home / ".local" / "share" / "Segno" / "Library");
    out.emplace_back("/usr/local/share/segno/library");
    out.emplace_back("/usr/share/segno/library");
#endif
#ifdef FD_DEV_LIBRARY
    out.emplace_back(FD_DEV_LIBRARY); // a development build's, made by the segno-library target
#endif
    return out;
}

void setRoot(fs::path folder)
{
    const std::scoped_lock lock(mutex());
    hostChoice() = std::move(folder);
}

fs::path root()
{
    {
        const std::scoped_lock lock(mutex());
        if (!hostChoice().empty())
            return hostChoice();
    }
    if (auto env = fromEnvironment("SEGNO_LIBRARY"); !env.empty())
        return env;
    for (const auto& place : standardPlaces())
        if (holdsLibrary(place))
            return place;
    return {};
}

bool isReference(std::string_view stored) noexcept
{
    return stored.starts_with(prefix);
}

fs::path resolve(std::string_view stored)
{
    if (!isReference(stored))
        return fs::path(stored);
    const auto base = root();
    if (base.empty())
        return {};
    return base / fs::path(stored.substr(prefix.size()));
}

std::string reference(const fs::path& file)
{
    const auto base = root();
    if (base.empty() || file.empty())
        return file.string();
    std::error_code ec;
    const auto canonicalBase = fs::weakly_canonical(base, ec);
    const auto canonicalFile = fs::weakly_canonical(file, ec);
    const auto relative = canonicalFile.lexically_relative(canonicalBase);
    if (ec || relative.empty() || relative.begin()->string() == "..")
        return file.string();
    return std::string(prefix) + relative.generic_string();
}

std::string version()
{
    std::ifstream in(root() / "library.json");
    const auto j = nlohmann::json::parse(in, nullptr, false);
    return j.is_object() && j.contains("version") && j["version"].is_string()
               ? j["version"].get<std::string>()
               : std::string();
}

std::string missingMessage(std::string_view stored)
{
    const auto where = std::string(stored.substr(isReference(stored) ? prefix.size() : 0));
    if (root().empty())
        return "This sound is in Segno's Library (" + where +
               "), which isn't installed here: install the Library, or point Settings > Library to it.";
    return "Segno's Library here has no " + where + ": it may be an older one (version " + version() + ").";
}
} // namespace fd::library
