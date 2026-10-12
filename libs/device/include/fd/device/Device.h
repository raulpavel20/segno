#pragma once

#include "fd/device/AnalysisBus.h"
#include "fd/device/Events.h"
#include "fd/device/Param.h"
#include "fd/device/ParamStore.h"

#include <nlohmann/json_fwd.hpp>

#include <cstdint>
#include <span>
#include <string_view>

namespace fd
{
struct DeviceState;

/// Non-owning view of an audio bus for one block.
struct AudioBusView
{
    float* const* channels = nullptr;
    int numChannels = 0;

    [[nodiscard]] float* channel(int c) const noexcept { return channels[c]; }
};

struct TransportInfo
{
    double tempoBpm = 120.0;
    double ppqPosition = 0.0;
    std::int64_t samplePosition = 0;
    int timeSigNumerator = 4;
    int timeSigDenominator = 4;
    bool playing = false;
};

struct ProcessSpec
{
    double sampleRate = 48000.0;
    int maxBlockSize = 512;
};

/// Everything a device sees for one block. `input` and `output` may alias (in-place processing),
/// so devices must read a sample before writing it.
struct ProcessContext
{
    AudioBusView input;
    AudioBusView output;
    AudioBusView sidechain; // numChannels == 0 when not connected
    const EventList* events = nullptr;
    EventList* outEvents = nullptr;
    TransportInfo transport;
    int numSamples = 0;
};

enum class DeviceKind : std::uint8_t
{
    AudioEffect,
    Instrument,
    MidiEffect
};

struct BusConfig
{
    int inputChannels = 2;
    int outputChannels = 2;
    int sidechainChannels = 0;
    bool acceptsMidi = false;
    bool producesMidi = false;
};

/// Static description of a device type (one constexpr instance per device).
/// What a meter slot on the analysis bus holds, so hosts can read it by name (the MCP
/// `measure_module` tool) and convert it to its unit.
enum class MeterKind
{
    Level,         // a peak, linear (published with publishPeak): read as dBFS
    TruePeak,      // a true peak, linear (publishPeak): read as dBTP
    Reduction,     // gain reduction in dB as a positive number (publishPeak)
    Flag,          // 1 if it happened since last read (publishPeak), e.g. a gate opening
    Loudness,      // LUFS, a current value (publish)
    Decibels,      // a current value in dB (publish), e.g. a true-peak maximum
    Count,         // a current count (publish), e.g. voices playing
    LoudnessUnits, // LU, a current range or difference (publish): a loudness range; NaN for none
    Correlation,   // -1..1, a current value (publish): stereo correlation; NaN for none
};

/// A named meter: one analysis slot.
struct MeterSpec
{
    int slot = 0;
    std::string_view id;   // stable within the device, e.g. "gain_reduction"
    std::string_view name; // for people, e.g. "Gain reduction"
    MeterKind kind = MeterKind::Level;
};

struct DeviceInfo
{
    std::string_view id;       // permanent (docs/ids.md)
    std::string_view name;     // display name (working name until branding)
    std::string_view category; // e.g. "Utility", "EQ & Filter"
    DeviceKind kind = DeviceKind::AudioEffect;
    BusConfig buses{};
    std::span<const ParamSpec> params{};
    std::uint32_t stateVersion = 1;
    /// The analysis slots its displays read, by name (none for display-only data).
    std::span<const MeterSpec> meters{};
    /// Extra-state keys that belong to the session, not the sound: presets leave them out, and
    /// loading a preset leaves them as they are (ADR 0044; the Spectrum's snapshot).
    std::span<const std::string_view> sessionExtraKeys{};
};

/// Base class for every instrument and effect. Hosts: the plugin adapter (plugins/) and the
/// tracktion_engine adapter (app/). See docs/architecture.md, "Device contract".
class Device
{
public:
    explicit Device(const DeviceInfo& info) : info_(info), params_(info.params) {}
    virtual ~Device() = default;

    Device(const Device&) = delete;
    Device& operator=(const Device&) = delete;

    [[nodiscard]] const DeviceInfo& info() const noexcept { return info_; }
    [[nodiscard]] ParamStore& params() noexcept { return params_; }
    [[nodiscard]] const ParamStore& params() const noexcept { return params_; }
    [[nodiscard]] AnalysisBus& analysis() noexcept { return analysis_; }

    /// Called before processing starts or when the sample rate / max block size changes.
    /// May allocate. Implies reset().
    virtual void prepare(const ProcessSpec& spec) = 0;

    /// Clears all internal state (delay lines, envelopes, voices) and snaps smoothed values
    /// to the current parameters. Must be deterministic: same params + same input after
    /// reset() => same output.
    virtual void reset() noexcept = 0;

    /// Real-time: no allocation, locks, I/O or exceptions.
    virtual void process(ProcessContext& context) noexcept = 0;

    [[nodiscard]] virtual int latencySamples() const noexcept { return 0; }
    [[nodiscard]] virtual double tailSeconds() const noexcept { return 0.0; }

    /// Non-parameter state (sample references, wavetables...). Not real-time.
    virtual void writeExtraState(nlohmann::json& /*extra*/) const {}
    virtual void readExtraState(const nlohmann::json& /*extra*/) {}

    /// Upgrades state saved with an older stateVersion, in place, before it is applied.
    virtual void migrateState(DeviceState& /*state*/) const {}

private:
    const DeviceInfo& info_;
    ParamStore params_;
    AnalysisBus analysis_;
};
} // namespace fd
