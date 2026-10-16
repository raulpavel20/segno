#pragma once

#include "fd/audiofile/AudioFile.h"

#include <cstdint>
#include <filesystem>
#include <iosfwd>
#include <optional>
#include <string>
#include <vector>

/// Building Segno's Library (ADR 0055, docs/design/factory-library.md §6): the factory tables and
/// the sample instruments, made from tools/library/recipe.json and the sources it names. The same
/// recipe gives the same Library on every machine. JUCE-free.
namespace fd::library_build
{
/// A sample made ready for the Library: its tail trimmed, capped, 16-bit.
struct Prepared
{
    std::vector<std::int16_t> interleaved;
    int channels = 0;
    double sampleRate = 0.0;
    int frames = 0;
    bool capped = false; // longer than the cap: cut there, with a longer fade
};

/// Trims the tail where it falls below -60 dB of the sample's peak (with a short fade), caps it at
/// `maxSeconds` (with a half-second fade), and makes it 16-bit: a 16-bit source's own samples
/// exactly, anything else with TPDF dither from a generator seeded by `seed`, so the same file
/// always comes out the same. With `trimStart` (a drum), what comes before its onset goes too, but
/// for a millisecond: a hit sounds the moment it's played.
[[nodiscard]] Prepared prepare(const audiofile::DecodedAudio& audio, double maxSeconds, std::uint32_t seed,
                               bool trimStart = false);

/// Where a sound starts: its first frame above -40 dB of its peak (0 for silence).
[[nodiscard]] int onsetOf(const audiofile::DecodedAudio& audio);

/// Writes `p` as a FLAC file (libFLAC, its strongest setting). False (and `error`) when it can't.
bool writeFlac(const std::filesystem::path& path, const Prepared& p, std::string* error = nullptr);

/// The pitch of a sample's steady part as a MIDI note (fractional), nullopt when it has none to
/// find. For checking a map's roots, not for playing.
[[nodiscard]] std::optional<double> detectPitch(const audiofile::DecodedAudio& audio);

/// Where the Library's sounds come from.
struct Source
{
    std::string id, name, url, commit, license;
    std::string raw; // where its files are fetched from: <raw>/<commit>/<path>
};

/// One of a zone's microphones (drum-kit.md §27.3): a file of its source, at a gain in dB, a mono
/// one panned (-1 left .. 1 right) into stereo, a stereo one balanced.
struct MixPart
{
    std::string file, sha256;
    double gainDb = 0.0, pan = 0.0;
};

/// One zone of an instrument, as the recipe gives it.
struct Zone
{
    std::string file, sha256; // in its source, at its commit; a mix's: its first part's
    std::string name;         // its name in the Library; empty: the source file's
    int loKey = 0, hiKey = 127, root = 60, loVel = 0, hiVel = 127;
    double volume = 0.0, tune = 0.0;
    int seqLength = 1, seqPosition = 1;
    double loRand = 0.0, hiRand = 1.0;
    /// Its sound mixed from several files (a drum's microphones), else empty: `file` alone.
    std::vector<MixPart> mix;
    /// Its sound rendered by Segno's own drum voices (DrumSynth.h): the voice's JSON, else empty.
    /// Its `file` is then "synth" and its `sha256` the voice's fingerprint.
    std::string synth;
};

/// One instrument: an SFZ file in the Library and the zones it maps.
struct Instrument
{
    std::string sfz;    // "Samples/Strings/Violin section/Sustain.sfz"
    std::string source; // a Source id
    double maxSeconds = 8.0;
    bool pitched = true;                                     // roots checked against the samples' pitch
    /// Its loudest zone's peak in dBFS, every zone scaled by the same gain so its layers keep their
    /// levels (a drum, drum-kit.md §27.3); none: the samples' own levels. A levelled instrument is a
    /// drum's: its samples start at their onsets too.
    std::optional<double> normalize;
    std::vector<std::pair<std::string, std::string>> global; // opcodes for the whole instrument
    std::vector<Zone> zones;
};

struct Recipe
{
    std::string version;
    std::vector<Source> sources;
    std::vector<Instrument> instruments;
    [[nodiscard]] const Source* source(const std::string& id) const;
};

[[nodiscard]] std::optional<Recipe> readRecipe(const std::filesystem::path& path,
                                               std::string* error = nullptr);

/// A file the recipe needs from a source, once however many zones play it.
struct SourceFile
{
    const Source* source;
    std::string file, sha256;
};
/// Every file the recipe's instruments need, a mix's every part, in the recipe's order.
[[nodiscard]] std::vector<SourceFile> sourceFiles(const Recipe& recipe);

/// Where a zone's source file is in the sources cache: <cache>/<source>/<commit>/<file>.
[[nodiscard]] std::filesystem::path sourceFile(const std::filesystem::path& cache, const Source& source,
                                               const std::string& file);
/// The name a zone's sample has in the Library, beside its SFZ in `samples/`: the recipe's, or its
/// source file's, as FLAC.
[[nodiscard]] std::string sampleName(const Zone& zone);
/// The SFZ an instrument's zones make.
[[nodiscard]] std::string sfzText(const Instrument& instrument, const Source& source);

/// A zone's sound, as the Library makes it before trimming: its file, or its parts mixed at their
/// gains and pans (stereo when any part is stereo or panned), times `gain`. A mixed or scaled
/// sound has no integer bit depth: it's dithered to 16 bits. Nullopt (and `error`) when a file
/// can't be read or two parts' sample rates differ.
[[nodiscard]] std::optional<audiofile::DecodedAudio> zoneAudio(const Zone& zone, const Source& source,
                                                               const std::filesystem::path& cache,
                                                               double gain, std::string* error = nullptr);

/// Builds the sample instruments into `library`, from the sources in `cache` (fetched and checked
/// already): each instrument's samples prepared and written as FLAC, and its SFZ. Says what it
/// does on `log`, with a warning wherever a root doesn't match its sample's pitch. False on the
/// first error.
bool buildInstruments(const Recipe& recipe, const std::filesystem::path& cache,
                      const std::filesystem::path& library, std::ostream& log);
} // namespace fd::library_build
