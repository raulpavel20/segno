#include "model/RoundTrip.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace fd::model
{
namespace
{
std::string ms(double value)
{
    char text[32];
    std::snprintf(text, sizeof text, "%.1f ms", value);
    return text;
}
} // namespace

RoundTripResult judgeRoundTrip(const RoundTripTries& tries, int reportedSamples, double sampleRate)
{
    RoundTripResult r;
    const double perSample = sampleRate > 0.0 ? 1000.0 / sampleRate : 0.0;
    r.reportedMs = reportedSamples * perSample;
    if (tries.inputPeak >= kRoundTripTooLoud)
    {
        r.problem = RoundTripResult::Problem::TooLoud;
        return r;
    }
    if (tries.samples.empty() ||
        std::any_of(tries.samples.begin(), tries.samples.end(), [](const auto& s) { return !s.has_value(); }))
    {
        r.problem = RoundTripResult::Problem::NothingCameBack;
        return r;
    }
    std::vector<int> found;
    for (const auto& s : tries.samples)
        found.push_back(*s);
    std::sort(found.begin(), found.end());
    r.spreadMs = (found.back() - found.front()) * perSample;
    if (r.spreadMs > kRoundTripSpreadMs)
    {
        r.problem = RoundTripResult::Problem::Disagree;
        return r;
    }
    r.roundTripMs = found[found.size() / 2] * perSample;
    r.offsetMs = r.roundTripMs - r.reportedMs;
    return r;
}

std::string roundTripText(const RoundTripResult& result)
{
    using P = RoundTripResult::Problem;
    switch (result.problem)
    {
    case P::NothingCameBack:
        return "Nothing came back: check the cable and the channels";
    case P::TooLoud:
        return "Too loud: turn the input down";
    case P::Disagree:
        return "The three tries disagree by " + ms(result.spreadMs) +
               ": try again, with nothing else playing";
    case P::None:
        break;
    }
    return "Round trip " + ms(result.roundTripMs) + " \xc2\xb7 the interface reports " +
           ms(result.reportedMs);
}

std::string offsetText(double value)
{
    const double tenths = std::round(value * 10.0) / 10.0;
    if (tenths == 0.0)
        return "0 ms";
    char text[32];
    if (tenths == std::round(tenths))
        std::snprintf(text, sizeof text, "%+.0f ms", tenths);
    else
        std::snprintf(text, sizeof text, "%+.1f ms", tenths);
    return text;
}
} // namespace fd::model
