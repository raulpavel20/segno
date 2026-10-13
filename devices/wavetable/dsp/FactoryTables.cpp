#include "FactoryTables.h"

#include "fd/dsp/Math.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace fd::devices::wavetable
{
namespace
{
using dsp::pi;
using dsp::twoPi;
constexpr int kTop = dsp::Wavetable::maxHarmonic - 1; // the highest harmonic a table holds
constexpr int kTimeLength = 16384;                    // time-domain recipes: 8 times a frame

/// One frame's harmonics (bins 1 ... 1024), built up by a spectral recipe.
struct Bins
{
    std::complex<float>* c;
    void sine(int h, double a) const // a sin(h x)
    {
        if (h >= 1 && h <= kTop)
            c[h] += std::complex<float>(0.0f, static_cast<float>(-0.5 * a));
    }
    void cosine(int h, double a) const // a cos(h x)
    {
        if (h >= 1 && h <= kTop)
            c[h] += static_cast<float>(0.5 * a);
    }
};

// Classic waves as sine series.
double sawAt(int h)
{
    return 1.0 / h;
}
double squareAt(int h)
{
    return h % 2 == 1 ? 1.0 / h : 0.0;
}
double triangleAt(int h)
{
    return h % 2 == 1 ? ((h / 2) % 2 == 0 ? 1.0 : -1.0) / (static_cast<double>(h) * h) : 0.0;
}
double sineAt(int h)
{
    return h == 1 ? 1.0 : 0.0;
}
double shapeAt(int shape, int h) // 0 saw, 1 square, 2 triangle, 3 sine
{
    switch (shape)
    {
    case 0:
        return sawAt(h);
    case 1:
        return squareAt(h);
    case 2:
        return triangleAt(h);
    default:
        return sineAt(h);
    }
}
double t01(int f, int frames)
{
    return frames <= 1 ? 0.0 : static_cast<double>(f) / (frames - 1);
}
double frac(double x)
{
    return x - std::floor(x);
}
double saw(double p)
{
    return 1.0 - 2.0 * frac(p);
}
double fold(double v)
{
    return 1.0 - 4.0 * std::abs(frac((v + 1.0) * 0.25) - 0.5);
}

/// A pulse of width w as harmonics (its DC left out).
void pulse(const Bins& b, double w, double gain = 1.0)
{
    for (int h = 1; h <= kTop; ++h)
    {
        const double a = gain * 2.0 * std::sin(pi * h * w) / (pi * h);
        const auto rot = std::polar(1.0, -pi * h * w); // centred on w/2
        b.c[h] += std::complex<float>(static_cast<float>(0.5 * a * rot.real()),
                                      static_cast<float>(0.5 * a * rot.imag()));
    }
}

/// Formant gain at `hz`: three bumps on a log scale (A, E, I, O, U, then back), `width` octaves.
constexpr double kVowels[5][3] = {{800.0, 1150.0, 2900.0},
                                  {350.0, 2000.0, 2800.0},
                                  {270.0, 2140.0, 2950.0},
                                  {450.0, 800.0, 2830.0},
                                  {325.0, 700.0, 2700.0}};
double vowelGain(double hz, double vowel, double width)
{
    const double at = std::clamp(vowel, 0.0, 4.0);
    const int i = std::min(3, static_cast<int>(at));
    const double t = at - i;
    constexpr double levels[3] = {1.0, 0.6, 0.3};
    double g = 0.02;
    for (int k = 0; k < 3; ++k)
    {
        const double centre = std::exp(std::log(kVowels[i][k]) * (1.0 - t) + std::log(kVowels[i + 1][k]) * t);
        const double d = std::log2(hz / centre) / width;
        g += levels[k] * std::exp(-0.5 * d * d);
    }
    return g;
}

std::uint32_t mix(std::uint32_t x)
{
    x ^= x >> 16;
    x *= 0x7feb352dU;
    x ^= x >> 15;
    x *= 0x846ca68bU;
    x ^= x >> 16;
    return x;
}
double random01(std::uint32_t seed, int h)
{
    return (mix(seed * 7919U + static_cast<std::uint32_t>(h) * 104729U) >> 8) / 16777216.0;
}

/// A seeded random spectrum, falling `tilt` per octave, with random phases.
void randomSpectrum(const Bins& b, std::uint32_t seed, double gain, double tilt = 0.8, int from = 1,
                    int to = 256)
{
    for (int h = from; h <= std::min(to, kTop); ++h)
    {
        const double a = gain * random01(seed, h) / std::pow(h, tilt);
        b.c[h] += std::polar(static_cast<float>(0.5 * a), static_cast<float>(twoPi * random01(seed + 1, h)));
    }
}

using Spectral = void (*)(int frame, int frames, const Bins& b);
using Wave = double (*)(int frame, int frames, double phase);

struct Recipe
{
    FactoryTableInfo info;
    int frames;
    Spectral spectral = nullptr;
    Wave wave = nullptr;
};

// clang-format off
const Recipe kRecipes[] = {
    // ---- Basic ----
    {{"basic/sine", "Sine", "Basic"}, 1, [](int, int, const Bins& b) { b.sine(1, 1.0); }},
    {{"basic/triangle", "Triangle", "Basic"}, 1, [](int, int, const Bins& b) { for (int h = 1; h <= kTop; ++h) b.sine(h, triangleAt(h)); }},
    {{"basic/saw", "Saw", "Basic"}, 1, [](int, int, const Bins& b) { for (int h = 1; h <= kTop; ++h) b.sine(h, sawAt(h)); }},
    {{"basic/square", "Square", "Basic"}, 1, [](int, int, const Bins& b) { for (int h = 1; h <= kTop; ++h) b.sine(h, squareAt(h)); }},
    {{"basic/pulse-25", "Pulse 25", "Basic"}, 1, [](int, int, const Bins& b) { pulse(b, 0.25); }},
    {{"basic/pulse-10", "Pulse 10", "Basic"}, 1, [](int, int, const Bins& b) { pulse(b, 0.10); }},
    {{"basic/shapes", "Basic Shapes", "Basic"}, 64, [](int f, int n, const Bins& b) {
        // Saw, then square, triangle and sine: the Subtractive Synth's morph, as a table.
        const double at = t01(f, n) * 3.0;
        const int i = std::min(2, static_cast<int>(at));
        const double t = at - i;
        for (int h = 1; h <= kTop; ++h) b.sine(h, (1.0 - t) * shapeAt(i, h) + t * shapeAt(i + 1, h));
    }},
    {{"basic/saw-builder", "Saw Builder", "Basic"}, 64, [](int f, int n, const Bins& b) {
        const double top = std::exp2(t01(f, n) * 10.0); // 1 ... 1024 harmonics, faded in
        for (int h = 1; h <= kTop; ++h) b.sine(h, sawAt(h) * std::clamp(top - h + 1.0, 0.0, 1.0));
    }},
    // ---- Analog ----
    {{"analog/pwm", "PWM", "Analog"}, 64, [](int f, int n, const Bins& b) { pulse(b, 0.5 - 0.47 * t01(f, n)); }},
    {{"analog/sync", "Hard Sync", "Analog"}, 64, nullptr, [](int f, int n, double p) { return saw(p * (1.0 + 7.0 * t01(f, n))); }},
    {{"analog/sync-square", "Sync Square", "Analog"}, 64, nullptr, [](int f, int n, double p) {
        return frac(p * (1.0 + 7.0 * t01(f, n))) < 0.5 ? 1.0 : -1.0; }},
    {{"analog/filter-sweep", "Filter Sweep", "Analog"}, 64, [](int f, int n, const Bins& b) {
        // A saw through a resonant two-pole low pass, its cutoff rising from the 2nd harmonic.
        const double cutoff = 1.5 * std::exp2(t01(f, n) * 7.0), q = 4.0;
        for (int h = 1; h <= kTop; ++h) {
            const double x = h / cutoff;
            b.sine(h, sawAt(h) / std::sqrt((1.0 - x * x) * (1.0 - x * x) + (x / q) * (x / q)));
        }
    }},
    {{"analog/rounded", "Rounded Saw", "Analog"}, 32, [](int f, int n, const Bins& b) {
        const double cutoff = 2.0 * std::exp2(t01(f, n) * 7.2);
        for (int h = 1; h <= kTop; ++h) b.sine(h, sawAt(h) / (1.0 + std::pow(h / cutoff, 4.0)));
    }},
    {{"analog/saw-square", "Saw to Square", "Analog"}, 32, [](int f, int n, const Bins& b) {
        const double t = t01(f, n);
        for (int h = 1; h <= kTop; ++h) b.sine(h, (1.0 - t) * sawAt(h) + t * squareAt(h));
    }},
    {{"analog/fold", "Triangle Fold", "Analog"}, 32, nullptr, [](int f, int n, double p) {
        const double tri = 1.0 - 4.0 * std::abs(frac(p + 0.25) - 0.5);
        return fold(tri * (1.0 + 6.0 * t01(f, n))); }},
    {{"analog/double-saw", "Double Saw", "Analog"}, 32, nullptr, [](int f, int n, double p) {
        return 0.5 * (saw(p) + saw(p + 0.5 * t01(f, n))); }},
    // ---- Digital ----
    {{"digital/stepped-sine", "Stepped Sine", "Digital"}, 32, nullptr, [](int f, int n, double p) {
        const double levels = std::exp2(6.0 - 5.0 * t01(f, n));
        return std::round(std::sin(twoPi * p) * levels) / levels; }},
    {{"digital/crushed-saw", "Crushed Saw", "Digital"}, 32, nullptr, [](int f, int n, double p) {
        const double steps = std::exp2(9.0 - 7.0 * t01(f, n));
        return saw(std::floor(p * steps) / steps); }},
    {{"digital/fm", "FM 1:1", "Digital"}, 64, nullptr, [](int f, int n, double p) {
        return std::sin(twoPi * p + 8.0 * t01(f, n) * std::sin(twoPi * p)); }},
    {{"digital/fm-2", "FM 1:2", "Digital"}, 64, nullptr, [](int f, int n, double p) {
        return std::sin(twoPi * p + 6.0 * t01(f, n) * std::sin(2.0 * twoPi * p)); }},
    {{"digital/fm-3", "FM 3:1", "Digital"}, 64, nullptr, [](int f, int n, double p) {
        return std::sin(3.0 * twoPi * p + 5.0 * t01(f, n) * std::sin(twoPi * p)); }},
    {{"digital/phase-distortion", "Phase Distortion", "Digital"}, 64, nullptr, [](int f, int n, double p) {
        const double k = 0.5 - 0.48 * t01(f, n);
        const double bent = p < k ? 0.5 * p / k : 0.5 + 0.5 * (p - k) / (1.0 - k);
        return -std::cos(twoPi * bent); }},
    {{"digital/chebyshev", "Chebyshev", "Digital"}, 32, nullptr, [](int f, int n, double p) {
        const double order = 1.0 + 15.0 * t01(f, n);
        const double x = 0.95 * std::sin(twoPi * p);
        return std::cos(order * std::acos(std::clamp(x, -1.0, 1.0))); }},
    {{"digital/ring", "Ring", "Digital"}, 32, nullptr, [](int f, int, double p) {
        return std::sin(twoPi * p) * std::sin(static_cast<double>(f + 1) * twoPi * p); }},
    {{"digital/sine-fold", "Sine Fold", "Digital"}, 64, nullptr, [](int f, int n, double p) {
        return fold(std::sin(twoPi * p) * (1.0 + 7.0 * t01(f, n))); }},
    {{"digital/stairs", "Stairs", "Digital"}, 16, nullptr, [](int f, int, double p) {
        const double steps = 2.0 + f;
        return 2.0 * std::floor(p * steps) / (steps - 1.0) - 1.0; }},
    // ---- Harmonic ----
    {{"harmonic/odd", "Odd Harmonics", "Harmonic"}, 32, [](int f, int, const Bins& b) {
        for (int h = 1; h <= 2 * f + 1; h += 2) b.sine(h, 1.0 / h); }},
    {{"harmonic/even", "Even Harmonics", "Harmonic"}, 32, [](int f, int, const Bins& b) {
        b.sine(1, 1.0);
        for (int h = 2; h <= 2 * f; h += 2) b.sine(h, 1.0 / h); }},
    {{"harmonic/organ", "Organ", "Harmonic"}, 8, [](int f, int, const Bins& b) {
        // Drawbar registrations: 16', 8', 5 1/3', 4', 2 2/3', 2', 1 3/5', 1 1/3', 1'.
        constexpr int feet[9] = {1, 2, 3, 4, 6, 8, 10, 12, 16};
        constexpr int bars[8][9] = {{8, 8, 8, 0, 0, 0, 0, 0, 0}, {8, 8, 8, 8, 8, 8, 8, 8, 8}, {0, 0, 8, 8, 0, 0, 0, 0, 0},
                                    {8, 3, 8, 0, 0, 0, 0, 0, 0}, {6, 8, 8, 6, 0, 0, 0, 0, 0}, {8, 8, 0, 0, 0, 0, 0, 0, 8},
                                    {8, 6, 8, 8, 6, 8, 0, 6, 8}, {0, 8, 0, 8, 0, 8, 0, 0, 8}};
        for (int k = 0; k < 9; ++k) // each drawbar step about 3 dB
            if (bars[f][k] > 0) b.sine(feet[k], std::pow(10.0, -3.0 * (8 - bars[f][k]) / 20.0));
    }},
    {{"harmonic/series", "Harmonic Series", "Harmonic"}, 64, [](int f, int, const Bins& b) {
        for (int h = 1; h <= f + 1; ++h) b.sine(h, 1.0 / std::sqrt(h)); }},
    {{"harmonic/bell", "Bell", "Harmonic"}, 16, [](int f, int n, const Bins& b) {
        constexpr int partials[10] = {2, 4, 5, 6, 8, 10, 12, 15, 19, 24};
        const double bright = t01(f, n);
        for (int k = 0; k < 10; ++k) b.sine(partials[k], std::pow(0.7 - 0.35 * (1.0 - bright), k) * (k % 2 ? 0.8 : 1.0));
    }},
    {{"harmonic/brass", "Brass", "Harmonic"}, 32, [](int f, int n, const Bins& b) {
        const double centre = 3.0 + 9.0 * t01(f, n);
        for (int h = 1; h <= kTop; ++h) {
            const double d = std::log2(h / centre);
            b.sine(h, sawAt(h) * (0.3 + 1.5 * std::exp(-2.0 * d * d)) * (h < 3 * centre ? 1.0 : std::pow(3.0 * centre / h, 2.0)));
        }
    }},
    {{"harmonic/strings", "Strings", "Harmonic"}, 16, [](int f, int n, const Bins& b) {
        constexpr double body[3] = {3.0, 7.0, 13.0};
        const double shift = 1.0 + 0.5 * t01(f, n);
        for (int h = 1; h <= kTop; ++h) {
            double g = 0.3;
            for (const double r : body) { const double d = std::log2(h / (r * shift)) * 4.0; g += std::exp(-0.5 * d * d); }
            b.sine(h, sawAt(h) * g);
        }
    }},
    {{"harmonic/flute", "Flute", "Harmonic"}, 16, [](int f, int n, const Bins& b) {
        const double breath = t01(f, n);
        b.sine(1, 1.0); b.sine(2, 0.25 + 0.2 * breath); b.sine(3, 0.12 + 0.1 * breath); b.sine(4, 0.05 * breath);
        randomSpectrum(b, 11, 0.06 * breath, 0.3, 5, 48);
    }},
    {{"harmonic/clarinet", "Clarinet", "Harmonic"}, 16, [](int f, int n, const Bins& b) {
        const double even = 0.25 * t01(f, n);
        for (int h = 1; h <= 48; ++h) b.sine(h, (h % 2 ? 1.0 : even) / std::pow(h, 0.9));
    }},
    {{"harmonic/primes", "Primes", "Harmonic"}, 32, [](int f, int, const Bins& b) {
        int count = 0;
        for (int h = 1; h <= kTop && count <= 2 + f; ++h) {
            bool prime = h > 1;
            for (int d = 2; d * d <= h && prime; ++d) prime = h % d != 0;
            if (h == 1 || prime) { b.sine(h, 1.0 / std::sqrt(h)); ++count; }
        }
    }},
    {{"harmonic/gaps", "Gapped Saw", "Harmonic"}, 32, [](int f, int, const Bins& b) {
        const int skip = 2 + f;
        for (int h = 1; h <= kTop; ++h) if (h % skip != 0) b.sine(h, sawAt(h));
    }},
    // ---- Formant and vocal ----
    {{"formant/vowels", "Vowels", "Formant"}, 64, [](int f, int n, const Bins& b) {
        for (int h = 1; h <= kTop; ++h) b.sine(h, sawAt(h) * vowelGain(130.0 * h, 4.0 * t01(f, n), 0.18) * std::sqrt(h));
    }},
    {{"formant/vowels-high", "Vowels High", "Formant"}, 64, [](int f, int n, const Bins& b) {
        for (int h = 1; h <= kTop; ++h) b.sine(h, sawAt(h) * vowelGain(262.0 * h, 4.0 * t01(f, n), 0.18) * std::sqrt(h));
    }},
    {{"formant/sweep", "Formant Sweep", "Formant"}, 64, [](int f, int n, const Bins& b) {
        const double centre = 250.0 * std::exp2(t01(f, n) * 4.0);
        for (int h = 1; h <= kTop; ++h) { const double d = std::log2(130.0 * h / centre) / 0.2; b.sine(h, sawAt(h) * (0.05 + std::exp(-0.5 * d * d)) * std::sqrt(h)); }
    }},
    {{"formant/choir", "Choir", "Formant"}, 32, [](int f, int n, const Bins& b) {
        for (int h = 1; h <= kTop; ++h) b.sine(h, sawAt(h) * vowelGain(196.0 * h, 4.0 * t01(f, n), 0.3) * std::sqrt(h));
    }},
    {{"formant/talk", "Talk", "Formant"}, 64, [](int f, int n, const Bins& b) {
        constexpr double path[6] = {0.0, 3.0, 1.0, 4.0, 2.0, 0.0}; // A, O, E, U, I, A
        const double at = t01(f, n) * 5.0;
        const int i = std::min(4, static_cast<int>(at));
        const double vowel = path[i] + (path[i + 1] - path[i]) * (at - i);
        for (int h = 1; h <= kTop; ++h) b.sine(h, sawAt(h) * vowelGain(147.0 * h, vowel, 0.2) * std::sqrt(h));
    }},
    {{"formant/nasal", "Nasal", "Formant"}, 32, [](int f, int n, const Bins& b) {
        const double notch = 900.0 * std::exp2(t01(f, n));
        for (int h = 1; h <= kTop; ++h) {
            const double d = std::log2(130.0 * h / notch) / 0.12;
            b.sine(h, sawAt(h) * vowelGain(130.0 * h, 1.5, 0.2) * std::sqrt(h) * (1.0 - 0.95 * std::exp(-0.5 * d * d)));
        }
    }},
    // ---- FM ----
    {{"fm/bell", "FM Bell", "FM"}, 64, nullptr, [](int f, int n, double p) {
        return std::sin(2.0 * twoPi * p + 4.0 * t01(f, n) * std::sin(7.0 * twoPi * p)); }},
    {{"fm/bass", "FM Bass", "FM"}, 32, nullptr, [](int f, int n, double p) {
        const double i = 3.0 * t01(f, n);
        return 0.6 * std::sin(twoPi * p + i * std::sin(twoPi * p)) + 0.4 * std::sin(twoPi * p + 0.7 * i * std::sin(2.0 * twoPi * p)); }},
    {{"fm/feedback", "Feedback", "FM"}, 64, nullptr, [](int f, int n, double p) {
        // y = sin(x + b y), found by iterating.
        const double beta = 1.5 * t01(f, n);
        double y = 0.0;
        for (int k = 0; k < 24; ++k) y = std::sin(twoPi * p + beta * y);
        return y; }},
    {{"fm/metal", "Metal", "FM"}, 32, nullptr, [](int f, int n, double p) {
        return std::sin(3.0 * twoPi * p + 3.0 * t01(f, n) * std::sin(11.0 * twoPi * p)); }},
    {{"fm/keys", "FM Keys", "FM"}, 32, nullptr, [](int f, int n, double p) {
        const double t = t01(f, n);
        return 0.8 * std::sin(twoPi * p + 0.6 * std::sin(twoPi * p)) + 0.25 * t * std::sin(twoPi * p + 1.2 * std::sin(14.0 * twoPi * p)); }},
    {{"fm/growl", "Growl", "FM"}, 64, nullptr, [](int f, int n, double p) {
        const double t = t01(f, n);
        return std::sin(twoPi * p + 4.0 * t * std::sin(2.0 * twoPi * p + 2.0 * t * std::sin(twoPi * p))); }},
    // ---- Texture ----
    {{"texture/random-a", "Random A", "Texture"}, 32, [](int f, int n, const Bins& b) { const double t = t01(f, n); randomSpectrum(b, 101, 1.0 - t); randomSpectrum(b, 102, t); }},
    {{"texture/random-b", "Random B", "Texture"}, 32, [](int f, int n, const Bins& b) { const double t = t01(f, n); randomSpectrum(b, 201, 1.0 - t, 0.5); randomSpectrum(b, 202, t, 0.5); }},
    {{"texture/random-c", "Random C", "Texture"}, 32, [](int f, int n, const Bins& b) { const double t = t01(f, n); randomSpectrum(b, 301, 1.0 - t, 1.2); randomSpectrum(b, 302, t, 1.2); }},
    {{"texture/random-d", "Random D", "Texture"}, 32, [](int f, int n, const Bins& b) { const double t = t01(f, n); randomSpectrum(b, 401, 1.0 - t, 0.3, 1, 96); randomSpectrum(b, 402, t, 0.3, 1, 96); }},
    {{"texture/dust", "Dust", "Texture"}, 32, [](int f, int, const Bins& b) {
        for (int h = 1; h <= 400; ++h) if (random01(500 + static_cast<std::uint32_t>(f / 4), h) > 0.93) b.sine(h, 1.0 / std::sqrt(h));
        b.sine(1, 0.6); }},
    {{"texture/air", "Air", "Texture"}, 32, [](int f, int n, const Bins& b) {
        b.sine(1, 0.5);
        randomSpectrum(b, 600, 1.5, 0.2, 8 + static_cast<int>(16 * t01(f, n)), 96); }},
    {{"texture/grit", "Grit", "Texture"}, 32, [](int f, int n, const Bins& b) {
        const double jitter = t01(f, n);
        for (int h = 1; h <= kTop; ++h) b.sine(h, sawAt(h) * (1.0 - jitter + 2.0 * jitter * random01(700, h)));
    }},
    // ---- Spectral ----
    {{"spectral/comb", "Comb", "Spectral"}, 64, [](int f, int n, const Bins& b) {
        const double spacing = 2.0 + 14.0 * t01(f, n);
        for (int h = 1; h <= kTop; ++h) b.sine(h, sawAt(h) * (0.5 + 0.5 * std::cos(twoPi * h / spacing)));
    }},
    {{"spectral/tilt", "Tilt", "Spectral"}, 64, [](int f, int n, const Bins& b) {
        const double slope = -12.0 + 18.0 * t01(f, n); // dB per octave
        for (int h = 1; h <= 512; ++h) b.sine(h, std::pow(10.0, slope * std::log2(h) / 20.0));
    }},
    {{"spectral/stretch", "Stretch", "Spectral"}, 64, [](int f, int n, const Bins& b) {
        const double s = 1.0 + 0.25 * t01(f, n);
        for (int k = 1; k <= 200; ++k) b.sine(static_cast<int>(std::lround(std::pow(k, s))), 1.0 / k);
    }},
    {{"spectral/peak", "Peak", "Spectral"}, 64, [](int f, int n, const Bins& b) {
        const double centre = 2.0 * std::exp2(t01(f, n) * 7.0);
        for (int h = 1; h <= kTop; ++h) { const double d = std::log2(h / centre) / 0.15; b.sine(h, sawAt(h) * (0.15 + 3.0 * std::exp(-0.5 * d * d))); }
    }},
    {{"spectral/notch", "Notch", "Spectral"}, 64, [](int f, int n, const Bins& b) {
        const double centre = 2.0 * std::exp2(t01(f, n) * 7.0);
        for (int h = 1; h <= kTop; ++h) { const double d = std::log2(h / centre) / 0.2; b.sine(h, sawAt(h) * (1.0 - std::exp(-0.5 * d * d))); }
    }},
    {{"spectral/odd-even", "Odd to Even", "Spectral"}, 64, [](int f, int n, const Bins& b) {
        const double t = t01(f, n);
        b.sine(1, 1.0);
        for (int h = 2; h <= kTop; ++h) b.sine(h, sawAt(h) * (h % 2 ? 1.0 - t : t));
    }},
    {{"spectral/octaves", "Octaves", "Spectral"}, 32, [](int f, int n, const Bins& b) {
        const double centre = 1.0 + 6.0 * t01(f, n);
        for (int k = 0; k <= 9; ++k) { const double d = (k - centre) / 1.5; b.sine(1 << k, std::exp(-0.5 * d * d)); }
    }},
    {{"spectral/disperse", "Disperse", "Spectral"}, 32, [](int f, int n, const Bins& b) {
        const double spread = 6.0 * t01(f, n);
        for (int h = 1; h <= kTop; ++h) b.c[h] += std::polar(static_cast<float>(0.5 * sawAt(h)), static_cast<float>(-0.5 * pi + spread * h * h / 1024.0));
    }},
};
// clang-format on

std::vector<FactoryTableInfo> makeInfos()
{
    std::vector<FactoryTableInfo> out;
    for (const auto& r : kRecipes)
        out.push_back(r.info);
    return out;
}

dsp::Wavetable build(const Recipe& r)
{
    const int frames = r.frames;
    if (r.wave != nullptr)
    {
        std::vector<float> x(static_cast<size_t>(frames) * kTimeLength);
        for (int f = 0; f < frames; ++f)
            for (int i = 0; i < kTimeLength; ++i)
                x[static_cast<size_t>(f) * kTimeLength + static_cast<size_t>(i)] =
                    static_cast<float>(r.wave(f, frames, static_cast<double>(i) / kTimeLength));
        return dsp::Wavetable::fromCycles(x.data(), kTimeLength, frames);
    }
    std::vector<std::complex<float>> bins(static_cast<size_t>(frames) * (dsp::Wavetable::maxHarmonic + 1));
    for (int f = 0; f < frames; ++f)
        r.spectral(f, frames, Bins{bins.data() + static_cast<size_t>(f) * (dsp::Wavetable::maxHarmonic + 1)});
    return dsp::Wavetable::fromSpectra(std::move(bins), frames);
}
} // namespace

std::span<const FactoryTableInfo> factoryTables()
{
    static const std::vector<FactoryTableInfo> infos = makeInfos();
    return infos;
}

std::shared_ptr<const dsp::Wavetable> factoryTable(std::string_view id)
{
    static std::mutex mutex;
    static std::map<std::string, std::shared_ptr<const dsp::Wavetable>, std::less<>> cache;
    const std::scoped_lock lock(mutex);
    if (const auto it = cache.find(id); it != cache.end())
        return it->second;
    for (const auto& r : kRecipes)
        if (r.info.id == id)
        {
            auto table = std::make_shared<dsp::Wavetable>(build(r));
            table->name = std::string(r.info.name);
            cache.emplace(std::string(id), table);
            return table;
        }
    return nullptr;
}
} // namespace fd::devices::wavetable
