// The song's sections (M3 step C3, ADR 0049, docs/design/arranger.md §17): marks, and sections
// that carry their music, through engine::Session, each command one undo step.

#include "RecordingFixture.h"
#include "fd/testkit/WavIO.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>

using Catch::Approx;
using namespace recording_test;

namespace
{
/// A sine of `seconds` at `hz` as a clip at `start` on track `t`.
std::uint64_t tone(Recording& r, std::uint64_t t, double start, double seconds, double hz)
{
    const double rate = 48000.0;
    fd::testkit::AudioBuffer sine(2, static_cast<int>(seconds * rate));
    for (int c = 0; c < 2; ++c)
        for (int i = 0; i < sine.numSamples(); ++i)
            sine.at(c, i) =
                0.3f * static_cast<float>(std::sin(2.0 * juce::MathConstants<double>::pi * hz * i / rate));
    REQUIRE((r.root.isDirectory() || r.root.createDirectory()));
    const auto file = r.root.getChildFile("tone-" + juce::Uuid().toString() + ".wav");
    REQUIRE(fd::testkit::writeWav(file.getFullPathName().toStdString(), sine, rate));
    const auto clip = r.session->insertAudioFile(t, file, start);
    REQUIRE(clip != 0);
    return clip;
}

std::shared_ptr<const fd::model::Snapshot> now(Recording& r)
{
    r.session->updateSnapshotNow();
    return r.session->snapshot();
}

std::string order(Recording& r)
{
    std::string out;
    for (const auto& s : now(r)->sections)
        out += (out.empty() ? "" : " ") + s.name;
    return out;
}

/// Where each clip of a track starts, in order.
std::vector<double> starts(Recording& r, std::uint64_t track)
{
    std::vector<double> out;
    for (const auto& c : now(r)->channel(track)->clips)
        out.push_back(c.start);
    return out;
}

/// Whether two lists of times match, to a microsecond.
bool near(const std::vector<double>& a, const std::vector<double>& b)
{
    if (a.size() != b.size())
        return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (std::abs(a[i] - b[i]) > 1e-6)
            return false;
    return true;
}

/// The mix of [start, end), rendered offline.
std::vector<std::vector<float>> render(Recording& r, double start, double end)
{
    juce::TemporaryFile out(".wav");
    bool done = false;
    auto job = r.session->render({start, end, std::nullopt, r.session->sampleRate()}, out.getFile(),
                                 [&](const std::string&) { done = true; });
    REQUIRE(job != nullptr);
    for (int i = 0; i < 1000 && !done; ++i)
        juce::MessageManager::getInstance()->runDispatchLoopUntil(10);
    REQUIRE(done);
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(out.getFile()));
    REQUIRE(reader != nullptr);
    juce::AudioBuffer<float> buffer(static_cast<int>(reader->numChannels),
                                    static_cast<int>(reader->lengthInSamples));
    reader->read(&buffer, 0, buffer.getNumSamples(), 0, true, true);
    std::vector<std::vector<float>> channels;
    for (int c = 0; c < buffer.getNumChannels(); ++c)
        channels.emplace_back(buffer.getReadPointer(c), buffer.getReadPointer(c) + buffer.getNumSamples());
    return channels;
}

float peakDifference(const std::vector<std::vector<float>>& a, const std::vector<std::vector<float>>& b)
{
    float worst = 0.0f;
    for (size_t c = 0; c < std::min(a.size(), b.size()); ++c)
        for (size_t i = 0; i < std::min(a[c].size(), b[c].size()); ++i)
            worst = std::max(worst, std::abs(a[c][i] - b[c][i]));
    return worst;
}

float peak(const std::vector<std::vector<float>>& a)
{
    float worst = 0.0f;
    for (const auto& c : a)
        for (const float v : c)
            worst = std::max(worst, std::abs(v));
    return worst;
}

/// A B C D, two seconds each, and a tone of its own in each on track 0.
void abcd(Recording& r)
{
    auto& s = *r.session;
    for (int i = 0; i < 4; ++i)
    {
        REQUIRE(s.addSection(2.0 * i, 2.0 * i + 2.0) != 0);
        tone(r, r.track(0), 2.0 * i + 0.25, 1.5, 220.0 * (i + 1));
    }
    REQUIRE(order(r) == "A B C D");
}

std::uint64_t idOf(Recording& r, const std::string& name)
{
    for (const auto& s : now(r)->sections)
        if (s.name == name)
            return s.id;
    FAIL("no section " << name);
    return 0;
}
} // namespace

TEST_CASE("sections: marked, renamed, coloured and resized; each one undo step", "[integration]")
{
    Recording r;
    auto& s = *r.session;
    const auto a = s.addSection(0.0, 4.0);
    REQUIRE(a != 0);
    CHECK(s.undoDescription() == "Add section");
    const auto b = s.addSection(4.0, 8.0, "Chorus");
    auto snap = now(r);
    REQUIRE(snap->sections.size() == 2);
    CHECK(snap->sections[0].name == "A");
    CHECK(snap->sections[1].name == "Chorus");
    CHECK(snap->sections[1].colour != snap->sections[0].colour); // the next colour
    CHECK(s.addSection(3.0, 5.0) == 0);                          // it would overlap

    CHECK(s.renameSection(a, " Intro "));
    CHECK(now(r)->section(a)->name == "Intro");
    CHECK(s.undoDescription() == "Rename section");
    CHECK(s.setSectionColour(b, 5));
    CHECK(now(r)->section(b)->colour == 5);
    CHECK(s.setSectionMarks(b, 4.0, 10.0)); // the music stays
    CHECK(now(r)->section(b)->end == Approx(10.0));
    CHECK(s.undoDescription() == "Move section mark");
    CHECK_FALSE(s.setSectionMarks(b, 2.0, 10.0)); // onto Intro

    s.undo();
    CHECK(now(r)->section(b)->end == Approx(8.0));
    // Saved with the project, and back.
    const auto file = r.root.getChildFile("sections.segno");
    REQUIRE((r.root.isDirectory() || r.root.createDirectory()));
    REQUIRE(s.saveAs(file));
    REQUIRE(s.open(file));
    CHECK(order(r) == "Intro Chorus");
}

TEST_CASE("sections: moving one takes its music, and the rest makes room", "[integration]")
{
    Recording r;
    auto& s = *r.session;
    abcd(r);
    const auto t = r.track(0);
    // A clip on another track crossing B's start, and a lane ramping over the song.
    const auto u = r.track(1);
    tone(r, u, 1.0, 2.0, 330.0); // 1-3 s: split at 2 s when B moves
    const auto lane = s.addAutomationLane();
    REQUIRE(s.setLanePoints(lane, {{0.0, 0.0f, 0.0f}, {8.0, 1.0f, 0.0f}}));
    const auto before = starts(r, t);

    REQUIRE(s.moveSections({idOf(r, "B")}, 6.0)); // after C
    CHECK(order(r) == "A C B D");
    CHECK(s.undoDescription() == "Move section B");
    CHECK(near(starts(r, t), {0.25, 2.25, 4.25, 6.25}));
    // B's tone (440 Hz) now from 4.25 s, C's (660 Hz) from 2.25 s.
    const auto snap = now(r);
    // The crossing clip was split at B's start: its first half stays, its second moved with B.
    CHECK(near(starts(r, u), {1.0, 4.0}));
    // The lane: C's values where B was, B's where C was.
    const auto* l = snap->automationLane(lane);
    REQUIRE(l != nullptr);
    CHECK(fd::model::automationValueAt(l->points, 3.0) == Approx(0.625f));
    CHECK(fd::model::automationValueAt(l->points, 5.0) == Approx(0.375f));

    // One undo step puts it all back.
    s.undo();
    CHECK(order(r) == "A B C D");
    CHECK(starts(r, t) == before);
    CHECK(near(starts(r, u), {1.0}));
    CHECK(fd::model::automationValueAt(now(r)->automationLane(lane)->points, 5.0) == Approx(0.625f));
}

TEST_CASE("sections: copied, inserted and deleted with their music", "[integration]")
{
    Recording r;
    auto& s = *r.session;
    abcd(r);
    const auto t = r.track(0);

    SECTION("copy B after C: A B C B 2 D")
    {
        const auto copies = s.copySections({idOf(r, "B")}, 6.0);
        REQUIRE(copies.size() == 1);
        CHECK(order(r) == "A B C B 2 D");
        CHECK(s.undoDescription() == "Copy section B");
        CHECK(near(starts(r, t), {0.25, 2.25, 4.25, 6.25, 8.25}));
        CHECK(now(r)->section(copies[0])->colour == now(r)->section(idOf(r, "B"))->colour);
    }
    SECTION("insert an empty section after B: A B E C D")
    {
        const auto e = s.insertSection(4.0, 2.0);
        REQUIRE(e != 0);
        CHECK(order(r) == "A B E C D");
        CHECK(near(starts(r, t), {0.25, 2.25, 6.25, 8.25}));
        CHECK(s.insertSection(5.0, 2.0) == 0); // inside a section
    }
    SECTION("delete B: A C D, the gap closed")
    {
        REQUIRE(s.deleteSections({idOf(r, "B")}));
        CHECK(order(r) == "A C D");
        CHECK(s.undoDescription() == "Delete section B");
        CHECK(near(starts(r, t), {0.25, 2.25, 4.25}));
        s.undo();
        CHECK(order(r) == "A B C D");
    }
    SECTION("remove B's mark: the music stays")
    {
        REQUIRE(s.deleteSections({idOf(r, "B")}, true));
        CHECK(order(r) == "A C D");
        CHECK(s.undoDescription() == "Remove section mark");
        CHECK(starts(r, t).size() == 4);
    }
}

TEST_CASE("sections: a moved section sounds the same in its new place, a copy the same twice",
          "[integration]")
{
    Recording r;
    auto& s = *r.session;
    abcd(r);
    const auto b = render(r, 2.0, 4.0);
    REQUIRE(peak(b) > 0.1f);

    SECTION("moved after C")
    {
        REQUIRE(s.moveSections({idOf(r, "B")}, 6.0));
        CHECK(peakDifference(render(r, 4.0, 6.0), b) < 1e-4f);
    }
    SECTION("copied after D")
    {
        REQUIRE(!s.copySections({idOf(r, "B")}, 8.0).empty());
        CHECK(peakDifference(render(r, 8.0, 10.0), b) < 1e-4f);
        CHECK(peakDifference(render(r, 2.0, 4.0), b) < 1e-4f);
    }
}

TEST_CASE("sections: clips that touch move as cleanly as clips with gaps", "[integration]")
{
    Recording r;
    auto& s = *r.session;
    const auto t = r.track(0);
    for (int i = 0; i < 3; ++i)
    {
        REQUIRE(s.addSection(2.0 * i, 2.0 * i + 2.0) != 0);
        REQUIRE(s.createMidiClip(t, 2.0 * i, 2.0) != 0);
    }
    REQUIRE(near(starts(r, t), {0.0, 2.0, 4.0}));
    REQUIRE(s.moveSections({idOf(r, "B")}, 6.0)); // after C
    CHECK(order(r) == "A C B");
    INFO("starts " << ::Catch::Detail::stringify(starts(r, t)));
    CHECK(near(starts(r, t), {0.0, 2.0, 4.0}));
}
