#include "fd/dsp/VoiceModules.h"

#include "fd/dsp/Math.h"

#include <algorithm>
#include <cmath>
#include <complex>

namespace fd::dsp
{
namespace
{
using C = std::complex<double>;

double resonanceK(double resonance) noexcept // the state-variable filter's damping: Q 0.5 ... 25
{
    return 1.0 / (0.5 * std::pow(50.0, std::clamp(resonance, 0.0, 1.0)));
}

/// The second stage of a 24 dB filter: Butterworth (Q 1.31) at no resonance, up to Q 25.
double secondStageK(double resonance) noexcept
{
    return 1.0 / std::max(1.3066, 0.5 * std::pow(50.0, std::clamp(resonance, 0.0, 1.0)));
}
constexpr double kFirstStageK = 1.0 / 0.5412;

/// The vowels the formant filter morphs through (A, E, I, O, U): three formants each, in hertz,
/// and their levels.
constexpr double kVowels[5][3] = {{800.0, 1150.0, 2900.0},
                                  {350.0, 2000.0, 2800.0},
                                  {270.0, 2140.0, 2950.0},
                                  {450.0, 800.0, 2830.0},
                                  {325.0, 700.0, 2700.0}};
constexpr double kFormantGain[3] = {1.0, 0.5, 0.25};

std::array<double, 3> formantsAt(double shape, double cutoff) noexcept
{
    const double at = std::clamp(shape, 0.0, 1.0) * 4.0;
    const int i = std::min(3, static_cast<int>(at));
    const double t = at - i;
    const double shift = std::clamp(cutoff, 20.0, 20000.0) / 1000.0; // cutoff 1 kHz: as sung
    std::array<double, 3> f{};
    for (size_t k = 0; k < 3; ++k)
        f[k] = shift * std::exp(std::log(kVowels[i][k]) * (1.0 - t) + std::log(kVowels[i + 1][k]) * t);
    return f;
}

double formantK(double resonance) noexcept
{
    return 1.0 / (4.0 + 16.0 * std::clamp(resonance, 0.0, 1.0));
}

double phaserFreq(int stage, double cutoff, double shape) noexcept
{
    return cutoff * std::exp2((stage - 2.5) * std::clamp(shape, 0.0, 1.0) * 0.6);
}

double gOf(double hz, double sampleRate) noexcept
{
    return std::tan(pi * std::clamp(hz, 10.0, 0.49 * sampleRate) / sampleRate);
}

/// The bilinear (TPT) analog frequency variable at `freq` for a stage tuned to `hz`.
C sOf(double freq, double hz, double sampleRate) noexcept
{
    return {0.0, std::tan(pi * std::clamp(freq, 0.0, 0.499 * sampleRate) / sampleRate) / gOf(hz, sampleRate)};
}
} // namespace

// ---- the filter ------------------------------------------------------------------------------

void VoiceFilter::Svf::tick(double x, double g, double k) noexcept
{
    const double a1 = 1.0 / (1.0 + g * (g + k));
    const double a2 = g * a1;
    const double a3 = g * a2;
    const double v3 = x - s2;
    const double v1 = a1 * s1 + a2 * v3;
    const double v2 = s2 + a2 * s1 + a3 * v3;
    s1 = 2.0 * v1 - s1;
    s2 = 2.0 * v2 - s2;
    lp = v2;
    bp = v1;
    hp = x - k * v1 - v2;
}

void VoiceFilter::prepare(double sampleRate)
{
    sampleRate_ = sampleRate > 0.0 ? sampleRate : 48000.0;
    int size = 4;
    while (size < static_cast<int>(sampleRate_ / 20.0) + 8)
        size <<= 1;
    comb_.assign(static_cast<size_t>(size), 0.0f);
    combMask_ = size - 1;
    reset();
}

void VoiceFilter::reset() noexcept
{
    a_ = b_ = {};
    formants_ = {};
    ladder_.reset();
    diode_ = {};
    phaser_ = {};
    phaserLast_ = 0.0;
    std::fill(comb_.begin(), comb_.end(), 0.0f);
    combWrite_ = 0;
    combDamp_ = 0.0;
}

float VoiceFilter::process(float input, Type type, double cutoff, double resonance, double drive,
                           double shape) noexcept
{
    double x = input;
    if (drive > 0.0)
    {
        // Soft saturation: louder and rounder as it's pushed.
        const double d = 1.0 + 15.0 * std::clamp(drive, 0.0, 1.0);
        x = std::tanh(x * d) / std::sqrt(d);
    }
    const double fs = sampleRate_;
    switch (type)
    {
    case Type::Lp12:
    case Type::Hp12:
    case Type::Bp12:
    case Type::Notch:
    case Type::Peak:
    {
        const double k = resonanceK(resonance);
        a_.tick(x, gOf(cutoff, fs), k);
        switch (type)
        {
        case Type::Lp12:
            return static_cast<float>(a_.lp);
        case Type::Hp12:
            return static_cast<float>(a_.hp);
        case Type::Bp12:
            return static_cast<float>(k * a_.bp);
        case Type::Notch:
            return static_cast<float>(a_.lp + a_.hp);
        default:
            return static_cast<float>(a_.lp - a_.hp);
        }
    }
    case Type::Lp24:
    case Type::Hp24:
    {
        const double g = gOf(cutoff, fs);
        a_.tick(x, g, kFirstStageK);
        b_.tick(type == Type::Lp24 ? a_.lp : a_.hp, g, secondStageK(resonance));
        return static_cast<float>(type == Type::Lp24 ? b_.lp : b_.hp);
    }
    case Type::Bp24:
    {
        const double g = gOf(cutoff, fs), k = resonanceK(resonance);
        a_.tick(x, g, k);
        b_.tick(k * a_.bp, g, k);
        return static_cast<float>(k * b_.bp);
    }
    case Type::Ladder:
        return ladder_.process(static_cast<float>(x), cutoff, resonance, fs);
    case Type::Diode:
    {
        // Three one-pole stages with zero-delay feedback through a soft clip: a rounder,
        // rubbery resonance than the ladder's (self-oscillation above k = 8).
        const double g = gOf(cutoff, fs);
        const double G = g / (1.0 + g);
        const double k = 8.4 * std::clamp(resonance, 0.0, 1.0);
        const double S = (G * G * diode_[0] + G * diode_[1] + diode_[2]) / (1.0 + g);
        const double in = x * (1.0 + 0.25 * k);
        const double u = std::tanh((in - k * S) / (1.0 + k * G * G * G));
        double y = u;
        for (double& s : diode_)
        {
            const double v = (y - s) * G;
            const double out = v + s;
            s = out + v;
            y = out;
        }
        return static_cast<float>(y);
    }
    case Type::CombPlus:
    case Type::CombMinus:
    {
        // A delay of one period of the cutoff, fed back through a damping low pass: resonances
        // at its harmonics (Comb +) or at its odd ones (Comb -).
        const double delay = std::clamp(fs / std::max(cutoff, 20.0), 2.0, static_cast<double>(combMask_ - 4));
        const double at = combWrite_ - delay;
        const double floor = std::floor(at);
        const auto t = static_cast<float>(at - floor);
        const auto i = static_cast<int>(floor);
        const float x0 = comb_[static_cast<size_t>((i - 1) & combMask_)],
                    x1 = comb_[static_cast<size_t>(i & combMask_)],
                    x2 = comb_[static_cast<size_t>((i + 1) & combMask_)],
                    x3 = comb_[static_cast<size_t>((i + 2) & combMask_)];
        const double delayed =
            x1 +
            0.5f * t *
                (x2 - x0 + t * (2.0f * x0 - 5.0f * x1 + 4.0f * x2 - x3 + t * (3.0f * (x1 - x2) + x3 - x0)));
        const double damp = 0.95 * std::clamp(shape, 0.0, 1.0);
        combDamp_ += (1.0 - damp) * (delayed - combDamp_);
        const double feedback = (type == Type::CombPlus ? 0.98 : -0.98) * std::clamp(resonance, 0.0, 1.0);
        const double y = x + feedback * combDamp_;
        comb_[static_cast<size_t>(combWrite_ & combMask_)] = static_cast<float>(std::clamp(y, -8.0, 8.0));
        ++combWrite_;
        return static_cast<float>(y * (1.0 - 0.5 * std::abs(feedback)));
    }
    case Type::Formant:
    {
        const auto f = formantsAt(shape, cutoff);
        const double k = formantK(resonance);
        double y = 0.0;
        for (size_t i = 0; i < 3; ++i)
        {
            formants_[i].tick(x, gOf(f[i], fs), k);
            y += kFormantGain[i] * k * formants_[i].bp;
        }
        return static_cast<float>(2.0 * y);
    }
    case Type::Phaser:
    {
        // Six all-passes spread around the cutoff, fed back by the resonance, half mixed with
        // the dry sound: notches that move with the cutoff.
        const double feedback = 0.9 * std::clamp(resonance, 0.0, 1.0);
        double y = x + feedback * phaserLast_;
        for (int i = 0; i < 6; ++i)
        {
            const double g = gOf(phaserFreq(i, cutoff, shape), fs);
            const double G = g / (1.0 + g);
            auto& s = phaser_[static_cast<size_t>(i)];
            const double v = (y - s) * G;
            const double lp = v + s;
            s = lp + v;
            y = 2.0 * lp - y;
        }
        phaserLast_ = y;
        return static_cast<float>(0.5 * (x + y));
    }
    }
    return static_cast<float>(x);
}

double VoiceFilter::magnitude(Type type, double freq, double cutoff, double resonance, double shape,
                              double sampleRate) noexcept
{
    const auto svf = [&](double hz, double k, int output) // 0 lp, 1 bp (unity peak), 2 hp
    {
        const C s = sOf(freq, hz, sampleRate);
        const C d = s * s + k * s + 1.0;
        return output == 0 ? 1.0 / d : output == 1 ? k * s / d : s * s / d;
    };
    switch (type)
    {
    case Type::Lp12:
        return std::abs(svf(cutoff, resonanceK(resonance), 0));
    case Type::Hp12:
        return std::abs(svf(cutoff, resonanceK(resonance), 2));
    case Type::Bp12:
        return std::abs(svf(cutoff, resonanceK(resonance), 1));
    case Type::Notch:
    {
        const double k = resonanceK(resonance);
        return std::abs(svf(cutoff, k, 0) + svf(cutoff, k, 2));
    }
    case Type::Peak:
    {
        const double k = resonanceK(resonance);
        return std::abs(svf(cutoff, k, 0) - svf(cutoff, k, 2));
    }
    case Type::Lp24:
        return std::abs(svf(cutoff, kFirstStageK, 0) * svf(cutoff, secondStageK(resonance), 0));
    case Type::Hp24:
        return std::abs(svf(cutoff, kFirstStageK, 2) * svf(cutoff, secondStageK(resonance), 2));
    case Type::Bp24:
    {
        const C b = svf(cutoff, resonanceK(resonance), 1);
        return std::abs(b * b);
    }
    case Type::Ladder:
        return LadderFilter::magnitude(freq, cutoff, resonance, sampleRate);
    case Type::Diode:
    {
        const C s = sOf(freq, cutoff, sampleRate);
        const double k = 8.4 * std::clamp(resonance, 0.0, 1.0);
        const C stage = 1.0 / (1.0 + s);
        const C g3 = stage * stage * stage;
        return std::abs((1.0 + 0.25 * k) * g3 / (1.0 + k * g3));
    }
    case Type::CombPlus:
    case Type::CombMinus:
    {
        const double w = twoPi * freq / sampleRate;
        const double delay = std::clamp(sampleRate / std::max(cutoff, 20.0), 2.0, sampleRate / 20.0);
        const double damp = 0.95 * std::clamp(shape, 0.0, 1.0);
        const C z1 = std::polar(1.0, -w);
        const C lowpass = (1.0 - damp) / (1.0 - damp * z1);
        const double feedback = (type == Type::CombPlus ? 0.98 : -0.98) * std::clamp(resonance, 0.0, 1.0);
        return std::abs((1.0 - 0.5 * std::abs(feedback)) /
                        (1.0 - feedback * lowpass * std::polar(1.0, -w * delay)));
    }
    case Type::Formant:
    {
        const auto f = formantsAt(shape, cutoff);
        const double k = formantK(resonance);
        C y;
        for (size_t i = 0; i < 3; ++i)
            y += kFormantGain[i] * svf(f[i], k, 1);
        return std::abs(2.0 * y);
    }
    case Type::Phaser:
    {
        C chain = 1.0;
        for (int i = 0; i < 6; ++i)
        {
            const C s = sOf(freq, phaserFreq(i, cutoff, shape), sampleRate);
            chain *= (1.0 - s) / (1.0 + s);
        }
        // Fed back a sample late, as process() does.
        const double feedback = 0.9 * std::clamp(resonance, 0.0, 1.0);
        const C late = std::polar(1.0, -twoPi * freq / sampleRate);
        return std::abs(0.5 * (1.0 + chain / (1.0 - feedback * late * chain)));
    }
    }
    return 1.0;
}

// ---- the envelope ----------------------------------------------------------------------------

double CurvedEnvelope::shape(double u, double curve) noexcept
{
    u = std::clamp(u, 0.0, 1.0);
    const double a = 8.0 * std::clamp(curve, -1.0, 1.0);
    if (std::abs(a) < 1e-6)
        return u;
    return (1.0 - std::exp(-a * u)) / (1.0 - std::exp(-a));
}

void CurvedEnvelope::reset() noexcept
{
    stage_ = Stage::Idle;
    level_ = 0.0;
    released_ = false;
}

void CurvedEnvelope::noteOn() noexcept
{
    released_ = false;
    enter(Stage::Attack);
}

void CurvedEnvelope::noteOff() noexcept
{
    released_ = true;
    if (stage_ == Stage::Idle || stage_ == Stage::Release)
        return;
    if (settings_.mode == Mode::OneShot) // it plays through first
        return;
    enter(Stage::Release);
}

void CurvedEnvelope::enter(Stage stage) noexcept
{
    stage_ = stage;
    from_ = level_;
    u_ = 0.0;
    double ms = 0.0, curve = 0.0;
    switch (stage)
    {
    case Stage::Attack:
        to_ = 1.0, ms = settings_.attackMs, curve = settings_.attackCurve;
        break;
    case Stage::Hold:
        to_ = level_, ms = settings_.holdMs;
        break;
    case Stage::Decay:
        to_ = settings_.sustain, ms = settings_.decayMs, curve = settings_.decayCurve;
        break;
    case Stage::Release:
        to_ = 0.0, ms = settings_.releaseMs, curve = settings_.releaseCurve;
        break;
    case Stage::Sustain:
    case Stage::Idle:
        du_ = 0.0;
        return;
    }
    const double samples = ms * 0.001 * sampleRate_;
    du_ = samples >= 1.0 ? 1.0 / samples : 1.0;
    a_ = 8.0 * std::clamp(curve, -1.0, 1.0);
    e_ = 1.0;
    step_ = std::exp(-a_ * du_);
    norm_ = std::abs(a_) < 1e-6 ? 0.0 : 1.0 / (1.0 - std::exp(-a_));
}

float CurvedEnvelope::next() noexcept
{
    switch (stage_)
    {
    case Stage::Idle:
        level_ = 0.0;
        return 0.0f;
    case Stage::Sustain:
        level_ = settings_.sustain;
        if (settings_.mode == Mode::Loop)
            enter(Stage::Attack);
        return static_cast<float>(level_);
    default:
        break;
    }
    u_ += du_;
    e_ *= step_;
    const double bent = norm_ == 0.0 ? std::min(u_, 1.0) : (1.0 - e_) * norm_;
    level_ = from_ + (to_ - from_) * std::clamp(bent, 0.0, 1.0);
    if (u_ >= 1.0 - 1e-9)
    {
        level_ = to_;
        switch (stage_)
        {
        case Stage::Attack:
            enter(settings_.holdMs > 0.0 ? Stage::Hold : Stage::Decay);
            break;
        case Stage::Hold:
            enter(Stage::Decay);
            break;
        case Stage::Decay:
            if (settings_.mode == Mode::OneShot)
                enter(Stage::Release);
            else if (settings_.mode == Mode::Loop && !released_)
                enter(Stage::Attack);
            else
                enter(released_ ? Stage::Release : Stage::Sustain);
            break;
        case Stage::Release:
            stage_ = Stage::Idle;
            level_ = 0.0;
            break;
        default:
            break;
        }
    }
    return static_cast<float>(level_);
}

// ---- LFOs and random -------------------------------------------------------------------------

void ShapeLfo::reset(double phase) noexcept
{
    phase_ = phase - std::floor(phase);
    done_ = false;
}

void ShapeLfo::noteOn(double phase) noexcept
{
    reset(phase);
}

float ShapeLfo::advance(const CurveTable& shape, Mode mode, int samples, double cyclesPerSample,
                        double songPhase) noexcept
{
    if (mode == Mode::Free && songPhase >= 0.0)
        phase_ = songPhase - std::floor(songPhase);
    else if (!done_)
    {
        double next = phase_ + cyclesPerSample * samples;
        if (mode == Mode::Envelope && next >= 1.0)
        {
            next = 1.0;
            done_ = true;
        }
        phase_ = mode == Mode::Envelope ? next : next - std::floor(next);
    }
    return 2.0f * lookup(shape, std::min(phase_, 1.0)) - 1.0f;
}

void RandomSource::reset(std::uint32_t seed) noexcept
{
    state_ = seed == 0 ? 1 : seed;
    phase_ = 0.0;
    from_ = random();
    to_ = random();
    note_ = random();
    x_ = 0.1, y_ = 0.0, z_ = 0.0;
}

double RandomSource::random() noexcept
{
    state_ ^= state_ << 13;
    state_ ^= state_ >> 17;
    state_ ^= state_ << 5;
    return 2.0 * static_cast<double>(state_ >> 8) / static_cast<double>(1u << 24) - 1.0;
}

void RandomSource::noteOn() noexcept
{
    note_ = random();
}

float RandomSource::advance(Mode mode, int samples, double cyclesPerSample) noexcept
{
    switch (mode)
    {
    case Mode::PerNote:
        return static_cast<float>(note_);
    case Mode::Chaos:
    {
        // The Lorenz attractor, stepped in proportion to the rate; x scaled to about -1 ... +1.
        const double dt = std::min(0.02, cyclesPerSample * samples * 2.0);
        const int steps = 4;
        for (int i = 0; i < steps; ++i)
        {
            const double h = dt / steps;
            const double dx = 10.0 * (y_ - x_), dy = x_ * (28.0 - z_) - y_, dz = x_ * y_ - (8.0 / 3.0) * z_;
            x_ += h * dx, y_ += h * dy, z_ += h * dz;
        }
        return static_cast<float>(std::clamp(x_ / 20.0, -1.0, 1.0));
    }
    case Mode::Steps:
    case Mode::Smooth:
    {
        phase_ += cyclesPerSample * samples;
        while (phase_ >= 1.0)
        {
            phase_ -= 1.0;
            from_ = to_;
            to_ = random();
        }
        if (mode == Mode::Steps)
            return static_cast<float>(from_);
        const double t = 0.5 - 0.5 * std::cos(pi * phase_);
        return static_cast<float>(from_ + (to_ - from_) * t);
    }
    }
    return 0.0f;
}
} // namespace fd::dsp
