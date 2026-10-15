#pragma once

#include "fd/device/Device.h"
#include "fd/device/Registry.h"
#include "fd/dsp/PitchDetector.h"
#include "fd/dsp/Smoother.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <optional>

namespace fd::devices
{
/// Names the note it hears and how far off it is, steadily enough to tune by, with a strobe and
/// the pitch's history; measures against every note or only the song's key's. The audio passes
/// unchanged, unless Mute is on. See devices/tuner/SPEC.md.
class Tuner final : public Device
{
public:
    enum Param : size_t
    {
        Reference,
        View,
        Notes,
        Mute,
        NumParams
    };

    /// Analysis bus slots. NaN while there's no reading.
    enum Analysis : int
    {
        Frequency = 0,   // Hz
        Note = 1,        // the note measured against, as a MIDI number (60 = C3)
        Cents = 2,       // from it, steadied (the median of the last three readings)
        RawCents = 3,    // the latest reading's
        Clarity = 4,     // 0 … 1: how periodic the sound is
        Level = 5,       // dBFS (always published)
        StrobePhase = 6, // 0 … 1, turning one way when sharp, the other when flat (always)
        KeyRoot = 7,     // the key it measures against while In key: its root (0 = C), −1 none
        KeyNotes = 8,    // and its notes (bit n: pitch class n)
        Since = 9,       // seconds since the last reading (0 while reading)
    };

    /// The history the panel draws (the last 12 seconds and more): one entry a reading, pitch as
    /// a fractional MIDI number against the Reference, NaN where nothing was heard.
    static constexpr int historySize = 2048;
    /// The strobe's turns a second per semitone off.
    static constexpr double strobeTurnsPerSemitone = 4.0;
    /// In tune within this many cents (TU4).
    static constexpr float inTuneCents = 3.0f;

    Tuner();

    void prepare(const ProcessSpec& spec) override;
    void reset() noexcept override;
    void process(ProcessContext& context) noexcept override;

    /// The history (message thread): how many readings were written, and one of them,
    /// `ago` readings back from the newest (0).
    [[nodiscard]] std::uint32_t historyWritten() const noexcept
    {
        return written_.load(std::memory_order_acquire);
    }
    [[nodiscard]] float historyAt(std::uint32_t ago) const noexcept;
    /// The time between readings.
    [[nodiscard]] double readingSeconds() const noexcept
    {
        return readingSeconds_.load(std::memory_order_relaxed);
    }

    /// The note to measure `pitch` (a fractional MIDI number) against: the nearest, or with
    /// `key` known the nearest of its notes (the lower one at a tie).
    [[nodiscard]] static int nearestNote(double pitch, const KeyInfo* key) noexcept;

private:
    void onReading(const dsp::PitchDetector::Reading& reading) noexcept;
    void publishNone() noexcept;

    dsp::PitchDetector detector_;
    dsp::LinearSmoother gain_;
    int fadeSamples_ = 480;
    double sampleRate_ = 48000.0;
    KeyInfo key_; // the key this block measures against (In key, a key known)
    std::optional<int> held_;
    std::array<float, 3> recent_{}; // the last cents, for the median
    int recentCount_ = 0;
    double strobe_ = 0.0, since_ = 1e9;
    float rawCents_ = NAN;
    std::array<std::atomic<float>, historySize> history_{};
    std::atomic<std::uint32_t> written_{0};
    std::atomic<double> readingSeconds_{0.01};
    std::vector<float> mono_;
};

const DeviceFactory& tunerFactory();
} // namespace fd::devices
