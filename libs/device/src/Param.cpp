#include "fd/device/Param.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstdio>

namespace fd
{
namespace
{
double skewFor(const ParamSpec& s) noexcept
{
    if (s.kind != ParamKind::Continuous || !(s.skewCentre > s.min && s.skewCentre < s.max))
        return 1.0;
    return std::log(0.5) / std::log((static_cast<double>(s.skewCentre) - s.min) / (static_cast<double>(s.max) - s.min));
}

std::string formatNumber(double v, int decimals)
{
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.*f", decimals, v);
    std::string out(buf);
    if (out == "-0" || (out.starts_with("-0.") && std::all_of(out.begin() + 3, out.end(), [](char c) { return c == '0'; })))
        out.erase(0, 1); // avoid "-0.0"
    return out;
}

std::string lower(std::string_view s)
{
    std::string out(s);
    for (auto& c : out)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

std::string_view trim(std::string_view s)
{
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front())))
        s.remove_prefix(1);
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back())))
        s.remove_suffix(1);
    return s;
}

/// Parses a leading number; returns the value and the remaining text.
std::optional<std::pair<double, std::string_view>> parseLeadingNumber(std::string_view s)
{
    s = trim(s);
    if (!s.empty() && s.front() == '+')
        s.remove_prefix(1);
    double value = 0.0;
    const auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), value);
    if (ec != std::errc{})
        return std::nullopt;
    return std::pair{value, trim(std::string_view(ptr, static_cast<size_t>(s.data() + s.size() - ptr)))};
}
} // namespace

int ParamSpec::numSteps() const noexcept
{
    switch (kind)
    {
    case ParamKind::Continuous:
        return 0;
    case ParamKind::Toggle:
        return 2;
    case ParamKind::Choice:
        return static_cast<int>(choices.size());
    case ParamKind::Discrete:
        return static_cast<int>(std::lround(max - min)) + 1;
    }
    return 0;
}

float ParamSpec::sanitize(float plain) const noexcept
{
    if (!std::isfinite(plain))
        plain = def;
    plain = std::clamp(plain, min, max);
    if (isStepped())
        plain = std::round(plain);
    return plain;
}

float ParamSpec::toNormalized(float plain) const noexcept
{
    if (max <= min)
        return 0.0f;
    const double p = (static_cast<double>(sanitize(plain)) - min) / (static_cast<double>(max) - min);
    const double skew = skewFor(*this);
    return static_cast<float>(skew == 1.0 ? p : std::pow(p, skew));
}

float ParamSpec::fromNormalized(float normalized) const noexcept
{
    double p = std::clamp(static_cast<double>(normalized), 0.0, 1.0);
    const double skew = skewFor(*this);
    if (skew != 1.0 && p > 0.0)
        p = std::exp(std::log(p) / skew);
    return sanitize(static_cast<float>(min + (static_cast<double>(max) - min) * p));
}

std::string ParamSpec::toText(float plain) const
{
    const float v = sanitize(plain);
    switch (kind)
    {
    case ParamKind::Toggle:
        return v >= 0.5f ? "On" : "Off";
    case ParamKind::Choice:
    {
        const auto i = static_cast<size_t>(v);
        return i < choices.size() ? std::string(choices[i]) : std::to_string(i);
    }
    default:
        break;
    }

    switch (unit)
    {
    case Unit::Decibels:
        if (hasFlag(ParamFlags::MinusInfinityAtMin) && v <= min)
            return "-inf dB";
        return formatNumber(v, decimals) + " dB";
    case Unit::Hertz:
        if (v >= 1000.0f)
            return formatNumber(v / 1000.0, 2) + " kHz";
        // Whole hertz from 100 Hz on, unless the range is narrow enough for a tenth to matter (a
        // tuner's reference: 440.0 Hz).
        return formatNumber(v, v < 100.0f || max - min <= 100.0f ? decimals : 0) + " Hz";
    case Unit::Milliseconds:
        if (v >= 1000.0f)
            return formatNumber(v / 1000.0, 2) + " s";
        return formatNumber(v, v >= 100.0f ? 0 : decimals) + " ms";
    case Unit::Seconds:
        return formatNumber(v, decimals) + " s";
    case Unit::Percent:
        return formatNumber(v, decimals) + " %";
    case Unit::Semitones:
        return (v > 0 ? "+" : "") + formatNumber(v, decimals) + " st";
    case Unit::Cents:
        return (v > 0 ? "+" : "") + formatNumber(v, decimals) + " ct";
    case Unit::Ratio:
        if (hasFlag(ParamFlags::InfinityAtMax) && v >= max)
            return "\xE2\x88\x9E:1"; // infinity sign (UTF-8)
        return formatNumber(v, decimals) + ":1";
    case Unit::Degrees:
        return formatNumber(v, decimals) + "\xC2\xB0"; // degree sign (UTF-8)
    case Unit::Note:
    {
        static constexpr const char* names[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
        const long n = std::lround(v);
        return std::string(names[((n % 12) + 12) % 12]) + std::to_string(static_cast<long>(std::floor(n / 12.0)) - 2);
    }
    case Unit::Pan:
    {
        const long amount = std::lround(std::abs(v) * 50.0f);
        if (amount == 0)
            return "C";
        return std::to_string(amount) + (v < 0 ? "L" : "R");
    }
    case Unit::Lufs:
        return formatNumber(v, decimals) + " LUFS";
    case Unit::Dbtp:
        return formatNumber(v, decimals) + " dBTP";
    case Unit::None:
        break;
    }
    return formatNumber(v, kind == ParamKind::Discrete ? 0 : decimals);
}

std::optional<float> ParamSpec::fromText(std::string_view text) const
{
    const std::string t = lower(trim(text));
    if (t.empty())
        return std::nullopt;

    if (kind == ParamKind::Toggle)
    {
        if (t == "on" || t == "1" || t == "true" || t == "yes")
            return 1.0f;
        if (t == "off" || t == "0" || t == "false" || t == "no")
            return 0.0f;
        return std::nullopt;
    }
    if (kind == ParamKind::Choice)
    {
        for (size_t i = 0; i < choices.size(); ++i)
            if (lower(choices[i]) == t)
                return static_cast<float>(i);
        if (auto n = parseLeadingNumber(t); n && n->second.empty())
            return sanitize(static_cast<float>(n->first));
        return std::nullopt;
    }
    if (unit == Unit::Decibels && (t == "-inf" || t == "-inf db" || t == "-inf dB"))
        return min;
    if (hasFlag(ParamFlags::InfinityAtMax) && (t.starts_with("inf") || t.starts_with("\xE2\x88\x9E")))
        return max;
    if (unit == Unit::Note && t[0] >= 'a' && t[0] <= 'g')
    {
        static constexpr int semis[] = {9, 11, 0, 2, 4, 5, 7}; // a b c d e f g
        int note = semis[t[0] - 'a'];
        size_t i = 1;
        if (i < t.size() && (t[i] == '#' || t[i] == 'b'))
            note += t[i++] == '#' ? 1 : -1;
        auto octave = parseLeadingNumber(std::string_view(t).substr(i));
        if (!octave || !octave->second.empty())
            return std::nullopt;
        return sanitize(static_cast<float>(note + 12 * (octave->first + 2)));
    }
    if (unit == Unit::Pan)
    {
        if (t == "c" || t == "center" || t == "centre")
            return 0.0f;
        auto n = parseLeadingNumber(t);
        if (!n)
            return std::nullopt;
        if (n->second == "l")
            return sanitize(static_cast<float>(-n->first / 50.0));
        if (n->second == "r")
            return sanitize(static_cast<float>(n->first / 50.0));
        if (n->second.empty())
            return sanitize(static_cast<float>(n->first / 50.0));
        return std::nullopt;
    }

    auto n = parseLeadingNumber(t);
    if (!n)
        return std::nullopt;
    double value = n->first;
    const std::string_view suffix = n->second;
    if (unit == Unit::Hertz && (suffix == "k" || suffix == "khz"))
        value *= 1000.0;
    else if (unit == Unit::Seconds && suffix == "ms")
        value /= 1000.0;
    else if (unit == Unit::Milliseconds && suffix == "s")
        value *= 1000.0;
    return sanitize(static_cast<float>(value));
}
} // namespace fd
