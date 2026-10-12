#include "model/MusicalTime.h"

#include <cmath>
#include <sstream>

namespace fd::model
{
namespace
{
constexpr double kEps = 1e-6;

std::string count(double n, const char* one, const char* many)
{
    std::ostringstream out;
    if (std::abs(n - std::round(n)) < kEps)
        out << std::llround(n);
    else
        out << std::round(n * 100.0) / 100.0;
    out << ' ' << (std::abs(n - 1.0) < kEps ? one : many);
    return out.str();
}
} // namespace

std::string lengthText(double beats, const TempoMap& tempo)
{
    const double perBar = tempo.beatsPerBar();
    const double bars = std::floor(beats / perBar + kEps);
    // The rest in the signature's beats (an eighth in 6/8).
    const double rest = (beats - bars * perBar) * tempo.denominator / 4.0;
    if (bars >= 1.0 && rest < kEps)
        return count(bars, "bar", "bars");
    if (bars < 1.0)
        return count(rest, "beat", "beats");
    return count(bars, "bar", "bars") + " " + count(rest, "beat", "beats");
}
} // namespace fd::model
