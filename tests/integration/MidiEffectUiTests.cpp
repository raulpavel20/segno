// MIDI effects step 0 drawn (docs/design/midi-effects.md §12, §18): the panels of step 1's modules
// (Pitch, Chord, Arpeggiator) and of one generator (Melody), with the real panel kit, from a stub of
// what the engine will feed them: the notes heard and played around the playhead, the song's
// chords, the lanes' steps. Each state passes the module panel lint (tests/panels/PanelLint.h), and
// is drawn for review with FD_DUMP_DIR, at 1x and 2x.
#include "../../devices/arpeggiator/dsp/Arpeggiator.h"
#include "../../devices/arpeggiator/ui/ArpeggiatorPanel.h"
#include "../../devices/chord/dsp/Chord.h"
#include "../../devices/chord/ui/ChordPanel.h"
#include "../../devices/melody/dsp/Melody.h"
#include "../../devices/melody/ui/MelodyPanel.h"
#include "../../devices/pitch/dsp/Pitch.h"
#include "../../devices/pitch/ui/PitchPanel.h"
#include "../panels/PanelLint.h"

#include <catch2/catch_test_macros.hpp>
#include <cstdlib>
#include <filesystem>

using fd::ui::NoteFeed;

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

/// Segno's look while it lives, as the app sets it.
struct SegnoLook
{
    fd::ui::LookAndFeel lnf;
    SegnoLook() { juce::LookAndFeel::setDefaultLookAndFeel(&lnf); }
    ~SegnoLook() { juce::LookAndFeel::setDefaultLookAndFeel(nullptr); }
    SegnoLook(const SegnoLook&) = delete;
    SegnoLook& operator=(const SegnoLook&) = delete;
};

struct StubBinding final : fd::ui::ParamBinding
{
    explicit StubBinding(fd::ParamStore& s) : store(s) {}
    float getPlain(size_t i) const override { return store.get(i); }
    void setPlain(size_t i, float v) override { store.set(i, v); }
    void beginGesture(size_t) override {}
    void endGesture(size_t) override {}
    void applyExtraState(const nlohmann::json&) override {}
    fd::ParamStore& store;
};

struct StubPresets final : fd::PresetHost
{
    fd::PresetLibrary presets{{},
                              std::filesystem::temp_directory_path() / "fd-midi-effect-ui-tests-no-presets"};
    fd::PresetMemo current;
    const fd::Device* device = nullptr;
    fd::PresetLibrary& library() override { return presets; }
    fd::DeviceState state() const override
    {
        return device != nullptr ? fd::captureState(*device) : fd::DeviceState{};
    }
    fd::PresetMemo memo() const override { return current; }
    void apply(const fd::DeviceState&, const fd::PresetMemo& m, const std::string&) override { current = m; }
    void setMemo(const fd::PresetMemo& m) override { current = m; }
    bool trash(const std::filesystem::path&) override { return false; }
};

/// The header's Listen: none, or one of the song's tracks.
struct StubListen final : fd::ui::SidechainRouting
{
    std::vector<std::string> sources() const override { return {"None", "Kick", "Bass", "Keys"}; }
    int selected() const override { return choice; }
    void select(int index) override { choice = index; }
    int choice = 0;
};

/// What the engine will feed a note display: a view set by the test, shown as if the playhead
/// were at its `now`.
struct StubNotes final : NoteFeed
{
    View shown;
    View view(double, double) const override { return shown; }
};

/// The song's chords along four bars at 4/4: Am7, Fmaj7, C, G, a bar each.
std::vector<NoteFeed::Chord> chords()
{
    return {{0.0, 4.0, "Am7"}, {4.0, 8.0, "Fmaj7"}, {8.0, 12.0, "C"}, {12.0, 16.0, "G"}};
}

/// A riff of eighths from beat `from`, its notes as written (`played` false) or as fitted.
std::vector<NoteFeed::Note> riff(double from, double to, const std::vector<int>& keys, bool played,
                                 int shift = 0)
{
    std::vector<NoteFeed::Note> out;
    int i = 0;
    for (double b = from; b < to; b += 0.5, ++i)
    {
        const int key = keys[static_cast<std::size_t>(i) % keys.size()] + shift;
        out.push_back({b, b + 0.4, key, 0.6f + 0.3f * static_cast<float>(i % 3) / 2.0f, played});
    }
    return out;
}

/// One module's panel, its device, its host stubs; laid out at its width.
template <typename Device> struct Drawn
{
    Device device;
    StubBinding binding{device.params()};
    StubPresets presets;
    StubListen listen;
    StubNotes notes;
    fd::ui::DeviceUiContext ctx{.info = device.info(),
                                .params = binding,
                                .analysis = &device.analysis(),
                                .device = &device,
                                .sidechain = &listen,
                                .presets = &presets,
                                .notes = &notes};
    std::unique_ptr<fd::ui::DevicePanel> panel;

    explicit Drawn(const fd::ui::PanelFactory& factory)
    {
        presets.device = &device;
        panel = factory.create(ctx);
        layout();
    }
    void set(std::size_t param, float plain)
    {
        device.params().set(param, plain);
        layout();
    }
    void layout()
    {
        panel->refreshNow();
        panel->setSize(panel->preferredWidth(), fd::ui::Theme::current().devicePanelHeight);
        refresh();
    }
    /// Every display draws what the stub feeds now.
    void refresh()
    {
        std::vector<juce::Component*> all;
        fd::test::descendants(*panel, all);
        for (auto* c : all)
            if (auto* d = dynamic_cast<fd::ui::NoteDisplay*>(c))
                d->refresh();
    }
    template <typename T> T* find()
    {
        std::vector<juce::Component*> all;
        fd::test::descendants(*panel, all);
        for (auto* c : all)
            if (auto* t = dynamic_cast<T*>(c))
                return t;
        return nullptr;
    }
    void check(const juce::String& name)
    {
        INFO(name);
        fd::test::lintPanel(*panel, device.info(), "None");
        dump(name, *panel);
    }
    fd::ui::PanelSection& section(const std::string& id)
    {
        auto* s = panel->findSection(id);
        REQUIRE(s != nullptr);
        return *s;
    }
};
} // namespace

TEST_CASE("midi effects drawn: Pitch", "[integration]")
{
    juce::ScopedJuceInitialiser_GUI init;
    SegnoLook look;
    using P = fd::devices::Pitch;
    Drawn<P> d(fd::devices::pitchPanelFactory());

    SECTION("idle: nothing played yet, the song's chords along the top")
    {
        d.notes.shown = {.chords = chords(), .now = 2.0, .following = "Song"};
        d.refresh();
        d.check("pitch-idle");
    }
    SECTION("fitting a riff to the song's chords")
    {
        d.set(P::Fit, 2.0f); // Chord
        const std::vector<int> written{57, 60, 64, 62, 60, 64, 67, 64};
        const std::vector<int> fitted{57, 60, 64, 64, 60, 64, 67, 64, 57, 60, 65, 64, 60, 65, 69, 65};
        auto in = riff(0.0, 8.0, written, false);
        auto out = riff(0.0, 8.0, fitted, true);
        in.insert(in.end(), out.begin(), out.end());
        d.notes.shown = {.notes = in, .chords = chords(), .now = 5.5, .sectionEnd = 8.0, .following = "Song"};
        d.refresh();
        d.check("pitch-fitting");
    }
    SECTION("HARMONY open: its own key and chord")
    {
        d.set(P::Fit, 1.0f);
        d.set(P::HarmonyFirst + std::size_t{fd::devices::harmony::Follow}, 3.0f); // Own
        d.set(P::HarmonyFirst + std::size_t{fd::devices::harmony::Key}, 1.0f);    // Own
        d.set(P::HarmonyFirst + std::size_t{fd::devices::harmony::Root}, 2.0f);   // D
        d.section("harmony").setCollapsed(false);
        d.layout();
        d.notes.shown = {.now = 2.0, .following = "Own", .fallback = "Own chord: D major"};
        d.refresh();
        d.check("pitch-harmony-open");
    }
    SECTION("following a track's chords: Listen in the header")
    {
        d.set(P::Fit, 2.0f);
        d.set(P::HarmonyFirst + std::size_t{fd::devices::harmony::Follow}, 1.0f); // Track
        d.listen.choice = 3;                                                      // Keys
        d.layout();
        d.notes.shown = {.chords = chords(), .now = 2.0, .following = "Keys"};
        d.refresh();
        d.check("pitch-follows-track");
    }
    SECTION("folded")
    {
        d.panel->setFoldable(true);
        d.panel->setFolded(true);
        d.panel->setSize(d.panel->preferredWidth(), fd::ui::Theme::current().devicePanelHeight);
        dump("pitch-folded", *d.panel);
    }
    SECTION("in a plugin host: no song, its own key")
    {
        d.notes.shown = {.now = 1.0, .following = "Own key", .fallback = "No song here: C major"};
        d.refresh();
        d.check("pitch-no-song");
    }
}

TEST_CASE("midi effects drawn: Chord", "[integration]")
{
    juce::ScopedJuceInitialiser_GUI init;
    SegnoLook look;
    using C = fd::devices::Chord;
    Drawn<C> d(fd::devices::chordPanelFactory());
    const auto stabs = [](const std::vector<std::vector<int>>& chordsByBar)
    {
        std::vector<NoteFeed::Note> out;
        for (double b = 0.0; b < 8.0; b += 1.0)
        {
            out.push_back({b + 0.5, b + 0.9, 57, 0.8f, false});
            for (int k : chordsByBar[static_cast<std::size_t>(b / 4.0)])
                out.push_back({b + 0.5, b + 0.9, k, 0.8f, true});
        }
        return out;
    };

    SECTION("Add: shifts over each note")
    {
        d.set(C::InKey, 1.0f);
        d.set(C::Shift1 + 2, 7.0f);
        d.notes.shown = {.notes = stabs({{57, 60, 64, 67}, {57, 60, 65, 69}}),
                         .chords = chords(),
                         .now = 5.0,
                         .following = "Song"};
        d.refresh();
        d.check("chord-add");
    }
    SECTION("Song: the song's chord on each note, voice-led")
    {
        d.set(C::Mode, static_cast<float>(C::Song));
        d.set(C::Kind, 1.0f);
        d.set(C::Strum, 30.0f);
        d.notes.shown = {.notes = stabs({{57, 60, 64, 67}, {57, 60, 64, 65}}),
                         .chords = chords(),
                         .now = 5.0,
                         .following = "Song"};
        d.refresh();
        d.check("chord-song");
    }
    SECTION("Keys: the key's chords from one finger")
    {
        d.set(C::Mode, static_cast<float>(C::Keys));
        d.notes.shown = {.chords = chords(), .now = 2.0, .following = "Song's key"};
        d.refresh();
        d.check("chord-keys");
    }
}

TEST_CASE("midi effects drawn: Arpeggiator", "[integration]")
{
    juce::ScopedJuceInitialiser_GUI init;
    SegnoLook look;
    using A = fd::devices::Arpeggiator;
    Drawn<A> d(fd::devices::arpeggiatorPanelFactory());
    // Accents on a 16-step velocity lane, a 5-step gate lane drifting against it, an octave lane.
    const std::array<float, 16> accents{140, 70, 90, 70, 130, 70, 90, 60, 140, 70, 90, 70, 120, 60, 100, 80};
    for (int s = 0; s < 16; ++s)
        d.device.setStep(A::VelocityLane, s, accents[static_cast<std::size_t>(s)]);
    const std::array<float, 5> gates{100, 40, 100, 160, 60};
    for (int s = 0; s < 5; ++s)
        d.device.setStep(A::GateLane, s, gates[static_cast<std::size_t>(s)]);
    d.set(A::GateLength, 5.0f);
    for (int s = 0; s < 16; ++s)
        d.device.setStep(A::OctaveLane, s, s % 8 == 7 ? 1.0f : (s % 4 == 3 ? -1.0f : 0.0f));
    d.device.setPlayingStep(A::VelocityLane, 5);
    d.device.setPlayingStep(A::GateLane, 0);
    d.device.setPlayingStep(A::OctaveLane, 5);
    std::vector<NoteFeed::Note> notes;
    const std::vector<int> held{57, 60, 64, 67};
    for (double b = 0.0; b < 6.0; b += 0.25)
        notes.push_back({b, b + 0.2, held[static_cast<std::size_t>(b * 4.0) % 4],
                         0.5f + 0.4f * static_cast<float>((static_cast<int>(b * 4.0) % 4 == 0)), true});
    for (int k : held)
        notes.push_back({0.0, 6.0, k, 0.7f, false});

    SECTION("lanes: velocity accents, the step playing lit")
    {
        d.notes.shown = {.notes = notes, .chords = chords(), .now = 5.4, .following = "Song"};
        d.refresh();
        d.check("arpeggiator-velocity");
    }
    SECTION("the gate lane, five steps against sixteen")
    {
        auto* chooser = d.find<fd::ui::Selector>();
        REQUIRE(chooser != nullptr);
        chooser->setSelected(1);
        if (chooser->onSelect)
            chooser->onSelect(1);
        d.notes.shown = {.notes = notes, .chords = chords(), .now = 5.4, .following = "Song"};
        d.refresh();
        d.check("arpeggiator-gate");
    }
    SECTION("the octave lane, both ways")
    {
        auto* chooser = d.find<fd::ui::Selector>();
        REQUIRE(chooser != nullptr);
        chooser->setSelected(2);
        if (chooser->onSelect)
            chooser->onSelect(2);
        d.notes.shown = {.notes = notes, .chords = chords(), .now = 5.4, .following = "Song"};
        d.refresh();
        d.check("arpeggiator-octave");
    }
    SECTION("song chords: no notes held, it plays the song's")
    {
        d.set(A::SongChords, 1.0f);
        d.set(A::Style, 2.0f); // Up-down
        std::vector<NoteFeed::Note> fromSong;
        const std::vector<std::vector<int>> chordNotes{{57, 60, 64, 67}, {53, 57, 60, 64}};
        for (double b = 0.0; b < 8.0; b += 0.25)
        {
            const auto& c = chordNotes[static_cast<std::size_t>(b / 4.0)];
            const int i = static_cast<int>(b * 4.0) % 6;
            fromSong.push_back({b, b + 0.2, c[static_cast<std::size_t>(i < 4 ? i : 6 - i)], 0.7f, true});
        }
        d.notes.shown = {.notes = fromSong, .chords = chords(), .now = 5.2, .following = "Song"};
        d.refresh();
        d.check("arpeggiator-song-chords");
    }
}

TEST_CASE("midi effects drawn: Melody, a generator", "[integration]")
{
    juce::ScopedJuceInitialiser_GUI init;
    SegnoLook look;
    using M = fd::devices::Melody;
    Drawn<M> d(fd::devices::melodyPanelFactory());
    // A line in steps of the key from the chord's root: a motif and its varied repeat.
    const std::array<float, 16> line{0, 2, 4, 2, 7, 4, 2, 0, 0, 2, 4, 5, 7, 9, 7, 4};
    for (int s = 0; s < 16; ++s)
        d.device.setStep(M::PitchLane, s, line[static_cast<std::size_t>(s)]);
    d.device.setPlayingStep(M::PitchLane, 9);
    d.set(M::Change, 30.0f);
    std::vector<NoteFeed::Note> out;
    const std::vector<int> scale{57, 59, 60, 62, 64, 65, 67, 69, 71, 72};
    for (int s = 0; s < 24; ++s)
    {
        const double b = s * 0.25;
        const int deg = static_cast<int>(line[static_cast<std::size_t>(s % 16)]);
        out.push_back({b, b + 0.22, scale[static_cast<std::size_t>(std::clamp(deg, 0, 9))], 0.75f, true});
    }
    d.notes.shown = {.notes = out, .chords = chords(), .now = 5.6, .sectionEnd = 8.0, .following = "Song"};
    d.refresh();
    d.check("melody");
}

// ---- the song's chords on the song row, and what MIDI effects play drawn in clips ----------

#include "ShellTestKit.h"

namespace
{
/// Keys playing a progression (Am7, Fmaj7, C, G, a bar each, twice), a Bass clip with no notes a
/// generator plays in, a Lead riff that Pitch fits to the chords; Verse and Chorus; A minor.
struct SongFixture : fd::testing::ShellFixture
{
    juce::File root = juce::File::getSpecialLocation(juce::File::tempDirectory)
                          .getChildFile("fd-midi-effect-ui-" + juce::Uuid().toString());
    std::uint64_t keys = 0, bass = 0, lead = 0, keysClip = 0, bassClip = 0, leadClip = 0;

    explicit SongFixture(int width)
    {
        shell->setSize(width, 900);
        REQUIRE(root.createDirectory());
        session->setUnsavedRoot(root.getChildFile("Unsaved"));
        session->updateSnapshotNow();
        const auto tracks = session->snapshot()->tracks;
        REQUIRE(tracks.size() >= 3);
        keys = tracks[0].id;
        bass = tracks[1].id;
        lead = tracks[2].id;
        for (std::size_t i = 3; i < tracks.size(); ++i)
            REQUIRE(session->deleteTrack(tracks[i].id));
        session->renameTrack(keys, "Keys");
        session->renameTrack(bass, "Bass");
        session->renameTrack(lead, "Lead");
        REQUIRE(session->setKey(fd::music::Key{.root = 9, .scale = "minor"}));
        session->updateSnapshotNow();
        REQUIRE(session->snapshot()->keys.first().has_value());
        REQUIRE(session->addSection(0.0, 8.0, "Verse") != 0);
        REQUIRE(session->addSection(8.0, 16.0, "Chorus") != 0);

        // Keys: four chords a bar each, twice over (16 bars at 120 BPM: 2 s a bar).
        keysClip = session->createMidiClip(keys, 0.0, 16.0);
        const std::vector<std::vector<int>> progression{
            {57, 60, 64, 67}, {53, 57, 60, 64}, {48, 55, 60, 64}, {55, 59, 62, 67}};
        std::vector<fd::model::Note> chordsNotes;
        for (int bar = 0; bar < 8; ++bar)
            for (int k : progression[static_cast<std::size_t>(bar % 4)])
                chordsNotes.push_back({.pitch = k, .start = bar * 4.0, .length = 3.75, .velocity = 0.7f});
        session->setNotes(keysClip, chordsNotes);
        // Bass: a clip with no notes, for a generator to play in.
        bassClip = session->createMidiClip(bass, 0.0, 16.0);
        // Lead: one riff over the verse.
        leadClip = session->createMidiClip(lead, 0.0, 8.0);
        std::vector<fd::model::Note> riff;
        const std::vector<int> line{69, 72, 76, 74, 72, 76, 79, 76};
        for (int i = 0; i < 32; ++i)
            riff.push_back({.pitch = line[static_cast<std::size_t>(i % 8)],
                            .start = i * 0.5,
                            .length = 0.4,
                            .velocity = 0.8f});
        session->setNotes(leadClip, riff);
        settle();
        shell->arrangement().setView(0.0, (width - 600.0) / 17.0);
        shell->arrangement().setSongChordsShown(true);
        settle();
        juce::MessageManager::getInstance()->runDispatchLoopUntil(300); // the top bar shows the key
    }

    ~SongFixture()
    {
        shell.reset();
        session.reset();
        root.deleteRecursively();
    }

    /// The snapshot as the engine will make it: what the Bass's generator and the Lead's Pitch play.
    std::shared_ptr<fd::model::Snapshot> withPlayed()
    {
        auto s = std::make_shared<fd::model::Snapshot>(*session->snapshot());
        const std::vector<int> roots{45, 41, 36, 43};
        for (auto& t : s->tracks)
            for (auto& c : t.clips)
                if (c.id == bassClip)
                    for (int bar = 0; bar < 8; ++bar)
                        for (double b : {0.0, 1.5, 2.0, 3.5})
                        {
                            const int low = roots[static_cast<std::size_t>(bar % 4)];
                            c.played.push_back(
                                {.pitch = b == 3.5 ? low + 7 : low, .start = bar * 4.0 + b, .length = 0.4});
                        }
                else if (c.id == leadClip)
                    for (const auto& n : c.notes)
                    {
                        // Fitted to the chord at each note: over Fmaj7, its E and G move to F and A.
                        const int bar = static_cast<int>(n.start / 4.0) % 4;
                        auto fitted = n;
                        if (bar == 1 && (n.pitch % 12 == 4 || n.pitch % 12 == 7))
                            fitted.pitch += 1 + (n.pitch % 12 == 7 ? 1 : 0);
                        c.played.push_back(fitted);
                    }
        return s;
    }

    void settle()
    {
        session->updateSnapshotNow();
        shell->arrangement().setSnapshot(session->snapshot());
        juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
        shell->finishAnimations();
        shell->arrangement().frame(1.0 / 60.0, 0.0);
    }
};
} // namespace

TEST_CASE("midi effects drawn: the song's chords on the song row; a generator's notes in its clip",
          "[integration]")
{
    for (const int width : {1800, 1280})
    {
        SongFixture f(width);
        auto& view = f.shell->arrangement();
        const auto snapshot = f.withPlayed();
        // The song's harmony read from the Keys' notes: four chords, again in the chorus.
        REQUIRE(snapshot->songChords.size() == 8);
        CHECK(snapshot->songChords[0].name == "Am7");
        CHECK(snapshot->songChords[1].name == "Fmaj7");
        CHECK(snapshot->songChords[2].name == "C");
        CHECK(snapshot->songChords[3].name == "G");
        view.setSnapshot(snapshot);
        view.frame(1.0 / 60.0, 0.0);
        // The arrangement alone: the top bar learns the key on the frame clock, which tests don't run.
        if (const char* dir = std::getenv("FD_DUMP_DIR"))
            fd::testing::dumpPng(
                dir, "song-chords-" + juce::String(width) + "@2x.png",
                view.createComponentSnapshot(view.getLocalBounds().withHeight(360), true, 2.0f));
    }
}

TEST_CASE("midi effects drawn: a track's Harmony in the inspector's TRACK, at its narrowest", "[integration]")
{
    SongFixture f(1800);
    auto& inspector = f.shell->inspector();
    f.shell->arrangement().selectTrack(f.bass);
    f.settle();
    auto& harmony = inspector.trackHarmony();
    REQUIRE(harmony.isVisible());
    CHECK(harmony.getText() == "Auto");
    const int narrowest = fd::ui::Theme::current().shell.inspectorWidth;
    inspector.setSize(narrowest, inspector.getHeight());
    f.settle();
    // On its row, inside the section, clear of its label.
    CHECK(harmony.getWidth() > 0);
    CHECK(harmony.getRight() <= inspector.getWidth());
    harmony.setSelectedId(2, juce::sendNotificationSync); // On, as a click would
    f.session->updateSnapshotNow();
    f.settle();
    CHECK(f.session->snapshot()->channel(f.bass)->harmony == fd::model::HarmonyUse::On);
    CHECK(harmony.getText() == "On");
    if (const char* dir = std::getenv("FD_DUMP_DIR"))
        fd::testing::dumpPng(
            dir, "inspector-track-harmony@2x.png",
            inspector.createComponentSnapshot(inspector.getLocalBounds().withHeight(320), true, 2.0f));
    // A return track has no Harmony.
    const auto ret = f.session->addReturnTrack();
    f.session->updateSnapshotNow();
    f.shell->arrangement().selectTrack(ret);
    f.settle();
    CHECK_FALSE(harmony.isVisible());
}
