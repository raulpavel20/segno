#include "model/AppSettings.h"

#include <algorithm>
#include <nlohmann/json.hpp>

namespace fd::model
{
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

std::string toJson(const AppSettings& settings)
{
    return nlohmann::json{{"recentProjects", settings.recent.paths},
                          {"systemTitleBar", settings.systemTitleBar}}
        .dump(2);
}

AppSettings appSettingsFromJson(const std::string& json)
{
    AppSettings s;
    const auto j = nlohmann::json::parse(json, nullptr, false);
    if (!j.is_object())
        return s;
    if (const auto it = j.find("recentProjects"); it != j.end() && it->is_array())
        for (const auto& p : *it)
            if (p.is_string() && s.recent.paths.size() < RecentProjects::kMax)
                s.recent.paths.push_back(p.get<std::string>());
    if (const auto it = j.find("systemTitleBar"); it != j.end() && it->is_boolean())
        s.systemTitleBar = it->get<bool>();
    return s;
}
} // namespace fd::model
