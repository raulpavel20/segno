#include "fd/device/VoiceManager.h"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

using namespace fd;

namespace
{
struct Log final : VoiceManager::Listener
{
    std::vector<std::string> events;
    void voiceStart(int v, int key, float, int, std::int32_t, bool retrigger) override
    {
        events.push_back("start " + std::to_string(v) + " " + std::to_string(key) + (retrigger ? " retrig" : ""));
    }
    void voiceLegato(int v, int key, float) override
    {
        events.push_back("legato " + std::to_string(v) + " " + std::to_string(key));
    }
    void voiceRelease(int v) override { events.push_back("release " + std::to_string(v)); }
};

Event on(int key) { return {.type = EventType::NoteOn, .key = static_cast<std::uint8_t>(key), .value = 0.8f}; }
Event off(int key) { return {.type = EventType::NoteOff, .key = static_cast<std::uint8_t>(key)}; }
Event pedal(bool down) { return {.type = EventType::Controller, .key = 64, .value = down ? 1.0f : 0.0f}; }
} // namespace

TEST_CASE("poly allocation uses free voices, then steals released, then oldest", "[fast]")
{
    VoiceManager vm;
    vm.setVoiceCount(2);
    Log log;
    vm.handle(on(60), log);
    vm.handle(on(62), log);
    vm.handle(off(60), log);   // voice 0 released (still active, fading)
    vm.handle(on(64), log);    // steals the released voice 0
    vm.handle(on(65), log);    // none released: steals the oldest playing (voice 1, key 62)
    CHECK(log.events == std::vector<std::string>{"start 0 60", "start 1 62", "release 0", "start 0 64 retrig",
                                                 "start 1 65 retrig"});
}

TEST_CASE("finished voices become free", "[fast]")
{
    VoiceManager vm;
    vm.setVoiceCount(1);
    Log log;
    vm.handle(on(60), log);
    vm.handle(off(60), log);
    vm.voiceFinished(0);
    CHECK(vm.activeVoices() == 0);
    vm.handle(on(61), log);
    CHECK(log.events.back() == "start 0 61");
}

TEST_CASE("sustain pedal holds released notes until it is lifted", "[fast]")
{
    VoiceManager vm;
    Log log;
    vm.handle(pedal(true), log);
    vm.handle(on(60), log);
    vm.handle(off(60), log);
    CHECK(log.events.back() == "start 0 60"); // no release yet
    vm.handle(pedal(false), log);
    CHECK(log.events.back() == "release 0");
}

TEST_CASE("mono: last-note priority with retrigger", "[fast]")
{
    VoiceManager vm;
    vm.setMode(VoiceManager::Mode::Mono);
    Log log;
    vm.handle(on(60), log);
    vm.handle(on(64), log);
    vm.handle(off(64), log); // back to 60
    vm.handle(off(60), log);
    CHECK(log.events == std::vector<std::string>{"start 0 60", "start 0 64 retrig", "start 0 60 retrig", "release 0"});
}

TEST_CASE("legato: overlapping notes glide without retrigger", "[fast]")
{
    VoiceManager vm;
    vm.setMode(VoiceManager::Mode::Legato);
    Log log;
    vm.handle(on(60), log);
    vm.handle(on(67), log);
    vm.handle(off(67), log);
    vm.handle(off(60), log);
    CHECK(log.events == std::vector<std::string>{"start 0 60", "legato 0 67", "legato 0 60", "release 0"});
}

TEST_CASE("all notes off releases everything, even sustained notes", "[fast]")
{
    VoiceManager vm;
    Log log;
    vm.handle(pedal(true), log);
    vm.handle(on(60), log);
    vm.handle(on(62), log);
    vm.handle(off(62), log);
    vm.handle({.type = EventType::AllNotesOff}, log);
    CHECK(log.events == std::vector<std::string>{"start 0 60", "start 1 62", "release 0", "release 1"});
}

TEST_CASE("held notes never stick when the mode or the voice count changes", "[fast]")
{
    VoiceManager vm;
    vm.setVoiceCount(8);
    Log log;
    vm.handle(on(60), log);
    vm.handle(on(64), log);
    vm.handle(on(67), log);
    REQUIRE(vm.activeVoices() == 3);

    // Poly to Mono with three keys held: only the first voice gets note-offs now, so the others go.
    vm.setMode(VoiceManager::Mode::Mono, log);
    CHECK(log.events.back() == "release 2");
    CHECK_FALSE(vm.voice(1).gate);
    CHECK_FALSE(vm.voice(2).gate);
    CHECK(vm.voice(0).gate);
    vm.voiceFinished(1);
    vm.voiceFinished(2);
    CHECK(vm.activeVoices() == 1);
    vm.handle(off(60), log); // the first voice's own note-off still ends it
    CHECK_FALSE(vm.voice(0).gate);

    // A lower count releases the voices above it, and they're freed when they finish.
    vm.setMode(VoiceManager::Mode::Poly, log);
    vm.reset();
    vm.handle(on(60), log);
    vm.handle(on(62), log);
    vm.handle(on(64), log);
    vm.setVoiceCount(1, log);
    CHECK(log.events.back() == "release 2");
    vm.voiceFinished(1);
    vm.voiceFinished(2);
    vm.setVoiceCount(8, log);
    CHECK(vm.activeVoices() == 1); // they don't come back
}
