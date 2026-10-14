#include "model/Browser.h"

#include <algorithm>
#include <cmath>
#include <nlohmann/json.hpp>
#include <sstream>

namespace fd::model
{
namespace
{
using Json = nlohmann::json;

std::string lower(std::string_view text)
{
    std::string out(text);
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c)
                   { return static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c); });
    return out;
}

std::vector<std::string> wordsOf(std::string_view query)
{
    std::vector<std::string> out;
    std::istringstream in{lower(query)};
    for (std::string w; in >> w;)
        out.push_back(w);
    return out;
}

bool isWordStart(const std::string& text, size_t at)
{
    if (at == 0)
        return true;
    const auto c = static_cast<unsigned char>(text[at - 1]);
    return !std::isalnum(c);
}

/// How well `word` matches `text`: 3 at its start, 2 at a word's start, 1 inside, 0 not at all.
int matchIn(const std::string& text, const std::string& word)
{
    int best = 0;
    for (auto at = text.find(word); at != std::string::npos; at = text.find(word, at + 1))
    {
        best = std::max(best, at == 0 ? 3 : isWordStart(text, at) ? 2 : 1);
        if (best == 3)
            break;
    }
    return best;
}

constexpr BrowserKind kKinds[] = {BrowserKind::Module,  BrowserKind::Preset,    BrowserKind::Plugin,
                                  BrowserKind::Sample,  BrowserKind::Wavetable, BrowserKind::Midi,
                                  BrowserKind::Project, BrowserKind::Folder};

const char* kindKey(BrowserKind k)
{
    switch (k)
    {
    case BrowserKind::Module:
        return "module";
    case BrowserKind::Preset:
        return "preset";
    case BrowserKind::Plugin:
        return "plugin";
    case BrowserKind::Sample:
        return "sample";
    case BrowserKind::Wavetable:
        return "wavetable";
    case BrowserKind::Midi:
        return "midi";
    case BrowserKind::Project:
        return "project";
    case BrowserKind::Folder:
        return "folder";
    }
    return "sample";
}

bool lessByName(const std::string& a, const std::string& b)
{
    const auto la = lower(a), lb = lower(b);
    return la != lb ? la < lb : a < b;
}
} // namespace

std::string groupTitle(BrowserKind kind)
{
    switch (kind)
    {
    case BrowserKind::Module:
        return "MODULES";
    case BrowserKind::Preset:
        return "PRESETS";
    case BrowserKind::Plugin:
        return "PLUGINS";
    case BrowserKind::Sample:
        return "SAMPLES";
    case BrowserKind::Wavetable:
        return "WAVETABLES";
    case BrowserKind::Midi:
        return "MIDI FILES";
    case BrowserKind::Project:
        return "PROJECTS";
    case BrowserKind::Folder:
        return "FOLDERS";
    }
    return {};
}

std::string kindPlural(BrowserKind kind)
{
    switch (kind)
    {
    case BrowserKind::Module:
        return "modules";
    case BrowserKind::Preset:
        return "presets";
    case BrowserKind::Plugin:
        return "plugins";
    case BrowserKind::Sample:
        return "samples";
    case BrowserKind::Wavetable:
        return "wavetables";
    case BrowserKind::Midi:
        return "MIDI files";
    case BrowserKind::Project:
        return "projects";
    case BrowserKind::Folder:
        return "folders";
    }
    return {};
}

std::optional<BrowserKind> fileKind(std::string_view path)
{
    const auto dot = path.find_last_of('.');
    const auto slash = path.find_last_of("/\\");
    if (dot == std::string_view::npos || (slash != std::string_view::npos && dot < slash))
        return std::nullopt;
    const auto ext = lower(path.substr(dot + 1));
    if (ext == "wt")
        return BrowserKind::Wavetable;
    for (const auto* e : {"wav", "wave", "aif", "aiff", "flac", "ogg", "mp3"})
        if (ext == e)
        {
            // In a folder named Wavetables (Segno's own, or yours): tables.
            const auto folders = lower(path.substr(0, slash == std::string_view::npos ? 0 : slash + 1));
            for (auto at = folders.find("wavetables"); at != std::string::npos;
                 at = folders.find("wavetables", at + 1))
                if ((at == 0 || folders[at - 1] == '/' || folders[at - 1] == '\\') &&
                    (folders[at + 10] == '/' || folders[at + 10] == '\\'))
                    return BrowserKind::Wavetable;
            return BrowserKind::Sample;
        }
    if (ext == "mid" || ext == "midi")
        return BrowserKind::Midi;
    if (ext == "segno")
        return BrowserKind::Project;
    return std::nullopt;
}

double tempoFromName(std::string_view name)
{
    const auto text = lower(name);
    // "124 bpm", "124bpm", "124-bpm": the number right before "bpm" wins.
    for (auto at = text.find("bpm"); at != std::string::npos; at = text.find("bpm", at + 3))
    {
        auto end = at;
        while (end > 0 && (text[end - 1] == ' ' || text[end - 1] == '_' || text[end - 1] == '-'))
            --end;
        auto start = end;
        while (start > 0 &&
               (std::isdigit(static_cast<unsigned char>(text[start - 1])) || text[start - 1] == '.'))
            --start;
        if (start < end)
            if (const double t = std::atof(text.substr(start, end - start).c_str()); t >= 40.0 && t <= 300.0)
                return t;
    }
    // Otherwise the last number standing alone, if it's a likely tempo: "Loop_90", "Groove 128".
    double last = 0.0;
    for (size_t i = 0; i < text.size();)
    {
        if (!std::isdigit(static_cast<unsigned char>(text[i])))
        {
            ++i;
            continue;
        }
        size_t j = i;
        while (j < text.size() && std::isdigit(static_cast<unsigned char>(text[j])))
            ++j;
        const bool alone = (i == 0 || !std::isalpha(static_cast<unsigned char>(text[i - 1]))) &&
                           (j == text.size() || !std::isalpha(static_cast<unsigned char>(text[j])));
        last = alone ? std::atof(text.substr(i, j - i).c_str()) : 0.0;
        i = j;
    }
    return last >= 60.0 && last <= 200.0 ? last : 0.0;
}

// ---- the index file ------------------------------------------------------------------------

std::string toJson(const BrowserIndexFile& index)
{
    Json places = Json::object();
    for (const auto& [place, files] : index.places)
    {
        Json list = Json::array();
        for (const auto& f : files)
        {
            Json j{{"path", f.path}, {"size", f.size}, {"modified", f.modified}, {"kind", kindKey(f.kind)}};
            if (f.kind == BrowserKind::Sample)
            {
                j["seconds"] = f.seconds;
                j["rate"] = f.sampleRate;
                j["channels"] = f.channels;
                if (f.tempo > 0.0)
                    j["tempo"] = f.tempo;
            }
            if (f.kind == BrowserKind::Wavetable && f.frames > 0)
                j["frames"] = f.frames;
            list.push_back(std::move(j));
        }
        places[place] = std::move(list);
    }
    return Json{{"segnoBrowserIndex", 1}, {"places", std::move(places)}}.dump();
}

BrowserIndexFile browserIndexFromJson(const std::string& json)
{
    BrowserIndexFile out;
    const auto j = Json::parse(json, nullptr, false);
    if (!j.is_object() || !j.contains("places") || !j["places"].is_object())
        return out;
    for (const auto& [place, list] : j["places"].items())
    {
        if (!list.is_array())
            continue;
        auto& files = out.places[place];
        for (const auto& f : list)
        {
            if (!f.is_object() || !f.contains("path") || !f["path"].is_string())
                continue;
            IndexedFile file;
            file.path = f["path"].get<std::string>();
            const auto kind = fileKind(file.path);
            if (!kind)
                continue;
            file.kind = *kind;
            file.size = f.value("size", std::int64_t{0});
            file.modified = f.value("modified", std::int64_t{0});
            file.seconds = f.value("seconds", 0.0);
            file.sampleRate = f.value("rate", 0.0);
            file.channels = f.value("channels", 0);
            file.tempo = f.value("tempo", 0.0);
            file.frames = f.value("frames", 0);
            files.push_back(std::move(file));
        }
    }
    return out;
}

// ---- search --------------------------------------------------------------------------------

void BrowserSearch::setItems(std::vector<BrowserItem> items)
{
    items_ = std::move(items);
    prepared_.clear();
    prepared_.reserve(items_.size());
    for (const auto& item : items_)
    {
        Prepared p;
        p.name = lower(item.name);
        for (const auto& t : item.tags)
            p.tags += lower(t) + " ";
        p.rest = lower(item.caption + " " + item.words);
        prepared_.push_back(std::move(p));
    }
}

std::vector<BrowserGroup> BrowserSearch::search(std::string_view query,
                                                const std::set<std::string>& tags) const
{
    const auto words = wordsOf(query);
    if (words.empty() && tags.empty())
        return {};
    std::set<std::string> wanted;
    for (const auto& t : tags)
        wanted.insert(lower(t));

    struct Hit
    {
        size_t index;
        int score;
    };
    std::map<BrowserKind, std::vector<Hit>> hits;
    for (size_t i = 0; i < items_.size(); ++i)
    {
        const auto& p = prepared_[i];
        if (!wanted.empty())
        {
            bool all = true;
            for (const auto& t : wanted)
                all = all && std::any_of(items_[i].tags.begin(), items_[i].tags.end(),
                                         [&](const std::string& has) { return lower(has) == t; });
            if (!all)
                continue;
        }
        int score = 0;
        bool matched = true;
        for (const auto& w : words)
        {
            // The name counts most; a tag or a word's start next; anywhere else least.
            if (const int n = matchIn(p.name, w); n > 0)
                score += n == 3 ? 100 : n == 2 ? 60 : 30;
            else if (const int t = matchIn(p.tags, w); t > 0)
                score += t >= 2 ? 25 : 10;
            else if (const int r = matchIn(p.rest, w); r > 0)
                score += r >= 2 ? 12 : 5;
            else
            {
                matched = false;
                break;
            }
        }
        if (!matched)
            continue;
        if (p.name == lower(query))
            score += 50; // the whole name
        if (items_[i].favourite)
            score += 15;
        hits[items_[i].kind].push_back({i, score});
    }

    std::vector<std::pair<int, BrowserGroup>> groups;
    for (const auto kind : kKinds)
    {
        auto it = hits.find(kind);
        if (it == hits.end())
            continue;
        auto& list = it->second;
        std::stable_sort(list.begin(), list.end(),
                         [this](const Hit& a, const Hit& b)
                         {
                             return a.score != b.score
                                        ? a.score > b.score
                                        : lessByName(items_[a.index].name, items_[b.index].name);
                         });
        BrowserGroup g{kind, {}};
        g.items.reserve(list.size());
        for (const auto& h : list)
            g.items.push_back(h.index);
        groups.emplace_back(list.front().score, std::move(g));
    }
    // The group with the best match first; on a tie, in kind order.
    std::stable_sort(groups.begin(), groups.end(),
                     [](const auto& a, const auto& b) { return a.first > b.first; });
    std::vector<BrowserGroup> out;
    for (auto& [score, g] : groups)
        out.push_back(std::move(g));
    return out;
}

std::vector<std::string> BrowserSearch::tagsOf(const std::vector<BrowserGroup>& groups) const
{
    std::map<std::string, int> count;
    for (const auto& g : groups)
        for (const auto i : g.items)
            for (const auto& t : items_[i].tags)
                ++count[t];
    std::vector<std::pair<std::string, int>> list(count.begin(), count.end());
    std::stable_sort(list.begin(), list.end(),
                     [](const auto& a, const auto& b) { return a.second > b.second; });
    std::vector<std::string> out;
    for (const auto& [tag, n] : list)
        out.push_back(tag);
    return out;
}

// ---- a module's folder ---------------------------------------------------------------------

PresetFolder modulePresetTree(const std::vector<PresetEntry>& presets, bool instrument)
{
    PresetFolder root;
    const auto byName = [&presets](size_t a, size_t b)
    { return lessByName(presets[a].name, presets[b].name); };

    // Yours, in your folders.
    PresetFolder yours{"Yours", {}, {}};
    for (size_t i = 0; i < presets.size(); ++i)
    {
        if (!presets[i].yours)
            continue;
        auto* at = &yours;
        std::istringstream path(presets[i].folder);
        for (std::string part; std::getline(path, part, '/');)
        {
            if (part.empty())
                continue;
            auto it = std::find_if(at->folders.begin(), at->folders.end(),
                                   [&](const PresetFolder& f) { return f.name == part; });
            if (it == at->folders.end())
            {
                at->folders.push_back({part, {}, {}});
                it = at->folders.end() - 1;
            }
            at = &*it;
        }
        at->presets.push_back(i);
    }
    const auto sortFolder = [&](auto& self, PresetFolder& f) -> void
    {
        std::sort(f.folders.begin(), f.folders.end(),
                  [](const PresetFolder& a, const PresetFolder& b) { return lessByName(a.name, b.name); });
        std::sort(f.presets.begin(), f.presets.end(), byName);
        for (auto& sub : f.folders)
            self(self, sub);
    };
    sortFolder(sortFolder, yours);
    if (!yours.folders.empty() || !yours.presets.empty())
        root.folders.push_back(std::move(yours));

    // The factory set: an instrument's by kind of sound (its first tag), an effect's as one list.
    std::vector<PresetFolder> kinds;
    for (size_t i = 0; i < presets.size(); ++i)
    {
        if (presets[i].yours)
            continue;
        if (!instrument || presets[i].tags.empty())
        {
            root.presets.push_back(i);
            continue;
        }
        const auto& kind = presets[i].tags.front();
        auto it =
            std::find_if(kinds.begin(), kinds.end(), [&](const PresetFolder& f) { return f.name == kind; });
        if (it == kinds.end())
        {
            kinds.push_back({kind, {}, {}});
            it = kinds.end() - 1;
        }
        it->presets.push_back(i);
    }
    for (auto& k : kinds)
        std::sort(k.presets.begin(), k.presets.end(), byName);
    std::sort(kinds.begin(), kinds.end(),
              [](const PresetFolder& a, const PresetFolder& b) { return lessByName(a.name, b.name); });
    root.folders.insert(root.folders.end(), kinds.begin(), kinds.end());
    std::sort(root.presets.begin(), root.presets.end(), byName);
    return root;
}
} // namespace fd::model
