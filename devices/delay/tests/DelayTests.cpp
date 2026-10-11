// Delay (M3 step B1): the automated test targets of devices/delay/SPEC.md.

#include "Delay.h"
#include "fd/dsp/Math.h"
#include "fd/dsp/Modulation.h"
#include "fd/testkit/DeviceRender.h"
#include "fd/testkit/Golden.h"
#include "fd/testkit/Metrics.h"
#include "fd/testkit/Signals.h"

#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <complex>
#include <initializer_list>
#include <numbers>
#include <utility>
#include <vector>

using namespace fd;
using namespace fd::testkit;
using D = fd::devices::Delay;

namespace
{
constexpr double kRate = 48000.0;

using Settings = std::initializer_list<std::pair<D::Param, float>>;

/// Only the echoes, exactly: Clean, no filter, no drive or modulation, all wet, times in ms.
constexpr Settings kExact{{D::Mode, 0.0f},    {D::Filter, 0.0f}, {D::Drive, 0.0f}, {D::Wobble, 0.0f},
                          {D::ModTime, 0.0f}, {D::Mix, 100.0f},  {D::Sync, 0.0f}};

void setAll(Device& d, Settings settings)
{
    for (const auto& [param, value] : settings)
        d.params().set(param, value);
}

AudioBuffer renderWith(const AudioBuffer& in, Settings base, Settings settings, RenderOptions o = {})
{
    D d;
    setAll(d, base);
    setAll(d, settings);
    o.sampleRate = kRate;
    return render(d, in, o);
}

int samples(double seconds)
{
    return static_cast<int>(std::lround(seconds * kRate));
}

double db(double ratio)
{
    return 20.0 * std::log10(ratio);
}

/// The RMS of channel `c` over [from, to) seconds.
double rmsOver(const AudioBuffer& b, int c, double from, double to)
{
    double sum = 0.0;
    const int a = samples(from), z = samples(to);
    for (int i = a; i < z; ++i)
        sum += static_cast<double>(b.at(c, i)) * b.at(c, i);
    return std::sqrt(sum / (z - a));
}

/// Where the echo of an impulse at 0 lies, in samples between them: read from its phase at 1 kHz
/// around `expected` (exact for the modulated delay's flat, linear-phase interpolation).
double echoAt(const AudioBuffer& b, int c, double expected)
{
    const int start = static_cast<int>(std::floor(expected)) - 32;
    const auto h = responseAt(b.channel(c) + start, 64, 1000.0, kRate);
    const double period = kRate / 1000.0;
    double at = -std::arg(h) / (2.0 * std::numbers::pi) * period; // in (-period/2, period/2]
    at += start + std::round((expected - start - at) / period) * period;
    return at;
}

/// The intervals between a channel's rising zero crossings over [from, to) samples.
std::vector<double> periods(const AudioBuffer& b, int c, int from, int to)
{
    std::vector<double> crossings, out;
    for (int i = from + 1; i < to; ++i)
    {
        const float a = b.at(c, i - 1), z = b.at(c, i);
        if (a < 0.0f && z >= 0.0f)
            crossings.push_back(i - 1 + a / (a - z));
    }
    for (size_t i = 1; i < crossings.size(); ++i)
        out.push_back(crossings[i] - crossings[i - 1]);
    return out;
}

/// A Hann-windowed 1 kHz burst in both channels: a tone the repeats display's levels describe.
AudioBuffer toneBurst(int length, int total, double amplitude = 0.5)
{
    AudioBuffer b(2, total);
    for (int i = 0; i < length; ++i)
    {
        const double w = 0.5 - 0.5 * std::cos(2.0 * std::numbers::pi * i / length);
        b.at(0, i) = b.at(1, i) =
            static_cast<float>(amplitude * w * std::sin(2.0 * std::numbers::pi * 1000.0 * i / kRate));
    }
    return b;
}
} // namespace

TEST_CASE("Delay: note values and swing at the song's tempo", "[delay][fast]")
{
    const D::TimeSettings dotted{}; // synced, 1/8D, linked
    CHECK(D::times(dotted, 120.0, 4.0).left == 0.375);
    CHECK(D::times(dotted, 60.0, 4.0).left == 0.75);
    D::TimeSettings swung{.sync = true, .leftDiv = 7, .rightDiv = 7, .link = false, .swingPercent = 33.0};
    CHECK(std::abs(D::times(swung, 120.0, 4.0).right - 0.25 * 1.33) < 1e-12);
    CHECK(D::times(swung, 120.0, 4.0).left == 0.25); // swing moves only the right side
    // A bar follows the time signature; the longest synced time is held at ten seconds.
    CHECK(D::divisionBeats(D::numDivisions - 2, 3.0) == 3.0);
    CHECK(D::divisionBeats(D::numDivisions - 1, 4.0) == 8.0);
    D::TimeSettings twoBars{.sync = true, .leftDiv = D::numDivisions - 1};
    CHECK(D::times(twoBars, 20.0, 4.0).left == D::maxSeconds);
    // The note values run from the shortest to the longest.
    for (int i = 1; i < D::numDivisions; ++i)
        CHECK(D::divisionBeats(i, 4.0) > D::divisionBeats(i - 1, 4.0));
}

TEST_CASE("Delay: an impulse comes back at the exact sample, in ms and synced", "[delay][fast]")
{
    struct Case
    {
        const char* name;
        double tempo;
        Settings settings;
        double left, right; // seconds
    };
    const std::array<Case, 6> cases{{
        {"milliseconds", 120.0, {{D::Link, 0.0f}, {D::TimeLeft, 250.0f}, {D::TimeRight, 410.0f}}, 0.25, 0.41},
        {"1/8D and 1/4 at 120",
         120.0,
         {{D::Sync, 1.0f}, {D::Link, 0.0f}, {D::DivLeft, 9.0f}, {D::DivRight, 10.0f}},
         0.375,
         0.5},
        {"1/8D and 1/16 at 60",
         60.0,
         {{D::Sync, 1.0f}, {D::Link, 0.0f}, {D::DivLeft, 9.0f}, {D::DivRight, 4.0f}},
         0.75,
         0.25},
        {"swing +33 %",
         120.0,
         {{D::Sync, 1.0f}, {D::Link, 0.0f}, {D::DivLeft, 7.0f}, {D::DivRight, 7.0f}, {D::Swing, 33.0f}},
         0.25,
         0.3325},
        {"swing -33 %",
         120.0,
         {{D::Sync, 1.0f}, {D::Link, 0.0f}, {D::DivLeft, 7.0f}, {D::DivRight, 7.0f}, {D::Swing, -33.0f}},
         0.25,
         0.1675},
        {"linked", 120.0, {{D::Sync, 1.0f}, {D::DivLeft, 12.0f}, {D::DivRight, 2.0f}}, 0.75, 0.75},
    }};
    for (const auto& c : cases)
    {
        INFO(c.name);
        RenderOptions o;
        o.transport.tempoBpm = c.tempo;
        const auto out = renderWith(impulse(samples(1.0)), kExact, c.settings, o);
        for (const auto& [channel, seconds] : {std::pair{0, c.left}, std::pair{1, c.right}})
        {
            INFO("channel " << channel);
            const int at = samples(seconds);
            // A whole-sample delay is a copy (to rounding: 0.25 x 0.67 isn't exact in binary).
            CHECK(std::abs(out.at(channel, at) - 1.0f) < 1e-6f);
            CHECK(std::abs(out.at(channel, at - 1)) < 1e-6f);
            CHECK(std::abs(out.at(channel, at + 1)) < 1e-6f);
        }
    }

    SECTION("between samples, at 174 BPM")
    {
        RenderOptions o;
        o.transport.tempoBpm = 174.0;
        const auto out =
            renderWith(impulse(samples(1.0)), kExact,
                       {{D::Sync, 1.0f}, {D::Link, 0.0f}, {D::DivLeft, 9.0f}, {D::DivRight, 7.0f}}, o);
        const double beat = 60.0 / 174.0 * kRate;
        CHECK(std::abs(echoAt(out, 0, 0.75 * beat) - 0.75 * beat) < 0.05);
        CHECK(std::abs(echoAt(out, 1, 0.5 * beat) - 0.5 * beat) < 0.05);
    }

    SECTION("the panel reads the tempo the times follow")
    {
        D d;
        RenderOptions o;
        o.sampleRate = kRate;
        o.transport.tempoBpm = 93.0;
        o.transport.timeSigNumerator = 6;
        o.transport.timeSigDenominator = 8;
        (void)render(d, silence(1024), o);
        CHECK(d.analysis().read(D::TempoBpm) == 93.0f);
        CHECK(d.analysis().read(D::BeatsPerBar) == 3.0f);
    }
}

TEST_CASE("Delay: each repeat is the feedback times the last", "[delay][fast]")
{
    for (const float feedback : {50.0f, 80.0f, 100.0f})
    {
        INFO("feedback " << feedback);
        const auto out =
            renderWith(impulse(samples(1.0)), kExact, {{D::TimeLeft, 100.0f}, {D::Feedback, feedback}});
        for (int k = 1; k <= 9; ++k)
        {
            INFO("repeat " << k);
            const double expected = std::pow(feedback / 100.0, k - 1);
            CHECK(std::abs(db(out.at(0, samples(0.1 * k)) / expected)) < 0.05);
            CHECK(std::abs(db(out.at(1, samples(0.1 * k)) / expected)) < 0.05);
        }
    }
}

TEST_CASE("Delay: each pass through the filter takes FeedbackPath::magnitude", "[delay][fast]")
{
    constexpr double low = 200.0, high = 4000.0, feedback = 0.8;
    const int period = samples(0.25);
    const auto out = renderWith(impulse(4 * period + 1000), kExact,
                                {{D::Filter, 1.0f},
                                 {D::FilterLow, static_cast<float>(low)},
                                 {D::FilterHigh, static_cast<float>(high)},
                                 {D::TimeLeft, 250.0f},
                                 {D::Feedback, static_cast<float>(feedback * 100.0)}});
    const auto repeat = [&](int k, double hz)
    { return std::abs(responseAt(out.channel(0) + k * period - 64, period, hz, kRate)); };
    for (const double hz : {100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0})
        for (int k = 2; k <= 3; ++k)
        {
            INFO(hz << " Hz, repeat " << k);
            const double expected = feedback * dsp::FeedbackPath::magnitude(hz, low, high, kRate);
            CHECK(std::abs(db(repeat(k, hz) / repeat(k - 1, hz) / expected)) < 0.2);
        }
}

TEST_CASE("Delay: ping-pong bounces from side to side", "[delay][fast]")
{
    // An impulse in the left only enters the left line (as the mono sum) and crosses each pass.
    auto in = impulse(samples(1.0), 2);
    in.at(1, 0) = 0.0f;
    const auto out = renderWith(in, kExact,
                                {{D::Stereo, 1.0f},
                                 {D::Link, 0.0f},
                                 {D::TimeLeft, 100.0f},
                                 {D::TimeRight, 150.0f},
                                 {D::Feedback, 50.0f}});
    std::vector<std::pair<int, int>> echoes; // (sample, side)
    double at = 0.0, level = 0.5;
    for (int k = 0; k < 6; ++k)
    {
        at += k % 2 == 0 ? 0.1 : 0.15;
        const int side = k % 2;
        INFO("echo " << k);
        CHECK(std::abs(db(out.at(side, samples(at)) / level)) < 0.05);
        CHECK(out.at(1 - side, samples(at)) == 0.0f);
        echoes.emplace_back(samples(at), side);
        level *= 0.5;
    }
    // Nothing anywhere else.
    double elsewhere = 0.0;
    for (int c = 0; c < 2; ++c)
        for (int i = 0; i < samples(0.75); ++i)
            if (std::find(echoes.begin(), echoes.end(), std::pair{i, c}) == echoes.end())
                elsewhere += std::abs(out.at(c, i));
    CHECK(elsewhere < 1e-6);
}

TEST_CASE("Delay: mid-side echoes the mid at Left's time and the side at Right's", "[delay][fast]")
{
    const Settings ms{{D::Stereo, 2.0f},
                      {D::Link, 0.0f},
                      {D::TimeLeft, 300.0f},
                      {D::TimeRight, 500.0f},
                      {D::Feedback, 50.0f}};
    const auto isEcho = [](int i, double seconds)
    {
        const int t = samples(seconds);
        return i > 0 && i % t == 0;
    };

    SECTION("a mono input: the mid only, Right's time not heard")
    {
        const auto out = renderWith(impulse(samples(1.4)), kExact, ms);
        double elsewhere = 0.0;
        for (int i = 0; i < out.numSamples(); ++i)
        {
            CHECK(out.at(0, i) == out.at(1, i));
            if (!isEcho(i, 0.3))
                elsewhere += std::abs(out.at(0, i));
        }
        CHECK(elsewhere < 1e-6);
        CHECK(out.at(0, samples(0.3)) == 1.0f);
        CHECK(std::abs(db(out.at(0, samples(0.9)) / 0.25)) < 0.05);
    }

    SECTION("a side-only input: the side at Right's time, opposite in the two channels")
    {
        auto in = impulse(samples(1.4));
        in.at(1, 0) = -1.0f;
        const auto out = renderWith(in, kExact, ms);
        double elsewhere = 0.0;
        for (int i = 0; i < out.numSamples(); ++i)
        {
            CHECK(out.at(0, i) == -out.at(1, i));
            if (!isEcho(i, 0.5))
                elsewhere += std::abs(out.at(0, i));
        }
        CHECK(elsewhere < 1e-6);
        CHECK(out.at(0, samples(0.5)) == 1.0f);
    }
}

TEST_CASE("Delay: Freeze holds what's in the loop and lets nothing new in", "[delay][fast]")
{
    // A burst, frozen 200 ms later (the filter on: Freeze bypasses it), then a louder one that
    // must not get in.
    const int total = samples(31.0);
    AudioBuffer in(2, total);
    const auto first = whiteNoise(samples(0.1), 7, 0.25f);
    const auto second = whiteNoise(samples(0.1), 8, 0.5f);
    for (int c = 0; c < 2; ++c)
        for (int i = 0; i < first.numSamples(); ++i)
        {
            in.at(c, i) = first.at(c, i);
            in.at(c, samples(5.0) + i) = second.at(c, i);
        }
    RenderOptions o;
    o.beforeBlock = [](Device& d, std::int64_t start)
    {
        if (start >= samples(0.2))
            d.params().set(D::Freeze, 1.0f);
    };
    const auto out =
        renderWith(in, {{D::Mix, 100.0f}, {D::Sync, 0.0f}}, {{D::TimeLeft, 500.0f}, {D::Feedback, 50.0f}}, o);
    const double held = rmsOver(out, 0, 1.0, 1.5);
    REQUIRE(held > 0.01);
    for (double t = 1.5; t + 0.5 <= 31.0; t += 0.5)
    {
        INFO("at " << t << " s");
        CHECK(std::abs(db(rmsOver(out, 0, t, t + 0.5) / held)) < 0.5);
        CHECK(std::abs(db(rmsOver(out, 1, t, t + 0.5) / held)) < 1.0); // the other side's noise
    }
}

TEST_CASE("Delay: full feedback with full drive stays bounded", "[delay][fast]")
{
    // A 0 dBFS input into a loop that gives back everything: the drive holds it within +6 dB.
    const auto in = sine(200.0, kRate, samples(10.0), 1.0f);
    for (const float mode : {0.0f, 1.0f, 2.0f})
        for (const float filter : {0.0f, 1.0f})
        {
            INFO("mode " << mode << ", filter " << filter);
            const auto out = renderWith(in, kExact,
                                        {{D::Mode, mode},
                                         {D::Filter, filter},
                                         {D::Feedback, 100.0f},
                                         {D::Drive, 100.0f},
                                         {D::TimeLeft, 100.0f}});
            REQUIRE(allFinite(out));
            CHECK(peakDb(out) <= 6.0);
            // No growth: the last second is no louder than the second one.
            CHECK(rmsOver(out, 0, 9.0, 10.0) <= rmsOver(out, 0, 1.0, 2.0) * dsp::dbToGain(0.5f));
        }
}

TEST_CASE("Delay: time changes glide (Repitch) or crossfade (Fade), without clicks", "[delay][fast]")
{
    const auto in = sine(1000.0, kRate, samples(2.5), 0.5f);
    const auto change = [](float to)
    {
        RenderOptions o;
        o.beforeBlock = [to](Device& d, std::int64_t start)
        {
            if (start >= samples(1.0))
                d.params().set(D::TimeLeft, to);
        };
        return o;
    };
    for (const float transition : {0.0f, 1.0f})
    {
        INFO((transition == 0.0f ? "Repitch" : "Fade"));
        // Clicks: a time whose sine lands at another phase, with the echoes feeding back.
        const auto out =
            renderWith(in, kExact, {{D::Transition, transition}, {D::TimeLeft, 300.0f}, {D::Feedback, 35.0f}},
                       change(412.3f));
        CHECK(clickScore(out, 0) < 8.0);

        // Pitch: with no feedback, a glide bends it and a crossfade doesn't.
        const auto dry =
            renderWith(in, kExact, {{D::Transition, transition}, {D::TimeLeft, 300.0f}, {D::Feedback, 0.0f}},
                       change(420.0f));
        double worst = 0.0;
        for (const double p : periods(dry, 0, samples(1.0), samples(1.2)))
            worst = std::max(worst, std::abs(p / 48.0 - 1.0));
        if (transition == 0.0f)
            CHECK(worst > 0.1);
        else
            CHECK(worst < 0.02);
    }
}

TEST_CASE("Delay: Ducking dips the echoes while the input plays", "[delay][fast]")
{
    // -10 dBFS for two seconds, then silence; the echoes keep going round.
    auto in = sine(1000.0, kRate, samples(4.0), 0.31623f);
    for (int c = 0; c < 2; ++c)
        for (int i = samples(2.0); i < in.numSamples(); ++i)
            in.at(c, i) = 0.0f;
    const auto ducked = renderWith(
        in, kExact,
        {{D::TimeLeft, 100.0f}, {D::Feedback, 80.0f}, {D::DuckRelease, 300.0f}, {D::Duck, 100.0f}});
    const auto open =
        renderWith(in, kExact, {{D::TimeLeft, 100.0f}, {D::Feedback, 80.0f}, {D::DuckRelease, 300.0f}});
    CHECK(db(rmsOver(open, 0, 1.0, 1.5) / rmsOver(ducked, 0, 1.0, 1.5)) >= 25.0);
    // Back within Duck Release of the input stopping.
    CHECK(std::abs(db(rmsOver(open, 0, 2.31, 2.36) / rmsOver(ducked, 0, 2.31, 2.36))) < 0.5);
}

TEST_CASE("Delay: at 0 % Dry/Wet the output is the input", "[delay][fast]")
{
    const auto in = pinkNoise(samples(2.0), 3, 0.5f);
    const auto out = renderWith(in, {},
                                {{D::Mix, 0.0f},
                                 {D::Feedback, 80.0f},
                                 {D::Drive, 50.0f},
                                 {D::Mode, 1.0f},
                                 {D::ModTime, 50.0f},
                                 {D::Wobble, 50.0f}});
    CHECK(nullResidualDb(in, out) < -120.0);
}

TEST_CASE("Delay: the repeats display shows what the audio plays", "[delay][fast]")
{
    SECTION("times and levels, side by side and ping-pong")
    {
        for (const auto stereo : {D::StereoKind::Stereo, D::StereoKind::PingPong})
        {
            INFO("stereo " << static_cast<int>(stereo));
            const Settings settings{{D::Stereo, static_cast<float>(stereo)},
                                    {D::Link, 0.0f},
                                    {D::TimeLeft, 120.0f},
                                    {D::TimeRight, 170.0f},
                                    {D::Feedback, 60.0f}};
            const auto out = renderWith(impulse(samples(2.0)), kExact, settings);
            const auto times =
                D::times({.sync = false, .leftMs = 120.0, .rightMs = 170.0, .link = false}, 120.0, 4.0);
            std::array<D::Repeat, 64> bars{};
            const int count = D::repeats(
                times, stereo, 0.6, D::passGain(D::ModeKind::Clean, false, 120.0, 6000.0, kRate), 1.9, bars);
            REQUIRE(count > 8);
            double heard = 0.0, shown = 0.0;
            for (int i = 0; i < count; ++i)
            {
                const auto& bar = bars[static_cast<size_t>(i)];
                INFO("bar " << i << " at " << bar.seconds << " s");
                const int at = samples(bar.seconds);
                CHECK(std::abs(bar.seconds * kRate - at) < 1e-6); // whole samples here: exact
                CHECK(std::abs(db(out.at(bar.lane, at) / bar.level)) < 0.1);
                shown += bar.level;
            }
            for (int c = 0; c < 2; ++c)
                for (int i = 0; i < samples(1.9); ++i)
                    if (std::abs(out.at(c, i)) >= 1e-3f)
                        heard += std::abs(out.at(c, i));
            CHECK(std::abs(heard - shown) < 1e-4); // and no echo the display leaves out
        }
    }

    SECTION("levels through the filter and the mode's tone, at 1 kHz")
    {
        constexpr double feedback = 0.7;
        const int period = samples(0.25);
        const auto in = toneBurst(samples(0.04), 6 * period);
        for (const auto mode : {D::ModeKind::Clean, D::ModeKind::Tape, D::ModeKind::Analog})
        {
            INFO("mode " << static_cast<int>(mode));
            const auto out = renderWith(in, kExact,
                                        {{D::Mode, static_cast<float>(mode)},
                                         {D::Filter, 1.0f},
                                         {D::TimeLeft, 250.0f},
                                         {D::Feedback, static_cast<float>(feedback * 100.0)}});
            const double pass = D::passGain(mode, true, 120.0, 6000.0, kRate);
            const double first = rmsOver(out, 0, 0.24, 0.33);
            CHECK(std::abs(db(first / rmsOver(in, 0, 0.0, 0.09))) < 0.1);
            for (int k = 2; k <= 5; ++k)
            {
                INFO("repeat " << k);
                const double t = 0.25 * k;
                CHECK(std::abs(db(rmsOver(out, 0, t - 0.01, t + 0.08) / first /
                                  std::pow(feedback * pass, k - 1))) < 0.1);
            }
        }
    }
}

TEST_CASE("Delay: the tail is the time the echoes take to fall 60 dB", "[delay][fast]")
{
    D d;
    setAll(d, kExact);
    d.params().set(D::TimeLeft, 200.0f);
    d.params().set(D::Feedback, 50.0f);
    // 0.5 per pass: 60 dB in about ten passes, and one more.
    CHECK(std::abs(d.tailSeconds() - (3.0 / std::log10(2.0) + 1.0) * 0.2 - 0.1) < 1e-9);
    d.params().set(D::Freeze, 1.0f);
    CHECK(d.tailSeconds() == 60.0);
}

TEST_CASE("Delay: the modes' character matches golden metrics", "[delay][fast]")
{
    nlohmann::json metrics;
    constexpr const char* kModeNames[] = {"clean", "tape", "analog"};
    const int period = samples(0.25);
    for (const auto mode : {D::ModeKind::Clean, D::ModeKind::Tape, D::ModeKind::Analog})
    {
        const std::string name = kModeNames[static_cast<int>(mode)];
        // The tone of one pass: an impulse's second echo against its first, low, middle and high.
        const auto ir =
            renderWith(impulse(3 * period), kExact,
                       {{D::Mode, static_cast<float>(mode)}, {D::TimeLeft, 250.0f}, {D::Feedback, 100.0f}});
        for (const double hz : {100.0, 1000.0, 8000.0})
            metrics[name + ".pass_" + std::to_string(static_cast<int>(hz)) + "hz_db"] =
                db(std::abs(responseAt(ir.channel(0) + 2 * period - 64, period, hz, kRate)) /
                   std::abs(responseAt(ir.channel(0) + period - 64, period, hz, kRate)));
        // Drive: a loud burst's repeats, filter on as it comes.
        const auto out = renderWith(toneBurst(samples(0.04), 5 * period, 1.0), kExact,
                                    {{D::Mode, static_cast<float>(mode)},
                                     {D::Filter, 1.0f},
                                     {D::TimeLeft, 250.0f},
                                     {D::Feedback, 70.0f},
                                     {D::Drive, 60.0f}});
        for (int k = 1; k <= 4; ++k)
            metrics[name + ".drive60.repeat" + std::to_string(k) + "_db"] =
                db(rmsOver(out, 0, 0.25 * k - 0.01, 0.25 * k + 0.08));
    }
    // Wobble at 100 %: how far it bends a 1 kHz tone's pitch over four seconds.
    const auto wavering = renderWith(sine(1000.0, kRate, samples(4.5), 0.5f), kExact,
                                     {{D::TimeLeft, 400.0f}, {D::Feedback, 0.0f}, {D::Wobble, 100.0f}});
    double worst = 0.0;
    for (const double p : periods(wavering, 0, samples(0.5), samples(4.5)))
        worst = std::max(worst, std::abs(p / 48.0 - 1.0));
    metrics["wobble100.max_pitch_dev_pct"] = 100.0 * worst;

    const auto result = checkGolden(std::string(FD_GOLDEN_DIR) + "/delay/character.json", metrics, 0.05);
    INFO(result.report);
    CHECK(result.passed);
}
