// The tools that transform and generate notes (docs/design/piano-roll.md §12, §23): the catalogue
// is consistent, settings keep to their ranges, and each tool does what it says, the same for the
// same seed.

#include "model/NoteToolkit.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace fd::model;
using Catch::Approx;

namespace
{
const fd::music::Key cMajor{0, "major"};

Note note(int pitch, double start, double length = 1.0, int velocity = 100)
{
    return {pitch, start, length, static_cast<float>(velocity) / 127.0f};
}

/// A context at 120 BPM (a beat is half a second), C major, writing into the first bar.
ToolContext context(const fd::music::Key* key = &cMajor, std::pair<double, double> span = {0.0, 4.0})
{
    return {.key = key, .beatsPerBar = 4.0, .secondsPerBeat = 0.5, .origin = 0.0, .span = span};
}

ToolRun run(std::string_view id, const std::vector<Note>& notes, ToolValues values = {},
            const std::set<std::size_t>& selection = {}, const ToolContext& c = context())
{
    const auto* tool = findNoteTool(id);
    REQUIRE(tool != nullptr);
    return runNoteTool(*tool, notes, selection, values, c);
}

std::vector<int> pitchesOf(const std::vector<Note>& notes)
{
    std::vector<int> out;
    for (const auto& n : notes)
        out.push_back(n.pitch);
    return out;
}

std::vector<Note> cMajorChord(double start = 0.0, double length = 1.0)
{
    return {note(60, start, length), note(64, start, length), note(67, start, length)};
}
} // namespace

TEST_CASE("Note tools: the catalogue is consistent", "[fast]")
{
    const auto& tools = noteTools();
    REQUIRE(tools.size() == 12);
    std::set<std::string_view> ids;
    int transforms = 0;
    for (const auto& tool : tools)
    {
        INFO(tool.id);
        CHECK(ids.insert(tool.id).second);
        CHECK_FALSE(tool.name.empty());
        CHECK_FALSE(tool.about.empty());
        transforms += tool.kind == ToolKind::Transform ? 1 : 0;
        std::set<std::string_view> settings;
        for (const auto& s : tool.settings)
        {
            INFO(s.id);
            CHECK(settings.insert(s.id).second);
            CHECK_FALSE(s.label.empty());
            if (s.isChoice())
            {
                CHECK(s.choices.size() == s.words.size());
                CHECK(s.initial < static_cast<double>(s.choices.size()));
            }
            else
            {
                CHECK(s.min <= s.initial);
                CHECK(s.initial <= s.max);
            }
            if (!s.when.empty())
            {
                const auto* other = tool.setting(s.when);
                REQUIRE(other != nullptr);
                CHECK(other->isChoice());
                CHECK(s.whenIs < static_cast<int>(other->choices.size()));
            }
        }
        CHECK(findNoteTool(tool.id) == &tool);
    }
    CHECK(transforms == 8);
    CHECK(findNoteTool("nonsense") == nullptr);
}

TEST_CASE("Note tools: settings take their defaults, keep to their ranges and steps; text as shown", "[fast]")
{
    const auto& strum = *findNoteTool("strum");
    auto v = toolValues(strum, {{"time", 9999.0}, {"direction", 7.4}, {"bogus", 1.0}});
    CHECK(v.at("time") == 250.0);
    CHECK(v.at("direction") == 2.0);
    CHECK(v.at("curve") == 0.0);
    CHECK(v.at("velocity") == 0.0);
    CHECK_FALSE(v.contains("bogus"));
    CHECK(settingText(*strum.setting("time"), 40.0, nullptr) == "40 ms");
    CHECK(settingText(*strum.setting("velocity"), -8.0, nullptr) == "\xE2\x88\x92"
                                                                    "8 per note");
    CHECK(settingText(*strum.setting("direction"), 1.0, nullptr) == "Down");
    const auto& rhythm = *findNoteTool("rhythm");
    CHECK(settingText(*rhythm.setting("pitch"), 60.0, nullptr) == "C3");
    // The Velocity tool's settings show for their shape.
    const auto& velocity = *findNoteTool("velocity");
    CHECK(settingShown(velocity, *velocity.setting("from"), {{"shape", 0.0}}));
    CHECK_FALSE(settingShown(velocity, *velocity.setting("from"), {{"shape", 1.0}}));
    CHECK(settingShown(velocity, *velocity.setting("cycles"), {{"shape", 1.0}}));
}

TEST_CASE("Note tools: Humanize moves a little, the same for the same seed", "[fast]")
{
    const std::vector<Note> notes{note(60, 1.0), note(62, 2.0), note(64, 3.0)};
    const auto a = run("humanize", notes).edit.notes, b = run("humanize", notes).edit.notes;
    CHECK(a == b);
    CHECK(a != run("humanize", notes, {{"seed", 2.0}}).edit.notes);
    for (std::size_t i = 0; i < a.size(); ++i)
    {
        CHECK(std::abs(a[i].start - notes[i].start) <= 0.03 + 1e-9); // 15 ms at 120 BPM
        CHECK(std::abs(a[i].velocity - notes[i].velocity) <= 10.5f / 127.0f);
        CHECK(std::abs(a[i].length - notes[i].length) <= 0.1 + 1e-9);
    }
}

TEST_CASE("Note tools: Strum spreads a chord's notes, ending where they did", "[fast]")
{
    auto out = run("strum", cMajorChord(), {{"time", 50.0}, {"velocity", -8.0}}).edit.notes;
    REQUIRE(out.size() == 3);
    CHECK(out[0].pitch == 60);
    CHECK(out[1].start == Approx(0.1)); // 50 ms = a tenth of a beat
    CHECK(out[2].start == Approx(0.2));
    CHECK(out[2].start + out[2].length == Approx(1.0));
    CHECK(std::lround(out[2].velocity * 127.0f) == 84);
    // Down: the top note first.
    out = run("strum", cMajorChord(), {{"direction", 1.0}, {"time", 50.0}}).edit.notes;
    const auto first =
        std::min_element(out.begin(), out.end(), [](auto& a, auto& b) { return a.start < b.start; });
    CHECK(first->pitch == 67);
}

TEST_CASE("Note tools: Arpeggiate plays a chord's notes one after another over its length", "[fast]")
{
    const auto out = run("arpeggiate", cMajorChord(0.0, 1.0)).edit.notes; // 1/16, up, gate 80 %
    REQUIRE(out.size() == 4);
    CHECK(pitchesOf(out) == std::vector<int>{60, 64, 67, 60});
    CHECK(out[1].start == Approx(0.25));
    CHECK(out[0].length == Approx(0.2));
    const auto updown =
        run("arpeggiate", cMajorChord(0.0, 2.0), {{"order", 2.0}, {"octaves", 2.0}}).edit.notes;
    CHECK(pitchesOf(updown) == std::vector<int>{60, 64, 67, 72, 76, 79, 76, 72});
    // Random: the same for the same seed.
    CHECK(run("arpeggiate", cMajorChord(0.0, 2.0), {{"order", 4.0}}).edit.notes ==
          run("arpeggiate", cMajorChord(0.0, 2.0), {{"order", 4.0}}).edit.notes);
}

TEST_CASE("Note tools: Chop, Ornament, Span and Velocity", "[fast]")
{
    // Chop: four even pieces, falling by half.
    auto out = run("chop", {note(60, 0.0, 2.0)}, {{"velocity", 1.0}}).edit.notes;
    REQUIRE(out.size() == 4);
    CHECK(out[3].start == Approx(1.5));
    CHECK(out[3].length == Approx(0.5));
    CHECK(std::lround(out[3].velocity * 127.0f) == 50);

    // Ornament: a flam before; grace notes from a step of the key above.
    out = run("ornament", {note(64, 1.0)}).edit.notes;
    REQUIRE(out.size() == 2);
    CHECK(out[0].pitch == 64);
    CHECK(out[0].start == Approx(1.0 - 0.06)); // 30 ms
    CHECK(std::lround(out[0].velocity * 127.0f) == 60);
    out = run("ornament", {note(64, 1.0)}, {{"kind", 1.0}, {"notes", 1.0}}).edit.notes;
    CHECK(pitchesOf(out) == std::vector<int>{67, 65, 64});          // G then F, from above, to E
    CHECK(run("ornament", {note(64, 0.0)}).edit.notes.size() == 1); // nothing before 0

    // Span: legato, and half.
    const std::vector<Note> line{note(60, 0.0, 0.25), note(62, 1.0, 0.25), note(64, 3.0, 0.25)};
    out = run("span", line).edit.notes;
    CHECK(out[0].length == Approx(1.0));
    CHECK(out[1].length == Approx(2.0));
    CHECK(out[2].length == Approx(0.25)); // the last keeps its length
    CHECK(run("span", line, {{"length", 50.0}}).edit.notes[0].length == Approx(0.5));

    // Velocity: a ramp; a range squeezed to the middle.
    const std::vector<Note> four{note(60, 0.0, 1, 20), note(60, 1.0, 1, 60), note(60, 2.0, 1, 100),
                                 note(60, 3.0, 1, 120)};
    out = run("velocity", four, {{"from", 10.0}, {"to", 100.0}}).edit.notes;
    CHECK(std::lround(out[0].velocity * 127.0f) == 10);
    CHECK(std::lround(out[3].velocity * 127.0f) == 100);
    out = run("velocity", four, {{"shape", 2.0}, {"range", -100.0}}).edit.notes;
    CHECK(std::lround(out[0].velocity * 127.0f) == std::lround(out[3].velocity * 127.0f));
}

TEST_CASE("Note tools: Randomise pitch stays in the key and its range; transforms keep the rest and select "
          "what they made",
          "[fast]")
{
    std::vector<Note> notes;
    for (int k = 0; k < 32; ++k)
        notes.push_back(note(60 + (k % 3) * 2, k * 0.5, 0.5));
    const auto out = run("randomise", notes, {{"range", 3.0}}).edit.notes;
    bool moved = false;
    for (std::size_t i = 0; i < out.size(); ++i)
    {
        CHECK(fd::music::contains(cMajor, out[i].pitch));
        CHECK(std::abs(out[i].pitch - 64) <= 9);
        moved = moved || out[i].pitch != notes[i].pitch;
    }
    CHECK(moved);
    // Only the selected notes change, and they're selected after.
    const auto chopped = run("chop", notes, {}, {0});
    CHECK(chopped.edit.notes.size() == notes.size() + 3);
    CHECK(chopped.edit.selection.size() == 4);
    // On every note: nothing selected after.
    CHECK(run("chop", notes).edit.selection.empty());
    // Without notes: why not.
    CHECK_FALSE(run("chop", {}).why.empty());
}

TEST_CASE("Note tools: Rhythm writes a Euclidean pattern, on a pitch or on the chords there", "[fast]")
{
    const auto out = run("rhythm", {}).edit; // 16 steps of 1/16, 5 pulses, over a bar
    CHECK(out.notes.size() == 5);
    CHECK(out.selection.size() == 5);
    for (const auto& n : out.notes)
    {
        CHECK(n.pitch == 60);
        CHECK(n.length == Approx(0.25));
    }
    CHECK(out.notes.front().start == 0.0);
    // Rotated, it starts later.
    CHECK(run("rhythm", {}, {{"rotate", 1.0}}).edit.notes.front().start > 0.0);
    // On the chords: the held chord becomes stabs.
    const auto stabs = run("rhythm", cMajorChord(0.0, 4.0), {{"on", 1.0}, {"pulses", 4.0}}).edit.notes;
    CHECK(stabs.size() == 12);
    for (const auto& n : stabs)
        CHECK(n.length == Approx(0.25));
}

TEST_CASE("Note tools: Melody stays in the key and its range; Keep and Replace", "[fast]")
{
    const auto out =
        run("melody", {}, {{"density", 100.0}, {"step", 1.0}}, {}, context(&cMajor, {0.0, 8.0})).edit.notes;
    CHECK(out.size() == 32);
    for (const auto& n : out)
    {
        CHECK(fd::music::contains(cMajor, n.pitch));
        CHECK(n.pitch >= 60);
        CHECK(n.pitch <= 72);
    }
    CHECK(run("melody", {}).edit.notes == run("melody", {}).edit.notes);
    // Keep leaves a kept note's pitch alone where it sounds; Replace clears the span first.
    const std::vector<Note> held{note(64, 0.0, 4.0)};
    const auto kept = run("rhythm", held, {{"pitch", 64.0}}).edit.notes;
    CHECK(kept.size() == 1);
    const auto replaced = run("rhythm", held, {{"pitch", 64.0}, {"existing", 1.0}}).edit.notes;
    CHECK(replaced.size() == 5);
}

TEST_CASE("Note tools: Chords writes a progression of the key's chords; Bassline follows them", "[fast]")
{
    // The chords as named without their inversions: Smooth voices each nearest the one before.
    const auto named = [](const std::vector<Note>& notes)
    {
        std::vector<std::string> names;
        for (auto span : chordSpans(notes))
        {
            span.chord.bass.reset();
            names.push_back(fd::music::chordName(span.chord, &cMajor));
        }
        return names;
    };
    auto out = run("chords", {}, {}, {}, context(&cMajor, {0.0, 16.0})).edit.notes; // 1–5–6–4, a bar each
    auto names = named(out);
    CHECK(names == std::vector<std::string>{"C", "G", "Am", "F"});
    // Sevenths, two beats each.
    out = run("chords", {}, {{"kind", 1.0}, {"each", 1.0}}, {}, context(&cMajor, {0.0, 4.0})).edit.notes;
    CHECK(named(out) == std::vector<std::string>{"Cmaj7", "G7"});
    // Suggested: the same for the same seed.
    CHECK(run("chords", {}, {{"progression", 5.0}}).edit.notes ==
          run("chords", {}, {{"progression", 5.0}}).edit.notes);

    // Bassline: the chords' roots around C1, a beat each.
    const auto progression = run("chords", {}, {}, {}, context(&cMajor, {0.0, 8.0})).edit.notes;
    const auto bass = run("bassline", progression, {}, {}, context(&cMajor, {0.0, 8.0})).edit;
    std::vector<int> low;
    for (auto i : bass.selection)
        low.push_back(bass.notes[i].pitch);
    CHECK(low == std::vector<int>{36, 36, 36, 36, 31, 31, 31, 31}); // C1, then the G nearest it (G0)
    CHECK_FALSE(run("bassline", {}).why.empty());                   // no chords to follow
}
