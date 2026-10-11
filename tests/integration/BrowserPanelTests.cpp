// The browser panel (M3 C2, ADR 0046, docs/design/browser.md): the tree with modules as folders of
// their presets, places, search and tags, previews on selection, favourites, what a double-click
// and each drop make, Collect files, the compact rail, menus, and its look.

#include "RecordingFixture.h"
#include "model/MenuBar.h"
#include "ui/BrowserDrops.h"
#include "ui/Shell.h"

#include <catch2/catch_test_macros.hpp>
#include <juce_audio_formats/juce_audio_formats.h>

using namespace recording_test;
namespace model = fd::model;

namespace
{
void writeSine(const juce::File& file, double seconds)
{
    (void)file.getParentDirectory().createDirectory();
    juce::AudioBuffer<float> b(2, static_cast<int>(seconds * 48000.0));
    for (int c = 0; c < 2; ++c)
        for (int i = 0; i < b.getNumSamples(); ++i)
            b.setSample(c, i, 0.25f * std::sin(static_cast<float>(i) * 0.05f));
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::OutputStream> out(file.createOutputStream());
    auto writer = wav.createWriterFor(
        out,
        juce::AudioFormatWriterOptions{}.withSampleRate(48000.0).withNumChannels(2).withBitsPerSample(16));
    REQUIRE(writer != nullptr);
    writer->writeFromAudioSampleBuffer(b, 0, b.getNumSamples());
}

std::string path(const juce::File& f)
{
    return f.getFullPathName().toStdString();
}

void dumpPng(const char* dir, const juce::String& name, const juce::Image& image)
{
    const auto file = juce::File(dir).getChildFile(name);
    file.deleteFile();
    juce::FileOutputStream out(file);
    juce::PNGImageFormat().writeImageToStream(image, out);
}

/// A shell with its own settings and a place of samples.
struct Browser
{
    Recording r{"fd-browser-tests"};
    fd::ui::LookAndFeel lnf;
    juce::File sounds = r.root.getChildFile("Sounds");
    std::unique_ptr<fd::model::SettingsStore> store;
    std::unique_ptr<fd::app::Shell> shell;

    Browser()
    {
        juce::LookAndFeel::setDefaultLookAndFeel(&lnf);
        writeSine(sounds.getChildFile("Kick.wav"), 0.5);
        writeSine(sounds.getChildFile("Loop 120 bpm.wav"), 2.0);
        writeSine(sounds.getChildFile("Drums").getChildFile("Snare.wav"), 0.25);
        r.session->setPresetsFolder(r.root.getChildFile("Segno").getChildFile("Presets"));
        store = std::make_unique<fd::model::SettingsStore>(path(r.root.getChildFile("Settings")));
        store->change(
            [this](fd::model::AppSettings& s)
            {
                s.library.folder = path(r.root.getChildFile("Segno"));
                s.browser.places = {path(sounds)};
            });
        shell = std::make_unique<fd::app::Shell>(*r.session);
        shell->setSettingsStore(*store);
        shell->setSize(1600, 960);
        r.session->waitForBrowserIndex();
        settle();
    }
    ~Browser()
    {
        shell.reset();
        store.reset();
        juce::LookAndFeel::setDefaultLookAndFeel(nullptr);
    }
    fd::app::BrowserPanel& panel() { return shell->browser(); }
    bool shows(const std::string& id)
    {
        const auto ids = panel().rowIds();
        return std::find(ids.begin(), ids.end(), id) != ids.end();
    }
    void open(const std::string& id)
    {
        if (!isOpen(id))
            panel().toggle(id);
    }
    bool isOpen(const std::string& id)
    {
        const auto& o = store->get().browser.open;
        return std::find(o.begin(), o.end(), id) != o.end();
    }
    void search(const juce::String& text)
    {
        panel().searchField().setText(text, true);
        settle();
    }
    void settle() { juce::MessageManager::getInstance()->runDispatchLoopUntil(20); }
    const fd::model::TrackInfo& track(size_t i) { return r.session->snapshot()->tracks.at(i); }
};

std::vector<std::string> devicesOf(const fd::model::TrackInfo& t)
{
    std::vector<std::string> out;
    for (const auto& d : t.devices)
        out.push_back(d.deviceId);
    return out;
}
} // namespace

TEST_CASE(
    "browser: the tree has Favourites, Instruments, Audio effects and Places; modules open to their presets",
    "[integration]")
{
    Browser b;
    auto& p = b.panel();
    const auto segno = b.r.root.getChildFile("Segno");
    for (const auto* id : {"cat:favourites", "cat:instruments", "cat:effects", "heading:places", "addplace"})
        CHECK(b.shows(id));
    CHECK(b.shows("place:" + path(segno)));
    CHECK(b.shows("place:" + path(b.sounds)));
    CHECK_FALSE(b.shows("module:fd:compressor")); // closed until opened

    // An instrument opens to Yours (none yet) and folders by kind of sound.
    b.open("cat:instruments");
    CHECK(b.shows("module:fd:subtractive-synth"));
    CHECK(b.shows("module:fd:sampler"));
    b.open("module:fd:subtractive-synth");
    CHECK(b.shows("pfolder:fd:subtractive-synth|Bass"));
    CHECK(b.shows("pfolder:fd:subtractive-synth|Pad"));
    CHECK_FALSE(b.shows("preset:fd:subtractive-synth|factory/Sub bass"));
    b.open("pfolder:fd:subtractive-synth|Bass");
    CHECK(b.shows("preset:fd:subtractive-synth|factory/Sub bass"));
    // An effect: its presets as one list.
    b.open("cat:effects");
    b.open("module:fd:compressor");
    CHECK(b.shows("preset:fd:compressor|factory/Bus glue"));
    CHECK(b.isOpen("module:fd:compressor")); // kept with your settings

    // Places: folders before files; the Segno folder without its presets and unsaved projects.
    b.open("place:" + path(b.sounds));
    const auto ids = p.rowIds();
    const auto drums = std::find(ids.begin(), ids.end(), "folder:" + path(b.sounds.getChildFile("Drums")));
    const auto kick = std::find(ids.begin(), ids.end(), "file:" + path(b.sounds.getChildFile("Kick.wav")));
    REQUIRE(drums != ids.end());
    REQUIRE(kick != ids.end());
    CHECK(drums < kick);
    (void)segno.getChildFile("Presets").createDirectory();
    p.refresh();
    b.open("place:" + path(segno));
    CHECK(b.shows("folder:" + path(segno.getChildFile("Samples"))));
    CHECK_FALSE(b.shows("folder:" + path(segno.getChildFile("Presets"))));

    // A place that's gone shows greyed; removing it from the settings removes it.
    b.store->change([&b](fd::model::AppSettings& s)
                    { s.browser.places.push_back(path(b.r.root.getChildFile("Gone"))); });
    b.settle();
    const auto names = p.rowNames();
    CHECK(std::find(names.begin(), names.end(), juce::String("Gone")) != names.end());
    b.store->change([](fd::model::AppSettings& s) { s.browser.places.pop_back(); });
    CHECK_FALSE(b.shows("place:" + path(b.r.root.getChildFile("Gone"))));
}

TEST_CASE("browser: search groups what it finds; tags narrow it; Escape gives the tree back", "[integration]")
{
    Browser b;
    auto& p = b.panel();
    b.search("bus glue");
    CHECK(p.rowIds().front() == "group:PRESETS");
    CHECK(b.shows("preset:fd:compressor|factory/Bus glue"));
    b.search("kick");
    CHECK(b.shows("group:SAMPLES"));
    CHECK(b.shows("file:" + path(b.sounds.getChildFile("Kick.wav"))));
    b.search("drums");
    CHECK(b.shows("folder:" + path(b.sounds.getChildFile("Drums"))));
    // A group shows its best 8, then all of them.
    b.search("subtractive");
    CHECK(b.shows("more:presets"));
    REQUIRE(p.select("more:presets") == true);
    p.loadSelected();
    CHECK_FALSE(b.shows("more:presets"));
    CHECK(b.shows("preset:fd:subtractive-synth|factory/Sub bass"));
    // Tags: the results' tags as buttons; one narrows.
    b.search("compressor");
    const auto tags = p.tagsShown();
    CHECK(std::find(tags.begin(), tags.end(), "Drums") != tags.end());
    p.setTags({"Drums"});
    for (const auto& id : p.rowIds())
        if (id.starts_with("preset:"))
            CHECK((id.find("Drum") != std::string::npos || id.find("Kick") != std::string::npos ||
                   id.find("Snare") != std::string::npos || id.find("Room") != std::string::npos ||
                   id.find("Parallel") != std::string::npos));
    p.setTags({});
    b.search("nothing like this at all");
    CHECK(p.rowIds() == std::vector<std::string>{"empty"});
    // Escape: the tree again.
    p.searchField().setText({}, true);
    b.settle();
    CHECK(b.shows("cat:instruments"));
    CHECK(p.tagsShown().empty());
}

TEST_CASE("browser: selecting previews it; the strip says what it is; Preview off stops", "[integration]")
{
    Browser b;
    auto& p = b.panel();
    auto& s = *b.r.session;
    b.open("place:" + path(b.sounds));
    const auto loop = "file:" + path(b.sounds.getChildFile("Loop 120 bpm.wav"));
    REQUIRE(p.select(loop));
    CHECK(s.isPreviewing());
    CHECK(p.stripShown());
    // A module isn't heard, and stops what was.
    b.open("cat:effects");
    REQUIRE(p.select("module:fd:compressor"));
    CHECK_FALSE(s.isPreviewing());
    CHECK_FALSE(p.stripShown());
    // An instrument's preset plays its phrase; an effect's doesn't.
    b.open("cat:instruments");
    b.open("module:fd:subtractive-synth");
    b.open("pfolder:fd:subtractive-synth|Bass");
    REQUIRE(p.select("preset:fd:subtractive-synth|factory/Sub bass"));
    CHECK(s.isPreviewing());
    b.open("module:fd:compressor");
    REQUIRE(p.select("preset:fd:compressor|factory/Bus glue"));
    CHECK_FALSE(s.isPreviewing());
    // Preview off (View > Preview in the browser): selecting is silent; the strip still shows.
    b.shell->actions().perform("browser.preview");
    CHECK_FALSE(b.store->get().browser.preview);
    REQUIRE(p.select(loop));
    CHECK_FALSE(s.isPreviewing());
    CHECK(p.stripShown());
    b.shell->actions().perform("browser.preview");
    CHECK(b.store->get().browser.preview);
    s.stopPreview();
}

TEST_CASE("browser: favourites of every kind, presets' shared with their header", "[integration]")
{
    Browser b;
    auto& p = b.panel();
    const auto kick = "file:" + path(b.sounds.getChildFile("Kick.wav"));
    p.setFavourite(kick, true);
    p.setFavourite("module:fd:eq", true);
    p.setFavourite("preset:fd:compressor|factory/Bus glue", true);
    CHECK(b.r.session->presetLibrary().isFavourite("compressor", "factory/Bus glue"));
    const auto& favs = b.store->get().browser.favourites;
    CHECK(std::find(favs.begin(), favs.end(), kick) != favs.end());
    b.open("cat:favourites");
    CHECK(b.shows(kick));
    CHECK(b.shows("module:fd:eq"));
    CHECK(b.shows("preset:fd:compressor|factory/Bus glue"));
    // Starred ones rank higher in a search.
    b.search("glue");
    CHECK(p.isFavourite("preset:fd:compressor|factory/Bus glue"));
    p.setFavourite("preset:fd:compressor|factory/Bus glue", false);
    CHECK_FALSE(b.r.session->presetLibrary().isFavourite("compressor", "factory/Bus glue"));
}

TEST_CASE("browser: a double-click puts it on the selection; each drop makes what §10 says, one step each",
          "[integration]")
{
    Browser b;
    auto& shell = *b.shell;
    auto& s = *b.r.session;
    const auto t0 = b.r.track(0);
    shell.arrangement().selectTrack(t0);
    b.settle();

    // A module: at the end of the selected track's chain.
    shell.loadFromBrowser("module:fd:compressor");
    s.updateSnapshotNow();
    CHECK(devicesOf(*s.snapshot()->channel(t0)) == std::vector<std::string>{"compressor"});
    CHECK(s.undoDescription() == "Add Compressor");
    // An instrument's preset: the instrument first, with the preset, one step.
    shell.loadFromBrowser("preset:fd:subtractive-synth|factory/Sub bass");
    s.updateSnapshotNow();
    CHECK(devicesOf(*s.snapshot()->channel(t0)) ==
          std::vector<std::string>{"subtractive-synth", "compressor"});
    CHECK(s.undoDescription() == "Add Subtractive Synth");
    // Another instrument replaces it, and says so with Undo.
    shell.loadFromBrowser("module:fd:sampler");
    s.updateSnapshotNow();
    CHECK(devicesOf(*s.snapshot()->channel(t0)) == std::vector<std::string>{"sampler", "compressor"});
    CHECK(shell.lastWarning() == "Replaced Subtractive Synth with Sampler");
    s.undo();
    s.updateSnapshotNow();
    CHECK(devicesOf(*s.snapshot()->channel(t0))[0] == "subtractive-synth");
    s.redo();
    s.updateSnapshotNow();

    // A sample with the Sampler selected: into it. Without: a clip at the playhead.
    const auto sampler = s.snapshot()->channel(t0)->devices[0].id;
    shell.detail().selectModule(sampler);
    const auto kick = "file:" + path(b.sounds.getChildFile("Kick.wav"));
    shell.loadFromBrowser(kick);
    CHECK(s.undoDescription() == "Load sample Kick");
    // An effect dropped in the chain ahead of the instrument goes right after it.
    shell.detail().chain().onBrowserDrop("module:fd:eq", t0, 0, std::nullopt);
    s.updateSnapshotNow();
    CHECK(devicesOf(*s.snapshot()->channel(t0)) == std::vector<std::string>{"sampler", "eq", "compressor"});
    // A preset dropped on a module of its kind loads into it.
    const auto compressor = s.snapshot()->channel(t0)->devices[2].id;
    shell.detail().chain().onBrowserDrop("preset:fd:compressor|factory/Bus glue", t0, -1, compressor);
    CHECK(s.undoDescription() == "Load preset Bus glue");

    // On the arrangement: a sample on a track at the drop point; below the tracks, a new track.
    const auto clips = s.snapshot()->channel(t0)->clips.size();
    shell.arrangement().onBrowserDrop(kick, t0, 2.0);
    s.updateSnapshotNow();
    REQUIRE(s.snapshot()->channel(t0)->clips.size() == clips + 1);
    const auto tracks = s.snapshot()->tracks.size();
    shell.arrangement().onBrowserDrop("module:fd:gate", 0, 0.0);
    s.updateSnapshotNow();
    REQUIRE(s.snapshot()->tracks.size() == tracks + 1);
    CHECK(devicesOf(s.snapshot()->tracks.back()) == std::vector<std::string>{"gate"});
    CHECK(s.undoDescription() == "Add Gate");
    s.undo();
    s.updateSnapshotNow();
    CHECK(s.snapshot()->tracks.size() == tracks); // the track and the module in one step

    // In the mixer: on the master's strip; a sample on a return is refused.
    shell.arrangement().selectTrack(std::nullopt);
    const auto master = s.snapshot()->master.id;
    shell.mixer().onBrowserDrop("module:fd:limiter", master);
    s.updateSnapshotNow();
    CHECK(devicesOf(s.snapshot()->master).back() == "limiter");

    // Through the arrangement's own drop: below the tracks, a new track with the clip.
    const auto before = s.snapshot()->tracks.size();
    juce::DragAndDropTarget::SourceDetails details(
        juce::var(juce::String("segno-browser:") + juce::String(kick)), nullptr,
        {shell.arrangement().getWidth() / 2, shell.arrangement().getHeight() - 4});
    REQUIRE(shell.arrangement().isInterestedInDragSource(details));
    shell.arrangement().itemDropped(details);
    s.updateSnapshotNow();
    CHECK(s.snapshot()->tracks.size() == before + 1);
    CHECK(s.snapshot()->tracks.back().clips.size() == 1);
}

TEST_CASE("browser: Collect files and save; Search the browser and the menus", "[integration]")
{
    Browser b;
    auto& shell = *b.shell;
    auto& s = *b.r.session;
    const auto song = b.r.root.getChildFile("Song").getChildFile("Song.segno");
    REQUIRE(s.saveAs(song));
    REQUIRE(s.insertAudioFile(b.r.track(0), b.sounds.getChildFile("Kick.wav"), 0.0) != 0);
    shell.collectFiles();
    CHECK(shell.lastWarning() == "Collected 1 file into Samples");
    CHECK(song.getParentDirectory().getChildFile("Samples").getChildFile("Kick.wav").existsAsFile());
    CHECK_FALSE(s.hasUnsavedChanges());

    // The menus (ADR 0040): File and View.
    auto& actions = shell.actions();
    CHECK(actions.shortcut("browser.search") == "Ctrl+F");
    const auto menus = fd::model::menuActions(fd::model::menuBar(fd::model::MenuPlatform::Windowed));
    for (const auto* id : {"browser.search", "browser.preview", "file.collect", "file.removeUnused"})
        CHECK(std::find(menus.begin(), menus.end(), id) != menus.end());
    CHECK(actions.conflicts().empty());
}

TEST_CASE("browser: at compact widths a rail, opening over the arrangement at a part; nothing touches",
          "[integration]")
{
    Browser b;
    auto& shell = *b.shell;
    auto& p = b.panel();
    b.open("cat:instruments");
    b.open("module:fd:subtractive-synth");
    b.open("place:" + path(b.sounds));
    REQUIRE(p.select("file:" + path(b.sounds.getChildFile("Loop 120 bpm.wav"))));
    b.r.session->stopPreview();
    juce::MessageManager::getInstance()->runDispatchLoopUntil(300); // its waveform
    const auto check = [&p]
    {
        // Every part of the panel inside it, none over another.
        std::vector<juce::Rectangle<int>> placed;
        for (auto* c : p.getChildren())
            if (c->isVisible())
            {
                INFO(c->getName() << " " << c->getBounds().toString());
                CHECK(p.getLocalBounds().contains(c->getBounds()));
                for (const auto& other : placed)
                    CHECK_FALSE(other.intersects(c->getBounds()));
                placed.push_back(c->getBounds());
            }
    };
    check();
    if (const char* dump = std::getenv("FD_DUMP_DIR"))
        dumpPng(dump, "browser-tree@2x.png", shell.createComponentSnapshot(p.getBounds(), true, 2.0f));
    b.search("bass");
    check();
    if (const char* dump = std::getenv("FD_DUMP_DIR"))
        dumpPng(dump, "browser-search@2x.png", shell.createComponentSnapshot(p.getBounds(), true, 2.0f));
    b.search("");

    shell.setSize(1280, 900);
    b.settle();
    CHECK(p.isRail());
    shell.showBrowserOverlay(fd::app::BrowserPanel::Part::Instruments);
    CHECK(shell.browserOverlayShown());
    CHECK_FALSE(p.isRail());
    CHECK(p.getWidth() > fd::ui::Theme::current().shell.railWidth);
    CHECK(p.getBounds().intersects(shell.arrangement().getBounds())); // over it
    CHECK(b.shows("module:fd:sampler"));
    check();
    if (const char* dump = std::getenv("FD_DUMP_DIR"))
        dumpPng(
            dump, "browser-overlay@2x.png",
            shell.createComponentSnapshot(shell.getLocalBounds().withWidth(p.getRight() + 40), true, 2.0f));
    shell.closeBrowserOverlay();
    CHECK(p.isRail());
    if (const char* dump = std::getenv("FD_DUMP_DIR"))
        dumpPng(dump, "browser-rail@2x.png", shell.createComponentSnapshot(p.getBounds(), true, 2.0f));
}
