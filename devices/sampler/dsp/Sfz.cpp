#include "Sfz.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <fstream>
#include <map>
#include <sstream>
#include <utility>

namespace fd::devices::sfz
{
namespace
{
namespace fs = std::filesystem;
using Opcodes = std::vector<std::pair<std::string, std::string>>;

std::string lower(std::string_view s)
{
    std::string out(s);
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

std::string_view trim(std::string_view s)
{
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front())) != 0)
        s.remove_prefix(1);
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back())) != 0)
        s.remove_suffix(1);
    return s;
}

bool isNameChar(char c)
{
    return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_';
}

template <typename T> std::optional<T> number(std::string_view s)
{
    s = trim(s);
    if (!s.empty() && s.front() == '+')
        s.remove_prefix(1);
    T v{};
    const auto [end, ec] = std::from_chars(s.data(), s.data() + s.size(), v);
    if (ec != std::errc{} || end == s.data())
        return std::nullopt;
    return v;
}

/// Comments out, `#define`d names put in, `#include`s noted.
std::string prepare(std::string_view text, std::vector<std::string>& ignored)
{
    std::string out;
    out.reserve(text.size());
    for (size_t i = 0; i < text.size();)
    {
        if (text.compare(i, 2, "//") == 0)
        {
            while (i < text.size() && text[i] != '\n')
                ++i;
        }
        else if (text.compare(i, 2, "/*") == 0)
        {
            const auto close = text.find("*/", i + 2);
            i = close == std::string_view::npos ? text.size() : close + 2;
            out += ' ';
        }
        else
            out += text[i++];
    }
    // #define $NAME value, then $NAME anywhere after it.
    std::vector<std::pair<std::string, std::string>> defines;
    std::string result;
    std::istringstream lines(out);
    for (std::string line; std::getline(lines, line);)
    {
        const auto t = trim(line);
        if (t.starts_with("#define"))
        {
            std::istringstream words{std::string(t.substr(7))};
            std::string name, value;
            words >> name;
            std::getline(words, value);
            if (name.starts_with('$'))
            {
                defines.emplace_back(name, std::string(trim(value)));
                // Longest names first, so $A doesn't eat $AB.
                std::stable_sort(defines.begin(), defines.end(), [](const auto& a, const auto& b)
                                 { return a.first.size() > b.first.size(); });
            }
            continue;
        }
        if (t.starts_with("#include"))
        {
            ignored.emplace_back("#include");
            continue;
        }
        for (const auto& [name, value] : defines)
            for (size_t at = line.find(name); at != std::string::npos;
                 at = line.find(name, at + value.size()))
                line.replace(at, name.size(), value);
        result += line;
        result += '\n';
    }
    return result;
}

enum class Level
{
    None,
    Control,
    Global,
    Master,
    Group,
    Region,
    Other, // a header the Sampler doesn't read (<curve>, <effect>…): its opcodes are skipped
};

void note(std::vector<std::string>& ignored, std::string name)
{
    // A numbered family once: locc64 and locc1 are both "loccN".
    if (const auto last = name.find_last_not_of("0123456789");
        last != std::string::npos && last + 1 < name.size())
        name = name.substr(0, last + 1) + "N";
    if (std::find(ignored.begin(), ignored.end(), name) == ignored.end())
        ignored.push_back(std::move(name));
}

/// An opcode that makes a region play only on a condition the Sampler doesn't read.
bool isCondition(const std::string& name)
{
    for (const auto* prefix : {"sw_", "locc", "hicc", "on_locc", "on_hicc", "start_locc", "start_hicc",
                               "lobend", "hibend", "lochan", "hichan", "loprog", "hiprog", "lochanaft",
                               "hichanaft", "lopolyaft", "hipolyaft", "lobpm", "hibpm"})
        if (name.starts_with(prefix))
            return true;
    return false;
}

/// Opcodes read with no effect here: names and labels, and ARIA's envelope update rate.
bool isHarmless(const std::string& name)
{
    return name.ends_with("_label") || name == "ampeg_dynamic" || name == "sw_label";
}

LoopMode loopModeOf(std::string_view v)
{
    const auto s = lower(trim(v));
    if (s == "no_loop")
        return LoopMode::NoLoop;
    if (s == "one_shot")
        return LoopMode::OneShot;
    if (s == "loop_continuous")
        return LoopMode::Continuous;
    if (s == "loop_sustain")
        return LoopMode::Sustain;
    return LoopMode::Unset;
}

/// The value most regions give `opcode`; nullopt when most give none.
std::optional<double> commonValue(const std::vector<Opcodes>& regions, const std::string& opcode)
{
    std::map<std::optional<double>, int> counts;
    for (const auto& r : regions)
    {
        std::optional<double> v;
        for (const auto& [name, value] : r)
            if (name == opcode)
                v = number<double>(value);
        ++counts[v];
    }
    const auto best = std::max_element(counts.begin(), counts.end(),
                                       [](const auto& a, const auto& b) { return a.second < b.second; });
    return best == counts.end() ? std::nullopt : best->first;
}
} // namespace

std::optional<int> noteNumber(std::string_view text)
{
    text = trim(text);
    if (text.empty())
        return std::nullopt;
    if (const auto n = number<int>(text);
        n && text.find_first_not_of("+-0123456789") == std::string_view::npos)
        return std::clamp(*n, 0, 127);
    static constexpr int kPitch[] = {9, 11, 0, 2, 4, 5, 7}; // a b c d e f g
    const char letter = static_cast<char>(std::tolower(static_cast<unsigned char>(text.front())));
    if (letter < 'a' || letter > 'g')
        return std::nullopt;
    int pc = kPitch[letter - 'a'];
    text.remove_prefix(1);
    if (!text.empty() && (text.front() == '#' || text.front() == 's'))
    {
        ++pc;
        text.remove_prefix(1);
    }
    else if (!text.empty() && text.front() == 'b')
    {
        --pc;
        text.remove_prefix(1);
    }
    const auto octave = number<int>(text);
    if (!octave)
        return std::nullopt;
    return std::clamp((*octave + 1) * 12 + pc, 0, 127); // SFZ's c4 is 60
}

fs::path findFile(const fs::path& path)
{
    std::error_code ec;
    if (fs::is_regular_file(path, ec))
        return path;
    // Part by part, ignoring case where the part isn't there as written.
    fs::path found = path.root_path();
    for (const auto& part : path.relative_path())
    {
        if (fs::exists(found / part, ec))
        {
            found /= part;
            continue;
        }
        const auto want = lower(part.string());
        bool matched = false;
        for (const auto& entry : fs::directory_iterator(found.empty() ? fs::path(".") : found, ec))
            if (lower(entry.path().filename().string()) == want)
            {
                found = entry.path();
                matched = true;
                break;
            }
        if (!matched)
            return {};
    }
    return fs::is_regular_file(found, ec) ? found : fs::path();
}

File parse(std::string_view source, const fs::path& folder)
{
    File file;
    const auto text = prepare(source, file.ignored);

    // Headers and opcodes, in order.
    Opcodes control, global, master, group, region;
    std::vector<Opcodes> regions;
    Level level = Level::None;
    const auto closeRegion = [&]
    {
        if (level != Level::Region)
            return;
        Opcodes merged = control;
        for (const auto* part : {&global, &master, &group, &region})
            merged.insert(merged.end(), part->begin(), part->end());
        regions.push_back(std::move(merged));
        region.clear();
    };

    for (size_t i = 0; i < text.size();)
    {
        if (std::isspace(static_cast<unsigned char>(text[i])) != 0)
        {
            ++i;
            continue;
        }
        if (text[i] == '<')
        {
            const auto close = text.find('>', i);
            if (close == std::string::npos)
                break;
            const auto name = lower(trim(std::string_view(text).substr(i + 1, close - i - 1)));
            i = close + 1;
            closeRegion();
            if (name == "control")
            {
                level = Level::Control;
                control.clear();
            }
            else if (name == "global")
            {
                level = Level::Global;
                global.clear();
                master.clear();
                group.clear();
            }
            else if (name == "master")
            {
                level = Level::Master;
                master.clear();
                group.clear();
            }
            else if (name == "group")
            {
                level = Level::Group;
                group.clear();
            }
            else if (name == "region")
                level = Level::Region;
            else
            {
                level = Level::Other;
                note(file.ignored, "<" + name + ">");
            }
            continue;
        }
        // name=value, the value up to the next name= or header or the line's end.
        size_t n = i;
        while (n < text.size() && isNameChar(text[n]))
            ++n;
        if (n == i || n >= text.size() || text[n] != '=')
        {
            while (i < text.size() && std::isspace(static_cast<unsigned char>(text[i])) == 0)
                ++i; // not an opcode: skipped
            continue;
        }
        const auto name = lower(std::string_view(text).substr(i, n - i));
        size_t end = n + 1;
        while (end < text.size() && text[end] != '\n' && text[end] != '<')
        {
            if (std::isspace(static_cast<unsigned char>(text[end])) != 0)
            {
                size_t k = end;
                while (k < text.size() && (text[k] == ' ' || text[k] == '\t'))
                    ++k;
                size_t m = k;
                while (m < text.size() && isNameChar(text[m]))
                    ++m;
                if (m > k && m < text.size() && text[m] == '=')
                    break; // the next opcode
            }
            ++end;
        }
        std::string value(trim(std::string_view(text).substr(n + 1, end - n - 1)));
        i = end;
        switch (level)
        {
        case Level::Control:
            control.emplace_back(name, std::move(value));
            break;
        case Level::Global:
            global.emplace_back(name, std::move(value));
            break;
        case Level::Master:
            master.emplace_back(name, std::move(value));
            break;
        case Level::Group:
            group.emplace_back(name, std::move(value));
            break;
        case Level::Region:
            region.emplace_back(name, std::move(value));
            break;
        case Level::None:
        case Level::Other:
            break;
        }
    }
    closeRegion();

    // Each region, read.
    std::vector<Opcodes> played;
    for (const auto& opcodes : regions)
    {
        Region r;
        std::string defaultPath, sample;
        int octaveOffset = 0, noteOffset = 0;
        bool skip = false;
        for (const auto& [name, value] : opcodes)
            if (name == "octave_offset")
                octaveOffset = number<int>(value).value_or(0);
            else if (name == "note_offset")
                noteOffset = number<int>(value).value_or(0);
        const auto key = [&](const std::string& v) -> std::optional<int>
        {
            const auto k = noteNumber(v);
            return k ? std::optional<int>(std::clamp(*k + 12 * octaveOffset + noteOffset, 0, 127))
                     : std::nullopt;
        };
        for (const auto& [name, value] : opcodes)
        {
            if (name == "sample")
                sample = value;
            else if (name == "default_path")
                defaultPath = value;
            else if (name == "lokey")
                r.loKey = key(value).value_or(r.loKey);
            else if (name == "hikey")
                r.hiKey = key(value).value_or(r.hiKey);
            else if (name == "key")
            {
                if (const auto k = key(value))
                    r.loKey = r.hiKey = r.root = *k;
            }
            else if (name == "pitch_keycenter")
            {
                if (lower(value) == "sample")
                    r.rootFromSample = true;
                else if (const auto k = key(value))
                {
                    r.root = *k;
                    r.rootFromSample = false;
                }
            }
            else if (name == "lovel")
                r.loVel = std::clamp(number<int>(value).value_or(r.loVel), 0, 127);
            else if (name == "hivel")
                r.hiVel = std::clamp(number<int>(value).value_or(r.hiVel), 0, 127);
            else if (name == "volume")
                r.volume = number<double>(value).value_or(0.0);
            else if (name == "tune" || name == "pitch")
                r.tune = number<double>(value).value_or(0.0);
            else if (name == "transpose")
                r.transpose = number<int>(value).value_or(0);
            else if (name == "offset")
                r.offset = std::max<std::int64_t>(0, number<std::int64_t>(value).value_or(0));
            else if (name == "end")
                r.end = number<std::int64_t>(value).value_or(-1);
            else if (name == "loop_mode" || name == "loopmode")
                r.loopMode = loopModeOf(value);
            else if (name == "loop_start" || name == "loopstart")
                r.loopStart = number<std::int64_t>(value).value_or(-1);
            else if (name == "loop_end" || name == "loopend")
                r.loopEnd = number<std::int64_t>(value).value_or(-1);
            else if (name == "seq_length")
                r.seqLength = std::max(1, number<int>(value).value_or(1));
            else if (name == "seq_position")
                r.seqPosition = std::max(1, number<int>(value).value_or(1));
            else if (name == "lorand")
                r.loRand = number<double>(value).value_or(0.0);
            else if (name == "hirand")
                r.hiRand = number<double>(value).value_or(1.0);
            else if (name == "trigger")
            {
                if (lower(value) != "attack")
                {
                    skip = true;
                    note(file.ignored, "trigger=" + lower(value));
                }
            }
            else if (isCondition(name))
            {
                skip = true;
                note(file.ignored, name);
            }
            else if (name.starts_with("ampeg_") && (name == "ampeg_attack" || name == "ampeg_decay" ||
                                                    name == "ampeg_sustain" || name == "ampeg_release"))
            {
                // the whole file's, below
            }
            else if (name == "amp_veltrack" || name == "octave_offset" || name == "note_offset" ||
                     isHarmless(name))
            {
            }
            else
                note(file.ignored, name);
        }
        if (sample.empty())
            continue;
        if (sample.front() == '*')
        {
            note(file.ignored, "sample=" + sample); // a generator (*sine, *noise)
            continue;
        }
        if (skip)
            continue;
        std::replace(defaultPath.begin(), defaultPath.end(), '\\', '/');
        std::replace(sample.begin(), sample.end(), '\\', '/');
        r.sample = (folder / defaultPath / sample).lexically_normal();
        if (r.loKey > r.hiKey)
            std::swap(r.loKey, r.hiKey);
        if (r.loVel > r.hiVel)
            std::swap(r.loVel, r.hiVel);
        r.seqPosition = std::min(r.seqPosition, r.seqLength);
        file.regions.push_back(std::move(r));
        played.push_back(opcodes);
    }

    // What the whole instrument says: the value most regions have.
    file.ampAttack = commonValue(played, "ampeg_attack");
    file.ampDecay = commonValue(played, "ampeg_decay");
    file.ampSustain = commonValue(played, "ampeg_sustain");
    file.ampRelease = commonValue(played, "ampeg_release");
    file.ampVeltrack = commonValue(played, "amp_veltrack");
    if (!file.regions.empty() && std::all_of(file.regions.begin(), file.regions.end(), [&](const Region& r)
                                             { return r.loopMode == file.regions.front().loopMode; }))
        file.loopMode = file.regions.front().loopMode;
    return file;
}

std::optional<File> read(const fs::path& path, std::string* error)
{
    std::ifstream in(path, std::ios::binary);
    if (!in)
    {
        if (error != nullptr)
            *error = "cannot open " + path.string();
        return std::nullopt;
    }
    std::ostringstream text;
    text << in.rdbuf();
    return parse(text.str(), path.parent_path());
}
} // namespace fd::devices::sfz
