#pragma once

#include "SampleData.h"
#include "Sfz.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace fd::devices
{
/// What the Sampler plays (devices/sampler/SPEC.md, "Stage 2: Multisample"): one sample across
/// the keyboard, or an SFZ file's zones. Immutable once built; built off the audio thread and
/// handed over whole (ADR 0017).
struct SampleMap
{
    /// One sample with the keys and velocities it plays.
    struct Zone
    {
        std::shared_ptr<const SampleData> sample; // null: the file is missing
        std::string file;                         // where it was looked for
        int loKey = 0, hiKey = 127, loVel = 0, hiVel = 127;
        int root = 60;          // a multisample's own; one sample follows Root Key
        double tune = 0.0;      // cents, transpose included
        float gain = 1.0f;      // linear
        int start = 0, end = 0; // frames; end exclusive (the sample's length when 0)
        bool loops = false;     // its own loop, from the file
        int loopStart = 0, loopEnd = 0;
        int seqLength = 1, seqPosition = 1; // round robin: its turn of seqLength
        float loRand = 0.0f, hiRand = 1.0f; // random choice: its share of 0..1

        [[nodiscard]] bool plays(int key, int velocity) const noexcept
        {
            return key >= loKey && key <= hiKey && velocity >= loVel && velocity <= hiVel;
        }
        [[nodiscard]] int frames() const noexcept { return sample != nullptr ? sample->frames : 0; }
        /// The frames it plays: [start, end).
        [[nodiscard]] int playEnd() const noexcept
        {
            return end > start ? std::min(end, frames()) : frames();
        }
    };

    /// What the file sets beyond its zones (the Sampler's controls, set through the host).
    struct Settings
    {
        std::optional<double> ampAttack, ampDecay, ampSustain, ampRelease; // seconds; sustain %
        std::optional<double> velocityToVolume;                            // %
        bool oneShot = false;                                              // every zone a one-shot
        bool loop = false;                                                 // every zone loops
    };

    std::string path;   // the sample or the SFZ, resolved
    bool single = true; // one sample: its zone plays at Root Key
    std::vector<Zone> zones;
    std::vector<std::string> ignored; // what the file says that isn't played
    int missing = 0;                  // zones whose sample isn't there
    Settings settings;

    /// Where a note is in the round robins and the random choice: the turns taken on each key and
    /// the random generator, both the same after every reset (SPEC, S5). Real-time safe.
    struct Choice
    {
        std::array<std::uint32_t, 128> turns{};
        std::uint32_t random = 0x9E3779B9u;
        void reset() noexcept { *this = {}; }
        /// The turn a note on `key` takes, and the next one's after it.
        [[nodiscard]] std::uint32_t nextTurn(int key) noexcept;
        /// A pick in 0..1 (xorshift32: plenty for choosing a sample).
        [[nodiscard]] float nextPick() noexcept;
    };
    static constexpr int maxChosen = 4;
    /// The zones a note plays, in order, at most maxChosen, into `out`; how many. Zones taking
    /// turns play on theirs (`turn` of the key's); zones sharing a random choice where `pick`
    /// falls; zones whose sample is missing never. Real-time safe.
    [[nodiscard]] int choose(int key, int velocity, std::uint32_t turn, float pick,
                             std::array<int, maxChosen>& out) const noexcept;

    /// The longest sample's length (one-shot tails).
    [[nodiscard]] double longestSeconds() const noexcept;
    /// The keys its zones cover.
    [[nodiscard]] int lowestKey() const noexcept;
    [[nodiscard]] int highestKey() const noexcept;
    /// The memory its samples hold, each file once.
    [[nodiscard]] size_t bytes() const noexcept;

    /// One sample, across every key and velocity.
    [[nodiscard]] static std::shared_ptr<const SampleMap> one(std::shared_ptr<const SampleData> sample);
    /// An SFZ file's zones, their samples decoded in parallel (one decode a file, shared with every
    /// Sampler that holds it). Null when the file can't be read (`error` says why); a map whose
    /// samples are missing is still a map, with `missing` counted.
    [[nodiscard]] static std::shared_ptr<const SampleMap> loadSfz(const std::string& path,
                                                                  std::string* error);
    /// Whether `path` names an SFZ file.
    [[nodiscard]] static bool isSfz(std::string_view path) noexcept;
};
} // namespace fd::devices
