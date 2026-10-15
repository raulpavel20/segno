#pragma once

#include "model/Bounce.h"
#include "model/Snapshot.h"

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace fd::model
{
// Export (M2 step C8, ADR 0039, docs/design/export.md): what an export renders and how its files
// are made, JUCE-free. The Session renders and writes; the panel shows these settings.

enum class ExportWhat
{
    Mix,
    SelectedTracks,
    AllTracks, // and each return track as a stem of its own
};
enum class ExportRange
{
    Song, // bar 1 to the last clip's end, then the tail until silent
    Loop, // exactly the loop brace
};
enum class ExportFormat
{
    Wav,
    Flac,
    Aiff,
    Mp3,  // constant bit rate, through LAME (export.md §17)
    Ogg,  // Ogg Vorbis
    Opus, // Ogg Opus: 48 kHz files, whatever the render's rate
};
enum class ExportNormalise
{
    Off,
    Peak,     // the true peak at kExportCeilingDbtp
    Loudness, // the integrated loudness at the target, never above the ceiling
};

struct ExportSettings
{
    ExportWhat what = ExportWhat::Mix;
    bool throughReturns = false; // stems through the returns and master (§2)
    ExportRange range = ExportRange::Song;
    ExportFormat format = ExportFormat::Wav;
    int bitDepth = 24;           // 16, 24, or 32 (float; WAV only)
    int kbps = 0;                // a lossy format's bit rate; 0: its default
    /// Several formats at once (§17.2): a second file of each, from the same render.
    std::optional<ExportFormat> alsoAs;
    int alsoBitDepth = 24, alsoKbps = 0;
    double sampleRate = 0.0;     // 0: the project's
    bool dither = true;          // automatic: whenever the bit depth is below 32-bit float
    ExportNormalise normalise = ExportNormalise::Off;
    double loudnessTarget = -14.0; // LUFS
    std::string folder;          // empty: the project's Exports folder; else absolute or relative to the project's folder
    std::string name;            // empty: the project's name

    friend bool operator==(const ExportSettings&, const ExportSettings&) = default;
};

/// The true peak normalising never passes (dBTP).
inline constexpr double kExportCeilingDbtp = -1.0;
/// The sample rates offered besides the project's.
inline constexpr double kExportRates[] = {44100.0, 48000.0, 88200.0, 96000.0};

/// Every format, in the panel's order.
inline constexpr ExportFormat kExportFormats[] = {ExportFormat::Wav, ExportFormat::Flac, ExportFormat::Aiff,
                                                  ExportFormat::Mp3, ExportFormat::Ogg,  ExportFormat::Opus};
/// MP3, Ogg and Opus: a bit rate, no bit depth, no dither (§17.1).
[[nodiscard]] bool isLossy(ExportFormat format) noexcept;
/// The bit depths a PCM format writes (none for a lossy one).
[[nodiscard]] std::vector<int> bitDepthsFor(ExportFormat format);
/// The bit rates a lossy format offers, in kbps (none for PCM): MP3's are constant, Ogg's about
/// that (Vorbis's quality), Opus's its target.
[[nodiscard]] std::vector<int> bitRatesFor(ExportFormat format);
[[nodiscard]] int defaultBitRate(ExportFormat format);       // 320 MP3, 192 Ogg, 160 Opus; 0 for PCM
[[nodiscard]] std::string extensionFor(ExportFormat format); // ".wav"
[[nodiscard]] std::string formatName(ExportFormat format);   // "WAV", "Ogg Vorbis"
/// One file of an export: its format with its bit depth (PCM) or bit rate (lossy).
struct ExportFileFormat
{
    ExportFormat format = ExportFormat::Wav;
    int bitDepth = 24, kbps = 0;
    friend bool operator==(const ExportFileFormat&, const ExportFileFormat&) = default;
};
/// The files each target writes: the format, then Also as's.
[[nodiscard]] std::vector<ExportFileFormat> exportFormats(const ExportSettings& settings);
/// "WAV 24-bit", "MP3 320 kbps", "Ogg Vorbis ≈192 kbps".
[[nodiscard]] std::string formatLabel(const ExportFileFormat& format);
/// The settings made valid: a bit depth the format writes (the nearest below), an offered rate,
/// a sane loudness target.
[[nodiscard]] ExportSettings validated(ExportSettings settings);
/// Settings as JSON text and back (kept with the project's view state); unknown or missing fields
/// keep their defaults.
[[nodiscard]] std::string toJson(const ExportSettings& settings);
[[nodiscard]] ExportSettings exportSettingsFromJson(const std::string& json);

/// The song's end: the latest end of a clip on any track (0 when there are none).
[[nodiscard]] double songEnd(const Snapshot& snapshot);

/// What an export renders: from `start` to `end` (seconds), then, with `tail`, on until silent.
struct ExportSpan
{
    double start = 0.0, end = 0.0;
    bool tail = false;
};
/// The span for a range; nullopt (with `why`) when there's nothing to export: no clips, or no loop.
[[nodiscard]] std::optional<ExportSpan> exportSpan(const Snapshot& snapshot, ExportRange range,
                                                   std::optional<std::pair<double, double>> loop, std::string* why = nullptr);
/// How long the export runs: to `span.end`, then (with a tail) to where `levels` (from the span's
/// start) last rise above kBounceSilenceDb, within kBounceTailSeconds.
[[nodiscard]] double exportLength(const ExportSpan& span, const BlockLevels& levels);

/// A file's name, without folder: the base (the project's name, or the panel's), a stem's track,
/// a loop's bars ("Song - Bass - Bars 9-17.wav").
[[nodiscard]] std::string exportFileName(const std::string& base, const std::string& track, const std::string& bars,
                                         ExportFormat format);
/// `name` if `exists` says it's free, else with a number before the extension ("Song 2.wav").
[[nodiscard]] std::string uniqueFileName(const std::string& name, const std::function<bool(const std::string&)>& exists);
/// "Bars 9-17" for a loop over bars 9 to 16 (its end is bar 17's start).
[[nodiscard]] std::string loopBars(const TempoMap& tempo, double start, double end);

/// What normalising does to a file measured at `truePeakDb` and `integratedLufs`: a gain in dB,
/// and whether the true-peak ceiling held a loudness target back.
struct NormaliseGain
{
    double db = 0.0;
    bool ceilingHeld = false;
};
[[nodiscard]] NormaliseGain normaliseGain(ExportNormalise mode, double target, double truePeakDb, double integratedLufs);

/// Float samples onto the grid a `bits`-bit file stores (k / 2^(bits-1), as JUCE's writers do),
/// with triangular (TPDF) dither of one step when `dither`; deterministic from `seed`. 32 bits:
/// unchanged (float).
class Quantiser
{
public:
    Quantiser(int bits, bool dither, std::uint32_t seed = 1);
    [[nodiscard]] float operator()(float sample) noexcept;

private:
    [[nodiscard]] float uniform() noexcept; // -0.5 .. 0.5
    double steps_;
    bool dither_, active_;
    std::uint32_t state_;
};
} // namespace fd::model
