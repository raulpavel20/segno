#pragma once

#include <fd/dsp/Onsets.h>
#include <optional>
#include <string>
#include <vector>

namespace fd::model
{
// What's found in an audio file (M3 step P6b, ADR 0063, docs/design/audio-editor.md §7.2–§7.4,
// §7.8): its transients at any sensitivity, how percussive it is, and its tempo. Found once per
// file in the background (streamed: never the whole file in memory), read by the editor, the
// piece player's Beats, Quantise, the Tempo tool and the warping a file gets when it's placed.

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

/// Whether a file placed in the song is a loop that plays in time (§7.2): a tempo (its own, or
/// found), a whole number of bars at it (1, 2, 4, 8 or 16, within 1 %), and at least a bar long.
[[nodiscard]] bool isLoopAt(double fileSeconds, double tempo) noexcept;
} // namespace fd::model
