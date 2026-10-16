#pragma once

#include "fd/audiofile/AudioFile.h"

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace fd::devices
{
/// A source's audio as Granular plays and draws it (devices/granular/SPEC.md, "DSP design"):
/// immutable, built off the audio thread and handed over (ADR 0017), shared by the instances that
/// load the same file.
///
/// **The pyramid.** Level 0 is the file at its own rate; each next level is halved, low-passed
/// first (`dsp::decimateByTwo`, keeping 0.41 of its rate), so a grain playing faster than the
/// source reads the level where it advances at most two samples per output sample (an oversampled
/// level) or one (a plain one), and nothing aliases. Up to two minutes, each level is also kept at
/// twice its rate (`oversampled`), so a cheap interpolator is clean (G12); longer sources and a
/// multisample's zones keep the plain levels in 16 bits (`compact`, G13). Frames are interleaved
/// (L R L R) with `pad` silent frames either side, so an interpolator never reads outside.
struct GrainSource
{
    static constexpr int overviewBlock = 64; // file frames per overview and level-map point
    static constexpr int pad = 8;            // silent frames before and after each level
    static constexpr int maxLevels = 8;
    static constexpr double oversampleSeconds = 120.0; // up to here, oversampled float levels
    static constexpr double longestSeconds = 1200.0;   // past here, the first 20 minutes

    struct Level
    {
        std::vector<float> f32;        // interleaved, padded (oversampled float sources)
        std::vector<std::int16_t> i16; // interleaved, padded (compact sources)
        int frames = 0;                // without the padding
        /// Frames of this level per frame of the file.
        double scale = 1.0;
        [[nodiscard]] const float* f32At(int frame, int channels) const noexcept
        {
            return f32.data() + static_cast<size_t>(frame + pad) * static_cast<size_t>(channels);
        }
        [[nodiscard]] const std::int16_t* i16At(int frame, int channels) const noexcept
        {
            return i16.data() + static_cast<size_t>(frame + pad) * static_cast<size_t>(channels);
        }
    };

    std::string name, path;
    double sampleRate = 48000.0;
    int numChannels = 0; // 1 or 2 (further channels are ignored)
    int frames = 0;      // the file's (after any cut to longestSeconds)
    bool truncated = false;
    /// Its root note (a MIDI note, fractional for its tuning): from the file's metadata, its name
    /// or its sound, or set by hand; none until one is found or set.
    std::optional<double> root;
    bool rootFound = false;

    bool oversampled = true; // its levels at twice their rates, in floats
    bool compact = false;    // its levels plain, in 16 bits
    std::vector<Level> levels;

    /// The waveform for the displays: each channel's lowest and highest sample per
    /// `overviewBlock` frames (mono: channel 0 only), at full scale.
    std::array<std::vector<float>, 2> overviewMin, overviewMax;
    /// How loud it is per `overviewBlock` frames (RMS of both channels, at full scale): silent
    /// grains are skipped, and Even lifts quiet ones.
    std::vector<float> levelMap;
    /// Where things start in it (seconds), its beats (seconds), and where its waveform crosses
    /// zero going up (frames): Snap's choices, and Pitched's alignment to the source's periods.
    std::vector<double> transients, beats;
    std::vector<std::int32_t> zeroCrossings;
    /// The level it was brought to full scale by (Source n Gain is on top).
    float normalise = 1.0f;

    [[nodiscard]] double seconds() const noexcept { return sampleRate > 0.0 ? frames / sampleRate : 0.0; }
    /// How loud it is at `frame` (linear, full scale 1).
    [[nodiscard]] float levelAt(double frame) const noexcept;
    /// The bytes its levels hold.
    [[nodiscard]] size_t bytes() const noexcept;
    /// The nearest of `places` (sorted) to `at`, or `at` when there are none.
    [[nodiscard]] static double nearest(std::span<const double> places, double at) noexcept;
    [[nodiscard]] std::int32_t nearestZeroCrossing(double frame) const noexcept;

    struct Options
    {
        bool compact = false; // force the plain 16-bit pyramid (a multisample's zones)
        bool analyse = true;  // transients, beats, the root from the sound
    };
    /// Builds a source from decoded audio (not real-time).
    [[nodiscard]] static std::shared_ptr<const GrainSource>
    build(const audiofile::DecodedAudio& audio, std::string name, std::string path, Options options);
    [[nodiscard]] static std::shared_ptr<const GrainSource> build(const audiofile::DecodedAudio& audio,
                                                                  std::string name, std::string path)
    {
        return build(audio, std::move(name), std::move(path), Options{});
    }
    /// A sound of Segno's own, made here: a soft A major chord that swells and beats slowly, in
    /// stereo, rooted on A2: what a new Granular plays (G21) and what tests play.
    [[nodiscard]] static audiofile::DecodedAudio chord(double seconds, double sampleRate);
    /// A root from a file's name ("Violin_A3", "pad-F#2"): a note name with its octave, A4 being
    /// 440 Hz (MIDI 69), standing apart from the rest of the name.
    [[nodiscard]] static std::optional<double> rootFromName(std::string_view name);
};

/// What a source slot holds: one file's audio, or a multisample's zones (an SFZ, the Library's
/// instruments), each with the keys and velocities it plays.
struct GrainSourceSet
{
    struct Zone
    {
        std::shared_ptr<const GrainSource> audio;
        int loKey = 0, hiKey = 127, loVel = 0, hiVel = 127;
        /// Its root (MIDI, its tune included).
        double root = 60.0;
        float gain = 1.0f; // linear, its volume
    };
    std::string name, path; // as stored (a `library:` reference stays one)
    std::vector<Zone> zones;
    bool multisample = false;

    /// The zone a note plays (the nearest by key when none covers it); null without zones.
    [[nodiscard]] const Zone* zoneFor(int key, int velocity) const noexcept;
    /// What the displays draw: the zone a middle C plays.
    [[nodiscard]] std::shared_ptr<const GrainSource> shown() const noexcept;
    [[nodiscard]] size_t bytes() const noexcept;

    /// One file's audio across every key.
    [[nodiscard]] static std::shared_ptr<const GrainSourceSet> one(std::shared_ptr<const GrainSource> audio,
                                                                   std::string path);
    /// Reads a file (any audio, or an SFZ) into a set (not real-time); null with `error` when it
    /// can't. Shared: a file read once serves every instance until none holds it.
    [[nodiscard]] static std::shared_ptr<const GrainSourceSet> load(const std::string& stored,
                                                                    std::string* error);
};
} // namespace fd::devices
