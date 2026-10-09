#include "model/Taper.h"

#include "model/Snapshot.h"

#include <algorithm>
#include <cmath>

namespace fd::model
{
namespace
{
constexpr double kVolumeZeroAt = 0.8;
// (1 / 0.8)^n = +6 dB: n = (6 / 20) ln 10 / ln 1.25.
const double kExponent = 0.3 * std::log(10.0) / std::log(1.25);

double dbAt(double position, double zeroAt)
{
    if (!(position > 0.0))
        return minusInfinityDb;
    return std::max<double>(minusInfinityDb, 20.0 * kExponent * std::log10(std::min(position, 1.0) / zeroAt));
}

double positionOf(double db, double zeroAt)
{
    if (db <= minusInfinityDb)
        return 0.0;
    return std::clamp(zeroAt * std::pow(10.0, db / (20.0 * kExponent)), 0.0, 1.0);
}
} // namespace

double volumeDbAt(double position) noexcept
{
    return dbAt(position, kVolumeZeroAt);
}

double volumePosition(double db) noexcept
{
    return positionOf(db, kVolumeZeroAt);
}

double sendDbAt(double position) noexcept
{
    return dbAt(position, 1.0);
}

double sendPosition(double db) noexcept
{
    return positionOf(db, 1.0);
}
} // namespace fd::model
