// The browser panel (M3 C2, ADR 0046, docs/design/browser.md): the tree with modules as folders of
// their presets, places, search and tags, previews on selection, favourites, what a double-click
// and each drop make, Collect files, the compact rail, menus, and its look.

#include "RecordingFixture.h"
#include "TestLibrary.h"
#include "WavetableSynth.h"
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
    /// A click on the rail's button for a part (its tooltip names it).
    void railClick(const juce::String& part)
    {
        for (auto* c : panel().getChildren())
            if (auto* button = dynamic_cast<fd::ui::IconButton*>(c);
                button != nullptr && c->getName() == "browser part" && button->getTooltip() == part)
            {
                REQUIRE(button->isVisible());
                button->onClick();
                return;
            }
        FAIL("no rail button " << part);
    }
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
    CHECK(s.undoDescription() == "Add Subtractive");
    // Another instrument replaces it, and says so with Undo.
    shell.loadFromBrowser("module:fd:sampler");
    s.updateSnapshotNow();
    CHECK(devicesOf(*s.snapshot()->channel(t0)) == std::vector<std::string>{"sampler", "compressor"});
    CHECK(shell.lastWarning() == "Replaced Subtractive with Sampler");
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

TEST_CASE("browser: at compact widths a rail, opening docked at a part; nothing touches", "[integration]")
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
    const int arrangementX = shell.arrangement().getX();
    b.railClick("Instruments");
    // It opens as its toggle opens it, easing out of its rail; the toggle's button and the menu
    // say it's open (one state, whichever way it opened).
    CHECK(p.isRail());
    CHECK(shell.actions().isToggled("view.browser"));
    shell.finishAnimations();
    CHECK_FALSE(p.isRail());
    CHECK(p.getWidth() > fd::ui::Theme::current().shell.railWidth);
    CHECK(shell.arrangement().getX() >= p.getRight()); // docked: the arrangement makes room
    CHECK(shell.arrangement().getX() > arrangementX + 100);
    CHECK(b.shows("module:fd:sampler"));
    check();
    if (const char* dump = std::getenv("FD_DUMP_DIR"))
        dumpPng(
            dump, "browser-from-rail@2x.png",
            shell.createComponentSnapshot(shell.getLocalBounds().withWidth(p.getRight() + 40), true, 2.0f));
    REQUIRE(shell.actions().perform("view.browser")); // the toggle closes it
    CHECK_FALSE(p.isRail()); // it eases back into its rail, as it came out
    shell.finishAnimations();
    CHECK(p.isRail());
    if (const char* dump = std::getenv("FD_DUMP_DIR"))
        dumpPng(dump, "browser-rail@2x.png", shell.createComponentSnapshot(p.getBounds(), true, 2.0f));
}

TEST_CASE("browser: the Library is a place before User library; its tables heard, found as the Library's, "
          "put in by their factory ID (ADR 0055)",
          "[integration]")
{
    Browser b;
    auto& p = b.panel();
    // Not installed: said, never silent; nothing to open.
    REQUIRE(b.shows("place:library"));
    const auto segno = b.r.root.getChildFile("Segno");
    {
        const auto ids = p.rowIds();
        const auto names = p.rowNames();
        const auto at = [&ids](const std::string& id)
        { return static_cast<size_t>(std::find(ids.begin(), ids.end(), id) - ids.begin()); };
        CHECK(names[at("place:library")] == "Library");
        CHECK(names[at("place:" + path(segno))] == "User library");
        CHECK(at("place:library") < at("place:" + path(segno)));
    }
    if (const char* dump = std::getenv("FD_DUMP_DIR"))
        dumpPng(dump, "browser-library-missing@2x.png",
                b.shell->createComponentSnapshot(p.getBounds(), true, 2.0f));

    // Pointed to in Settings > Library: a place, before your Segno folder.
    const auto library = writeTestLibrary(b.r.root.getChildFile("Library"));
    b.store->change([&](fd::model::AppSettings& s) { s.library.location = path(library); });
    b.r.session->waitForBrowserIndex();
    b.settle();
    CHECK(b.r.session->libraryFolder() == library);
    CHECK_FALSE(b.shows("place:library"));
    {
        const auto ids = p.rowIds();
        const auto lib = std::find(ids.begin(), ids.end(), "place:" + path(library));
        const auto yours = std::find(ids.begin(), ids.end(), "place:" + path(segno));
        REQUIRE(lib != ids.end());
        CHECK(lib < yours);
    }
    // The Wavetable's folder holds its presets only now.
    b.open("cat:instruments");
    b.open("module:fd:wavetable");
    CHECK_FALSE(b.shows("tables:fd:wavetable"));
    CHECK(b.shows("pfolder:fd:wavetable|Pad"));

    // Its tables are files: Wavetables, a folder a category.
    const auto cello = library.getChildFile("Wavetables").getChildFile("Acoustic").getChildFile("Cello.wav");
    b.open("place:" + path(library));
    b.open("folder:" + path(library.getChildFile("Wavetables")));
    b.open("folder:" + path(cello.getParentDirectory()));
    REQUIRE(b.shows("file:" + path(cello)));
    // Selected, one plays on a held note; the drag carries it.
    REQUIRE(p.select("file:" + path(cello)));
    b.settle();
    CHECK(b.r.session->isPreviewing());
    if (const char* dump = std::getenv("FD_DUMP_DIR"))
        dumpPng(dump, "browser-library@2x.png", b.shell->createComponentSnapshot(p.getBounds(), true, 2.0f));
    CHECK_FALSE(p.dragDescription("file:" + path(cello)).isVoid());
    b.r.session->stopPreview();

    // Found by search among the wavetables, saying it's the Library's.
    b.search("cello");
    CHECK(b.shows("group:WAVETABLES"));
    CHECK(b.shows("file:" + path(cello)));
    if (const char* dump = std::getenv("FD_DUMP_DIR"))
        dumpPng(dump, "browser-library-search@2x.png",
                b.shell->createComponentSnapshot(p.getBounds(), true, 2.0f));
    b.search("");
    const auto items = b.r.session->browserItems();
    const auto item = std::find_if(items.begin(), items.end(),
                                   [&](const auto& i) { return i.id == "file:" + path(cello); });
    REQUIRE(item != items.end());
    CHECK(item->caption == "Library · Acoustic");

    // Put in on the selected track: a Wavetable with the factory table itself, by its ID, one step.
    const auto t0 = b.track(0).id;
    b.shell->arrangement().selectTrack(t0);
    b.settle();
    REQUIRE(p.select("file:" + path(cello)));
    p.loadSelected();
    b.settle();
    b.r.session->updateSnapshotNow();
    REQUIRE(devicesOf(b.track(0)) == std::vector<std::string>{"wavetable"});
    CHECK(b.r.session->undoDescription() == "Add Wavetable with Cello");
    auto handle = b.r.session->openDevice(b.track(0).devices[0].id);
    const auto& synth = dynamic_cast<const fd::devices::WavetableSynth&>(*handle->context().device);
    CHECK(synth.tableId(0) == "acoustic/cello");
    // Onto that Wavetable: loaded where OSC shows, one step.
    const auto voice =
        "file:" +
        path(library.getChildFile("Wavetables").getChildFile("Formant").getChildFile("Human Voice.wav"));
    CHECK(fd::app::browser::landsOn(voice, b.track(0).devices[0]));
    const auto done = fd::app::browser::inChain(*b.r.session, voice, t0, -1, b.track(0).devices[0].id);
    CHECK(done.done);
    CHECK(synth.tableId(0) == "formant/human-voice");
    CHECK(b.r.session->undoDescription() == "Load Human Voice");

    // Its racks (racks.md §18.8) in Racks, before yours, saying where in it they are.
    {
        const auto riser =
            library.getChildFile("Racks").getChildFile("Transitions").getChildFile("Riser.segnorack");
        REQUIRE(riser.getParentDirectory().createDirectory());
        auto& s = *b.r.session;
        const auto mine = s.groupIntoRack({s.addDevice(b.track(0).id, "fd:saturator")});
        REQUIRE(s.saveRack(mine, riser)); // as a factory rack would be, in the Library
        REQUIRE(s.saveRack(mine, s.racksFolder().getChildFile("Mine.segnorack")));
        s.refreshBrowser(library);
        s.refreshBrowser(s.racksFolder());
        s.waitForBrowserIndex();
        b.settle();
        b.open("cat:racks");
        const auto ids = p.rowIds();
        const auto captions = p.rowCaptions();
        const auto at = [&ids](const std::string& id)
        { return static_cast<size_t>(std::find(ids.begin(), ids.end(), id) - ids.begin()); };
        const auto factory = at("file:" + path(riser)),
                   yours = at("file:" + path(s.racksFolder().getChildFile("Mine.segnorack")));
        REQUIRE(factory < ids.size());
        REQUIRE(yours < ids.size());
        CHECK(factory < yours);
        CHECK(captions[factory] == juce::String::fromUTF8("Library \xc2\xb7 Transitions"));
        CHECK(captions[yours].isEmpty());
        if (const char* dump = std::getenv("FD_DUMP_DIR"))
            dumpPng(dump, "browser-factory-racks@2x.png",
                    b.shell->createComponentSnapshot(p.getBounds(), true, 2.0f));
    }

    // Pointed somewhere it isn't: said.
    b.store->change([&](fd::model::AppSettings& s)
                    { s.library.location = path(b.r.root.getChildFile("Gone")); });
    b.settle();
    CHECK(b.shows("place:library"));
    b.store->change([](fd::model::AppSettings& s) { s.library.location.clear(); });
}

TEST_CASE("browser: a row's name comes before its caption, which shows only when both fit whole",
          "[integration]")
{
    const int height = fd::ui::Theme::current().shell.rowHeight;
    const juce::Rectangle<int> wide{0, 0, 600, height}, narrow{0, 0, 160, height};
    fd::ui::TreeItem item;
    item.name = "Cello.wav";
    item.caption = "64 frames";
    item.depth = 3;
    const auto shown = fd::ui::treeRowCaption(wide, item);
    REQUIRE_FALSE(shown.isEmpty());
    CHECK(shown.getRight() < fd::ui::treeRowStar(wide).getX()); // the star's column kept
    CHECK(fd::ui::treeRowCaption(narrow, item).isEmpty());      // the name needs the room
    item.name = "Kick";
    item.caption = "0:01";
    item.depth = 1;
    CHECK_FALSE(fd::ui::treeRowCaption(narrow, item).isEmpty());
}

TEST_CASE(
    "browser: an SFZ file is a multisample: found as one, its zones its caption, heard as a phrase, put "
    "in a Sampler (devices/sampler/SPEC.md, S11)",
    "[integration]")
{
    Browser b;
    auto& p = b.panel();
    const auto folder = b.sounds.getChildFile("Strings");
    writeSine(folder.getChildFile("Low.wav"), 0.5);
    writeSine(folder.getChildFile("High.wav"), 0.5);
    const auto sfz = folder.getChildFile("Violins.sfz");
    REQUIRE(sfz.replaceWithText("<region> sample=Low.wav hikey=59\n<region> sample=High.wav lokey=60\n"));
    b.r.session->refreshBrowser(b.sounds);
    b.r.session->waitForBrowserIndex();
    b.settle();
    const auto id = "file:" + path(sfz);

    b.search("violins");
    CHECK(b.shows("group:MULTISAMPLES"));
    REQUIRE(b.shows(id));
    b.search("");
    b.open("place:" + path(b.sounds));
    b.open("folder:" + path(folder));
    REQUIRE(b.shows(id));
    REQUIRE(p.select(id));
    b.settle();
    CHECK(b.r.session->isPreviewing());
    CHECK_FALSE(p.dragDescription(id).isVoid());
    if (const char* dump = std::getenv("FD_DUMP_DIR"))
        dumpPng(dump, "browser-multisample@2x.png",
                b.shell->createComponentSnapshot(p.getBounds(), true, 2.0f));
    b.r.session->stopPreview();

    // Put on the selected track: a Sampler with it, one step.
    const auto t0 = b.track(0).id;
    b.shell->arrangement().selectTrack(t0);
    b.settle();
    REQUIRE(p.select(id));
    p.loadSelected();
    b.settle();
    b.r.session->updateSnapshotNow();
    REQUIRE(devicesOf(b.track(0)) == std::vector<std::string>{"sampler"});
    CHECK(b.r.session->undoDescription() == "Add Sampler with Violins");
}

TEST_CASE("browser: the Sampler's folder holds its instruments' presets by family (factory-library.md §7)",
          "[integration]")
{
    Browser b;
    auto& p = b.panel();
    b.open("cat:instruments");
    b.open("module:fd:sampler");
    for (const auto* family : {"Brass", "Drums", "Keys", "Lead", "Pad", "Pluck", "Strings", "Woodwinds"})
        CHECK(b.shows(std::string("pfolder:fd:sampler|") + family));
    b.open("pfolder:fd:sampler|Strings");
    CHECK(b.shows("preset:fd:sampler|factory/Violins"));
    CHECK(b.shows("preset:fd:sampler|factory/Harp"));
    if (const char* dump = std::getenv("FD_DUMP_DIR"))
        dumpPng(dump, "browser-sampler-presets@2x.png",
                b.shell->createComponentSnapshot(p.getBounds(), true, 2.0f));
}

TEST_CASE(
    "browser: Racks a part of its own, instrument and effect racks together; each part's icon in the rail "
    "and the tree (ADR 0059)",
    "[integration]")
{
    Browser b;
    auto& s = *b.r.session;
    auto& p = b.panel();
    // An effect rack and an instrument rack, saved into your racks.
    const auto effect = s.groupIntoRack({s.addDevice(b.track(0).id, "fd:saturator")});
    const auto instrument = s.groupIntoRack({s.addDevice(b.track(1).id, "fd:subtractive-synth")});
    REQUIRE(s.renameRack(effect, "Heat"));
    REQUIRE(s.renameRack(instrument, "Layers"));
    REQUIRE(s.saveRack(effect, s.racksFolder().getChildFile("Heat.segnorack")));
    REQUIRE(s.saveRack(instrument, s.racksFolder().getChildFile("Layers.segnorack")));
    s.refreshBrowser(s.racksFolder());
    s.waitForBrowserIndex();
    b.settle();

    // The tree: Favourites, Instruments, Audio effects, Modulators (modulators.md §14), Racks, then
    // Places; the racks together.
    const auto heat = "file:" + path(s.racksFolder().getChildFile("Heat.segnorack"));
    const auto layers = "file:" + path(s.racksFolder().getChildFile("Layers.segnorack"));
    b.open("cat:instruments");
    b.open("cat:effects");
    CHECK_FALSE(b.shows(heat));
    CHECK_FALSE(b.shows(layers));
    const auto ids = p.rowIds();
    const auto at = [&ids](const std::string& id)
    { return std::find(ids.begin(), ids.end(), id) - ids.begin(); };
    CHECK(at("cat:favourites") < at("cat:instruments"));
    CHECK(at("cat:instruments") < at("cat:effects"));
    CHECK(at("cat:effects") < at("cat:modulators"));
    CHECK(at("cat:modulators") < at("cat:racks"));
    CHECK(at("cat:racks") < at("heading:places"));
    CHECK_FALSE(b.shows("module:fd:mod-lfo")); // not among the audio effects
    b.open("cat:racks");
    CHECK(b.shows(heat));
    CHECK(b.shows(layers));
    if (const char* dump = std::getenv("FD_DUMP_DIR"))
        dumpPng(dump, "browser-racks@2x.png", b.shell->createComponentSnapshot(p.getBounds(), true, 2.0f));
    // Selected, an instrument rack plays the presets' phrase through all its chains; an effect rack
    // waits for effects' previews (racks.md §18.9).
    REQUIRE(p.select(layers));
    CHECK(s.isPreviewing());
    s.stopPreview();
    REQUIRE(p.select(heat));
    CHECK_FALSE(s.isPreviewing());
    // Search finds them as racks.
    b.search("heat");
    CHECK(b.shows(heat));
    b.search("");

    // The rail: a button a part, Racks among them, each icon drawn as large as the tree's.
    b.shell->setSize(1280, 900);
    b.settle();
    REQUIRE(p.isRail());
    std::vector<fd::ui::IconButton*> rail;
    for (auto* c : p.getChildren())
        if (auto* button = dynamic_cast<fd::ui::IconButton*>(c);
            button != nullptr && c->getName() == "browser part")
            rail.push_back(button);
    CHECK(rail.size() == 8);
    if (const char* dump = std::getenv("FD_DUMP_DIR"))
        dumpPng(dump, "browser-rail-racks@2x.png",
                b.shell->createComponentSnapshot(p.getBounds(), true, 2.0f));
    b.railClick("Racks");
    CHECK(b.isOpen("cat:racks"));
    CHECK(b.shows(heat));
    REQUIRE(b.shell->actions().perform("view.browser"));
    bool modulatorsInRail = false;
    for (auto* button : rail)
        modulatorsInRail = modulatorsInRail || button->getTooltip() == "Modulators";
    CHECK(modulatorsInRail);
}

TEST_CASE("browser: Modulators a part of its own, each a folder of its presets (modulators.md §14)",
          "[integration][browser][modulators]")
{
    Browser b;
    auto& p = b.panel();
    b.open("cat:modulators");
    for (const auto* type : {"mod-lfo", "mod-random", "mod-envelope", "mod-follower", "mod-steps"})
        CHECK(b.shows("module:fd:" + std::string(type)));
    b.open("module:fd:mod-lfo");
    CHECK(b.shows("preset:fd:mod-lfo|factory/Slow sweep"));
    if (const char* dump = std::getenv("FD_DUMP_DIR"))
        dumpPng(dump, "browser-modulators@2x.png",
                b.shell->createComponentSnapshot(p.getBounds(), true, 2.0f));
    // Search finds them, and their presets.
    b.search("sweep");
    CHECK(b.shows("preset:fd:mod-lfo|factory/Slow sweep"));
    b.search("");
}

namespace
{
/// A press of the left button on `c` at `pos`, the `clicks`-th of quick clicks there.
juce::MouseEvent press(juce::Component& c, juce::Point<float> pos, int clicks)
{
    const auto now = juce::Time::getCurrentTime();
    return {juce::Desktop::getInstance().getMainMouseSource(),
            pos,
            juce::ModifierKeys::leftButtonModifier,
            0.0f,
            0.0f,
            0.0f,
            0.0f,
            0.0f,
            &c,
            &c,
            now,
            pos,
            now,
            clicks,
            false};
}
} // namespace

TEST_CASE(
    "browser: a quick second click on a folder toggles it once, never back on release; a double-click on a "
    "disclosure or a star loads nothing",
    "[integration]")
{
    Browser b;
    auto& p = b.panel();
    std::vector<std::string> loaded;
    p.onLoad = [&loaded](const std::string& item) { loaded.push_back(item); };
    // A click as JUCE sends it: down, up, then a double-click after the up of a second quick one.
    const auto click = [&p](const std::string& id, int clicks, float x = -1.0f)
    {
        const auto area = p.rowBounds(id);
        REQUIRE_FALSE(area.isEmpty());
        const auto at = juce::Point<float>(x >= 0.0f ? static_cast<float>(area.getX()) + x
                                                     : static_cast<float>(area.getCentreX()),
                                           static_cast<float>(area.getCentreY()));
        auto* list = p.getComponentAt(at.toInt());
        REQUIRE(list != nullptr);
        const auto local = list->getLocalPoint(&p, at);
        list->mouseDown(press(*list, local, clicks));
        list->mouseUp(press(*list, local, clicks));
        if (clicks >= 2)
            list->mouseDoubleClick(press(*list, local, clicks));
    };

    // Closed, then two quick clicks on its name: open, then closed, as two clicks are; the
    // double-click JUCE sends after the second doesn't toggle it a third time.
    REQUIRE_FALSE(b.isOpen("cat:instruments"));
    click("cat:instruments", 1);
    CHECK(b.isOpen("cat:instruments"));
    click("cat:instruments", 2);
    CHECK_FALSE(b.isOpen("cat:instruments"));
    // The bug the product owner met: a click soon after another reopens it and keeps it open.
    click("cat:instruments", 3);
    CHECK(b.isOpen("cat:instruments"));
    CHECK(loaded.empty());

    // A module: its disclosure opens it, and a quick second click there closes it, loading
    // nothing; a double-click on its name loads it.
    const auto& s = fd::ui::Theme::current().shell;
    const float chevron = static_cast<float>(s.listTextInset + s.browserIndent + s.listIconSize / 2);
    click("module:fd:sampler", 1, chevron);
    CHECK(b.isOpen("module:fd:sampler"));
    click("module:fd:sampler", 2, chevron);
    CHECK_FALSE(b.isOpen("module:fd:sampler"));
    CHECK(loaded.empty());
    click("module:fd:sampler", 1);
    click("module:fd:sampler", 2);
    CHECK(loaded == std::vector<std::string>{"module:fd:sampler"});
}

TEST_CASE("browser: opened from its rail, it docks as its toggle opens it, and the toggle closes it",
          "[integration]")
{
    Browser b;
    auto& shell = *b.shell;
    auto& p = b.panel();
    shell.setSize(1280, 900);
    b.settle();
    shell.finishAnimations();
    REQUIRE(p.isRail());
    REQUIRE_FALSE(shell.actions().isToggled("view.browser"));
    const int arrangementX = shell.arrangement().getX();

    // From the rail: open and docked, the arrangement making room, the toggle saying so.
    b.railClick("Favourites");
    shell.finishAnimations();
    CHECK(shell.actions().isToggled("view.browser"));
    CHECK_FALSE(p.isRail());
    const int dockedX = shell.arrangement().getX();
    CHECK(dockedX > arrangementX + 100);
    CHECK(dockedX >= p.getRight());

    // The toggle closes it, and opens it again just as the rail did.
    REQUIRE(shell.actions().perform("view.browser"));
    CHECK_FALSE(shell.actions().isToggled("view.browser"));
    shell.finishAnimations();
    CHECK(p.isRail());
    CHECK(shell.arrangement().getX() == arrangementX);
    REQUIRE(shell.actions().perform("view.browser"));
    shell.finishAnimations();
    CHECK(shell.actions().isToggled("view.browser"));
    CHECK(shell.arrangement().getX() == dockedX);
    REQUIRE(shell.actions().perform("view.browser"));
    shell.finishAnimations();
    CHECK(p.isRail());

    // Search the browser (Ctrl+F) from the rail: it opens the same way, the keyboard in its search.
    REQUIRE(shell.actions().perform("browser.search"));
    shell.finishAnimations();
    CHECK(shell.actions().isToggled("view.browser"));
    CHECK(shell.arrangement().getX() == dockedX);
    REQUIRE(shell.actions().perform("view.browser"));
    CHECK_FALSE(shell.actions().isToggled("view.browser"));

    // Focus mode hides the panels, and their toggles say so; a toggle then shows its panel and ends
    // focus mode (it never closes a panel you can't see).
    shell.setSize(1800, 960);
    shell.finishAnimations();
    REQUIRE(shell.actions().isToggled("view.detail"));
    REQUIRE(shell.actions().perform("view.focus"));
    CHECK_FALSE(shell.actions().isToggled("view.detail"));
    CHECK_FALSE(shell.actions().isToggled("view.browser"));
    REQUIRE(shell.actions().perform("view.detail"));
    CHECK_FALSE(shell.actions().isToggled("view.focus"));
    CHECK(shell.actions().isToggled("view.detail"));
}
