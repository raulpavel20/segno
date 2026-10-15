#include "model/Export.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>

namespace fd::model
{
namespace
{
template <typename E>
E enumFrom(const nlohmann::json& j, const char* key, std::initializer_list<std::pair<const char*, E>> names, E fallback)
{
    if (!j.contains(key) || !j[key].is_string())
        return fallback;
    const auto text = j[key].get<std::string>();
    for (const auto& [name, value] : names)
        if (text == name)
            return value;
    return fallback;
}

template <typename E>
std::string nameOf(E value, std::initializer_list<std::pair<const char*, E>> names)
{
    for (const auto& [name, v] : names)
        if (v == value)
            return name;
    return {};
}

const std::initializer_list<std::pair<const char*, ExportWhat>> kWhat{
    {"mix", ExportWhat::Mix}, {"selected", ExportWhat::SelectedTracks}, {"all", ExportWhat::AllTracks}};
const std::initializer_list<std::pair<const char*, ExportRange>> kRange{{"song", ExportRange::Song}, {"loop", ExportRange::Loop}};
const std::initializer_list<std::pair<const char*, ExportFormat>> kFormat{
    {"wav", ExportFormat::Wav}, {"flac", ExportFormat::Flac}, {"aiff", ExportFormat::Aiff},
    {"mp3", ExportFormat::Mp3}, {"ogg", ExportFormat::Ogg},   {"opus", ExportFormat::Opus}};
const std::initializer_list<std::pair<const char*, ExportNormalise>> kNormalise{
    {"off", ExportNormalise::Off}, {"peak", ExportNormalise::Peak}, {"loudness", ExportNormalise::Loudness}};

double levelOf(float db)
{
    return std::pow(10.0, static_cast<double>(db) / 20.0);
}
} // namespace

bool isLossy(ExportFormat format) noexcept
{
    return format == ExportFormat::Mp3 || format == ExportFormat::Ogg || format == ExportFormat::Opus;
}

std::vector<int> bitDepthsFor(ExportFormat format)
{
    if (isLossy(format))
        return {};
    return format == ExportFormat::Wav ? std::vector<int>{16, 24, 32} : std::vector<int>{16, 24};
}

std::vector<int> bitRatesFor(ExportFormat format)
{
    switch (format)
    {
    case ExportFormat::Mp3:
    case ExportFormat::Ogg:
        return {128, 192, 256, 320};
    case ExportFormat::Opus:
        return {96, 128, 160, 192, 256};
    default:
        return {};
    }
}

int defaultBitRate(ExportFormat format)
{
    switch (format)
    {
    case ExportFormat::Mp3:
        return 320;
    case ExportFormat::Ogg:
        return 192;
    case ExportFormat::Opus:
        return 160;
    default:
        return 0;
    }
}

std::string extensionFor(ExportFormat format)
{
    switch (format)
    {
    case ExportFormat::Wav: return ".wav";
    case ExportFormat::Flac: return ".flac";
    case ExportFormat::Aiff: return ".aiff";
    case ExportFormat::Mp3:
        return ".mp3";
    case ExportFormat::Ogg:
        return ".ogg";
    case ExportFormat::Opus:
        return ".opus";
    }
    return ".wav";
}

std::string formatName(ExportFormat format)
{
    switch (format)
    {
    case ExportFormat::Wav: return "WAV";
    case ExportFormat::Flac: return "FLAC";
    case ExportFormat::Aiff: return "AIFF";
    case ExportFormat::Mp3:
        return "MP3";
    case ExportFormat::Ogg:
        return "Ogg Vorbis";
    case ExportFormat::Opus:
        return "Opus";
    }
    return "WAV";
}

std::vector<ExportFileFormat> exportFormats(const ExportSettings& s)
{
    std::vector<ExportFileFormat> out{{s.format, s.bitDepth, s.kbps}};
    if (s.alsoAs && *s.alsoAs != s.format)
        out.push_back({*s.alsoAs, s.alsoBitDepth, s.alsoKbps});
    return out;
}

std::string formatLabel(const ExportFileFormat& f)
{
    if (!isLossy(f.format))
        return formatName(f.format) + " " +
               (f.bitDepth == 32 ? std::string("32-bit float") : std::to_string(f.bitDepth) + "-bit");
    // Vorbis aims at a quality, not a rate: about that.
    return formatName(f.format) + " " + (f.format == ExportFormat::Ogg ? "\xe2\x89\x88" : "") +
           std::to_string(f.kbps) + " kbps";
}

namespace
{
/// A bit depth the format writes (the nearest below), and a bit rate it offers (its default).
void validateFile(ExportFormat format, int& bitDepth, int& kbps)
{
    const auto depths = bitDepthsFor(format);
    if (!depths.empty() && std::find(depths.begin(), depths.end(), bitDepth) == depths.end())
    {
        int best = depths.front();
        for (const int d : depths)
            if (d <= bitDepth)
                best = d;
        bitDepth = best;
    }
    const auto rates = bitRatesFor(format);
    if (rates.empty())
        kbps = 0;
    else if (std::find(rates.begin(), rates.end(), kbps) == rates.end())
        kbps = defaultBitRate(format);
}
} // namespace

ExportSettings validated(ExportSettings s)
{
    validateFile(s.format, s.bitDepth, s.kbps);
    if (s.alsoAs && *s.alsoAs == s.format)
        s.alsoAs.reset();
    if (s.alsoAs)
        validateFile(*s.alsoAs, s.alsoBitDepth, s.alsoKbps);
    if (s.sampleRate != 0.0 && std::none_of(std::begin(kExportRates), std::end(kExportRates), [&](double r) { return r == s.sampleRate; }))
        s.sampleRate = 0.0;
    if (!std::isfinite(s.loudnessTarget))
        s.loudnessTarget = -14.0;
    s.loudnessTarget = std::clamp(s.loudnessTarget, -36.0, -6.0);
    return s;
}

std::string toJson(const ExportSettings& s)
{
    return nlohmann::json{
        {"what", nameOf(s.what, kWhat)},
        {"throughReturns", s.throughReturns},
        {"range", nameOf(s.range, kRange)},
        {"format", nameOf(s.format, kFormat)},
        {"bitDepth", s.bitDepth},
        {"kbps", s.kbps},
        {"alsoAs", s.alsoAs ? nlohmann::json(nameOf(*s.alsoAs, kFormat)) : nlohmann::json(nullptr)},
        {"alsoBitDepth", s.alsoBitDepth},
        {"alsoKbps", s.alsoKbps},
        {"sampleRate", s.sampleRate},
        {"dither", s.dither},
        {"normalise", nameOf(s.normalise, kNormalise)},
        {"loudnessTarget", s.loudnessTarget},
        {"folder", s.folder},
        {"name", s.name}}
        .dump();
}

ExportSettings exportSettingsFromJson(const std::string& text)
{
    ExportSettings s;
    const auto j = nlohmann::json::parse(text, nullptr, false);
    if (!j.is_object())
        return s;
    s.what = enumFrom(j, "what", kWhat, s.what);
    s.range = enumFrom(j, "range", kRange, s.range);
    s.format = enumFrom(j, "format", kFormat, s.format);
    s.normalise = enumFrom(j, "normalise", kNormalise, s.normalise);
    if (j.contains("throughReturns") && j["throughReturns"].is_boolean())
        s.throughReturns = j["throughReturns"].get<bool>();
    if (j.contains("bitDepth") && j["bitDepth"].is_number_integer())
        s.bitDepth = j["bitDepth"].get<int>();
    if (j.contains("kbps") && j["kbps"].is_number_integer())
        s.kbps = j["kbps"].get<int>();
    if (j.contains("alsoAs") && j["alsoAs"].is_string())
        s.alsoAs = enumFrom(j, "alsoAs", kFormat, ExportFormat::Wav);
    if (j.contains("alsoBitDepth") && j["alsoBitDepth"].is_number_integer())
        s.alsoBitDepth = j["alsoBitDepth"].get<int>();
    if (j.contains("alsoKbps") && j["alsoKbps"].is_number_integer())
        s.alsoKbps = j["alsoKbps"].get<int>();
    if (j.contains("sampleRate") && j["sampleRate"].is_number())
        s.sampleRate = j["sampleRate"].get<double>();
    if (j.contains("dither") && j["dither"].is_boolean())
        s.dither = j["dither"].get<bool>();
    if (j.contains("loudnessTarget") && j["loudnessTarget"].is_number())
        s.loudnessTarget = j["loudnessTarget"].get<double>();
    if (j.contains("folder") && j["folder"].is_string())
        s.folder = j["folder"].get<std::string>();
    if (j.contains("name") && j["name"].is_string())
        s.name = j["name"].get<std::string>();
    return validated(s);
}

double songEnd(const Snapshot& snapshot)
{
    double end = 0.0;
    for (const auto& t : snapshot.tracks)
        for (const auto& c : t.clips)
            end = std::max(end, c.end());
    return end;
}

std::optional<ExportSpan> exportSpan(const Snapshot& snapshot, ExportRange range, std::optional<std::pair<double, double>> loop,
                                     std::string* why)
{
    if (range == ExportRange::Loop)
    {
        if (!loop || !(loop->second > loop->first))
        {
            if (why != nullptr)
                *why = "There's no loop to export: draw a loop brace first.";
            return std::nullopt;
        }
        return ExportSpan{.start = loop->first, .end = loop->second, .tail = false};
    }
    const double end = songEnd(snapshot);
    if (!(end > 0.0))
    {
        if (why != nullptr)
            *why = "There's nothing to export yet: the project has no clips.";
        return std::nullopt;
    }
    return ExportSpan{.start = 0.0, .end = end, .tail = true};
}

double exportLength(const ExportSpan& span, const BlockLevels& levels)
{
    const double length = span.end - span.start;
    if (!span.tail || levels.blockSeconds <= 0.0)
        return length;
    const double silent = levelOf(kBounceSilenceDb);
    const auto from = static_cast<size_t>(std::max(0.0, std::floor(length / levels.blockSeconds)));
    const auto to = std::min(levels.peaks.size(), static_cast<size_t>(std::ceil((length + kBounceTailSeconds) / levels.blockSeconds)));
    double sounding = length;
    for (size_t b = from; b < to; ++b)
        if (levels.peaks[b] > silent)
            sounding = std::max(sounding, static_cast<double>(b + 1) * levels.blockSeconds);
    return std::min(sounding, length + kBounceTailSeconds);
}

std::string exportFileName(const std::string& base, const std::string& track, const std::string& bars, ExportFormat format)
{
    std::string name = base.empty() ? std::string("Untitled") : base;
    if (!track.empty())
        name += " - " + track;
    if (!bars.empty())
        name += " - " + bars;
    // Characters no file system takes become hyphens.
    for (auto& c : name)
        if (std::string_view("/\\:*?\"<>|").find(c) != std::string_view::npos)
            c = '-';
    return name + extensionFor(format);
}

std::string uniqueFileName(const std::string& name, const std::function<bool(const std::string&)>& exists)
{
    if (!exists(name))
        return name;
    const auto dot = name.rfind('.');
    const auto stem = dot == std::string::npos ? name : name.substr(0, dot);
    const auto ext = dot == std::string::npos ? std::string() : name.substr(dot);
    for (int n = 2;; ++n)
        if (const auto candidate = stem + " " + std::to_string(n) + ext; !exists(candidate))
            return candidate;
}

std::string loopBars(const TempoMap& tempo, double start, double end)
{
    const double perBar = tempo.beatsPerBar();
    const int first = static_cast<int>(std::floor(tempo.beatsForSeconds(start) / perBar + 1e-6)) + 1;
    const int last = static_cast<int>(std::ceil(tempo.beatsForSeconds(end) / perBar - 1e-6)) + 1;
    return "Bars " + std::to_string(first) + "-" + std::to_string(std::max(first + 1, last));
}

NormaliseGain normaliseGain(ExportNormalise mode, double target, double truePeakDb, double integratedLufs)
{
    if (mode == ExportNormalise::Off || !std::isfinite(truePeakDb))
        return {}; // silence stays silence
    const double toCeiling = kExportCeilingDbtp - truePeakDb;
    if (mode == ExportNormalise::Peak || !std::isfinite(integratedLufs))
        return {.db = toCeiling};
    const double toTarget = target - integratedLufs;
    if (toTarget > toCeiling)
        return {.db = toCeiling, .ceilingHeld = true};
    return {.db = toTarget};
}

Quantiser::Quantiser(int bits, bool dither, std::uint32_t seed)
    : steps_(std::ldexp(1.0, std::max(1, bits) - 1)), dither_(dither), active_(bits < 32), state_(seed == 0 ? 1u : seed)
{
}

float Quantiser::uniform() noexcept
{
    // xorshift32: small, fast and the same everywhere.
    state_ ^= state_ << 13;
    state_ ^= state_ >> 17;
    state_ ^= state_ << 5;
    return static_cast<float>(static_cast<double>(state_) / 4294967296.0 - 0.5);
}

float Quantiser::operator()(float sample) noexcept
{
    if (!active_)
        return sample;
    double v = static_cast<double>(sample) * steps_;
    if (dither_)
        v += static_cast<double>(uniform()) + static_cast<double>(uniform()); // triangular, ±1 step
    const double q = std::clamp(std::round(v), -(steps_ - 1.0), steps_ - 1.0);
    return static_cast<float>(q / steps_);
}
} // namespace fd::model
