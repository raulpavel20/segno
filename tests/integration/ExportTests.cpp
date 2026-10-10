// Export (M2 step C8, ADR 0039, docs/design/export.md): the mix nulls against an offline render,
// stems add up to it, the song runs to its tail and a loop is exact, formats and normalising do
// what they say, files land where asked and never overwrite. Through engine::Session.

#include "engine/DevicePlugin.h"
#include "engine/EngineBehaviour.h"
#include "engine/Session.h"
#include "fd/testkit/WavIO.h"

#include <juce_audio_formats/juce_audio_formats.h>
#include <tracktion_engine/tracktion_engine.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>

using Catch::Approx;
using fd::engine::Session;
using fd::model::ExportFormat;
using fd::model::ExportNormalise;
using fd::model::ExportRange;
using fd::model::ExportSettings;
using fd::model::ExportWhat;

namespace
{
/// Audio as read back: channels of samples, and the file's rate and depth.
struct Read
{
    std::vector<std::vector<float>> channels;
    double rate = 0.0;
    int bits = 0;
    [[nodiscard]] int frames() const { return channels.empty() ? 0 : static_cast<int>(channels[0].size()); }
};

Read readAny(const juce::File& file)
{
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));
    REQUIRE(reader != nullptr);
    juce::AudioBuffer<float> buffer(static_cast<int>(reader->numChannels), static_cast<int>(reader->lengthInSamples));
    reader->read(&buffer, 0, buffer.getNumSamples(), 0, true, true);
    Read out{.rate = reader->sampleRate, .bits = static_cast<int>(reader->bitsPerSample)};
    for (int c = 0; c < buffer.getNumChannels(); ++c)
        out.channels.emplace_back(buffer.getReadPointer(c), buffer.getReadPointer(c) + buffer.getNumSamples());
    return out;
}

/// The loudest difference between two reads over the frames both have, in dBFS (-200: identical).
double differenceDb(const Read& a, const Read& b, int frames = -1)
{
    REQUIRE(a.channels.size() == b.channels.size());
    const int n = frames >= 0 ? frames : std::min(a.frames(), b.frames());
    float worst = 0.0f;
    for (size_t c = 0; c < a.channels.size(); ++c)
        for (int i = 0; i < n; ++i)
            worst = std::max(worst, std::abs(a.channels[c][static_cast<size_t>(i)] - b.channels[c][static_cast<size_t>(i)]));
    return worst > 0.0f ? 20.0 * std::log10(worst) : -200.0;
}

struct Exporter
{
    juce::ScopedJuceInitialiser_GUI init;
    tracktion::Engine engine{"fd-export-tests", nullptr, fd::engine::makeEngineBehaviour()};
    std::unique_ptr<Session> session;
    juce::File root = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("fd-export-" + juce::Uuid().toString());

    Exporter()
    {
        fd::engine::registerDevices(engine);
        REQUIRE(root.createDirectory());
        session = std::make_unique<Session>(engine);
        session->setUnsavedRoot(root.getChildFile("Unsaved"));
    }

    ~Exporter()
    {
        session.reset();
        root.deleteRecursively();
    }

    std::uint64_t track(size_t i)
    {
        session->updateSnapshotNow();
        return session->snapshot()->tracks.at(i).id;
    }

    /// A sine of `seconds` at `hz` and `level`, as a clip at `start` on track `t`.
    std::uint64_t tone(std::uint64_t t, double start, double seconds, double hz, float level)
    {
        const double rate = 48000.0;
        fd::testkit::AudioBuffer sine(2, static_cast<int>(seconds * rate));
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < sine.numSamples(); ++i)
                sine.at(c, i) = level * static_cast<float>(std::sin(2.0 * juce::MathConstants<double>::pi * hz * i / rate));
        const auto file = root.getChildFile("tone-" + juce::String(hz) + "-" + juce::String(start) + ".wav");
        REQUIRE(fd::testkit::writeWav(file.getFullPathName().toStdString(), sine, rate));
        const auto clip = session->insertAudioFile(t, file, start);
        REQUIRE(clip != 0);
        return clip;
    }

    /// Exports and waits for it to finish.
    Session::ExportResult exportNow(const ExportSettings& settings, const std::vector<std::uint64_t>& tracks = {})
    {
        std::optional<Session::ExportResult> result;
        auto job = session->exportAudio(settings, tracks, [&](const Session::ExportResult& r) { result = r; });
        REQUIRE(job != nullptr);
        for (int i = 0; i < 3000 && !result; ++i)
            juce::MessageManager::getInstance()->runDispatchLoopUntil(10);
        REQUIRE(result);
        INFO(result->error);
        REQUIRE(result->error.empty());
        return *result;
    }

    /// The mix (or one channel), rendered offline at the engine's rate.
    Read render(double start, double end)
    {
        juce::TemporaryFile out(".wav");
        bool done = false;
        auto job = session->render({start, end, std::nullopt, session->sampleRate()}, out.getFile(), [&](const std::string&) { done = true; });
        REQUIRE(job != nullptr);
        for (int i = 0; i < 1000 && !done; ++i)
            juce::MessageManager::getInstance()->runDispatchLoopUntil(10);
        REQUIRE(done);
        return readAny(out.getFile());
    }
};

const ExportSettings kFloat{.bitDepth = 32};
} // namespace

TEST_CASE("export: the mix nulls against an offline render; the song runs to its tail, a loop is exact", "[integration]")
{
    Exporter x;
    const auto t = x.track(0);
    // A synth with a long release: its last note rings past the clip's end.
    const auto synth = x.session->addDevice(t, "fd:subtractive-synth");
    REQUIRE(synth != 0);
    {
        auto handle = x.session->openDevice(synth);
        REQUIRE(handle != nullptr);
        const auto& params = handle->context().info.params;
        for (const auto& [id, text] : {std::pair{"voice.drift", "0"}, std::pair{"amp.r", "1.5 s"}})
        {
            const auto it = std::find_if(params.begin(), params.end(), [&](const auto& p) { return p.id == id; });
            REQUIRE(it != params.end());
            handle->context().params.setPlain(static_cast<size_t>(it - params.begin()), *it->fromText(text));
        }
    }
    const auto clip = x.session->createMidiClip(t, 0.5, 2.0);
    x.session->setNotes(clip, {{.pitch = 48, .start = 2.5, .length = 1.0, .velocity = 0.8f}}); // ends with the clip
    const double rate = x.session->sampleRate();

    const auto song = x.exportNow(kFloat);
    REQUIRE(song.files.size() == 1);
    const auto file = song.files[0].file;
    CHECK(file.getParentDirectory() == x.session->exportsFolder());
    CHECK(file.getFileName() == "Untitled.wav");
    const auto exported = readAny(file);
    CHECK(exported.bits == 32);
    // To the clip's end (2.5 s), then the release, then cut where it falls silent (within 10 s).
    const double seconds = exported.frames() / rate;
    CHECK(seconds > 2.5 + 1.0);
    CHECK(seconds < 2.5 + 10.0);
    const auto rendered = x.render(0.0, seconds);
    CHECK(differenceDb(exported, rendered) < -120.0);
    // Its report is the file's.
    CHECK(song.files[0].measured.seconds == Approx(seconds).margin(1.0 / rate));
    CHECK_FALSE(song.files[0].measured.silent);

    // A loop: exactly the brace, no tail, named after its bars.
    x.session->setLoopRange(1.0, 2.0);
    const auto loop = x.exportNow({.range = ExportRange::Loop, .bitDepth = 32});
    REQUIRE(loop.files.size() == 1);
    CHECK(loop.files[0].file.getFileName() == "Untitled - Bars 1-2.wav");
    CHECK(readAny(loop.files[0].file).frames() == static_cast<int>(std::lround(1.0 * rate)));
}

TEST_CASE("export: stems add up to the mix; a return's stem is what the sends feed it", "[integration]")
{
    Exporter x;
    const auto a = x.track(0), b = x.track(1);
    x.session->updateSnapshotNow();
    x.tone(a, 0.0, 2.0, 220.0, 0.25f);
    x.tone(b, 0.5, 2.0, 330.0, 0.25f);
    const auto back = x.session->addReturnTrack("Room");
    REQUIRE(back != 0);
    REQUIRE(x.session->setSend(a, back, -6.0f, std::nullopt));
    x.session->updateSnapshotNow();

    const auto mix = readAny(x.exportNow(kFloat).files.at(0).file);
    const auto stems = x.exportNow({.what = ExportWhat::AllTracks, .bitDepth = 32});
    // Every track, and the return, a file each, named after the track.
    std::vector<std::string> names;
    for (const auto& f : stems.files)
        names.push_back(f.track);
    x.session->updateSnapshotNow();
    std::vector<std::string> expected;
    for (const auto* list : {&x.session->snapshot()->tracks, &x.session->snapshot()->returns})
        for (const auto& t : *list)
            expected.push_back(t.name);
    CHECK(expected.back() == "Room");
    CHECK(names == expected);
    CHECK(stems.files.front().file.getFileName() == juce::String("Untitled - ") + juce::String(expected.front()) + ".wav");
    // Summed, they're the mix (the master has no modules here).
    Read sum = mix;
    for (auto& ch : sum.channels)
        std::fill(ch.begin(), ch.end(), 0.0f);
    for (const auto& f : stems.files)
    {
        const auto stem = readAny(f.file);
        for (size_t c = 0; c < sum.channels.size(); ++c)
            for (int i = 0; i < std::min(stem.frames(), sum.frames()); ++i)
                sum.channels[c][static_cast<size_t>(i)] += stem.channels[c][static_cast<size_t>(i)];
    }
    CHECK(differenceDb(sum, mix) < -100.0);
    // The return's stem: the send's share of the first track, nothing else.
    const auto room = std::find_if(stems.files.begin(), stems.files.end(), [](const auto& f) { return f.track == "Room"; });
    REQUIRE(room != stems.files.end());
    CHECK_FALSE(room->measured.silent);
    CHECK(room->measured.peakDb == Approx(20.0 * std::log10(0.25) - 6.0).margin(0.5));

    // Through the returns: the first track's stem holds its send's share too.
    const auto through = x.exportNow({.what = ExportWhat::SelectedTracks, .throughReturns = true, .bitDepth = 32}, {a});
    REQUIRE(through.files.size() == 1);
    const auto dry = std::find_if(stems.files.begin(), stems.files.end(), [&](const auto& f) { return f.track == expected.front(); });
    REQUIRE(dry != stems.files.end());
    CHECK(through.files[0].measured.peakDb > dry->measured.peakDb + 2.0);
}

TEST_CASE("export: formats, depths, rates, dither and normalising", "[integration]")
{
    Exporter x;
    const auto t = x.track(0);
    x.tone(t, 0.0, 1.0, 440.0, 0.1f);
    const auto exact = readAny(x.exportNow(kFloat).files.at(0).file);

    // 16 bits without dither: the float export rounded to the 16-bit grid.
    const auto plain = readAny(x.exportNow({.bitDepth = 16, .dither = false}).files.at(0).file);
    CHECK(plain.bits == 16);
    float worst = 0.0f;
    for (size_t c = 0; c < plain.channels.size(); ++c)
        for (int i = 0; i < plain.frames(); ++i)
            worst = std::max(worst, std::abs(plain.channels[c][static_cast<size_t>(i)] -
                                             std::round(exact.channels[c][static_cast<size_t>(i)] * 32768.0f) / 32768.0f));
    CHECK(worst == 0.0f);
    // FLAC and AIFF hold the same samples as WAV at the same depth (dither is the same every time).
    const auto wav24 = readAny(x.exportNow({.bitDepth = 24}).files.at(0).file);
    const auto flac24 = readAny(x.exportNow({.format = ExportFormat::Flac, .bitDepth = 24}).files.at(0).file);
    const auto aiff16 = x.exportNow({.format = ExportFormat::Aiff, .bitDepth = 16});
    CHECK(flac24.bits == 24);
    CHECK(differenceDb(wav24, flac24) == -200.0);
    CHECK(aiff16.files.at(0).file.hasFileExtension("aiff"));
    CHECK(readAny(aiff16.files.at(0).file).bits == 16);
    // Dither at 24 bits stays within a step of the float export.
    CHECK(differenceDb(wav24, exact) < -130.0);

    // Another rate: rendered at it, the same length in seconds.
    const auto rate441 = readAny(x.exportNow({.bitDepth = 32, .sampleRate = 44100.0}).files.at(0).file);
    CHECK(rate441.rate == 44100.0);
    CHECK(rate441.frames() / 44100.0 == Approx(exact.frames() / x.session->sampleRate()).margin(0.01));

    // Normalising: the true peak at -1 dBTP; a loudness target, the true peak first.
    const auto peak = x.exportNow({.bitDepth = 32, .normalise = ExportNormalise::Peak}).files.at(0);
    CHECK(peak.measured.truePeakDb == Approx(-1.0).margin(0.1));
    const auto loud = x.exportNow({.bitDepth = 32, .normalise = ExportNormalise::Loudness, .loudnessTarget = -20.0}).files.at(0);
    CHECK_FALSE(loud.ceilingHeld);
    CHECK(loud.measured.integratedLufs == Approx(-20.0).margin(0.2));
    // A quiet tone with one loud burst: to -14 LUFS it would peak far above -1 dBTP, so the
    // ceiling holds it, and says so.
    x.session->deleteClips({x.session->snapshot()->tracks.at(0).clips.at(0).id});
    x.tone(t, 0.0, 3.0, 440.0, 0.01f);
    x.tone(x.track(1), 1.0, 0.02, 440.0, 0.5f);
    x.session->updateSnapshotNow();
    const auto held = x.exportNow({.bitDepth = 32, .normalise = ExportNormalise::Loudness}).files.at(0);
    CHECK(held.ceilingHeld);
    CHECK(held.measured.truePeakDb == Approx(-1.0).margin(0.1));
    CHECK(held.measured.integratedLufs < -14.0);
}

TEST_CASE("export: files go where asked and never overwrite; cancelling and refusals", "[integration]")
{
    Exporter x;
    const auto t = x.track(0);
    // Refused while there's nothing to export, no loop, or no tracks chosen.
    CHECK(x.session->exportRefusal({}, {}).find("no clips") != std::string::npos);
    x.tone(t, 0.0, 1.0, 440.0, 0.1f);
    x.session->updateSnapshotNow();
    CHECK(x.session->exportRefusal({}, {}).empty());
    CHECK(x.session->exportRefusal({.range = ExportRange::Loop}, {}).find("loop") != std::string::npos);
    CHECK(x.session->exportRefusal({.what = ExportWhat::SelectedTracks}, {}).find("Select") != std::string::npos);

    // Never overwritten: a number is added.
    const auto first = x.exportNow({}).files.at(0).file;
    const auto second = x.exportNow({}).files.at(0).file;
    CHECK(first.getFileName() == "Untitled.wav");
    CHECK(second.getFileName() == "Untitled 2.wav");
    CHECK(first.existsAsFile());
    // Anywhere on disk, under the name asked.
    const auto elsewhere = x.root.getChildFile("Elsewhere");
    const auto there = x.exportNow({.folder = elsewhere.getFullPathName().toStdString(), .name = "Demo"}).files.at(0).file;
    CHECK(there == elsewhere.getChildFile("Demo.wav"));

    // One at a time; cancelled, it leaves nothing.
    std::optional<Session::ExportResult> result;
    auto job = x.session->exportAudio({.folder = elsewhere.getFullPathName().toStdString(), .name = "Gone"}, {},
                                      [&](const Session::ExportResult& r) { result = r; });
    REQUIRE(job != nullptr);
    CHECK(x.session->isExporting());
    CHECK(x.session->exportRefusal({}, {}).find("running") != std::string::npos);
    job->cancel();
    for (int i = 0; i < 1000 && !result; ++i)
        juce::MessageManager::getInstance()->runDispatchLoopUntil(10);
    REQUIRE(result);
    CHECK(result->cancelled);
    CHECK_FALSE(elsewhere.getChildFile("Gone.wav").exists());
    CHECK(elsewhere.findChildFiles(juce::File::findFiles, false, "*.part").isEmpty());
    CHECK_FALSE(x.session->isExporting());

    // An unsaved project's exports move into its folder when it's saved.
    const auto project = x.root.getChildFile("Song").getChildFile("Song.segno");
    REQUIRE(x.session->saveAs(project));
    CHECK(project.getParentDirectory().getChildFile("Exports").getChildFile("Untitled.wav").existsAsFile());
    CHECK(x.session->exportsFolder() == project.getParentDirectory().getChildFile("Exports"));
    CHECK(x.exportNow({}).files.at(0).file.getFileName() == "Song.wav");

    // Edits while it exports don't change it: what was there when it started is exported.
    std::optional<Session::ExportResult> during;
    REQUIRE(x.session->exportAudio(kFloat, {}, [&](const Session::ExportResult& r) { during = r; }) != nullptr);
    x.session->deleteClips({x.session->snapshot()->tracks.at(0).clips.at(0).id});
    for (int i = 0; i < 2000 && !during; ++i)
        juce::MessageManager::getInstance()->runDispatchLoopUntil(10);
    REQUIRE(during);
    REQUIRE(during->files.size() == 1);
    CHECK_FALSE(during->files[0].measured.silent);
}
