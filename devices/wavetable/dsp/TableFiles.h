#pragma once

#include "fd/dsp/Wavetable.h"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace fd::devices::wavetable
{
/// A table read from a file: its cycles, all `frameLength` samples long.
struct ImportedTable
{
    std::string name;
    std::vector<float> samples;
    int frameLength = dsp::Wavetable::frameSize;
    int frames = 1;
};

/// Reads a wavetable file (devices/wavetable/SPEC.md, "Tables and their files"):
/// - a WAV in any format: its frame size from a `clm ` chunk (the common convention), `srge` or
///   `srgo` (Surge's), or `uhWT` (2 048); without one, 64 × 256 for exactly 16 384 samples (a
///   WaveEdit bank), one cycle for up to 4 096 samples, otherwise 2 048;
/// - Surge's and Bitwig's `.wt`.
/// `frameLength` above zero sets the frame size by hand. At most 256 frames are kept; several
/// channels are mixed. Not real-time. On failure, nothing, and `error` says why.
[[nodiscard]] std::optional<ImportedTable> importTable(const std::string& path, int frameLength = 0,
                                                       std::string* error = nullptr);

/// Whether a file is a table to load as it is: a `.wt`, a WAV whose metadata gives its frame size
/// (`clm `, `srge`, `srgo`, `uhWT`), a WaveEdit bank, or a single cycle (up to 4 096 samples).
/// Anything else is audio to make a table from (docs/design/table-editor.md §5).
[[nodiscard]] bool isTableFile(const std::string& path);

/// A sound file's samples mixed to mono, and their rate: audio to make a table from.
struct MonoAudio
{
    std::vector<float> samples;
    double sampleRate = 0.0;
};
[[nodiscard]] std::optional<MonoAudio> readMono(const std::string& path, std::string* error = nullptr);

/// Single cycles (one per WAV file, any lengths), one frame each, in the order given (up to 256).
[[nodiscard]] std::optional<dsp::Wavetable> importCycles(const std::vector<std::string>& paths,
                                                         std::string* error = nullptr);

/// Writes a table (devices/wavetable/SPEC.md, "Tables and their files"): a WAV of 32-bit float
/// frames with a `clm ` chunk giving the frame size ("<!>2048", no other vendor's flags), or a
/// `.wt` of float frames. Read back by importTable exactly. False (and `error`) when it can't.
/// A factory table's file (the Library's, ADR 0055) also carries its factory ID, in a `sgno` chunk
/// of ours, so loading it puts that factory table on the oscillator by its ID.
[[nodiscard]] bool exportWav(const dsp::Wavetable& table, const std::string& path,
                             std::string* error = nullptr, std::string_view factoryId = {});
[[nodiscard]] bool exportWt(const dsp::Wavetable& table, const std::string& path,
                            std::string* error = nullptr);

/// The factory table a WAV file is (its `sgno` chunk), or empty: a table of its own.
[[nodiscard]] std::string factoryIdOf(const std::string& path);

/// Writes every factory table as a WAV into `folder`, a folder a category ("Acoustic/Cello.wav"),
/// each carrying its ID: the Library's Wavetables (ADR 0055). False (and `error`) when it can't.
[[nodiscard]] bool writeFactoryTableFiles(const std::filesystem::path& folder, std::string* error = nullptr);

/// Base64, for tables carried inside presets and projects.
[[nodiscard]] std::string toBase64(const std::vector<unsigned char>& bytes);
[[nodiscard]] std::vector<unsigned char> fromBase64(const std::string& text);
} // namespace fd::devices::wavetable
