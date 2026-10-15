// MIDI learn's maths (docs/design/midi-learn.md §3, §5): labels, what learning makes, relative
// encoders, takeover, Min and Max, buttons.
#include "model/AppSettings.h"
#include "model/MidiMap.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace fd::model;
using Catch::Approx;

namespace
{
MidiMapping knob(float min = 0.0f, float max = 1.0f)
{
    return {.source = {.kind = MidiSource::Kind::Cc, .channel = 1, .number = 21, .device = "Launchkey"},
            .target = {.channel = 5, .key = "m7/freq"},
            .min = min,
            .max = max};
}
} // namespace

TEST_CASE("midi map: a source's label and text; what the first message makes", "[fast]")
{
    MidiSource cc{.kind = MidiSource::Kind::Cc, .channel = 1, .number = 21};
    CHECK(sourceLabel(cc) == "CC 21");
    CHECK(sourceText(cc) == "CC 21");
    cc.channel = 2;
    CHECK(sourceText(cc) == "CC 21 \xc2\xb7 Ch 2");
    CHECK(sourceLabel({.kind = MidiSource::Kind::Note, .number = 60}) == "C3");
    CHECK(sourceLabel({.kind = MidiSource::Kind::PitchBend}) == "PB");
    CHECK(sourceText({.kind = MidiSource::Kind::PitchBend}) == "Pitch bend");
    CHECK(sourceText({.kind = MidiSource::Kind::Cc14, .number = 1}) == "CC 1 (14-bit)");

    CHECK(learnedMode({.kind = MidiSource::Kind::Note, .number = 36}, 100) == MidiMode::Toggle);
    CHECK(learnedMode(cc, 64) == MidiMode::Absolute);
    CHECK(learnedMode(cc, 127) == MidiMode::Toggle); // a button's press
    CHECK(learnedMode({.kind = MidiSource::Kind::PitchBend}, 127) == MidiMode::Absolute);
    CHECK(modesFor(MidiSource::Kind::Note) == std::vector{MidiMode::Toggle, MidiMode::Momentary});
    CHECK(modesFor(MidiSource::Kind::PitchBend) == std::vector{MidiMode::Absolute});
    CHECK(modesFor(MidiSource::Kind::Cc).size() == 6);
    CHECK(modeName(MidiMode::RelativeTwos) == "Relative (two's)");
}

TEST_CASE("midi map: relative encoders guessed from their first values and decoded", "[fast]")
{
    // An absolute knob turned up from the bottom never repeats itself: not a guess.
    CHECK_FALSE(guessRelative({1, 2, 3, 4}));
    CHECK_FALSE(guessRelative({1}));
    // Two's complement: 1 up, 127 down, repeated as it turns.
    CHECK(guessRelative({1, 1, 2, 127}) == MidiMode::RelativeTwos);
    // Binary offset: around 64.
    CHECK(guessRelative({65, 65, 66, 63}) == MidiMode::RelativeOffset);
    // Signed bit: 1 up, 65 down.
    CHECK(guessRelative({65, 65, 66, 1}) == MidiMode::RelativeSigned);
    // A fader resting and nudged: repeats, but far from any encoder's steps.
    CHECK_FALSE(guessRelative({40, 40, 41}));

    CHECK(relativeSteps(MidiMode::RelativeTwos, 1) == 1);
    CHECK(relativeSteps(MidiMode::RelativeTwos, 127) == -1);
    CHECK(relativeSteps(MidiMode::RelativeTwos, 125) == -3);
    CHECK(relativeSteps(MidiMode::RelativeOffset, 66) == 2);
    CHECK(relativeSteps(MidiMode::RelativeOffset, 63) == -1);
    CHECK(relativeSteps(MidiMode::RelativeSigned, 3) == 3);
    CHECK(relativeSteps(MidiMode::RelativeSigned, 67) == -3);
    CHECK(relativeSteps(MidiMode::Absolute, 67) == 0);

    // Steps of the control's small step, kept within Min and Max; inverted turns the other way.
    CHECK(relativeMove(knob(), 3, 0.01f, 0.5f) == Approx(0.53f));
    CHECK(relativeMove(knob(0.2f, 0.6f), 100, 0.01f, 0.5f) == Approx(0.6f));
    CHECK(relativeMove(knob(1.0f, 0.0f), 3, 0.01f, 0.5f) == Approx(0.47f));
}

TEST_CASE("midi map: takeover: pick up waits until the hardware passes the value, then follows", "[fast]")
{
    const auto m = knob();
    MidiFollow follow;
    // The control at 0.5, the hardware at the bottom: it stays.
    CHECK_FALSE(absoluteMove(m, 0.1f, 0.5f, MidiTakeover::PickUp, follow));
    CHECK_FALSE(absoluteMove(m, 0.3f, 0.5f, MidiTakeover::PickUp, follow));
    // Passing it: caught, and it follows from there.
    CHECK(absoluteMove(m, 0.55f, 0.5f, MidiTakeover::PickUp, follow) == Approx(0.55f));
    CHECK(follow.following);
    CHECK(absoluteMove(m, 0.2f, 0.55f, MidiTakeover::PickUp, follow) == Approx(0.2f));
    // The mouse moves it meanwhile: picked up again.
    CHECK_FALSE(absoluteMove(m, 0.25f, 0.9f, MidiTakeover::PickUp, follow));
    CHECK_FALSE(follow.following);
    // Within a step of it: caught at once.
    MidiFollow fresh;
    CHECK(absoluteMove(m, 0.501f, 0.5f, MidiTakeover::PickUp, fresh) == Approx(0.501f));
}

TEST_CASE("midi map: takeover: jump goes straight there; scale meets the control at the end", "[fast]")
{
    const auto m = knob();
    MidiFollow follow;
    CHECK(absoluteMove(m, 0.1f, 0.5f, MidiTakeover::Jump, follow) == Approx(0.1f));

    // Scale: the hardware at 0.2, the control at 0.6. Up by 0.4 of the 0.8 left: the control half
    // of its 0.4 left.
    MidiFollow scale;
    CHECK_FALSE(absoluteMove(m, 0.2f, 0.6f, MidiTakeover::Scale, scale)); // where the hardware is
    CHECK(absoluteMove(m, 0.6f, 0.6f, MidiTakeover::Scale, scale) == Approx(0.8f));
    CHECK(absoluteMove(m, 1.0f, 0.8f, MidiTakeover::Scale, scale) == Approx(1.0f));
    CHECK(scale.following);
    // Down from the top: they move together now.
    CHECK(absoluteMove(m, 0.7f, 1.0f, MidiTakeover::Scale, scale) == Approx(0.7f));
}

TEST_CASE("midi map: Min and Max; a Max below the Min turns it the other way", "[fast]")
{
    MidiFollow follow;
    const auto narrow = knob(0.2f, 0.6f);
    CHECK(absoluteMove(narrow, 0.0f, 0.2f, MidiTakeover::Jump, follow) == Approx(0.2f));
    CHECK(absoluteMove(narrow, 0.5f, 0.2f, MidiTakeover::Jump, follow) == Approx(0.4f));
    CHECK(absoluteMove(narrow, 1.0f, 0.4f, MidiTakeover::Jump, follow) == Approx(0.6f));
    MidiFollow other;
    const auto inverted = knob(1.0f, 0.0f);
    CHECK(absoluteMove(inverted, 0.25f, 0.75f, MidiTakeover::PickUp, other) == Approx(0.75f));
    CHECK(absoluteMove(inverted, 1.0f, 0.75f, MidiTakeover::PickUp, other) == Approx(0.0f));
}

TEST_CASE("midi map: buttons: toggle flips a switch, a knob between Min and Max, steps a choice; momentary "
          "holds",
          "[fast]")
{
    auto toggle = knob(0.0f, 1.0f);
    toggle.mode = MidiMode::Toggle;
    CHECK(buttonMove(toggle, true, 2, 0.0f) == Approx(1.0f));
    CHECK(buttonMove(toggle, true, 2, 1.0f) == Approx(0.0f));
    CHECK_FALSE(buttonMove(toggle, false, 2, 1.0f)); // a release does nothing
    auto range = knob(0.25f, 0.75f);
    range.mode = MidiMode::Toggle;
    CHECK(buttonMove(range, true, 0, 0.3f) == Approx(0.75f));
    CHECK(buttonMove(range, true, 0, 0.75f) == Approx(0.25f));
    // A choice of four: 0, 1/3, 2/3, 1, then back to the first.
    CHECK(buttonMove(toggle, true, 4, 0.0f) == Approx(1.0f / 3.0f));
    CHECK(buttonMove(toggle, true, 4, 2.0f / 3.0f) == Approx(1.0f));
    CHECK(buttonMove(toggle, true, 4, 1.0f) == Approx(0.0f));

    auto momentary = knob();
    momentary.mode = MidiMode::Momentary;
    CHECK(buttonMove(momentary, true, 2, 0.0f) == Approx(1.0f));
    CHECK(buttonMove(momentary, false, 2, 1.0f) == Approx(0.0f));
    CHECK(isButton(MidiMode::Momentary));
    CHECK_FALSE(isButton(MidiMode::RelativeTwos));
    CHECK(isRelative(MidiMode::RelativeSigned));
}

TEST_CASE("midi map: a mapping to JSON and back; the transport's kept in the app's settings", "[fast]")
{
    MidiMapping m{
        .source = {.kind = MidiSource::Kind::Cc14, .channel = 3, .number = 7, .device = "Faderport"},
        .target = {.channel = 42, .key = "volume"},
        .min = 0.25f,
        .max = 0.75f};
    CHECK(midiMappingFromJson(toJson(m)) == m);
    m.mode = MidiMode::Toggle; // a 14-bit control only sets
    CHECK_FALSE(midiMappingFromJson(toJson(m)));
    CHECK_FALSE(midiMappingFromJson(nlohmann::json::object()));
    CHECK(takeoverFromKey(takeoverKey(MidiTakeover::Scale)) == MidiTakeover::Scale);

    AppSettings s;
    s.controllers.takeover = MidiTakeover::Jump;
    s.controllers.sendBack = {"Faderport"};
    s.controllers.transport = {{.source = {.kind = MidiSource::Kind::Note, .number = 36, .device = "Foot"},
                                .target = {.key = "transport.record"},
                                .mode = MidiMode::Toggle}};
    const auto back = appSettingsFromJson(toJson(s));
    CHECK(back.controllers == s.controllers);
    CHECK(appSettingsFromJson("{}").controllers.takeover == MidiTakeover::PickUp);
}

TEST_CASE("midi map: values sent back follow Min and Max in the source's resolution", "[fast]")
{
    auto m = knob(0.2f, 0.6f);
    CHECK(feedbackValue(m, 0.2f) == 0);
    CHECK(std::abs(feedbackValue(m, 0.4f) - 64) <= 1); // the middle: 63.5
    CHECK(feedbackValue(m, 0.9f) == 127);
    m.source.kind = MidiSource::Kind::Cc14;
    CHECK(feedbackValue(m, 0.6f) == 16383);
    m.source.kind = MidiSource::Kind::Note;
    m.mode = MidiMode::Toggle;
    CHECK(feedbackValue(m, 0.6f) == 127);
    CHECK(feedbackValue(m, 0.3f) == 0);
    CHECK(fullScale(MidiSource::Kind::PitchBend) == 16383);
}
