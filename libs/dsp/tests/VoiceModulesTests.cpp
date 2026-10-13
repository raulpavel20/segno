// The Wavetable Synth's filter, envelopes, LFOs and random sources (devices/wavetable/SPEC.md).
#include "fd/dsp/Math.h"
#include "fd/dsp/VoiceModules.h"
#include "fd/testkit/Metrics.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <vector>

using namespace fd::dsp;
using Catch::Approx;

namespace
{
constexpr double sr = 48000.0;
}

TEST_CASE("voice filter: every type sounds as its drawn response says", "[fast]")
{
    using T = VoiceFilter::Type;
    struct Case
    {
        T type;
        double cutoff, resonance, shape, toleranceDb;
    };
    const Case cases[] = {
        {T::Lp12, 1000.0, 0.3, 0.0, 0.05},    {T::Lp24, 800.0, 0.5, 0.0, 0.05},
        {T::Ladder, 1200.0, 0.4, 0.0, 0.05},  {T::Diode, 900.0, 0.4, 0.0, 0.05},
        {T::Hp12, 500.0, 0.2, 0.0, 0.05},     {T::Hp24, 300.0, 0.6, 0.0, 0.05},
        {T::Bp12, 2000.0, 0.5, 0.0, 0.05},    {T::Bp24, 1500.0, 0.3, 0.0, 0.05},
        {T::Notch, 1000.0, 0.3, 0.0, 0.05},   {T::Peak, 1000.0, 0.6, 0.0, 0.05},
        {T::CombPlus, 220.0, 0.7, 0.3, 0.3},  {T::CombMinus, 330.0, 0.6, 0.0, 0.3},
        {T::Formant, 1000.0, 0.5, 0.4, 0.05}, {T::Phaser, 900.0, 0.5, 0.6, 0.05},
    };
    for (const auto& c : cases)
    {
        VoiceFilter f;
        f.prepare(sr);
        constexpr int n = 1 << 16;
        std::vector<float> ir(n);
        constexpr float tiny = 1e-4f; // small signals: the saturating types stay linear
        for (int i = 0; i < n; ++i)
            ir[static_cast<size_t>(i)] =
                f.process(i == 0 ? tiny : 0.0f, c.type, c.cutoff, c.resonance, 0.0, c.shape) / tiny;
        for (const double hz : {60.0, 250.0, 700.0, 1111.0, 3000.0, 9000.0})
        {
            INFO("type " << static_cast<int>(c.type) << " at " << hz << " Hz");
            const double measured =
                20.0 * std::log10(std::abs(fd::testkit::responseAt(ir.data(), n, hz, sr)));
            const double drawn =
                20.0 * std::log10(VoiceFilter::magnitude(c.type, hz, c.cutoff, c.resonance, c.shape, sr));
            if (drawn > -60.0)
                CHECK(measured == Approx(drawn).margin(c.toleranceDb));
        }
    }
}

TEST_CASE("voice filter: the ladders sing at full resonance, bounded; drive saturates", "[fast]")
{
    for (const auto type : {VoiceFilter::Type::Ladder, VoiceFilter::Type::Diode})
    {
        VoiceFilter f;
        f.prepare(sr);
        float peak = 0.0f, late = 0.0f;
        for (int i = 0; i < 48000; ++i)
        {
            const float y = f.process(i == 0 ? 0.5f : 0.0f, type, 440.0, 1.0, 0.0, 0.0);
            peak = std::max(peak, std::abs(y));
            if (i > 40000)
                late = std::max(late, std::abs(y));
        }
        INFO("type " << static_cast<int>(type));
        CHECK(late > 0.05f); // still ringing: self-oscillation
        CHECK(peak < 2.5f);
    }
    VoiceFilter f;
    f.prepare(sr);
    float loudest = 0.0f;
    for (int i = 0; i < 4800; ++i)
        loudest =
            std::max(loudest, std::abs(f.process(4.0f * static_cast<float>(std::sin(twoPi * 100.0 * i / sr)),
                                                 VoiceFilter::Type::Lp12, 18000.0, 0.0, 1.0, 0.0)));
    CHECK(loudest < 1.1f);
}

TEST_CASE("curved envelope: times, curves and modes", "[fast]")
{
    CurvedEnvelope e;
    e.prepare(sr);
    CurvedEnvelope::Settings s;
    s.attackMs = 10.0, s.holdMs = 5.0, s.decayMs = 20.0, s.sustain = 0.5, s.releaseMs = 30.0;
    e.set(s);
    e.noteOn();
    int peakAt = -1;
    for (int i = 0; i < 480 + 240 + 960 + 10; ++i)
        if (e.next() >= 1.0f && peakAt < 0)
            peakAt = i;
    CHECK(std::abs(peakAt - 479) <= 1); // the attack reaches the top after its 10 ms
    CHECK(e.stage() == CurvedEnvelope::Stage::Sustain);
    CHECK(e.level() == Approx(0.5f));
    e.noteOff();
    int silentAt = -1;
    for (int i = 0; i < 2000 && silentAt < 0; ++i)
        if (e.next() == 0.0f && !e.isActive())
            silentAt = i;
    CHECK(std::abs(silentAt - 1440) <= 2);

    // Curves: above zero quick to start, below zero slow; zero straight.
    CHECK(CurvedEnvelope::shape(0.3, 0.0) == Approx(0.3));
    CHECK(CurvedEnvelope::shape(0.3, 0.6) > 0.3);
    CHECK(CurvedEnvelope::shape(0.3, -0.6) < 0.3);
    CHECK(CurvedEnvelope::shape(1.0, 0.8) == Approx(1.0));

    // Loop repeats attack to decay while held; One-shot plays through a short note.
    s.mode = CurvedEnvelope::Mode::Loop;
    e.set(s);
    e.reset();
    e.noteOn();
    int attacks = 0;
    auto last = CurvedEnvelope::Stage::Idle;
    for (int i = 0; i < 48000 / 10; ++i)
    {
        e.next();
        if (e.stage() == CurvedEnvelope::Stage::Attack && last != CurvedEnvelope::Stage::Attack)
            ++attacks;
        last = e.stage();
    }
    CHECK(attacks >= 3);
    s.mode = CurvedEnvelope::Mode::OneShot;
    e.set(s);
    e.reset();
    e.noteOn();
    e.next();
    e.noteOff(); // too early: it goes on
    for (int i = 0; i < 400; ++i)
        e.next();
    CHECK(e.stage() == CurvedEnvelope::Stage::Attack);
}

TEST_CASE("shape LFO and random sources", "[fast]")
{
    CurveTable sine{};
    for (size_t i = 0; i < sine.size(); ++i)
        sine[i] =
            static_cast<float>(0.5 + 0.5 * std::sin(twoPi * static_cast<double>(i) / (sine.size() - 1)));
    ShapeLfo lfo;
    lfo.reset();
    CHECK(lfo.advance(sine, ShapeLfo::Mode::Free, 120, 1.0 / 480.0) ==
          Approx(1.0f).margin(1e-3));                                                           // a quarter
    CHECK(lfo.advance(sine, ShapeLfo::Mode::Free, 0, 0.0, 0.75) == Approx(-1.0f).margin(1e-3)); // the song's
    lfo.noteOn(0.0);
    for (int i = 0; i < 10; ++i)
        lfo.advance(sine, ShapeLfo::Mode::Envelope, 100, 1.0 / 480.0);
    CHECK(lfo.phase() == 1.0); // played once, then held

    RandomSource a, b;
    a.reset(42);
    b.reset(42);
    for (const auto mode : {RandomSource::Mode::Steps, RandomSource::Mode::Smooth, RandomSource::Mode::Chaos})
        for (int i = 0; i < 2000; ++i)
        {
            const float va = a.advance(mode, 32, 0.002), vb = b.advance(mode, 32, 0.002);
            CHECK(va == vb);
            CHECK(std::abs(va) <= 1.0f);
        }
}
