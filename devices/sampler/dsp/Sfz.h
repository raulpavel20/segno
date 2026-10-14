#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

/// Reading SFZ files (devices/sampler/SPEC.md, "SFZ files"): the plain text maps most free sample
/// libraries come with. The opcodes the Sampler plays are read; anything else is named in
/// `ignored`, and regions that depend on what isn't read (key switches, controllers, release
/// triggers) are left out. JUCE-free; not real-time.
namespace fd::devices::sfz
{
enum class LoopMode
{
    Unset,
    NoLoop,
    OneShot,
    Continuous,
    Sustain,
};

/// One region, its headers' opcodes inherited.
struct Region
{
    std::filesystem::path sample; // where it should be (relative paths resolved against the file)
    int loKey = 0, hiKey = 127;
    int root = 60;
    bool rootFromSample = false; // pitch_keycenter=sample: the file's own root note
    int loVel = 0, hiVel = 127;
    double volume = 0.0; // dB
    double tune = 0.0;   // cents
    int transpose = 0;   // semitones
    std::int64_t offset = 0;
    std::int64_t end = -1; // the last frame played; -1: the sample's end
    LoopMode loopMode = LoopMode::Unset;
    std::int64_t loopStart = -1, loopEnd = -1; // -1: the file's own (or none)
    int seqLength = 1, seqPosition = 1;
    double loRand = 0.0, hiRand = 1.0;
};

/// A file read.
struct File
{
    std::vector<Region> regions;
    /// Opcodes and headers the Sampler doesn't play, each once ("trigger", "sw_last").
    std::vector<std::string> ignored;
    /// What the file says for the whole instrument: from its headers, or what most of its
    /// regions say.
    std::optional<double> ampAttack, ampDecay, ampSustain, ampRelease; // seconds; sustain in %
    std::optional<double> ampVeltrack;                                 // %
    /// When every region says the same.
    LoopMode loopMode = LoopMode::Unset;
};

/// Reads `path`; nullopt (and `error`) if it can't be read.
[[nodiscard]] std::optional<File> read(const std::filesystem::path& path, std::string* error = nullptr);
/// Reads SFZ text whose samples are relative to `folder`.
[[nodiscard]] File parse(std::string_view text, const std::filesystem::path& folder);
/// A key as SFZ writes it: a number, or a note name with SFZ's octaves (`c4` is 60, `c#4`, `db4`);
/// nullopt when it's neither.
[[nodiscard]] std::optional<int> noteNumber(std::string_view text);
/// Finds `path` as written, or ignoring case in each part (maps made on Windows); empty if not.
[[nodiscard]] std::filesystem::path findFile(const std::filesystem::path& path);
} // namespace fd::devices::sfz
