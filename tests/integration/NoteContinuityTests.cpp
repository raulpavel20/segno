// No note is left hanging: whatever changes while the song plays, an instrument sounds only what the
// project plays at the playhead, or what a player holds (the stuck notes found in the M2 song test).
// Through engine::Session on tracktion's hosted test device, in real time.

#include "RecordingFixture.h"
#include "SubtractiveSynth.h"

#include <catch2/catch_test_macros.hpp>

using namespace recording_test;

namespace
{
constexpr double kPastRelease = 0.7; // the synth's release is 200 ms: well past its tail

/// A Subtractive Synth on each of the first `count` tracks; their device ids.
std::vector<std::uint64_t> synths(Recording& r, int count)
{
    std::vector<std::uint64_t> out;
    for (int i = 0; i < count; ++i)
    {
        const auto id = r.session->addDevice(r.track(static_cast<size_t>(i)), "fd:subtractive-synth");
        REQUIRE(id != 0);
        out.push_back(id);
    }
    return out;
}

/// How many voices the synth `device` plays now (its "Voices playing" meter).
int voices(Recording& r, std::uint64_t device)
{
    auto* plugin = dynamic_cast<fd::engine::DevicePlugin*>(r.session->impl().plugin(device));
    REQUIRE(plugin != nullptr);
    return static_cast<int>(plugin->device().analysis().read(fd::devices::SubtractiveSynth::ActiveVoices));
}

/// A MIDI clip of `beats` on `track` from 0, holding one note of `pitch` from beat 0 to the end.
std::uint64_t heldNote(Recording& r, std::uint64_t track, double beats = 16.0, int pitch = 60)
{
    const double seconds = beats * 0.5; // 120 BPM
    const auto clip = r.session->createMidiClip(track, 0.0, seconds);
    REQUIRE(clip != 0);
    r.session->setNotes(clip, {{.pitch = pitch, .start = 0.0, .length = beats}});
    return clip;
}

/// Plays from the start until the note sounds.
void playUntilSounding(Recording& r, std::uint64_t device)
{
    r.session->setPosition(0.0);
    r.session->play();
    r.pump(0.5);
    REQUIRE(voices(r, device) == 1);
}
} // namespace

TEST_CASE("notes: a clip moved to another track mid-note ends its note on the track it left", "[integration]")
{
    Recording r;
    const auto devices = synths(r, 2);
    const auto from = r.track(0), to = r.track(1);
    const auto clip = heldNote(r, from);
    playUntilSounding(r, devices[0]);

    r.session->applyClipEdits({{.clip = clip, .track = to, .start = 0.0, .length = 8.0}});
    r.pump(kPastRelease);
    CHECK(voices(r, devices[0]) == 0);
}

TEST_CASE("notes: a clip moved, trimmed or deleted under a sounding note ends it", "[integration]")
{
    Recording r;
    const auto device = synths(r, 1)[0];
    const auto t = r.track(0);
    SECTION("moved later on its track")
    {
        const auto clip = heldNote(r, t);
        playUntilSounding(r, device);
        r.session->applyClipEdits({{.clip = clip, .track = t, .start = 4.0, .length = 8.0}});
    }
    SECTION("deleted")
    {
        const auto clip = heldNote(r, t);
        playUntilSounding(r, device);
        r.session->deleteClips({clip});
    }
    SECTION("its end trimmed before the playhead")
    {
        const auto clip = heldNote(r, t);
        playUntilSounding(r, device);
        r.session->applyClipEdits({{.clip = clip, .track = t, .start = 0.0, .length = 0.25}});
    }
    r.pump(kPastRelease);
    CHECK(voices(r, device) == 0);
}

TEST_CASE("notes: a sounding note deleted, transposed or shortened ends", "[integration]")
{
    Recording r;
    const auto device = synths(r, 1)[0];
    const auto clip = heldNote(r, r.track(0));
    playUntilSounding(r, device);
    SECTION("deleted") { r.session->setNotes(clip, {}); }
    SECTION("transposed") { r.session->setNotes(clip, {{.pitch = 62, .start = 0.0, .length = 16.0}}); }
    SECTION("shortened before the playhead") { r.session->setNotes(clip, {{.pitch = 60, .start = 0.0, .length = 0.5}}); }
    r.pump(kPastRelease);
    CHECK(voices(r, device) == 0);
}

TEST_CASE("notes: what still plays keeps playing", "[integration]")
{
    Recording r;
    const auto devices = synths(r, 2);
    const auto t = r.track(0);
    const auto clip = heldNote(r, t);
    SECTION("one note of a chord deleted")
    {
        r.session->setNotes(clip, {{.pitch = 60, .start = 0.0, .length = 16.0}, {.pitch = 64, .start = 0.0, .length = 16.0}});
        r.session->setPosition(0.0);
        r.session->play();
        r.pump(0.5);
        REQUIRE(voices(r, devices[0]) == 2);
        r.session->setNotes(clip, {{.pitch = 60, .start = 0.0, .length = 16.0}});
        r.pump(kPastRelease);
        CHECK(voices(r, devices[0]) == 1);
    }
    SECTION("an edit elsewhere: another note, another track, a rename")
    {
        playUntilSounding(r, devices[0]);
        r.session->setNotes(clip, {{.pitch = 60, .start = 0.0, .length = 16.0}, {.pitch = 72, .start = 12.0, .length = 1.0}});
        heldNote(r, r.track(1));
        r.session->renameTrack(t, "Keys");
        r.pump(kPastRelease);
        CHECK(voices(r, devices[0]) == 1);
    }
    SECTION("a note made longer")
    {
        r.session->setNotes(clip, {{.pitch = 60, .start = 0.0, .length = 2.0}});
        playUntilSounding(r, devices[0]);
        r.session->setNotes(clip, {{.pitch = 60, .start = 0.0, .length = 16.0}});
        r.pump(1.0); // past the old end (1 s) and its release
        CHECK(voices(r, devices[0]) == 1);
    }
}

TEST_CASE("notes: undo, a tempo change and a loop wrap end what no longer plays", "[integration]")
{
    Recording r;
    const auto devices = synths(r, 2);
    const auto a = r.track(0), b = r.track(1);
    SECTION("undo of a move: the note started on the track it's taken back from")
    {
        const auto clip = heldNote(r, a);
        r.session->applyClipEdits({{.clip = clip, .track = b, .start = 0.0, .length = 8.0}});
        playUntilSounding(r, devices[1]);
        r.session->undo();
        r.pump(kPastRelease);
        CHECK(voices(r, devices[1]) == 0);
    }
    SECTION("a tempo change brings the note's end before the playhead")
    {
        heldNote(r, a, 2.0); // 2 beats: 1 s at 120 BPM
        playUntilSounding(r, devices[0]);
        r.session->setTempo(480.0); // 2 beats: 0.25 s
        r.pump(kPastRelease);
        CHECK(voices(r, devices[0]) == 0);
    }
    SECTION("a note crossing the loop's end deleted just before the wrap; the note at its start still plays")
    {
        const auto clip = r.session->createMidiClip(a, 0.0, 4.0);
        r.session->setNotes(clip, {{.pitch = 48, .start = 0.0, .length = 1.0}, {.pitch = 60, .start = 3.0, .length = 3.0}});
        r.session->setLoopRange(0.0, 2.0);
        r.session->setLooping(true);
        r.session->setPosition(1.2);
        r.session->play();
        r.pump(0.5); // at 1.7 s: the long note sounds
        REQUIRE(voices(r, devices[0]) == 1);
        r.session->setNotes(clip, {{.pitch = 48, .start = 0.0, .length = 1.0}});
        r.pump(0.5); // wrapped: 0.2 s into the loop, the first note sounds
        CHECK(voices(r, devices[0]) == 1);
        r.pump(1.0); // at 1.2 s: past it and its release, before the next wrap
        CHECK(voices(r, devices[0]) == 0);
    }
}

TEST_CASE("notes: switching takes or ending an audition ends the notes that went", "[integration]")
{
    Recording r;
    const auto device = synths(r, 1)[0];
    const auto t = r.track(0);
    r.session->setCountIn(fd::model::CountIn::Off); // the setting persists between runs
    r.session->setMidiInputTarget(t);
    // Two passes, the second over the whole of the first (so they're its takes): the first holds a
    // short note, the second (playing) a long one.
    auto pass = [&](int note, double from, double held, double length) {
        r.session->setPosition(from);
        r.session->record();
        r.pump(0.1); // recording has started
        r.session->computerKeyboardNote(note, 0.8f);
        r.pump(held);
        r.session->computerKeyboardNote(note, 0.0f);
        r.pump(length - held - 0.1);
        r.session->stop();
        r.pump(0.1);
        juce::MessageManager::getInstance()->runDispatchLoopUntil(50);
    };
    pass(60, 0.3, 0.1, 1.5); // a note at 0.4 to 0.5 s
    pass(64, 0.0, 1.9, 2.2); // a note at 0.1 to 2.0 s
    r.session->setMidiInputTarget(std::nullopt);
    const auto takes = allTakes(r.info(t));
    REQUIRE(takes.size() >= 2);
    const auto first = takes.front().id;
    r.session->setPosition(0.0);
    r.session->play();
    r.pump(0.6); // the second pass's long note sounds; the first pass has nothing here
    REQUIRE(voices(r, device) == 1);
    SECTION("switched to the take with nothing here") { REQUIRE(r.session->useTake(t, first)); }
    SECTION("auditioning it, then stopping")
    {
        REQUIRE(r.session->auditionTake(t, first));
        r.pump(0.1);
        r.session->stopAudition();
    }
    r.pump(kPastRelease);
    CHECK(voices(r, device) == 0);
}

TEST_CASE("notes: a module turned back on doesn't play notes that ended while it was off", "[integration]")
{
    Recording r;
    const auto device = synths(r, 1)[0];
    heldNote(r, r.track(0), 1.0); // 0.5 s
    r.session->setPosition(0.0);
    r.session->play();
    r.pump(0.3);
    REQUIRE(voices(r, device) == 1);
    r.session->setDeviceEnabled(device, false);
    r.pump(0.5); // the note's end passes while it's off
    r.session->setDeviceEnabled(device, true);
    r.pump(kPastRelease);
    CHECK(voices(r, device) == 0);
}

TEST_CASE("notes: a held key ends on the track it stops reaching", "[integration]")
{
    Recording r;
    const auto devices = synths(r, 2);
    const auto a = r.track(0), b = r.track(1);
    r.session->setMidiInputTarget(a);
    r.pump(0.1);
    r.session->computerKeyboardNote(60, 0.8f);
    r.pump(0.2);
    REQUIRE(voices(r, devices[0]) == 1);
    SECTION("the live input moved to another track") { r.session->setMidiInputTarget(b); }
    SECTION("monitoring turned off") { r.session->setMonitor(a, fd::model::Monitor::Off); }
    SECTION("another channel") { r.session->setMidiChannel(a, 2); }
    r.pump(kPastRelease);
    CHECK(voices(r, devices[0]) == 0); // the key is still down
    r.session->computerKeyboardNote(60, 0.0f);
}
