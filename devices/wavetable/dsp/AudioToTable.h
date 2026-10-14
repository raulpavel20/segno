#pragma once

#include "TableRecipe.h"

#include <optional>
#include <string>
#include <vector>

namespace fd::devices::wavetable
{
/// The three ways audio becomes a table (docs/design/table-editor.md §5, the SPEC's W10).
enum class AudioWay
{
    Pitched,  // the pitch followed, a wave taken at each frame, its harmonics' levels and phases
    Spectral, // the harmonics' levels at each frame, without the phases: the colour, not the noise
    Slices    // windows of 2 048 samples, as they are
};

struct AudioFrames
{
    std::vector<float> samples; // `count` frames of 2 048 samples
    int count = 0;
    double pitch = 0.0; // the pitch found (Hz), 0 for Slices
};

/// The pitch of a sound (Hz, 30 Hz to 2 kHz), or nothing if it has none: YIN over windows through
/// it, each refined by how the fundamental's phase moves, then the median.
[[nodiscard]] std::optional<double> findPitch(const float* audio, int length, double sampleRate);

/// Frames from mono audio, `count` of them (1 to 256), spread from its start to its end. On
/// failure (no pitch for Pitched or Spectral, nothing to take), nothing, and `error` says why.
/// Not real-time.
[[nodiscard]] std::optional<AudioFrames> framesFromAudio(const float* audio, int length, double sampleRate,
                                                         AudioWay way, int count,
                                                         std::string* error = nullptr);

/// The way's name ("Pitched"), and back from its lower-case form ("pitched").
[[nodiscard]] const char* wayName(AudioWay way) noexcept;
[[nodiscard]] std::optional<AudioWay> wayFromName(const std::string& name) noexcept;

/// An Audio layer of frames taken from audio, its note saying how and from where ("Pitched, 64
/// frames from Vox").
[[nodiscard]] Recipe::Layer audioLayer(const AudioFrames& frames, AudioWay way, const std::string& from);
/// A recipe made from audio (§5): that one layer, as many frames as were taken, named for
/// where they came from.
[[nodiscard]] Recipe recipeFromAudio(const AudioFrames& frames, AudioWay way, const std::string& from);
} // namespace fd::devices::wavetable
