#include "fd/dsp/WavetableOscillator.h"

#include "fd/dsp/Math.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace fd::dsp
{
namespace
{
std::uint32_t xorshift(std::uint32_t& s) noexcept
{
    s ^= s << 13;
    s ^= s >> 17;
    s ^= s << 5;
    return s;
}

double unit(std::uint32_t& s) noexcept // 0 ... 1
{
    return static_cast<double>(xorshift(s) >> 8) / static_cast<double>(1u << 24);
}

constexpr int kStacks[][3] = {{0, 0, 0},  {0, 12, 12}, {0, 12, 24}, {0, 7, 7},
                              {0, 7, 12}, {0, 4, 7},   {0, 3, 7}};
constexpr int kStackSize[] = {1, 2, 3, 2, 3, 3, 3, 3};
} // namespace

void WavetableOscillator::start(std::uint32_t seed, float phase, float phaseRandom) noexcept
{
    seed_ = seed == 0 ? 1 : seed;
    std::uint32_t s = seed_;
    for (int i = 0; i < maxUnison; ++i)
    {
        const double r = unit(s);
        const double p = phase + phaseRandom * r;
        phase_[static_cast<size_t>(i)] = p - std::floor(p);
        randomDetune_[static_cast<size_t>(i)] = static_cast<float>(2.0 * unit(s) - 1.0);
    }
    rendered_ = false;
    fade_ = 0;
    for (auto& c : cycles_)
    {
        c.position = -1.0;
        c.harmonics = 0;
    }
    setUnison(unison_); // Random's detunes are per note
}

void WavetableOscillator::setUnison(const Unison& u) noexcept
{
    unison_ = u;
    count_ = std::clamp(u.count, 1, maxUnison);
    const double reach = static_cast<double>(u.detune) * u.detune; // semitones either side, at the edges
    const int stack = static_cast<int>(u.stack);
    const int stackSize = kStackSize[stack];
    // Copies from flat to sharp; the middle one (or pair) is the "centre" Blend weighs.
    double power = 0.0;
    widest_ = 1.0;
    const int middle = count_ / 2;
    for (int i = 0; i < count_; ++i)
    {
        const auto k = static_cast<size_t>(i);
        const double t = count_ == 1 ? 0.0 : -1.0 + 2.0 * i / (count_ - 1);
        double law = t;
        switch (u.mode)
        {
        case UnisonMode::Even:
            break;
        case UnisonMode::Super: // fine in the middle, steep at the edges
            law = std::copysign(t * t, t);
            break;
        case UnisonMode::Exponential:
            law = std::copysign((std::exp2(4.0 * std::abs(t)) - 1.0) / 15.0, t);
            break;
        case UnisonMode::Random:
            law = count_ == 1 ? 0.0 : randomDetune_[k];
            break;
        }
        // The stack's intervals, the middle copy keeping the note itself.
        const int slot = ((i - middle) % stackSize + stackSize) % stackSize;
        const int interval =
            u.stack == Stack::KeyChord ? u.keyChord[static_cast<size_t>(slot % 3)] : kStacks[stack][slot];
        ratio_[k] = std::exp2((reach * law + interval) / 12.0);
        widest_ = std::max(widest_, ratio_[k]);
        const bool centre = count_ == 1 || i == middle || (count_ % 2 == 0 && i == middle - 1);
        const double weight = centre ? 1.0 : static_cast<double>(u.blend);
        level_[k] = static_cast<float>(weight);
        power += weight * weight;
        // Equal power, flat copies to the left and sharp to the right; 0 dB in the middle.
        const double angle = (static_cast<double>(u.width) * t + 1.0) * 0.25 * pi;
        gainL_[k] = static_cast<float>(std::sqrt(2.0) * std::cos(angle));
        gainR_[k] = static_cast<float>(std::sqrt(2.0) * std::sin(angle));
    }
    const auto norm = static_cast<float>(1.0 / std::sqrt(std::max(power, 1e-12)));
    for (int i = 0; i < count_; ++i)
    {
        level_[static_cast<size_t>(i)] *= norm;
        gainL_[static_cast<size_t>(i)] *= level_[static_cast<size_t>(i)];
        gainR_[static_cast<size_t>(i)] *= level_[static_cast<size_t>(i)];
    }
    const bool spread = u.positionSpread > 0.0f || u.warpSpread > 0.0f;
    numCycles_ = spread ? std::min(count_, maxCycles) : 1;
    for (int i = 0; i < count_; ++i)
        cycleOf_[static_cast<size_t>(i)] = static_cast<std::uint8_t>(
            numCycles_ == 1
                ? 0
                : static_cast<int>(std::lround(static_cast<double>(i) * (numCycles_ - 1) / (count_ - 1))));
}

void WavetableOscillator::render(CycleRenderer& renderer, const Wavetable& table, double position,
                                 const std::array<CycleRenderer::WarpSetting, 2>& warps, double maxIncrement,
                                 double sampleRate, int fadeSamples) noexcept
{
    // The highest harmonic the fastest copy can play without passing 20 kHz (or 0.45 of the rate).
    const double top = std::min(20000.0 / sampleRate, 0.45);
    const double fastest = std::max(1e-9, std::abs(maxIncrement) * widest_);
    const int harmonics = std::clamp(static_cast<int>(top / fastest), 1, Wavetable::maxHarmonic);

    bool changed = !rendered_ || &table != table_;
    std::array<double, maxCycles> positions{};
    std::array<std::array<float, 2>, maxCycles> amounts{};
    for (int j = 0; j < numCycles_; ++j)
    {
        const double spreadAt = numCycles_ == 1 ? 0.0 : static_cast<double>(j) / (numCycles_ - 1) - 0.5;
        positions[static_cast<size_t>(j)] =
            std::clamp(position + unison_.positionSpread * spreadAt, 0.0, 1.0);
        for (size_t w = 0; w < 2; ++w)
            amounts[static_cast<size_t>(j)][w] =
                std::clamp(warps[w].amount + unison_.warpSpread * static_cast<float>(spreadAt), 0.0f, 1.0f);
        const auto& c = cycles_[static_cast<size_t>(j)];
        changed = changed || c.harmonics != harmonics ||
                  std::abs(c.position - positions[static_cast<size_t>(j)]) > 1e-6 ||
                  c.warps[0] != warps[0].warp || c.warps[1] != warps[1].warp ||
                  std::abs(c.amounts[0] - amounts[static_cast<size_t>(j)][0]) > 1e-6f ||
                  std::abs(c.amounts[1] - amounts[static_cast<size_t>(j)][1]) > 1e-6f;
    }
    if (!changed)
        return;
    for (int j = 0; j < numCycles_; ++j)
    {
        auto& c = cycles_[static_cast<size_t>(j)];
        if (rendered_)
        {
            std::memcpy(c.before.data(), c.now.data(),
                        sizeof(float) * static_cast<size_t>(c.size + 2 * CycleRenderer::guard));
            c.sizeBefore = c.size;
        }
        std::array<CycleRenderer::WarpSetting, 2> w = warps;
        w[0].amount = amounts[static_cast<size_t>(j)][0];
        w[1].amount = amounts[static_cast<size_t>(j)][1];
        c.size = renderer.render(table, positions[static_cast<size_t>(j)], w, harmonics, c.now.data());
        if (!rendered_)
        {
            std::memcpy(c.before.data(), c.now.data(),
                        sizeof(float) * static_cast<size_t>(c.size + 2 * CycleRenderer::guard));
            c.sizeBefore = c.size;
        }
        c.position = positions[static_cast<size_t>(j)];
        c.warps = {warps[0].warp, warps[1].warp};
        c.amounts = amounts[static_cast<size_t>(j)];
        c.harmonics = harmonics;
    }
    fade_ = rendered_ ? std::max(1, fadeSamples) : 0;
    fadeLength_ = std::max(1, fadeSamples);
    rendered_ = true;
    table_ = &table;
}

float WavetableOscillator::next(double increment, double fm, double phaseOffset, float& left,
                                float& right) noexcept
{
    if (!rendered_)
        return 0.0f;
    const float fresh = fade_ > 0 ? 1.0f - static_cast<float>(fade_) / static_cast<float>(fadeLength_) : 1.0f;
    float mono = 0.0f, l = 0.0f, r = 0.0f;
    for (int i = 0; i < count_; ++i)
    {
        const auto k = static_cast<size_t>(i);
        const auto& c = cycles_[cycleOf_[k]];
        double p = phase_[k] + phaseOffset;
        p -= std::floor(p);
        const auto read = [this, p](const float* buffer, int size)
        {
            const float* cycle = CycleRenderer::cycle(buffer);
            return fine_ ? readSinc(cycle, size, p) : readCubic(cycle, size, p);
        };
        float v = read(c.now.data(), c.size);
        if (fresh < 1.0f)
        {
            const float old = read(c.before.data(), c.sizeBefore);
            v = old + fresh * (v - old);
        }
        mono += v * level_[k];
        l += v * gainL_[k];
        r += v * gainR_[k];
        double next = phase_[k] + increment * ratio_[k] * fm;
        phase_[k] = next - std::floor(next);
    }
    if (fade_ > 0)
        --fade_;
    left += l;
    right += r;
    return mono;
}

// ---- noise -----------------------------------------------------------------------------------

void ColouredNoise::reset(std::uint32_t seed) noexcept
{
    state_ = seed == 0 ? 1 : seed;
    pink_ = {};
    brown_ = held_ = holdPhase_ = breathEnv_ = breathLp_ = breathBp_ = tiltLow_ = 0.0;
    metalPhase_ = {};
}

float ColouredNoise::white() noexcept
{
    return static_cast<float>(2.0 * unit(state_) - 1.0);
}

float ColouredNoise::next(Type type, float colour, double sampleRate) noexcept
{
    double x = 0.0;
    switch (type)
    {
    case Type::White:
        x = white();
        break;
    case Type::Pink: // Kellet's filters, about -3 dB per octave
    {
        const double w = white();
        auto& b = pink_;
        b[0] = 0.99886 * b[0] + w * 0.0555179;
        b[1] = 0.99332 * b[1] + w * 0.0750759;
        b[2] = 0.96900 * b[2] + w * 0.1538520;
        b[3] = 0.86650 * b[3] + w * 0.3104856;
        b[4] = 0.55000 * b[4] + w * 0.5329522;
        b[5] = -0.7616 * b[5] - w * 0.0168980;
        x = 0.2 * (b[0] + b[1] + b[2] + b[3] + b[4] + b[5] + b[6] + w * 0.5362);
        b[6] = w * 0.115926;
        break;
    }
    case Type::Brown: // integrated, leaking back to zero
        brown_ = (brown_ + 0.02 * white()) / 1.02;
        x = 3.5 * brown_;
        break;
    case Type::Digital: // held at 4 kHz: steps
        holdPhase_ += 4000.0 / sampleRate;
        if (holdPhase_ >= 1.0)
        {
            holdPhase_ -= std::floor(holdPhase_);
            held_ = white();
        }
        x = held_;
        break;
    case Type::Crackle: // sparse clicks, about 40 a second, each decaying fast
        if (unit(state_) < 40.0 / sampleRate)
            held_ = white();
        else
            held_ *= std::exp(-1.0 / (0.0004 * sampleRate));
        x = held_;
        break;
    case Type::Breath: // a band of noise around 1–3 kHz, slowly swelling
    {
        const double w = white();
        breathLp_ += (1.0 - std::exp(-twoPi * 3000.0 / sampleRate)) * (w - breathLp_);
        breathBp_ += (1.0 - std::exp(-twoPi * 800.0 / sampleRate)) * (breathLp_ - breathBp_);
        breathEnv_ += (1.0 - std::exp(-twoPi * 2.0 / sampleRate)) * (std::abs(white()) - breathEnv_);
        x = 3.0 * (breathLp_ - breathBp_) * (0.4 + 1.2 * breathEnv_);
        break;
    }
    case Type::Metallic: // inharmonic partials, ring-modulated in pairs
    {
        constexpr double hz[] = {317.0, 452.3, 538.9, 799.1, 1147.4, 1663.0};
        double tone[6];
        for (size_t i = 0; i < 6; ++i)
        {
            metalPhase_[i] += hz[i] / sampleRate;
            metalPhase_[i] -= std::floor(metalPhase_[i]);
            tone[i] = std::sin(twoPi * metalPhase_[i]);
        }
        x = 0.6 * (tone[0] * tone[3] + tone[1] * tone[4] + tone[2] * tone[5]) + 0.15 * white();
        break;
    }
    }
    // Tilt around 1 kHz: darker below zero, brighter above.
    tiltLow_ += (1.0 - std::exp(-twoPi * 1000.0 / sampleRate)) * (x - tiltLow_);
    const double high = x - tiltLow_;
    const double c = std::clamp(static_cast<double>(colour), -1.0, 1.0);
    return static_cast<float>((1.0 - c) * tiltLow_ + (1.0 + c) * high);
}
} // namespace fd::dsp
