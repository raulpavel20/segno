#pragma once

#include <fd/dsp/Onsets.h>
#include <fd/dsp/PitchClasses.h>
#include <fd/music/Key.h>
#include <optional>
#include <string>
#include <vector>

namespace fd::model
{
// What's found in an audio file (M3 steps P6b and P6c, ADR 0063, docs/design/audio-editor.md
// §7.2–§7.4, §7.8, §8.2): its transients at any sensitivity, how percussive it is, its tempo and
// its key. Found once per file in the background (streamed: never the whole file in memory), read
// by the editor, the piece player's Beats, Quantise, the Tempo tool, the warping a file gets when
// it's placed, and Follow key.

struct FileAnalysis
{
    double length = 0.0; // seconds
    /// Every moment some sensitivity counts as a transient (seconds of the file), with how strong.
    std::vector<dsp::OnsetCandidate> onsets;
    /// 0 … 1; above 0.5 Warp starts in Beats (§7.3).
    double percussiveness = 0.0;
    /// From its first minute (§7.8): what the Tempo tool shows; and a loop's when it's placed with no
    /// tempo of its own (§7.2).
    std::optional<dsp::TempoEstimate> tempo;
    /// From its first two minutes (§8.2): how much of each pitch class sounds, and its key where
    /// that's clear (keyOfSound). The engine adds what the file's metadata and name say (fileKey).
    dsp::PitchClasses pitchClasses;
    std::optional<music::Key> key;
};

/// Analyses the file at `path`, streamed, its channels mixed. Nullopt, with `error`, if it can't be
/// read. Not real-time: a background thread's.
[[nodiscard]] std::optional<FileAnalysis> analyseFile(const std::string& path, std::string* error = nullptr);

/// A clip's transients (§7.4): the file's at its sensitivity, without those removed (within a
/// millisecond) and with those added by hand, in order. Seconds of the file as recorded.
[[nodiscard]] std::vector<double> clipTransients(const FileAnalysis& file, double sensitivity,
                                                 const std::vector<double>& added,
                                                 const std::vector<double>& removed);

struct ClipInfo;
/// A clip's transients (clipTransients at its sensitivity, with its edits); none while its file is
/// being analysed.
[[nodiscard]] std::vector<double> transientsOf(const ClipInfo& clip);

/// The mode Warp starts in when it's turned on (§7.3): Beats for a percussive file, else Tones.
[[nodiscard]] bool startsInBeats(const FileAnalysis& file) noexcept;

/// How pitched a sound must be for its key to count (§8.2): notes and chords score 0.8 and more,
/// noise and drums about 0.3.
inline constexpr double kKeyTonal = 0.6;
/// A sound's key, when it's pitched and its key is clear (music::clearKey): none for drums, noise
/// or a note held alone.
[[nodiscard]] std::optional<music::Key> keyOfSound(const dsp::PitchClasses& sound);
/// A file's key (§8.2): its metadata's root (an acid chunk's), in the mode its name gives it, else
/// the one its sound fits better at that root, else major; failing a root, its name's key, else
/// its sound's.
[[nodiscard]] std::optional<music::Key> fileKey(std::optional<int> metadataRoot,
                                                const std::optional<music::Key>& named,
                                                const dsp::PitchClasses* sound);

/// Whether a file placed in the song is a loop that plays in time (§7.2): a tempo (its own, or
/// found), a whole number of bars at it (1, 2, 4, 8 or 16, within 1 %), and at least a bar long.
[[nodiscard]] bool isLoopAt(double fileSeconds, double tempo) noexcept;
} // namespace fd::model
