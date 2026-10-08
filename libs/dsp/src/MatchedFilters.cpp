#include "fd/dsp/MatchedFilters.h"

#include "fd/dsp/Math.h"

#include <algorithm>
#include <cmath>

namespace fd::dsp
{
namespace
{
using cd = std::complex<double>;

/// |b0 + b1 z^-1 + b2 z^-2|^2 = B0 phi0 + B1 phi1 + B2 phi2, with
/// B0 = (b0+b1+b2)^2, B1 = (b0-b1+b2)^2, B2 = -4 b0 b2,
/// phi0 = cos^2(w/2), phi1 = sin^2(w/2), phi2 = 4 phi0 phi1.
struct Phi
{
    double p0, p1, p2;
};

Phi phiAt(double w) noexcept
{
    const double s = std::sin(0.5 * w);
    const double p1 = s * s;
    const double p0 = 1.0 - p1;
    return {p0, p1, 4.0 * p0 * p1};
}
} // namespace

double AnalogBiquad::magnitudeSquared(double x) const noexcept
{
    const cd s(0.0, x);
    const cd num = n0 * s * s + n1 * s + n2;
    const cd den = d0 * s * s + d1 * s + d2;
    return std::norm(num) / std::max(std::norm(den), 1e-300);
}

static AnalogBiquad analogPrototypeRaw(MatchedType type, double gainDb, double q) noexcept;

AnalogBiquad analogPrototype(MatchedType type, double gainDb, double q) noexcept
{
    q = std::max(q, 1e-3);
    AnalogBiquad p = analogPrototypeRaw(type, gainDb, q);
    p.q = q;
    return p;
}

static AnalogBiquad analogPrototypeRaw(MatchedType type, double gainDb, double q) noexcept
{
    const double A = std::pow(10.0, gainDb / 40.0); // sqrt of the linear gain
    const double sqrtA = std::sqrt(A);
    switch (type)
    {
    case MatchedType::Bell:
        return {1.0, A / q, 1.0, 1.0, 1.0 / (A * q), 1.0};
    case MatchedType::LowShelf: // DC gain A^2, HF gain 1
        return {A, sqrtA / q * A, A * A, A, sqrtA / q, 1.0};
    case MatchedType::HighShelf: // DC gain 1, HF gain A^2
        return {A * A, sqrtA / q * A, A, 1.0, sqrtA / q, A};
    case MatchedType::Tilt: // high shelf scaled by 1/A: DC -g/2, HF +g/2, 0 dB at fc
        return {A, sqrtA / q, 1.0, 1.0, sqrtA / q, A};
    case MatchedType::Lowpass:
        return {0.0, 0.0, 1.0, 1.0, 1.0 / q, 1.0};
    case MatchedType::Highpass:
        return {1.0, 0.0, 0.0, 1.0, 1.0 / q, 1.0};
    case MatchedType::Bandpass:
        return {0.0, 1.0 / q, 0.0, 1.0, 1.0 / q, 1.0};
    case MatchedType::Notch:
        return {1.0, 0.0, 1.0, 1.0, 1.0 / q, 1.0};
    }
    return {};
}

BiquadCoeffs designMatched3(const AnalogBiquad& proto, double freqHz, double sampleRate) noexcept
{
    const double fc = std::clamp(freqHz, 1.0, 0.499 * sampleRate);
    const double wc = twoPi * fc / sampleRate; // radians per sample

    // 1. Exact pole mapping: roots of d0 s^2 + d1 s + d2 (normalised s), scaled by wc.
    double a1, a2;
    {
        const double disc = proto.d1 * proto.d1 - 4.0 * proto.d0 * proto.d2;
        const double re = -proto.d1 / (2.0 * proto.d0) * wc;
        if (disc < 0.0)
        {
            const double im = std::sqrt(-disc) / (2.0 * proto.d0) * wc;
            a1 = -2.0 * std::exp(re) * std::cos(im);
        }
        else
        {
            const double r = std::sqrt(disc) / (2.0 * proto.d0) * wc;
            a1 = -2.0 * std::exp(re) * std::cosh(r);
        }
        a2 = std::exp(2.0 * re);
    }

    // Denominator magnitude in the phi basis.
    const double A0 = (1.0 + a1 + a2) * (1.0 + a1 + a2);
    const double A1 = (1.0 - a1 + a2) * (1.0 - a1 + a2);
    const double A2 = -4.0 * a2;
    auto denMag2 = [&](const Phi& p) { return A0 * p.p0 + A1 * p.p1 + A2 * p.p2; };

    // 2. Match |H|^2 at DC, at fc, and at Nyquist (physical frequencies: x = f / fc).
    const double xNyq = 0.5 * sampleRate / fc;
    const Phi pDc = phiAt(0.0), pC = phiAt(wc), pNy = phiAt(pi);
    const double tDc = proto.magnitudeSquared(0.0) * denMag2(pDc);
    const double tC = proto.magnitudeSquared(1.0) * denMag2(pC);
    const double tNy = proto.magnitudeSquared(xNyq) * denMag2(pNy);
    // At DC phi = (1,0,0) and at Nyquist (0,1,0), so B0 and B1 follow directly; B2 from fc.
    const double B0 = std::max(tDc, 0.0);
    const double B1 = std::max(tNy, 0.0);
    const double B2 = pC.p2 > 1e-12 ? (tC - B0 * pC.p0 - B1 * pC.p1) / pC.p2 : 0.0;

    // 3. Minimum-phase numerator from (B0, B1, B2).
    const double s0 = std::sqrt(B0), s1 = std::sqrt(B1);
    const double W = 0.5 * (s0 + s1);
    const double b0 = 0.5 * (W + std::sqrt(std::max(W * W + B2, 0.0)));
    const double b1 = 0.5 * (s0 - s1);
    const double b2 = b0 > 1e-300 ? -B2 / (4.0 * b0) : 0.0;
    return {b0, b1, b2, a1, a2};
}

namespace
{
/// Minimum-phase factorisation: coefficients c0..c2 with |c0 + c1 z^-1 + c2 z^-2|^2 having the
/// given (C0, C1, C2) in the phi basis. False if no real factorisation exists.
bool factorPhi(double C0, double C1, double C2, double out[3]) noexcept
{
    if (C0 < -1e-12 || C1 < -1e-12)
        return false;
    const double s0 = std::sqrt(std::max(C0, 0.0)), s1 = std::sqrt(std::max(C1, 0.0));
    const double W = 0.5 * (s0 + s1);
    const double disc = W * W + C2;
    if (disc < -1e-9 * std::max(1.0, W * W))
        return false;
    const double c0 = 0.5 * (W + std::sqrt(std::max(disc, 0.0)));
    if (!(c0 > 0.0))
        return false;
    out[0] = c0;
    out[1] = 0.5 * (s0 - s1);
    out[2] = -C2 / (4.0 * c0);
    return true;
}

double det5(double m[5][5]) noexcept
{
    double d = 1.0;
    for (int c = 0; c < 5; ++c)
    {
        int p = c;
        for (int r = c + 1; r < 5; ++r)
            if (std::abs(m[r][c]) > std::abs(m[p][c]))
                p = r;
        if (std::abs(m[p][c]) < 1e-300)
            return 0.0;
        if (p != c)
        {
            for (int k = 0; k < 5; ++k)
                std::swap(m[p][k], m[c][k]);
            d = -d;
        }
        d *= m[c][c];
        for (int r = c + 1; r < 5; ++r)
        {
            const double f = m[r][c] / m[c][c];
            for (int k = c; k < 5; ++k)
                m[r][k] -= f * m[c][k];
        }
    }
    return d;
}

bool isStable(const BiquadCoeffs& c) noexcept
{
    return std::isfinite(c.b0) && std::isfinite(c.b1) && std::isfinite(c.b2) && std::abs(c.a2) < 1.0 &&
           std::abs(c.a1) < 1.0 + c.a2;
}
} // namespace

bool designMatched5(const AnalogBiquad& proto, double freqHz, double sampleRate, BiquadCoeffs& out) noexcept
{
    const double ny = 0.5 * sampleRate;
    const double fc = std::clamp(freqHz, 1.0, 0.499 * sampleRate);
    // Match points: DC, the band edges (from Q), the centre, and Nyquist.
    const double bw = 2.0 / std::log(2.0) * std::asinh(1.0 / (2.0 * std::max(proto.q, 0.05)));
    const double fl = std::min(fc * std::pow(2.0, -0.5 * bw), 0.5 * fc);
    const double fh = std::min(fc * std::pow(2.0, 0.5 * bw), std::sqrt(fc * ny));
    const double fs[5] = {0.0, fl, fc, fh, ny};

    // Unknowns v = (A0, A1, A2, B0, B1, B2): B.phi(w) - T(w) A.phi(w) = 0 at each point.
    double rows[5][6];
    for (int k = 0; k < 5; ++k)
    {
        const Phi ph = phiAt(twoPi * fs[k] / sampleRate);
        const double T = proto.magnitudeSquared(fs[k] / fc);
        const double r[6] = {-T * ph.p0, -T * ph.p1, -T * ph.p2, ph.p0, ph.p1, ph.p2};
        std::copy(r, r + 6, rows[k]);
    }
    // Null vector by cofactors.
    double v[6];
    for (int j = 0; j < 6; ++j)
    {
        double m[5][5];
        for (int r = 0; r < 5; ++r)
            for (int c = 0, c2 = 0; c < 6; ++c)
                if (c != j)
                    m[r][c2++] = rows[r][c];
        v[j] = ((j % 2) != 0 ? -1.0 : 1.0) * det5(m);
    }
    if (v[0] + v[1] < 0.0)
        for (double& x : v)
            x = -x;

    double a[3], b[3];
    if (!factorPhi(v[0], v[1], v[2], a) || !factorPhi(v[3], v[4], v[5], b))
        return false;
    out = {b[0] / a[0], b[1] / a[0], b[2] / a[0], a[1] / a[0], a[2] / a[0]};
    return isStable(out);
}

BiquadCoeffs designBilinear(const AnalogBiquad& p, double freqHz, double sampleRate) noexcept
{
    // s_norm = K (1 - z^-1) / (1 + z^-1), K = 1 / tan(wc / 2): prewarped at the centre.
    const double fc = std::clamp(freqHz, 1.0, 0.499 * sampleRate);
    const double K = 1.0 / std::tan(pi * fc / sampleRate);
    const double K2 = K * K;
    const double b0 = p.n0 * K2 + p.n1 * K + p.n2;
    const double b1 = 2.0 * (p.n2 - p.n0 * K2);
    const double b2 = p.n0 * K2 - p.n1 * K + p.n2;
    const double a0 = p.d0 * K2 + p.d1 * K + p.d2;
    const double a1 = 2.0 * (p.d2 - p.d0 * K2);
    const double a2 = p.d0 * K2 - p.d1 * K + p.d2;
    return {b0 / a0, b1 / a0, b2 / a0, a1 / a0, a2 / a0};
}

double matchErrorDb(const AnalogBiquad& p, const BiquadCoeffs& c, double freqHz, double sampleRate,
                    int numPoints) noexcept
{
    const double fMax = std::min(20000.0, 0.95 * 0.5 * sampleRate);
    double worst = 0.0;
    for (int i = 0; i < numPoints; ++i)
    {
        const double f = 20.0 * std::pow(fMax / 20.0, static_cast<double>(i) / (numPoints - 1));
        const double analog = 10.0 * std::log10(std::max(p.magnitudeSquared(f / freqHz), 1e-24));
        const double digital = 20.0 * std::log10(std::max(biquadMagnitude(c, f, sampleRate), 1e-12));
        worst = std::max(worst, std::abs(digital - analog));
    }
    return worst;
}

BiquadCoeffs designMatched(const AnalogBiquad& p, double freqHz, double sampleRate) noexcept
{
    BiquadCoeffs best = designMatched3(p, freqHz, sampleRate);
    double bestError = isStable(best) ? matchErrorDb(p, best, freqHz, sampleRate) : 1e9;
    BiquadCoeffs five;
    if (designMatched5(p, freqHz, sampleRate, five))
        if (const double e = matchErrorDb(p, five, freqHz, sampleRate); e < bestError)
        {
            best = five;
            bestError = e;
        }
    const BiquadCoeffs bilinear = designBilinear(p, freqHz, sampleRate);
    if (isStable(bilinear) && matchErrorDb(p, bilinear, freqHz, sampleRate) < bestError)
        best = bilinear;
    return best;
}

BiquadCoeffs designMatched(MatchedType type, double freqHz, double gainDb, double q, double sampleRate) noexcept
{
    const auto proto = analogPrototype(type, gainDb, q);
    if (type != MatchedType::Notch)
        return designMatched(proto, freqHz, sampleRate);

    // Notch: zeros exactly on the unit circle at the centre, exactly mapped poles, unity at DC.
    const auto c = designMatched3(proto, freqHz, sampleRate);
    const double wc = twoPi * std::clamp(freqHz, 1.0, 0.499 * sampleRate) / sampleRate;
    const double cosw = std::cos(wc);
    const double g = (1.0 + c.a1 + c.a2) / (2.0 - 2.0 * cosw);
    return {g, -2.0 * cosw * g, g, c.a1, c.a2};
}

BiquadCoeffs designMatchedFirstOrder(bool highpass, double freqHz, double sampleRate) noexcept
{
    const double fc = std::clamp(freqHz, 1.0, 0.499 * sampleRate);
    const double a1 = -std::exp(-twoPi * fc / sampleRate);
    // Analog magnitude at Nyquist: LP 1/sqrt(1+x^2), HP x/sqrt(1+x^2).
    const double x = 0.5 * sampleRate / fc;
    const double magNy = highpass ? x / std::sqrt(1.0 + x * x) : 1.0 / std::sqrt(1.0 + x * x);
    if (highpass)
    {
        const double b0 = 0.5 * (1.0 - a1) * magNy; // DC gain 0, Nyquist matched
        return {b0, -b0, 0.0, a1, 0.0};
    }
    const double sum = 1.0 + a1;              // b0 + b1: unity DC gain
    const double diff = (1.0 - a1) * magNy;   // b0 - b1: Nyquist matched
    return {0.5 * (sum + diff), 0.5 * (sum - diff), 0.0, a1, 0.0};
}

int butterworthQs(int order, double* qs, int maxSections) noexcept
{
    const int sections = std::min(order / 2, maxSections);
    for (int k = 1; k <= sections; ++k)
    {
        const double theta = (2.0 * k - 1.0) * pi / (2.0 * order);
        qs[k - 1] = 1.0 / (2.0 * std::cos(theta));
    }
    return sections;
}
} // namespace fd::dsp
