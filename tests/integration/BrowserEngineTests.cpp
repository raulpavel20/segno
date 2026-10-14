// The browser in the engine (M3 C2, ADR 0046, docs/design/browser.md): the index of the places,
// what the browser finds, previews in time and of presets, replacing an instrument, loading the
// Sampler, and keeping a project's files together. Through engine::Session.

#include "FactoryTables.h"
#include "RecordingFixture.h"
#include "TableFiles.h"
#include "WavetableSynth.h"
#include "engine/PreviewPlayer.h"
#include "ui/BrowserDrops.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <juce_audio_formats/juce_audio_formats.h>

using Catch::Approx;
using namespace recording_test;
namespace model = fd::model;

namespace
{
/// A sine of `seconds` written as a WAV.
void writeSine(const juce::File& file, double seconds, double rate = 48000.0, int channels = 2)
{
    (void)file.getParentDirectory().createDirectory();
    file.deleteFile();
    juce::AudioBuffer<float> b(channels, static_cast<int>(seconds * rate));
    for (int c = 0; c < channels; ++c)
        for (int i = 0; i < b.getNumSamples(); ++i)
            b.setSample(c, i, 0.25f * std::sin(static_cast<float>(i) * 0.05f));
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::OutputStream> out(file.createOutputStream());
    auto writer = wav.createWriterFor(
        out,
        juce::AudioFormatWriterOptions{}.withSampleRate(rate).withNumChannels(channels).withBitsPerSample(
            16));
    REQUIRE(writer != nullptr);
    writer->writeFromAudioSampleBuffer(b, 0, b.getNumSamples());
}

void writeMidi(const juce::File& file)
{
    juce::MidiMessageSequence seq;
    seq.addEvent(juce::MidiMessage::noteOn(1, 60, 0.8f), 0);
    seq.addEvent(juce::MidiMessage::noteOff(1, 60), 480);
    juce::MidiFile midi;
    midi.setTicksPerQuarterNote(480);
    midi.addTrack(seq);
    file.deleteFile();
    juce::FileOutputStream out(file);
    REQUIRE(midi.writeTo(out));
}

/// The preview's next half second, as the device would get it.
float previewPeak(Session& s)
{
    auto& player = s.previewPlayer();
    player.prepare(48000.0, 512);
    float peak = 0.0f;
    juce::AudioBuffer<float> block(2, 512);
    for (int i = 0; i < 47; ++i)
    {
        player.renderNextBlock(block);
        peak = std::max(peak, block.getMagnitude(0, 512));
    }
    return peak;
}

struct Files
{
    Recording r;
    juce::File place = r.root.getChildFile("Sounds");
    Files()
    {
        writeSine(place.getChildFile("Kick.wav"), 0.5, 44100.0, 1);
        writeSine(place.getChildFile("Loop 120 bpm.wav"), 2.0); // four beats at 120
        writeSine(place.getChildFile("Drums").getChildFile("Snare.wav"), 0.25);
        writeMidi(place.getChildFile("Groove.mid"));
        (void)place.getChildFile("notes.txt").replaceWithText("not listed");
        writeSine(place.getChildFile(".hidden.wav"), 0.1);
        r.session->setPresetsFolder(r.root.getChildFile("Segno").getChildFile("Presets"));
        r.session->setBrowserIndexFile(r.root.getChildFile("browser-index.json"));
        r.session->setSegnoFolder(r.root.getChildFile("Segno"));
        r.session->setBrowserPlaces({place.getFullPathName().toStdString()});
        r.session->waitForBrowserIndex();
    }
    const model::IndexedFile* indexed(const std::string& name)
    {
        for (const auto& [p, files] : r.session->browserIndex().places)
            for (const auto& f : files)
                if (juce::File(juce::String::fromUTF8(f.path.c_str())).getFileName() == juce::String(name))
                    return &f;
        return nullptr;
    }
};
} // namespace

TEST_CASE("browser engine: the places are indexed in the background and kept in the index file",
          "[integration]")
{
    Files f;
    auto& s = *f.r.session;
    const auto key = f.place.getFullPathName().toStdString();
    REQUIRE(s.browserIndex().places.contains(key));
    CHECK(s.browserIndex().places.at(key).size() == 4); // not the text file, not the hidden one
    const auto* kick = f.indexed("Kick.wav");
    REQUIRE(kick != nullptr);
    CHECK(kick->kind == model::BrowserKind::Sample);
    CHECK(kick->seconds == Approx(0.5).margin(0.001));
    CHECK(kick->sampleRate == 44100.0);
    CHECK(kick->channels == 1);
    CHECK(f.indexed("Loop 120 bpm.wav")->tempo == 120.0);
    CHECK(f.indexed("Snare.wav") != nullptr); // in a folder below
    CHECK(f.indexed("Groove.mid")->kind == model::BrowserKind::Midi);
    // The Segno folder is a place, with a Samples folder for your sounds.
    CHECK(s.browserIndex().places.contains(f.r.root.getChildFile("Segno").getFullPathName().toStdString()));
    CHECK(f.r.root.getChildFile("Segno").getChildFile("Samples").isDirectory());

    // What the browser finds: modules, presets (not Default settings), files and their folders.
    const auto items = s.browserItems({"file:" + kick->path});
    const auto find = [&items](const std::string& id)
    { return std::find_if(items.begin(), items.end(), [&](const auto& i) { return i.id == id; }); };
    CHECK(find("module:fd:compressor") != items.end());
    const auto glue = find("preset:fd:compressor|factory/Bus glue");
    REQUIRE(glue != items.end());
    CHECK(glue->caption == "Compressor");
    CHECK(find("preset:fd:compressor|") == items.end());
    CHECK(find("file:" + kick->path)->favourite);
    CHECK(find("folder:" + f.place.getChildFile("Drums").getFullPathName().toStdString()) != items.end());

    // The index file: the next session searches before it has looked at anything.
    Session again(f.r.engine);
    again.setBrowserIndexFile(f.r.root.getChildFile("browser-index.json"));
    CHECK(again.browserIndex() == s.browserIndex());

    // A file added shows after a refresh; a place removed leaves the index.
    writeSine(f.place.getChildFile("Clap.wav"), 0.2);
    s.refreshBrowser(f.place);
    s.waitForBrowserIndex();
    CHECK(f.indexed("Clap.wav") != nullptr);
    s.setBrowserPlaces({});
    s.waitForBrowserIndex();
    CHECK_FALSE(s.browserIndex().places.contains(key));
}

TEST_CASE("browser engine: a loop that fits the song's tempo previews in time; anything else once",
          "[integration]")
{
    Files f;
    auto& s = *f.r.session;
    const auto loop = f.place.getChildFile("Loop 120 bpm.wav");
    const auto kick = f.place.getChildFile("Kick.wav");
    REQUIRE(s.snapshot()->tempo.bpm == Approx(120.0));

    // Stopped: once, from its start, at its own speed.
    auto p = s.previewFile(loop);
    REQUIRE(p);
    CHECK_FALSE(p->inTime);
    CHECK(p->tempo == 120.0);
    CHECK(s.previewPosition() == Approx(0.0).margin(0.01));
    CHECK(previewPeak(s) > 0.1f); // it sounds, at its own volume
    s.setPreviewVolume(-60.0f);
    CHECK(previewPeak(s) < 0.001f);
    s.setPreviewVolume(-6.0f);

    // Playing: the loop starts where the song's bar is (a beat in: half a second into it).
    s.play();
    f.r.pump(0.5);
    p = s.previewFile(loop);
    REQUIRE(p);
    CHECK(p->inTime);
    const double songBeat = std::fmod(s.position() * 2.0, 4.0);
    CHECK(s.previewPosition() == Approx(songBeat * 0.5).margin(0.02));
    // A one-beat sound isn't a loop: once, from its start.
    p = s.previewFile(kick);
    REQUIRE(p);
    CHECK_FALSE(p->inTime);
    CHECK(s.previewPosition() == Approx(0.0).margin(0.01));
    // In another tempo, the loop no longer fits: once.
    s.setTempo(100.0);
    s.updateSnapshotNow();
    CHECK_FALSE(s.previewFile(loop)->inTime);
    s.stop();
    s.stopPreview();
    CHECK_FALSE(s.isPreviewing());
    CHECK(previewPeak(s) == 0.0f);
    CHECK_FALSE(s.previewFile(f.place.getChildFile("notes.txt")));
}

TEST_CASE("browser engine: instrument presets preview a phrase; MIDI files play on the track's instrument",
          "[integration]")
{
    Files f;
    auto& s = *f.r.session;
    CHECK(s.previewPreset("subtractive-synth", "factory/Sub bass"));
    CHECK(s.isPreviewing());
    CHECK(s.previewLength() == Approx(2.0).margin(1.0));
    CHECK(previewPeak(s) > 0.01f);
    CHECK_FALSE(s.previewPreset("compressor", "factory/Glue")); // effects don't preview
    CHECK_FALSE(s.previewPreset("subtractive-synth", "factory/Nothing like this"));

    const auto t = f.r.track(0);
    CHECK_FALSE(s.previewMidiFile(f.place.getChildFile("Groove.mid"), t)); // no instrument
    REQUIRE(s.addDevice(t, "fd:subtractive-synth") != 0);
    s.updateSnapshotNow();
    CHECK(s.previewMidiFile(f.place.getChildFile("Groove.mid"), t));
    CHECK(s.isPreviewing());
    s.stopPreview();
    CHECK_FALSE(s.isPreviewing());
}

TEST_CASE("browser engine: an instrument replaces the track's in one step; a sample loads into the Sampler",
          "[integration]")
{
    Files f;
    auto& s = *f.r.session;
    const auto t = f.r.track(0);
    const auto sampler = s.addDevice(t, "fd:sampler");
    REQUIRE(sampler != 0);
    REQUIRE(s.addDevice(t, "fd:eq") != 0);
    s.updateSnapshotNow();

    // A sample: its length back, one step.
    const auto seconds = s.loadSample(sampler, f.place.getChildFile("Kick.wav"));
    REQUIRE(seconds);
    CHECK(*seconds == Approx(0.5).margin(0.001));
    CHECK(s.undoDescription() == "Load sample Kick");
    std::string error;
    CHECK_FALSE(s.loadSample(sampler, f.place.getChildFile("notes.txt"), &error));
    CHECK_FALSE(error.empty());

    // The instrument: in the Sampler's place, the EQ after it, one step.
    const auto synth = s.replaceInstrument(t, "fd:subtractive-synth");
    REQUIRE(synth != 0);
    s.updateSnapshotNow();
    const auto& devices = s.snapshot()->channel(t)->devices;
    REQUIRE(devices.size() == 2);
    CHECK(devices[0].deviceId == "subtractive-synth");
    CHECK(devices[1].deviceId == "eq");
    CHECK(s.undoDescription() == "Replace Sampler with Subtractive");
    s.undo();
    s.updateSnapshotNow();
    CHECK(s.snapshot()->channel(t)->devices[0].deviceId == "sampler");
    // On a track with none: at the start of its chain.
    const auto t1 = f.r.track(1);
    REQUIRE(s.addDevice(t1, "fd:eq") != 0);
    REQUIRE(s.replaceInstrument(t1, "fd:sampler") != 0);
    s.updateSnapshotNow();
    CHECK(s.snapshot()->channel(t1)->devices[0].deviceId == "sampler");
}

TEST_CASE(
    "browser engine: wavetables in the Segno folder's Wavetables, previewed on a held note, loaded onto a "
    "Wavetable or onto a track with one (table-editor.md §7)",
    "[integration]")
{
    Files f;
    auto& s = *f.r.session;
    const auto folder = f.r.root.getChildFile("Segno").getChildFile("Wavetables");
    REQUIRE(folder.isDirectory()); // made with the place, as Samples is
    const auto vowels = fd::devices::wavetable::factoryTable("formant/vowels");
    REQUIRE(vowels != nullptr);
    const auto wav = folder.getChildFile("Vowels.wav"), wt = f.place.getChildFile("Vowels.wt");
    REQUIRE(fd::devices::wavetable::exportWav(*vowels, wav.getFullPathName().toStdString()));
    REQUIRE(fd::devices::wavetable::exportWt(*vowels, wt.getFullPathName().toStdString()));
    const auto other = folder.getChildFile("Other.wav");
    REQUIRE(fd::devices::wavetable::exportWav(*fd::devices::wavetable::factoryTable("basic/shapes"),
                                              other.getFullPathName().toStdString()));
    s.refreshBrowser(f.r.root.getChildFile("Segno"));
    s.refreshBrowser(f.place);
    s.waitForBrowserIndex();
    for (const auto* name : {"Vowels.wav", "Vowels.wt"})
    {
        INFO(name);
        const auto* indexed = f.indexed(name);
        REQUIRE(indexed != nullptr);
        CHECK(indexed->kind == model::BrowserKind::Wavetable);
        CHECK(indexed->frames == vowels->frames());
    }

    // Heard on a held note, through a Wavetable of its own.
    REQUIRE(s.previewWavetable(wav));
    CHECK(previewPeak(s) > 0.05f);
    s.stopPreview();
    CHECK_FALSE(s.previewWavetable(f.place.getChildFile("notes.txt")));

    // On a track: a Wavetable with it, in place of the track's instrument, one step.
    const auto t = f.r.track(0);
    REQUIRE(s.addDevice(t, "fd:sampler") != 0);
    s.updateSnapshotNow();
    const auto item = "file:" + wt.getFullPathName().toStdString();
    CHECK(fd::app::browser::fitsChannel(item, true));
    const auto onTrack = fd::app::browser::onChannel(s, item, t, 0.0);
    REQUIRE(onTrack.done);
    CHECK(onTrack.toast == "Replaced Sampler with Wavetable");
    CHECK(s.undoDescription() == "Replace Sampler with Wavetable");
    s.updateSnapshotNow();
    const auto devices = s.snapshot()->channel(t)->devices;
    REQUIRE(devices.size() == 1);
    CHECK(devices[0].deviceId == "wavetable");
    auto handle = s.openDevice(devices[0].id);
    const auto& synth = dynamic_cast<const fd::devices::WavetableSynth&>(*handle->context().device);
    CHECK(synth.tableId(0) == "user");
    CHECK(synth.tableName(0) == "Vowels");
    CHECK(synth.table(0)->frames() == vowels->frames());

    // On the Wavetable itself: loaded, one step.
    CHECK(fd::app::browser::landsOn(item, devices[0]));
    s.updateSnapshotNow();
    const auto onModule =
        fd::app::browser::inChain(s, "file:" + other.getFullPathName().toStdString(), t, -1, devices[0].id);
    CHECK(onModule.done);
    CHECK(s.undoDescription() == "Load Other");
    CHECK(synth.tableName(0) == "Other");
    std::string error;
    CHECK_FALSE(s.loadWavetable(devices[0].id, 0, f.place.getChildFile("notes.txt"), &error));
    CHECK_FALSE(error.empty());
    wav.deleteFile();
    other.deleteFile();
}

TEST_CASE("browser engine: Collect files copies what's outside into the project; unused files are found",
          "[integration]")
{
    Files f;
    auto& s = *f.r.session;
    const auto t = f.r.track(0);
    const auto song = f.r.root.getChildFile("Song").getChildFile("Song.segno");
    REQUIRE(s.saveAs(song));
    REQUIRE(s.insertAudioFile(t, f.place.getChildFile("Kick.wav"), 0.0) != 0);
    REQUIRE(s.insertAudioFile(t, f.place.getChildFile("Drums").getChildFile("Snare.wav"), 2.0) != 0);
    s.updateSnapshotNow();

    // Collect: copies into Samples, the clips point at the copies; the originals stay.
    const auto collected = s.collectFiles();
    CHECK(collected.copied == 2);
    CHECK(collected.failed.empty());
    s.updateSnapshotNow();
    for (const auto& c : s.snapshot()->channel(t)->clips)
        CHECK(juce::File(juce::String::fromUTF8(c.sourcePath.c_str()))
                  .isAChildOf(song.getParentDirectory().getChildFile("Samples")));
    CHECK(f.place.getChildFile("Kick.wav").existsAsFile());
    CHECK(s.collectFiles().copied == 0); // nothing outside any more
    REQUIRE(s.save());
    // The project moves as a whole and opens with every clip.
    const auto moved = f.r.root.getChildFile("Elsewhere");
    REQUIRE(song.getParentDirectory().moveFileTo(moved));
    REQUIRE(s.open(moved.getChildFile("Song.segno")));
    const auto reopened = f.r.info(f.r.track(0));
    REQUIRE(reopened.clips.size() == 2);
    for (const auto& c : reopened.clips)
    {
        INFO(c.sourcePath);
        CHECK(juce::File(juce::String::fromUTF8(c.sourcePath.c_str())).existsAsFile());
    }

    // Unused: a recording nothing uses; not a file a deleted clip used (undo could bring it back).
    const auto old = moved.getChildFile("Recordings").getChildFile("Old.wav");
    writeSine(old, 0.1);
    const auto clips = reopened.clips;
    s.deleteClips({clips.front().id});
    s.updateSnapshotNow();
    CHECK(s.unusedFiles() == std::vector<juce::File>{old});
    CHECK(s.removeUnusedFiles({old, juce::File(juce::String::fromUTF8(clips.front().sourcePath.c_str()))},
                              [](const juce::File& file) { return file.deleteFile(); }) == 1);
    CHECK_FALSE(old.exists());
    CHECK(juce::File(juce::String::fromUTF8(clips.front().sourcePath.c_str())).existsAsFile());
}
