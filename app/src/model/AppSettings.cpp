#include "model/AppSettings.h"

#include <algorithm>
#include <fstream>
#include <sstream>
#include <system_error>

namespace fd::model
{
namespace
{
using Json = nlohmann::json;

/// The keys this build reads; the rest of the file is kept as it is.
constexpr const char* kKnown[] = {"segnoSettings", "recentProjects", "display",    "midi",
                                  "audioClips",    "recording",      "library",    "plugins",
                                  "export",        "keys",           "browser",    "assistants",
                                  "controllers"};

std::vector<std::string> strings(const Json& j, const char* key)
{
    std::vector<std::string> out;
    if (const auto it = j.find(key); it != j.end() && it->is_array())
        for (const auto& v : *it)
            if (v.is_string() && !v.get<std::string>().empty())
                out.push_back(v.get<std::string>());
    return out;
}

template <typename T> void read(const Json& j, const char* key, T& out)
{
    if (const auto it = j.find(key); it != j.end())
    {
        if constexpr (std::is_same_v<T, bool>)
        {
            if (it->is_boolean())
                out = it->get<bool>();
        }
        else if constexpr (std::is_same_v<T, int>)
        {
            if (it->is_number_integer())
                out = it->get<int>();
        }
        else if constexpr (std::is_same_v<T, std::string>)
        {
            if (it->is_string())
                out = it->get<std::string>();
        }
    }
}

const char* formatKey(RecordingFormat f)
{
    switch (f)
    {
    case RecordingFormat::Wav:
        return "wav";
    case RecordingFormat::Aiff:
        return "aiff";
    case RecordingFormat::Flac:
        return "flac";
    case RecordingFormat::Mp3: // recordings are never lossy: these never reach a recording
    case RecordingFormat::Ogg:
    case RecordingFormat::Opus:
        break;
    }
    return "wav";
}

std::optional<std::string> readFile(const std::filesystem::path& file)
{
    std::ifstream in(file, std::ios::binary);
    if (!in)
        return std::nullopt;
    std::ostringstream text;
    text << in.rdbuf();
    return text.str();
}
} // namespace

void RecentProjects::add(const std::string& path)
{
    if (path.empty())
        return;
    remove(path);
    paths.insert(paths.begin(), path);
    if (paths.size() > kMax)
        paths.resize(kMax);
}

void RecentProjects::remove(const std::string& path)
{
    paths.erase(std::remove(paths.begin(), paths.end(), path), paths.end());
}

std::vector<std::string> RecentProjects::existing(const std::function<bool(const std::string&)>& exists) const
{
    std::vector<std::string> out;
    for (const auto& p : paths)
        if (exists(p))
            out.push_back(p);
    return out;
}

std::string toJson(const AppSettings& s)
{
    nlohmann::ordered_json j;
    j["segnoSettings"] = 1;
    j["recentProjects"] = s.recent.paths;
    j["display"] = {{"interfaceSize", s.display.interfaceSize}, {"systemTitleBar", s.display.systemTitleBar}};
    j["midi"] = {{"computerKeyboard", s.midi.computerKeyboard}};
    j["audioClips"] = {{"loopsFollowTempo", s.audioClips.loopsFollowTempo},
                       {"recordingsFollowTempo", s.audioClips.recordingsFollowTempo},
                       {"loopsFollowKey", s.audioClips.loopsFollowKey}};
    j["recording"] = {{"format", formatKey(s.recording.format)}, {"bitDepth", s.recording.bitDepth}};
    j["library"] = {{"location", s.library.location},
                    {"folder", s.library.folder},
                    {"newProjects", s.library.newProjects}};
    j["plugins"] = {{"folders", s.plugins.folders}};
    auto exportDefaults = s.exportDefaults;
    exportDefaults.folder.clear(); // the project's
    exportDefaults.name.clear();
    j["export"] = nlohmann::ordered_json::parse(toJson(exportDefaults));
    j["keys"] = s.keys;
    j["browser"] = {{"places", s.browser.places},
                    {"favourites", s.browser.favourites},
                    {"preview", s.browser.preview},
                    {"previewVolume", s.browser.previewVolume},
                    {"open", s.browser.open}};
    j["assistants"] = {
        {"enabled", s.assistants.enabled}, {"readOnly", s.assistants.readOnly}, {"port", s.assistants.port}};
    auto transport = nlohmann::ordered_json::array();
    for (const auto& m : s.controllers.transport)
        transport.push_back(nlohmann::ordered_json::parse(toJson(m).dump()));
    j["controllers"] = {{"takeover", takeoverKey(s.controllers.takeover)},
                        {"sendBack", s.controllers.sendBack},
                        {"transport", std::move(transport)}};
    for (const auto& [key, value] : s.others.items())
        j[key] = value;
    return j.dump(2) + "\n";
}

AppSettings appSettingsFromJson(const std::string& json)
{
    AppSettings s;
    const auto j = Json::parse(json, nullptr, false);
    if (!j.is_object())
        return s;
    if (const auto it = j.find("recentProjects"); it != j.end() && it->is_array())
        for (const auto& p : *it)
            if (p.is_string() && s.recent.paths.size() < RecentProjects::kMax)
                s.recent.paths.push_back(p.get<std::string>());
    if (const auto it = j.find("display"); it != j.end() && it->is_object())
    {
        read(*it, "interfaceSize", s.display.interfaceSize);
        if (s.display.interfaceSize != 0 && std::find(std::begin(kInterfaceSizes), std::end(kInterfaceSizes),
                                                      s.display.interfaceSize) == std::end(kInterfaceSizes))
            s.display.interfaceSize = 0;
        read(*it, "systemTitleBar", s.display.systemTitleBar);
    }
    if (const auto it = j.find("midi"); it != j.end() && it->is_object())
        read(*it, "computerKeyboard", s.midi.computerKeyboard);
    if (const auto it = j.find("audioClips"); it != j.end() && it->is_object())
    {
        read(*it, "loopsFollowTempo", s.audioClips.loopsFollowTempo);
        read(*it, "recordingsFollowTempo", s.audioClips.recordingsFollowTempo);
        read(*it, "loopsFollowKey", s.audioClips.loopsFollowKey);
    }
    if (const auto it = j.find("recording"); it != j.end() && it->is_object())
    {
        std::string format;
        read(*it, "format", format);
        for (const auto f : {RecordingFormat::Wav, RecordingFormat::Aiff, RecordingFormat::Flac})
            if (format == formatKey(f))
                s.recording.format = f;
        read(*it, "bitDepth", s.recording.bitDepth);
        if (const auto depths = bitDepthsFor(s.recording.format);
            std::find(depths.begin(), depths.end(), s.recording.bitDepth) == depths.end())
            s.recording.bitDepth = 24;
    }
    if (const auto it = j.find("library"); it != j.end() && it->is_object())
    {
        read(*it, "location", s.library.location);
        read(*it, "folder", s.library.folder);
        read(*it, "newProjects", s.library.newProjects);
    }
    if (const auto it = j.find("plugins"); it != j.end() && it->is_object())
        if (const auto folders = it->find("folders"); folders != it->end() && folders->is_array())
            for (const auto& f : *folders)
                if (f.is_string() && !f.get<std::string>().empty())
                    s.plugins.folders.push_back(f.get<std::string>());
    if (const auto it = j.find("export"); it != j.end() && it->is_object())
    {
        s.exportDefaults = exportSettingsFromJson(it->dump());
        s.exportDefaults.folder.clear();
        s.exportDefaults.name.clear();
    }
    if (const auto it = j.find("keys"); it != j.end() && it->is_object())
        for (const auto& [id, keys] : it->items())
            if (keys.is_array())
            {
                auto& list = s.keys[id];
                for (const auto& k : keys)
                    if (k.is_string())
                        list.push_back(k.get<std::string>());
            }
    if (const auto it = j.find("browser"); it != j.end() && it->is_object())
    {
        s.browser.places = strings(*it, "places");
        s.browser.favourites = strings(*it, "favourites");
        s.browser.open = strings(*it, "open");
        read(*it, "preview", s.browser.preview);
        if (const auto v = it->find("previewVolume"); v != it->end() && v->is_number())
            s.browser.previewVolume = std::clamp(v->get<double>(), -60.0, 6.0);
    }
    if (const auto it = j.find("assistants"); it != j.end() && it->is_object())
    {
        read(*it, "enabled", s.assistants.enabled);
        read(*it, "readOnly", s.assistants.readOnly);
        read(*it, "port", s.assistants.port);
        s.assistants.port = std::clamp(s.assistants.port, 1024, 65535);
    }
    if (const auto it = j.find("controllers"); it != j.end() && it->is_object())
    {
        std::string takeover;
        read(*it, "takeover", takeover);
        s.controllers.takeover = takeoverFromKey(takeover).value_or(MidiTakeover::PickUp);
        s.controllers.sendBack = strings(*it, "sendBack");
        if (const auto t = it->find("transport"); t != it->end() && t->is_array())
            for (const auto& m : *t)
                if (auto mapping = midiMappingFromJson(m); mapping && mapping->target.isTransport())
                    s.controllers.transport.push_back(std::move(*mapping));
    }
    for (const auto& [key, value] : j.items())
        if (std::none_of(std::begin(kKnown), std::end(kKnown), [&](const char* k) { return key == k; }))
            s.others[key] = value;
    return s;
}

AppSettings settingsFromOlderFiles(const std::string& appJson, const std::string& assistantsJson)
{
    AppSettings s;
    if (const auto app = Json::parse(appJson, nullptr, false); app.is_object())
    {
        if (const auto it = app.find("recentProjects"); it != app.end() && it->is_array())
            for (const auto& p : *it)
                if (p.is_string() && s.recent.paths.size() < RecentProjects::kMax)
                    s.recent.paths.push_back(p.get<std::string>());
        read(app, "systemTitleBar", s.display.systemTitleBar);
    }
    if (const auto mcp = Json::parse(assistantsJson, nullptr, false); mcp.is_object())
    {
        read(mcp, "enabled", s.assistants.enabled);
        read(mcp, "readOnly", s.assistants.readOnly);
        read(mcp, "port", s.assistants.port);
        s.assistants.port = std::clamp(s.assistants.port, 1024, 65535);
    }
    return s;
}

double interfaceScale(const AppSettings& settings)
{
    return settings.display.interfaceSize > 0 ? settings.display.interfaceSize / 100.0 : 1.0;
}

// ---- the store -----------------------------------------------------------------------------

SettingsStore::SettingsStore(std::filesystem::path folder) : folder_(std::move(folder))
{
    if (folder_.empty())
        return;
    if (const auto text = readFile(file()))
    {
        settings_ = appSettingsFromJson(*text);
        return;
    }
    // Before ADR 0045: app.json and assistants.json, moved in once.
    const auto app = readFile(folder_ / "app.json"), assistants = readFile(folder_ / "assistants.json");
    if (!app && !assistants)
        return;
    settings_ = settingsFromOlderFiles(app.value_or(""), assistants.value_or(""));
    save();
    std::error_code ec;
    if (std::filesystem::exists(file(), ec))
    {
        std::filesystem::remove(folder_ / "app.json", ec);
        std::filesystem::remove(folder_ / "assistants.json", ec);
    }
}

void SettingsStore::change(const std::function<void(AppSettings&)>& edit)
{
    auto changed = settings_;
    edit(changed);
    if (changed == settings_)
        return;
    settings_ = std::move(changed);
    save();
    for (const auto& [id, listener] : std::map<int, Listener>(listeners_)) // a listener may remove itself
        if (listeners_.contains(id))
            listener(settings_);
}

int SettingsStore::addListener(Listener listener)
{
    listeners_[nextListener_] = std::move(listener);
    return nextListener_++;
}

void SettingsStore::removeListener(int id)
{
    listeners_.erase(id);
}

void SettingsStore::save() const
{
    if (folder_.empty())
        return;
    std::error_code ec;
    std::filesystem::create_directories(folder_, ec);
    auto temp = file();
    temp += ".tmp";
    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        if (!out)
            return;
        out << toJson(settings_);
    }
    std::filesystem::rename(temp, file(), ec);
}
} // namespace fd::model
