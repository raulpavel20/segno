// Long work (mcp.md §5): rendering and measuring a range, bouncing clips (bounce.md §11), and
// scanning for plugins. These start on the message thread and are waited for on the transport's
// thread, with progress.

#include "mcp/tools/Tools.h"

#include "mcp/protocol/Encoding.h"
#include "model/AudioMeasure.h"

#include <fd/audiofile/AudioFile.h>

#include <fd/device/Device.h>

#include <chrono>
#include <cmath>
#include <condition_variable>
#include <mutex>
#include <thread>

namespace fd::mcp
{
using namespace tools;

namespace
{
constexpr double kMaxRenderSeconds = 600.0;
constexpr double kMaxAudioSeconds = 30.0; // open question B19

struct Waiter
{
    std::mutex mutex;
    std::condition_variable changed;
    bool done = false;
    std::string error;
    int found = 0, skipped = 0;
    std::string current;

    void finish(std::string e = {}, int n = 0)
    {
        {
            std::lock_guard lock(mutex);
            done = true;
            error = std::move(e);
            found = n;
        }
        changed.notify_all();
    }
};

Json measurementsJson(const model::AudioMeasurements& m)
{
    Json bands = Json::array();
    for (const auto& b : m.bands)
        bands.push_back({{"band", b.name}, {"from", b.low}, {"to", b.high}, {"level", db(b.levelDb)}});
    Json peaks = Json::array(), rms = Json::array();
    for (auto v : m.channelPeakDb)
        peaks.push_back(db(v));
    for (auto v : m.channelRmsDb)
        rms.push_back(db(v));
    return {{"seconds", roundTime(m.seconds)},
            {"sampleRate", m.sampleRate},
            {"silent", m.silent},
            {"peak", db(m.peakDb)},
            {"truePeak", db(m.truePeakDb)},
            {"rms", db(m.rmsDb)},
            {"loudness",
             {{"integrated", db(m.integratedLufs)},
              {"shortTermMax", db(m.shortTermMaxLufs)},
              {"momentaryMax", db(m.momentaryMaxLufs)},
              {"range", m.loudnessRange ? Json(std::round(*m.loudnessRange * 10.0) / 10.0) : Json(nullptr)}}},
            {"crest", db(m.crestDb)},
            {"clippedSamples", m.clippedSamples},
            {"correlation", m.correlation ? Json(std::round(*m.correlation * 100.0) / 100.0) : Json(nullptr)},
            {"channels", {{"peak", peaks}, {"rms", rms}}},
            {"spectrum", bands}};
}

std::string describe(const model::AudioMeasurements& m)
{
    if (m.silent)
        return "The range is silent.";
    const auto f = [](double v) { return juce::String(v, 1).toStdString(); };
    std::string s = "Peak " + f(m.peakDb) + " dBFS (true peak " + f(m.truePeakDb) + " dBTP), RMS " + f(m.rmsDb) + " dBFS";
    if (std::isfinite(m.integratedLufs))
        s += ", " + f(m.integratedLufs) + " LUFS integrated";
    if (m.clippedSamples > 0)
        s += ", " + std::to_string(m.clippedSamples) + " clipped samples";
    return s + ".";
}
} // namespace

void addRenderTools(Registry& registry, ToolEnv& env)
{
    Tool render;
    render.name = "render";
    render.title = "Render and measure";
    render.description =
        "Renders a range of the song (the mix, or one track through its chain) offline, without touching the "
        "project or playback, and measures it: peak and true peak, RMS, loudness (LUFS integrated, loudest 3 "
        "s and "
        "400 ms, and the loudness range in LU), crest factor, clipped samples, stereo correlation, and the "
        "level in seven frequency bands. With "
        "\"audio\": true, also returns the audio (16-bit WAV) when the range is 30 seconds or shorter. "
        "Ranges up to "
        "10 minutes.";
    render.inputSchema = object({{"start", position("Where to start.")},
                                 {"end", position("Where to stop.")},
                                 {"what", id(IdKind::Track, "A track, or \"mix\" (the default) for the whole song")},
                                 {"audio", boolean("Also return the audio (30 s at most)")}},
                                {"start", "end"});
    render.outputSchema = result(
        {{"what", string("\"mix\" or the track")},
         {"start", positionResult()},
         {"end", positionResult()},
         {"peak", {{"type", {"number", "null"}}, {"description", "dBFS; null when silent"}}},
         {"truePeak", {{"type", {"number", "null"}}, {"description", "dBTP"}}},
         {"loudness", result({{"integrated", {{"type", {"number", "null"}}, {"description", "LUFS"}}},
                              {"shortTermMax", {{"type", {"number", "null"}}, {"description", "LUFS"}}},
                              {"momentaryMax", {{"type", {"number", "null"}}, {"description", "LUFS"}}},
                              {"range",
                               {{"type", {"number", "null"}},
                                {"description", "LU: how much the loudness moves (EBU Tech 3342)"}}}})},
         {"rms", {{"type", {"number", "null"}}, {"description", "dBFS"}}},
         {"clippedSamples", integer("")},
         {"spectrum", arrayOf(result({{"band", string("")}}), "", std::nullopt)}},
        {"what", "start", "end", "peak", "truePeak", "rms", "loudness", "clippedSamples", "spectrum"});
    render.annotations.readOnly = true;
    render.annotations.idempotent = true;
    render.call = [&env](const Json& args, CallContext& context) -> ToolResult {
        auto waiter = std::make_shared<Waiter>();
        std::shared_ptr<engine::Session::RenderJob> job;
        juce::File file;
        Json range;
        std::optional<ToolResult> refused;
        const bool ran = env.main.run([&] {
            try
            {
                env.session.flushSnapshot();
                const auto t = tempo(env);
                const double start = positionArg(env, args["start"]), end = positionArg(env, args["end"]);
                if (!(end > start))
                    throw Problem{"The end must come after the start."};
                if (t.secondsForBeats(end - start) > kMaxRenderSeconds)
                    throw Problem{"Render at most 10 minutes at a time."};
                std::optional<std::uint64_t> trackId;
                const auto what = args.value("what", Json("mix"));
                if (!(what.is_string() && what.get<std::string>() == "mix"))
                    trackId = track(env, what).id;
                range = {{"what", trackId ? formatId(IdKind::Track, *trackId) : std::string("mix")},
                         {"start", positionJson(start, t)},
                         {"end", positionJson(end, t)}};
                (void)env.scratch.createDirectory();
                file = env.scratch.getNonexistentChildFile("render", ".wav");
                job = env.session.render({t.secondsForBeats(start), t.secondsForBeats(end), trackId, 48000.0}, file,
                                         [waiter](const std::string& error) { waiter->finish(error); });
                if (job == nullptr)
                    throw Problem{"There's nothing to render there."};
            }
            catch (const Problem& p)
            {
                refused = ToolResult::error(p.message);
            }
        });
        if (!ran)
            return ToolResult::error("The DAW is closing.");
        if (refused)
            return *refused;

        for (;;)
        {
            {
                std::unique_lock lock(waiter->mutex);
                if (waiter->changed.wait_for(lock, std::chrono::milliseconds(200), [&] { return waiter->done; }))
                    break;
            }
            context.progress(job->progress(), 1.0, "Rendering");
            if (context.cancelled() || !env.main.isOpen())
                job->cancel();
            if (!env.main.isOpen())
                return ToolResult::error("The DAW is closing.");
        }
        if (!waiter->error.empty())
        {
            file.deleteFile();
            return ToolResult::error("The render didn't finish: " + waiter->error);
        }
        std::string error;
        const auto audio = audiofile::decode(file.getFullPathName().toStdString(), &error);
        file.deleteFile();
        if (!audio)
            return ToolResult::error("The rendered audio couldn't be read: " + error);
        const auto m = model::measureAudio(audio->channels, audio->sampleRate);
        Json out = range;
        out.update(measurementsJson(m));
        auto result = ToolResult::ok(std::move(out), describe(m));
        if (args.value("audio", false))
        {
            if (m.seconds <= kMaxAudioSeconds + 1e-6)
            {
                const auto wav = model::wav16(audio->channels, audio->sampleRate);
                result.content.push_back({{"type", "audio"}, {"data", base64Encode(wav)}, {"mimeType", "audio/wav"}});
            }
            else
                result.summary += " The audio isn't included: ranges longer than 30 seconds give measurements only.";
        }
        return result;
    };
    registry.tools.push_back(std::move(render));

    Tool bounce;
    bounce.name = "bounce_clips";
    bounce.title = "Bounce clips to audio";
    bounce.description =
        "Renders clips to audio offline (playback goes on) and lands them as one undo step. In place (the "
        "default): a MIDI clip through its track's modules up to and including its instrument, an audio clip as "
        "it plays; the modules after it, the mixer and sends keep running on the bounce, so the track sounds the "
        "same. The clip keeps its source as a take, \"Before bounce\" (use_take brings the MIDI back). With "
        "\"newTrack\": the clips through the track's whole chain before the fader, onto a new track right after "
        "it that copies its volume, pan, sends and the lanes driving them; the source clips are muted. A bounce "
        "runs past its clip until its tail is silent (10 s at most), never past the next clip. Refused for a MIDI "
        "clip on a track without an instrument, a muted clip, and (in place) a MIDI clip after audio modules on "
        "its track. render the track before and after: the measurements match (an instrument's own randomness, such "
        "as drift, moves them by a hair).";
    bounce.inputSchema = object({{"clips", ids(IdKind::Clip, "The clips (a clip with takes is bounced whole)")},
                                 {"newTrack", boolean("Onto a new track, through the whole chain (default false: in place)")}},
                                {"clips"});
    bounce.outputSchema = result({{"clips", arrayOf(clipResultSchema(), "The bounced clips", std::nullopt)},
                                  {"tracks", arrayOf(string("A new track's id"), "The new tracks (newTrack)", std::nullopt)},
                                  {"cutTails", arrayOf(result({{"clip", string("")}, {"next", string("")}}),
                                                       "Audible tails a clip that wasn't bounced cut", std::nullopt)}},
                                 {"clips"});
    bounce.call = [&env](const Json& args, CallContext& context) -> ToolResult {
        struct Landed
        {
            std::mutex mutex;
            std::condition_variable changed;
            std::optional<engine::Session::BounceResult> result;
        };
        auto landed = std::make_shared<Landed>();
        std::shared_ptr<engine::Session::RenderJob> job;
        std::optional<ToolResult> refused;
        const bool ran = env.main.run([&] {
            try
            {
                env.session.flushSnapshot();
                std::vector<std::uint64_t> clips;
                for (const auto& value : args["clips"])
                    clips.push_back(clip(env, value).id);
                const auto mode = args.value("newTrack", false) ? model::BounceMode::NewTrack : model::BounceMode::InPlace;
                if (const auto why = env.session.bounceRefusal(clips, mode); !why.empty())
                    throw Problem{why};
                job = env.session.bounce(
                    clips, mode,
                    [landed](const engine::Session::BounceResult& r) {
                        {
                            std::lock_guard lock(landed->mutex);
                            landed->result = r;
                        }
                        landed->changed.notify_all();
                    },
                    "Assistant: bounce clips"); // one call, one step (ADR 0021), though it lands later
                if (job == nullptr)
                    throw Problem{"There's nothing to bounce."};
            }
            catch (const Problem& p)
            {
                refused = ToolResult::error(p.message);
            }
        });
        if (!ran)
            return ToolResult::error("The DAW is closing.");
        if (refused)
            return *refused;
        for (;;)
        {
            {
                std::unique_lock lock(landed->mutex);
                if (landed->changed.wait_for(lock, std::chrono::milliseconds(200), [&] { return landed->result.has_value(); }))
                    break;
            }
            context.progress(job->progress(), 1.0, "Bouncing");
            if (context.cancelled() || !env.main.isOpen())
                job->cancel();
            if (!env.main.isOpen())
                return ToolResult::error("The DAW is closing.");
        }
        const auto r = *landed->result;
        if (r.cancelled)
            return ToolResult::error("The bounce was cancelled; nothing changed.");
        if (!r.error.empty())
            return ToolResult::error(r.error);
        Json out;
        std::string summary;
        env.main.run([&] {
            env.session.flushSnapshot();
            Json clips = Json::array();
            for (const auto id : r.clips)
                if (const auto found = model::findClip(*env.session.snapshot(), id))
                    clips.push_back(clipJson(env, *found->first, false));
            Json tracks = Json::array();
            for (const auto id : r.tracks)
                tracks.push_back(formatId(IdKind::Track, id));
            Json cut = Json::array();
            for (const auto& c : r.cutTails)
            {
                cut.push_back({{"clip", c.clip}, {"next", c.next}});
                summary += c.clip + "'s tail stops where " + c.next + " starts; bounce both to keep it. ";
            }
            out = {{"clips", std::move(clips)}, {"tracks", std::move(tracks)}, {"cutTails", std::move(cut)}};
        });
        return ToolResult::ok(std::move(out), summary);
    };
    registry.tools.push_back(std::move(bounce));

    Tool exporting;
    exporting.name = "export_audio";
    exporting.title = "Export audio files";
    exporting.description =
        "Exports the mix, or tracks as stems, of the song (bar 1 to the last clip, then the tail until "
        "silent) or "
        "the loop, as WAV, FLAC, AIFF, MP3, Ogg Vorbis or Opus, offline (playback goes on), and waits for "
        "the "
        "files; alsoAs writes a second format of each from the same render (a master and an MP3). Stems are "
        "each track "
        "(and, with all tracks, each return track) through its modules and fader, adding up to the mix; with "
        "throughReturns, each track as heard through the returns and master. Dither is automatic below "
        "32-bit "
        "float. normalise: \"off\", \"peak\" (true peak at -1 dBTP) or \"loudness\" (to target LUFS, -14 by "
        "default, never above -1 dBTP). Unset arguments use the project's export settings (the export "
        "panel's), "
        "which also say where the files go: the project's Exports folder, or the folder the user chose. "
        "Files are "
        "never overwritten. Each file is reported with its loudness, true peak and clipping; an MP3, Ogg or "
        "Opus "
        "file measured decoded, as listeners get it. Doesn't change the project.";
    exporting.inputSchema = object(
        {{"what",
          {{"type", "string"},
           {"enum", Json::array({"mix", "tracks"})},
           {"description", "The mix, or tracks as stems"}}},
         {"tracks", ids(IdKind::Track, "The tracks or return tracks as stems (default: all of them)")},
         {"throughReturns", boolean("Stems through the returns and master")},
         {"range",
          {{"type", "string"},
           {"enum", Json::array({"song", "loop"})},
           {"description", "The song, or the loop"}}},
         {"format",
          {{"type", "string"},
           {"enum", Json::array({"wav", "flac", "aiff", "mp3", "ogg", "opus"})},
           {"description", ""}}},
         {"bitDepth", integer("16, 24, or 32 (float, WAV only)", 16, 32)},
         {"quality",
          integer("A lossy format's bit rate in kbps: MP3 128, 192, 256, 320 (default 320, constant); Ogg "
                  "about 128, 192, 256, 320 (default 192); Opus 96, 128, 160, 192, 256 (default 160)",
                  96, 320)},
         {"alsoAs", object({{"format",
                             {{"type", "string"},
                              {"enum", Json::array({"wav", "flac", "aiff", "mp3", "ogg", "opus"})},
                              {"description", ""}}},
                            {"bitDepth", integer("Its bit depth (PCM)", 16, 32)},
                            {"quality", integer("Its bit rate in kbps (lossy)", 96, 320)}},
                           {"format"})},
         {"sampleRate", number("44100, 48000, 88200 or 96000 (default: the project's)")},
         {"dither", boolean("Dither below 32-bit float (default true)")},
         {"normalise",
          {{"type", "string"}, {"enum", Json::array({"off", "peak", "loudness"})}, {"description", ""}}},
         {"target", number("The loudness target in LUFS (normalise: loudness)")},
         {"name",
          string("The file's name, without extension (default: the project's; stems add the track's)")}});
    exporting.outputSchema =
        result({{"files", arrayOf(result({{"file", string("")}, {"track", {{"type", {"string", "null"}}}}}), "The files", std::nullopt)}},
               {"files"});
    exporting.annotations.readOnly = true; // the project stays as it is
    exporting.call = [&env](const Json& args, CallContext& context) -> ToolResult {
        struct Done
        {
            std::mutex mutex;
            std::condition_variable changed;
            std::optional<engine::Session::ExportResult> result;
        };
        auto done = std::make_shared<Done>();
        std::shared_ptr<engine::Session::RenderJob> job;
        std::optional<ToolResult> refused;
        const bool ran = env.main.run([&] {
            try
            {
                env.session.flushSnapshot();
                auto s = env.session.exportSettings(); // where files go is the user's choice, never the assistant's
                std::vector<std::uint64_t> tracks;
                if (args.contains("what"))
                    s.what = args["what"] == "mix" ? model::ExportWhat::Mix : model::ExportWhat::AllTracks;
                if (args.contains("tracks"))
                {
                    s.what = model::ExportWhat::SelectedTracks;
                    for (const auto& value : args["tracks"])
                        tracks.push_back(channel(env, value).id);
                }
                s.throughReturns = args.value("throughReturns", s.throughReturns);
                if (args.contains("range"))
                    s.range = args["range"] == "loop" ? model::ExportRange::Loop : model::ExportRange::Song;
                const auto formatOf = [](const Json& name)
                {
                    for (const auto f : model::kExportFormats)
                        if (model::extensionFor(f).substr(1) == name.get<std::string>())
                            return f;
                    return model::ExportFormat::Wav;
                };
                // A format's bit rate: one it offers, or say which.
                const auto rateFor = [](model::ExportFormat format, int kbps)
                {
                    const auto rates = model::bitRatesFor(format);
                    if (std::find(rates.begin(), rates.end(), kbps) == rates.end())
                    {
                        std::string list;
                        for (const int r : rates)
                            list += (list.empty() ? "" : ", ") + std::to_string(r);
                        throw Problem{model::formatName(format) + " takes " + list + " kbps."};
                    }
                    return kbps;
                };
                if (args.contains("format"))
                    s.format = formatOf(args["format"]);
                s.bitDepth = args.value("bitDepth", s.bitDepth);
                if (model::isLossy(s.format))
                    s.kbps = args.contains("quality") ? rateFor(s.format, args["quality"].get<int>())
                                                      : model::validated(s).kbps;
                if (args.contains("alsoAs"))
                {
                    const auto& also = args["alsoAs"];
                    s.alsoAs = formatOf(also["format"]);
                    if (*s.alsoAs == s.format)
                        throw Problem{"alsoAs is a second format: not " + model::formatName(s.format) +
                                      " again."};
                    if (model::isLossy(*s.alsoAs))
                        s.alsoKbps = also.contains("quality") ? rateFor(*s.alsoAs, also["quality"].get<int>())
                                                              : model::defaultBitRate(*s.alsoAs);
                    else
                        s.alsoBitDepth = also.value("bitDepth", 24);
                }
                if (args.contains("sampleRate"))
                {
                    s.sampleRate = args["sampleRate"].get<double>();
                    if (model::validated(s).sampleRate != s.sampleRate)
                        throw Problem{"Choose 44100, 48000, 88200 or 96000."};
                }
                if (const auto depths = model::bitDepthsFor(s.format);
                    !depths.empty() && std::find(depths.begin(), depths.end(), s.bitDepth) == depths.end())
                    throw Problem{model::formatName(s.format) + " holds 16 or 24 bits."};
                s.dither = args.value("dither", s.dither);
                if (args.contains("normalise"))
                    s.normalise = args["normalise"] == "peak"       ? model::ExportNormalise::Peak
                                  : args["normalise"] == "loudness" ? model::ExportNormalise::Loudness
                                                                    : model::ExportNormalise::Off;
                s.loudnessTarget = args.value("target", s.loudnessTarget);
                s.name = args.value("name", s.name);
                if (s.name.find_first_of("/\\") != std::string::npos)
                    throw Problem{"A name, not a path: files go where the project's export settings say."};
                if (const auto why = env.session.exportRefusal(s, tracks); !why.empty())
                    throw Problem{why};
                job = env.session.exportAudio(s, tracks, [done](const engine::Session::ExportResult& r) {
                    {
                        std::lock_guard lock(done->mutex);
                        done->result = r;
                    }
                    done->changed.notify_all();
                });
                if (job == nullptr)
                    throw Problem{"There's nothing to export."};
            }
            catch (const Problem& p)
            {
                refused = ToolResult::error(p.message);
            }
        });
        if (!ran)
            return ToolResult::error("The DAW is closing.");
        if (refused)
            return *refused;
        for (;;)
        {
            {
                std::unique_lock lock(done->mutex);
                if (done->changed.wait_for(lock, std::chrono::milliseconds(200), [&] { return done->result.has_value(); }))
                    break;
            }
            context.progress(job->progress(), 1.0, "Exporting");
            if (context.cancelled() || !env.main.isOpen())
                job->cancel();
            if (!env.main.isOpen())
                return ToolResult::error("The DAW is closing.");
        }
        const auto r = *done->result;
        if (r.cancelled)
            return ToolResult::error("The export was cancelled; no file was left.");
        if (!r.error.empty())
            return ToolResult::error(r.error);
        Json files = Json::array();
        std::string summary;
        for (const auto& f : r.files)
        {
            Json file = measurementsJson(f.measured);
            file["file"] = f.file.getFullPathName().toStdString();
            file["track"] = f.track.empty() ? Json(nullptr) : Json(f.track);
            file["seconds"] = f.measured.seconds;
            file["ceilingHeld"] = f.ceilingHeld;
            file["encoded"] = f.file.hasFileExtension("mp3;ogg;opus"); // measured decoded
            files.push_back(std::move(file));
            summary += f.file.getFileName().toStdString() + ": " + describe(f.measured) + (f.ceilingHeld ? " (held by the -1 dBTP ceiling)" : "") + " ";
        }
        return ToolResult::ok({{"files", std::move(files)}}, summary);
    };
    registry.tools.push_back(std::move(exporting));

    Tool measure;
    measure.name = "measure_module";
    measure.title = "Read a module's meters";
    measure.description =
        "Reads what one of our modules' meters show while the project plays, over a short window (half a second "
        "by default): levels, gain reduction, a gate opening, loudness, voices. Meters read silence when nothing "
        "plays, so start playback first. render measures any sound, including third-party plugins'.";
    measure.inputSchema = object({{"module", id(IdKind::Module, "The module")},
                                  {"seconds", number("How long to watch, 0.1 to 5 (default 0.5)")}},
                                 {"module"});
    measure.outputSchema = result({{"module", string("")},
                                   {"playing", boolean("Whether the project was playing")},
                                   {"meters", arrayOf(result({{"id", string("")}, {"name", string("")}}), "",
                                                      std::nullopt)}},
                                  {"module", "playing", "meters"});
    measure.annotations.readOnly = true;
    measure.call = [&env](const Json& args, CallContext& context) -> ToolResult {
        std::unique_ptr<engine::DeviceHandle> handle;
        std::span<const MeterSpec> meters;
        std::string moduleId, moduleName;
        bool playing = false;
        std::optional<ToolResult> refused;
        const double seconds = std::clamp(args.value("seconds", 0.5), 0.1, 5.0);
        const bool ran = env.main.run([&] {
            try
            {
                env.session.flushSnapshot();
                const auto [slot, owner] = module(env, args["module"]);
                moduleId = formatId(IdKind::Module, slot.id);
                moduleName = slot.name;
                handle = env.session.openDevice(slot.id);
                if (handle == nullptr || handle->context().analysis == nullptr)
                    throw Problem{"measure_module reads our modules' meters; render measures a plugin's sound."};
                meters = handle->context().info.meters;
                if (meters.empty())
                    throw Problem{"This module has no meters; render measures its sound."};
                playing = env.session.isPlaying();
                for (const auto& m : meters) // start the window: drop what was held before it
                    (void)handle->context().analysis->takeProbePeak(m.slot);
            }
            catch (const Problem& p)
            {
                refused = ToolResult::error(p.message);
            }
        });
        // The handle keeps the module alive; it is let go on the message thread.
        auto release = [&] { env.main.run([&] { handle.reset(); }); };
        if (!ran)
            return ToolResult::error("The DAW is closing.");
        if (refused)
        {
            release();
            return *refused;
        }

        const auto until = std::chrono::steady_clock::now() + std::chrono::duration<double>(seconds);
        while (std::chrono::steady_clock::now() < until && !context.cancelled() && env.main.isOpen())
            std::this_thread::sleep_for(std::chrono::milliseconds(20));

        auto& bus = *handle->context().analysis;
        auto dbOrNull = [](double v) { return v > 1e-5 ? Json(db(20.0 * std::log10(v))) : Json(nullptr); };
        Json list = Json::array();
        std::string summary;
        for (const auto& m : meters)
        {
            Json value, unit;
            switch (m.kind)
            {
            case MeterKind::Level: value = dbOrNull(bus.takeProbePeak(m.slot)); unit = "dBFS"; break;
            case MeterKind::TruePeak: value = dbOrNull(bus.takeProbePeak(m.slot)); unit = "dBTP"; break;
            case MeterKind::Reduction: value = db(bus.takeProbePeak(m.slot)); unit = "dB"; break;
            case MeterKind::Flag: value = bus.takeProbePeak(m.slot) > 0.5f; unit = nullptr; break;
            case MeterKind::Loudness:
            {
                const double v = bus.read(m.slot);
                value = std::isfinite(v) && v > -70.0 ? Json(db(v)) : Json(nullptr);
                unit = "LUFS";
                break;
            }
            case MeterKind::Decibels:
            {
                const double v = bus.read(m.slot);
                value = std::isfinite(v) && v > -99.0 ? Json(db(v)) : Json(nullptr);
                unit = "dB";
                break;
            }
            case MeterKind::Count: value = static_cast<int>(std::lround(bus.read(m.slot))); unit = nullptr; break;
            case MeterKind::LoudnessUnits:
            {
                const double v = bus.read(m.slot);
                value = std::isfinite(v) ? Json(db(v)) : Json(nullptr);
                unit = "LU";
                break;
            }
            case MeterKind::Correlation:
            case MeterKind::Ratio:
            {
                const double v = bus.read(m.slot);
                value = std::isfinite(v) ? Json(std::round(v * 100.0) / 100.0) : Json(nullptr);
                unit = nullptr;
                break;
            }
            case MeterKind::Hertz:
            {
                const double v = bus.read(m.slot);
                value = std::isfinite(v) ? Json(std::round(v * 100.0) / 100.0) : Json(nullptr);
                unit = "Hz";
                break;
            }
            case MeterKind::Pitch:
            {
                // The note by name (C3 = 60), as the note tools name pitches.
                const double v = bus.read(m.slot);
                value = std::isfinite(v) ? Json(pitchName(static_cast<int>(std::lround(v)))) : Json(nullptr);
                unit = nullptr;
                break;
            }
            case MeterKind::Cents:
            {
                const double v = bus.read(m.slot);
                value = std::isfinite(v) ? Json(std::round(v * 10.0) / 10.0) : Json(nullptr);
                unit = "cents";
                break;
            }
            }
            Json entry{{"id", std::string(m.id)}, {"name", std::string(m.name)}, {"value", value}};
            if (!unit.is_null())
                entry["unit"] = unit;
            if (summary.size() < 160)
                summary += (summary.empty() ? "" : ", ") + std::string(m.name) + " " +
                           (value.is_null() ? std::string("silent") : value.dump()) +
                           (unit.is_null() || value.is_null() ? "" : " " + unit.get<std::string>());
            list.push_back(std::move(entry));
        }
        release();
        if (!playing)
            summary = "Nothing was playing, so the meters read silence. " + summary;
        return ToolResult::ok({{"module", moduleId}, {"seconds", seconds}, {"playing", playing}, {"meters", list}},
                              moduleName + ": " + summary + ".");
    };
    registry.tools.push_back(std::move(measure));

    Tool scan;
    scan.name = "scan_plugins";
    scan.title = "Scan for plugins";
    scan.description = "Scans the standard plugin folders for third-party instruments and effects (VST3, and LV2 on "
                       "Linux), so they can be added with add_module. Can take a minute; reports progress.";
    scan.inputSchema = object({});
    scan.outputSchema =
        result({{"found", integer("Plugins known after the scan")},
                {"skipped", integer("Files skipped: they crashed the scan, or couldn't be read")}},
               {"found"});
    scan.annotations.idempotent = true;
    scan.call = [&env](const Json&, CallContext& context) -> ToolResult {
        auto waiter = std::make_shared<Waiter>();
        std::optional<ToolResult> refused;
        const bool ran = env.main.run([&] {
            if (env.session.isScanning())
            {
                refused = ToolResult::error("A plugin scan is already running; try again when it's done.");
                return;
            }
            env.session.scanPlugins(
                [waiter](const juce::String& current)
                {
                    std::lock_guard lock(waiter->mutex);
                    waiter->current = current.toStdString();
                },
                [waiter](engine::Session::ScanResult r)
                {
                    {
                        std::lock_guard lock(waiter->mutex);
                        waiter->skipped = r.skipped;
                    }
                    waiter->finish({}, r.found);
                });
        });
        if (!ran)
            return ToolResult::error("The DAW is closing.");
        if (refused)
            return *refused;
        for (int tick = 0;; ++tick)
        {
            std::string current;
            {
                std::unique_lock lock(waiter->mutex);
                if (waiter->changed.wait_for(lock, std::chrono::milliseconds(250), [&] { return waiter->done; }))
                    break;
                current = waiter->current;
            }
            context.progress(tick, std::nullopt, current.empty() ? "Scanning" : "Scanning " + current);
            if (!env.main.isOpen())
                return ToolResult::error("The DAW is closing.");
        }
        return ToolResult::ok(
            {{"found", waiter->found}, {"skipped", waiter->skipped}},
            std::to_string(waiter->found) + " plugins are known; list_module_types lists them." +
                (waiter->skipped > 0 ? " " + std::to_string(waiter->skipped) +
                                           " skipped: they crashed the scan or couldn't be read."
                                     : std::string()));
    };
    registry.tools.push_back(std::move(scan));
}
} // namespace fd::mcp
