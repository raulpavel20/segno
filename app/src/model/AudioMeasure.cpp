#include "model/AudioMeasure.h"

#include <fd/dsp/Fft.h>
#include <fd/dsp/Loudness.h>

#include <algorithm>
#include <cmath>
#include <complex>
#include <limits>

namespace fd::model
{
namespace
{
constexpr double kMinusInf = -std::numeric_limits<double>::infinity();
constexpr int kFftOrder = 12; // 4096 points
constexpr double kHannEnbw = 1.5; // a full-scale sine's summed bin power with the Fft's window

double toDb(double amplitude)
{
    return amplitude > 0.0 ? 20.0 * std::log10(amplitude) : kMinusInf;
}

struct BandSpec
{
    const char* name;
    double low, high;
};
constexpr BandSpec kBands[] = {{"sub", 20.0, 60.0},          {"bass", 60.0, 250.0},       {"low mids", 250.0, 500.0},
                               {"mids", 500.0, 2000.0},      {"high mids", 2000.0, 4000.0}, {"presence", 4000.0, 6000.0},
                               {"air", 6000.0, 20000.0}};

void put16(std::vector<std::uint8_t>& out, std::uint16_t v)
{
    out.push_back(static_cast<std::uint8_t>(v & 0xff));
    out.push_back(static_cast<std::uint8_t>(v >> 8));
}

void put32(std::vector<std::uint8_t>& out, std::uint32_t v)
{
    for (int i = 0; i < 4; ++i)
        out.push_back(static_cast<std::uint8_t>((v >> (8 * i)) & 0xff));
}

void putTag(std::vector<std::uint8_t>& out, const char* tag)
{
    out.insert(out.end(), tag, tag + 4);
}
} // namespace

AudioMeasurements measureAudio(const std::vector<std::vector<float>>& channels, double sampleRate)
{
    AudioMeasurements m;
    m.sampleRate = sampleRate;
    m.channels = static_cast<int>(channels.size());
    const size_t frames = channels.empty() ? 0 : channels[0].size();
    m.seconds = sampleRate > 0.0 ? static_cast<double>(frames) / sampleRate : 0.0;

    double peak = 0.0, sumSquares = 0.0;
    for (const auto& c : channels)
    {
        double channelPeak = 0.0, channelSquares = 0.0;
        for (float x : c)
        {
            const double a = std::abs(static_cast<double>(x));
            channelPeak = std::max(channelPeak, a);
            channelSquares += a * a;
            if (a >= 1.0)
                ++m.clippedSamples;
        }
        m.channelPeakDb.push_back(toDb(channelPeak));
        m.channelRmsDb.push_back(toDb(frames > 0 ? std::sqrt(channelSquares / static_cast<double>(frames)) : 0.0));
        peak = std::max(peak, channelPeak);
        sumSquares += channelSquares;
    }
    const double samples = static_cast<double>(frames) * static_cast<double>(std::max<size_t>(1, channels.size()));
    const double rms = frames > 0 ? std::sqrt(sumSquares / samples) : 0.0;
    m.peakDb = toDb(peak);
    m.rmsDb = toDb(rms);
    m.silent = peak < 1e-6;
    m.crestDb = m.silent ? 0.0 : m.peakDb - m.rmsDb;

    // Loudness and true peak: the first two channels (left and right) for loudness, all for peaks.
    std::vector<const float*> pointers;
    for (const auto& c : channels)
        pointers.push_back(c.data());
    dsp::LoudnessMeter loudness;
    loudness.prepare(sampleRate > 0.0 ? sampleRate : 48000.0, std::clamp(static_cast<int>(channels.size()), 1, 2));
    dsp::TruePeakDetector truePeak;
    truePeak.prepare(std::max(1, static_cast<int>(channels.size())));
    if (!channels.empty())
    {
        loudness.process(pointers.data(), static_cast<int>(frames));
        truePeak.process(pointers.data(), static_cast<int>(channels.size()), static_cast<int>(frames));
    }
    m.truePeakDb = std::max(m.peakDb, toDb(truePeak.peak()));
    m.integratedLufs = loudness.integrated();
    m.shortTermMaxLufs = loudness.maxShortTerm();
    m.momentaryMaxLufs = loudness.maxMomentary();

    if (channels.size() == 2 && !m.silent)
    {
        double lr = 0.0, ll = 0.0, rr = 0.0;
        for (size_t i = 0; i < frames; ++i)
        {
            const double l = channels[0][i], r = channels[1][i];
            lr += l * r;
            ll += l * l;
            rr += r * r;
        }
        if (ll > 0.0 && rr > 0.0)
            m.correlation = std::clamp(lr / std::sqrt(ll * rr), -1.0, 1.0);
    }

    // Spectral balance: bin power averaged over the channels and half-overlapping frames (each
    // channel on its own, so out-of-phase content counts as it sounds in stereo).
    dsp::Fft fft(kFftOrder);
    const auto size = static_cast<size_t>(fft.size());
    const size_t bins = size / 2 + 1;
    std::vector<double> power(bins, 0.0);
    std::vector<float> block(size), magnitudes(bins);
    std::vector<std::complex<float>> scratch;
    int blocks = 0;
    for (const auto& c : channels)
        for (size_t start = 0; start == 0 || start + size / 2 < frames; start += size / 2)
        {
            std::fill(block.begin(), block.end(), 0.0f);
            for (size_t i = 0; i < size && start + i < frames; ++i)
                block[i] = c[start + i];
            fft.magnitudes(block.data(), magnitudes.data(), scratch);
            for (size_t b = 0; b < bins; ++b)
                power[b] += static_cast<double>(magnitudes[b]) * magnitudes[b];
            ++blocks;
            if (frames == 0)
                break;
        }
    const double binHz = sampleRate / static_cast<double>(size);
    for (const auto& spec : kBands)
    {
        double sum = 0.0;
        for (size_t b = 1; b < bins; ++b)
        {
            const double hz = static_cast<double>(b) * binHz;
            if (hz >= spec.low && hz < spec.high)
                sum += power[b];
        }
        const double average = blocks > 0 ? sum / blocks / kHannEnbw : 0.0;
        m.bands.push_back({spec.name, spec.low, spec.high, average > 1e-12 ? 10.0 * std::log10(average) : kMinusInf});
    }
    return m;
}

std::vector<std::uint8_t> wav16(const std::vector<std::vector<float>>& channels, double sampleRate)
{
    const auto numChannels = static_cast<std::uint16_t>(channels.size());
    const size_t frames = channels.empty() ? 0 : channels[0].size();
    const auto dataBytes = static_cast<std::uint32_t>(frames * numChannels * 2);
    const auto rate = static_cast<std::uint32_t>(std::lround(sampleRate));
    std::vector<std::uint8_t> out;
    out.reserve(44 + dataBytes);
    putTag(out, "RIFF");
    put32(out, 36 + dataBytes);
    putTag(out, "WAVE");
    putTag(out, "fmt ");
    put32(out, 16);
    put16(out, 1); // PCM
    put16(out, numChannels);
    put32(out, rate);
    put32(out, rate * numChannels * 2u);
    put16(out, static_cast<std::uint16_t>(numChannels * 2));
    put16(out, 16);
    putTag(out, "data");
    put32(out, dataBytes);
    for (size_t i = 0; i < frames; ++i)
        for (const auto& c : channels)
        {
            const auto v = static_cast<std::int16_t>(std::lround(std::clamp(static_cast<double>(c[i]), -1.0, 1.0) * 32767.0));
            put16(out, static_cast<std::uint16_t>(v));
        }
    return out;
}
} // namespace fd::model
