// The Drum Kit in the chain view (docs/design/drum-kit.md §5, §6, §8, §12; step 1): its panel, its
// selected pad's modules in its frame with the frame naming the pad; a pad pressed selects the kit
// and the pad and plays it; browser items dropped on pads and into the frame; a pad's settings from
// PAD; the Module menu's pad commands; pads lit as they're hit. Drawn with FD_DUMP_DIR.
#include "engine/DevicePlugin.h"
#include "engine/EngineBehaviour.h"
#include "engine/Session.h"
#include "fd/testkit/Signals.h"
#include "fd/testkit/WavIO.h"
#include "model/DrumKit.h"
#include "model/Racks.h"
#include "ui/BrowserDrops.h"
#include "ui/DeviceChain.h"
#include "ui/DrumKitPanel.h"

#include <catch2/catch_test_macros.hpp>
#include <cstdlib>
#include <fd_device_ui/fd_device_ui.h>
#include <tracktion_engine/testing/tracktion_EnginePlayer.h>
#include <tracktion_engine/tracktion_engine.h>

using fd::engine::Session;
namespace model = fd::model;

namespace
{
void dump(const juce::String& name, juce::Component& c)
{
    if (const char* dir = std::getenv("FD_DUMP_DIR"))
        for (const float scale : {1.0f, 2.0f})
        {
            const auto file = juce::File(dir).getChildFile(name + (scale > 1.0f ? "@2x.png" : "@1x.png"));
            file.deleteFile();
            juce::FileOutputStream stream(file);
            juce::PNGImageFormat().writeImageToStream(
                c.createComponentSnapshot(c.getLocalBounds(), true, scale), stream);
        }
}

struct SegnoLook
{
    fd::ui::LookAndFeel lnf;
    SegnoLook() { juce::LookAndFeel::setDefaultLookAndFeel(&lnf); }
    ~SegnoLook() { juce::LookAndFeel::setDefaultLookAndFeel(nullptr); }
    SegnoLook(const SegnoLook&) = delete;
    SegnoLook& operator=(const SegnoLook&) = delete;
};

template <typename T> void collect(juce::Component& root, std::vector<T*>& out)
{
    for (auto* c : root.getChildren())
    {
        if (auto* t = dynamic_cast<T*>(c))
            out.push_back(t);
        collect(*c, out);
    }
}

/// The chain view on a session with a Drum Kit on its first track.
struct KitChain
{
    juce::ScopedJuceInitialiser_GUI init;
    SegnoLook look;
    tracktion::Engine engine{"fd-kit-chain-tests", nullptr, fd::engine::makeEngineBehaviour()};
    std::unique_ptr<Session> session;
    fd::app::ActionRegistry actions;
    juce::Component window;
    std::unique_ptr<fd::app::DeviceChain> chain;
    juce::File root = juce::File::getSpecialLocation(juce::File::tempDirectory)
                          .getChildFile("fd-kit-chain-" + juce::Uuid().toString());
    std::uint64_t t = 0, kit = 0;

    KitChain()
    {
        fd::engine::registerDevices(engine);
        session = std::make_unique<Session>(engine);
        session->setUnsavedRoot(root.getChildFile("Unsaved"));
        session->setPresetsFolder(root.getChildFile("Presets"));
        (void)root.createDirectory();
        session->updateSnapshotNow();
        t = session->snapshot()->tracks[0].id;
        actions.add({.id = "app.scanPlugins", .name = "Scan", .perform = [] {}});
        actions.add({.id = "module.ungroupRack", .name = "Ungroup rack", .perform = [] {}});
        chain = std::make_unique<fd::app::DeviceChain>(*session, actions);
        window.setSize(2400, 800);
        window.addAndMakeVisible(*chain);
        chain->setBounds(0, window.getHeight() - fd::app::DeviceChain::preferredHeight(), window.getWidth(),
                         fd::app::DeviceChain::preferredHeight());
        session->setMidiInputTarget(t);
        kit = session->addDevice(t, "fd:drum-kit");
        REQUIRE(kit != 0);
        show();
    }

    ~KitChain()
    {
        chain.reset();
        session.reset();
        root.deleteRecursively();
    }

    void show()
    {
        session->updateSnapshotNow();
        chain->setTrack(*session->snapshot()->channel(t));
    }

    juce::File sample(const std::string& name)
    {
        const auto file = root.getChildFile(name + ".wav");
        REQUIRE(fd::testkit::writeWav(file.getFullPathName().toStdString(),
                                      fd::testkit::sine(80.0, 48000.0, 9600, 0.5f, 1), 48000.0));
        return file;
    }

    model::RackInfo info()
    {
        session->updateSnapshotNow();
        const auto* d = model::findDevice(session->snapshot()->channel(t)->devices, kit);
        REQUIRE(d != nullptr);
        REQUIRE(d->rack != nullptr);
        return *d->rack;
    }

    fd::app::DrumKitPanel& panel()
    {
        std::vector<fd::app::DrumKitPanel*> found;
        collect(*chain, found);
        REQUIRE(found.size() == 1);
        return *found[0];
    }

    /// The panels shown after the kit's, left to right (its selected pad's modules first).
    std::vector<fd::ui::DevicePanel*> panelsAfterKit()
    {
        std::vector<fd::ui::DevicePanel*> panels;
        collect(*chain, panels);
        std::erase_if(panels, [&](auto* p) { return !p->isVisible() || p == &panel(); });
        std::sort(panels.begin(), panels.end(), [](auto* a, auto* b) { return a->getX() < b->getX(); });
        return panels;
    }

    /// A browser item dropped at a pad (as the browser's drag does).
    void dropOnPad(const std::string& item, int note)
    {
        auto& pads = panel().pads();
        const auto at = chain->getLocalPoint(&pads, pads.padBounds(note).getCentre());
        chain->itemDropped(
            {juce::var(juce::String(std::string(fd::app::browser::dragPrefix) + item)), nullptr, at});
        show();
    }
};
} // namespace

TEST_CASE("drum kit in the chain: its panel, its pad's modules in its frame, the frame naming the pad",
          "[ui]")
{
    KitChain c;
    auto frames = c.chain->frames();
    REQUIRE(frames.size() == 1);
    CHECK(frames[0].text == "Pad C1"); // an empty pad's frame: waiting for a sound
    CHECK(c.panelsAfterKit().empty());
    dump("kit-chain-empty", *c.chain);

    c.session->loadPads(c.kit, 36, {c.sample("Kick")});
    c.session->addDevice(Session::ChainPlace{.rack = c.kit, .pad = 36}, "fd:compressor");
    c.show();
    frames = c.chain->frames();
    CHECK(frames[0].text == juce::String::fromUTF8("Pad C1 \xc2\xb7 Kick"));
    const auto shown = c.panelsAfterKit();
    REQUIRE(shown.size() >= 2); // its Sampler and Compressor, in its frame
    // The kit's panel shows its pads: the kick's pad has a sound.
    CHECK(c.panel().overview().from() == 36);
    dump("kit-chain-kick", *c.chain);
}

TEST_CASE("drum kit in the chain: a pad pressed selects the kit and the pad; PAD sets the pad", "[ui]")
{
    KitChain c;
    c.session->loadPads(c.kit, 36, {c.sample("Kick"), c.sample("Snare")});
    c.show();
    auto& pads = c.panel().pads();
    REQUIRE(pads.onPress);
    pads.onPress(37);
    REQUIRE(pads.onRelease);
    pads.onRelease(37);
    c.show();
    CHECK(c.chain->selectedDevice() == c.kit);
    CHECK(c.info().selectedPad == 37);
    CHECK(c.chain->frames()[0].text == juce::String::fromUTF8("Pad C\xe2\x99\xaf"
                                                              "1 \xc2\xb7 Snare"));
    REQUIRE(c.chain->selectedPad());
    CHECK(c.chain->selectedPad()->second == 37);

    // PAD's choke group and solo, through the panel's callbacks.
    auto& panel = c.panel();
    panel.onPadChoke(37, 2);
    CHECK(model::kit::padOn(c.info(), 37)->choke == 2); // the info lives to the end of the line
    panel.onPadSolo(37, true);
    c.show();
    CHECK(c.chain->frames()[0].soloed);

    // The pads shown move a row with the wheel's callback.
    panel.onShowPads(40);
    CHECK(c.info().padsFrom == 40);
}

TEST_CASE("drum kit in the chain: browser items dropped on pads and into the frame", "[ui]")
{
    KitChain c;
    // A sample on an empty pad: a Sampler of its own, the pad named for it.
    const auto kick = c.sample("Kick");
    c.dropOnPad("file:" + kick.getFullPathName().toStdString(), 38);
    auto info = c.info();
    auto pad = model::kit::padOn(info, 38);
    REQUIRE(pad != nullptr);
    CHECK(pad->name == "Kick");
    CHECK(pad->devices.at(0).deviceId == "sampler");
    CHECK(c.info().selectedPad == 38);
    // A module on it: after its Sampler.
    c.dropOnPad("module:fd:saturator", 38);
    info = c.info();
    pad = model::kit::padOn(info, 38);
    REQUIRE(pad->devices.size() == 2);
    CHECK(pad->devices[1].deviceId == "saturator");
    // A synth: in place of its Sampler.
    c.dropOnPad("module:fd:subtractive-synth", 38);
    info = c.info();
    pad = model::kit::padOn(info, 38);
    REQUIRE(pad->devices.size() == 2);
    CHECK(pad->devices[0].deviceId == "subtractive-synth");
    // A kit never goes on a pad.
    c.dropOnPad("module:fd:drum-kit", 40);
    CHECK(model::kit::padOn(c.info(), 40) == nullptr);
    CHECK_FALSE(fd::app::browser::fitsPad("module:fd:drum-kit"));
    CHECK_FALSE(fd::app::browser::fitsPad("module:fd:mod-lfo"));
}

TEST_CASE("drum kit in the chain: the Module menu's pad commands act on the selected pad", "[ui]")
{
    KitChain c;
    c.session->loadPads(c.kit, 36, {c.sample("Kick")});
    c.session->selectPad(c.kit, 36);
    c.show();
    c.chain->selectDevice(c.kit);
    REQUIRE(c.chain->selectedKit() == c.kit);
    c.chain->copySelectedPad();
    c.session->selectPad(c.kit, 41);
    c.show();
    CHECK_FALSE(c.chain->selectedPad()); // an empty pad selected
    c.chain->pasteOnSelectedPad();
    CHECK(model::kit::padOn(c.info(), 41)->name == "Kick");
    c.show();
    c.chain->clearSelectedPad();
    CHECK(model::kit::padOn(c.info(), 41) == nullptr);
    // A rack's own items don't take a kit: the macros do.
    CHECK(c.chain->selectedRack() == c.kit);
}

TEST_CASE("drum kit in the chain: several sample files make a kit; on a pad, they load from it up", "[ui]")
{
    KitChain c;
    // Another track, a Subtractive on it: several samples in its chain make a kit in its place.
    const auto other = c.session->addTrack();
    c.session->addDevice(other, "fd:subtractive-synth");
    c.session->updateSnapshotNow();
    c.chain->setTrack(*c.session->snapshot()->channel(other));
    c.chain->dropFiles({c.sample("Kick"), c.sample("Snare"), c.sample("Hat")}, {40, 40});
    c.session->updateSnapshotNow();
    const auto& devices = c.session->snapshot()->channel(other)->devices;
    REQUIRE(devices.size() == 1);
    CHECK(devices[0].deviceId == "drum-kit");
    REQUIRE(devices[0].rack != nullptr);
    CHECK(devices[0].rack->chains.size() == 3);
    CHECK(c.session->undoDescription() == "Replace Subtractive with Drum Kit");

    // On a pad of the first kit: from that pad up.
    c.show();
    auto& pads = c.panel().pads();
    const auto at = c.chain->content().getLocalPoint(&pads, pads.padBounds(44).getCentre());
    c.chain->dropFiles({c.sample("Tom"), c.sample("Tom 2")}, at);
    const auto info = c.info();
    CHECK(model::kit::padOn(info, 44)->name == "Tom");
    CHECK(model::kit::padOn(info, 45)->name == "Tom 2");
}

TEST_CASE("drum kit in the chain: pads light a moment as they're hit", "[ui]")
{
    KitChain c;
    c.session->loadPads(c.kit, 36, {c.sample("Kick"), c.sample("Snare")});
    c.show();
    c.chain->updateNotices(); // what it's seen so far
    CHECK(c.panel().pads().playing().none());
    // The kick hit, live (playback runs the kit's own pads), through tracktion's hosted device.
    tracktion::HostedAudioDeviceInterface::Parameters io;
    io.sampleRate = 48000.0;
    io.blockSize = 480;
    io.inputChannels = 0;
    io.outputChannels = 2;
    tracktion::test_utilities::EnginePlayer player(c.engine, io);
    const auto clip = c.session->createMidiClip(c.t, 0.0, 1.0);
    c.session->setNotes(clip, {{.pitch = 36, .start = 0.0, .length = 0.25, .velocity = 1.0f}});
    c.session->setPosition(0.0);
    c.session->play();
    for (int b = 0; b < 30; ++b)
    {
        player.process(480);
        juce::MessageManager::getInstance()->runDispatchLoopUntil(1);
    }
    c.session->stop();
    CHECK(c.session->padHits(c.kit)[36] >= 1);
    c.chain->updateNotices();
    CHECK(c.panel().pads().playing().test(36));
    CHECK_FALSE(c.panel().pads().playing().test(37));
    juce::Thread::sleep(200);
    c.chain->updateNotices();
    CHECK_FALSE(c.panel().pads().playing().test(36)); // a moment only
}
