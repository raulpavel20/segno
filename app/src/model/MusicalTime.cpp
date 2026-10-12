#include "model/MusicalTime.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <cmath>
#include <sstream>
#include <vector>

namespace fd::model
{
namespace
{
constexpr double kEps = 1e-6;

std::string trim(std::string s)
{
    const auto notSpace = [](unsigned char c) { return !std::isspace(c); };
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), notSpace));
    s.erase(std::find_if(s.rbegin(), s.rend(), notSpace).base(), s.end());
    return s;
}

std::string lower(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
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

std::vector<std::string> split(const std::string& s, char sep)
{
    std::vector<std::string> parts;
    std::string current;
    for (char c : s)
        if (c == sep)
        {
            parts.push_back(current);
            current.clear();
        }
        else
            current += c;
    parts.push_back(current);
    return parts;
}

/// "N bars", "N beats", "N sixteenths", "1 bar 2 beats", "a/b": nullopt if it isn't one of those.
std::optional<double> unitLength(const std::string& text, const TempoMap& tempo)
{
    if (const auto slash = text.find('/'); slash != std::string::npos)
    {
        const auto a = number(trim(text.substr(0, slash))), b = number(trim(text.substr(slash + 1)));
        if (a && b && *b > 0.0)
            return *a / *b * 4.0;
        return std::nullopt;
    }
    // Amounts each followed by a unit.
    std::vector<std::string> words;
    std::istringstream in(text);
    for (std::string w; in >> w;)
        words.push_back(w);
    if (words.empty() || words.size() % 2 != 0)
        return std::nullopt;
    double beats = 0.0;
    for (size_t i = 0; i < words.size(); i += 2)
    {
        const auto amount = number(words[i]);
        const auto& unit = words[i + 1];
        if (!amount)
            return std::nullopt;
        if (unit == "bar" || unit == "bars")
            beats += *amount * tempo.beatsPerBar();
        else if (unit == "beat" || unit == "beats")
            beats += *amount;
        else if (unit == "sixteenth" || unit == "sixteenths")
            beats += *amount * 0.25;
        else
            return std::nullopt;
    }
    return beats;
}

/// "a.b.c": bars, beats (of the signature) and sixteenths. `base` is 1 for positions, 0 for
/// lengths.
std::optional<double> barsBeats(const std::string& text, const TempoMap& tempo, int base)
{
    const auto parts = split(text, '.');
    if (parts.size() > 3)
        return std::nullopt;
    std::array<double, 3> v{static_cast<double>(base), static_cast<double>(base), static_cast<double>(base)};
    for (size_t i = 0; i < parts.size(); ++i)
    {
        const auto n = number(trim(parts[i]));
        if (!n || *n < base)
            return std::nullopt;
        v[i] = *n;
    }
    const double unit = 4.0 / tempo.denominator; // quarter notes per signature beat
    return (v[0] - base) * tempo.beatsPerBar() + (v[1] - base) * unit + (v[2] - base) * 0.25;
}

std::string count(double n, const char* one, const char* many)
{
    std::ostringstream out;
    if (std::abs(n - std::round(n)) < kEps)
        out << std::llround(n);
    else
        out << std::round(n * 100.0) / 100.0;
    out << ' ' << (std::abs(n - 1.0) < kEps ? one : many);
    return out.str();
}
} // namespace

std::string lengthText(double beats, const TempoMap& tempo)
{
    const double perBar = tempo.beatsPerBar();
    const double bars = std::floor(beats / perBar + kEps);
    const double rest = beats - bars * perBar;
    if (bars >= 1.0 && rest < kEps)
        return count(bars, "bar", "bars");
    if (bars < 1.0)
        return count(rest, "beat", "beats");
    return count(bars, "bar", "bars") + " " + count(rest, "beat", "beats");
}
} // namespace fd::model

namespace fd::model
{
std::string positionText(double beats, const TempoMap& tempo)
{
    const double perBar = tempo.beatsPerBar();
    const double unit = 4.0 / tempo.denominator;
    const double q = std::max(0.0, beats) + 1e-9;
    const auto bar = static_cast<long long>(std::floor(q / perBar));
    const double inBar = q - static_cast<double>(bar) * perBar;
    const auto beat = static_cast<long long>(std::floor(inBar / unit));
    const double inBeat = inBar - static_cast<double>(beat) * unit;
    const auto sixteenth = static_cast<long long>(std::floor(inBeat / 0.25));
    const auto hundredths = static_cast<long long>(std::floor((inBeat - static_cast<double>(sixteenth) * 0.25) / 0.25 * 100.0));
    auto s = std::to_string(bar + 1) + "." + std::to_string(beat + 1) + "." + std::to_string(sixteenth + 1);
    if (hundredths > 0)
        s += "." + std::string(hundredths < 10 ? "0" : "") + std::to_string(hundredths);
    return s;
}

std::optional<double> positionFromText(const std::string& text, const TempoMap& tempo)
{
    const auto t = lower(trim(text));
    if (const auto v = unitLength(t, tempo))
        return *v >= 0.0 ? v : std::nullopt;
    return barsBeats(t, tempo, 1);
}

std::optional<double> lengthFromText(const std::string& text, const TempoMap& tempo)
{
    const auto t = lower(trim(text));
    auto v = unitLength(t, tempo);
    if (!v && std::count(t.begin(), t.end(), '.') == 2)
        v = barsBeats(t, tempo, 0);
    if (!v)
        v = number(t);
    return v && *v > 0.0 ? v : std::nullopt;
}
} // namespace fd::model
