// fd-render: render any registered device offline. JUCE-free (ADR 0006).
//
//   fd-render --list
//   fd-render --device utility --input sweep --set gain=-6 --out out.wav --metrics out.json
//   fd-render --export-test-signals dir/
//
// Inputs: sweep | noise | sine:<hz> | impulse | silence | <file.wav>

#include "fd/device/Registry.h"
#include "fd/device/State.h"
#include "fd/testkit/DeviceRender.h"
#include "fd/testkit/Metrics.h"
#include "fd/testkit/Signals.h"
#include "fd/testkit/WavIO.h"

#include <nlohmann/json.hpp>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

using namespace fd;
using namespace fd::testkit;

namespace
{
struct Args
{
    bool list = false;
    std::string device, statePath, input = "sweep", outPath, metricsPath, exportDir;
    std::vector<std::pair<std::string, std::string>> sets;
    double sampleRate = 48000.0, durationSeconds = 2.0;
    int blockSize = 512;
};

void usage()
{
    std::cerr << "usage: fd-render --list\n"
                 "       fd-render --device <id> [--state file.json] [--set id=value]... [--input sweep|noise|sine:<hz>|impulse|silence|file.wav]\n"
                 "                 [--duration s] [--sr rate] [--block n] [--out file.wav] [--metrics file.json]\n"
                 "       fd-render --export-test-signals <dir> [--sr rate]\n";
}

bool parse(int argc, char** argv, Args& a)
{
    for (int i = 1; i < argc; ++i)
    {
        const std::string k = argv[i];
        auto value = [&]() -> std::string {
            if (i + 1 >= argc)
                throw std::runtime_error("missing value for " + k);
            return argv[++i];
        };
        if (k == "--list")
            a.list = true;
        else if (k == "--device")
            a.device = value();
        else if (k == "--state")
            a.statePath = value();
        else if (k == "--input")
            a.input = value();
        else if (k == "--out")
            a.outPath = value();
        else if (k == "--metrics")
            a.metricsPath = value();
        else if (k == "--export-test-signals")
            a.exportDir = value();
        else if (k == "--sr")
            a.sampleRate = std::stod(value());
        else if (k == "--duration")
            a.durationSeconds = std::stod(value());
        else if (k == "--block")
            a.blockSize = std::stoi(value());
        else if (k == "--set")
        {
            const std::string kv = value();
            const auto eq = kv.find('=');
            if (eq == std::string::npos)
                throw std::runtime_error("--set expects id=value");
            a.sets.emplace_back(kv.substr(0, eq), kv.substr(eq + 1));
        }
        else
            throw std::runtime_error("unknown argument " + k);
    }
    return true;
}

AudioBuffer makeInput(const std::string& spec, double sr, int n, int channels)
{
    if (spec == "sweep")
        return logSweep(20.0, 20000.0, sr, n, 0.5f, channels);
    if (spec == "noise")
        return whiteNoise(n, 1, 0.25f, channels);
    if (spec == "impulse")
        return impulse(n, channels);
    if (spec == "silence")
        return silence(n, channels);
    if (spec.rfind("sine:", 0) == 0)
        return sine(std::stod(spec.substr(5)), sr, n, 0.5f, channels);
    auto wav = readWav(spec);
    if (!wav)
        throw std::runtime_error("cannot read input '" + spec + "'");
    if (wav->sampleRate != sr)
        std::cerr << "warning: input sample rate " << wav->sampleRate << " differs from --sr " << sr << "\n";
    return wav->buffer;
}

int exportTestSignals(const std::string& dir, double sr)
{
    namespace fs = std::filesystem;
    fs::create_directories(dir);
    const int n = static_cast<int>(sr * 5);
    const std::vector<std::pair<std::string, AudioBuffer>> signals{
        {"sweep-20-20k.wav", logSweep(20.0, 20000.0, sr, n, 0.5f)},
        {"noise-white.wav", whiteNoise(n, 1)},
        {"sine-1k.wav", sine(1000.0, sr, n)},
        {"sine-100.wav", sine(100.0, sr, n)},
        {"impulse.wav", impulse(static_cast<int>(sr))},
        {"stereo-440-660.wav", stereoSines(440.0, 660.0, sr, n)},
    };
    for (const auto& [name, buffer] : signals)
    {
        const auto path = (fs::path(dir) / name).string();
        if (!writeWav(path, buffer, sr))
        {
            std::cerr << "error: cannot write " << path << "\n";
            return 1;
        }
        std::cout << path << "\n";
    }
    return 0;
}
} // namespace

int main(int argc, char** argv)
{
    Args a;
    try
    {
        parse(argc, argv, a);
    }
    catch (const std::exception& e)
    {
        std::cerr << "error: " << e.what() << "\n";
        usage();
        return 2;
    }

    if (a.list)
    {
        for (const auto* f : allDevices())
            std::cout << f->info().id << "\t" << f->info().name << "\t(" << f->info().params.size() << " params)\n";
        return 0;
    }
    if (!a.exportDir.empty())
        return exportTestSignals(a.exportDir, a.sampleRate);
    if (a.device.empty())
    {
        usage();
        return 2;
    }

    const auto* factory = findDevice(a.device);
    if (factory == nullptr)
    {
        std::cerr << "error: unknown device '" << a.device << "' (try --list)\n";
        return 2;
    }
    auto device = factory->create();

    if (!a.statePath.empty())
    {
        std::ifstream in(a.statePath);
        std::stringstream ss;
        ss << in.rdbuf();
        if (deserializeState(*device, ss.str()) != ApplyResult::Ok)
        {
            std::cerr << "error: cannot apply state from " << a.statePath << "\n";
            return 1;
        }
    }
    for (const auto& [id, text] : a.sets)
    {
        const auto index = device->params().indexOf(id);
        if (!index)
        {
            std::cerr << "error: device '" << a.device << "' has no parameter '" << id << "'\n";
            return 2;
        }
        const auto value = device->params().spec(*index).fromText(text);
        if (!value)
        {
            std::cerr << "error: cannot parse '" << text << "' for parameter '" << id << "'\n";
            return 2;
        }
        device->params().set(*index, *value);
    }

    const auto& info = device->info();
    const int n = static_cast<int>(a.durationSeconds * a.sampleRate);
    AudioBuffer input;
    try
    {
        input = makeInput(a.input, a.sampleRate, n, std::max(1, info.buses.inputChannels));
    }
    catch (const std::exception& e)
    {
        std::cerr << "error: " << e.what() << "\n";
        return 1;
    }

    RenderOptions opts;
    opts.sampleRate = a.sampleRate;
    opts.blockPattern = {a.blockSize};
    opts.tailSamples = static_cast<int>(device->tailSeconds() * a.sampleRate);
    const auto out = render(*device, input, opts);

    if (!a.outPath.empty() && !writeWav(a.outPath, out, a.sampleRate))
    {
        std::cerr << "error: cannot write " << a.outPath << "\n";
        return 1;
    }

    nlohmann::json m;
    m["device"] = std::string(info.id);
    m["state"] = toJson(captureState(*device));
    m["sampleRate"] = a.sampleRate;
    m["samples"] = out.numSamples();
    m["finite"] = allFinite(out);
    m["latencyReported"] = device->latencySamples();
    m["latencyMeasured"] = estimateLatency(input, out, 8192);
    for (int c = 0; c < out.numChannels(); ++c)
    {
        auto& ch = m["channels"][c];
        ch["peakDb"] = peakDb(out, c);
        ch["rmsDb"] = rmsDb(out, c);
        ch["dc"] = dcOffset(out, c);
        ch["clickScore"] = clickScore(out, c);
    }
    const std::string text = m.dump(2);
    if (!a.metricsPath.empty())
        std::ofstream(a.metricsPath) << text << "\n";
    else
        std::cout << text << "\n";
    return 0;
}
