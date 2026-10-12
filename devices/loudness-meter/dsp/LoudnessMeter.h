#pragma once

#include "fd/device/Device.h"
#include "fd/device/Registry.h"
#include "fd/dsp/Filters.h"
#include "fd/dsp/Loudness.h"

#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <mutex>
#include <optional>
#include <vector>

namespace fd::devices
{
/// Measures loudness as streaming services and broadcasters do (BS.1770, EBU R 128 and EBU Mode),
/// and the song rather than the session: each moment of the song counts once, the latest time it
/// played. The audio passes unchanged. See devices/loudness-meter/SPEC.md.
class LoudnessMeter final : public Device
{
public:
    enum Param : size_t
    {
        Target,
        Ceiling,
        Measure,
        Scale,
        Units,
        NumParams
    };
    enum class MeasureMode
    {
        Song,
        Running
    };

    /// Analysis bus slots. Loudness in LUFS, ranges and differences in LU, NaN without a reading.
    enum Analysis : int
    {
        Momentary = 0,
        ShortTerm = 1,
        Integrated = 2,
        Range = 3,
        MaxMomentary = 4,
        MaxShortTerm = 5,
        TruePeakMax = 6,    // dBTP
        PeakToLoudness = 7, // LU
        Normalised = 8,     // dB: what a service playing everything at the target does
        Correlation = 9,    // -1..1
        CorrelationMin = 10,
        Balance = 11,  // LU: + leans left
        Counted = 12,  // seconds of song counted
        Counting = 13, // a CountingState
        Position = 14, // the step under the playhead (Song) or the newest (Running); -1: none
    };
    /// What the meter is doing, for the panel's status line.
    enum class CountingState
    {
        Counting = 0,
        Paused = 1,
        WaitingForSong = 2,
        Full = 3
    };
    /// Display commands (AnalysisBus::request).
    enum Request : int
    {
        Reset = 0,
        Pause = 1,
        Resume = 2
    };

    static constexpr double stepSeconds = 0.1;
    /// The windows slide this finely: readings, and the maximums, every 10 ms.
    static constexpr int slidesPerStep = 10;
    static constexpr int slidesMomentary = 40, slidesShortTerm = 300;
    static constexpr int storeSteps = 108000; // 3 hours
    static constexpr std::string_view measurementKey = "song.measurement";

    LoudnessMeter();

    void prepare(const ProcessSpec& spec) override;
    /// Clears what's measured now (filters, the live readings); never what's counted: hosts call
    /// it on transport changes, and only Reset clears the count.
    void reset() noexcept override;
    void process(ProcessContext& context) noexcept override;

    void writeExtraState(nlohmann::json& extra) const override;
    void readExtraState(const nlohmann::json& extra) override;

    /// What's been counted, step by step (the history). Read-only, from the message thread.
    [[nodiscard]] const dsp::LoudnessStore& store() const noexcept;
    [[nodiscard]] MeasureMode countedMode() const noexcept;

    /// What a service playing everything at `target` does to a song at `integrated` LUFS whose
    /// true peaks reach `truePeakDb`: turned down to the target whatever the peaks; turned up only
    /// as far as the peaks stay under `ceiling`. NaN without an integrated reading.
    [[nodiscard]] static float normalisedGain(float integrated, float truePeakDb, float target,
                                              float ceiling) noexcept;

private:
    /// A step being counted: where it is and what it has so far, and the loudest windows ending
    /// in it, slid every 10 ms.
    struct Partial
    {
        int index = -1;
        int samples = 0;
        double energy = 0.0;
        float peak = 0.0f;
        float loudestMomentary = NAN, loudestShortTerm = NAN;
        int slide = 0;            // which 10 ms of the step
        double slideEnergy = 0.0; // and its energy so far
        int slideSamples = 0;
        bool valid = false;
    };
    /// 10 ms of live audio (the newest 3 s), for momentary, short-term, balance and correlation.
    struct Slice
    {
        double energy = 0.0, left = 0.0, right = 0.0; // K-weighted mean squares
        double lr = 0.0, ll = 0.0, rr = 0.0;          // unweighted sums, for correlation
    };
    /// A measurement read from a project, waiting to be counted again.
    struct Loaded
    {
        struct Step
        {
            int index = 0;
            double meanSquare = 0.0;
            float truePeak = 0.0f, loudestMomentary = NAN, loudestShortTerm = NAN;
        };
        MeasureMode mode = MeasureMode::Song;
        std::vector<Step> steps;
    };

    [[nodiscard]] dsp::LoudnessStore& active() noexcept
    {
        return stores_[static_cast<size_t>(active_.load())];
    }
    [[nodiscard]] MeasureMode mode() const noexcept;
    void clearCount() noexcept;
    void finishLiveStep() noexcept;
    void updateReadings() noexcept;
    void publish() noexcept;
    /// Counts a sample's energy and peak at `position` (in samples, from the song's start or Reset).
    void count(std::int64_t position, double energy, float peak) noexcept;
    static void fill(dsp::LoudnessStore& store, const Loaded& loaded);

    double sampleRate_ = 48000.0;
    int stepLength_ = 4800;
    std::array<std::array<dsp::Biquad, 2>, 2> kWeighting_{};
    dsp::TruePeakDetector truePeak_;

    // Live.
    std::array<Slice, slidesShortTerm> live_{};
    Slice liveNow_;
    int sliceLength_ = 480, livePos_ = 0, liveWrite_ = 0, liveDone_ = 0;
    float momentary_ = 0.0f, shortTerm_ = 0.0f, balance_ = 0.0f, correlation_ = 0.0f;

    // The counted slides (10 ms mean squares), for the maximums.
    std::array<double, slidesShortTerm> slides_{};
    int slideWrite_ = 0, slidesInARow_ = 0;
    std::int64_t nextCounted_ = -1; // the position that would follow the last sample counted

    // Counted.
    std::array<dsp::LoudnessStore, 2> stores_; // the one counting into, and one to load into
    std::atomic<int> active_{0};
    std::atomic<bool> swapReady_{false}; // the other store holds a loaded measurement
    MeasureMode loadedMode_ = MeasureMode::Song;
    std::atomic<int> appliedMode_{-1}; // the mode counted in (the panel and saving read it)
    bool prepared_ = false, paused_ = false, full_ = false, waiting_ = false;
    Partial partial_;
    std::int64_t expected_ = -1; // where the next block should start, if nothing jumped
    std::int64_t running_ = 0;   // Running: samples counted since Reset
    std::int64_t lastPosition_ = -1;
    bool readingsStale_ = true;
    float integrated_ = 0.0f, range_ = 0.0f, maxMomentary_ = 0.0f, maxShortTerm_ = 0.0f, peakMaxDb_ = 0.0f;
    float correlationMin_ = 0.0f;

    // Loading (message thread).
    mutable std::mutex loadMutex_;
    std::optional<Loaded> pending_; // read before prepare(): counted then
};

const DeviceFactory& loudnessMeterFactory();
} // namespace fd::devices
