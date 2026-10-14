#include "SampleMap.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <map>
#include <set>
#include <thread>

namespace fd::devices
{
namespace
{
/// A multisample's zones: one decimated copy, a 16-bit file kept 16-bit (SPEC, "State, loading
/// and memory").
constexpr SampleData::Options kZoneOptions{.levels = 2, .compact = true};
} // namespace

std::uint32_t SampleMap::Choice::nextTurn(int key) noexcept
{
    return turns[static_cast<size_t>(std::clamp(key, 0, 127))]++;
}

float SampleMap::Choice::nextPick() noexcept
{
    random ^= random << 13;
    random ^= random >> 17;
    random ^= random << 5;
    return static_cast<float>(random >> 8) * (1.0f / 16777216.0f);
}

int SampleMap::choose(int key, int velocity, std::uint32_t turn, float pick,
                      std::array<int, maxChosen>& out) const noexcept
{
    int n = 0;
    for (size_t i = 0; i < zones.size() && n < maxChosen; ++i)
    {
        const auto& z = zones[i];
        if (!z.plays(key, velocity) || z.sample == nullptr || pick < z.loRand || pick >= z.hiRand)
            continue;
        if (z.seqLength > 1 &&
            static_cast<int>(turn % static_cast<std::uint32_t>(z.seqLength)) + 1 != z.seqPosition)
            continue;
        out[static_cast<size_t>(n++)] = static_cast<int>(i);
    }
    return n;
}

double SampleMap::longestSeconds() const noexcept
{
    double longest = 0.0;
    for (const auto& z : zones)
        if (z.sample != nullptr)
            longest = std::max(longest, z.sample->seconds());
    return longest;
}

int SampleMap::lowestKey() const noexcept
{
    int low = 127;
    for (const auto& z : zones)
        low = std::min(low, z.loKey);
    return zones.empty() ? 0 : low;
}

int SampleMap::highestKey() const noexcept
{
    int high = 0;
    for (const auto& z : zones)
        high = std::max(high, z.hiKey);
    return zones.empty() ? 127 : high;
}

size_t SampleMap::bytes() const noexcept
{
    std::set<const SampleData*> counted;
    size_t total = 0;
    for (const auto& z : zones)
        if (z.sample != nullptr && counted.insert(z.sample.get()).second)
            total += z.sample->bytes();
    return total;
}

std::shared_ptr<const SampleMap> SampleMap::one(std::shared_ptr<const SampleData> sample)
{
    auto m = std::make_shared<SampleMap>();
    if (sample == nullptr)
        return m;
    m->path = sample->path;
    Zone z;
    z.file = sample->path;
    z.sample = std::move(sample);
    m->zones.push_back(std::move(z));
    return m;
}

bool SampleMap::isSfz(std::string_view path) noexcept
{
    if (path.size() < 4)
        return false;
    const auto ext = path.substr(path.size() - 4);
    return ext.size() == 4 && ext[0] == '.' && (ext[1] == 's' || ext[1] == 'S') &&
           (ext[2] == 'f' || ext[2] == 'F') && (ext[3] == 'z' || ext[3] == 'Z');
}

std::shared_ptr<const SampleMap> SampleMap::loadSfz(const std::string& path, std::string* error)
{
    const auto file = sfz::read(path, error);
    if (!file)
        return nullptr;
    auto m = std::make_shared<SampleMap>();
    m->path = path;
    m->single = false;
    m->ignored = file->ignored;

    // Each file once, found ignoring case where it must be, decoded in parallel.
    std::vector<std::string> files;
    std::map<std::string, size_t> index;
    std::vector<size_t> fileOf;
    for (const auto& r : file->regions)
    {
        const auto found = sfz::findFile(r.sample);
        const auto name = found.empty() ? r.sample.string() : found.string();
        const auto [it, added] = index.emplace(name, files.size());
        if (added)
            files.push_back(found.empty() ? std::string() : name);
        fileOf.push_back(it->second);
    }
    std::vector<std::shared_ptr<const SampleData>> decoded(files.size());
    std::atomic<size_t> next{0};
    const auto work = [&]
    {
        for (size_t i = next++; i < files.size(); i = next++)
            if (!files[i].empty())
                decoded[i] = SampleData::load(files[i], nullptr, kZoneOptions);
    };
    const size_t threads = std::min<size_t>(files.size(), std::max(1u, std::thread::hardware_concurrency()));
    std::vector<std::thread> pool;
    for (size_t t = 1; t < threads; ++t)
        pool.emplace_back(work);
    work();
    for (auto& t : pool)
        t.join();

    for (size_t i = 0; i < file->regions.size(); ++i)
    {
        const auto& r = file->regions[i];
        Zone z;
        z.sample = decoded[fileOf[i]];
        z.file = files[fileOf[i]].empty() ? r.sample.string() : files[fileOf[i]];
        z.loKey = r.loKey;
        z.hiKey = r.hiKey;
        z.loVel = r.loVel;
        z.hiVel = r.hiVel;
        z.root = r.rootFromSample && z.sample != nullptr && z.sample->rootKey ? *z.sample->rootKey : r.root;
        z.tune = r.tune + 100.0 * r.transpose;
        z.gain = static_cast<float>(std::pow(10.0, r.volume / 20.0));
        z.start = static_cast<int>(std::max<std::int64_t>(0, r.offset));
        z.end = r.end >= 0 ? static_cast<int>(r.end + 1) : 0;
        const bool loopMode = r.loopMode == sfz::LoopMode::Continuous || r.loopMode == sfz::LoopMode::Sustain;
        if (loopMode && r.loopEnd > r.loopStart && r.loopStart >= 0)
        {
            z.loops = true;
            z.loopStart = static_cast<int>(r.loopStart);
            z.loopEnd = static_cast<int>(r.loopEnd + 1);
        }
        z.seqLength = r.seqLength;
        z.seqPosition = r.seqPosition;
        z.loRand = static_cast<float>(r.loRand);
        z.hiRand = static_cast<float>(r.hiRand);
        if (z.sample == nullptr)
            ++m->missing;
        m->zones.push_back(std::move(z));
    }

    auto& s = m->settings;
    s.ampAttack = file->ampAttack;
    s.ampDecay = file->ampDecay;
    s.ampSustain = file->ampSustain;
    s.ampRelease = file->ampRelease;
    s.velocityToVolume = file->ampVeltrack;
    s.oneShot = file->loopMode == sfz::LoopMode::OneShot;
    s.loop = file->loopMode == sfz::LoopMode::Continuous || file->loopMode == sfz::LoopMode::Sustain;
    return m;
}
} // namespace fd::devices
