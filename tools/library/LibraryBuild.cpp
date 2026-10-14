#include "LibraryBuild.h"

#include <FLAC/stream_encoder.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <fstream>
#include <map>
#include <mutex>
#include <nlohmann/json.hpp>
#include <numbers>
#include <ostream>
#include <set>
#include <sstream>
#include <thread>

namespace fd::library_build
{
namespace
{
namespace fs = std::filesystem;

/// Bumped when what the tool makes of a sample changes, so every sample is made again.
constexpr const char* kMaker = "fd-library 1";

/// FNV-1a: the dither's seed from a file's name, the same on every machine.
std::uint32_t seedOf(const std::string& text)
{
    std::uint32_t h = 2166136261u;
    for (const unsigned char c : text)
        h = (h ^ c) * 16777619u;
    return h == 0 ? 1u : h;
}

std::string number(double v)
{
    std::ostringstream s;
    s << v;
    return s.str();
}
} // namespace

// ---- a sample ------------------------------------------------------------------------------

Prepared prepare(const audiofile::DecodedAudio& audio, double maxSeconds, std::uint32_t seed)
{
    Prepared p;
    p.channels = std::min(2, audio.numChannels());
    p.sampleRate = audio.sampleRate;
    const int frames = audio.numFrames();
    float peak = 0.0f;
    for (int c = 0; c < p.channels; ++c)
        for (const float x : audio.channels[static_cast<size_t>(c)])
            peak = std::max(peak, std::abs(x));
    if (frames == 0 || peak <= 0.0f)
        return p;

    // The tail: cut 20 ms after the last sample above -60 dB of the peak.
    const float floor = peak * 0.001f;
    int last = 0;
    for (int c = 0; c < p.channels; ++c)
    {
        const auto& ch = audio.channels[static_cast<size_t>(c)];
        for (int i = frames - 1; i > last; --i)
            if (std::abs(ch[static_cast<size_t>(i)]) > floor)
            {
                last = i;
                break;
            }
    }
    int end = std::min(frames, last + 1 + static_cast<int>(0.02 * p.sampleRate));
    int fade = std::min(end, static_cast<int>(0.01 * p.sampleRate));
    if (const int cap = static_cast<int>(maxSeconds * p.sampleRate); end > cap)
    {
        end = cap;
        fade = std::min(end, static_cast<int>(0.5 * p.sampleRate));
        p.capped = true;
    }
    p.frames = end;

    // 16-bit: a 16-bit file's own samples exactly; the rest (and every faded sample) with TPDF dither.
    const bool exact = audio.bitsPerSample > 0 && audio.bitsPerSample <= 16;
    std::uint32_t state = seed;
    const auto uniform = [&state]
    {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return static_cast<double>(state) / 4294967296.0;
    };
    p.interleaved.resize(static_cast<size_t>(end) * static_cast<size_t>(p.channels));
    for (int i = 0; i < end; ++i)
    {
        double gain = 1.0;
        if (const int into = i - (end - fade); into >= 0)
            gain = 0.5 * (1.0 + std::cos(std::numbers::pi * (into + 1) / fade)); // to 0 at the end
        for (int c = 0; c < p.channels; ++c)
        {
            const double v = audio.channels[static_cast<size_t>(c)][static_cast<size_t>(i)] * gain * 32768.0;
            const double dither = exact && gain == 1.0 ? 0.0 : uniform() - uniform();
            p.interleaved[static_cast<size_t>(i) * static_cast<size_t>(p.channels) + static_cast<size_t>(c)] =
                static_cast<std::int16_t>(std::clamp(std::lround(v + dither), -32768L, 32767L));
        }
    }
    return p;
}

bool writeFlac(const fs::path& path, const Prepared& p, std::string* error)
{
    auto* encoder = FLAC__stream_encoder_new();
    if (encoder == nullptr)
    {
        if (error != nullptr)
            *error = "libFLAC couldn't make an encoder";
        return false;
    }
    bool ok =
        FLAC__stream_encoder_set_verify(encoder, true) &&
        FLAC__stream_encoder_set_compression_level(encoder, 8) &&
        FLAC__stream_encoder_set_channels(encoder, static_cast<unsigned>(p.channels)) &&
        FLAC__stream_encoder_set_bits_per_sample(encoder, 16) &&
        FLAC__stream_encoder_set_sample_rate(encoder, static_cast<unsigned>(std::lround(p.sampleRate))) &&
        FLAC__stream_encoder_set_total_samples_estimate(encoder, static_cast<FLAC__uint64>(p.frames));
    ok = ok && FLAC__stream_encoder_init_file(encoder, path.string().c_str(), nullptr, nullptr) ==
                   FLAC__STREAM_ENCODER_INIT_STATUS_OK;
    constexpr int block = 4096;
    std::vector<FLAC__int32> buffer;
    for (int at = 0; ok && at < p.frames; at += block)
    {
        const int n = std::min(block, p.frames - at);
        const auto from = p.interleaved.begin() + static_cast<std::ptrdiff_t>(at) * p.channels;
        buffer.assign(from, from + static_cast<std::ptrdiff_t>(n) * p.channels);
        ok = FLAC__stream_encoder_process_interleaved(encoder, buffer.data(), static_cast<unsigned>(n));
    }
    if (!FLAC__stream_encoder_finish(encoder))
        ok = false;
    if (!ok && error != nullptr)
        *error = std::string("libFLAC: ") +
                 FLAC__StreamEncoderStateString[FLAC__stream_encoder_get_state(encoder)];
    FLAC__stream_encoder_delete(encoder);
    return ok;
}

std::optional<double> detectPitch(const audiofile::DecodedAudio& audio)
{
    // YIN (de Cheveigné and Kawahara) on the mono mix, in three windows of the steady part; the
    // median wins.
    const int frames = audio.numFrames();
    const double rate = audio.sampleRate;
    if (frames == 0 || rate <= 0.0)
        return std::nullopt;
    std::vector<float> mono(static_cast<size_t>(frames), 0.0f);
    for (const auto& ch : audio.channels)
        for (int i = 0; i < frames; ++i)
            mono[static_cast<size_t>(i)] += ch[static_cast<size_t>(i)];
    const int tauMin = std::max(2, static_cast<int>(rate / 4500.0));
    const int tauMax = static_cast<int>(rate / 25.0);
    const int window = 2 * tauMax;
    std::vector<double> found;
    for (const double at : {0.15, 0.3, 0.45})
    {
        const int start = static_cast<int>(at * rate);
        if (start + window + tauMax >= frames)
            break;
        std::vector<double> d(static_cast<size_t>(tauMax + 1), 0.0);
        for (int tau = 1; tau <= tauMax; ++tau)
        {
            double sum = 0.0;
            for (int i = 0; i < window; ++i)
            {
                const double diff =
                    mono[static_cast<size_t>(start + i)] - mono[static_cast<size_t>(start + i + tau)];
                sum += diff * diff;
            }
            d[static_cast<size_t>(tau)] = sum;
        }
        // Cumulative mean normalised difference; the first dip under the threshold.
        std::vector<double> cmnd(static_cast<size_t>(tauMax + 1), 1.0);
        double running = 0.0;
        for (int tau = 1; tau <= tauMax; ++tau)
        {
            running += d[static_cast<size_t>(tau)];
            cmnd[static_cast<size_t>(tau)] =
                running > 0.0 ? d[static_cast<size_t>(tau)] * tau / running : 1.0;
        }
        int best = -1;
        for (int tau = tauMin; tau < tauMax; ++tau)
            if (cmnd[static_cast<size_t>(tau)] < 0.15)
            {
                while (tau + 1 < tauMax &&
                       cmnd[static_cast<size_t>(tau + 1)] < cmnd[static_cast<size_t>(tau)])
                    ++tau;
                best = tau;
                break;
            }
        if (best < 0)
            continue;
        // Between samples: a parabola through the dip.
        const double a = cmnd[static_cast<size_t>(best - 1)], b = cmnd[static_cast<size_t>(best)],
                     c = cmnd[static_cast<size_t>(best + 1)];
        const double shift = (a - 2.0 * b + c) != 0.0 ? 0.5 * (a - c) / (a - 2.0 * b + c) : 0.0;
        const double hz = rate / (best + shift);
        found.push_back(69.0 + 12.0 * std::log2(hz / 440.0));
    }
    if (found.empty())
        return std::nullopt;
    std::sort(found.begin(), found.end());
    return found[found.size() / 2];
}

// ---- the recipe ------------------------------------------------------------------------------

const Source* Recipe::source(const std::string& id) const
{
    for (const auto& s : sources)
        if (s.id == id)
            return &s;
    return nullptr;
}

std::optional<Recipe> readRecipe(const fs::path& path, std::string* error)
{
    const auto fail = [error](std::string why) -> std::optional<Recipe>
    {
        if (error != nullptr)
            *error = std::move(why);
        return std::nullopt;
    };
    std::ifstream in(path);
    if (!in)
        return fail("cannot open " + path.string());
    const auto j = nlohmann::json::parse(in, nullptr, false);
    if (!j.is_object() || j.value("segnoLibraryRecipe", 0) != 1)
        return fail(path.string() + " isn't a Library recipe");
    try
    {
        Recipe r;
        r.version = j.at("version").get<std::string>();
        for (const auto& [id, s] : j.at("sources").items())
            r.sources.push_back({id, s.at("name").get<std::string>(), s.at("url").get<std::string>(),
                                 s.at("commit").get<std::string>(), s.at("license").get<std::string>()});
        for (const auto& i : j.at("instruments"))
        {
            Instrument inst;
            inst.sfz = i.at("sfz").get<std::string>();
            inst.source = i.at("source").get<std::string>();
            inst.maxSeconds = i.value("maxSeconds", 8.0);
            inst.pitched = i.value("pitched", true);
            if (i.contains("global"))
                for (const auto& [k, v] : i["global"].items())
                    inst.global.emplace_back(k,
                                             v.is_string() ? v.get<std::string>() : number(v.get<double>()));
            for (const auto& z : i.at("zones"))
            {
                Zone zone;
                zone.file = z.at("file").get<std::string>();
                zone.sha256 = z.at("sha256").get<std::string>();
                zone.name = z.value("name", std::string());
                zone.loKey = z.value("lokey", 0);
                zone.hiKey = z.value("hikey", 127);
                zone.root = z.value("root", 60);
                zone.loVel = z.value("lovel", 0);
                zone.hiVel = z.value("hivel", 127);
                zone.volume = z.value("volume", 0.0);
                zone.tune = z.value("tune", 0.0);
                if (z.contains("seq"))
                {
                    zone.seqLength = z["seq"].at(0).get<int>();
                    zone.seqPosition = z["seq"].at(1).get<int>();
                }
                if (z.contains("rand"))
                {
                    zone.loRand = z["rand"].at(0).get<double>();
                    zone.hiRand = z["rand"].at(1).get<double>();
                }
                inst.zones.push_back(std::move(zone));
            }
            if (r.source(inst.source) == nullptr)
                return fail(inst.sfz + ": no source " + inst.source);
            r.instruments.push_back(std::move(inst));
        }
        return r;
    }
    catch (const nlohmann::json::exception& e)
    {
        return fail(path.string() + ": " + e.what());
    }
}

fs::path sourceFile(const fs::path& cache, const Source& source, const std::string& file)
{
    return cache / source.id / source.commit / fs::path(file);
}

std::string sampleName(const Zone& zone)
{
    return (zone.name.empty() ? fs::path(zone.file).stem().string() : zone.name) + ".flac";
}

std::string sfzText(const Instrument& instrument, const Source& source)
{
    std::ostringstream s;
    s << "// " << fs::path(instrument.sfz).stem().string()
      << ", in Segno's Library: made by tools/library from " << source.name << " (" << source.url << ", "
      << source.commit.substr(0, 8) << ", " << source.license
      << ").\n// Made from tools/library/recipe.json: change that, not this.\n\n";
    s << "<control>\ndefault_path=samples/\n\n";
    if (!instrument.global.empty())
    {
        s << "<global>";
        for (const auto& [k, v] : instrument.global)
            s << " " << k << "=" << v;
        s << "\n\n";
    }
    for (const auto& z : instrument.zones)
    {
        s << "<region> sample=" << sampleName(z) << " lokey=" << z.loKey << " hikey=" << z.hiKey
          << " pitch_keycenter=" << z.root << " lovel=" << z.loVel << " hivel=" << z.hiVel;
        if (z.volume != 0.0)
            s << " volume=" << number(z.volume);
        if (z.tune != 0.0)
            s << " tune=" << number(z.tune);
        if (z.seqLength > 1)
            s << " seq_length=" << z.seqLength << " seq_position=" << z.seqPosition;
        if (z.loRand > 0.0 || z.hiRand < 1.0)
            s << " lorand=" << number(z.loRand) << " hirand=" << number(z.hiRand);
        s << "\n";
    }
    return s.str();
}

// ---- building --------------------------------------------------------------------------------

bool buildInstruments(const Recipe& recipe, const fs::path& cache, const fs::path& library, std::ostream& log)
{
    // What was made from what: a sample made from the same file, the same way, isn't made again.
    const auto madeFile = library / ".made.json";
    nlohmann::json made = nlohmann::json::object();
    if (std::ifstream in(madeFile); in)
        made = nlohmann::json::parse(in, nullptr, false);
    if (!made.is_object())
        made = nlohmann::json::object();

    struct Job
    {
        const Instrument* instrument;
        const Zone* zone;
        fs::path from, to;
        std::string stamp;
    };
    std::vector<Job> jobs;
    std::set<fs::path> kept;
    std::error_code ec;
    for (const auto& inst : recipe.instruments)
    {
        const auto& source = *recipe.source(inst.source);
        const auto folder = library / fs::path(inst.sfz).parent_path();
        fs::create_directories(folder / "samples", ec);
        std::set<std::string> names;
        for (const auto& z : inst.zones)
        {
            const auto to = folder / "samples" / sampleName(z);
            if (!names.insert(sampleName(z)).second)
                continue; // a file two zones play
            kept.insert(to);
            const auto stamp = z.sha256 + " " + number(inst.maxSeconds) + " " + kMaker;
            const auto key = fs::relative(to, library).generic_string();
            if (made.contains(key) && made[key] == stamp && fs::exists(to))
                continue;
            jobs.push_back({&inst, &z, sourceFile(cache, source, z.file), to, stamp});
        }
        std::ofstream(library / inst.sfz) << sfzText(inst, source);
    }

    log << jobs.size() << " samples to make\n";
    std::mutex mutex;
    std::atomic<size_t> next{0};
    std::atomic<bool> failed{false};
    const auto work = [&]
    {
        for (size_t i = next++; i < jobs.size() && !failed; i = next++)
        {
            const auto& job = jobs[i];
            std::string error;
            const auto audio = audiofile::decode(job.from.string(), &error);
            if (!audio)
            {
                const std::scoped_lock lock(mutex);
                log << "error: " << job.from.string() << ": " << error << "\n";
                failed = true;
                return;
            }
            std::string warning;
            if (job.instrument->pitched)
                if (const auto pitch = detectPitch(*audio))
                {
                    const double expected = job.zone->root - job.zone->tune / 100.0;
                    if (std::abs(*pitch - expected) > 0.75)
                        warning = "sounds at " + number(std::round(*pitch * 10.0) / 10.0) + ", mapped at " +
                                  std::to_string(job.zone->root);
                }
            const auto prepared = prepare(*audio, job.instrument->maxSeconds, seedOf(job.zone->file));
            if (!writeFlac(job.to, prepared, &error))
            {
                const std::scoped_lock lock(mutex);
                log << "error: " << job.to.string() << ": " << error << "\n";
                failed = true;
                return;
            }
            const std::scoped_lock lock(mutex);
            made[fs::relative(job.to, library).generic_string()] = job.stamp;
            if (!warning.empty())
                log << "warning: " << job.zone->file << " " << warning << "\n";
        }
    };
    std::vector<std::thread> pool;
    for (unsigned t = 1; t < std::max(1u, std::thread::hardware_concurrency()); ++t)
        pool.emplace_back(work);
    work();
    for (auto& t : pool)
        t.join();
    std::ofstream(madeFile) << made.dump(1) << "\n";
    if (failed)
        return false;

    // What the recipe no longer makes goes: samples and maps.
    for (const auto& inst : recipe.instruments)
        kept.insert(library / inst.sfz);
    std::vector<fs::path> gone;
    for (const auto& entry : fs::recursive_directory_iterator(library / "Samples", ec))
        if (entry.is_regular_file() &&
            (entry.path().extension() == ".flac" || entry.path().extension() == ".sfz") &&
            !kept.contains(entry.path()))
            gone.push_back(entry.path());
    for (const auto& path : gone)
    {
        made.erase(fs::relative(path, library).generic_string());
        fs::remove(path, ec);
    }
    std::ofstream(madeFile) << made.dump(1) << "\n";
    return true;
}
} // namespace fd::library_build
