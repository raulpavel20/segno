#pragma once

#include "fd/device/Device.h"
#include "fd/device/Registry.h"
#include "fd/dsp/Spectrum.h"

#include <array>
#include <atomic>
#include <mutex>
#include <nlohmann/json_fwd.hpp>
#include <optional>
#include <string>

namespace fd::devices
{
/// Shows a track's frequency content, compared with another track, a snapshot or a reference; the
/// audio passes unchanged. See devices/spectrum/SPEC.md.
class Spectrum final : public Device
{
public:
    enum Param : size_t
    {
        Channel,
        Resolution,
        Average,
        Hold,
        Slope,
        Range,
        Side,
        Labels,
        View,
        Length,
        NumParams
    };

    /// The analysis slot that says whether the song plays (1) or not (0): the spectrogram moves
    /// while it does (SPEC.md, stage 2).
    static constexpr int PlayingSlot = 0;

    /// The analysis FIFOs: the input's left and right, and the sidechain's (the compared track)
    /// while one is connected.
    enum Fifo : int
    {
        InputLeft = 0,
        InputRight = 1,
        CompareLeft = 2,
        CompareRight = 3
    };

    static const DeviceInfo& staticInfo() noexcept;

    Spectrum();

    void prepare(const ProcessSpec& spec) override;
    void reset() noexcept override {}
    void process(ProcessContext& context) noexcept override;

    void writeExtraState(nlohmann::json& extra) const override;
    void readExtraState(const nlohmann::json& extra) override;

    // ---- the display's settings, from the parameters' plain values ------------------------

    /// The FFT order for a Resolution choice (2 048 … 16 384 points: 11 … 14).
    [[nodiscard]] static int fftOrder(float resolution) noexcept;
    /// How long peaks are held for a Hold choice: nullopt for Off, negative for Infinite.
    [[nodiscard]] static std::optional<double> holdSeconds(float hold) noexcept;
    /// The dB the display spans for a Range choice.
    [[nodiscard]] static float rangeDb(float range) noexcept;
    /// The tilt in dB per octave for a Slope choice (0, 1.5, 3, 4.5 or 6).
    [[nodiscard]] static float slopeDb(float slope) noexcept;
    /// How many peaks are labelled for a Peak Labels choice.
    [[nodiscard]] static int labelCount(float labels) noexcept;
    /// The seconds the spectrogram shows for a Length choice (2, 5, 10 or 20).
    [[nodiscard]] static double lengthSeconds(float length) noexcept;

    // ---- non-parameter state (ADR 0017): changed only through the host binding -------------

    /// The frozen curve and the Channel it was taken with.
    struct Snapshot
    {
        dsp::SpectrumCurve curve{};
        int channel = 0;
    };
    /// A song's average spectrum, one curve per Channel choice.
    struct Reference
    {
        std::string name, path;
        std::array<dsp::SpectrumCurve, dsp::numSpectrumChannels> curves{};
    };
    [[nodiscard]] std::optional<Snapshot> snapshot() const;
    [[nodiscard]] std::optional<Reference> reference() const;
    /// Patches for the binding's applyExtraState: sets, or clears with nullopt.
    [[nodiscard]] static nlohmann::json snapshotPatch(const std::optional<Snapshot>& snapshot);
    [[nodiscard]] static nlohmann::json referencePatch(const std::optional<Reference>& reference);

private:
    mutable std::mutex stateMutex_; // the host restores state on its thread; the panel reads it
    std::optional<Snapshot> snapshot_;
    std::optional<Reference> reference_;
};

/// A file's average spectrum for each Channel choice (the reference curve), read block by block
/// so a whole song never sits in memory. Returns nullopt with `error` if it can't be read, and
/// nullopt as soon as `cancel` is set. Not real-time.
[[nodiscard]] std::optional<std::array<dsp::SpectrumCurve, dsp::numSpectrumChannels>>
analyseFile(const std::string& path, std::string* error = nullptr, const std::atomic<bool>* cancel = nullptr);

/// "62 Hz", "3.15 kHz": a frequency as every Hz value in the suite reads.
[[nodiscard]] std::string frequencyText(double hz);
/// The nearest note, named as everywhere in Segno (middle C is C3, so 440 Hz is A3).
[[nodiscard]] std::string noteText(double hz);
/// A peak label: "62 Hz · B0".
[[nodiscard]] std::string peakLabel(double hz);

const DeviceFactory& spectrumFactory();
} // namespace fd::devices
