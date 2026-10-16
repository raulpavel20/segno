#pragma once

#include "fd/audiofile/AudioFile.h"

#include <array>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace fd::devices
{
/// A source's audio as Granular plays and draws it (devices/granular/SPEC.md, "DSP design"):
/// immutable, built off the audio thread and handed over (ADR 0017), shared by the instances that
/// load the same file. Step 1 adds its pyramid of levels and what's found in it; this holds what
/// the panel draws.
struct GrainSource
{
    static constexpr int overviewBlock = 64; // frames per overview point

    std::string name, path;
    double sampleRate = 48000.0;
    int numChannels = 0; // 1 or 2 (further channels are ignored)
    int frames = 0;
    /// Its root note (a MIDI note, fractional for its tuning): from the file's metadata, its name
    /// or its sound, or set by hand; none until one is found or set.
    std::optional<double> root;
    bool rootFound = false;
    /// The waveform for the displays: each channel's lowest and highest sample per
    /// `overviewBlock` frames (mono: channel 0 only).
    std::array<std::vector<float>, 2> overviewMin, overviewMax;

    [[nodiscard]] double seconds() const noexcept { return sampleRate > 0.0 ? frames / sampleRate : 0.0; }

    /// Builds a source from decoded audio (not real-time), its level brought to full scale as
    /// Granular plays it (SPEC: Source n Gain).
    [[nodiscard]] static std::shared_ptr<const GrainSource> build(const audiofile::DecodedAudio& audio,
                                                                  std::string name, std::string path);
    /// A sound of Segno's own, made here: a soft A major chord that swells and beats slowly, in
    /// stereo, rooted on A2 (for tests and the displays; step 1 decides whether a new Granular
    /// starts with it).
    [[nodiscard]] static audiofile::DecodedAudio chord(double seconds, double sampleRate);
};
} // namespace fd::devices
