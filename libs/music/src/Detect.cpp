#include "fd/music/Detect.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace fd::music
{
namespace
{
// Krumhansl and Kessler's key profiles (1982): how well each degree, from the tonic up, fits a
// major or a minor key.
constexpr std::array<double, 12> majorProfile{6.35, 2.23, 3.48, 2.33, 4.38, 4.09, 2.52, 5.19, 2.39, 3.66, 2.29, 2.88};
constexpr std::array<double, 12> minorProfile{6.33, 2.68, 3.52, 5.38, 2.60, 3.53, 2.54, 4.75, 3.98, 2.69, 3.34, 3.17};

double correlation(const std::array<double, 12>& weights, const std::array<double, 12>& profile, int root)
{
    double mw = 0.0, mp = 0.0;
    for (int i = 0; i < 12; ++i)
    {
        mw += weights[static_cast<size_t>(i)];
        mp += profile[static_cast<size_t>(i)];
    }
    mw /= 12.0;
    mp /= 12.0;
    double num = 0.0, dw = 0.0, dp = 0.0;
    for (int pc = 0; pc < 12; ++pc)
    {
        const double w = weights[static_cast<size_t>(pc)] - mw;
        const double p = profile[static_cast<size_t>((pc - root + 12) % 12)] - mp;
        num += w * p;
        dw += w * w;
        dp += p * p;
    }
    return dw > 0.0 && dp > 0.0 ? num / std::sqrt(dw * dp) : 0.0;
}
} // namespace

std::vector<KeyCandidate> detectKeys(std::span<const DetectNote> notes, int count)
{
    std::array<double, 12> weights{};
    int counted = 0;
    for (const auto& n : notes)
        if (n.length > 0.0)
        {
            weights[static_cast<size_t>(((n.pitch % 12) + 12) % 12)] += n.length;
            ++counted;
        }
    const auto classes = std::count_if(weights.begin(), weights.end(), [](double w) { return w > 0.0; });
    if (counted < detectMinNotes || classes < detectMinPitchClasses || count <= 0)
        return {};

    std::vector<KeyCandidate> all;
    for (int root = 0; root < 12; ++root)
    {
        all.push_back({Key{root, "major"}, correlation(weights, majorProfile, root)});
        all.push_back({Key{root, "minor"}, correlation(weights, minorProfile, root)});
    }
    std::stable_sort(all.begin(), all.end(), [](const auto& a, const auto& b) { return a.fit > b.fit; });
    std::vector<KeyCandidate> best;
    for (const auto& c : all)
        if (c.fit > 0.0 && static_cast<int>(best.size()) < count)
            best.push_back(c);
    return best;
}
} // namespace fd::music
