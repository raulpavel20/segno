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

/// Runs `work(i)` for i in [0, count) on every core; false once any returns false.
template <typename Work> bool forEach(size_t count, Work work)
{
    std::atomic<size_t> next{0};
    std::atomic<bool> failed{false};
    const auto run = [&]
    {
        for (size_t i = next++; i < count && !failed; i = next++)
            if (!work(i))
                failed = true;
    };
    std::vector<std::thread> pool;
    for (unsigned t = 1; t < std::max(1u, std::thread::hardware_concurrency()); ++t)
        pool.emplace_back(run);
    run();
    for (auto& t : pool)
        t.join();
    return !failed;
}

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

int onsetOf(const audiofile::DecodedAudio& audio)
{
    float peak = 0.0f;
    for (const auto& ch : audio.channels)
        for (const float x : ch)
            peak = std::max(peak, std::abs(x));
    int onset = audio.numFrames();
    for (const auto& ch : audio.channels)
        for (int i = 0; i < onset; ++i)
            if (std::abs(ch[static_cast<size_t>(i)]) > peak * 0.01f)
            {
                onset = i;
                break;
            }
    return peak > 0.0f ? onset : 0;
}

Prepared prepare(const audiofile::DecodedAudio& audio, double maxSeconds, std::uint32_t seed, bool trimStart)
{
    if (trimStart)
        if (const int cut = std::max(0, onsetOf(audio) - static_cast<int>(0.001 * audio.sampleRate)); cut > 0)
        {
            auto from = audio;
            for (auto& ch : from.channels)
                ch.erase(ch.begin(), ch.begin() + cut);
            from.bitsPerSample = 0; // its first sample faded in: dithered
            for (auto& ch : from.channels)
            {
                const int fade =
                    std::min(static_cast<int>(ch.size()), static_cast<int>(0.001 * audio.sampleRate));
                for (int i = 0; i < fade; ++i)
                    ch[static_cast<size_t>(i)] *= static_cast<float>(i) / static_cast<float>(fade);
            }
            return prepare(from, maxSeconds, seed, false);
        }
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
                                 s.at("commit").get<std::string>(), s.at("license").get<std::string>(),
                                 s.value("raw", std::string())});
        for (const auto& i : j.at("instruments"))
        {
            Instrument inst;
            inst.sfz = i.at("sfz").get<std::string>();
            inst.source = i.at("source").get<std::string>();
            inst.maxSeconds = i.value("maxSeconds", 8.0);
            inst.pitched = i.value("pitched", true);
            if (i.contains("normalize"))
                inst.normalize = i["normalize"].get<double>();
            if (i.contains("global"))
                for (const auto& [k, v] : i["global"].items())
                    inst.global.emplace_back(k,
                                             v.is_string() ? v.get<std::string>() : number(v.get<double>()));
            for (const auto& z : i.at("zones"))
            {
                Zone zone;
                // A mix's file is its first part's.
                const auto& first = z.contains("mix") ? z["mix"].at(0) : z;
                zone.file = first.at("file").get<std::string>();
                zone.sha256 = first.at("sha256").get<std::string>();
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
                if (z.contains("mix"))
                    for (const auto& m : z["mix"])
                        zone.mix.push_back({m.at("file").get<std::string>(),
                                            m.at("sha256").get<std::string>(), m.value("gain", 0.0),
                                            m.value("pan", 0.0)});
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

std::vector<SourceFile> sourceFiles(const Recipe& recipe)
{
    std::vector<SourceFile> out;
    std::set<std::pair<std::string, std::string>> seen;
    const auto add = [&](const Source* source, const std::string& file, const std::string& sha256)
    {
        if (seen.insert({source->id, file}).second)
            out.push_back({source, file, sha256});
    };
    for (const auto& inst : recipe.instruments)
        for (const auto& z : inst.zones)
        {
            if (z.mix.empty())
                add(recipe.source(inst.source), z.file, z.sha256);
            for (const auto& m : z.mix)
                add(recipe.source(inst.source), m.file, m.sha256);
        }
    return out;
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

std::optional<audiofile::DecodedAudio> zoneAudio(const Zone& zone, const Source& source,
                                                 const fs::path& cache, double gain, std::string* error)
{
    if (zone.mix.empty())
    {
        auto audio = audiofile::decode(sourceFile(cache, source, zone.file).string(), error);
        if (audio && gain != 1.0)
        {
            for (auto& ch : audio->channels)
                for (auto& x : ch)
                    x = static_cast<float>(x * gain);
            audio->bitsPerSample = 0;
        }
        return audio;
    }
    std::vector<audiofile::DecodedAudio> parts;
    bool stereo = false;
    for (const auto& part : zone.mix)
    {
        auto audio = audiofile::decode(sourceFile(cache, source, part.file).string(), error);
        if (!audio)
            return std::nullopt;
        if (!parts.empty() && audio->sampleRate != parts.front().sampleRate)
        {
            if (error != nullptr)
                *error = part.file + ": another sample rate than " + zone.mix.front().file;
            return std::nullopt;
        }
        stereo = stereo || audio->numChannels() > 1 || part.pan != 0.0;
        parts.push_back(std::move(*audio));
    }
    audiofile::DecodedAudio out;
    out.sampleRate = parts.front().sampleRate;
    int frames = 0;
    for (const auto& p : parts)
        frames = std::max(frames, p.numFrames());
    out.channels.assign(stereo ? 2 : 1, std::vector<float>(static_cast<size_t>(frames), 0.0f));
    for (size_t k = 0; k < parts.size(); ++k)
    {
        const auto& p = parts[k];
        const double level = gain * std::pow(10.0, zone.mix[k].gainDb / 20.0);
        const double pan = std::clamp(zone.mix[k].pan, -1.0, 1.0);
        // A balance: the side it leans away from comes down, the centre keeps both at full level.
        const double left = level * (pan > 0.0 ? 1.0 - pan : 1.0),
                     right = level * (pan < 0.0 ? 1.0 + pan : 1.0);
        for (size_t c = 0; c < out.channels.size(); ++c)
        {
            const auto& from = p.channels[std::min(c, p.channels.size() - 1)];
            const double g = out.channels.size() == 1 ? level : c == 0 ? left : right;
            auto& to = out.channels[c];
            for (size_t i = 0; i < from.size(); ++i)
                to[i] += static_cast<float>(from[i] * g);
        }
    }
    return out; // bitsPerSample 0: dithered
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
    std::mutex mutex;

    // A zone made the same way: its files, its mix, its instrument's cap and the maker.
    const auto zoneStamp = [](const Instrument& inst, const Zone& z)
    {
        std::string stamp = z.sha256;
        for (const auto& m : z.mix)
            stamp += " " + m.sha256 + "@" + number(m.gainDb) + "/" + number(m.pan);
        return stamp + " " + number(inst.maxSeconds) + (inst.normalize ? " onset" : "") + " " + kMaker;
    };

    // Instruments levelled as one (drums): their gain, found from their zones' loudest peak once,
    // kept until any of their zones changes.
    std::map<const Instrument*, double> gains;
    {
        struct Level
        {
            const Instrument* instrument;
            std::string key, stamp;
        };
        std::vector<Level> levels;
        for (const auto& inst : recipe.instruments)
        {
            if (!inst.normalize)
            {
                gains[&inst] = 1.0;
                continue;
            }
            std::string stamp = number(*inst.normalize);
            for (const auto& z : inst.zones)
                stamp += "|" + zoneStamp(inst, z);
            const auto key = "level:" + inst.sfz;
            if (made.contains(key) && made[key].is_object() && made[key].value("stamp", "") == stamp)
                gains[&inst] = made[key].value("gain", 1.0);
            else
                levels.push_back({&inst, key, stamp});
        }
        // Every zone of those still to level, decoded and mixed, for its peak.
        struct Peak
        {
            size_t level;
            const Zone* zone;
        };
        std::vector<Peak> peaks;
        for (size_t l = 0; l < levels.size(); ++l)
            for (const auto& z : levels[l].instrument->zones)
                peaks.push_back({l, &z});
        std::vector<float> loudest(levels.size(), 0.0f);
        const bool read = forEach(peaks.size(),
                                  [&](size_t i)
                                  {
                                      const auto& [l, zone] = peaks[i];
                                      const auto& inst = *levels[l].instrument;
                                      std::string error;
                                      const auto audio =
                                          zoneAudio(*zone, *recipe.source(inst.source), cache, 1.0, &error);
                                      const std::scoped_lock lock(mutex);
                                      if (!audio)
                                      {
                                          log << "error: " << zone->file << ": " << error << "\n";
                                          return false;
                                      }
                                      for (const auto& ch : audio->channels)
                                          for (const float x : ch)
                                              loudest[l] = std::max(loudest[l], std::abs(x));
                                      return true;
                                  });
        if (!read)
            return false;
        for (size_t l = 0; l < levels.size(); ++l)
        {
            const double gain = loudest[l] > 0.0f
                                    ? std::pow(10.0, *levels[l].instrument->normalize / 20.0) / loudest[l]
                                    : 1.0;
            gains[levels[l].instrument] = gain;
            made[levels[l].key] = {{"stamp", levels[l].stamp}, {"gain", gain}};
        }
    }

    struct Job
    {
        const Instrument* instrument;
        const Zone* zone;
        fs::path to;
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
            auto stamp = zoneStamp(inst, z);
            if (inst.normalize)
                stamp += " x" + number(gains[&inst]);
            const auto key = fs::relative(to, library).generic_string();
            if (made.contains(key) && made[key] == stamp && fs::exists(to))
                continue;
            jobs.push_back({&inst, &z, to, stamp});
        }
        std::ofstream(library / inst.sfz) << sfzText(inst, source);
    }

    log << jobs.size() << " samples to make\n";
    const bool ok =
        forEach(jobs.size(),
                [&](size_t i)
                {
                    const auto& job = jobs[i];
                    std::string error;
                    const auto audio = zoneAudio(*job.zone, *recipe.source(job.instrument->source), cache,
                                                 gains[job.instrument], &error);
                    if (!audio)
                    {
                        const std::scoped_lock lock(mutex);
                        log << "error: " << job.zone->file << ": " << error << "\n";
                        return false;
                    }
                    std::string warning;
                    if (job.instrument->pitched)
                        if (const auto pitch = detectPitch(*audio))
                        {
                            const double expected = job.zone->root - job.zone->tune / 100.0;
                            if (std::abs(*pitch - expected) > 0.75)
                                warning = "sounds at " + number(std::round(*pitch * 10.0) / 10.0) +
                                          ", mapped at " + std::to_string(job.zone->root);
                        }
                    const auto prepared = prepare(*audio, job.instrument->maxSeconds, seedOf(job.zone->file),
                                                  job.instrument->normalize.has_value());
                    if (!writeFlac(job.to, prepared, &error))
                    {
                        const std::scoped_lock lock(mutex);
                        log << "error: " << job.to.string() << ": " << error << "\n";
                        return false;
                    }
                    const std::scoped_lock lock(mutex);
                    made[fs::relative(job.to, library).generic_string()] = job.stamp;
                    if (!warning.empty())
                        log << "warning: " << job.zone->file << " " << warning << "\n";
                    return true;
                });
    std::ofstream(madeFile) << made.dump(1) << "\n";
    if (!ok)
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
