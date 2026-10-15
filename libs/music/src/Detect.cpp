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
constexpr std::array<double, 12> majorProfile{6.35, 2.23, 3.48, 2.33, 4.38, 4.09,
                                              2.52, 5.19, 2.39, 3.66, 2.29, 2.88};
constexpr std::array<double, 12> minorProfile{6.33, 2.68, 3.52, 5.38, 2.60, 3.53,
                                              2.54, 4.75, 3.98, 2.69, 3.34, 3.17};

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

/// Every major and minor key, by how well it fits, best first.
std::vector<KeyCandidate> ranked(const std::array<double, 12>& weights)
{
    std::vector<KeyCandidate> all;
    for (int root = 0; root < 12; ++root)
    {
        all.push_back({Key{root, "major"}, correlation(weights, majorProfile, root)});
        all.push_back({Key{root, "minor"}, correlation(weights, minorProfile, root)});
    }
    std::stable_sort(all.begin(), all.end(), [](const auto& a, const auto& b) { return a.fit > b.fit; });
    return all;
}
} // namespace

std::vector<KeyCandidate> detectKeys(std::span<const DetectNote> notes, int count)
{
    return detectKeys(notes, std::array<double, 12>{}, count);
}

std::vector<KeyCandidate> detectKeys(std::span<const DetectNote> notes, const std::array<double, 12>& more,
                                     int count)
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
    const bool others = std::any_of(more.begin(), more.end(), [](double w) { return w > 0.0; });
    if (!others && (counted < detectMinNotes || classes < detectMinPitchClasses))
        return {};
    for (size_t pc = 0; pc < 12; ++pc)
        weights[pc] += std::max(0.0, more[pc]);
    return keysFromProfile(weights, count);
}

std::array<double, 12> keyProfile(const Key& key)
{
    std::array<double, 12> out{};
    const int root = ((key.root % 12) + 12) % 12;
    if (key.scale == "major" || key.scale == "minor")
    {
        const auto& profile = key.scale == "major" ? majorProfile : minorProfile;
        for (int i = 0; i < 12; ++i)
            out[static_cast<size_t>((root + i) % 12)] = profile[static_cast<size_t>(i)];
    }
    else
    {
        const PitchSet set = notes(key);
        for (int pc = 0; pc < 12; ++pc)
            if ((set & (1u << pc)) != 0)
                out[static_cast<size_t>(pc)] = pc == root ? 2.0 : 1.0;
    }
    double sum = 0.0;
    for (const double w : out)
        sum += w;
    if (sum > 0.0)
        for (auto& w : out)
            w /= sum;
    return out;
}

std::vector<KeyCandidate> keysFromProfile(const std::array<double, 12>& weights, int count)
{
    if (count <= 0 || std::none_of(weights.begin(), weights.end(), [](double w) { return w > 0.0; }))
        return {};
    std::vector<KeyCandidate> best;
    for (const auto& c : ranked(weights))
        if (c.fit > 0.0 && static_cast<int>(best.size()) < count)
            best.push_back(c);
    return best;
}

std::optional<KeyCandidate> clearKey(const std::array<double, 12>& weights)
{
    if (std::none_of(weights.begin(), weights.end(), [](double w) { return w > 0.0; }))
        return std::nullopt;
    const auto all = ranked(weights);
    const auto& best = all.front();
    if (best.fit < kClearKeyFit)
        return std::nullopt;
    const auto relative = relativeKey(best.key);
    for (size_t i = 1; i < all.size(); ++i)
        if (all[i].key != relative)
            return best.fit - all[i].fit >= kClearKeyMargin ? std::optional(best) : std::nullopt;
    return best;
}

std::optional<Key> relativeKey(const Key& key)
{
    if (key.scale == "major")
        return Key{(key.root + 9) % 12, "minor"};
    if (key.scale == "minor")
        return Key{(key.root + 3) % 12, "major"};
    return std::nullopt;
}
} // namespace fd::music
