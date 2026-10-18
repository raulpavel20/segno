// MIDI effects, step 0 (docs/design/midi-effects.md §15, ADR 0074): what MIDI effects need from
// tracktion, checked before any is built, on modules written for the spike. Checked: a MIDI
// effect's notes reach the module after it, to the sample, and a chain of them adds up; the
// track's audio passes it untouched; a module processed in pieces (a moving parameter) keeps its
// notes' time (the sub-block-midi-time patch); the song reaches a module whole (SongView), so a
// generator plays the song's chords where its track has clips; notes read ahead play early and
// once; a track hears another's notes after that track's MIDI effects and apart from its own (the
// note-sources patch); running a module over a clip's notes off the audio thread gives what
// playback gave; and no note is left hanging when the song stops, jumps or loops, or the module
// is turned off or removed.
#include "RecordingFixture.h"
#include "SubtractiveSynth.h"
#include "engine/DevicePlugin.h"
#include "engine/EngineBehaviour.h"
#include "engine/Session.h"
#include "fd/testkit/WavIO.h"

#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <fd/device/NoteLedger.h>
#include <fd/device/Realtime.h>
#include <fd/device/Registry.h>
#include <fd/device/SongView.h>

namespace te = tracktion;
using namespace fd::testkit;

namespace
{
constexpr double kSampleRate = 48000.0;
constexpr int kBeat = 24000; // samples a beat at 120 bpm
constexpr fd::BusConfig kMidiEffect{
    .inputChannels = 0, .outputChannels = 0, .acceptsMidi = true, .producesMidi = true};

// ---- the spike's modules -----------------------------------------------------------------

/// The beat at a sample of the block, and the sample of a beat.
struct BlockTime
{
    double first = 0.0, perSample = 0.0;
    int numSamples = 0;

    explicit BlockTime(const fd::ProcessContext& c)
        : first(c.transport.ppqPosition), perSample(c.transport.tempoBpm / 60.0 / kSampleRate),
          numSamples(c.numSamples)
    {
    }
    [[nodiscard]] double end() const { return first + numSamples * perSample; }
    [[nodiscard]] int offsetOf(double beat) const
    {
        return std::clamp(static_cast<int>(std::lround((beat - first) / perSample)), 0,
                          std::max(0, numSamples - 1));
    }
};

/// Notes a module will end later: when, and which.
struct Pending
{
    std::array<std::pair<double, std::uint8_t>, 64> offs{};
    std::size_t count = 0;

    void add(double beat, std::uint8_t key) noexcept
    {
        if (count < offs.size())
            offs[count++] = {beat, key};
    }
    /// Writes the note-offs falling in this block.
    void play(const BlockTime& t, fd::NoteLedger& ledger, fd::EventList& out) noexcept
    {
        for (std::size_t i = 0; i < count;)
            if (offs[i].first < t.end())
            {
                ledger.write(out, {.sampleOffset = t.offsetOf(offs[i].first),
                                   .type = fd::EventType::NoteOff,
                                   .key = offs[i].second});
                offs[i] = offs[--count];
            }
            else
                ++i;
    }
};

/// Passes every event on, its notes moved by Amount semitones (a module after another's notes).
const std::array upParams{fd::ParamSpec{.id = "amount",
                                        .name = "Amount",
                                        .kind = fd::ParamKind::Discrete,
                                        .min = 0.0f,
                                        .max = 24.0f,
                                        .def = 12.0f}};
const fd::DeviceInfo upInfo{.id = "spike-up",
                            .name = "Up",
                            .category = "Test",
                            .kind = fd::DeviceKind::MidiEffect,
                            .buses = kMidiEffect,
                            .params = upParams};
class Up final : public fd::Device
{
public:
    Up() : Device(upInfo) {}
    void prepare(const fd::ProcessSpec&) override {}
    void reset() noexcept override { ledger_.clear(); }
    void process(fd::ProcessContext& c) noexcept override
    {
        const int amount = params().getInt(0);
        for (const auto& e : *c.events)
        {
            if (e.type == fd::EventType::AllNotesOff)
            {
                ledger_.endAll(*c.outEvents, e.sampleOffset);
                c.outEvents->push(e);
                continue;
            }
            auto out = e;
            if (e.type == fd::EventType::NoteOn || e.type == fd::EventType::NoteOff)
                out.key = static_cast<std::uint8_t>(std::clamp(e.key + amount, 0, 127));
            ledger_.write(*c.outEvents, out);
        }
    }

private:
    fd::NoteLedger ledger_;
};

/// A generator: on each beat where its track has a clip, the root of the song's chord there
/// (from C4), an eighth of a beat long. It ignores the notes it's given.
const fd::DeviceInfo rootsInfo{.id = "spike-roots",
                               .name = "Roots",
                               .category = "Test",
                               .kind = fd::DeviceKind::MidiEffect,
                               .buses = kMidiEffect};
class Roots final : public fd::Device
{
public:
    Roots() : Device(rootsInfo) {}
    void prepare(const fd::ProcessSpec&) override {}
    void reset() noexcept override
    {
        ledger_.clear();
        pending_.count = 0;
    }
    void process(fd::ProcessContext& c) noexcept override
    {
        const BlockTime t(c);
        pending_.play(t, ledger_, *c.outEvents);
        if (!c.transport.playing || c.song == nullptr)
            return;
        for (double beat = std::ceil(t.first - 1.0e-9); beat < t.end() - 1.0e-9; beat += 1.0)
            if (c.song->inClip(beat))
                if (const auto* chord = c.song->chordAt(beat))
                {
                    const auto key = static_cast<std::uint8_t>(60 + chord->root);
                    ledger_.write(*c.outEvents, {.sampleOffset = t.offsetOf(beat),
                                                 .type = fd::EventType::NoteOn,
                                                 .key = key,
                                                 .value = 0.8f});
                    pending_.add(beat + 0.125, key);
                }
        pending_.play(t, ledger_, *c.outEvents);
    }

private:
    fd::NoteLedger ledger_;
    Pending pending_;
};

/// Plays this track's clip notes a sixteenth early, read ahead from the song, and leaves out each
/// one as it arrives on time: every note once, early.
const fd::DeviceInfo earlyInfo{.id = "spike-early",
                               .name = "Early",
                               .category = "Test",
                               .kind = fd::DeviceKind::MidiEffect,
                               .buses = kMidiEffect};
class Early final : public fd::Device
{
public:
    static constexpr double lead = 0.25;
    Early() : Device(earlyInfo) {}
    void prepare(const fd::ProcessSpec&) override {}
    void reset() noexcept override
    {
        ledger_.clear();
        pending_.count = 0;
        played_.fill(0);
    }
    void process(fd::ProcessContext& c) noexcept override
    {
        const BlockTime t(c);
        for (const auto& e : *c.events)
        {
            // A note played early is its own, ended by its own note-off: the one arriving is left out.
            if ((e.type == fd::EventType::NoteOn || e.type == fd::EventType::NoteOff) && played_[e.key] > 0)
            {
                if (e.type == fd::EventType::NoteOff)
                    --played_[e.key];
                continue;
            }
            ledger_.write(*c.outEvents, e);
        }
        if (c.transport.playing && c.song != nullptr)
            for (const auto& n : c.song->notesFrom(t.first + lead, t.end() + lead))
            {
                ledger_.write(*c.outEvents, {.sampleOffset = t.offsetOf(n.start - lead),
                                             .type = fd::EventType::NoteOn,
                                             .channel = n.channel,
                                             .key = n.key,
                                             .value = n.velocity});
                pending_.add(n.start - lead + n.length, n.key);
                ++played_[n.key];
            }
        pending_.play(t, ledger_, *c.outEvents);
        c.outEvents->sortByTime();
    }

private:
    fd::NoteLedger ledger_;
    Pending pending_;
    std::array<int, 128> played_{};
};

/// For each note-on of the track it listens to, a note 50 semitones higher, 40 samples later (so
/// a probe's marks of the two never land on one sample).
const fd::DeviceInfo answerInfo{.id = "spike-answer",
                                .name = "Answer",
                                .category = "Test",
                                .kind = fd::DeviceKind::MidiEffect,
                                .buses = kMidiEffect};
class Answer final : public fd::Device
{
public:
    Answer() : Device(answerInfo) {}
    void prepare(const fd::ProcessSpec&) override {}
    void reset() noexcept override { ledger_.clear(); }
    void process(fd::ProcessContext& c) noexcept override
    {
        for (const auto& e : *c.events)
            ledger_.write(*c.outEvents, e);
        if (c.listen != nullptr)
            for (const auto& e : *c.listen)
                if (e.type == fd::EventType::NoteOn)
                {
                    const auto key = static_cast<std::uint8_t>(std::min(127, e.key + 50));
                    const int at = std::min(e.sampleOffset + 40, c.numSamples - 1);
                    ledger_.write(
                        *c.outEvents,
                        {.sampleOffset = at, .type = fd::EventType::NoteOn, .key = key, .value = e.value});
                    ledger_.write(*c.outEvents, {.sampleOffset = std::min(at + 1, c.numSamples - 1),
                                                 .type = fd::EventType::NoteOff,
                                                 .key = key});
                }
        c.outEvents->sortByTime();
    }

private:
    fd::NoteLedger ledger_;
};

/// Holds every note it's given until the song stops or jumps (an all notes off): a module whose
/// notes outlive the notes it was given, which only its ledger can end.
const fd::DeviceInfo holdInfo{.id = "spike-hold",
                              .name = "Hold",
                              .category = "Test",
                              .kind = fd::DeviceKind::MidiEffect,
                              .buses = kMidiEffect};
class Hold final : public fd::Device
{
public:
    Hold() : Device(holdInfo) {}
    void prepare(const fd::ProcessSpec&) override {}
    void reset() noexcept override { ledger_.clear(); }
    void process(fd::ProcessContext& c) noexcept override
    {
        for (const auto& e : *c.events)
            if (e.type == fd::EventType::AllNotesOff)
            {
                ledger_.endAll(*c.outEvents, e.sampleOffset);
                c.outEvents->push(e);
            }
            else if (e.type == fd::EventType::NoteOn && !ledger_.sounding(e.channel, e.key))
                ledger_.write(*c.outEvents, e);
            else if (e.type != fd::EventType::NoteOff && e.type != fd::EventType::NoteOn)
                c.outEvents->push(e);
    }

private:
    fd::NoteLedger ledger_;
};

/// An instrument marking what reaches it: a note-on's key / 256 at its sample, a note-off minus
/// its key / 512.
const fd::DeviceInfo probeInfo{.id = "note-probe",
                               .name = "Note probe",
                               .category = "Test",
                               .kind = fd::DeviceKind::Instrument,
                               .buses = {.inputChannels = 0, .outputChannels = 2, .acceptsMidi = true}};
class Probe final : public fd::Device
{
public:
    Probe() : Device(probeInfo) {}
    void prepare(const fd::ProcessSpec&) override {}
    void reset() noexcept override {}
    void process(fd::ProcessContext& c) noexcept override
    {
        for (int ch = 0; ch < c.output.numChannels; ++ch)
            std::fill_n(c.output.channel(ch), c.numSamples, 0.0f);
        for (const auto& e : *c.events)
        {
            float mark = 0.0f;
            if (e.type == fd::EventType::NoteOn)
                mark = static_cast<float>(e.key) / 256.0f;
            else if (e.type == fd::EventType::NoteOff)
                mark = -static_cast<float>(e.key) / 512.0f;
            for (int ch = 0; ch < c.output.numChannels; ++ch)
                c.output.channel(ch)[e.sampleOffset] += mark;
        }
    }
};

const fd::DeviceFactory upFactory{[]() -> const fd::DeviceInfo& { return upInfo; },
                                  []() -> std::unique_ptr<fd::Device> { return std::make_unique<Up>(); }};
const fd::DeviceFactory rootsFactory{[]() -> const fd::DeviceInfo& { return rootsInfo; },
                                     []() -> std::unique_ptr<fd::Device>
                                     { return std::make_unique<Roots>(); }};
const fd::DeviceFactory earlyFactory{[]() -> const fd::DeviceInfo& { return earlyInfo; },
                                     []() -> std::unique_ptr<fd::Device>
                                     { return std::make_unique<Early>(); }};
const fd::DeviceFactory answerFactory{[]() -> const fd::DeviceInfo& { return answerInfo; },
                                      []() -> std::unique_ptr<fd::Device>
                                      { return std::make_unique<Answer>(); }};
const fd::DeviceFactory holdFactory{[]() -> const fd::DeviceInfo& { return holdInfo; },
                                    []() -> std::unique_ptr<fd::Device> { return std::make_unique<Hold>(); }};
const fd::DeviceFactory probeFactory{[]() -> const fd::DeviceInfo& { return probeInfo; },
                                     []() -> std::unique_ptr<fd::Device>
                                     { return std::make_unique<Probe>(); }};

void registerSpikeModules(te::Engine& engine)
{
    for (const auto* f :
         {&upFactory, &rootsFactory, &earlyFactory, &answerFactory, &holdFactory, &probeFactory})
        fd::engine::registerDevice(engine, *f);
}

// ---- edits rendered offline ---------------------------------------------------------------

struct TestUIBehaviour final : te::UIBehaviour
{
    void runTaskWithProgressBar(te::ThreadPoolJobWithProgress& task) override
    {
        while (task.runJob() == juce::ThreadPoolJob::jobNeedsRunningAgain)
        {
        }
    }
};

struct Fixture
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    te::Engine engine{"fd-midi-effect-spike", std::make_unique<TestUIBehaviour>(),
                      fd::engine::makeEngineBehaviour()};
    std::unique_ptr<te::Edit> edit;

    Fixture()
    {
        fd::engine::registerDevices(engine);
        registerSpikeModules(engine);
        edit = te::Edit::createSingleTrackEdit(engine, te::Edit::EditRole::forRendering);
    }

    te::AudioTrack& track(int i)
    {
        while (static_cast<int>(te::getAudioTracks(*edit).size()) <= i)
            edit->insertNewAudioTrack(te::TrackInsertPoint(nullptr, te::getAudioTracks(*edit).getLast()),
                                      nullptr);
        return *te::getAudioTracks(*edit)[i];
    }

    fd::engine::DevicePlugin& add(te::AudioTrack& t, const std::string& id)
    {
        auto p = edit->getPluginCache().createNewPlugin(fd::engine::DevicePlugin::typeNameFor(id), {});
        REQUIRE(p != nullptr);
        t.pluginList.insertPlugin(p, -1, nullptr);
        return dynamic_cast<fd::engine::DevicePlugin&>(*p);
    }

    te::MidiClip& clip(te::AudioTrack& t, double startBeat = 0.0, double lengthBeats = 8.0)
    {
        auto c = t.insertMIDIClip({te::TimePosition::fromSeconds(startBeat * 0.5),
                                   te::TimeDuration::fromSeconds(lengthBeats * 0.5)},
                                  nullptr);
        REQUIRE(c != nullptr);
        return *c;
    }

    /// A note at a sample from the clip's start (at 120 bpm).
    static void note(te::MidiClip& c, int key, int atSample, double lengthBeats = 0.25)
    {
        c.getSequence().addNote(key, te::BeatPosition::fromBeats(atSample / static_cast<double>(kBeat)),
                                te::BeatDuration::fromBeats(lengthBeats), 100, 0, nullptr);
    }

    AudioBuffer render(double seconds, const juce::BigInteger* tracks = nullptr)
    {
        juce::TemporaryFile out(".wav");
        juce::WavAudioFormat wav;
        te::Renderer::Parameters p(*edit);
        p.destFile = out.getFile();
        p.audioFormat = &wav;
        p.bitDepth = 32;
        p.sampleRateForAudio = kSampleRate;
        p.blockSizeForAudio = 512;
        p.time = {te::TimePosition(), te::TimeDuration::fromSeconds(seconds)};
        p.tracksToDo = tracks != nullptr ? *tracks : te::toBitSet(te::getAllTracks(*edit));
        p.canRenderInMono = false;
        p.checkNodesForAudio = false;
        const auto file = te::Renderer::renderToFile("midi effect spike", p);
        REQUIRE(file.existsAsFile());
        auto read = readWav(file.getFullPathName().toStdString());
        REQUIRE(read.has_value());
        return read->buffer;
    }
};

/// Note-ons marked in channel 0, as (sample, key).
std::vector<std::pair<int, int>> noteOns(const AudioBuffer& b)
{
    std::vector<std::pair<int, int>> out;
    for (int i = 0; i < b.numSamples(); ++i)
        if (const float v = b.at(0, i); v > 1.0e-6f)
            out.emplace_back(i, static_cast<int>(std::lround(v * 256.0f)));
    return out;
}

std::string text(const std::vector<std::pair<int, int>>& notes)
{
    std::string out;
    for (const auto& [at, key] : notes)
        out += "(" + std::to_string(at) + ", " + std::to_string(key) + ") ";
    return out;
}

/// The song view for a track: its clips and their notes, with chords and sections given.
std::shared_ptr<fd::SongView> viewOf(te::AudioTrack& t, std::vector<fd::SongChord> chords = {})
{
    auto v = std::make_shared<fd::SongView>();
    v->chords = std::move(chords);
    for (auto* c : t.getClips())
        if (auto* midi = dynamic_cast<te::MidiClip*>(c))
        {
            const double start = midi->getStartBeat().inBeats();
            v->clips.push_back({start, midi->getEndBeat().inBeats()});
            for (auto* n : midi->getSequence().getNotes())
                v->notes.push_back({.start = start + n->getStartBeat().inBeats(),
                                    .length = n->getLengthBeats().inBeats(),
                                    .key = static_cast<std::uint8_t>(n->getNoteNumber()),
                                    .velocity = n->getVelocity() / 127.0f});
        }
    std::sort(v->notes.begin(), v->notes.end(),
              [](const auto& a, const auto& b) { return a.start < b.start; });
    return v;
}
} // namespace

TEST_CASE("midi effects (spike): a module's notes reach the next, to the sample, and add up", "[integration]")
{
    Fixture f;
    auto& t = f.track(0);
    f.add(t, "spike-up");
    f.add(t, "spike-up");
    f.add(t, "note-probe");
    auto& c = f.clip(t);
    Fixture::note(c, 48, kBeat / 2);
    Fixture::note(c, 50, 30'011); // mid-block
    const auto got = noteOns(f.render(2.0));
    // Up twice: an octave and an octave.
    const std::vector<std::pair<int, int>> expected{{kBeat / 2, 72}, {30'011, 74}};
    CHECK(got == expected);
}

TEST_CASE("midi effects (spike): the track's audio passes a MIDI effect untouched", "[integration]")
{
    Fixture f;
    auto& t = f.track(0);
    f.add(t, "note-probe"); // first: it hears the notes as written
    f.add(t, "spike-up");   // after the instrument, it changes no sound
    auto& c = f.clip(t);
    Fixture::note(c, 48, kBeat / 2);
    const auto got = noteOns(f.render(1.0));
    CHECK(got == std::vector<std::pair<int, int>>{{kBeat / 2, 48}});
}

TEST_CASE("midi effects (spike): a module processed in pieces keeps its notes' time", "[integration]")
{
    // A parameter with automation makes tracktion process the module in pieces of 128 samples
    // (fine-grain automation); a note in a later piece must still land on its sample.
    Fixture f;
    auto& t = f.track(0);
    auto& up = f.add(t, "spike-up");
    f.add(t, "note-probe");
    auto& curve = up.parameter(0).getCurve();
    curve.addPoint(te::TimePosition(), 0.5f, 0.0f, nullptr);
    curve.addPoint(te::TimePosition::fromSeconds(4.0), 0.5f, 0.0f, nullptr);
    REQUIRE(up.isAutomationNeeded());
    auto& c = f.clip(t);
    Fixture::note(c, 48, 512 * 20 + 300); // the third piece of its block
    Fixture::note(c, 50, 512 * 40 + 450); // the fourth
    const auto got = noteOns(f.render(1.0));
    CHECK(got == std::vector<std::pair<int, int>>{{512 * 20 + 300, 60}, {512 * 40 + 450, 62}});
}

TEST_CASE("midi effects (spike): a generator plays the song's chords where its track has clips",
          "[integration]")
{
    Fixture f;
    auto& t = f.track(0);
    auto& roots = f.add(t, "spike-roots");
    f.add(t, "note-probe");
    f.clip(t, 2.0, 4.0); // beats 2 to 6, no notes: it lets the generator play there
    roots.setSongView(
        viewOf(t, {{.start = 0.0, .end = 4.0, .root = 0}, {.start = 4.0, .end = 8.0, .root = 9}}));
    const auto got = noteOns(f.render(4.0));
    // C on beats 2 and 3, A on 4 and 5; nothing outside the clip.
    CHECK(got == std::vector<std::pair<int, int>>{
                     {2 * kBeat, 60}, {3 * kBeat, 60}, {4 * kBeat, 69}, {5 * kBeat, 69}});
}

TEST_CASE("midi effects (spike): notes read ahead play early, once", "[integration]")
{
    Fixture f;
    auto& t = f.track(0);
    auto& early = f.add(t, "spike-early");
    f.add(t, "note-probe");
    auto& c = f.clip(t);
    Fixture::note(c, 60, 2 * kBeat);
    Fixture::note(c, 64, 3 * kBeat + 1'234);
    early.setSongView(viewOf(t));
    const auto got = noteOns(f.render(3.0));
    const int lead = static_cast<int>(Early::lead * kBeat);
    CHECK(got == std::vector<std::pair<int, int>>{{2 * kBeat - lead, 60}, {3 * kBeat + 1'234 - lead, 64}});
}

TEST_CASE("midi effects (spike): a track hears another's notes, after its MIDI effects, apart from its own",
          "[integration]")
{
    Fixture f;
    auto& kick = f.track(0);
    auto& bass = f.track(1);
    f.add(kick, "spike-up"); // the kick's MIDI effect: what's heard comes after it
    f.add(kick, "note-probe");
    auto& answer = f.add(bass, "spike-answer");
    f.add(bass, "note-probe");
    answer.setListenTo(kick.itemID, nullptr);
    REQUIRE(answer.getNoteSourceID() == kick.itemID);
    auto& k = f.clip(kick);
    Fixture::note(k, 24, kBeat);          // heard as 36
    Fixture::note(k, 24, 2 * kBeat + 77); // mid-block
    auto& b = f.clip(bass);
    Fixture::note(b, 40, kBeat + 5'000); // the bass's own

    SECTION("the bass hears the kick's notes, and keeps its own")
    {
        auto all = te::toBitSet(te::getAllTracks(*f.edit));
        const auto mix = noteOns(f.render(2.0, &all));
        // The kick's probe marks 36 at its notes; the bass's marks 86 (36 + 50) 40 samples after
        // them, and its own 40, never 36 or 24: the kick's notes don't reach the bass's
        // instrument, and the bass hears them after the kick's Up.
        INFO("all: " << text(mix));
        CHECK(mix == std::vector<std::pair<int, int>>{{kBeat, 36},
                                                      {kBeat + 40, 86},
                                                      {kBeat + 5'000, 40},
                                                      {2 * kBeat + 77, 36},
                                                      {2 * kBeat + 117, 86}});
    }
    SECTION("with the kick muted, the bass still hears it, and the kick makes no sound")
    {
        // A muted track others hear plays its notes for them, silently (as a sidechain source
        // keeps processing): a muted guide part still leads.
        kick.setMute(true);
        auto all = te::toBitSet(te::getAllTracks(*f.edit));
        const auto mix = noteOns(f.render(2.0, &all));
        INFO("all: " << text(mix));
        CHECK(mix ==
              std::vector<std::pair<int, int>>{{kBeat + 40, 86}, {kBeat + 5'000, 40}, {2 * kBeat + 117, 86}});
    }
}

TEST_CASE("midi effects (spike): run over a clip's notes off the audio thread, a module plays what it played",
          "[integration]")
{
    Fixture f;
    auto& t = f.track(0);
    auto& early = f.add(t, "spike-early");
    f.add(t, "spike-up");
    f.add(t, "note-probe");
    auto& c = f.clip(t);
    Fixture::note(c, 48, kBeat, 1.0);
    Fixture::note(c, 52, kBeat + 6'000, 0.5);
    Fixture::note(c, 55, 2 * kBeat + 3'333, 0.25);
    const auto view = viewOf(t);
    early.setSongView(view);
    const auto played = noteOns(f.render(3.0));

    // The same modules, fresh, fed the clip's notes block by block as tracktion does.
    std::vector<std::unique_ptr<fd::Device>> chain;
    chain.push_back(earlyFactory.create());
    chain.push_back(upFactory.create());
    for (auto& d : chain)
    {
        d->prepare({.sampleRate = kSampleRate, .maxBlockSize = 512});
        d->reset();
    }
    std::vector<std::pair<int, int>> offline;
    fd::EventList in, out;
    in.reserve(256);
    out.reserve(256);
    for (int block = 0; block * 512 < static_cast<int>(3.0 * kSampleRate); ++block)
    {
        const int first = block * 512;
        in.clear();
        for (const auto& n : view->notes)
            for (const auto& [beat, type] : {std::pair{n.start, fd::EventType::NoteOn},
                                             std::pair{n.start + n.length, fd::EventType::NoteOff}})
                if (const int at = static_cast<int>(std::lround(beat * kBeat));
                    at >= first && at < first + 512)
                    in.push({.sampleOffset = at - first, .type = type, .key = n.key, .value = n.velocity});
        in.sortByTime();
        for (auto& d : chain)
        {
            out.clear();
            fd::ProcessContext ctx;
            ctx.events = &in;
            ctx.outEvents = &out;
            ctx.numSamples = 512;
            ctx.transport = {.tempoBpm = 120.0,
                             .ppqPosition = first / static_cast<double>(kBeat),
                             .samplePosition = first,
                             .playing = true};
            ctx.song = view.get();
            fd::processRealtime(*d, ctx);
            in.clear();
            for (const auto& e : out)
                in.push(e);
        }
        for (const auto& e : in)
            if (e.type == fd::EventType::NoteOn)
                offline.emplace_back(first + e.sampleOffset, e.key);
    }
    CHECK(offline == played);
}

// ---- in real time, through the Session ----------------------------------------------------

namespace
{
using recording_test::Recording;
constexpr double kPastRelease = 0.7;

int voices(Recording& r, std::uint64_t synth)
{
    auto* plugin = dynamic_cast<fd::engine::DevicePlugin*>(r.session->impl().plugin(synth));
    REQUIRE(plugin != nullptr);
    return static_cast<int>(plugin->device().analysis().read(fd::devices::SubtractiveSynth::ActiveVoices));
}

/// A track with Hold before a Subtractive and a clip of one short note at its start; the ids of
/// Hold, the synth and the clip.
struct Held
{
    std::uint64_t hold = 0, synth = 0, clip = 0;
};
Held held(Recording& r)
{
    registerSpikeModules(r.engine);
    const auto t = r.track(0);
    Held h;
    h.hold = r.session->addDevice(t, "fd:spike-hold");
    h.synth = r.session->addDevice(t, "fd:subtractive-synth");
    REQUIRE(h.hold != 0);
    REQUIRE(h.synth != 0);
    h.clip = r.session->createMidiClip(t, 0.0, 8.0);
    r.session->setNotes(h.clip, {{.pitch = 60, .start = 0.0, .length = 0.25}});
    r.session->setPosition(0.0);
    r.session->play();
    std::string trace;
    for (int i = 0; i < 12; ++i)
    {
        r.pump(0.05);
        trace += std::to_string(voices(r, h.synth)) + " ";
    }
    INFO("voices every 50 ms: " << trace);
    REQUIRE(voices(r, h.synth) == 1); // the note is over; Hold still sounds it
    return h;
}
} // namespace

TEST_CASE("midi effects (spike): no note a module started is left hanging", "[integration]")
{
    Recording r;
    const auto h = held(r);
    SECTION("the song stops")
    {
        r.session->stop();
    }
    SECTION("the song jumps")
    {
        r.session->setPosition(3.0);
    }
    SECTION("the module is turned off")
    {
        r.session->setDeviceEnabled(h.hold, false);
    }
    SECTION("the module is removed")
    {
        r.session->removeDevice(h.hold);
    }
    r.pump(kPastRelease);
    CHECK(voices(r, h.synth) == 0);
}

TEST_CASE("midi effects (spike): a loop's lap ends the notes of the lap before", "[integration]")
{
    Recording r;
    const auto h = held(r);
    r.session->setLoopRange(0.0, 1.0);
    r.session->setLooping(true);
    r.pump(3.5); // three laps and more: each lap plays the note again
    CHECK(voices(r, h.synth) == 1);
}
