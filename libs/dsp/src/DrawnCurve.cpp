#include "fd/dsp/DrawnCurve.h"

#include "fd/dsp/Math.h"
#include "fd/dsp/VoiceModules.h"

#include <algorithm>
#include <cmath>

namespace fd::dsp
{
namespace
{
constexpr std::string_view kNamedShapes[] = {"sine", "triangle", "saw-up", "saw-down", "square", "steps"};
} // namespace

std::span<const std::string_view> namedCurveShapes() noexcept
{
    return kNamedShapes;
}

CurveTable DrawnCurve::table() const
{
    CurveTable t{};
    const auto n = t.size() - 1;
    for (size_t i = 0; i <= n; ++i)
    {
        const double x = static_cast<double>(i) / n;
        double y = 0.0;
        if (!points.empty())
        {
            if (x <= points.front()[0])
                y = points.front()[1];
            else if (x >= points.back()[0])
                y = points.back()[1];
            else
                for (size_t k = 0; k + 1 < points.size(); ++k)
                    if (x >= points[k][0] && x <= points[k + 1][0])
                    {
                        const double span =
                            std::max(1e-9, static_cast<double>(points[k + 1][0] - points[k][0]));
                        const double bent = CurvedEnvelope::shape((x - points[k][0]) / span, points[k][2]);
                        y = points[k][1] + (points[k + 1][1] - points[k][1]) * bent;
                        break;
                    }
        }
        else if (shape == "triangle") // from the middle, up first, as the sine
            y = 0.5 + 0.5 * (1.0 - 4.0 * std::abs(std::fmod(x + 0.25, 1.0) - 0.5));
        else if (shape == "saw-up")
            y = x;
        else if (shape == "saw-down")
            y = 1.0 - x;
        else if (shape == "square")
            y = x < 0.5 ? 1.0 : 0.0;
        else if (shape == "steps")
            y = std::min(3.0, std::floor(x * 4.0)) / 3.0;
        else // sine
            y = 0.5 + 0.5 * std::sin(twoPi * x);
        t[i] = static_cast<float>(std::clamp(y, 0.0, 1.0));
    }
    return t;
}

std::vector<std::array<float, 3>> editablePoints(const DrawnCurve& c)
{
    if (!c.points.empty())
        return c.points;
    if (c.shape == "triangle")
        return {{0.0f, 0.5f, 0.0f}, {0.25f, 1.0f, 0.0f}, {0.75f, 0.0f, 0.0f}, {1.0f, 0.5f, 0.0f}};
    if (c.shape == "saw-up")
        return {{0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 0.0f}};
    if (c.shape == "saw-down")
        return {{0.0f, 1.0f, 0.0f}, {1.0f, 0.0f, 0.0f}};
    if (c.shape == "square")
        return {{0.0f, 1.0f, 0.0f}, {0.5f, 1.0f, 0.0f}, {0.5f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}};
    if (c.shape == "steps")
        return {{0.0f, 0.0f, 0.0f},        {0.25f, 0.0f, 0.0f},       {0.25f, 1.0f / 3.0f, 0.0f},
                {0.5f, 1.0f / 3.0f, 0.0f}, {0.5f, 2.0f / 3.0f, 0.0f}, {0.75f, 2.0f / 3.0f, 0.0f},
                {0.75f, 1.0f, 0.0f},       {1.0f, 1.0f, 0.0f}};
    // The sine: quarters bent towards its own roundness.
    return {{0.0f, 0.5f, 0.3f},
            {0.25f, 1.0f, -0.3f},
            {0.5f, 0.5f, 0.3f},
            {0.75f, 0.0f, -0.3f},
            {1.0f, 0.5f, 0.0f}};
}
} // namespace fd::dsp
