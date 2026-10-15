// MIDI learn (M3 C8, docs/design/midi-learn.md) through the Session: learning a control from the
// first message, mapped moves going the mouse's way (takeover, one undo step a gesture), buttons,
// relative encoders, one message one target, a mapping's target gone and back, saving, the
// transport's mappings, and what the inputs' listeners take.
#include "engine/DevicePlugin.h"
#include "engine/EngineBehaviour.h"
#include "engine/MidiMapper.h"
#include "engine/Session.h"
#include "ui/Shell.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <tracktion_engine/testing/tracktion_EnginePlayer.h>
#include <tracktion_engine/tracktion_engine.h>

using Catch::Approx;
using fd::engine::Session;
using fd::model::MidiMapping;
using fd::model::MidiMode;
using fd::model::MidiSource;
using fd::model::MidiTarget;

namespace
{
struct Learn
{
    juce::ScopedJuceInitialiser_GUI init;
    tracktion::Engine engine{"fd-midi-learn-tests", nullptr, fd::engine::makeEngineBehaviour()};
    std::unique_ptr<Session> session;
    std::uint64_t track = 0, filter = 0;
    MidiTarget freq;

    Learn()
    {
        fd::engine::registerDevices(engine);
        session = std::make_unique<Session>(engine);
        session->setMidiTakeover(fd::model::MidiTakeover::Jump);
        track = session->snapshot()->tracks[0].id;
        filter = session->addDevice(track, "fd:filter");
        freq = {track, "m" + std::to_string(filter) + "/freq"};
        settle();
    }
    ~Learn() { session.reset(); }

    void settle()
    {
        session->flushSnapshot();
        juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
        session->flushSnapshot();
    }
    /// Messages from the input named "Launchkey", handled.
    void send(std::initializer_list<juce::MidiMessage> messages, const std::string& device = "Launchkey")
    {
        for (const auto& m : messages)
            session->receiveMidi(device, m);
        session->drainMidiNow();
        settle();
    }
    [[nodiscard]] float value(const MidiTarget& t) const
    {
        return session->paramValue({t.channel, t.key}).value_or(-1.0f);
    }
    /// Lets a move rest, so its gesture ends.
    void rest()
    {
        juce::Thread::sleep(600);
        session->drainMidiNow();
        settle();
    }
};

juce::MidiMessage cc(int number, int value, int channel = 1)
{
    return juce::MidiMessage::controllerEvent(channel, number, value);
}
} // namespace

TEST_CASE("midi learn: the next message maps the control waiting; moves follow, one undo step a gesture",
          "[integration]")
{
    Learn l;
    auto& s = *l.session;
    s.learnMidi(l.freq);
    CHECK(s.midiLearnTarget() == l.freq);
    l.send({cc(74, 10)});
    // A coarse controller (under 32) waits a moment for a fine half; 74 doesn't.
    REQUIRE(s.midiMappingOf(l.freq));
    const auto m = *s.midiMappingOf(l.freq);
    CHECK(m.source ==
          MidiSource{.kind = MidiSource::Kind::Cc, .channel = 1, .number = 74, .device = "Launchkey"});
    CHECK(m.mode == MidiMode::Absolute);
    CHECK_FALSE(s.midiLearnTarget());
    CHECK(s.undoDescription() == "Map MIDI");
    CHECK(l.value(l.freq) != Approx(10.0f / 127.0f)); // learning doesn't move it

    // Moved: it follows (Jump), and the whole move is one undo step, named after the parameter.
    const float before = l.value(l.freq);
    l.send({cc(74, 20), cc(74, 40), cc(74, 100)});
    CHECK(l.value(l.freq) == Approx(100.0f / 127.0f).margin(1e-3));
    l.send({cc(74, 110)});
    l.rest();
    CHECK(s.undoDescription() == s.paramName({l.freq.channel, l.freq.key}).first);
    s.undo();
    l.settle();
    CHECK(l.value(l.freq) == Approx(before).margin(1e-3));
    CHECK(s.midiMappingOf(l.freq)); // the mapping stays: only the move went
}

TEST_CASE("midi learn: pick up waits for the hardware to pass the control's value", "[integration]")
{
    Learn l;
    auto& s = *l.session;
    s.setMidiTakeover(fd::model::MidiTakeover::PickUp);
    s.setParamValue({l.freq.channel, l.freq.key}, 0.5f);
    REQUIRE(s.mapMidi(
        {.source = {.kind = MidiSource::Kind::Cc, .number = 74, .device = "Launchkey"}, .target = l.freq}));
    l.settle();
    l.send({cc(74, 0), cc(74, 30)});
    CHECK(l.value(l.freq) == Approx(0.5f).margin(1e-3)); // still waiting
    l.send({cc(74, 70)});                                // passed it
    CHECK(l.value(l.freq) == Approx(70.0f / 127.0f).margin(1e-3));
    l.send({cc(74, 20)});
    CHECK(l.value(l.freq) == Approx(20.0f / 127.0f).margin(1e-3));
}

TEST_CASE("midi learn: a 14-bit pair, a note, pitch bend; Min and Max", "[integration]")
{
    Learn l;
    auto& s = *l.session;
    // CC 1 then CC 33 arriving together: one 14-bit control.
    s.learnMidi(l.freq);
    l.send({cc(1, 64), cc(33, 0)});
    REQUIRE(s.midiMappingOf(l.freq));
    CHECK(s.midiMappingOf(l.freq)->source.kind == MidiSource::Kind::Cc14);
    CHECK(s.midiMappingOf(l.freq)->source.number == 1);
    l.send({cc(1, 32), cc(33, 0)});
    CHECK(l.value(l.freq) == Approx(32.0f * 128.0f / 16383.0f).margin(1e-3));
    // Min and Max (ML3): the hardware's travel over part of the control's, here inverted.
    REQUIRE(s.setMidiMapping(l.freq, 0.8f, 0.2f, MidiMode::Absolute));
    CHECK(s.undoDescription() == "MIDI mapping");
    l.settle();
    l.send({cc(1, 127), cc(33, 127)});
    CHECK(l.value(l.freq) == Approx(0.2f).margin(1e-3));
    CHECK_FALSE(s.setMidiMapping(l.freq, 0.0f, 1.0f, MidiMode::Toggle)); // a 14-bit control only sets

    // Pitch bend on the resonance.
    const MidiTarget res{l.track, "m" + std::to_string(l.filter) + "/res"};
    s.learnMidi(res);
    l.send({juce::MidiMessage::pitchWheel(1, 8192)});
    REQUIRE(s.midiMappingOf(res));
    CHECK(s.midiMappingOf(res)->source.kind == MidiSource::Kind::PitchBend);
    l.send({juce::MidiMessage::pitchWheel(1, 16383)});
    CHECK(l.value(res) == Approx(1.0f).margin(1e-3));

    // A note: a button, toggling the track's mute.
    const MidiTarget mute{l.track, "mute"};
    s.learnMidi(mute);
    l.send({juce::MidiMessage::noteOn(1, 36, 1.0f)});
    REQUIRE(s.midiMappingOf(mute));
    CHECK(s.midiMappingOf(mute)->mode == MidiMode::Toggle);
    l.send({juce::MidiMessage::noteOff(1, 36), juce::MidiMessage::noteOn(1, 36, 1.0f)});
    CHECK(s.snapshot()->channel(l.track)->mix.mute);
    l.send({juce::MidiMessage::noteOff(1, 36)});
    CHECK(s.snapshot()->channel(l.track)->mix.mute); // a release does nothing
    l.send({juce::MidiMessage::noteOn(1, 36, 1.0f)});
    CHECK_FALSE(s.snapshot()->channel(l.track)->mix.mute);
    // Momentary: on while held.
    REQUIRE(s.setMidiMapping(mute, 0.0f, 1.0f, MidiMode::Momentary));
    l.settle();
    l.send({juce::MidiMessage::noteOn(1, 36, 1.0f)});
    CHECK(s.snapshot()->channel(l.track)->mix.mute);
    l.send({juce::MidiMessage::noteOff(1, 36)});
    CHECK_FALSE(s.snapshot()->channel(l.track)->mix.mute);
}

TEST_CASE("midi learn: a relative encoder is guessed from its first moves, then adds steps", "[integration]")
{
    Learn l;
    auto& s = *l.session;
    s.setParamValue({l.freq.channel, l.freq.key}, 0.5f);
    REQUIRE(s.mapMidi(
        {.source = {.kind = MidiSource::Kind::Cc, .number = 20, .device = "Launchkey"}, .target = l.freq}));
    l.settle();
    l.send({cc(20, 1), cc(20, 1)}); // the same value twice, a small step: two's complement
    REQUIRE(s.midiMappingOf(l.freq));
    CHECK(s.midiMappingOf(l.freq)->mode == MidiMode::RelativeTwos);
    const float at = l.value(l.freq);
    l.send({cc(20, 1), cc(20, 1), cc(20, 127)});
    CHECK(l.value(l.freq) == Approx(at + 0.01f).margin(1e-4));
}

TEST_CASE("midi learn: one message, one target; a target mapped again; its module gone and back; saved",
          "[integration]")
{
    Learn l;
    auto& s = *l.session;
    const MidiTarget res{l.track, "m" + std::to_string(l.filter) + "/res"};
    const MidiSource knob{.kind = MidiSource::Kind::Cc, .number = 21, .device = "Launchkey"};
    REQUIRE(s.mapMidi({.source = knob, .target = l.freq}));
    REQUIRE(s.mapMidi({.source = knob, .target = res})); // the knob moves to Res (ML6)
    l.settle();
    CHECK_FALSE(s.midiMappingOf(l.freq));
    CHECK(s.midiMappingOf(res)->source == knob);
    // Res mapped again: it leaves its old message.
    const MidiSource other{.kind = MidiSource::Kind::Cc, .number = 22, .device = "Launchkey"};
    REQUIRE(s.mapMidi({.source = other, .target = res}));
    l.settle();
    CHECK(s.midiMappings().size() == 1);
    CHECK(s.midiMappingOf(res)->source == other);
    CHECK(s.midiTargetName(res) == "Filter \xc2\xb7 " + s.paramName({res.channel, res.key}).first);
    CHECK(s.midiTargetName({l.track, "mute"}).ends_with(" \xc2\xb7 Mute"));

    // The Filter deleted: its mapping goes with it, and comes back with its undo.
    s.removeDevice(l.filter);
    l.settle();
    CHECK(s.midiMappings().empty());
    s.undo();
    l.settle();
    CHECK(s.midiMappingOf(res));

    // Saved with the project.
    juce::TemporaryFile saved(Session::fileExtension);
    REQUIRE(s.saveAs(saved.getFile()));
    REQUIRE(s.removeMidiMapping(res));
    CHECK(s.undoDescription() == "Remove MIDI mapping");
    REQUIRE(s.open(saved.getFile()));
    l.settle();
    REQUIRE(s.midiMappingOf(res));
    CHECK(s.midiMappingOf(res)->source == other);
}

TEST_CASE("midi learn: the transport's mappings are the app's; a press runs its button", "[integration]")
{
    Learn l;
    auto& s = *l.session;
    std::vector<fd::model::MidiMapping> kept;
    std::vector<std::string> pressed;
    s.onAppMidiMappingsChanged = [&](const auto& m) { kept = m; };
    s.onMidiTransport = [&](const std::string& key) { pressed.push_back(key); };
    const MidiTarget record{0, "transport.record"};
    CHECK(s.midiTargetName(record) == "Record");
    s.learnMidi(record);
    l.send({cc(64, 127)}, "Foot");
    REQUIRE(kept.size() == 1);
    CHECK(kept[0].target == record);
    CHECK(kept[0].mode == MidiMode::Toggle);  // it started at its top: a button
    CHECK(s.undoDescription() != "Map MIDI"); // the app's, not the project's
    l.send({cc(64, 0), cc(64, 127)}, "Foot");
    CHECK(pressed == std::vector<std::string>{"transport.record"});
    // Kept by the app's settings: a new project keeps it.
    s.newProject();
    l.settle();
    REQUIRE(s.midiMappings().size() == 1);
    l.send({cc(64, 0), cc(64, 127)}, "Foot");
    CHECK(pressed.size() == 2);
    REQUIRE(s.removeMidiMapping(record));
    CHECK(kept.empty());
    s.onAppMidiMappingsChanged = nullptr;
    s.onMidiTransport = nullptr;
}

TEST_CASE("midi learn: the inputs' listeners take what's mapped, and everything while learning (ML8)",
          "[integration]")
{
    juce::ScopedJuceInitialiser_GUI init;
    tracktion::Engine engine{"fd-midi-learn-tests", nullptr, fd::engine::makeEngineBehaviour()};
    fd::engine::MidiMapper mapper(engine);
    const auto taken = mapper.takenFrom("Launchkey");
    const auto elsewhere = mapper.takenFrom("Keys");
    mapper.setMapped({{.kind = MidiSource::Kind::Cc, .channel = 2, .number = 1, .device = "Launchkey"},
                      {.kind = MidiSource::Kind::Cc14, .channel = 1, .number = 7, .device = "Launchkey"},
                      {.kind = MidiSource::Kind::Note, .channel = 1, .number = 36, .device = "Launchkey"}});
    CHECK(taken.takes(cc(1, 64, 2)));
    CHECK_FALSE(taken.takes(cc(1, 64, 1))); // another channel plays on
    CHECK(taken.takes(cc(7, 64)));
    CHECK(taken.takes(cc(39, 64))); // the pair's fine half
    CHECK(taken.takes(juce::MidiMessage::noteOn(1, 36, 1.0f)));
    CHECK(taken.takes(juce::MidiMessage::noteOff(1, 36)));
    CHECK_FALSE(taken.takes(juce::MidiMessage::noteOn(1, 37, 1.0f)));
    CHECK_FALSE(elsewhere.takes(cc(1, 64, 2))); // another input's message
    // An input plugged in later finds its mappings ready.
    CHECK(mapper.takenFrom("Launchkey").takes(cc(7, 1)));
    mapper.setLearning(true);
    CHECK(elsewhere.takes(juce::MidiMessage::noteOn(1, 60, 1.0f)));
    CHECK_FALSE(elsewhere.takes(juce::MidiMessage::programChange(1, 3))); // only what can be mapped
    mapper.setLearning(false);
    CHECK_FALSE(elsewhere.takes(juce::MidiMessage::noteOn(1, 60, 1.0f)));
}

TEST_CASE("midi learn: a mapped fader moved while automation is armed and playing writes its lane (ML9)",
          "[integration]")
{
    Learn l;
    auto& s = *l.session;
    tracktion::HostedAudioDeviceInterface::Parameters io;
    io.sampleRate = 48000.0;
    io.blockSize = 480;
    io.inputChannels = 0;
    io.outputChannels = 2;
    tracktion::test_utilities::EnginePlayer player(l.engine, io);
    const auto pump = [&](double seconds)
    {
        for (int i = 0; i < static_cast<int>(seconds * 100.0); ++i)
        {
            player.process(480);
            s.drainMidiNow();
            juce::MessageManager::getInstance()->runDispatchLoopUntil(1);
        }
    };
    const MidiTarget volume{l.track, "volume"};
    REQUIRE(s.mapMidi(
        {.source = {.kind = MidiSource::Kind::Cc, .number = 7, .device = "Faders"}, .target = volume}));
    l.settle();
    s.setAutomationArmed(true);
    s.setPosition(0.0);
    s.play();
    pump(0.2);
    REQUIRE(s.isPlaying());
    for (const int v : {100, 80, 60, 40})
    {
        s.receiveMidi("Faders", cc(7, v));
        pump(0.2);
    }
    pump(0.8); // it rests: the move ends
    s.stop();
    pump(0.3);
    juce::MessageManager::getInstance()->runDispatchLoopUntil(600);
    s.updateSnapshotNow();
    const auto lane = s.laneOf({volume.channel, volume.key});
    REQUIRE(lane);
    const auto* info = s.snapshot()->automationLane(*lane);
    REQUIRE(info != nullptr);
    CHECK(info->points.size() >= 2);
    CHECK(info->points.back().value == Approx(40.0f / 127.0f).margin(0.02));
    s.setAutomationArmed(false);
}

namespace
{
template <typename T> std::vector<T*> findAll(juce::Component& root)
{
    std::vector<T*> found;
    for (auto* child : root.getChildren())
    {
        if (auto* t = dynamic_cast<T*>(child))
            found.push_back(t);
        auto deeper = findAll<T>(*child);
        found.insert(found.end(), deeper.begin(), deeper.end());
    }
    return found;
}

void dump(const juce::String& name, juce::Component& c, juce::Rectangle<int> area)
{
    if (const char* dir = std::getenv("FD_DUMP_DIR"))
    {
        const auto file = juce::File(dir).getChildFile(name);
        file.deleteFile();
        juce::PNGImageFormat png;
        if (auto out = file.createOutputStream())
            png.writeImageToStream(c.createComponentSnapshot(area, true, 2.0f), *out);
    }
}
} // namespace

TEST_CASE("midi learn in the shell: Learn MIDI on a control's right-click, its tooltip; map mode's outlines, "
          "labels, banner and the inspector's list; a mixer control and a transport button mapped by click",
          "[integration]")
{
    Learn l;
    auto& s = *l.session;
    fd::ui::LookAndFeel lnf;
    juce::LookAndFeel::setDefaultLookAndFeel(&lnf);
    {
        const juce::TemporaryFile folderName;
        const auto folder = folderName.getFile();
        struct Cleanup // after the shell and its store
        {
            juce::File dir;
            ~Cleanup() { dir.deleteRecursively(); }
        } cleanup{folder};
        fd::model::SettingsStore store(folder.getFullPathName().toStdString());
        fd::app::Shell shell(s);
        shell.setSettingsStore(store);
        s.setMidiTakeover(fd::model::MidiTakeover::Jump);
        shell.setSize(1800, 1000);
        shell.arrangement().selectTrack(l.track);
        const auto frame = [&]
        {
            l.settle();
            shell.updateInspectorModule();
            shell.midiOverlay().update();
            shell.inspector().updateMidiMappings();
        };
        frame();
        auto& chain = shell.detail().chain();
        fd::ui::Knob* knob = nullptr;
        for (auto* k : findAll<fd::ui::Knob>(chain.content()))
            if (const auto t = chain.targetOf(*k); t && t->key == l.freq.key)
                knob = k;
        REQUIRE(knob != nullptr);
        REQUIRE(shell.midiTargetOf(*knob) == l.freq);

        // Its right-click: Learn MIDI, after the automation items.
        juce::PopupMenu menu;
        REQUIRE(s.automationMenu);
        s.automationMenu({l.freq.channel, l.freq.key}, menu);
        std::vector<juce::String> items;
        for (juce::PopupMenu::MenuItemIterator it(menu); it.next();)
            items.push_back(it.getItem().text);
        CHECK(std::find(items.begin(), items.end(), "Learn MIDI") != items.end());
        s.learnMidi(l.freq);
        frame();
        // Waiting: only it shows, outlined; its tooltip says what to do.
        REQUIRE(shell.midiOverlay().shown().size() == 1);
        CHECK(shell.midiOverlay().shown()[0].control == knob);
        fd::app::Tooltips tips(nullptr, s, [&](const juce::Component& c) { return shell.midiTargetOf(c); });
        CHECK(tips.getTipFor(*knob) == "Move a control on your MIDI device");
        dump("midi-learn-waiting@2x.png", shell,
             shell.getLocalArea(knob, knob->getLocalBounds()).expanded(40, 20));
        l.send({cc(74, 64)});
        frame();
        CHECK_FALSE(s.midiLearnTarget());
        CHECK(tips.getTipFor(*knob).endsWith("Mapped to CC 74 (Launchkey)"));
        juce::PopupMenu again;
        s.automationMenu({l.freq.channel, l.freq.key}, again);
        bool removeItem = false;
        for (juce::PopupMenu::MenuItemIterator it(again); it.next();)
            removeItem = removeItem || it.getItem().text == "Remove MIDI mapping (CC 74)";
        CHECK(removeItem);

        // Options > MIDI map mode: the banner, every mappable control outlined, the inspector's list.
        auto& actions = shell.actions();
        REQUIRE(actions.perform("midi.mapMode"));
        CHECK(actions.isToggled("midi.mapMode"));
        frame();
        CHECK(shell.midiMapMode());
        CHECK(shell.inspector().midiMappingsShown());
        REQUIRE(shell.inspector().midiMappings().rows().size() == 1);
        const auto& overlay = shell.midiOverlay();
        bool sawKnob = false, sawVolume = false, sawRecord = false;
        // The track's mute (its header at its default height shows mute, solo and arm).
        const fd::model::MidiTarget volume{l.track, "mute"}, record{0, "transport.record"};
        juce::Component* volumeControl = nullptr;
        juce::Component* recordButton = nullptr;
        for (const auto& shown : overlay.shown())
        {
            sawKnob = sawKnob || shown.control == knob;
            if (shown.target == volume && volumeControl == nullptr)
                volumeControl = shown.control;
            if (shown.target == record)
                recordButton = shown.control;
            // A mapped control's label inside it, at every size (§4).
            if (const auto label = overlay.labelOf(shown); !label.area.isEmpty())
                CHECK(shown.bounds.contains(label.area));
        }
        sawVolume = volumeControl != nullptr;
        sawRecord = recordButton != nullptr;
        CHECK(sawKnob);
        CHECK(sawVolume);
        CHECK(sawRecord);
        CHECK(overlay.labelOf(overlay.shown()[0]).text.isNotEmpty() == (overlay.shown()[0].control == knob));
        dump("midi-map-mode@2x.png", shell, shell.getLocalBounds());
        dump("midi-map-mode-chain@2x.png", shell, shell.getLocalArea(&chain, chain.getLocalBounds()));
        dump("midi-map-mode-inspector@2x.png", shell,
             shell.getLocalArea(&shell.inspector(), shell.inspector().getLocalBounds()));

        // A click on the track's mute in map mode chooses it; a button pressed maps it.
        auto& overlayComponent = shell.midiOverlay();
        const auto at =
            overlayComponent.getLocalArea(volumeControl, volumeControl->getLocalBounds()).getCentre();
        REQUIRE(overlayComponent.hitTest(at.x, at.y));
        overlayComponent.mouseDown(juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(),
                                                    at.toFloat(), juce::ModifierKeys::leftButtonModifier,
                                                    0.0f, 0.0f, 0.0f, 0.0f, 0.0f, &overlayComponent,
                                                    &overlayComponent, juce::Time::getCurrentTime(),
                                                    at.toFloat(), juce::Time::getCurrentTime(), 1, false));
        CHECK(s.midiLearnTarget() == volume);
        frame();
        dump("midi-map-mode-chosen@2x.png", shell,
             shell.getLocalArea(volumeControl, volumeControl->getLocalBounds()).expanded(60, 30));
        l.send({cc(7, 100, 2)}, "Faders");
        juce::Thread::sleep(40); // a coarse controller waits 30 ms for a fine half
        s.drainMidiNow();
        frame();
        REQUIRE(s.midiMappingOf(volume));
        CHECK(s.midiMappingOf(volume)->source.channel == 2);
        CHECK(shell.inspector().midiMappings().rows().size() == 2);
        // Clicks elsewhere go through (the views still scroll and select): not on a control, no hit.
        CHECK_FALSE(overlayComponent.hitTest(shell.getWidth() - 2, shell.getHeight() / 2));

        // The record button, mapped from a foot switch: the app's.
        const auto onRecord =
            overlayComponent.getLocalArea(recordButton, recordButton->getLocalBounds()).getCentre();
        s.learnMidi(record);
        l.send({cc(64, 127)}, "Foot");
        frame();
        CHECK(s.midiMappingOf(record));
        CHECK(shell.settings().controllers.transport.size() == 1);
        CHECK(shell.inspector().midiMappings().rows().size() == 2); // the transport's are in Settings
        CHECK(overlayComponent.hitTest(onRecord.x, onRecord.y));
        dump("midi-map-mode-bar@2x.png", shell, juce::Rectangle<int>(0, 0, shell.getWidth(), 48));

        // Escape leaves the mode: back to the selection's sections.
        CHECK(shell.keyPressed(juce::KeyPress(juce::KeyPress::escapeKey)));
        frame();
        CHECK_FALSE(shell.midiMapMode());
        CHECK_FALSE(shell.inspector().midiMappingsShown());
        CHECK(overlay.shown().empty());

        // Settings > Controllers: takeover, values sent back, the transport's.
        shell.showSettings("controllers.takeover");
        juce::MessageManager::getInstance()->runDispatchLoopUntil(50);
        auto& view = shell.settingsView();
        auto* takeover = dynamic_cast<juce::ComboBox*>(view.controlsOf("controllers.takeover").front());
        REQUIRE(takeover != nullptr);
        takeover->setSelectedId(3, juce::sendNotificationSync);
        CHECK(s.midiTakeover() == fd::model::MidiTakeover::Scale);
        const auto transport = view.controlsOf("controllers.transport");
        REQUIRE(transport.size() == 2); // the mapping and its Remove
        if (auto* content = transport.front()->getParentComponent())
            dump("midi-settings-controllers@2x.png", view,
                 view.getLocalArea(content,
                                   view.rowBounds("controllers.takeover").withHeight(320).expanded(0, 40)));
        shell.closeSettings();
    }
    juce::LookAndFeel::setDefaultLookAndFeel(nullptr);
}
