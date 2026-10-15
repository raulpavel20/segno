#pragma once

#include <optional>
#include <string>
#include <vector>

namespace fd::model
{
/// Measuring the round trip (recording.md §22.2): a test signal played on an output, found coming
/// back on an input through a cable, three times. What the tries found, judged.
struct RoundTripTries
{
    /// Each try: the samples from playing the signal to finding it (nullopt: not found).
    std::vector<std::optional<int>> samples;
    /// The loudest the input got over the tries (full scale 1).
    float inputPeak = 0.0f;
};

struct RoundTripResult
{
    /// Why there's no figure (RL7, the reasons Ardour names): none, nothing found, the input
    /// clipping, or tries that don't agree.
    enum class Problem
    {
        None,
        NothingCameBack,
        TooLoud,
        Disagree
    };
    Problem problem = Problem::None;
    double roundTripMs = 0.0; // the middle try
    double reportedMs = 0.0;  // what the interface reports: its input and output latency
    double offsetMs = 0.0;    // what recordings need besides: roundTrip - reported (Audio offset)
    double spreadMs = 0.0;    // the tries' range

    [[nodiscard]] bool ok() const noexcept { return problem == Problem::None; }
};

/// The input is too loud from here (-3 dBFS): the measurement can't be trusted.
inline constexpr float kRoundTripTooLoud = 0.708f;
/// Tries further apart than this disagree.
inline constexpr double kRoundTripSpreadMs = 0.25;

/// Judges `tries` at `sampleRate`, the interface reporting `reportedSamples` (its input and output
/// latency): too loud first (a clipped input can't be trusted), then a try that found nothing, then
/// tries that disagree; else the middle try's round trip and the offset recordings need.
[[nodiscard]] RoundTripResult judgeRoundTrip(const RoundTripTries& tries, int reportedSamples,
                                             double sampleRate);

/// What the panel says about a result: "Round trip 12.4 ms · the interface reports 10.1 ms", or
/// why there's none ("Nothing came back: check the cable and the channels").
[[nodiscard]] std::string roundTripText(const RoundTripResult& result);

/// An offset as Settings shows it: "+2.3 ms", "-1 ms", "0 ms" (tenths where there are any).
[[nodiscard]] std::string offsetText(double ms);
} // namespace fd::model
