// Tuner (devices/tuner/SPEC.md): the audio passes unchanged, and Mute fades it out cleanly; the
// note and its cents against the Reference; In key against the song's key; a note held on the
// boundary between two doesn't flicker; the history, the strobe and the time since a reading.
#include "Tuner.h"
#include "fd/device/Realtime.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <functional>
#include <numbers>
#include <vector>

using namespace fd;
using Catch::Approx;
using T = fd::devices::Tuner;

namespace
{
constexpr double kPi = std::numbers::pi;

double hzOf(double midi, double reference = 440.0)
{
    return reference * std::exp2((midi - 69.0) / 12.0);
}

/// A tuner fed a signal by hand, in uneven blocks, the song's key the test's.
struct Fixture
{
    explicit Fixture(double rate = 48000.0) : rate(rate)
    {
        tuner.prepare({.sampleRate = rate, .maxBlockSize = 1024});
    }

    /// Plays `seconds` of `signal` (time in seconds → sample), from `t0`; what came out.
    std::vector<float> play(const std::function<double(double)>& signal, double seconds, KeyInfo key = {})
    {
        std::vector<float> in(1024), outL(1024), outR(1024), heard;
        const auto total = static_cast<std::int64_t>(seconds * rate);
        std::int64_t done = 0;
        for (int block = 1; done < total; block = block * 7 % 1021 + 1)
        {
            const int n = static_cast<int>(std::min<std::int64_t>(block, total - done));
            for (int i = 0; i < n; ++i)
                in[static_cast<size_t>(i)] =
                    static_cast<float>(signal(static_cast<double>(t + done + i) / rate));
            float* ins[2] = {in.data(), in.data()};
            float* outs[2] = {outL.data(), outR.data()};
            ProcessContext ctx;
            ctx.input = {ins, 2};
            ctx.output = {outs, 2};
            ctx.numSamples = n;
            ctx.transport.key = key;
            processRealtime(tuner, ctx);
            heard.insert(heard.end(), outL.begin(), outL.begin() + n);
            done += n;
        }
        t += total;
        return heard;
    }

    [[nodiscard]] float read(int slot) { return tuner.analysis().read(slot); }

    double rate;
    std::int64_t t = 0;
    T tuner;
};

std::function<double(double)> sine(double hz, double amplitude = 0.3)
{
    return [=](double t) { return amplitude * std::sin(2.0 * kPi * hz * t); };
}
} // namespace

TEST_CASE("tuner: the audio passes unchanged, with no delay; Mute fades it out without a click", "[fast]")
{
    Fixture f;
    const auto signal = sine(220.0);
    const auto heard = f.play(signal, 0.3);
    for (size_t i = 0; i < heard.size(); ++i)
        REQUIRE(heard[i] == static_cast<float>(signal(static_cast<double>(i) / f.rate)));

    f.tuner.params().set(T::Mute, 1.0f);
    const auto muted = f.play(signal, 0.1);
    // Silent after the 10 ms fade...
    for (size_t i = static_cast<size_t>(0.011 * f.rate); i < muted.size(); ++i)
        REQUIRE(muted[i] == 0.0f);
    // ...and no step on the way: each sample within what the sine itself moves.
    const double maxStep = 0.3 * 2.0 * kPi * 220.0 / f.rate + 1e-4;
    float last = heard.back();
    for (size_t i = 0; i < 500; ++i)
    {
        CHECK(std::abs(muted[i] - last) <= maxStep + 0.3 / (0.010 * f.rate) + 1e-6);
        last = muted[i];
    }
}

TEST_CASE("tuner: the note and its cents against the Reference", "[fast]")
{
    {
        Fixture f;
        f.play(sine(hzOf(57) * std::exp2(7.0 / 1200.0)), 0.6); // A2 (Segno's names), 7 cents sharp
        CHECK(f.read(T::Note) == 57.0f);
        CHECK(f.read(T::Cents) == Approx(7.0).margin(0.1));
        CHECK(f.read(T::Frequency) == Approx(hzOf(57) * std::exp2(7.0 / 1200.0)).epsilon(1e-4));
        CHECK(f.read(T::Clarity) > 0.95f);
        CHECK(f.read(T::Since) == 0.0f);
    }
    {
        Fixture f;
        f.tuner.params().set(T::Reference, 415.0f);
        f.play(sine(415.0), 0.6); // A, at a baroque pitch
        CHECK(f.read(T::Note) == 69.0f);
        CHECK(f.read(T::Cents) == Approx(0.0).margin(0.1));
    }
}

TEST_CASE("tuner: nothing heard, no reading; the time since one grows", "[fast]")
{
    Fixture f;
    f.play(sine(220.0), 0.4);
    REQUIRE(std::isfinite(f.read(T::Note)));
    f.play([](double) { return 0.0; }, 0.5);
    CHECK_FALSE(std::isfinite(f.read(T::Note)));
    CHECK_FALSE(std::isfinite(f.read(T::Cents)));
    CHECK(f.read(T::Since) > 0.3f);
}

TEST_CASE("tuner: In key measures against the song's key's notes", "[fast]")
{
    // A minor: A B C D E F G.
    KeyInfo aMinor{.root = 9,
                   .notes =
                       (1u << 9) | (1u << 11) | (1u << 0) | (1u << 2) | (1u << 4) | (1u << 5) | (1u << 7)};
    CHECK(T::nearestNote(68.0, &aMinor) == 67); // G sharp: G below, at a tie
    CHECK(T::nearestNote(68.4, &aMinor) == 69); // nearer A
    CHECK(T::nearestNote(68.4, nullptr) == 68); // every note: G sharp
    // A G sharp 20 cents flat: nearer G, 80 cents over it.
    Fixture f;
    f.tuner.params().set(T::Notes, 1.0f);
    f.play(sine(hzOf(67.8)), 0.6, aMinor);
    CHECK(f.read(T::Note) == 67.0f);
    CHECK(f.read(T::Cents) == Approx(80.0).margin(0.2));
    CHECK(f.read(T::KeyRoot) == 9.0f);
    // Without a key (a plugin host), In key measures against every note.
    Fixture g;
    g.tuner.params().set(T::Notes, 1.0f);
    g.play(sine(hzOf(68)), 0.6);
    CHECK(g.read(T::Note) == 68.0f);
    CHECK(g.read(T::KeyRoot) == -1.0f);
}

TEST_CASE("tuner: a pitch gliding over the boundary between two notes, with vibrato, changes note once",
          "[fast]")
{
    Fixture f;
    // From 45 cents over A to 60 over it in four seconds, ±2 cents of vibrato at 5 Hz.
    double phase = 0.0, lastT = 0.0;
    const auto glide = [&](double t)
    {
        const double cents = 45.0 + 15.0 * t / 4.0 + 2.0 * std::sin(2.0 * kPi * 5.0 * t);
        phase += 2.0 * kPi * hzOf(57.0 + cents / 100.0) * (t - lastT);
        lastT = t;
        return 0.3 * std::sin(phase);
    };
    int changes = 0;
    float note = NAN;
    for (int step = 0; step < 80; ++step)
    {
        f.play(glide, 0.05);
        const float now = f.read(T::Note);
        if (std::isfinite(now) && std::isfinite(note) && now != note)
            ++changes;
        if (std::isfinite(now))
            note = now;
    }
    CHECK(changes == 1);
    CHECK(note == 58.0f);
}

TEST_CASE("tuner: the history keeps the pitch; the strobe stands still in tune and turns when off", "[fast]")
{
    Fixture f;
    f.play(sine(hzOf(57)), 0.6);
    REQUIRE(f.tuner.historyWritten() > 20);
    CHECK(f.tuner.historyAt(0) == Approx(57.0).margin(0.002));
    CHECK(f.tuner.readingSeconds() == Approx(256.0 / 24000.0));
    const float still = f.read(T::StrobePhase);
    f.play(sine(hzOf(57)), 0.5);
    const float after = f.read(T::StrobePhase);
    CHECK(std::abs(after - still) < 0.01f); // in tune: barely moves

    Fixture sharp;
    sharp.play(sine(hzOf(57) * std::exp2(10.0 / 1200.0)), 0.6);
    const float from = sharp.read(T::StrobePhase);
    sharp.play(sine(hzOf(57) * std::exp2(10.0 / 1200.0)), 0.25);
    // 10 cents sharp: 0.4 turns a second, so a tenth of a turn in a quarter second.
    float moved = sharp.read(T::StrobePhase) - from;
    moved -= std::floor(moved);
    CHECK(moved == Approx(0.1).margin(0.01));
}
