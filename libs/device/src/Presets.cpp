#include "fd/device/Presets.h"

#include "fd/device/Registry.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <sstream>
#include <system_error>

namespace fd
{
namespace fs = std::filesystem;

namespace
{
constexpr std::string_view kFavouritesFile = "Favourites.json";
constexpr std::string_view kDefaultsFolder = "Defaults";
constexpr size_t kMaxNameLength = 100;

std::string lower(std::string_view text)
{
    std::string out(text);
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c)
                   { return static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c); });
    return out;
}

bool sameName(std::string_view a, std::string_view b)
{
    return lower(a) == lower(b);
}

/// Case-insensitive name order, then by bytes so it's total.
bool nameOrder(std::string_view a, std::string_view b)
{
    const auto la = lower(a), lb = lower(b);
    return la != lb ? la < lb : a < b;
}

std::optional<std::string> readFile(const fs::path& file)
{
    std::ifstream in(file, std::ios::binary);
    if (!in)
        return std::nullopt;
    std::ostringstream text;
    text << in.rdbuf();
    return text.str();
}

/// Writes through a temporary file, so a failed write never leaves half a preset.
bool writeFile(const fs::path& file, const std::string& text)
{
    std::error_code ec;
    fs::create_directories(file.parent_path(), ec);
    auto temp = file;
    temp += ".tmp";
    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        if (!out)
            return false;
        out << text;
        if (!out)
            return false;
    }
    fs::rename(temp, file, ec);
    if (ec)
    {
        fs::remove(temp, ec);
        return false;
    }
    return true;
}

/// A merge patch from `from` to `to`, recursing into objects both have.
nlohmann::json diff(const nlohmann::json& from, const nlohmann::json& to)
{
    auto patch = nlohmann::json::object();
    for (const auto& [key, value] : to.items())
    {
        const auto it = from.find(key);
        if (it != from.end() && it->is_object() && value.is_object())
        {
            if (auto inner = diff(*it, value); !inner.empty())
                patch[key] = std::move(inner);
        }
        else if (it == from.end() || *it != value)
            patch[key] = value;
    }
    for (const auto& [key, value] : from.items())
        if (!to.contains(key))
            patch[key] = nullptr;
    return patch;
}

/// The words of a search, lowered.
std::vector<std::string> words(std::string_view query)
{
    std::vector<std::string> out;
    std::istringstream in{std::string(query)};
    for (std::string w; in >> w;)
        out.push_back(lower(w));
    return out;
}

/// How well `word` matches a preset: 0 if not at all. The name counts most, its start most of all.
int score(const Preset& p, const std::string& word)
{
    const auto name = lower(p.name);
    if (name.starts_with(word))
        return 8;
    if (const auto at = name.find(word); at != std::string::npos)
        return name[at - 1] == ' ' || name[at - 1] == '-' ? 6 : 4;
    for (const auto& tag : p.tags)
        if (lower(tag).starts_with(word))
            return 3;
    if (lower(p.description).find(word) != std::string::npos ||
        lower(p.author).find(word) != std::string::npos)
        return 1;
    return 0;
}
} // namespace

// ---- a preset ------------------------------------------------------------------------------

std::string Preset::key() const
{
    switch (source)
    {
    case Source::Builtin:
        return {};
    case Source::Factory:
        return "factory/" + name;
    case Source::User:
        return "user/" + (folder.empty() ? std::string() : folder + "/") + name;
    case Source::Default:
        return "default";
    }
    return {};
}

bool presetTooNew(const DeviceInfo& info, const Preset& preset)
{
    return preset.newerFormat || preset.state.stateVersion > info.stateVersion;
}

std::string presetFileText(const Preset& preset)
{
    nlohmann::ordered_json json;
    json["segnoPreset"] = presetFormat;
    json["tags"] = preset.tags;
    json["description"] = preset.description;
    json["author"] = preset.author;
    // The state as projects store it (keys in their usual order), parameters to six significant
    // digits so the file reads well ("0.71", not "0.7099999785"); a float keeps those digits.
    auto state = nlohmann::ordered_json::parse(toJson(preset.state).dump());
    for (auto& [id, value] : state["params"].items())
        if (value.is_number_float())
        {
            char text[32];
            std::snprintf(text, sizeof text, "%.6g", value.get<double>());
            value = std::strtod(text, nullptr);
        }
    json["state"] = std::move(state);
    return json.dump(2) + "\n";
}

std::optional<Preset> parsePresetFile(std::string_view text, std::string name)
{
    const auto json = nlohmann::json::parse(text.begin(), text.end(), nullptr, /*allow_exceptions*/ false);
    if (json.is_discarded() || !json.is_object() || !json.contains("segnoPreset") ||
        !json["segnoPreset"].is_number_integer())
        return std::nullopt;
    auto state = json.contains("state") ? stateFromJson(json["state"]) : std::nullopt;
    if (!state)
        return std::nullopt;
    Preset p;
    p.name = std::move(name);
    p.state = std::move(*state);
    p.newerFormat = json["segnoPreset"].get<int>() > presetFormat;
    if (const auto it = json.find("tags"); it != json.end() && it->is_array())
        for (const auto& tag : *it)
            if (tag.is_string() && !tag.get<std::string>().empty())
                p.tags.push_back(tag.get<std::string>());
    if (const auto it = json.find("description"); it != json.end() && it->is_string())
        p.description = it->get<std::string>();
    if (const auto it = json.find("author"); it != json.end() && it->is_string())
        p.author = it->get<std::string>();
    return p;
}

DeviceState presetState(const DeviceInfo& info, DeviceState state)
{
    if (state.extra.is_object())
        for (const auto key : info.sessionExtraKeys)
            state.extra.erase(std::string(key));
    return state;
}

DeviceState defaultState(const DeviceInfo& info)
{
    DeviceState s;
    s.deviceId = std::string(info.id);
    s.stateVersion = info.stateVersion;
    for (const auto& spec : info.params)
        s.params[std::string(spec.id)] = spec.def;
    return s;
}

bool sameSound(const DeviceInfo& info, const DeviceState& a, const DeviceState& b)
{
    for (const auto& spec : info.params)
    {
        const std::string id(spec.id);
        const auto ia = a.params.find(id), ib = b.params.find(id);
        const float va = ia != a.params.end() ? ia->second : spec.def;
        const float vb = ib != b.params.end() ? ib->second : spec.def;
        const float tolerance = std::max(1e-4f * std::abs(spec.max - spec.min), 1e-6f);
        if (std::abs(va - vb) > tolerance)
            return false;
    }
    const auto ea = presetState(info, a).extra, eb = presetState(info, b).extra;
    const auto emptyA = ea.is_null() || ea.empty(), emptyB = eb.is_null() || eb.empty();
    return emptyA || emptyB ? emptyA == emptyB : ea == eb;
}

nlohmann::json extraPatch(const DeviceInfo& info, const nlohmann::json& from, const nlohmann::json& to)
{
    auto patch = diff(from.is_object() ? from : nlohmann::json::object(),
                      to.is_object() ? to : nlohmann::json::object());
    for (const auto key : info.sessionExtraKeys)
        patch.erase(std::string(key));
    return patch;
}

std::string presetNameProblem(std::string_view name)
{
    if (name.empty())
        return "A preset needs a name.";
    size_t characters = 0;
    for (const unsigned char c : name)
        if ((c & 0xC0) != 0x80)
            ++characters;
    if (characters > kMaxNameLength)
        return "A preset's name can be 100 characters at most.";
    if (name.front() == ' ' || name.back() == ' ' || name.front() == '.' || name.back() == '.')
        return "A preset's name can't start or end with a space or a dot.";
    for (const unsigned char c : name)
        if (c < 0x20 || std::string_view("/\\:*?\"<>|").find(static_cast<char>(c)) != std::string_view::npos)
            return "A preset's name can't hold / \\ : * ? \" < > or |.";
    if (sameName(name, defaultSettingsName))
        return "\"Default settings\" is the module's own: choose another name.";
    // Names Windows keeps for devices.
    static constexpr std::string_view reserved[] = {"con", "prn", "aux", "nul"};
    const auto l = lower(name);
    const bool numbered =
        l.size() == 4 && (l.starts_with("com") || l.starts_with("lpt")) && l[3] >= '1' && l[3] <= '9';
    if (numbered || std::find(std::begin(reserved), std::end(reserved), l) != std::end(reserved))
        return "That name is kept for devices on Windows: choose another.";
    return {};
}

// ---- the library ---------------------------------------------------------------------------

PresetLibrary::PresetLibrary(std::span<const FactoryPresetFile> factory, fs::path root)
    : root_(std::move(root))
{
    for (const auto& f : factory)
        if (auto p = parsePresetFile(f.text, std::string(f.name)))
        {
            p->source = Preset::Source::Factory;
            p->author = p->author.empty() ? "Segno" : p->author;
            factory_.push_back(std::move(*p));
        }
    std::stable_sort(factory_.begin(), factory_.end(),
                     [](const Preset& a, const Preset& b)
                     {
                         return a.state.deviceId != b.state.deviceId ? a.state.deviceId < b.state.deviceId
                                                                     : nameOrder(a.name, b.name);
                     });
    rescan();
}

void PresetLibrary::rescan()
{
    user_.clear();
    defaults_.clear();
    std::error_code ec;
    if (fs::is_directory(root_, ec))
    {
        for (fs::recursive_directory_iterator it(root_, fs::directory_options::skip_permission_denied, ec),
             end;
             !ec && it != end; it.increment(ec))
        {
            const auto& path = it->path();
            if (path.extension() != presetExtension || !it->is_regular_file(ec))
                continue;
            const auto text = readFile(path);
            if (!text)
                continue;
            auto p = parsePresetFile(*text, path.stem().string());
            if (!p)
                continue;
            p->file = path;
            const auto relative = path.parent_path().lexically_relative(root_);
            const auto folder = relative == "." ? std::string() : relative.generic_string();
            if (folder == kDefaultsFolder)
            {
                p->source = Preset::Source::Default;
                defaults_.push_back(std::move(*p));
            }
            else
            {
                p->source = Preset::Source::User;
                p->folder = folder;
                user_.push_back(std::move(*p));
            }
        }
    }
    std::stable_sort(user_.begin(), user_.end(),
                     [](const Preset& a, const Preset& b)
                     {
                         if (a.state.deviceId != b.state.deviceId)
                             return a.state.deviceId < b.state.deviceId;
                         return a.folder != b.folder ? nameOrder(a.folder, b.folder)
                                                     : nameOrder(a.name, b.name);
                     });
    std::sort(defaults_.begin(), defaults_.end(),
              [](const Preset& a, const Preset& b) { return a.file < b.file; });
    readFavourites();
}

const Preset& PresetLibrary::builtin(const DeviceInfo& info) const
{
    for (const auto& p : builtin_)
        if (p.state.deviceId == info.id)
            return p;
    Preset p;
    p.source = Preset::Source::Builtin;
    p.name = std::string(defaultSettingsName);
    p.author = "Segno";
    p.description = "The module's own defaults.";
    p.state = defaultState(info);
    return builtin_.emplace_back(std::move(p));
}

std::vector<const Preset*> PresetLibrary::presetsFor(const DeviceInfo& info) const
{
    std::vector<const Preset*> out{&builtin(info)};
    for (const auto& p : factory_)
        if (p.state.deviceId == info.id)
            out.push_back(&p);
    for (const auto& p : user_)
        if (p.state.deviceId == info.id)
            out.push_back(&p);
    return out;
}

const Preset* PresetLibrary::find(const DeviceInfo& info, std::string_view key) const
{
    if (key == "default")
        return defaultFor(info);
    for (const auto* p : presetsFor(info))
        if (p->key() == key)
            return p;
    return nullptr;
}

std::vector<const Preset*> PresetLibrary::filter(const std::vector<const Preset*>& list,
                                                 std::string_view device, std::string_view query,
                                                 const std::set<std::string>& tags, bool favouritesOnly) const
{
    const auto ws = words(query);
    std::vector<std::pair<int, const Preset*>> scored;
    for (const auto* p : list)
    {
        if (favouritesOnly && !isFavourite(device, p->key()))
            continue;
        if (!std::all_of(tags.begin(), tags.end(),
                         [&](const std::string& tag)
                         {
                             return std::any_of(p->tags.begin(), p->tags.end(),
                                                [&](const std::string& t) { return sameName(t, tag); });
                         }))
            continue;
        int total = 0;
        bool all = true;
        for (const auto& w : ws)
        {
            const int s = score(*p, w);
            all = all && s > 0;
            total += s;
        }
        if (all)
            scored.emplace_back(total, p);
    }
    std::stable_sort(scored.begin(), scored.end(),
                     [](const auto& a, const auto& b) { return a.first > b.first; });
    std::vector<const Preset*> out;
    for (const auto& [s, p] : scored)
        out.push_back(p);
    return out;
}

std::vector<std::string> PresetLibrary::tagsIn(const std::vector<const Preset*>& list)
{
    std::map<std::string, std::pair<int, std::string>> counts; // lowered -> (uses, as first written)
    for (const auto* p : list)
        for (const auto& tag : p->tags)
        {
            auto& c = counts[lower(tag)];
            if (c.first++ == 0)
                c.second = tag;
        }
    std::vector<std::pair<int, std::string>> sorted;
    for (const auto& [l, c] : counts)
        sorted.push_back(c);
    std::stable_sort(sorted.begin(), sorted.end(), [](const auto& a, const auto& b)
                     { return a.first != b.first ? a.first > b.first : nameOrder(a.second, b.second); });
    std::vector<std::string> out;
    for (auto& [n, tag] : sorted)
        out.push_back(std::move(tag));
    return out;
}

// ---- favourites ----------------------------------------------------------------------------

void PresetLibrary::readFavourites()
{
    favourites_.clear();
    const auto text = readFile(root_ / kFavouritesFile);
    if (!text)
        return;
    const auto json = nlohmann::json::parse(*text, nullptr, false);
    if (!json.is_object() || !json.contains("favourites") || !json["favourites"].is_object())
        return;
    for (const auto& [device, keys] : json["favourites"].items())
        if (keys.is_array())
            for (const auto& k : keys)
                if (k.is_string())
                    favourites_[device].insert(k.get<std::string>());
}

bool PresetLibrary::writeFavourites() const
{
    nlohmann::ordered_json json;
    json["segnoFavourites"] = 1;
    auto& all = json["favourites"] = nlohmann::ordered_json::object();
    for (const auto& [device, keys] : favourites_)
        if (!keys.empty())
            all[device] = std::vector<std::string>(keys.begin(), keys.end());
    return writeFile(root_ / kFavouritesFile, json.dump(2) + "\n");
}

bool PresetLibrary::isFavourite(std::string_view device, std::string_view key) const
{
    const auto it = favourites_.find(std::string(device));
    return it != favourites_.end() && it->second.contains(std::string(key));
}

bool PresetLibrary::setFavourite(std::string_view device, std::string_view key, bool favourite)
{
    auto& keys = favourites_[std::string(device)];
    if (favourite)
        keys.insert(std::string(key));
    else
        keys.erase(std::string(key));
    return writeFavourites();
}

// ---- defaults ------------------------------------------------------------------------------

const Preset* PresetLibrary::defaultFor(const DeviceInfo& info) const
{
    for (const auto& p : defaults_)
        if (p.state.deviceId == info.id)
            return &p;
    return nullptr;
}

bool PresetLibrary::saveDefault(const DeviceInfo& info, const DeviceState& state)
{
    Preset p;
    p.source = Preset::Source::Default;
    p.name = std::string(info.name);
    p.state = presetState(info, state);
    // One default per module: an older one under another file name goes.
    std::error_code ec;
    for (const auto& d : defaults_)
        if (d.state.deviceId == info.id && d.file.stem() != p.name)
            fs::remove(d.file, ec);
    auto file = root_ / kDefaultsFolder / p.name;
    file += presetExtension;
    const bool ok = writeFile(file, presetFileText(p));
    rescan();
    return ok;
}

bool PresetLibrary::clearDefault(const DeviceInfo& info)
{
    bool any = false;
    std::error_code ec;
    for (const auto& d : defaults_)
        if (d.state.deviceId == info.id)
            any = fs::remove(d.file, ec) || any;
    rescan();
    return any;
}

// ---- yours ---------------------------------------------------------------------------------

fs::path PresetLibrary::folderFor(const DeviceInfo& info) const
{
    return root_ / std::string(info.name);
}

PresetLibrary::Saved PresetLibrary::writeYours(const DeviceInfo& info, Preset preset, const fs::path& file)
{
    preset.state = presetState(info, std::move(preset.state));
    if (!writeFile(file, presetFileText(preset)))
        return {.problem = "The preset couldn't be written to " + file.parent_path().string() + "."};
    rescan();
    const auto relative = file.parent_path().lexically_relative(root_);
    Preset found;
    found.source = Preset::Source::User;
    found.folder = relative == "." ? std::string() : relative.generic_string();
    found.name = file.stem().string();
    return {.preset = find(info, found.key())};
}

PresetLibrary::Saved PresetLibrary::save(const DeviceInfo& info, const DeviceState& state,
                                         const std::string& name, std::vector<std::string> tags,
                                         std::string description, std::string author, bool replace)
{
    if (auto problem = presetNameProblem(name); !problem.empty())
        return {.problem = std::move(problem)};
    const auto folder = folderFor(info);
    std::error_code ec;
    for (const auto& p : user_)
        if (p.state.deviceId == info.id && p.folder == info.name && sameName(p.name, name))
        {
            if (!replace)
                return {.problem = "You have a preset called " + p.name + ".", .exists = true};
            if (p.name != name) // differently cased: the new name wins
                fs::remove(p.file, ec);
        }
    Preset p;
    p.source = Preset::Source::User;
    p.name = name;
    p.tags = std::move(tags);
    p.description = std::move(description);
    p.author = std::move(author);
    p.state = state;
    auto file = folder / name;
    file += presetExtension;
    return writeYours(info, std::move(p), file);
}

PresetLibrary::Saved PresetLibrary::overwrite(const DeviceInfo& info, const Preset& preset,
                                              const DeviceState& state)
{
    if (!preset.isYours())
        return {.problem = "Factory presets can't be changed: save it as one of yours."};
    auto p = preset;
    p.state = state;
    const auto file = p.file;
    return writeYours(info, std::move(p), file);
}

PresetLibrary::Saved PresetLibrary::rename(const DeviceInfo& info, const Preset& preset,
                                           const std::string& name)
{
    if (!preset.isYours())
        return {.problem = "Factory presets can't be renamed."};
    if (auto problem = presetNameProblem(name); !problem.empty())
        return {.problem = std::move(problem)};
    auto to = preset.file.parent_path() / name;
    to += presetExtension;
    std::error_code ec;
    for (const auto& p : user_)
        if (p.file != preset.file && p.state.deviceId == info.id && p.folder == preset.folder &&
            sameName(p.name, name))
            return {.problem = "You have a preset called " + p.name + ".", .exists = true};
    const auto oldKey = preset.key();
    const auto from = preset.file;
    fs::rename(from, to, ec);
    if (ec)
        return {.problem = "The preset couldn't be renamed."};
    auto renamed = preset;
    renamed.name = name;
    if (auto& keys = favourites_[std::string(info.id)]; keys.erase(oldKey) > 0)
    {
        keys.insert(renamed.key());
        writeFavourites();
    }
    rescan();
    return {.preset = find(info, renamed.key())};
}

bool PresetLibrary::remove(const DeviceInfo& info, const Preset& preset,
                           const std::function<bool(const fs::path&)>& remove)
{
    if (!preset.isYours() || !remove || !remove(preset.file))
        return false;
    if (auto& keys = favourites_[std::string(info.id)]; keys.erase(preset.key()) > 0)
        writeFavourites();
    rescan();
    return true;
}

// ---- recipes -------------------------------------------------------------------------------

RecipeResult presetsFromRecipes(const DeviceFactory& factory, const nlohmann::json& recipes)
{
    RecipeResult out;
    const auto& info = factory.info();
    const auto author = recipes.value("author", std::string("Segno"));
    if (!recipes.contains("presets") || !recipes["presets"].is_array())
    {
        out.problems.push_back("no presets");
        return out;
    }
    for (const auto& r : recipes["presets"])
    {
        const auto name = r.value("name", std::string());
        if (const auto why = presetNameProblem(name); !why.empty())
        {
            out.problems.push_back(name + ": " + why);
            continue;
        }
        auto device = factory.create();
        auto& store = device->params();
        const auto params = r.value("params", nlohmann::json::object());
        for (const auto& [key, value] : params.items())
        {
            size_t i = 0;
            while (i < info.params.size() && info.params[i].id != key)
                ++i;
            if (i == info.params.size())
            {
                out.problems.push_back(name + ": no parameter " + key);
                continue;
            }
            const auto& spec = info.params[i];
            std::optional<float> plain;
            if (value.is_number())
                plain = value.get<float>();
            else if (value.is_boolean())
                plain = value.get<bool>() ? spec.max : spec.min;
            else if (value.is_string())
                plain = spec.fromText(value.get<std::string>());
            if (!plain || spec.sanitize(*plain) != *plain)
            {
                out.problems.push_back(name + ": " + key + " = " + value.dump() + " isn't a value it takes");
                continue;
            }
            store.set(i, *plain);
        }
        if (r.contains("extra"))
            device->readExtraState(r["extra"]);
        Preset p;
        p.source = Preset::Source::Factory;
        p.name = name;
        p.tags = r.value("tags", std::vector<std::string>{});
        p.description = r.value("description", std::string());
        p.author = author;
        p.state = presetState(info, captureState(*device));
        for (const auto& other : out.presets)
            if (sameSound(info, other.state, p.state))
                out.problems.push_back(name + " sounds the same as " + other.name);
        out.presets.push_back(std::move(p));
    }
    return out;
}

// ---- the memo ------------------------------------------------------------------------------

nlohmann::json PresetMemo::toJson() const
{
    auto json = nlohmann::json::object();
    if (!preset.empty())
        json["preset"] = preset;
    if (onB)
        json["onB"] = true;
    if (other)
    {
        json["other"] = fd::toJson(*other);
        if (!otherPreset.empty())
            json["otherPreset"] = otherPreset;
    }
    return json;
}

PresetMemo PresetMemo::fromJson(const nlohmann::json& json)
{
    PresetMemo m;
    if (!json.is_object())
        return m;
    m.preset = json.value("preset", std::string());
    m.onB = json.value("onB", false);
    if (const auto it = json.find("other"); it != json.end())
        if (auto s = stateFromJson(*it))
        {
            m.other = std::move(*s);
            m.otherPreset = json.value("otherPreset", std::string());
        }
    return m;
}

// ---- the controller ------------------------------------------------------------------------

std::vector<const Preset*> PresetController::list() const
{
    return library().presetsFor(info_);
}

const Preset* PresetController::byKey(std::string_view key) const
{
    return library().find(info_, key);
}

const Preset* PresetController::loaded() const
{
    return byKey(host_.memo().preset);
}

std::string PresetController::loadedName() const
{
    const auto key = host_.memo().preset;
    if (key.empty())
        return std::string(defaultSettingsName);
    if (key == "default")
        return std::string(yourDefaultName);
    if (const auto* p = loaded())
        return p->name;
    const auto slash = key.find_last_of('/');
    return slash == std::string::npos ? key : key.substr(slash + 1);
}

bool PresetController::modified() const
{
    const auto* p = loaded();
    return p != nullptr && !sameSound(info_, host_.migrated(p->state), host_.state());
}

bool PresetController::load(const Preset& preset)
{
    if (presetTooNew(info_, preset))
        return false;
    auto memo = host_.memo();
    memo.preset = preset.key();
    const auto name = preset.source == Preset::Source::Default ? std::string(yourDefaultName) : preset.name;
    host_.apply(host_.migrated(preset.state), memo, "Load preset " + name);
    return true;
}

bool PresetController::step(const std::vector<const Preset*>& list, int direction)
{
    if (list.empty() || direction == 0)
        return false;
    const auto key = host_.memo().preset;
    const auto n = static_cast<int>(list.size());
    int at = -1;
    for (int i = 0; i < n; ++i)
        if (list[static_cast<size_t>(i)]->key() == key)
            at = i;
    if (at < 0)
        at = direction > 0 ? -1 : n;
    for (int tries = 0; tries < n; ++tries)
    {
        at = ((at + (direction > 0 ? 1 : -1)) % n + n) % n;
        if (!presetTooNew(info_, *list[static_cast<size_t>(at)]))
            return load(*list[static_cast<size_t>(at)]);
    }
    return false;
}

bool PresetController::revert()
{
    const auto* p = loaded();
    if (p == nullptr || !modified())
        return false;
    host_.apply(host_.migrated(p->state), host_.memo(), "Revert to preset");
    return true;
}

PresetLibrary::Saved PresetController::saveAs(const std::string& name, std::vector<std::string> tags,
                                              std::string description, std::string author, bool replace)
{
    auto saved = library().save(info_, host_.state(), name, std::move(tags), std::move(description),
                                std::move(author), replace);
    if (saved.preset != nullptr)
    {
        auto memo = host_.memo();
        memo.preset = saved.preset->key();
        host_.setMemo(memo);
    }
    return saved;
}

bool PresetController::canSave() const
{
    const auto* p = loaded();
    return p != nullptr && p->isYours();
}

PresetLibrary::Saved PresetController::save()
{
    const auto* p = loaded();
    if (p == nullptr || !p->isYours())
        return {.problem = "Factory presets can't be changed: save it as one of yours."};
    return library().overwrite(info_, *p, host_.state());
}

PresetLibrary::Saved PresetController::rename(const Preset& preset, const std::string& name)
{
    const auto oldKey = preset.key();
    auto renamed = library().rename(info_, preset, name);
    if (renamed.preset != nullptr)
    {
        auto memo = host_.memo();
        bool changed = false;
        if (memo.preset == oldKey)
            memo.preset = renamed.preset->key(), changed = true;
        if (memo.otherPreset == oldKey)
            memo.otherPreset = renamed.preset->key(), changed = true;
        if (changed)
            host_.setMemo(memo);
    }
    return renamed;
}

bool PresetController::remove(const Preset& preset)
{
    return library().remove(info_, preset,
                            [this](const std::filesystem::path& file) { return host_.trash(file); });
}

void PresetController::switchCompare()
{
    auto memo = host_.memo();
    const auto shown = host_.state();
    const auto target = memo.other.value_or(shown);
    const auto targetPreset = memo.other ? memo.otherPreset : memo.preset;
    memo.other = shown;
    memo.otherPreset = memo.preset;
    memo.preset = targetPreset;
    memo.onB = !memo.onB;
    host_.apply(target, memo, memo.onB ? "Compare B" : "Compare A");
}

void PresetController::copyCompare()
{
    auto memo = host_.memo();
    memo.other = host_.state();
    memo.otherPreset = memo.preset;
    host_.apply(host_.state(), memo, memo.onB ? "Copy B to A" : "Copy A to B");
}

bool PresetController::useAsDefault()
{
    return library().saveDefault(info_, host_.state());
}

bool PresetController::clearDefault()
{
    return library().clearDefault(info_);
}

bool PresetController::hasDefault() const
{
    return library().defaultFor(info_) != nullptr;
}

std::optional<std::pair<DeviceState, PresetMemo>> PresetController::startingPoint(const DeviceInfo& info,
                                                                                  PresetLibrary& library)
{
    const auto* d = library.defaultFor(info);
    if (d == nullptr || presetTooNew(info, *d))
        return std::nullopt;
    PresetMemo memo;
    memo.preset = d->key();
    return std::pair{d->state, memo};
}
} // namespace fd
