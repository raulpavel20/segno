#include "mcp/Conventions.h"

#include "model/MusicalTime.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <cmath>
#include <vector>

namespace fd::mcp
{
namespace
{
std::string trim(std::string s)
{
    const auto notSpace = [](unsigned char c) { return !std::isspace(c); };
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), notSpace));
    s.erase(std::find_if(s.rbegin(), s.rend(), notSpace).base(), s.end());
    return s;
}

std::optional<double> number(const std::string& s)
{
    if (s.empty())
        return std::nullopt;
    double v = 0.0;
    const auto* end = s.data() + s.size();
    const auto [ptr, ec] = std::from_chars(s.data(), end, v);
    if (ec != std::errc() || ptr != end || !std::isfinite(v))
        return std::nullopt;
    return v;
}
} // namespace

std::string formatId(IdKind kind, std::uint64_t id)
{
    return std::string(1, static_cast<char>(kind)) + ":" + std::to_string(id);
}

std::optional<std::uint64_t> parseId(const Json& value, IdKind kind)
{
    if (value.is_number_unsigned())
        return value.get<std::uint64_t>();
    if (value.is_number_integer() && value.get<std::int64_t>() > 0)
        return static_cast<std::uint64_t>(value.get<std::int64_t>());
    if (!value.is_string())
        return std::nullopt;
    auto s = trim(value.get<std::string>());
    const std::string prefix = std::string(1, static_cast<char>(kind)) + ":";
    if (s.starts_with(prefix))
        s = s.substr(prefix.size());
    else if (s.size() > 1 && s[1] == ':')
        return std::nullopt; // another kind's id
    std::uint64_t id = 0;
    const auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), id);
    if (ec != std::errc() || ptr != s.data() + s.size() || id == 0)
        return std::nullopt;
    return id;
}

std::optional<double> parsePosition(const Json& value, const model::TempoMap& tempo, std::string* error)
{
    const auto fail = [&]() -> std::optional<double> {
        if (error != nullptr)
            *error = "Positions are beats (a number), \"bar.beat.sixteenth\" such as \"5.1.1\", or a length "
                     "such as \"4 bars\"; got " +
                     value.dump();
        return std::nullopt;
    };
    if (value.is_number())
    {
        const double v = value.get<double>();
        return std::isfinite(v) && v >= 0.0 ? std::optional(v) : fail();
    }
    if (!value.is_string())
        return fail();
    if (auto v = model::positionFromText(value.get<std::string>(), tempo))
        return v;
    return fail();
}

std::optional<double> parseLength(const Json& value, const model::TempoMap& tempo, std::string* error)
{
    const auto fail = [&]() -> std::optional<double> {
        if (error != nullptr)
            *error = "Lengths are beats (a number), or text such as \"2 bars\", \"3 beats\" or \"1/16\"; got " +
                     value.dump();
        return std::nullopt;
    };
    if (value.is_number())
    {
        const double v = value.get<double>();
        return std::isfinite(v) && v > 0.0 ? std::optional(v) : fail();
    }
    if (!value.is_string())
        return fail();
    if (auto v = model::lengthFromText(value.get<std::string>(), tempo))
        return v;
    return fail();
}

std::string formatPosition(double beats, const model::TempoMap& tempo)
{
    return model::positionText(beats, tempo);
}

Json positionJson(double beats, const model::TempoMap& tempo)
{
    return {{"beats", std::round(beats * 1e6) / 1e6}, {"bbt", formatPosition(beats, tempo)}};
}

std::optional<int> parsePitch(const Json& value)
{
    if (value.is_number_integer())
    {
        const auto p = value.get<std::int64_t>();
        return p >= 0 && p <= 127 ? std::optional(static_cast<int>(p)) : std::nullopt;
    }
    if (!value.is_string())
        return std::nullopt;
    const auto s = trim(value.get<std::string>());
    if (s.empty())
        return std::nullopt;
    if (const auto n = number(s))
        return *n >= 0 && *n <= 127 && std::floor(*n) == *n ? std::optional(static_cast<int>(*n)) : std::nullopt;
    static constexpr std::array<int, 7> steps{9, 11, 0, 2, 4, 5, 7}; // A B C D E F G
    const char letter = static_cast<char>(std::toupper(static_cast<unsigned char>(s[0])));
    if (letter < 'A' || letter > 'G')
        return std::nullopt;
    int semitone = steps[static_cast<size_t>(letter - 'A')];
    size_t i = 1;
    for (; i < s.size() && (s[i] == '#' || s[i] == 'b'); ++i)
        semitone += s[i] == '#' ? 1 : -1;
    const auto octave = number(s.substr(i));
    if (!octave || std::floor(*octave) != *octave)
        return std::nullopt;
    const int pitch = (static_cast<int>(*octave) + 2) * 12 + semitone; // C3 = 60
    return pitch >= 0 && pitch <= 127 ? std::optional(pitch) : std::nullopt;
}

std::string pitchName(int pitch)
{
    static constexpr std::array<const char*, 12> names{"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    const int p = std::clamp(pitch, 0, 127);
    return std::string(names[static_cast<size_t>(p % 12)]) + std::to_string(p / 12 - 2);
}

int velocityToMidi(float velocity) noexcept
{
    return std::clamp(static_cast<int>(std::lround(velocity * 127.0f)), 1, 127);
}

float velocityFromMidi(double velocity) noexcept
{
    return static_cast<float>(std::clamp(std::round(velocity), 1.0, 127.0) / 127.0);
}

double roundTime(double value) noexcept
{
    return std::round(value * 1e6) / 1e6;
}
} // namespace fd::mcp
