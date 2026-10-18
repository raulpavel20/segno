// The MCP tools against a real Session (ADR 0021: every DAW feature is driven through MCP in a
// test). An in-process client calls the dispatcher as a transport would; each edit must be one
// named undo step, read-only mode must hold, and confirmations must be asked.

#include "TestLibrary.h"
#include "engine/DevicePlugin.h"
#include "engine/EngineBehaviour.h"
#include "engine/Session.h"
#include "engine/SessionImpl.h"
#include "fd/testkit/Signals.h"
#include "fd/testkit/WavIO.h"
#include "mcp/McpServer.h"
#include "mcp/protocol/Encoding.h"
#include "mcp/protocol/Schema.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <future>
#include <httplib.h>
#include <regex>
#include <tracktion_engine/testing/tracktion_EnginePlayer.h>
#include <tracktion_engine/tracktion_engine.h>

using Catch::Approx;
using fd::mcp::Json;

namespace
{
Json modernRequest(const std::string& method, Json params, Json capabilities = Json::object())
{
    static int nextId = 1;
    params["_meta"] = {{"io.modelcontextprotocol/protocolVersion", fd::mcp::modernVersion},
                       {"io.modelcontextprotocol/clientInfo", {{"name", "mcp-tool-tests"}, {"version", "1"}}},
                       {"io.modelcontextprotocol/clientCapabilities", std::move(capabilities)}};
    return {{"jsonrpc", "2.0"}, {"id", nextId++}, {"method", method}, {"params", std::move(params)}};
}

/// A DAW with its MCP server, driven in process.
struct Daw
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    /// What the engine keeps between runs (its Settings.xml) is per name: a test that changes it
    /// (the count-in, the recording format) uses a name of its own.
    tracktion::Engine engine;
    std::unique_ptr<fd::engine::Session> session;
    juce::TemporaryFile folderName;
    juce::File folder;
    std::unique_ptr<fd::mcp::McpServer> server;

    explicit Daw(const juce::String& engineName = "fd-mcp-tests")
        : engine(engineName, nullptr, fd::engine::makeEngineBehaviour())
    {
        fd::engine::registerDevices(engine);
        session = std::make_unique<fd::engine::Session>(engine);
        // The engine keeps these between runs: start from what the tests expect, whatever an
        // earlier run left.
        session->setRecordingFormat(fd::model::RecordingFormat::Wav, 24);
        session->setCountIn(fd::model::CountIn::Off);
        folder = folderName.getFile();
        REQUIRE(folder.createDirectory());
        // What an unsaved project writes (recordings, bounces, reversed copies) stays here too.
        session->setUnsavedRoot(folder.getChildFile("Unsaved"));
        server = std::make_unique<fd::mcp::McpServer>(*session, folder, "test");
        REQUIRE(server->start(0));
    }
    ~Daw()
    {
        server.reset();
        session.reset();
        folder.deleteRecursively();
    }

    /// The full result of a tool call, made on the message thread (this one).
    Json callResult(const std::string& tool, Json args = Json::object(), Json capabilities = Json::object())
    {
        const auto out = server->dispatcher()->handle(
            modernRequest("tools/call", {{"name", tool}, {"arguments", std::move(args)}}, std::move(capabilities)), {});
        REQUIRE(out.message);
        INFO(out.message->dump());
        REQUIRE(out.message->contains("result"));
        return (*out.message)["result"];
    }
    /// A tool's structured result; the call must succeed and match the tool's output schema.
    Json call(const std::string& tool, Json args = Json::object())
    {
        const auto r = callResult(tool, std::move(args));
        INFO(tool << ": " << r.dump());
        REQUIRE(r["isError"] == false);
        if (const auto* t = server->registry().findTool(tool); t != nullptr && !t->outputSchema.is_null())
            CHECK(fd::mcp::validate(t->outputSchema, r["structuredContent"]) == std::nullopt);
        return r["structuredContent"];
    }
    /// A call that must fail; returns its message.
    std::string fails(const std::string& tool, Json args = Json::object())
    {
        const auto r = callResult(tool, std::move(args));
        INFO(r.dump());
        REQUIRE(r["isError"] == true);
        return r["content"][0]["text"].get<std::string>();
    }
    /// A call made from another thread (as transports do) while this thread runs the message loop:
    /// for tools that wait on the message thread (render).
    Json callAway(const std::string& tool, Json args)
    {
        auto future = std::async(std::launch::async, [&] {
            return server->dispatcher()->handle(modernRequest("tools/call", {{"name", tool}, {"arguments", args}}), {});
        });
        while (future.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready)
            juce::MessageManager::getInstance()->runDispatchLoopUntil(10);
        const auto out = future.get();
        REQUIRE(out.message);
        return (*out.message)["result"];
    }
    Json read(const std::string& uri)
    {
        const auto out = server->dispatcher()->handle(modernRequest("resources/read", {{"uri", uri}}), {});
        REQUIRE(out.message);
        INFO(out.message->dump());
        REQUIRE(out.message->contains("result"));
        return Json::parse((*out.message)["result"]["contents"][0]["text"].get<std::string>());
    }
    Json project() { return call("get_project"); }
};
} // namespace

TEST_CASE("mcp tools: the catalogue is well formed and vendor-free", "[integration][mcp]")
{
    Daw daw;
    const auto& registry = daw.server->registry();
    std::set<std::string> names;
    const std::regex vendors("Ableton|Bitwig|Cubase|FabFilter|Serum|Logic Pro|Pro Tools|FL Studio|Reaper|Native Instruments",
                             std::regex::icase);
    for (const auto& t : registry.tools)
    {
        INFO(t.name);
        CHECK(std::regex_match(t.name, std::regex("[a-z_]+")));
        CHECK(names.insert(t.name).second);
        CHECK(t.inputSchema["type"] == "object");
        CHECK(fd::mcp::unsupportedKeywords(t.inputSchema).empty());
        CHECK(fd::mcp::unsupportedKeywords(t.outputSchema).empty());
        CHECK_FALSE(t.description.empty());
        CHECK_FALSE(std::regex_search(t.description + t.inputSchema.dump(), vendors));
    }
    // The catalogue of docs/design/mcp.md §5 (measure_module comes with the dynamics core, A1).
    for (const auto* name : {"get_project",
                             "get_clip",
                             "get_module",
                             "list_module_types",
                             "play",
                             "stop",
                             "set_position",
                             "set_loop",
                             "set_metronome",
                             "set_tempo",
                             "set_time_signature",
                             "add_track",
                             "create_midi_clip",
                             "insert_audio_file",
                             "insert_midi_file",
                             "move_clips",
                             "trim_clip",
                             "duplicate_clips",
                             "split_clips",
                             "delete_clips",
                             "add_notes",
                             "set_notes",
                             "remove_notes",
                             "edit_notes",
                             "add_module",
                             "remove_module",
                             "move_module",
                             "set_module_on",
                             "set_parameters",
                             "load_sample",
                             "get_zones",
                             "load_granular_source",
                             "get_granular_source",
                             "list_wavetables",
                             "set_wavetable",
                             "make_wavetable",
                             "get_wavetable",
                             "edit_wavetable",
                             "export_wavetable",
                             "set_module_view",
                             "get_grid",
                             "set_grid",
                             "set_module_sidechain",
                             "scan_plugins",
                             "render",
                             "measure_module",
                             "set_mix",
                             "add_return_track",
                             "set_send",
                             "measure_mix",
                             "get_automation",
                             "set_automation",
                             "clear_automation",
                             "set_automation_on",
                             "add_automation_lane",
                             "set_automation_lane",
                             "remove_automation_lane",
                             "move_automation_lane",
                             "assign_automation",
                             "unassign_automation",
                             "set_track_input",
                             "get_settings",
                             "set_settings",
                             "search",
                             "browse",
                             "preview",
                             "collect_files",
                             "arm_tracks",
                             "set_reduced_latency",
                             "measure_latency",
                             "get_midi_mappings",
                             "map_midi",
                             "remove_midi_mapping",
                             "record",
                             "get_takes",
                             "use_take",
                             "audition_take",
                             "set_take",
                             "move_takes_to_tracks",
                             "delete_takes",
                             "capture_midi",
                             "bounce_clips",
                             "group_tracks",
                             "ungroup_tracks",
                             "bounce_group",
                             "mute_clips",
                             "set_fades",
                             "crossfade",
                             "set_edge_fades",
                             "export_audio",
                             "list_presets",
                             "load_preset",
                             "save_preset",
                             "compare",
                             "undo",
                             "redo",
                             "save_project",
                             "new_project",
                             "open_project",
                             "set_key",
                             "list_scales",
                             "detect_key",
                             "get_chords",
                             "set_chord",
                             "make_chords",
                             "suggest_chords",
                             "reroll_chance",
                             "get_controllers",
                             "set_controllers",
                             "list_note_tools",
                             "transform_notes",
                             "generate_notes",
                             "get_modulators",
                             "route_modulator",
                             "set_route",
                             "remove_route",
                             "keep_roll",
                             "set_lfo_curve",

                             "set_audio",
                             "get_gain_line",
                             "set_gain_line",
                             "edit_audio_range",
                             "list_audio_tools",
                             "transform_audio",
                             "set_warp_markers",
                             "get_transients",
                             "edit_transients",
                             "detect_tempo"})
        CHECK(names.contains(name));
    for (const auto& p : registry.prompts)
    {
        Json args{{"module", "m:1"}};
        CHECK_FALSE(std::regex_search(p.get(args).dump(), vendors));
    }
    CHECK_FALSE(std::regex_search(daw.read("segno://modules").dump(), vendors));
}

TEST_CASE("mcp tools: tracks, MIDI clips and notes, each call one named undo step", "[integration][mcp]")
{
    Daw daw;
    auto project = daw.project();
    REQUIRE(project["tracks"].size() == 4);
    CHECK(project["tempo"] == Approx(120.0));
    CHECK(project["file"].is_null());

    const auto track = daw.call("add_track", {{"name", "Bass"}})["track"].get<std::string>();
    CHECK(daw.session->undoDescription() == "Assistant: add track");
    CHECK(daw.project()["tracks"][4]["name"] == "Bass");

    const auto created = daw.call("create_midi_clip", {{"track", track},
                                                       {"start", "2.1.1"},
                                                       {"length", "1 bar"},
                                                       {"notes",
                                                        {{{"pitch", "C2"}, {"start", 0}, {"length", 1}, {"velocity", 90}},
                                                         {{"pitch", 51}, {"start", "1 beat"}, {"length", "1/8"}}}}});
    CHECK_FALSE(created.contains("addedInstrument")); // no instrument by itself (ADR 0033)
    CHECK(daw.project()["tracks"][4]["instrument"].is_null());
    const auto clipId = created["clip"]["id"].get<std::string>();
    CHECK(created["clip"]["start"]["bbt"] == "2.1.1");
    CHECK(created["clip"]["length"] == Approx(4.0));
    CHECK(daw.session->undoDescription() == "Assistant: create midi clip");

    auto clip = daw.call("get_clip", {{"clip", clipId}});
    REQUIRE(clip["notes"].size() == 2);
    CHECK(clip["notes"][0]["name"] == "C2");
    CHECK(clip["notes"][0]["pitch"] == 48);
    CHECK(clip["notes"][0]["velocity"] == 90);
    CHECK(clip["notes"][1]["start"] == Approx(1.0));
    CHECK(clip["notes"][1]["length"] == Approx(0.5));
    CHECK(clip["notes"][1]["velocity"] == 100);

    // One undo takes back the clip and its notes.
    const auto undone = daw.call("undo");
    CHECK(undone["undone"][0] == "Assistant: create midi clip");
    project = daw.project();
    CHECK(project["tracks"][4]["clips"].empty());
    CHECK(project["tracks"][4]["instrument"].is_null());
    CHECK(daw.call("redo")["redone"][0] == "Assistant: create midi clip");
    CHECK(daw.project()["tracks"][4]["clips"].size() == 1);

    // add_notes: the new note wins over the old one of the same pitch it overlaps.
    auto written = daw.call("add_notes", {{"clip", clipId}, {"notes", {{{"pitch", 48}, {"start", 0.5}, {"length", 1}}}}});
    CHECK(written["notes"] == 1);
    clip = daw.call("get_clip", {{"clip", clipId}});
    REQUIRE(clip["notes"].size() == 3);
    CHECK(clip["notes"][0]["length"] == Approx(0.5)); // shortened to where the new one starts

    // Notes past the end lengthen the clip to the end of that bar (B15).
    written = daw.call("add_notes", {{"clip", clipId}, {"notes", {{{"pitch", 60}, {"start", "5 beats"}, {"length", 1}}}}});
    CHECK(written["lengthened"] == true);
    CHECK(written["clip"]["end"]["bbt"] == "4.1.1");

    // edit_notes: velocity, transpose and quantise on a filter.
    daw.call("edit_notes", {{"clip", clipId}, {"where", {{"pitch", "C3"}}}, {"velocity", "+20"}, {"transpose", 12}});
    clip = daw.call("get_clip", {{"clip", clipId}});
    CHECK(clip["notes"].back()["pitch"] == 72);
    CHECK(clip["notes"].back()["velocity"] == 120);
    daw.call("edit_notes", {{"clip", clipId}, {"where", "all"}, {"shift", "1/16"}, {"quantise", "1/4"}});
    clip = daw.call("get_clip", {{"clip", clipId}});
    for (const auto& n : clip["notes"])
        CHECK(std::fmod(n["start"].get<double>(), 1.0) == Approx(0.0).margin(1e-6));
    CHECK(daw.call("edit_notes", {{"clip", clipId}, {"where", {{"pitch", 1}}}, {"transpose", 1}})["notes"] == 0);

    daw.call("remove_notes", {{"clip", clipId}, {"where", {{"lowest", "C2"}, {"highest", "B2"}}}});
    clip = daw.call("get_clip", {{"clip", clipId}});
    for (const auto& n : clip["notes"])
        CHECK((n["pitch"] < 48 || n["pitch"] > 59));
    daw.call("set_notes", {{"clip", clipId}, {"notes", Json::array()}});
    CHECK(daw.call("get_clip", {{"clip", clipId}})["noteCount"] == 0);

    // Problems come back as sentences the model can act on.
    CHECK(daw.fails("get_clip", {{"clip", "c:999999"}}).find("doesn't exist") != std::string::npos);
    CHECK(daw.fails("add_track", {{"name", 3}}).find("name: expected a string") != std::string::npos);
    CHECK(daw.fails("add_notes", {{"clip", clipId}, {"notes", {{{"pitch", "H9"}, {"start", 0}, {"length", 1}}}}})
              .find("notes[0].pitch") != std::string::npos);
    CHECK(daw.fails("create_midi_clip", {{"track", "t:424242"}, {"start", 0}}).find("t:424242") != std::string::npos);
}

TEST_CASE("mcp tools: chords read, written, made and suggested; the piano roll's edits (P4a)",
          "[integration][mcp]")
{
    Daw daw;
    const auto track = daw.call("add_track", {{"name", "Keys"}})["track"].get<std::string>();
    const auto clipId = daw.call("create_midi_clip",
                                 {{"track", track}, {"start", "1.1.1"}, {"length", "4 bars"}})["clip"]["id"]
                            .get<std::string>();
    REQUIRE(daw.call("set_key", {{"root", "A"}, {"scale", "minor"}})["key"]["name"] == "A minor");

    // make_chords: a one-finger line becomes the key's chords, voice-led.
    daw.call("set_notes", {{"clip", clipId},
                           {"notes",
                            {{{"pitch", "A2"}, {"start", 0}, {"length", "1 bar"}},
                             {{"pitch", "F2"}, {"start", 4}, {"length", "1 bar"}},
                             {{"pitch", "C3"}, {"start", 8}, {"length", "1 bar"}},
                             {{"pitch", "G2"}, {"start", 12}, {"length", "1 bar"}}}}});
    auto made = daw.call("make_chords", {{"clip", clipId}, {"where", "all"}});
    CHECK(made["notes"] == 12);
    CHECK(daw.session->undoDescription() == "Assistant: make chords");

    // make_chords note by note: a seventh, a chord with the note on top, one left alone.
    daw.call("undo");
    made = daw.call("make_chords",
                    {{"clip", clipId},
                     {"where", "all"},
                     {"chords", {{{"kind", "seventh"}}, {{"chord", "Dm"}, {"voicing", "close"}}, "leave"}}});
    CHECK(made["notes"] == 4 + 3 + 1 + 3);
    auto noteByNote = daw.call("get_chords", {{"clip", clipId}});
    REQUIRE(noteByNote["chords"].size() == 3);
    CHECK(noteByNote["chords"][0]["name"] == "Am7");
    CHECK(noteByNote["chords"][1]["name"].get<std::string>().rfind("Dm", 0) == 0); // F under it... on top
    CHECK(daw.fails("make_chords", {{"clip", clipId}, {"where", "all"}, {"chords", {{{"chord", "E"}}}}})
              .find("doesn't have") != std::string::npos);
    daw.call("undo");
    made = daw.call("make_chords", {{"clip", clipId}, {"where", "all"}});

    // get_chords: as the chord lane names them, with their steps.
    auto chords = daw.call("get_chords", {{"clip", clipId}});
    CHECK(chords["key"]["name"] == "A minor");
    REQUIRE(chords["chords"].size() == 4);
    const auto rootOf = [](const Json& chord)
    {
        auto name = chord["name"].get<std::string>();
        return name.substr(0, name.find_first_not_of("ABCDEFG#b", 1));
    };
    CHECK(rootOf(chords["chords"][0]) == "A");
    CHECK(rootOf(chords["chords"][1]) == "F");
    CHECK(rootOf(chords["chords"][3]) == "G");
    CHECK(chords["chords"][1]["step"] == "VI");
    CHECK(chords["chords"][0]["start"] == Approx(0.0));
    CHECK(chords["chords"][0]["length"] == Approx(4.0));
    CHECK(chords["chords"][0]["pitches"].size() == 3);

    // set_chord: a name or a numeral, replacing the chord there, voice-led.
    auto set = daw.call("set_chord", {{"clip", clipId}, {"chord", "E7"}, {"start", 12}, {"length", "1 bar"}});
    CHECK(set["chord"] == "E7");
    CHECK(set["notes"] == 4);
    chords = daw.call("get_chords", {{"clip", clipId}});
    CHECK(chords["chords"][3]["name"].get<std::string>().rfind("E7", 0) == 0);
    CHECK(chords["chords"][3]["step"] == "V7");
    daw.call("set_chord",
             {{"clip", clipId}, {"chord", "iv"}, {"start", 4}, {"voicing", "close"}, {"inversion", "root"}});
    chords = daw.call("get_chords", {{"clip", clipId}});
    CHECK(chords["chords"][1]["name"] == "Dm");
    CHECK(daw.fails("set_chord", {{"clip", clipId}, {"chord", "H7"}, {"start", 0}}).find("chord name") !=
          std::string::npos);
    CHECK(daw.fails("set_chord", {{"clip", clipId}, {"chord", "Dm"}, {"start", 0}, {"voicing", "wide"}})
              .find("drop2") != std::string::npos);

    // suggest_chords: what the Chord popover offers.
    const auto offered = daw.call("suggest_chords", {{"clip", clipId}, {"at", 8}});
    CHECK(offered["inKey"].size() == 7);
    CHECK(offered["inKey"][0]["name"] == "Am");
    CHECK(offered["before"]["name"] == "Dm");
    REQUIRE_FALSE(offered["next"].empty());
    CHECK(offered["next"][0].contains("why"));
    CHECK(offered["borrowed"].size() == 3);
    CHECK(daw.call("suggest_chords",
                   {{"clip", clipId}, {"at", 0}, {"kind", "seventh"}})["inKey"][0]["name"] == "Am7");

    // edit_notes: steps of the key, fit to key, mute, legato, stretch, reverse, invert, voicing, add bass.
    daw.call("set_notes", {{"clip", clipId},
                           {"notes",
                            {{{"pitch", "C3"}, {"start", 0}, {"length", 1}},
                             {{"pitch", "F#3"}, {"start", 2}, {"length", 1}}}}});
    daw.call("edit_notes", {{"clip", clipId}, {"where", {{"pitch", "C3"}}}, {"transpose_steps", 1}});
    auto clip = daw.call("get_clip", {{"clip", clipId}});
    CHECK(clip["notes"][0]["pitch"] == 62); // D: the next note of A minor
    daw.call("edit_notes", {{"clip", clipId}, {"where", "all"}, {"fit_to_key", true}});
    clip = daw.call("get_clip", {{"clip", clipId}});
    CHECK(clip["notes"][1]["pitch"] == 67); // F♯ after a rising step: up to G
    daw.call("edit_notes", {{"clip", clipId}, {"where", {{"pitch", "G3"}}}, {"mute", true}});
    clip = daw.call("get_clip", {{"clip", clipId}});
    CHECK(clip["notes"][1]["muted"] == true);
    CHECK_FALSE(clip["notes"][0].contains("muted"));
    daw.call("edit_notes", {{"clip", clipId}, {"where", "all"}, {"legato", true}});
    CHECK(daw.call("get_clip", {{"clip", clipId}})["notes"][0]["length"] == Approx(2.0));
    daw.call("edit_notes", {{"clip", clipId}, {"where", "all"}, {"stretch", 2}});
    CHECK(daw.call("get_clip", {{"clip", clipId}})["notes"][1]["start"] == Approx(4.0));
    daw.call("edit_notes", {{"clip", clipId}, {"where", "all"}, {"reverse", true}});
    CHECK(daw.call("get_clip", {{"clip", clipId}})["notes"][0]["pitch"] == 67);
    CHECK(daw.fails("edit_notes", {{"clip", clipId}, {"where", "all"}, {"stretch", 0}}).find("half speed") !=
          std::string::npos);

    daw.call("set_notes", {{"clip", clipId},
                           {"notes",
                            {{{"pitch", 60}, {"start", 0}, {"length", 4}},
                             {{"pitch", 64}, {"start", 0}, {"length", 4}},
                             {{"pitch", 67}, {"start", 0}, {"length", 4}}}}});
    daw.call("edit_notes", {{"clip", clipId}, {"where", "all"}, {"invert", "up"}});
    clip = daw.call("get_clip", {{"clip", clipId}});
    CHECK(clip["notes"][2]["pitch"] == 72);
    daw.call("edit_notes", {{"clip", clipId}, {"where", "all"}, {"voicing", "close"}, {"add_bass", true}});
    clip = daw.call("get_clip", {{"clip", clipId}});
    REQUIRE(clip["notes"].size() == 4);
    CHECK(clip["notes"][0]["pitch"].get<int>() % 12 == 0); // the bass: C, under the chord
    CHECK(daw.session->undoDescription() == "Assistant: edit notes");
    daw.call("undo");
    CHECK(daw.call("get_clip", {{"clip", clipId}})["notes"].size() == 3);

    // Without a key, the key's tools say what to do.
    daw.call("set_key", {{"none", true}});
    CHECK(daw.fails("edit_notes", {{"clip", clipId}, {"where", "all"}, {"transpose_steps", 1}})
              .find("set_key") != std::string::npos);
    CHECK(daw.call("suggest_chords", {{"clip", clipId}, {"at", 0}})["borrowed"].empty());
}

TEST_CASE("mcp tools: chance, velocity spread, release velocity, re-roll and controllers (P4b)",
          "[integration][mcp]")
{
    Daw daw;
    const auto track = daw.call("add_track", {{"name", "Drums"}})["track"].get<std::string>();
    const auto clipId = daw.call("create_midi_clip",
                                 {{"track", track}, {"start", "1.1.1"}, {"length", "1 bar"}})["clip"]["id"]
                            .get<std::string>();

    // Notes carry them in and out; the plain ones say nothing about them.
    daw.call("set_notes", {{"clip", clipId},
                           {"notes",
                            {{{"pitch", 42}, {"start", 0}, {"length", "1/16"}, {"chance", 50}},
                             {{"pitch", 42}, {"start", 1}, {"length", "1/16"}, {"velocity_spread", 20}},
                             {{"pitch", 36}, {"start", 2}, {"length", "1/16"}, {"release_velocity", 64}}}}});
    auto notes = daw.call("get_clip", {{"clip", clipId}})["notes"];
    REQUIRE(notes.size() == 3);
    CHECK(notes[0]["chance"] == Approx(50.0));
    CHECK_FALSE(notes[0].contains("velocity_spread"));
    CHECK(notes[1]["velocity_spread"] == 20);
    CHECK_FALSE(notes[1].contains("chance"));
    CHECK(notes[2]["release_velocity"] == 64);
    CHECK(daw.fails("add_notes",
                    {{"clip", clipId},
                     {"notes", {{{"pitch", 40}, {"start", 0}, {"length", "1/16"}, {"chance", 150}}}}})
              .find("0-100") != std::string::npos);

    // edit_notes sets them on the notes that match.
    daw.call("edit_notes",
             {{"clip", clipId}, {"where", {{"pitch", 42}}}, {"chance", 25}, {"velocity_spread", 10}});
    CHECK(daw.session->undoDescription() == "Assistant: edit notes");
    notes = daw.call("get_clip", {{"clip", clipId}})["notes"];
    CHECK(notes[0]["chance"] == Approx(25.0));
    CHECK(notes[1]["chance"] == Approx(25.0));
    CHECK(notes[1]["velocity_spread"] == 10);
    CHECK_FALSE(notes[2].contains("chance"));
    daw.call("edit_notes", {{"clip", clipId}, {"where", "all"}, {"chance", 100}, {"velocity_spread", 0}});
    notes = daw.call("get_clip", {{"clip", clipId}})["notes"];
    CHECK_FALSE(notes[0].contains("chance"));
    CHECK_FALSE(notes[1].contains("velocity_spread"));

    // reroll_chance: a given seed, or a new one; one undo step.
    auto rolled = daw.call("reroll_chance", {{"clip", clipId}, {"seed", 77}});
    CHECK(rolled["seed"] == 77);
    CHECK(rolled["clip"]["chanceSeed"] == 77);
    CHECK(daw.session->undoDescription() == "Assistant: reroll chance");
    rolled = daw.call("reroll_chance", {{"clip", clipId}});
    CHECK(rolled["seed"] != 77);

    // Controllers: lanes written as points, read back; pitch bend around 0; pedals on or off.
    auto lanes = daw.call(
        "set_controllers",
        {{"clip", clipId},
         {"lanes",
          {{{"controller", "mod_wheel"},
            {"points", {{{"beat", 0}, {"value", 0}}, {{"beat", 2}, {"value", 127}}}}},
           {{"controller", "pitch_bend"},
            {"points", {{{"beat", 1}, {"value", 0}}, {{"beat", 1.5}, {"value", 8191}, {"step", true}}}}},
           {{"controller", 64},
            {"points", {{{"beat", 0}, {"value", 100}}, {{"beat", 3}, {"value", 0}}}}}}}})["lanes"];
    CHECK(daw.session->undoDescription() == "Assistant: set controllers");
    REQUIRE(lanes.size() == 3);
    CHECK(lanes[0]["controller"] == 1);
    CHECK(lanes[0]["name"] == "Mod wheel");
    CHECK(lanes[0]["points"].size() == 2);
    CHECK(lanes[0]["points"][1]["value"] == 127);
    CHECK(lanes[1]["controller"] == 64);
    CHECK(lanes[1]["points"][0]["value"] == 127); // a pedal: on
    CHECK(lanes[1]["points"][0]["step"] == true);
    CHECK(lanes[2]["controller"] == "pitch_bend");
    CHECK(lanes[2]["points"][0]["value"] == 0);
    CHECK(lanes[2]["points"][1]["value"] == 8191);
    const auto clipNow = daw.call("get_clip", {{"clip", clipId}});
    REQUIRE(clipNow["controllers"].size() == 3);
    CHECK(clipNow["controllers"][0]["points"] == 2);
    const auto one = daw.call("get_controllers", {{"clip", clipId}, {"controller", "sustain"}})["lanes"];
    REQUIRE(one.size() == 1);
    CHECK(one[0]["controller"] == 64);
    // An empty lane removes it; a bad controller is named.
    daw.call("set_controllers",
             {{"clip", clipId}, {"lanes", {{{"controller", 1}, {"points", Json::array()}}}}});
    CHECK(daw.call("get_controllers", {{"clip", clipId}})["lanes"].size() == 2);
    CHECK(daw.fails("set_controllers",
                    {{"clip", clipId}, {"lanes", {{{"controller", "wobble"}, {"points", Json::array()}}}}})
              .find("wobble") != std::string::npos);
    CHECK(
        daw.fails("set_controllers",
                  {{"clip", clipId},
                   {"lanes", {{{"controller", "pitch_bend"}, {"points", {{{"beat", 0}, {"value", 9000}}}}}}}})
            .find("-8192") != std::string::npos);
}

TEST_CASE("mcp tools: the notes' transform and generate tools, listed, run and checked (P4c)",
          "[integration][mcp]")
{
    Daw daw;
    const auto track = daw.call("add_track", {{"name", "Keys"}})["track"].get<std::string>();
    const auto clipId = daw.call("create_midi_clip",
                                 {{"track", track}, {"start", "1.1.1"}, {"length", "4 bars"}})["clip"]["id"]
                            .get<std::string>();
    REQUIRE(daw.call("set_key", {{"root", "C"}, {"scale", "major"}})["key"]["name"] == "C major");

    // Listed with their settings and defaults.
    const auto listed = daw.call("list_note_tools", Json::object());
    REQUIRE(listed["transform"].size() == 9);
    REQUIRE(listed["generate"].size() == 4);
    CHECK(listed["transform"][0]["tool"] == "quantise");
    const auto strum = listed["transform"][2];
    CHECK(strum["tool"] == "strum");
    CHECK(strum["settings"][0]["choices"] == Json{"up", "down", "alternate"});
    CHECK(strum["settings"][1]["default"] == 40.0);
    const auto velocity = listed["transform"][6];
    CHECK(velocity["settings"][1]["onlyWhen"]["shape"] == "ramp");

    // Generate: a progression, a bar each; then a bassline under it.
    auto made = daw.call("generate_notes",
                         {{"clip", clipId}, {"tool", "chords"}, {"settings", {{"kind", "seventh"}}}});
    CHECK(made["notes"] == 16); // four sevenths
    CHECK(daw.session->undoDescription() == "Assistant: generate notes");
    auto chords = daw.call("get_chords", {{"clip", clipId}})["chords"];
    REQUIRE(chords.size() == 4);
    CHECK(chords[0]["name"] == "Cmaj7");
    made =
        daw.call("generate_notes", {{"clip", clipId}, {"tool", "bassline"}, {"settings", {{"step", "1/8"}}}});
    CHECK(made["notes"] == 32);
    // Only into the span asked for.
    made = daw.call("generate_notes", {{"clip", clipId},
                                       {"tool", "rhythm"},
                                       {"start", 0},
                                       {"length", "1 bar"},
                                       {"settings", {{"pitch", 84}}}});
    CHECK(made["notes"] == 5);

    // Transform: the chord in the first bar strummed.
    auto t =
        daw.call("transform_notes", {{"clip", clipId},
                                     {"where", {{"from", 0}, {"to", 1}, {"lowest", 55}, {"highest", 80}}},
                                     {"tool", "strum"},
                                     {"settings", {{"time", 60}}}});
    CHECK(t["notes"] == 4);
    CHECK(daw.session->undoDescription() == "Assistant: transform notes");
    // Quantise, to the grid given: the strum undone.
    t = daw.call("transform_notes", {{"clip", clipId},
                                     {"where", {{"from", 0}, {"to", 1}, {"lowest", 55}, {"highest", 80}}},
                                     {"tool", "quantise"},
                                     {"grid", "1 bar"}});
    CHECK(t["notes"] == 4);
    for (const auto& n : daw.call("get_clip", {{"clip", clipId}})["notes"])
        if (n["start"].get<double>() < 1.0 && n["pitch"].get<int>() >= 55 && n["pitch"].get<int>() <= 80)
            CHECK(n["start"].get<double>() == Approx(0.0));

    // Mistakes are named.
    CHECK(daw.fails("transform_notes", {{"clip", clipId}, {"where", "all"}, {"tool", "rhythm"}})
              .find("humanize") != std::string::npos);
    CHECK(daw.fails("transform_notes",
                    {{"clip", clipId}, {"where", "all"}, {"tool", "strum"}, {"settings", {{"tempo", 3}}}})
              .find("direction") != std::string::npos);
    CHECK(daw.fails("transform_notes", {{"clip", clipId},
                                        {"where", "all"},
                                        {"tool", "strum"},
                                        {"settings", {{"direction", "sideways"}}}})
              .find("alternate") != std::string::npos);
    CHECK(daw.fails("transform_notes",
                    {{"clip", clipId}, {"where", "all"}, {"tool", "strum"}, {"settings", {{"time", 9000}}}})
              .find("250") != std::string::npos);
    const auto empty = daw.call("create_midi_clip",
                                {{"track", track}, {"start", "9.1.1"}, {"length", "1 bar"}})["clip"]["id"];
    CHECK(daw.fails("generate_notes", {{"clip", empty}, {"tool", "bassline"}}).find("chords") !=
          std::string::npos);
}

TEST_CASE("mcp tools: audio clips: insert, move, trim, duplicate, split, delete", "[integration][mcp]")
{
    Daw daw;
    juce::TemporaryFile wav(".wav");
    REQUIRE(fd::testkit::writeWav(wav.getFile().getFullPathName().toStdString(),
                                  fd::testkit::sine(220.0, 48000.0, 96000, 0.5f, 2), 48000.0)); // 2 s = 4 beats
    const auto tracks = daw.project()["tracks"];
    const auto t0 = tracks[0]["id"].get<std::string>(), t1 = tracks[1]["id"].get<std::string>();

    auto inserted = daw.call("insert_audio_file", {{"track", t0}, {"start", 0}, {"path", wav.getFile().getFullPathName().toStdString()}});
    const auto clipId = inserted["clip"]["id"].get<std::string>();
    CHECK(inserted["clip"]["kind"] == "audio");
    CHECK(inserted["clip"]["length"] == Approx(4.0));
    CHECK(daw.fails("insert_audio_file", {{"track", t0}, {"start", 0}, {"path", "/no/such.wav"}}).find("no file") !=
          std::string::npos);
    CHECK(daw.fails("insert_audio_file", {{"track", t0}, {"start", 0}, {"path", "relative.wav"}}).find("absolute") !=
          std::string::npos);

    auto moved = daw.call("move_clips", {{"clips", {clipId}}, {"by", "1 bar"}, {"to_track", t1}});
    CHECK(moved["clips"][0]["start"]["bbt"] == "2.1.1");
    CHECK(moved["clips"][0]["track"] == t1);
    CHECK(daw.session->undoDescription() == "Assistant: move clips");

    auto trimmed = daw.call("trim_clip", {{"clip", clipId}, {"start", "2.2.1"}, {"end", "3.4.1"}});
    CHECK(trimmed["clip"]["start"]["beats"] == Approx(5.0));
    CHECK(trimmed["clip"]["end"]["beats"] == Approx(8.0)); // the file ends there
    CHECK(trimmed["clip"]["sourceOffsetSeconds"] == Approx(0.5));

    const auto copies = daw.call("duplicate_clips", {{"clips", {clipId}}, {"times", 2}})["clips"];
    REQUIRE(copies.size() == 2);
    CHECK(copies[0]["start"]["beats"] == Approx(8.0));
    CHECK(copies[1]["start"]["beats"] == Approx(11.0));

    const auto split = daw.call("split_clips", {{"clips", {copies[0]["id"]}}, {"at", 9.5}})["clips"];
    REQUIRE(split.size() == 1);
    CHECK(split[0]["start"]["beats"] == Approx(9.5));

    CHECK(daw.call("delete_clips", {{"clips", {clipId, split[0]["id"]}}})["deleted"] == 2);
    CHECK(daw.session->undoDescription() == "Assistant: delete clips");
    CHECK(daw.project()["tracks"][1]["clips"].size() == 2);
}

TEST_CASE("mcp tools: clip loops: loop what's shown, set the loop, extend past the file, notes from the loop's start",
          "[integration][mcp]")
{
    Daw daw;
    juce::TemporaryFile wav(".wav");
    REQUIRE(fd::testkit::writeWav(wav.getFile().getFullPathName().toStdString(),
                                  fd::testkit::sine(220.0, 48000.0, 96000, 0.5f, 2), 48000.0)); // 2 s = 4 beats
    const auto t0 = daw.project()["tracks"][0]["id"].get<std::string>();
    const auto audio = daw.call("insert_audio_file", {{"track", t0}, {"start", 0}, {"path", wav.getFile().getFullPathName().toStdString()}})["clip"];
    const auto audioId = audio["id"].get<std::string>();
    CHECK(audio["loop"].is_null());
    // Not looping: it stops at its file's end.
    CHECK(daw.call("trim_clip", {{"clip", audioId}, {"end", "5.1.1"}})["clip"]["end"]["beats"] == Approx(4.0));

    // Loop what it shows: nothing moves; then out past the file's end.
    auto looped = daw.call("loop_clips", {{"clips", {audioId}}})["clips"][0];
    CHECK(daw.session->undoDescription() == "Assistant: loop clips");
    CHECK(looped["loop"]["start"] == Approx(0.0));
    CHECK(looped["loop"]["length"] == Approx(4.0));
    CHECK(looped["loop"]["lengthSeconds"] == Approx(2.0));
    CHECK(looped["length"] == Approx(4.0));
    auto trimmed = daw.call("trim_clip", {{"clip", audioId}, {"end", "5.1.1"}})["clip"];
    CHECK(trimmed["end"]["beats"] == Approx(16.0)); // four laps
    // A new loop: one beat from beat 2 of the file, or as seconds.
    looped = daw.call("loop_clips", {{"clips", {audioId}}, {"start", 2}, {"length", "1 beat"}})["clips"][0];
    CHECK(looped["loop"]["start"] == Approx(2.0));
    CHECK(looped["loop"]["length"] == Approx(1.0));
    CHECK(looped["length"] == Approx(16.0));
    looped = daw.call("loop_clips", {{"clips", {audioId}}, {"length", "0.25 s"}})["clips"][0];
    CHECK(looped["loop"]["lengthSeconds"] == Approx(0.25));
    // Off: the repeats go.
    const auto off = daw.call("loop_clips", {{"clips", {audioId}}, {"loop", false}})["clips"][0];
    CHECK(off["loop"].is_null());
    CHECK(off["length"] == Approx(0.5));

    // MIDI: notes count from the loop's start; notes written past its end lengthen the loop.
    const auto midi = daw.call("create_midi_clip", {{"track", t0}, {"start", "9.1.1"}, {"length", "2 bars"},
                                                    {"notes", {{{"pitch", 60}, {"start", 4}, {"length", 1}}}}})["clip"];
    const auto midiId = midi["id"].get<std::string>();
    looped = daw.call("loop_clips", {{"clips", {midiId}}, {"start", "2.1.1"}, {"length", "1 bar"}})["clips"][0];
    CHECK(looped["loop"]["start"] == Approx(4.0));
    CHECK(looped["loop"]["length"] == Approx(4.0));
    const auto got = daw.call("get_clip", {{"clip", midiId}});
    REQUIRE(got["notes"].size() == 1);
    CHECK(got["notes"][0]["start"] == Approx(0.0)); // the loop starts on it
    const auto written = daw.call("add_notes", {{"clip", midiId}, {"notes", {{{"pitch", 64}, {"start", 5}, {"length", 1}}}}});
    CHECK(written["loopLengthened"] == true);
    CHECK(written["clip"]["loop"]["length"] == Approx(8.0)); // to the end of the next bar
    CHECK(written["clip"]["length"] == Approx(8.0));          // the clip as long as it was
    CHECK(written["lengthened"] == false);
}

TEST_CASE("mcp tools: get_module says how late a module plays its track", "[integration][mcp]")
{
    Daw daw;
    const auto t0 = daw.project()["tracks"][0]["id"].get<std::string>();
    const auto eq = daw.call("add_module", {{"track", t0}, {"type", "eq"}});
    CHECK(eq["latencyMs"] == 0.0);
    const auto id = eq["id"].get<std::string>();
    daw.call("set_parameters", {{"module", id}, {"values", {{"phase", "Linear phase"}}}});
    const double ms = daw.call("get_module", {{"module", id}})["latencyMs"];
    CHECK(ms > 100.0); // Medium: about 107 ms at 48 kHz, 116 at 44.1
    CHECK(ms < 120.0);
    daw.call("set_parameters", {{"module", id}, {"values", {{"resolution", "High"}}}});
    CHECK(daw.call("get_module", {{"module", id}})["latencyMs"].get<double>() > 2.0 * ms - 1.0);
}

TEST_CASE("mcp tools: reduced latency when monitoring bypasses a monitored track's delaying module",
          "[integration][mcp]")
{
    Daw daw;
    tracktion::HostedAudioDeviceInterface::Parameters io;
    io.sampleRate = 48000.0;
    io.blockSize = 480;
    io.inputChannels = 2;
    io.outputChannels = 2;
    tracktion::test_utilities::EnginePlayer player(daw.engine, io);
    auto pump = [&](double seconds)
    {
        juce::AudioBuffer<float> in(2, 480);
        for (int b = 0; b < static_cast<int>(seconds * 100.0); ++b)
        {
            in.clear();
            player.process(in);
            juce::MessageManager::getInstance()->runDispatchLoopUntil(1);
        }
    };
    pump(0.1);
    auto project = daw.project();
    CHECK(project["reducedLatency"] == false);
    const auto t0 = project["tracks"][0]["id"].get<std::string>();
    const auto input = project["inputs"]["audio"][0].get<std::string>();
    daw.call("set_track_input", {{"track", t0}, {"audioInput", input}, {"monitor", "in"}});
    const auto eq = daw.call("add_module", {{"track", t0}, {"type", "eq"}})["id"].get<std::string>();
    daw.call("set_parameters", {{"module", eq}, {"values", {{"phase", "Linear phase"}}}});
    pump(0.3);
    CHECK(daw.call("get_module", {{"module", eq}})["bypassedWhileMonitoring"] == false);

    const auto before = daw.session->undoDescription();
    CHECK(daw.call("set_reduced_latency", {{"on", true}})["on"] == true);
    CHECK(daw.session->undoDescription() == before); // a setting, not an edit
    pump(0.3);
    CHECK(daw.project()["reducedLatency"] == true);
    const auto module = daw.call("get_module", {{"module", eq}});
    CHECK(module["bypassedWhileMonitoring"] == true);
    CHECK(module["on"] == true); // its power as set

    daw.call("set_track_input", {{"track", t0}, {"monitor", "off"}});
    pump(0.3);
    CHECK(daw.call("get_module", {{"module", eq}})["bypassedWhileMonitoring"] == false);
    CHECK(daw.call("set_reduced_latency", {{"on", false}})["on"] == false);
}

TEST_CASE("mcp tools: MIDI learn: map by message, list, a controller moves it, remove", "[integration][mcp]")
{
    Daw daw;
    auto& s = *daw.session;
    s.setMidiTakeover(fd::model::MidiTakeover::Jump);
    const auto t0 = daw.project()["tracks"][0]["id"].get<std::string>();
    const auto filter = daw.call("add_module", {{"track", t0}, {"type", "filter"}})["id"].get<std::string>();
    const Json knob{{"type", "cc"}, {"number", 74}, {"device", "Launchkey"}};
    auto r = daw.call(
        "map_midi",
        {{"module", filter}, {"parameter", "freq"}, {"message", knob}, {"min", "200 Hz"}, {"max", "2 kHz"}});
    CHECK(r["mode"] == "absolute");
    CHECK(r["keptWith"] == "project");
    CHECK(r["message"]["label"] == "CC 74");
    CHECK(r["min"].get<std::string>().find("200") != std::string::npos);
    CHECK(s.undoDescription() == "Assistant: map midi");

    // A controller moves it, between Min and Max.
    s.receiveMidi("Launchkey", juce::MidiMessage::controllerEvent(1, 74, 127));
    s.drainMidiNow();
    const auto module = daw.call("get_module", {{"module", filter}});
    for (const auto& p : module["parameters"])
        if (p["id"] == "freq")
            CHECK(p["text"].get<std::string>().find("2.00 k") != std::string::npos);

    // A button: a note toggling the track's mute; a transport button, the app's.
    r = daw.call("map_midi", {{"track", t0},
                              {"button", "mute"},
                              {"message", {{"type", "note"}, {"number", "C1"}, {"device", "Launchkey"}}}});
    CHECK(r["mode"] == "toggle");
    CHECK(r["target"]["button"] == "mute");
    r = daw.call("map_midi", {{"transport", "record"},
                              {"message", {{"type", "cc"}, {"number", 64}, {"device", "Foot"}}},
                              {"mode", "momentary"}});
    CHECK(r["keptWith"] == "app");
    CHECK(r["target"]["transport"] == "record");
    const auto list = daw.call("get_midi_mappings")["mappings"];
    CHECK(list.size() == 3);
    const auto inputs = daw.project()["midiInputsMapped"];
    CHECK(std::find(inputs.begin(), inputs.end(), Json("Foot")) != inputs.end());
    CHECK(std::find(inputs.begin(), inputs.end(), Json("Launchkey")) != inputs.end());

    // Refused, saying why.
    CHECK(daw.fails("map_midi", {{"module", filter},
                                 {"parameter", "res"},
                                 {"message", {{"type", "cc14"}, {"number", 1}, {"device", "Launchkey"}}},
                                 {"mode", "toggle"}})
              .find("absolute") != std::string::npos);
    CHECK(daw.fails("map_midi",
                    {{"module", filter}, {"parameter", "res"}, {"message", {{"type", "cc"}, {"number", 1}}}})
              .find("device") != std::string::npos);
    CHECK(daw.fails("map_midi", {{"track", t0},
                                 {"button", "mute"},
                                 {"message", {{"type", "cc14"}, {"number", 40}, {"device", "Launchkey"}}}})
              .find("0-31") != std::string::npos);

    // Removed by message, or by target.
    r = daw.call("remove_midi_mapping", {{"message", knob}});
    CHECK(r["removed"]["target"]["parameter"] == "freq");
    CHECK(s.undoDescription() == "Assistant: remove midi mapping");
    daw.call("remove_midi_mapping", {{"transport", "record"}});
    CHECK(daw.call("get_midi_mappings")["mappings"].size() == 1);
    CHECK(daw.fails("remove_midi_mapping", {{"transport", "record"}}).find("no MIDI mapping") !=
          std::string::npos);
    s.setAppMidiMappings({}); // the engine's settings are per name: leave none
}

TEST_CASE("mcp tools: modules, parameters, samples and view state", "[integration][mcp]")
{
    Daw daw;
    const auto t0 = daw.project()["tracks"][0]["id"].get<std::string>();
    const auto types = daw.call("list_module_types", {{"kind", "effect"}})["types"];
    CHECK(std::any_of(types.begin(), types.end(), [](const Json& t) { return t["type"] == "eq"; }));
    CHECK(std::none_of(types.begin(), types.end(), [](const Json& t) { return t["kind"] == "instrument"; }));

    const auto eq = daw.call("add_module", {{"track", t0}, {"type", "eq"}});
    const auto sat = daw.call("add_module", {{"track", t0}, {"type", "Saturator"}, {"position", 0}});
    CHECK(sat["position"] == 0);
    CHECK(daw.session->undoDescription() == "Assistant: add module");
    CHECK(daw.fails("add_module", {{"track", t0}, {"type", "theremin"}}).find("list_module_types") != std::string::npos);

    const auto satId = sat["id"].get<std::string>();
    auto module = daw.call("get_module", {{"module", satId}});
    CHECK(module["type"] == "saturator");
    REQUIRE_FALSE(module["sections"].empty());
    REQUIRE_FALSE(module["parameters"].empty());
    const auto firstParam = module["parameters"][0];

    // Values, text, clamping and unknown names, in one undo step.
    const auto set = daw.call("set_parameters", {{"module", satId},
                                                  {"values",
                                                   {{"drive", 12},
                                                    {"output", "-3 dB"},
                                                    {"mix", 1000},
                                                    {"nonsense", 1}}}});
    CHECK(daw.session->undoDescription() == "Assistant: set parameters");
    std::map<std::string, Json> byId;
    for (const auto& e : set["set"])
        byId[e["id"].get<std::string>()] = e;
    REQUIRE(byId.contains("drive"));
    CHECK(byId["drive"]["value"] == Approx(12.0));
    CHECK(byId["output"]["value"] == Approx(-3.0));
    CHECK(byId["mix"]["clamped"] == true);
    CHECK(set["problems"][0]["id"] == "nonsense");
    module = daw.call("get_module", {{"module", satId}});
    for (const auto& p : module["parameters"])
        if (p["id"] == "drive")
            CHECK(p["text"].get<std::string>().find("12") != std::string::npos);
    daw.call("undo");
    for (const auto& p : daw.call("get_module", {{"module", satId}})["parameters"])
        if (p["id"] == "drive")
            CHECK(p["value"] != Approx(12.0));

    daw.call("set_module_on", {{"module", satId}, {"on", false}});
    CHECK(daw.call("get_module", {{"module", satId}})["on"] == false);
    const auto chain = daw.call("move_module", {{"module", satId}, {"position", 1}})["chain"];
    CHECK(chain[1]["id"] == satId);

    // View state: collapsed sections and fold, never an undo step.
    const auto steps = daw.session->undoDescription();
    const auto sectionId = module["sections"][0]["id"].get<std::string>();
    int viewChanges = 0;
    struct Listener final : fd::engine::Session::Listener
    {
        int& count;
        explicit Listener(int& c) : count(c) {}
        void deviceViewChanged(std::uint64_t) override { ++count; }
    } listener{viewChanges};
    daw.session->addListener(&listener);
    const auto view = daw.call("set_module_view", {{"module", satId}, {"collapsed", {sectionId}}, {"folded", true}});
    daw.session->removeListener(&listener);
    CHECK(view["collapsed"][0] == sectionId);
    CHECK(viewChanges == 1);
    CHECK(daw.session->undoDescription() == steps);
    module = daw.call("get_module", {{"module", satId}});
    CHECK(module["folded"] == true);
    CHECK(module["sections"][0]["collapsed"] == true);
    CHECK(daw.fails("set_module_view", {{"module", satId}, {"collapsed", {"nope"}}}).find(sectionId) != std::string::npos);
    // Pages (ADR 0051) are only for modules that have them.
    CHECK_FALSE(module.contains("pages"));
    CHECK(daw.fails("set_module_view", {{"module", satId}, {"page", "main"}}).find("no pages") !=
          std::string::npos);

    // Sidechains (ADR 0025): another track feeds a Compressor; never its own track, never a loop.
    const auto t1 = daw.call("add_track", {{"name", "Kick"}})["track"].get<std::string>();
    const auto comp = daw.call("add_module", {{"track", t0}, {"type", "compressor"}})["id"].get<std::string>();
    module = daw.call("get_module", {{"module", comp}});
    CHECK(module["sidechain"]["source"].is_null());
    const auto& possible = module["sidechain"]["possibleSources"];
    CHECK(std::find(possible.begin(), possible.end(), t1) != possible.end());
    CHECK(std::find(possible.begin(), possible.end(), t0) == possible.end());
    const auto routed = daw.call("set_module_sidechain", {{"module", comp}, {"source", t1}});
    CHECK(routed["source"] == t1);
    CHECK(daw.session->undoDescription() == "Assistant: set module sidechain");
    CHECK(daw.call("get_module", {{"module", comp}})["sidechain"]["source"] == t1);
    CHECK_FALSE(daw.fails("set_module_sidechain", {{"module", comp}, {"source", t0}}).empty()); // its own track
    const auto gate = daw.call("add_module", {{"track", t1}, {"type", "gate"}})["id"].get<std::string>();
    const auto gateSources = daw.call("get_module", {{"module", gate}})["sidechain"]["possibleSources"];
    CHECK(std::find(gateSources.begin(), gateSources.end(), t0) == gateSources.end()); // t0 hears t1: a loop
    CHECK(daw.fails("set_module_sidechain", {{"module", gate}, {"source", t0}}).find("loop") != std::string::npos);
    CHECK(daw.fails("set_module_sidechain", {{"module", satId}, {"source", t1}}).find("no sidechain") != std::string::npos);
    CHECK(daw.call("set_module_sidechain", {{"module", comp}, {"source", nullptr}})["source"].is_null());
    CHECK(daw.call("get_module", {{"module", comp}})["sidechain"]["source"].is_null());
    daw.call("undo");
    CHECK(daw.call("get_module", {{"module", comp}})["sidechain"]["source"] == t1);

    // A sample into a Sampler, with the file's root key.
    const auto sampler = daw.call("add_module", {{"track", t0}, {"type", "sampler"}})["id"];
    juce::TemporaryFile wav(".wav");
    REQUIRE(fd::testkit::writeWav(wav.getFile().getFullPathName().toStdString(),
                                  fd::testkit::sine(440.0, 48000.0, 24000, 0.5f, 1), 48000.0));
    const auto loaded = daw.call("load_sample", {{"module", sampler}, {"path", wav.getFile().getFullPathName().toStdString()}});
    CHECK(loaded["seconds"] == Approx(0.5));
    CHECK(daw.call("get_module", {{"module", sampler}})["state"]["sample.path"] == wav.getFile().getFullPathName().toStdString());
    CHECK(daw.fails("load_sample", {{"module", satId}, {"path", wav.getFile().getFullPathName().toStdString()}})
              .find("isn't a Sampler") != std::string::npos);

    daw.call("remove_module", {{"module", eq["id"]}});
    CHECK(daw.project()["tracks"][0]["chain"].size() == 3); // the Saturator, the Compressor and the Sampler

    const auto reference = daw.read("segno://module-type/saturator");
    CHECK_FALSE(reference["description"].get<std::string>().empty());
    CHECK_FALSE(reference["sections"].empty());
}

TEST_CASE(
    "mcp tools: a multisample in a Sampler: loaded with what it sets, its zones listed, found and heard",
    "[integration][mcp]")
{
    Daw daw("fd-mcp-multisample-tests");
    auto& s = *daw.session;
    // An SFZ beside its samples: soft and loud below C3, a round robin above it, one setting the
    // Sampler doesn't play.
    const auto place = daw.folder.getChildFile("Sounds");
    const auto samples = place.getChildFile("Strings").getChildFile("samples");
    REQUIRE(samples.createDirectory());
    for (const auto* name : {"soft.wav", "loud.wav", "rr1.wav", "rr2.wav"})
        REQUIRE(fd::testkit::writeWav(samples.getChildFile(name).getFullPathName().toStdString(),
                                      fd::testkit::sine(220.0, 48000.0, 4800, 0.5f, 1), 48000.0));
    const auto sfz = place.getChildFile("Strings").getChildFile("Strings.sfz");
    REQUIRE(sfz.replaceWithText(
        "<control> default_path=samples/\n<global> ampeg_release=0.5 pan=10\n"
        "<group> hikey=59 pitch_keycenter=48\n"
        "<region> sample=soft.wav hivel=63\n<region> sample=loud.wav lovel=64\n"
        "<group> lokey=60 pitch_keycenter=c4 seq_length=2\n"
        "<region> sample=rr1.wav seq_position=1\n<region> sample=rr2.wav seq_position=2\n"));
    const auto path = sfz.getFullPathName().toStdString();

    const auto t0 = daw.call("add_track", {{"name", "Strings"}})["track"];
    const auto sampler = daw.call("add_module", {{"track", t0}, {"type", "sampler"}})["id"];
    const auto loaded = daw.call("load_sample", {{"module", sampler}, {"path", path}});
    CHECK(loaded["zones"] == 4);
    CHECK(loaded["missing"] == 0);
    CHECK(loaded["ignored"] == Json::array({"pan"}));
    CHECK(loaded["seconds"] == Approx(0.1));
    CHECK(daw.call("get_module", {{"module", sampler}})["parameters"].dump().find("500") !=
          std::string::npos);

    const auto zones = daw.call("get_zones", {{"module", sampler}});
    CHECK(zones["path"] == path);
    REQUIRE(zones["zones"].size() == 4);
    CHECK(zones["zones"][0]["keys"] == Json{{"low", "C-2"}, {"high", "B2"}});
    CHECK(zones["zones"][0]["velocities"] == Json{{"low", 0}, {"high", 63}});
    CHECK(zones["zones"][0]["root"] == "C2");
    CHECK(zones["zones"][2]["root"] == "C3"); // SFZ's c4, Segno's C3
    CHECK(zones["zones"][3]["roundRobin"] == "2 of 2");
    CHECK(daw.fails("get_zones", {{"module", daw.call("add_module", {{"track", t0}, {"type", "eq"}})["id"]}})
              .find("isn't a Sampler") != std::string::npos);

    // Found among the browser's places as a multisample, and heard as a phrase.
    s.setPresetsFolder(daw.folder.getChildFile("Segno").getChildFile("Presets"));
    s.setSegnoFolder(daw.folder.getChildFile("Segno"));
    s.setBrowserPlaces({place.getFullPathName().toStdString()});
    s.waitForBrowserIndex();
    const auto found = daw.call("search", {{"query", "strings"}, {"kinds", {"multisample"}}})["results"];
    REQUIRE(found.size() == 1);
    CHECK(found[0]["kind"] == "multisample");
    CHECK(found[0]["path"] == path);
    CHECK(daw.call("preview", {{"item", path}})["playing"] == true);
    daw.call("preview", {{"stop", true}});
    const auto listed =
        daw.call("browse", {{"folder", sfz.getParentDirectory().getFullPathName().toStdString()}});
    REQUIRE(listed["files"].size() == 1);
    CHECK(listed["files"][0]["zones"] == 4);
}

TEST_CASE("mcp tools: the Wavetable Synth's tables, listed and set by id or from a file; its pages",
          "[integration][mcp]")
{
    Daw daw;
    const auto t0 = daw.project()["tracks"][0]["id"].get<std::string>();
    const auto synth =
        daw.call("add_module", {{"track", t0}, {"type", "wavetable"}})["id"].get<std::string>();

    // The factory tables, all or by category; the files of your own (none here).
    const auto listed = daw.call("list_wavetables");
    CHECK(listed["factory"].size() >= 60);
    const auto formant = daw.call("list_wavetables", {{"category", "formant"}})["factory"];
    REQUIRE_FALSE(formant.empty());
    for (const auto& t : formant)
        CHECK(t["category"] == "Formant");

    // By id: one undo step, carried in the module's state.
    const auto set =
        daw.call("set_wavetable", {{"module", synth}, {"oscillator", 2}, {"table", "formant/vowels"}});
    CHECK(set["table"] == "formant/vowels");
    CHECK(set["frames"].get<int>() > 1);
    CHECK(daw.session->undoDescription() == "Assistant: set wavetable");
    CHECK(daw.call("get_module", {{"module", synth}})["state"]["osc2.table"] == "formant/vowels");
    daw.call("undo");
    const auto undone = daw.call("get_module", {{"module", synth}});
    CHECK_FALSE((undone.contains("state") && undone["state"].contains("osc2.table")));

    // From a file: a single cycle, carried in the project as a table of your own.
    juce::TemporaryFile wav(".wav");
    fd::testkit::AudioBuffer cycle(1, 600);
    for (int i = 0; i < 600; ++i)
        cycle.at(0, i) = 0.8f * (2.0f * static_cast<float>(i) / 600.0f - 1.0f);
    REQUIRE(fd::testkit::writeWav(wav.getFile().getFullPathName().toStdString(), cycle, 48000.0));
    const auto fromFile = daw.call(
        "set_wavetable",
        {{"module", synth}, {"oscillator", 1}, {"path", wav.getFile().getFullPathName().toStdString()}});
    CHECK(fromFile["table"] == "user");
    CHECK(fromFile["frames"] == 1);
    CHECK(fromFile["name"] == wav.getFile().getFileNameWithoutExtension().toStdString());
    CHECK(daw.call("get_module", {{"module", synth}})["state"]["osc1.table"] == "user");

    CHECK(daw.fails("set_wavetable", {{"module", synth}, {"oscillator", 1}}).find("either") !=
          std::string::npos);
    CHECK(daw.fails("set_wavetable", {{"module", synth}, {"oscillator", 1}, {"table", "basic/nope"}})
              .find("list_wavetables") != std::string::npos);
    const auto sat = daw.call("add_module", {{"track", t0}, {"type", "saturator"}})["id"];
    CHECK(daw.fails("set_wavetable", {{"module", sat}, {"oscillator", 1}, {"table", "basic/saw"}})
              .find("isn't a Wavetable.") != std::string::npos);

    // Its pages (ADR 0051): listed with their sections, turned by set_module_view.
    auto module = daw.call("get_module", {{"module", synth}});
    REQUIRE(module["pages"].size() == 3);
    CHECK(module["page"] == "oscillators");
    CHECK(module["pages"][2]["id"] == "modulation");
    const auto& onModulation = module["pages"][2]["sections"];
    CHECK(std::find(onModulation.begin(), onModulation.end(), "matrix") != onModulation.end());
    const auto view = daw.call("set_module_view", {{"module", synth}, {"page", "modulation"}});
    CHECK(view["page"] == "modulation");
    CHECK(daw.call("get_module", {{"module", synth}})["page"] == "modulation");
    CHECK(daw.fails("set_module_view", {{"module", synth}, {"page", "nope"}}).find("oscillators") !=
          std::string::npos);
}

TEST_CASE(
    "mcp tools: a wavetable made from audio, read and edited as a recipe, exported (table-editor.md §8)",
    "[integration][mcp]")
{
    Daw daw;
    const auto t0 = daw.project()["tracks"][0]["id"].get<std::string>();
    const auto synth =
        daw.call("add_module", {{"track", t0}, {"type", "wavetable"}})["id"].get<std::string>();

    // An audio clip: two seconds of a 220 Hz saw.
    juce::TemporaryFile wav(".wav");
    fd::testkit::AudioBuffer saw(1, 96000);
    for (int i = 0; i < saw.numSamples(); ++i)
    {
        double v = 0.0;
        for (int h = 1; h <= 30; ++h)
            v += std::sin(2.0 * 3.141592653589793 * h * 220.0 * i / 48000.0) / h;
        saw.at(0, i) = static_cast<float>(0.4 * v);
    }
    REQUIRE(fd::testkit::writeWav(wav.getFile().getFullPathName().toStdString(), saw, 48000.0));
    const auto clip = daw.call(
        "insert_audio_file",
        {{"track", t0}, {"start", 0}, {"path", wav.getFile().getFullPathName().toStdString()}})["clip"]["id"];

    // Made from the clip's first beat, pitched: one undo step, the pitch found.
    const auto made = daw.call("make_wavetable", {{"module", synth},
                                                  {"oscillator", 1},
                                                  {"clip", clip},
                                                  {"to", 1.0},
                                                  {"mode", "pitched"},
                                                  {"frames", 16}});
    CHECK(made["table"] == "user");
    CHECK(made["frames"] == 16);
    CHECK(std::abs(made["pitch"].get<double>() - 220.0) < 0.2);
    CHECK(daw.session->undoDescription() == "Assistant: make wavetable");
    CHECK(daw.fails("make_wavetable", {{"module", synth}, {"oscillator", 1}, {"mode", "pitched"}})
              .find("either") != std::string::npos);

    // Read as a recipe: one audio layer, its frames counted, not carried.
    auto got = daw.call("get_wavetable", {{"module", synth}, {"oscillator", 1}});
    CHECK(got["edited"] == true);
    CHECK(got["frames"] == 16);
    auto recipe = got["recipe"];
    REQUIRE(recipe["layers"].size() == 1);
    CHECK(recipe["layers"][0]["kind"] == "audio");
    CHECK(recipe["layers"][0]["dataFrames"] == 16);
    CHECK_FALSE(recipe["layers"][0].contains("data"));
    CHECK(daw.call("get_wavetable",
                   {{"module", synth}, {"oscillator", 1}, {"data", true}})["recipe"]["layers"][0]
              .contains("data"));
    // A factory table reads as the recipe the editor starts from.
    const auto factory = daw.call("get_wavetable", {{"module", synth}, {"oscillator", 2}});
    CHECK(factory["edited"] == false);
    CHECK(factory["recipe"]["layers"][0]["kind"] == "table");

    // Edited: a formula layer added and a change, the audio layer keeping its frames.
    recipe["layers"].push_back({{"kind", "formula"},
                                {"on", true},
                                {"level", 0.5},
                                {"fill", "crossfade"},
                                {"formula", "sin(2*pi*x*(1+3*t))"}});
    recipe["changes"].push_back({{"kind", "smooth"}, {"on", true}, {"amounts", {{0, 0.4}}}});
    recipe["name"] = "Saw and sines";
    const auto edited =
        daw.call("edit_wavetable", {{"module", synth}, {"oscillator", 1}, {"recipe", recipe}});
    CHECK(edited["name"] == "Saw and sines");
    CHECK(daw.session->undoDescription() == "Assistant: edit wavetable");
    got = daw.call("get_wavetable", {{"module", synth}, {"oscillator", 1}});
    REQUIRE(got["recipe"]["layers"].size() == 2);
    CHECK(got["recipe"]["layers"][0]["dataFrames"] == 16);
    CHECK(got["recipe"]["changes"].size() == 1);
    recipe["layers"][1]["formula"] = "sin(2*pi*x";
    CHECK(daw.fails("edit_wavetable", {{"module", synth}, {"oscillator", 1}, {"recipe", recipe}})
              .find("can't be used") != std::string::npos);
    CHECK(daw.call("get_wavetable", {{"module", synth}, {"oscillator", 1}})["name"] == "Saw and sines");

    // The factory tables in the Library (ADR 0055): found as its files, heard, set by their ID.
    const auto library = writeTestLibrary(daw.folder.getChildFile("Library"));
    daw.session->setLibraryFolder(library);
    daw.session->setSegnoFolder(daw.folder.getChildFile("Segno"));
    daw.session->waitForBrowserIndex();
    const auto cello = library.getChildFile("Wavetables")
                           .getChildFile("Acoustic")
                           .getChildFile("Cello.wav")
                           .getFullPathName()
                           .toStdString();
    const auto tables = daw.call("search", {{"query", "cello"}, {"kinds", {"wavetable"}}})["results"];
    REQUIRE_FALSE(tables.empty());
    CHECK(tables[0]["path"] == cello);
    CHECK(tables[0]["from"] == "Library · Acoustic");
    CHECK(daw.call("preview", {{"item", cello}})["playing"] == true);
    daw.call("preview", {{"stop", true}});
    const auto listed = daw.call("list_wavetables", {{"category", "Acoustic"}})["factory"];
    CHECK(std::any_of(listed.begin(), listed.end(), [&](const Json& t)
                      { return t["id"] == "acoustic/cello" && t.value("path", "") == cello; }));
    const auto set = daw.call("set_wavetable", {{"module", synth}, {"oscillator", 2}, {"path", cello}});
    CHECK(set["table"] == "acoustic/cello"); // the table itself, not a copy
    CHECK(set["name"] == "Cello");

    // Exported: WAV with its frame size, or .wt; read back by set_wavetable.
    const auto dir = juce::File::getSpecialLocation(juce::File::tempDirectory)
                         .getChildFile("fd-export-wavetable-" + juce::Uuid().toString());
    REQUIRE(dir.createDirectory());
    const auto steps = daw.session->undoDescription();
    const auto out = daw.call("export_wavetable",
                              {{"module", synth},
                               {"oscillator", 1},
                               {"path", dir.getChildFile("Saw and sines").getFullPathName().toStdString()},
                               {"format", "wt"}});
    CHECK(out["frames"] == 16);
    CHECK(juce::File(out["path"].get<std::string>()).hasFileExtension("wt"));
    CHECK(daw.session->undoDescription() == steps); // the project unchanged
    const auto back =
        daw.call("set_wavetable", {{"module", synth}, {"oscillator", 3}, {"path", out["path"]}});
    CHECK(back["frames"] == 16);
    CHECK(daw.call("preview", {{"item", out["path"]}})["playing"] ==
          true); // a wavetable file, on a held note
    daw.call("preview", {{"stop", true}});
    dir.deleteRecursively();
    daw.session->setLibraryFolder({});
}

TEST_CASE("mcp tools: each editor's grid, never an undo step (ADR 0047)", "[integration][mcp]")
{
    using Editor = fd::engine::Session::GridEditor;
    Daw daw;
    auto grids = daw.call("get_grid");
    CHECK(grids["arrangement"]["mode"] == "adaptive");
    CHECK(grids["arrangement"]["width"] == "narrow");
    CHECK(grids["arrangement"]["snap"] == true);
    CHECK(grids["piano_roll"]["width"] == "wide");
    CHECK(grids["piano_roll"]["step"].is_null()); // no editor shows it here

    int changes = 0;
    struct Listener final : fd::engine::Session::Listener
    {
        int& count;
        explicit Listener(int& c) : count(c) {}
        void gridChanged() override { ++count; }
    } listener{changes};
    daw.session->addListener(&listener);
    const auto steps = daw.session->undoDescription();
    // The piano roll at fixed 1/16 triplets: its step is known at once; the arrangement's stays.
    const auto roll = daw.call(
        "set_grid", {{"editor", "piano_roll"}, {"mode", "fixed"}, {"value", "1/16"}, {"triplets", true}});
    CHECK(roll["mode"] == "fixed");
    CHECK(roll["step"] == "1/16T");
    CHECK(changes == 1);
    CHECK(daw.session->grid(Editor::PianoRoll).triplets);
    CHECK(daw.session->grid(Editor::Arrangement) == fd::model::GridSettings::arrangement());
    // Only what's given changes; an adaptive step follows the editor's zoom.
    const auto arrange =
        daw.call("set_grid", {{"editor", "arrangement"}, {"width", "widest"}, {"snap", false}});
    CHECK(arrange["width"] == "widest");
    CHECK(arrange["snap"] == false);
    CHECK(arrange["mode"] == "adaptive");
    CHECK(arrange["step"].is_null());
    daw.session->setGridStep(Editor::Arrangement, "2 bars"); // as the editor publishes it
    grids = daw.call("get_grid");
    CHECK(grids["arrangement"]["step"] == "2 bars");
    CHECK(grids["piano_roll"]["value"] == "1/16");
    daw.session->removeListener(&listener);
    CHECK(changes == 2);
    CHECK(daw.session->undoDescription() == steps); // view state: never an undo step
    CHECK_FALSE(daw.fails("set_grid", {{"editor", "mixer"}}).empty());
    CHECK_FALSE(daw.fails("set_grid", {{"editor", "arrangement"}, {"value", "1/64"}}).empty());
}

TEST_CASE("mcp tools: transport, tempo and signature", "[integration][mcp]")
{
    Daw daw;
    CHECK(daw.call("set_tempo", {{"bpm", 96}})["tempo"] == Approx(96.0));
    CHECK(daw.session->undoDescription() == "Assistant: set tempo");
    CHECK(daw.call("set_time_signature", {{"numerator", 3}, {"denominator", 4}})["timeSignature"]["numerator"] == 3);
    CHECK(daw.call("set_position", {{"at", "3.1.1"}})["position"]["beats"] == Approx(6.0));
    const auto loop = daw.call("set_loop", {{"start", "2.1.1"}, {"end", "4.1.1"}});
    CHECK(loop["loop"]["on"] == true);
    CHECK(loop["loop"]["end"]["beats"] == Approx(9.0));
    CHECK(daw.fails("set_loop", {{"start", 8}, {"end", 4}}).find("after its start") != std::string::npos);
    CHECK(daw.call("set_metronome", {{"on", true}})["metronome"] == true);
    CHECK(daw.call("play")["playing"] == true);
    CHECK(daw.call("stop")["playing"] == false);
    CHECK(daw.read("segno://transport")["metronome"] == true);
    CHECK(daw.fails("set_time_signature", {{"numerator", 3}, {"denominator", 5}}).find("denominator") != std::string::npos);
}

TEST_CASE("mcp tools: the song's key: set, removed, its scales listed, found from the notes (P3)",
          "[integration][mcp]")
{
    Daw daw;
    CHECK(daw.project()["key"].is_null()); // a new project has none

    auto r = daw.call("set_key", {{"root", "F#"}, {"scale", "minor"}});
    CHECK(r["key"]["root"] == "F#");
    CHECK(r["key"]["scale"] == "minor");
    CHECK(r["key"]["name"] == "F# minor");
    CHECK(r["key"]["notes"] == Json::array({"F#", "G#", "A", "B", "C#", "D", "E"}));
    CHECK(daw.session->undoDescription() == "Assistant: set key");
    CHECK(daw.project()["key"]["name"] == "F# minor");
    CHECK(daw.read("segno://project")["key"]["scale"] == "minor");

    // Names, flats in any spelling, pitch classes; the scale by its name; major by default.
    CHECK(daw.call("set_key", {{"root", "Bb"}, {"scale", "Dorian"}})["key"]["name"] == "Bb Dorian");
    CHECK(daw.call("set_key", {{"root", "E♭"}})["key"]["name"] == "Eb major");
    CHECK(daw.call("set_key", {{"root", 1}, {"scale", "major"}})["key"]["name"] == "Db major");
    CHECK(daw.call("set_key", {{"root", "e"}, {"scale", "bhairav"}})["key"]["scale"] == "double-harmonic");

    CHECK(daw.call("set_key", {{"none", true}})["key"].is_null());
    CHECK(daw.project()["key"].is_null());
    daw.call("undo");
    CHECK(daw.project()["key"]["scale"] == "double-harmonic");

    CHECK(daw.fails("set_key", {{"root", "H"}}).find("note name") != std::string::npos);
    CHECK(daw.fails("set_key", {{"root", 12}}).find("pitch class") != std::string::npos);
    CHECK(daw.fails("set_key", {{"root", "C"}, {"scale", "chromatic"}}).find("hirajoshi") !=
          std::string::npos);
    CHECK(daw.fails("set_key", {}).find("\"none\"") != std::string::npos);
    CHECK(daw.fails("set_key", {{"root", "C"}, {"none", true}}).find("\"none\"") != std::string::npos);

    const auto scales = daw.call("list_scales")["scales"];
    REQUIRE(scales.size() == 36);
    CHECK(scales[0]["id"] == "major");
    CHECK(scales[0]["intervals"] == Json::array({0, 2, 4, 5, 7, 9, 11}));
    CHECK(scales[1]["group"] == "Common");

    // Found from the notes: none until there are enough; then A minor first.
    const auto t0 = daw.project()["tracks"][0]["id"].get<std::string>();
    const auto clip =
        daw.call("create_midi_clip", {{"track", t0}, {"start", "1.1.1"}, {"length", "2 bars"}})["clip"]["id"];
    auto found = daw.call("detect_key");
    CHECK(found["keys"].empty());
    CHECK(found["note"].get<std::string>().find("Too few notes") != std::string::npos);
    Json notes = Json::array();
    double at = 0.0;
    for (int p : {69, 71, 72, 74, 76, 77, 80, 81})
    {
        notes.push_back({{"pitch", p}, {"start", at}, {"length", 0.5}});
        at += 0.5;
    }
    for (int p : {57, 69, 72, 76})
        notes.push_back({{"pitch", p}, {"start", 4.0}, {"length", 2.0}});
    for (int p : {64, 68, 71, 74})
        notes.push_back({{"pitch", p}, {"start", 6.0}, {"length", 2.0}});
    daw.call("add_notes", {{"clip", clip}, {"notes", notes}});
    found = daw.call("detect_key", {{"clips", {clip}}});
    REQUIRE_FALSE(found["keys"].empty());
    CHECK(found["keys"][0]["name"] == "A minor");
    CHECK(found["keys"][0]["fit"].get<double>() > 0.5);
    CHECK(found["keys"].size() <= 3);
    CHECK(daw.project()["key"]["scale"] == "double-harmonic"); // it only suggests
}

TEST_CASE("mcp tools: get_settings and set_settings, never the assistants' own or the keys (C1)",
          "[integration][mcp]")
{
    Daw daw("fd-mcp-settings-tests");
    daw.session->setCountIn(fd::model::CountIn::OneBar); // the engine keeps it between runs
    auto settings = daw.call("get_settings");
    CHECK(settings["recording"]["format"] == "wav");
    CHECK(settings["recording"]["bitDepth"] == 24);
    CHECK(settings["recording"]["countIn"] == "1 bar");
    CHECK(settings["export"]["sampleRate"] == "project");
    CHECK(settings["display"]["interfaceSize"] == "automatic");
    CHECK(settings["keys"].empty());
    CHECK(settings.contains("audio"));

    const auto set = daw.call(
        "set_settings",
        {{"recording", {{"format", "flac"}, {"bitDepth", 16}, {"countIn", "2 bars"}, {"audioOffsetMs", 12}}},
         {"export", {{"format", "aiff"}, {"sampleRate", 96000}, {"normalise", "loudness"}}},
         {"display", {{"interfaceSize", 125}}}});
    CHECK(set["recording"]["format"] == "flac");
    CHECK(set["recording"]["bitDepth"] == 16);
    CHECK(set["recording"]["countIn"] == "2 bars");
    CHECK(set["recording"]["audioOffsetMs"] == Approx(12.0));
    CHECK(set["export"]["format"] == "aiff");
    CHECK(set["export"]["bitDepth"] == 24);
    CHECK(set["display"]["interfaceSize"] == 125);
    CHECK(daw.session->countIn() == fd::model::CountIn::TwoBars);
    CHECK(daw.session->exportSettings().format == fd::model::ExportFormat::Aiff); // a new project's start
    CHECK(daw.session->undoDescription().empty());                                // not an undo step
    CHECK(daw.folder.getChildFile("settings.json").loadFileAsString().contains("\"flac\"")); // kept

    // Refused: their own settings, the keys, what isn't offered; nothing changes.
    CHECK(daw.fails("set_settings", {{"assistants", {{"readOnly", false}}}}).find("can't change their own") !=
          std::string::npos);
    CHECK(daw.fails("set_settings", {{"keys", {{"edit.undo", {"Ctrl+U"}}}}}).find("shortcuts") !=
          std::string::npos);
    CHECK(daw.fails("set_settings", {{"recording", {{"format", "mp3"}}}}).find("\"wav\"") !=
          std::string::npos);
    CHECK(daw.fails("set_settings", {{"recording", {{"bitDepth", 32}}}}).find("16 or 24") !=
          std::string::npos);
    CHECK(daw.fails("set_settings", {{"display", {{"interfaceSize", 110}}}}).find("125") !=
          std::string::npos);
    CHECK(daw.fails("set_settings", {{"midi", {{"inputs", {{{"name", "Nowhere"}, {"enabled", false}}}}}}})
              .find("no MIDI input") != std::string::npos);
    CHECK(daw.fails("set_settings",
                    {{"recording", {{"countIn", "3 bars"}}}, {"display", {{"interfaceSize", 150}}}})
              .find("countIn") != std::string::npos);
    CHECK(daw.call("get_settings")["display"]["interfaceSize"] == 125); // the refused call changed nothing
    // Read-only mode refuses changes and allows reading.
    daw.server->setReadOnly(true);
    CHECK(daw.fails("set_settings", {{"display", {{"interfaceSize", 150}}}}).find("read-only") !=
          std::string::npos);
    CHECK(daw.call("get_settings")["assistants"]["readOnly"] == true);
}

TEST_CASE("mcp tools: search, browse, preview and collect_files, as the browser does (C2)",
          "[integration][mcp]")
{
    Daw daw("fd-mcp-browser-tests");
    auto& s = *daw.session;
    const auto place = daw.folder.getChildFile("Sounds");
    REQUIRE(place.createDirectory());
    const auto kick = place.getChildFile("Kick 120 bpm.wav");
    REQUIRE(fd::testkit::writeWav(kick.getFullPathName().toStdString(),
                                  fd::testkit::sine(220.0, 48000.0, 24000, 0.5f, 2), 48000.0));
    s.setPresetsFolder(daw.folder.getChildFile("Segno").getChildFile("Presets"));
    s.setSegnoFolder(daw.folder.getChildFile("Segno"));
    s.setBrowserPlaces({place.getFullPathName().toStdString()});
    s.waitForBrowserIndex();

    // search: modules, presets, files, best first, with ids the other tools take.
    auto found = daw.call("search", {{"query", "bus glue"}});
    REQUIRE_FALSE(found["results"].empty());
    CHECK(found["results"][0]["id"] == "preset:fd:compressor|factory/Bus glue");
    CHECK(found["results"][0]["from"] == "Compressor");
    found = daw.call("search", {{"query", "kick"}, {"kinds", {"sample"}}});
    REQUIRE(found["results"].size() == 1);
    CHECK(found["results"][0]["path"] == kick.getFullPathName().toStdString());
    found = daw.call("search", {{"query", "compressor"}, {"kinds", {"module"}}});
    REQUIRE(found["results"].size() == 1);
    CHECK(found["results"][0]["type"] == "fd:compressor");
    CHECK(daw.call("search", {{"query", "subtractive"}, {"limit", 3}})["results"].size() == 3);

    // browse: the places, then one's files. The Library first, missing here (ADR 0055), then yours.
    const auto places = daw.call("browse")["places"];
    CHECK(std::any_of(places.begin(), places.end(),
                      [&](const Json& p) { return p["path"] == place.getFullPathName().toStdString(); }));
    REQUIRE(places.size() >= 2);
    CHECK(places[0]["name"] == "Library");
    CHECK(places[0]["missing"] == true);
    CHECK(places[1]["name"] == "User library");
    const auto files = daw.call("browse", {{"folder", place.getFullPathName().toStdString()}})["files"];
    REQUIRE(files.size() == 1);
    CHECK(files[0]["tempo"] == Approx(120.0));
    CHECK(daw.fails("browse", {{"folder", "/"}}).find("places") != std::string::npos);

    // preview: a sample, an instrument's preset; an effect's preset refused.
    CHECK(daw.call("preview", {{"item", kick.getFullPathName().toStdString()}})["playing"] == true);
    CHECK(s.isPreviewing());
    CHECK(daw.call("preview", {{"item", "preset:fd:subtractive-synth|factory/Sub bass"}})["playing"] == true);
    CHECK(daw.fails("preview", {{"item", "preset:fd:compressor|factory/Bus glue"}}).find("instruments") !=
          std::string::npos);
    CHECK(daw.call("preview", {{"stop", true}})["playing"] == false);
    CHECK_FALSE(s.isPreviewing());
    // Read-only mode lets assistants look and listen.
    daw.server->setReadOnly(true);
    CHECK(daw.call("search", {{"query", "kick"}, {"kinds", {"sample"}}})["total"] == 1);
    CHECK(daw.fails("collect_files").find("read-only") != std::string::npos);
    daw.server->setReadOnly(false);

    // collect_files: only a saved project; then the outside file is copied in, and it's saved.
    CHECK(daw.fails("collect_files").find("Save the project first") != std::string::npos);
    const auto song = daw.folder.getChildFile("Song").getChildFile("Song.segno");
    REQUIRE(s.saveAs(song));
    REQUIRE(s.insertAudioFile(s.snapshot()->tracks.front().id, kick, 0.0) != 0);
    const auto collected = daw.call("collect_files");
    CHECK(collected["copied"] == 1);
    CHECK(song.getParentDirectory().getChildFile("Samples").getChildFile(kick.getFileName()).existsAsFile());
    CHECK_FALSE(s.hasUnsavedChanges());
}

TEST_CASE("mcp tools: read-only mode and confirmations", "[integration][mcp]")
{
    Daw daw;
    daw.server->setReadOnly(true);
    CHECK(daw.fails("add_track").find("read-only") != std::string::npos);
    CHECK(daw.project()["tracks"].size() == 4);
    daw.server->setReadOnly(false);

    daw.call("add_track"); // an unsaved change
    REQUIRE(daw.session->hasUnsavedChanges());
    // A client that can't ask the user: the assistant must ask, then confirm.
    CHECK(daw.fails("new_project").find("\"confirm\": true") != std::string::npos);
    CHECK(daw.project()["tracks"].size() == 5);
    // A client that can: a multi round-trip request, then the retry.
    const Json elicitation{{"elicitation", Json::object()}};
    const auto asked = daw.callResult("new_project", Json::object(), elicitation);
    REQUIRE(asked["resultType"] == "input_required");
    auto retry = modernRequest("tools/call",
                               {{"name", "new_project"},
                                {"arguments", Json::object()},
                                {"requestState", asked["requestState"]},
                                {"inputResponses", {{"confirm", {{"action", "accept"}}}}}},
                               elicitation);
    const auto done = daw.server->dispatcher()->handle(retry, {});
    CHECK((*done.message)["result"]["structuredContent"]["tracks"].size() == 4);

    // Saving: a project is a folder (ADR 0030); over another existing project only when confirmed.
    const auto path = daw.folder.getChildFile("song").getFullPathName().toStdString();
    const auto saved = daw.call("save_project", {{"path", path}});
    const auto songFile = daw.folder.getChildFile("song").getChildFile("song.segno").getFullPathName().toStdString();
    CHECK(saved["file"] == songFile);
    CHECK(daw.project()["unsaved"] == false);
    const auto other = daw.folder.getChildFile("other").getChildFile("other.segno");
    REQUIRE(other.getParentDirectory().createDirectory());
    REQUIRE(other.replaceWithText("x"));
    const auto otherPath = daw.folder.getChildFile("other").getFullPathName().toStdString();
    CHECK(daw.fails("save_project", {{"path", otherPath}}).find("Overwrite") != std::string::npos);
    CHECK(daw.call("save_project", {{"path", otherPath}, {"confirm", true}})["file"] == other.getFullPathName().toStdString());
    const auto opened = daw.call("open_project", {{"path", songFile}});
    CHECK(opened["file"] == songFile);
}

TEST_CASE("mcp tools: render measures a range without touching the project", "[integration][mcp]")
{
    Daw daw;
    const auto t0 = daw.project()["tracks"][0]["id"].get<std::string>();
    daw.call("add_module", {{"track", t0}, {"type", "Subtractive"}}); // something to play the clip
    daw.call("create_midi_clip", {{"track", t0},
                                  {"start", 0},
                                  {"length", "1 bar"},
                                  {"notes", {{{"pitch", "A3"}, {"start", 0}, {"length", 4}, {"velocity", 127}}}}});
    const auto undo = daw.session->undoDescription();
    const bool unsaved = daw.session->hasUnsavedChanges();

    const auto r = daw.callAway("render", {{"start", 0}, {"end", "1 bar"}, {"audio", true}});
    INFO(r.dump().substr(0, 2000));
    REQUIRE(r["isError"] == false);
    const auto m = r["structuredContent"];
    CHECK(m["what"] == "mix");
    CHECK(m["silent"] == false);
    CHECK(m["seconds"] == Approx(2.0));
    CHECK(m["peak"].get<double>() < 0.0);
    CHECK(m["peak"].get<double>() > -40.0);
    CHECK(m["spectrum"].size() == 7);
    CHECK(m["truePeak"].get<double>() >= m["peak"].get<double>() - 0.01);
    CHECK(m["loudness"]["integrated"].get<double>() < 0.0);
    CHECK(m["loudness"]["shortTermMax"].is_null()); // a 2 s range
    CHECK(m["loudness"]["range"].is_null());        // too short for one
    REQUIRE(r["content"].back()["type"] == "audio");
    const auto wav = fd::mcp::base64Decode(r["content"].back()["data"].get<std::string>());
    REQUIRE(wav);
    CHECK(wav->substr(0, 4) == "RIFF");
    CHECK(wav->size() == 44 + 96000 * 2 * 2);

    // One track, and a silent one.
    const auto t1 = daw.project()["tracks"][1]["id"].get<std::string>();
    const auto silent = daw.callAway("render", {{"start", 0}, {"end", 4}, {"what", t1}})["structuredContent"];
    CHECK(silent["silent"] == true);
    CHECK(silent["peak"].is_null());

    CHECK(daw.session->undoDescription() == undo);
    CHECK(daw.session->hasUnsavedChanges() == unsaved);
    CHECK(daw.callAway("render", {{"start", 4}, {"end", 0}})["isError"] == true);
}

TEST_CASE("mcp tools: measure_module reads a module's meters by name", "[integration][mcp]")
{
    Daw daw;
    const auto t0 = daw.project()["tracks"][0]["id"].get<std::string>();
    const auto comp = daw.call("add_module", {{"track", t0}, {"type", "compressor"}})["id"].get<std::string>();
    const auto spectrum =
        daw.call("add_module", {{"track", t0}, {"type", "spectrum"}})["id"].get<std::string>();

    // Stand in for the audio thread, publishing while it watches.
    auto handle = daw.session->openDevice(std::stoull(comp.substr(2)));
    REQUIRE(handle != nullptr);
    auto slotOf = [&](std::string_view id) {
        for (const auto& m : handle->context().info.meters)
            if (m.id == id)
                return m.slot;
        FAIL("no meter " << id);
        return 0;
    };
    auto& bus = *handle->context().analysis;
    bus.publishPeak(slotOf("gain_reduction"), 20.0f); // before the window: not counted
    auto future = std::async(std::launch::async, [&] {
        return daw.server->dispatcher()->handle(
            modernRequest("tools/call", {{"name", "measure_module"}, {"arguments", {{"module", comp}, {"seconds", 0.3}}}}),
            {});
    });
    while (future.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready)
    {
        bus.publishPeak(slotOf("gain_reduction"), 4.2f);
        bus.publishPeak(slotOf("output_left"), 0.5f);
        juce::MessageManager::getInstance()->runDispatchLoopUntil(10);
    }
    const auto out = future.get();
    REQUIRE(out.message);
    const auto result = (*out.message)["result"];
    INFO(result.dump());
    REQUIRE(result["isError"] == false);
    const auto& measured = result["structuredContent"];
    CHECK(measured["playing"] == false);
    std::map<std::string, Json> byId;
    for (const auto& m : measured["meters"])
        byId[m["id"].get<std::string>()] = m;
    CHECK(byId["gain_reduction"]["value"] == Approx(4.2));
    CHECK(byId["gain_reduction"]["unit"] == "dB");
    CHECK(byId["output_left"]["value"] == Approx(-6.0).margin(0.1));
    CHECK(byId["output_left"]["unit"] == "dBFS");
    CHECK(byId["input_left"]["value"].is_null()); // silent
    CHECK(result["content"][0]["text"].get<std::string>().find("Nothing was playing") != std::string::npos);
    // The panel's own meter still has its peak: the measurement read a copy.
    CHECK(bus.takePeak(slotOf("gain_reduction")) == Approx(20.0f));
    handle.reset();

    CHECK(daw.fails("measure_module", {{"module", spectrum}}).find("no meters") != std::string::npos);
}

TEST_CASE("mcp tools: the Loudness Meter on the master, measured through MCP: LUFS, LU and correlation (B7)",
          "[integration][mcp]")
{
    Daw daw;
    tracktion::HostedAudioDeviceInterface::Parameters io;
    io.sampleRate = 48000.0;
    io.blockSize = 480;
    io.inputChannels = 2;
    io.outputChannels = 2;
    tracktion::test_utilities::EnginePlayer player(daw.engine, io);
    // A 1 kHz sine at -20 dBFS into the input, heard on track 0: -20 LUFS on the master (at unity).
    juce::AudioBuffer<float> in(2, 480);
    std::int64_t clock = 0;
    auto pump = [&](double seconds)
    {
        for (int b = 0; b < static_cast<int>(seconds * 100.0); ++b)
        {
            for (int c = 0; c < 2; ++c)
                for (int s = 0; s < 480; ++s)
                    in.setSample(c, s,
                                 0.1f * std::sin(2.0f * juce::MathConstants<float>::pi * 1000.0f *
                                                 static_cast<float>(clock + s) / 48000.0f));
            clock += 480;
            player.process(in);
            juce::MessageManager::getInstance()->runDispatchLoopUntil(1);
        }
    };
    pump(0.1);
    auto project = daw.project();
    const auto t0 = project["tracks"][0]["id"].get<std::string>();
    const auto master = project["master"]["id"].get<std::string>();
    daw.call("set_track_input",
             {{"track", t0}, {"audioInput", project["inputs"]["audio"][0]}, {"monitor", "in"}});
    const auto meter =
        daw.call("add_module", {{"track", master}, {"type", "loudness-meter"}})["id"].get<std::string>();
    // Nothing plays, so it counts as it runs (EBU Mode).
    daw.call("set_parameters", {{"module", meter}, {"values", {{"measure", "Running"}}}});
    pump(5.0);

    auto future = std::async(
        std::launch::async,
        [&]
        {
            return daw.server->dispatcher()->handle(
                modernRequest("tools/call", {{"name", "measure_module"},
                                             {"arguments", {{"module", meter}, {"seconds", 0.2}}}}),
                {});
        });
    while (future.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready)
        pump(0.01);
    const auto out = future.get();
    REQUIRE(out.message);
    const auto result = (*out.message)["result"];
    INFO(result.dump());
    REQUIRE(result["isError"] == false);
    std::map<std::string, Json> byId;
    for (const auto& m : result["structuredContent"]["meters"])
        byId[m["id"].get<std::string>()] = m;
    const auto level = byId["integrated"]["value"];
    REQUIRE(level.is_number());
    const double lufs = level.get<double>();
    CHECK(byId["integrated"]["unit"] == "LUFS");
    CHECK(byId["short_term"]["value"].get<double>() == Approx(lufs).margin(0.2));
    CHECK(byId["range"]["unit"] == "LU");
    CHECK(byId["range"]["value"].get<double>() == Approx(0.0).margin(0.2)); // a steady tone
    CHECK(byId["correlation"]["value"].get<double>() == Approx(1.0).margin(0.01));
    CHECK_FALSE(byId["correlation"].contains("unit"));
    CHECK(byId["balance"]["value"].get<double>() == Approx(0.0).margin(0.1));
    CHECK(byId["normalised"]["unit"] == "dB");
    CHECK(byId["normalised"]["value"].get<double>() == Approx(-14.0 - lufs).margin(0.1));
    CHECK(byId["counting"]["value"] == 0);
    CHECK(byId["counted_seconds"]["value"].get<int>() >= 4);
}

TEST_CASE("mcp tools: the Tuner on a track, measured through MCP: note, cents, frequency (B10)",
          "[integration][mcp]")
{
    Daw daw;
    tracktion::HostedAudioDeviceInterface::Parameters io;
    io.sampleRate = 48000.0;
    io.blockSize = 480;
    io.inputChannels = 2;
    io.outputChannels = 2;
    tracktion::test_utilities::EnginePlayer player(daw.engine, io);
    // A 220 Hz sine (A2: C3 = 60) into the input, heard on track 0.
    juce::AudioBuffer<float> in(2, 480);
    std::int64_t clock = 0;
    auto pump = [&](double seconds)
    {
        for (int b = 0; b < static_cast<int>(seconds * 100.0); ++b)
        {
            for (int c = 0; c < 2; ++c)
                for (int s = 0; s < 480; ++s)
                    in.setSample(c, s,
                                 0.2f * std::sin(2.0f * juce::MathConstants<float>::pi * 220.0f *
                                                 static_cast<float>(clock + s) / 48000.0f));
            clock += 480;
            player.process(in);
            juce::MessageManager::getInstance()->runDispatchLoopUntil(1);
        }
    };
    pump(0.1);
    auto project = daw.project();
    const auto t0 = project["tracks"][0]["id"].get<std::string>();
    daw.call("set_track_input",
             {{"track", t0}, {"audioInput", project["inputs"]["audio"][0]}, {"monitor", "in"}});
    const auto tuner = daw.call("add_module", {{"track", t0}, {"type", "tuner"}})["id"].get<std::string>();
    pump(1.0);

    auto future = std::async(
        std::launch::async,
        [&]
        {
            return daw.server->dispatcher()->handle(
                modernRequest("tools/call", {{"name", "measure_module"},
                                             {"arguments", {{"module", tuner}, {"seconds", 0.2}}}}),
                {});
        });
    while (future.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready)
        pump(0.01);
    const auto out = future.get();
    REQUIRE(out.message);
    const auto result = (*out.message)["result"];
    INFO(result.dump());
    REQUIRE(result["isError"] == false);
    std::map<std::string, Json> byId;
    for (const auto& m : result["structuredContent"]["meters"])
        byId[m["id"].get<std::string>()] = m;
    CHECK(byId["note"]["value"] == "A2");
    CHECK(byId["cents"]["unit"] == "cents");
    CHECK(byId["cents"]["value"].get<double>() == Approx(0.0).margin(0.2));
    CHECK(byId["frequency"]["unit"] == "Hz");
    CHECK(byId["frequency"]["value"].get<double>() == Approx(220.0).margin(0.05));
    CHECK(byId["clarity"]["value"].get<double>() > 0.9);
}

TEST_CASE("mcp tools: the mixer: levels, returns, sends and meters", "[integration][mcp]")
{
    Daw daw;
    auto project = daw.project();
    const auto t0 = project["tracks"][0]["id"].get<std::string>();
    const auto master = project["master"]["id"].get<std::string>();
    CHECK(project["master"]["kind"] == "master");
    CHECK(project["tracks"][0]["kind"] == "track");
    CHECK(project["tracks"][0]["mix"]["volume"] == Approx(0.0));
    CHECK(project["returns"].empty());

    const auto added = daw.call("add_return_track");
    const auto r = added["track"].get<std::string>();
    CHECK(added["name"] == "A Return");
    CHECK(daw.session->undoDescription() == "Assistant: add return track");
    project = daw.project();
    REQUIRE(project["returns"].size() == 1);
    CHECK(project["returns"][0]["kind"] == "return");
    CHECK(project["tracks"][0]["mix"]["sends"][0]["return"] == r);
    CHECK(project["tracks"][0]["mix"]["sends"][0]["level"].is_null()); // off

    const auto mixed = daw.call("set_mix", {{"track", t0}, {"volume", "-6 dB"}, {"pan", "25L"}, {"mute", true}});
    CHECK(mixed["mix"]["volume"] == Approx(-6.0));
    CHECK(mixed["mix"]["pan"] == Approx(-0.5));
    CHECK(mixed["mix"]["panText"] == "25L");
    CHECK(mixed["mix"]["mute"] == true);
    CHECK(daw.session->undoDescription() == "Assistant: set mix");
    daw.call("undo");
    CHECK(daw.project()["tracks"][0]["mix"]["volume"] == Approx(0.0)); // the three in one step

    const auto steps = daw.session->undoDescription();
    CHECK(daw.call("set_mix", {{"track", t0}, {"solo", true}})["mix"]["solo"] == true);
    CHECK(daw.session->undoDescription() == steps); // solo is never an undo step
    CHECK(daw.fails("set_mix", {{"track", master}, {"mute", true}}).find("no mute") != std::string::npos);
    CHECK(daw.call("set_mix", {{"track", master}, {"volume", -2}})["mix"]["volume"] == Approx(-2.0));
    CHECK(daw.call("set_mix", {{"track", r}, {"volume", "-inf"}})["mix"]["volume"].is_null());
    CHECK(daw.fails("set_mix", {{"track", t0}, {"pan", "left"}}).find("pan") != std::string::npos);

    const auto send = daw.call("set_send", {{"track", t0}, {"return", r}, {"level", -12}, {"preFader", true}});
    CHECK(send["level"] == Approx(-12.0));
    CHECK(send["preFader"] == true);
    CHECK(daw.session->undoDescription() == "Assistant: set send");
    CHECK(daw.fails("set_send", {{"track", r}, {"return", r}}).find("Only tracks and groups send") !=
          std::string::npos);
    CHECK(daw.fails("set_send", {{"track", t0}, {"return", t0}}).find("isn't a return") != std::string::npos);

    // Modules on the master and on returns are found like any other.
    const auto limiter = daw.call("add_module", {{"track", master}, {"type", "limiter"}})["id"];
    CHECK(daw.call("get_module", {{"module", limiter}})["type"] == "limiter");
    CHECK(daw.project()["master"]["chain"][0]["id"] == limiter);

    const auto measured = daw.callAway("measure_mix", {{"seconds", 0.2}});
    REQUIRE(measured["isError"] == false);
    const auto& channels = measured["structuredContent"]["channels"];
    CHECK(channels.size() == daw.project()["tracks"].size() + 2); // the tracks, the return, the master
    CHECK(channels.back()["kind"] == "master");
    CHECK(channels[0]["peak"].is_null()); // nothing plays here
    CHECK(measured["structuredContent"]["playing"] == false);
}

TEST_CASE("mcp tools: resources and their change notifications", "[integration][mcp]")
{
    Daw daw;
    const auto project = daw.read("segno://project");
    const auto t0 = project["tracks"][0]["id"].get<std::string>();
    CHECK(daw.read("segno://track/" + t0)["id"] == t0);
    const auto clipId = daw.call("create_midi_clip", {{"track", t0}, {"start", 0}})["clip"]["id"].get<std::string>();
    CHECK(daw.read("segno://clip/" + clipId)["notes"].is_array());
    CHECK(daw.read("segno://modules")["types"].size() >= 5);
    CHECK(daw.read("segno://module-type/eq")["parameters"].size() > 3);
    const auto missing = daw.server->dispatcher()->handle(modernRequest("resources/read", {{"uri", "segno://clip/c:999999"}}), {});
    CHECK((*missing.message)["error"]["code"] == fd::mcp::codes::invalidParams);

    // A subscription hears about the clip it watches.
    struct Stream final : fd::mcp::Stream
    {
        std::mutex m;
        std::vector<Json> sent;
        std::atomic<bool> open{true};
        bool send(const Json& message) override
        {
            std::lock_guard lock(m);
            sent.push_back(message);
            return open;
        }
        bool keepAlive() override { return open; }
        bool isOpen() const override { return open; }
        size_t count()
        {
            std::lock_guard lock(m);
            return sent.size();
        }
    } stream;
    auto listening = std::async(std::launch::async, [&] {
        return daw.server->dispatcher()->handle(
            modernRequest("subscriptions/listen", {{"notifications", {{"resourceSubscriptions", {"segno://clip/" + clipId}}}}}),
            {}, &stream);
    });
    const auto pump = [&](auto until) {
        for (int i = 0; i < 300 && !until(); ++i)
            juce::MessageManager::getInstance()->runDispatchLoopUntil(10);
    };
    pump([&] { return stream.count() >= 1; });
    juce::MessageManager::getInstance()->runDispatchLoopUntil(50); // the baseline is taken
    daw.call("add_notes", {{"clip", clipId}, {"notes", {{{"pitch", 60}, {"start", 0}, {"length", 1}}}}});
    pump([&] { return stream.count() >= 2; });
    REQUIRE(stream.count() >= 2);
    CHECK(stream.sent[1]["method"] == "notifications/resources/updated");
    CHECK(stream.sent[1]["params"]["uri"] == "segno://clip/" + clipId);
    stream.open = false;
    daw.server->dispatcher()->resourceUpdated("segno://clip/" + clipId);
    listening.get();
}

TEST_CASE("mcp server: HTTP end to end, token file and endpoint file", "[integration][mcp]")
{
    Daw daw;
    const auto endpoint = Json::parse(fd::mcp::McpServer::endpointFile(daw.folder).loadFileAsString().toStdString());
    CHECK(endpoint["port"] == daw.server->port());
    CHECK(endpoint["token"] == daw.server->token());
    CHECK(daw.server->token().size() == 64);
    CHECK(daw.server->httpSetupLine().find(daw.server->token()) != std::string::npos);
    CHECK(Json::parse(daw.server->stdioSetupLine())["mcpServers"]["segno"]["args"][0] == "--mcp-stdio");

    auto request = modernRequest("tools/call", {{"name", "add_track"}, {"arguments", {{"name", "Over HTTP"}}}});
    auto response = std::async(std::launch::async, [&] {
        httplib::Client client("127.0.0.1", daw.server->port());
        return client.Post("/mcp",
                           {{"Authorization", "Bearer " + daw.server->token()},
                            {"MCP-Protocol-Version", fd::mcp::modernVersion},
                            {"Mcp-Method", "tools/call"},
                            {"Mcp-Name", "add_track"}},
                           request.dump(), "application/json");
    });
    while (response.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready)
        juce::MessageManager::getInstance()->runDispatchLoopUntil(10);
    const auto res = response.get();
    REQUIRE(res);
    CHECK(res->status == 200);
    CHECK(Json::parse(res->body)["result"]["isError"] == false);
    CHECK(daw.project()["tracks"].back()["name"] == "Over HTTP");
    CHECK(daw.server->isActive());
    REQUIRE_FALSE(daw.server->clients().empty());
    CHECK(daw.server->clients()[0].name == "mcp-tool-tests");

    // A new token: the old one stops working.
    const auto old = daw.server->token();
    daw.server->regenerateToken();
    CHECK(daw.server->token() != old);
    httplib::Client client("127.0.0.1", daw.server->port());
    CHECK(client.Post("/mcp", {{"Authorization", "Bearer " + old}}, request.dump(), "application/json")->status == 401);

    // Off: the endpoint file goes, and on again it comes back.
    daw.server->setEnabled(false);
    CHECK_FALSE(daw.server->isRunning());
    CHECK_FALSE(fd::mcp::McpServer::endpointFile(daw.folder).existsAsFile());
    REQUIRE(daw.server->start(0));
    CHECK(fd::mcp::McpServer::endpointFile(daw.folder).existsAsFile());
}

TEST_CASE("mcp tools: parameters say what they do (B22)", "[integration][mcp]")
{
    Daw daw;
    const auto reference = daw.read("segno://module-type/utility");
    for (const auto& p : reference["parameters"])
    {
        INFO(p.dump());
        CHECK_FALSE(p["description"].get<std::string>().empty());
    }
    const auto t0 = daw.project()["tracks"][0]["id"].get<std::string>();
    const auto eq = daw.call("add_module", {{"track", t0}, {"type", "eq"}});
    CHECK(eq["parameters"][0]["description"] == "Turns the band on or off.");
}

TEST_CASE("mcp tools: automation: lanes written, read, cleared and turned back on", "[integration][mcp]")
{
    Daw daw;
    auto project = daw.project();
    const auto t0 = project["tracks"][0]["id"].get<std::string>();
    const auto eq = daw.call("add_module", {{"track", t0}, {"type", "eq"}})["id"].get<std::string>();

    // A module's parameter, by id, with values as text and numbers, and a bend: it gets a lane.
    auto lane = daw.call("set_automation", {{"module", eq},
                                            {"parameter", "band1.freq"},
                                            {"points", Json::array({{{"position", "1.1.1"}, {"value", "200 Hz"}, {"bend", 0.5}},
                                                                    {{"position", 8}, {"value", 2000}}})}});
    CHECK(daw.session->undoDescription() == "Assistant: set automation");
    REQUIRE(lane["points"].size() == 2);
    CHECK(lane["points"][0]["value"] == Approx(200.0).margin(0.5));
    CHECK(lane["points"][0]["bend"] == Approx(0.5));
    CHECK(lane["points"][1]["position"]["beats"] == Approx(8.0));
    CHECK(lane["points"][1]["text"] == "2.00 kHz");
    CHECK(lane["on"] == true);
    const auto freqLane = lane["lane"].get<std::string>();
    CHECK(freqLane.rfind("a:", 0) == 0);
    CHECK(lane["after"] == project["tracks"].back()["id"]); // like a new track: at the end

    // It shows in the project and the module.
    project = daw.project();
    REQUIRE(project["automationLanes"].size() == 1);
    CHECK(project["automationLanes"][0]["lane"] == freqLane);
    CHECK(project["automationLanes"][0]["points"] == 2);
    CHECK(project["automationLanes"][0]["assignments"][0]["parameter"] == "band1.freq");
    CHECK(project["automationLanes"][0]["assignments"][0]["module"] == eq);
    const auto module = daw.call("get_module", {{"module", eq}});
    bool marked = false;
    for (const auto& p : module["parameters"])
        if (p["id"] == "band1.freq")
            marked = p.contains("automation") && p["automation"]["points"] == 2 && p["automation"]["lane"] == freqLane;
    CHECK(marked);

    // A channel's volume: a range replaces just its part.
    daw.call("set_automation", {{"track", t0}, {"parameter", "volume"},
                                {"points", Json::array({{{"position", 0}, {"value", "0 dB"}}, {{"position", 16}, {"value", -12}}})}});
    daw.call("set_automation", {{"track", t0}, {"parameter", "volume"}, {"from", 4}, {"to", 12},
                                {"points", Json::array({{{"position", 8}, {"value", "-inf"}}})}});
    auto volume = daw.call("get_automation", {{"track", t0}, {"parameter", "volume"}});
    REQUIRE(volume["points"].size() == 3);
    CHECK(volume["points"][1]["text"] == "-inf dB");
    CHECK(volume["points"][2]["value"] == Approx(-12.0).margin(0.05));
    CHECK(daw.call("get_automation", {{"track", t0}, {"parameter", "volume"}, {"from", 6}, {"to", 10}})["points"].size() == 1);
    const auto volumeLane = volume["lane"].get<std::string>();

    // Moving the fader turns its assignment off; set_automation_on turns it back on.
    daw.call("set_mix", {{"track", t0}, {"volume", -3}});
    CHECK(daw.call("get_automation", {{"lane", volumeLane}})["on"] == false);
    const auto steps = daw.session->undoDescription();
    CHECK(daw.call("set_automation_on", {{"all", true}})["on"] == true);
    CHECK(daw.call("get_automation", {{"track", t0}, {"parameter", "volume"}})["on"] == true);
    CHECK(daw.session->undoDescription() == steps); // not an undo step

    // Clearing; refusing what can't be automated.
    CHECK(daw.call("clear_automation", {{"track", t0}, {"parameter", "volume"}})["points"].empty());
    CHECK(daw.session->undoDescription() == "Assistant: clear automation");
    const auto comp = daw.call("add_module", {{"track", t0}, {"type", "compressor"}})["id"].get<std::string>();
    CHECK(daw.fails("set_automation", {{"module", comp}, {"parameter", "lookahead"}, {"points", Json::array()}})
              .find("can't be automated") != std::string::npos);
    CHECK(daw.fails("get_automation", {{"track", t0}, {"parameter", "send"}}).find("return") != std::string::npos);
}

TEST_CASE("mcp tools: automation lanes of their own: added, assigned with ranges, renamed and removed",
          "[integration][mcp]")
{
    Daw daw;
    auto project = daw.project();
    const auto t0 = project["tracks"][0]["id"].get<std::string>();
    const auto t1 = project["tracks"][1]["id"].get<std::string>();

    // A lane driving two tracks' volumes, one over part of its range and inverted: one step.
    auto lane = daw.call("add_automation_lane",
                         {{"after", t0},
                          {"name", "Swell"},
                          {"parameters", Json::array({{{"track", t0}, {"parameter", "volume"}},
                                                      {{"track", t1}, {"parameter", "volume"}, {"from", "0 dB"}, {"to", "-24 dB"}}})}});
    CHECK(daw.session->undoDescription() == "Assistant: add automation lane");
    const auto id = lane["lane"].get<std::string>();
    CHECK(lane["name"] == "Swell");
    REQUIRE(lane["assignments"].size() == 2);
    CHECK(lane["assignments"][1]["from"]["value"] == Approx(0.0).margin(0.01));
    CHECK(lane["assignments"][1]["to"]["text"] == "-24.0 dB");

    // By lane, values are 0 to 1 (it drives several); by a parameter, its own units in its range.
    daw.call("set_automation", {{"lane", id}, {"points", Json::array({{{"position", 0}, {"value", 0.0}}, {{"position", 8}, {"value", 1.0}}})}});
    auto read = daw.call("get_automation", {{"track", t1}, {"parameter", "volume"}});
    REQUIRE(read["points"].size() == 2);
    CHECK(read["points"][0]["text"] == "0.0 dB"); // inverted: the lane's bottom is its top
    CHECK(read["points"][1]["value"] == Approx(-24.0).margin(0.05));
    CHECK(daw.fails("set_automation", {{"track", t1}, {"parameter", "volume"},
                                       {"points", Json::array({{{"position", 0}, {"value", "+3 dB"}}})}})
              .find("outside") != std::string::npos);
    CHECK(daw.fails("set_automation", {{"lane", id}, {"points", Json::array({{{"position", 0}, {"value", 5}}})}})
              .find("0 to 1") != std::string::npos);
    // Clearing by a parameter of a shared lane is refused.
    CHECK(daw.fails("clear_automation", {{"track", t0}, {"parameter", "volume"}}).find("shares") != std::string::npos);

    // Assigning moves a parameter between lanes; unassigning takes it off.
    const auto added = daw.call("add_automation_lane", {});
    CHECK(added["name"] == "Automation 1"); // numbered when made: none else has a number
    const auto other = added["lane"].get<std::string>();
    auto moved = daw.call("assign_automation", {{"lane", other}, {"track", t1}, {"parameter", "volume"}});
    CHECK(moved["movedFrom"] == id);
    CHECK(daw.call("get_automation", {{"lane", id}})["assignments"].size() == 1);
    CHECK(daw.call("unassign_automation", {{"track", t1}, {"parameter", "volume"}})["lane"] == other);
    CHECK(daw.call("get_automation", {{"track", t1}, {"parameter", "volume"}})["lane"].is_null());

    // Renaming and removing; the project lists what's left, in row order.
    CHECK(daw.fails("set_automation_lane", {{"lane", other}, {"name", " "}}).find("can't be empty") != std::string::npos);
    CHECK(daw.call("set_automation_lane", {{"lane", other}, {"name", "Spare"}})["name"] == "Spare");
    project = daw.project();
    REQUIRE(project["automationLanes"].size() == 2);
    CHECK(project["automationLanes"][0]["lane"] == id); // after the first track

    // Moving a lane: right above a track or a lane, or to the end; nothing else moves (ADR 0042).
    auto rows = daw.call("move_automation_lane", {{"lane", other}, {"before", t0}})["rows"];
    CHECK(rows[0] == other);
    CHECK(rows[1] == t0);
    CHECK(daw.session->undoDescription() == "Assistant: move automation lane");
    rows = daw.call("move_automation_lane", {{"lane", other}, {"before", id}})["rows"];
    CHECK(rows[1] == other);
    CHECK(rows[2] == id);
    rows = daw.call("move_automation_lane", {{"lane", other}})["rows"];
    CHECK(rows.back() == other);
    CHECK(daw.fails("move_automation_lane", {{"lane", t0}}).find("isn't a lane id") != std::string::npos);
    CHECK(daw.call("set_automation_on", {{"lane", id}, {"on", false}})["on"] == false);
    daw.call("remove_automation_lane", {{"lane", id}});
    CHECK(daw.session->undoDescription() == "Assistant: remove automation lane");
    CHECK(daw.project()["automationLanes"].size() == 1);
    CHECK(daw.fails("get_automation", {{"lane", id}}).find("isn't an automation lane") != std::string::npos);
}

TEST_CASE("mcp tools: recording: inputs set, tracks armed, a few bars recorded", "[integration][mcp]")
{
    Daw daw;
    daw.session->setUnsavedRoot(daw.folder.getChildFile("Unsaved"));
    tracktion::HostedAudioDeviceInterface::Parameters io;
    io.sampleRate = 48000.0;
    io.blockSize = 480;
    io.inputChannels = 2;
    io.outputChannels = 2;
    tracktion::test_utilities::EnginePlayer player(daw.engine, io);
    juce::AudioBuffer<float> in(2, 480);
    std::int64_t clock = 0;
    auto pump = [&](double seconds) {
        for (int b = 0; b < static_cast<int>(seconds * 100.0); ++b)
        {
            for (int c = 0; c < 2; ++c)
                for (int s = 0; s < 480; ++s)
                    in.setSample(c, s, 0.4f * std::sin(static_cast<float>(clock + s) * 0.03f));
            clock += 480;
            player.process(in);
            juce::MessageManager::getInstance()->runDispatchLoopUntil(1);
        }
    };
    pump(0.1);
    auto project = daw.project();
    REQUIRE_FALSE(project["inputs"]["audio"].empty());
    CHECK(project["inputs"]["midi"][0]["id"] == "all");
    const auto t0 = project["tracks"][0]["id"].get<std::string>();
    CHECK(project["tracks"][0]["input"]["midi"] == "all");
    CHECK(project["tracks"][0]["input"]["armed"] == false);

    const auto input = project["inputs"]["audio"][0].get<std::string>();
    auto set = daw.call("set_track_input", {{"track", t0}, {"audioInput", input}, {"monitor", "off"}, {"records", "audio"}});
    CHECK(set["input"]["audio"] == input);
    CHECK(set["input"]["monitor"] == "off");
    CHECK(daw.fails("set_track_input", {{"track", t0}, {"audioInput", "Nowhere"}}).find("no audio input") != std::string::npos);
    CHECK(daw.call("arm_tracks", {{"tracks", Json::array({t0})}, {"exclusive", true}})["armed"].size() == 1);

    // Two bars at 120 BPM: four seconds, then it stops by itself.
    const auto started = daw.call("record", {{"from", "1.1.1"}, {"bars", 2}});
    CHECK(started["recording"] == true);
    CHECK(started["tracks"] == Json::array({t0}));
    pump(4.5);
    juce::MessageManager::getInstance()->runDispatchLoopUntil(100);
    CHECK_FALSE(daw.session->isRecording());
    project = daw.project();
    REQUIRE(project["tracks"][0]["clips"].size() == 1);
    CHECK(project["tracks"][0]["clips"][0]["length"] == Approx(8.0).margin(0.25)); // beats
    CHECK(daw.session->undoDescription() == "Record"); // the pass: one undo step

    // A loop pass: a take per lap (ADR 0031), then the take tools.
    daw.call("set_loop", {{"start", "9.1.1"}, {"end", "10.1.1"}, {"on", true}});
    daw.call("record", {{"from", "9.1.1"}, {"bars", 3}});
    pump(6.5);
    juce::MessageManager::getInstance()->runDispatchLoopUntil(100);
    daw.call("set_loop", {{"start", "9.1.1"}, {"end", "10.1.1"}, {"on", false}});
    auto takes = daw.call("get_takes", {{"track", t0}});
    REQUIRE(takes["takes"].size() >= 4); // the first pass, and three laps
    REQUIRE(takes["clips"].size() == 2);  // two clips, each with its takes (ADR 0032)
    CHECK(takes["clips"][1]["takes"].size() >= 3);
    CHECK(takes["clips"][1]["playing"] == takes["clips"][1]["takes"].back()["id"]);
    const auto looped = takes["clips"][1]["clip"].get<std::string>();
    CHECK(daw.call("get_clip", {{"clip", looped}})["takes"] >= 3);
    CHECK(daw.call("get_takes", {{"clip", looped}})["clips"].size() == 1);
    CHECK(daw.fails("get_takes", Json::object()).find("track or a clip") != std::string::npos);
    const auto lap1 = takes["takes"][1]["id"].get<std::string>();
    CHECK(takes["takes"].back()["playing"] == true);
    takes = daw.call("use_take", {{"track", t0}, {"take", lap1}});
    CHECK(takes["takes"][1]["playing"] == true);
    CHECK(daw.session->undoDescription() == "Assistant: use take");
    CHECK(daw.call("set_take", {{"track", t0}, {"take", lap1}, {"name", "Best"}})["takes"][1]["name"] == "Best");
    const auto tracks = daw.call("move_takes_to_tracks", {{"track", t0}, {"takes", Json::array({lap1})}})["tracks"];
    REQUIRE(tracks.size() == 1);
    CHECK(daw.project()["tracks"][1]["id"] == tracks[0]);
    CHECK(daw.fails("use_take", {{"track", t0}, {"take", "k:999999"}}).find("isn't one of") != std::string::npos);
    const auto left = daw.call("delete_takes", {{"track", t0}, {"unused", true}})["takes"];
    for (const auto& take : left)
        CHECK(take["playing"] == true);
    daw.call("undo");
    const auto kept = daw.call("delete_takes", {{"track", t0}, {"keep", lap1}});
    REQUIRE(kept["clips"].size() == 2);
    CHECK(kept["clips"][1]["takes"].size() == 1);
    CHECK(kept["clips"][1]["playing"] == lap1);

    // A pass over the end of the first clip makes its own clip (ADR 0037); its "Before take" plays
    // what was there, alone, and its own take comes back.
    daw.call("record", {{"from", "2.1.1"}, {"bars", 1}});
    pump(2.5);
    juce::MessageManager::getInstance()->runDispatchLoopUntil(100);
    takes = daw.call("get_takes", {{"track", t0}});
    REQUIRE(takes["clips"].size() == 3);
    const auto punch = takes["clips"][1];
    REQUIRE(punch["takes"].size() == 2);
    auto nameOf = [&](const Json& id) {
        for (const auto& take : takes["takes"])
            if (take["id"] == id)
                return take["name"].get<std::string>();
        return std::string();
    };
    CHECK(nameOf(punch["takes"][0]["id"]).starts_with("Before take"));
    CHECK(punch["playing"] == punch["takes"][1]["id"]);
    for (const auto& k : {punch["takes"][0]["id"], punch["takes"][1]["id"], punch["takes"][0]["id"]})
    {
        const auto used = daw.call("use_take", {{"track", t0}, {"take", k}});
        REQUIRE(used["clips"].size() == 3);
        CHECK(used["clips"][1]["playing"] == k);
    }
    // Comping a stretch of its take back in (bar 2, beat 3 to the end): a comp, one undo step.
    const auto comped = daw.call("use_take", {{"track", t0}, {"take", punch["takes"][1]["id"]}, {"from", "2.3.1"}, {"to", "3.1.1"}});
    CHECK(comped["clips"][1]["playing"] == "comp");
    CHECK(daw.session->undoDescription() == "Assistant: use take");
    CHECK(daw.fails("use_take", {{"track", t0}, {"take", punch["takes"][1]["id"]}, {"from", "2.3.1"}}).find("both") !=
          std::string::npos);
    CHECK(daw.fails("use_take", {{"track", t0}, {"take", punch["takes"][1]["id"]}, {"from", "20.1.1"}, {"to", "21.1.1"}})
              .find("no audio there") != std::string::npos);
    // Auditioning a take: heard in place of its clip, nothing changed, never an undo step.
    const auto heard = daw.call("audition_take", {{"track", t0}, {"take", punch["takes"][0]["id"]}});
    CHECK(heard["auditioning"] == punch["takes"][0]["id"]);
    CHECK(daw.call("get_takes", {{"track", t0}})["clips"][1]["playing"] == "comp");
    CHECK(daw.session->undoDescription() == "Assistant: use take");
    CHECK(daw.call("audition_take", {{"track", t0}, {"stop", true}})["auditioning"].is_null());
    CHECK(daw.fails("audition_take", {{"track", t0}}).find("stop") != std::string::npos);

    // Capture MIDI: nothing played yet; then a phrase onto the track asked for (none armed).
    CHECK(daw.call("capture_midi")["clip"].is_null());
    daw.call("arm_tracks", {{"tracks", Json::array({t0})}, {"on", false}});
    daw.session->computerKeyboardNote(60, 0.8f);
    pump(0.2);
    daw.session->computerKeyboardNote(60, 0.0f);
    pump(0.1);
    const auto t1 = daw.project()["tracks"][1]["id"].get<std::string>();
    const auto captured = daw.call("capture_midi", {{"track", t1}});
    CHECK(captured["track"] == t1);
    CHECK_FALSE(captured["clip"].is_null());
}

TEST_CASE("mcp tools: bounce clips in place and to a new track, and mute clips", "[integration][mcp]")
{
    Daw daw;
    daw.session->setUnsavedRoot(daw.folder.getChildFile("Unsaved"));
    const auto tracks = daw.project()["tracks"];
    const auto t0 = tracks[0]["id"].get<std::string>(), t1 = tracks[1]["id"].get<std::string>();
    daw.call("add_module", {{"track", t0}, {"type", "subtractive-synth"}});
    const Json notes = Json::array({{{"pitch", "C3"}, {"start", 0}, {"length", 1}}, {{"pitch", "E3"}, {"start", 2}, {"length", 1}}});
    const auto clip = daw.call("create_midi_clip", {{"track", t0}, {"start", 0}, {"notes", notes}})["clip"]["id"].get<std::string>();
    const auto silent = daw.call("create_midi_clip", {{"track", t1}, {"start", 0}, {"notes", notes}})["clip"]["id"].get<std::string>();

    // Refused, with the reason: no instrument on the second track.
    CHECK(daw.fails("bounce_clips", {{"clips", {silent}}}).find("instrument") != std::string::npos);

    // In place: an audio clip with the MIDI kept as its take.
    auto r = daw.callAway("bounce_clips", {{"clips", {clip}}});
    INFO(r.dump().substr(0, 2000));
    REQUIRE(r["isError"] == false);
    auto out = r["structuredContent"];
    REQUIRE(out["clips"].size() == 1);
    const auto bounced = out["clips"][0]["id"].get<std::string>();
    CHECK(out["clips"][0]["kind"] == "audio");
    CHECK(out["clips"][0]["track"] == t0);
    CHECK(out["clips"][0]["takes"] == 2);
    CHECK(out["clips"][0]["muted"] == false);
    CHECK(out["tracks"].empty());
    CHECK(daw.session->undoDescription() == "Assistant: bounce clips");
    const auto takes = daw.call("get_takes", {{"clip", bounced}});
    REQUIRE(takes["clips"].size() == 1);
    REQUIRE(takes["clips"][0]["takes"].size() == 2);
    CHECK(takes["clips"][0]["takes"][0]["name"] == "Before bounce");
    CHECK(takes["clips"][0]["takes"][1]["name"] == "Bounce");

    // Muted, it can't be bounced; unmuted, to a new track.
    auto muted = daw.call("mute_clips", {{"clips", {bounced}}});
    CHECK(muted["clips"][0]["muted"] == true);
    CHECK(daw.session->undoDescription() == "Assistant: mute clips");
    CHECK(daw.fails("bounce_clips", {{"clips", {bounced}}, {"newTrack", true}}).find("muted") != std::string::npos);
    muted = daw.call("mute_clips", {{"clips", {bounced}}, {"muted", false}});
    CHECK(muted["clips"][0]["muted"] == false);
    r = daw.callAway("bounce_clips", {{"clips", {bounced}}, {"newTrack", true}});
    INFO(r.dump().substr(0, 2000));
    REQUIRE(r["isError"] == false);
    out = r["structuredContent"];
    REQUIRE(out["tracks"].size() == 1);
    REQUIRE(out["clips"].size() == 1);
    CHECK(out["clips"][0]["track"] == out["tracks"][0]);
    CHECK(daw.call("get_clip", {{"clip", bounced}})["muted"] == true); // the source, muted
    const auto project = daw.project();
    CHECK(project["tracks"][1]["id"] == out["tracks"][0]); // right after the source

    // A stretch (time-selection.md §18): what plays in it on the track, one clip; a clip crossing its
    // end cut there.
    daw.call("create_midi_clip", {{"track", t0}, {"start", 16}, {"length", 4}, {"notes", notes}});
    r = daw.callAway("bounce_clips", {{"from", 16}, {"to", 18}, {"tracks", {t0}}});
    INFO(r.dump().substr(0, 2000));
    REQUIRE(r["isError"] == false);
    out = r["structuredContent"];
    REQUIRE(out["clips"].size() == 1);
    CHECK(out["clips"][0]["kind"] == "audio");
    CHECK(out["clips"][0]["takes"] == 2);
    CHECK(daw.session->undoDescription() == "Assistant: bounce clips");
    CHECK(daw.fails("bounce_clips", {{"from", 40}, {"to", 42}, {"tracks", {t0}}}).find("No clip") !=
          std::string::npos);
    CHECK(daw.fails("bounce_clips", {{"clips", {bounced}}, {"from", 16}}).find("or a stretch") !=
          std::string::npos);
    CHECK(daw.fails("bounce_clips", {{"from", 16}, {"to", 18}}).find("tracks") != std::string::npos);
}

TEST_CASE("mcp tools: fades, crossfades and edge fades", "[integration][mcp]")
{
    Daw daw;
    const auto t0 = daw.project()["tracks"][0]["id"].get<std::string>();
    // A 4 s sine in two files: the same audio, so a crossfade between them can be heard as one.
    fd::testkit::AudioBuffer sine(2, 4 * 48000);
    for (int c = 0; c < 2; ++c)
        for (int i = 0; i < sine.numSamples(); ++i)
            sine.at(c, i) = 0.5f * std::sin(static_cast<float>(i) * 0.05f);
    const auto a = daw.folder.getChildFile("a.wav"), b = daw.folder.getChildFile("b.wav");
    REQUIRE(fd::testkit::writeWav(a.getFullPathName().toStdString(), sine, 48000.0));
    REQUIRE(fd::testkit::writeWav(b.getFullPathName().toStdString(), sine, 48000.0));
    const auto first = daw.call("insert_audio_file", {{"track", t0}, {"start", 0}, {"path", a.getFullPathName().toStdString()}})["clip"]["id"]
                           .get<std::string>();
    CHECK(daw.call("get_clip", {{"clip", first}})["fadeIn"]["ms"] == 0.0);
    CHECK(daw.project()["edgeFades"] == true);

    // Fades: lengths in ms or text, bends.
    auto r = daw.call("set_fades", {{"clips", {first}}, {"in", 120}, {"out", "0.5 s"}, {"outBend", 0.4}});
    CHECK(r["clips"][0]["fadeIn"]["ms"] == 120.0);
    CHECK(r["clips"][0]["fadeOut"]["ms"] == 500.0);
    CHECK(r["clips"][0]["fadeOut"]["bend"] == Approx(0.4));
    CHECK(daw.session->undoDescription() == "Assistant: set fades");

    // A split is seamless: nothing to crossfade.
    const auto halves = daw.call("split_clips", {{"clips", {first}}, {"at", "2.1.1"}})["clips"];
    REQUIRE(halves.size() == 1);
    const auto second = halves[0]["id"].get<std::string>();
    r = daw.call("crossfade", {{"clips", {first, second}}});
    REQUIRE(r["joins"].size() == 1);
    CHECK(r["joins"][0].contains("why"));
    // The second half replaced by the copy from its start: it has no audio before it, so the
    // crossfade sits after the join, in the first half's spare audio.
    daw.call("delete_clips", {{"clips", {second}}});
    const auto copy = daw.call("insert_audio_file", {{"track", t0}, {"start", "2.1.1"}, {"path", b.getFullPathName().toStdString()}})["clip"]["id"]
                          .get<std::string>();
    r = daw.call("crossfade", {{"clips", {first, copy}}, {"length", "20 ms"}, {"shape", "equal gain"}});
    REQUIRE(r["joins"].size() == 1);
    REQUIRE(r["joins"][0].contains("crossfade"));
    CHECK(r["joins"][0]["crossfade"]["ms"] == 20.0);
    CHECK(r["joins"][0]["crossfade"]["startMs"] == 0.0);
    CHECK(r["joins"][0]["crossfade"]["placement"] == "after"); // the only place it fits
    CHECK(r["joins"][0]["crossfade"]["shape"] == "Equal gain");
    // Asked to sit before the join: there's no audio before the copy's start, so it stays after and says so.
    r = daw.call("crossfade", {{"clips", {first, copy}}, {"placement", "before"}});
    CHECK(r["joins"][0]["crossfade"]["placement"] == "after");
    CHECK(daw.callResult("crossfade", {{"clips", {first, copy}}, {"placement", "before"}})["content"][0]["text"].get<std::string>().find(
              "spare audio") != std::string::npos);
    CHECK(daw.fails("crossfade", {{"clips", {first, copy}}, {"placement", "sideways"}}).find("before") != std::string::npos);
    CHECK(daw.call("get_clip", {{"clip", first}})["crossfadeOut"]["ms"] == 20.0);
    CHECK(daw.fails("crossfade", {{"clips", {first}}, {"shape", "wobbly"}}).find("equal power") != std::string::npos);
    r = daw.call("crossfade", {{"clips", {first, copy}}, {"remove", true}});
    CHECK(r["joins"].size() == 1);
    CHECK_FALSE(daw.call("get_clip", {{"clip", first}}).contains("crossfadeOut"));

    // Edge fades.
    CHECK(daw.call("set_edge_fades", {{"on", false}})["on"] == false);
    CHECK(daw.project()["edgeFades"] == false);
    CHECK(daw.session->undoDescription() == "Assistant: set edge fades");
}


TEST_CASE("mcp tools: export audio with the project's settings, only where the user chose", "[integration][mcp]")
{
    Daw daw;
    daw.session->setUnsavedRoot(daw.folder.getChildFile("Unsaved"));
    const auto t0 = daw.project()["tracks"][0]["id"].get<std::string>();
    daw.call("add_module", {{"track", t0}, {"type", "subtractive-synth"}});
    const Json notes = Json::array({{{"pitch", "C3"}, {"start", 0}, {"length", 2}}});
    daw.call("create_midi_clip", {{"track", t0}, {"start", 0}, {"notes", notes}});

    // With the project's settings (the panel's): the mix into the project's Exports, with its report.
    auto r = daw.callAway("export_audio", Json::object());
    INFO(r.dump().substr(0, 2000));
    REQUIRE(r["isError"] == false);
    auto files = r["structuredContent"]["files"];
    REQUIRE(files.size() == 1);
    const juce::File mix(juce::String(files[0]["file"].get<std::string>()));
    CHECK(mix.existsAsFile());
    CHECK(mix.getParentDirectory() == daw.session->exportsFolder());
    CHECK(files[0]["track"].is_null());
    CHECK(files[0]["loudness"]["integrated"].is_number());
    CHECK(files[0]["seconds"].get<double>() > 1.0);

    // Stems, normalised to a loudness, as FLAC; read-only mode allows it (the project stays as it is).
    daw.server->setReadOnly(true);
    r = daw.callAway("export_audio", {{"what", "tracks"}, {"normalise", "loudness"}, {"target", -18}, {"format", "flac"}});
    daw.server->setReadOnly(false);
    REQUIRE(r["isError"] == false);
    files = r["structuredContent"]["files"];
    REQUIRE(files.size() == daw.project()["tracks"].size());
    CHECK(files[0]["track"].is_string());
    CHECK(juce::String(files[0]["file"].get<std::string>()).endsWith(".flac"));
    CHECK(files[0]["loudness"]["integrated"].get<double>() == Catch::Approx(-18.0).margin(0.3));

    // Files go where the user chose in the panel; an assistant can name a file, never a path.
    const auto chosen = daw.folder.getChildFile("Chosen");
    auto settings = daw.session->exportSettings();
    settings.folder = chosen.getFullPathName().toStdString();
    daw.session->setExportSettings(settings);
    r = daw.callAway("export_audio", {{"name", "Demo"}});
    REQUIRE(r["isError"] == false);
    CHECK(juce::File(juce::String(r["structuredContent"]["files"][0]["file"].get<std::string>())) == chosen.getChildFile("Demo.wav"));
    r = daw.callAway("export_audio", {{"name", "../Demo"}});
    REQUIRE(r["isError"] == true);
    CHECK(r["content"][0]["text"].get<std::string>().find("not a path") != std::string::npos);
    // Refusals say why.
    r = daw.callAway("export_audio", {{"range", "loop"}});
    REQUIRE(r["isError"] == true);
    CHECK(r["content"][0]["text"].get<std::string>().find("loop") != std::string::npos);
    r = daw.callAway("export_audio", {{"format", "flac"}, {"bitDepth", 32}});
    REQUIRE(r["isError"] == true);
    // A stretch (time-selection.md §18): exactly it, no tail, named for its bars; a backward one refused.
    r = daw.callAway("export_audio", {{"name", "Stretch"}, {"range", {{"from", "1.3.1"}, {"to", "2.1.1"}}}});
    INFO(r.dump().substr(0, 2000));
    REQUIRE(r["isError"] == false);
    files = r["structuredContent"]["files"];
    REQUIRE(files.size() == 1);
    CHECK(juce::String(files[0]["file"].get<std::string>()).endsWith("Stretch - Bars 1-2.wav"));
    CHECK(files[0]["seconds"].get<double>() == Catch::Approx(1.0).margin(1e-3)); // two beats at 120 BPM
    r = daw.callAway("export_audio", {{"range", {{"from", 4}, {"to", 2}}}});
    REQUIRE(r["isError"] == true);
    CHECK(r["content"][0]["text"].get<std::string>().find("after") != std::string::npos);

    // MP3 at 256 kbps and a WAV beside it, from one render (C7, export.md §17); measured decoded.
    r = daw.callAway("export_audio", {{"name", "Both"},
                                      {"format", "mp3"},
                                      {"quality", 256},
                                      {"alsoAs", {{"format", "wav"}, {"bitDepth", 16}}}});
    INFO(r.dump().substr(0, 2000));
    REQUIRE(r["isError"] == false);
    files = r["structuredContent"]["files"];
    REQUIRE(files.size() == 2);
    CHECK(juce::String(files[0]["file"].get<std::string>()).endsWith("Both.mp3"));
    CHECK(files[0]["encoded"] == true);
    CHECK(juce::String(files[1]["file"].get<std::string>()).endsWith("Both.wav"));
    CHECK(files[1]["encoded"] == false);
    // A bit rate the format doesn't offer, or the same format twice: said why.
    r = daw.callAway("export_audio", {{"format", "opus"}, {"quality", 320}});
    REQUIRE(r["isError"] == true);
    CHECK(r["content"][0]["text"].get<std::string>().find("96, 128, 160, 192, 256") != std::string::npos);
    r = daw.callAway("export_audio", {{"format", "ogg"}, {"alsoAs", {{"format", "ogg"}}}});
    REQUIRE(r["isError"] == true);
}

TEST_CASE("mcp tools: tracks renamed, recoloured, moved, duplicated and deleted; clips renamed, recoloured, copied (C11)",
          "[integration][mcp]")
{
    Daw daw;
    auto tracks = daw.project()["tracks"];
    REQUIRE(tracks.size() >= 3);
    const auto t0 = tracks[0]["id"].get<std::string>(), t1 = tracks[1]["id"].get<std::string>(),
               t2 = tracks[2]["id"].get<std::string>();

    // Names and colours.
    auto r = daw.call("set_track", {{"track", t0}, {"name", "Bass"}, {"colour", 5}});
    CHECK(r["name"] == "Bass");
    CHECK(r["colour"] == 5);
    CHECK(daw.session->undoDescription() == "Assistant: set track");
    CHECK(daw.fails("set_track", {{"track", t0}}).find("name or a colour") != std::string::npos);
    CHECK_FALSE(daw.fails("set_track", {{"track", t0}, {"colour", 16}}).empty()); // the palette has 16
    const auto master = daw.project()["master"]["id"].get<std::string>();
    CHECK(daw.fails("set_track", {{"track", master}, {"name", "Main"}}).find("master") != std::string::npos);

    // Move: Bass to the end, then above the first again; a lane after it stays where it was (ADR 0042).
    const auto lane = daw.call("add_automation_lane", {{"after", t0}})["lane"].get<std::string>();
    r = daw.call("move_track", {{"track", t0}});
    CHECK(r["order"].back() == t0);
    daw.session->updateSnapshotNow();
    REQUIRE(daw.session->snapshot()->automationLanes.size() == 1);
    CHECK(daw.session->snapshot()->automationLanes[0].after == 0); // where it was: now at the top
    r = daw.call("move_track", {{"track", t0}, {"before", t1}});
    CHECK(r["order"][0] == t0);
    daw.call("remove_automation_lane", {{"lane", lane}});

    // Duplicate: "Bass 2", right after it.
    const auto clip = daw.call("create_midi_clip", {{"track", t0}, {"start", "1.1.1"}, {"length", "1 bar"}})["clip"]["id"].get<std::string>();
    r = daw.call("duplicate_track", {{"track", t0}});
    CHECK(r["name"] == "Bass 2");
    const auto copy = r["track"].get<std::string>();
    tracks = daw.project()["tracks"];
    CHECK(tracks[1]["id"] == copy);
    CHECK(tracks[1]["clips"].size() == 1);

    // Clips: renamed, coloured, then given their track's colour, once (ADR 0043).
    r = daw.call("set_clips", {{"clips", {clip}}, {"name", "Riff"}, {"colour", 9}});
    CHECK(r["clips"][0]["name"] == "Riff");
    CHECK(r["clips"][0]["colour"] == 9);
    r = daw.call("set_clips", {{"clips", {clip}}, {"colour", nullptr}});
    CHECK(r["clips"][0]["colour"] == 5); // Bass's
    daw.call("set_track", {{"track", t0}, {"colour", 2}});
    CHECK(daw.project()["tracks"][0]["clips"][0]["colour"] == 5); // still its own

    // Copy to a place: on t2 at bar 3; then on its own track at bar 5.
    r = daw.call("copy_clips", {{"clips", {clip}}, {"to", "3.1.1"}, {"to_track", t2}});
    REQUIRE(r["clips"].size() == 1);
    CHECK(r["clips"][0]["track"] == t2);
    CHECK(r["clips"][0]["start"]["bbt"] == "3.1.1");
    CHECK(r["left_out"] == 0);
    r = daw.call("copy_clips", {{"clips", {clip}}, {"to", "5.1.1"}});
    CHECK(r["clips"][0]["track"] == t0);

    // Delete: the copy goes; it's one assistant step.
    r = daw.call("delete_tracks", {{"tracks", {copy}}});
    CHECK(r["deleted"] == 1);
    CHECK(daw.session->undoDescription() == "Assistant: delete tracks");
    for (const auto& t : daw.project()["tracks"])
        CHECK(t["id"] != copy);
    CHECK(daw.fails("delete_tracks", {{"tracks", {master}}}).find("master") != std::string::npos);
}

TEST_CASE("mcp tools: presets listed, loaded and saved; A/B; a module added from a preset (A1)",
          "[integration][mcp]")
{
    Daw daw;
    daw.session->setPresetsFolder(daw.folder.getChildFile("Presets"));
    const auto t0 = daw.project()["tracks"][0]["id"].get<std::string>();
    const auto comp =
        daw.call("add_module", {{"track", t0}, {"type", "compressor"}})["id"].get<std::string>();

    auto r = daw.call("list_presets", {{"module", comp}});
    REQUIRE_FALSE(r["presets"].empty());
    CHECK(r["presets"][0]["name"] == "Default settings");
    CHECK(r["loaded"] == "Default settings");
    CHECK(r["modified"] == false);
    CHECK(r["showing"] == "A");
    CHECK(daw.call("list_presets", {{"type", "compressor"}})["presets"].size() == r["presets"].size());
    CHECK(daw.fails("list_presets", {}).find("module") != std::string::npos);

    // Saved as one of yours: the module now came from it.
    daw.call("set_parameters", {{"module", comp}, {"values", {{"threshold", -30.0}}}});
    r = daw.call("save_preset",
                 {{"module", comp}, {"name", "Hard"}, {"tags", {"Heavy"}}, {"description", "Lots"}});
    CHECK(r["preset"]["name"] == "Hard");
    CHECK(r["preset"]["source"] == "yours");
    CHECK(daw.fails("save_preset", {{"module", comp}, {"name", "Hard"}}).find("replace") !=
          std::string::npos);
    CHECK_FALSE(daw.fails("save_preset", {{"module", comp}, {"name", "a/b"}}).empty());
    r = daw.call("list_presets", {{"module", comp}});
    CHECK(r["loaded"] == "Hard");

    // Loaded by name or id: one undo step.
    daw.call("load_preset", {{"module", comp}, {"preset", "Default settings"}});
    CHECK(daw.session->undoDescription() == "Assistant: load preset");
    CHECK(daw.call("list_presets", {{"module", comp}})["loaded"] == "Default settings");
    r = daw.call("load_preset", {{"module", comp}, {"preset", "user/Compressor/Hard"}});
    CHECK(r["preset"] == "Hard");
    CHECK(daw.fails("load_preset", {{"module", comp}, {"preset", "Nothing like it"}}).find("list_presets") !=
          std::string::npos);

    // A/B.
    r = daw.call("compare", {{"module", comp}, {"action", "switch"}});
    CHECK(r["showing"] == "B");
    daw.call("load_preset", {{"module", comp}, {"preset", "Default settings"}});
    r = daw.call("compare", {{"module", comp}, {"action", "switch"}});
    CHECK(r["showing"] == "A");
    CHECK(r["preset"] == "Hard");

    // A module added from a preset; a wrong one adds nothing.
    const auto modules = daw.project()["tracks"][1]["chain"].size();
    const auto t1 = daw.project()["tracks"][1]["id"].get<std::string>();
    r = daw.call("add_module", {{"track", t1}, {"type", "compressor"}, {"preset", "Hard"}});
    CHECK(r["preset"] == "Hard");
    CHECK(daw.call("list_presets", {{"module", r["id"]}})["loaded"] == "Hard");
    CHECK_FALSE(daw.fails("add_module", {{"track", t1}, {"type", "compressor"}, {"preset", "Nope"}}).empty());
    CHECK(daw.project()["tracks"][1]["chain"].size() == modules + 1);
}

TEST_CASE("mcp tools: sections marked, renamed, moved, copied, inserted and deleted with their music (C3)",
          "[integration][mcp]")
{
    Daw daw;
    const auto t0 = daw.project()["tracks"][0]["id"].get<std::string>();
    // A B C, a bar each, a MIDI clip in each.
    for (int i = 0; i < 3; ++i)
    {
        const auto start = std::to_string(i + 1) + ".1.1", end = std::to_string(i + 2) + ".1.1";
        daw.call("add_section", {{"start", start}, {"end", end}});
        daw.call("create_midi_clip", {{"track", t0}, {"start", start}, {"length", "1 bar"}});
    }
    CHECK(daw.session->undoDescription() == "Assistant: create midi clip");
    auto sections = daw.call("list_sections")["sections"];
    REQUIRE(sections.size() == 3);
    const auto a = sections[0]["section"].get<std::string>(), b = sections[1]["section"].get<std::string>(),
               c = sections[2]["section"].get<std::string>();
    CHECK(sections[1]["name"] == "B");
    CHECK(daw.project()["sections"].size() == 3);
    CHECK(daw.fails("add_section", {{"start", "1.3.1"}, {"end", "2.3.1"}}).find("overlap") !=
          std::string::npos);

    // Renamed, recoloured; its marks move without the music.
    auto r = daw.call("set_section", {{"section", b}, {"name", "Chorus"}, {"colour", 7}});
    CHECK(r["sections"][1]["name"] == "Chorus");
    CHECK(r["sections"][1]["colour"] == 7);
    CHECK(daw.session->undoDescription() == "Assistant: set section");

    const auto clipStarts = [&daw]
    {
        std::vector<double> out;
        const auto project = daw.project(); // kept: the loop reads into it
        for (const auto& clip : project["tracks"][0]["clips"])
            out.push_back(clip["start"]["beats"].get<double>());
        return out;
    };
    // Chorus after C: A C Chorus, its clip with it.
    r = daw.call("move_section", {{"sections", {b}}, {"after", c}});
    CHECK(r["sections"][1]["section"] == c);
    CHECK(r["sections"][2]["name"] == "Chorus");
    CHECK(clipStarts() == std::vector<double>{0.0, 4.0, 8.0});
    CHECK(daw.session->undoDescription() == "Assistant: move section");
    CHECK(daw.fails("move_section", {{"sections", {b}}, {"after", b}}).find("inside itself") !=
          std::string::npos);
    CHECK(daw.fails("move_section", {{"sections", {b}}}).find("before") != std::string::npos);

    // Copied before A: Chorus 2 A C Chorus, the song a bar longer.
    r = daw.call("copy_section", {{"sections", {b}}, {"before", a}});
    REQUIRE(r["copies"].size() == 1);
    CHECK(r["sections"][0]["name"] == "Chorus 2");
    CHECK(clipStarts().size() == 4);

    // An empty bar after A: the music after it later.
    r = daw.call("insert_section", {{"length", "1 bar"}, {"after", a}});
    CHECK(r["sections"].size() == 5);

    // Delete C with its music; then the copy's mark alone.
    daw.call("delete_section", {{"sections", {c}}});
    CHECK(daw.call("list_sections")["sections"].size() == 4);
    CHECK(clipStarts().size() == 3);
    daw.call("delete_section", {{"sections", {r["sections"][0]["section"]}}, {"keep_music", true}});
    CHECK(daw.call("list_sections")["sections"].size() == 3);
    CHECK(clipStarts().size() == 3);
    CHECK(daw.fails("set_section", {{"section", "s:999999"}, {"name", "x"}}).find("no section") !=
          std::string::npos);

    // At a point, as a paste at the playhead: in empty time past the end, then inside a section
    // (at its nearer edge).
    const auto listed = daw.call("list_sections")["sections"];
    const auto first = listed[0]["section"].get<std::string>();
    const double firstStart = listed[0]["start"]["beats"].get<double>();
    r = daw.call("copy_section", {{"sections", {first}}, {"at", "20.1.1"}});
    REQUIRE(r["copies"].size() == 1);
    REQUIRE(r["sections"].size() == 4);
    CHECK(r["sections"][3]["start"]["beats"].get<double>() == Approx(76.0));
    const auto last = r["sections"][3]["section"].get<std::string>();
    r = daw.call("move_section",
                 {{"sections", {last}}, {"at", firstStart + 1.0}}); // inside the first, near its start
    CHECK(r["sections"][0]["section"] == last);
    CHECK(r["sections"][0]["start"]["beats"].get<double>() == Approx(firstStart));
    CHECK(daw.fails("copy_section", {{"sections", {first}}, {"at", "1.1.1"}, {"after", first}}).find("at") !=
          std::string::npos);
}

TEST_CASE(
    "mcp tools: racks: group, chains and their mixer rows, macros mapped and set, ungroup (racks.md §10)",
    "[integration][mcp]")
{
    Daw daw;
    const auto t0 = daw.project()["tracks"][0]["id"].get<std::string>();
    const auto sat = daw.call("add_module", {{"track", t0}, {"type", "saturator"}})["id"].get<std::string>();
    const auto util = daw.call("add_module", {{"track", t0}, {"type", "utility"}})["id"].get<std::string>();

    // Grouped: one chain, in their order; eight macros.
    auto rack = daw.call("group_modules", {{"modules", {util, sat}}});
    CHECK(daw.session->undoDescription() == "Assistant: group modules");
    const auto rackId = rack["rack"].get<std::string>();
    REQUIRE(rack["chains"].size() == 1);
    CHECK(rack["chains"][0]["modules"][0]["id"] == sat);
    CHECK(rack["chains"][0]["modules"][1]["id"] == util);
    REQUIRE(rack["macros"].size() == 8);
    CHECK(rack["macros"][0]["value"] == 0.0);
    // The track's chain lists the rack with its modules; modules in it are reached by their ids.
    const auto chain = daw.project()["tracks"][0]["chain"];
    REQUIRE(chain.size() == 1);
    CHECK(chain[0]["type"] == "rack");
    CHECK(chain[0]["chains"][0]["modules"].size() == 2);
    CHECK(daw.call("get_module", {{"module", util}})["id"] == util);

    // A second chain, a module added into it, its mixer row set.
    rack = daw.call("add_rack_chain", {{"rack", rackId}, {"name", "Dry"}});
    REQUIRE(rack["chains"].size() == 2);
    CHECK(rack["chains"][1]["name"] == "Dry");
    const auto chorus =
        daw.call("add_module", {{"track", t0}, {"type", "chorus"}, {"rack", rackId}, {"chain", 2}});
    rack = daw.call("get_rack", {{"rack", rackId}});
    CHECK(rack["chains"][1]["modules"][0]["id"] == chorus["id"]);
    rack = daw.call("set_rack_chain",
                    {{"rack", rackId}, {"chain", 2}, {"volume_db", -6}, {"pan", "25L"}, {"on", false}});
    CHECK(rack["chains"][1]["volumeDb"] == -6.0);
    CHECK(rack["chains"][1]["pan"] == "25L");
    CHECK(rack["chains"][1]["on"] == false);
    CHECK(daw.fails("set_rack_chain", {{"rack", rackId}, {"chain", 3}}).find("counted from 1") !=
          std::string::npos);

    // A macro mapped in the parameter's unit, set and named; the parameter is then the macro's.
    rack = daw.call("map_macro", {{"rack", rackId},
                                  {"macro", 1},
                                  {"module", util},
                                  {"parameter", "gain"},
                                  {"from", -24},
                                  {"to", 0},
                                  {"curve", 50}});
    const auto mapping = rack["macros"][0]["mappings"][0];
    CHECK(mapping["module"] == util);
    CHECK(mapping["from"]["value"].get<double>() == Catch::Approx(-24.0).margin(0.1));
    CHECK(mapping["to"]["value"].get<double>() == Catch::Approx(0.0).margin(0.1));
    CHECK(mapping["curve"] == 50.0);
    rack = daw.call("set_macro", {{"rack", rackId}, {"macro", 1}, {"value", 100}, {"name", "Level"}});
    CHECK(rack["macros"][0]["name"] == "Level");
    CHECK(rack["macros"][0]["value"] == 100.0);
    const auto gain = daw.call("get_module", {{"module", util}})["parameters"][0];
    CHECK(gain["setByMacro"]["macro"] == 1);
    CHECK(gain["value"].get<double>() == Catch::Approx(0.0).margin(0.2));
    const auto refused = daw.call("set_parameters", {{"module", util}, {"values", {{"gain", -3}}}});
    REQUIRE(refused["problems"].size() == 1);
    CHECK(refused["problems"][0]["problem"].get<std::string>().find("set by macro 1") != std::string::npos);
    // The rack's macros are its parameters too, then Morph (racks.md §18.3).
    const auto rackParams = daw.call("get_module", {{"module", rackId}})["parameters"];
    REQUIRE(rackParams.size() == 9);
    CHECK(rackParams[8]["id"] == "morph");
    daw.call("set_parameters", {{"module", rackId}, {"values", {{"macro1", 50}}}});
    CHECK(daw.call("get_rack", {{"rack", rackId}})["macros"][0]["value"] == 50.0);
    CHECK(daw.call("unmap_macro", {{"module", util}, {"parameter", "gain"}})["unmapped"] == true);
    CHECK(daw.call("get_rack", {{"rack", rackId}})["macros"][0]["mappings"].empty());

    // Moved out of the rack, then back in.
    daw.call("move_module", {{"module", util}, {"position", 1}, {"rack", nullptr}});
    CHECK(daw.project()["tracks"][0]["chain"][1]["id"] == util);
    daw.call("move_module", {{"module", util}, {"position", 0}, {"rack", rackId}, {"chain", 1}});
    CHECK(daw.call("get_rack", {{"rack", rackId}})["chains"][0]["modules"][0]["id"] == util);

    // Saved as a file and loaded on another track (racks.md §7); search finds it as a rack.
    const auto saved =
        daw.call("save_rack", {{"rack", rackId},
                               {"path", (daw.folder.getFullPathName() + "/Mine.segnorack").toStdString()}});
    const auto t1 = daw.project()["tracks"][1]["id"].get<std::string>();
    const auto copy = daw.call("add_module", {{"track", t1}, {"type", "rack"}, {"path", saved["path"]}});
    CHECK(copy["type"] == "rack");
    CHECK(copy["rack"]["chains"].size() == 2);
    CHECK(daw.session->undoDescription() == "Assistant: add module");

    // A chain removed; the rack ungrouped: its shown chain's modules back in the track's chain.
    daw.call("remove_rack_chain", {{"rack", rackId}, {"chain", 2}});
    daw.call("set_rack_chain", {{"rack", rackId}, {"chain", 1}, {"shown", true}});
    const auto placed = daw.call("ungroup_rack", {{"rack", rackId}})["modules"];
    CHECK(placed.size() == 2);
    CHECK(daw.project()["tracks"][0]["chain"].size() == 2);
    CHECK(daw.fails("get_rack", {{"rack", util}}).find("isn't a rack") != std::string::npos);
    // An empty rack by its type.
    CHECK(daw.call("add_module", {{"track", t0}, {"type", "rack"}})["type"] == "rack");
}

TEST_CASE("mcp tools: racks' snapshots stored, recalled, renamed and removed; Morph between two; a macro "
          "locked; Vary (racks.md §18)",
          "[integration][mcp]")
{
    Daw daw;
    const auto t0 = daw.project()["tracks"][0]["id"].get<std::string>();
    const auto util = daw.call("add_module", {{"track", t0}, {"type", "utility"}})["id"].get<std::string>();
    auto rack = daw.call("group_modules", {{"modules", {util}}});
    const auto rackId = rack["rack"].get<std::string>();
    CHECK(rack["snapshots"].empty());
    CHECK(rack["currentSnapshot"].is_null());
    CHECK(rack["morph"]["on"] == false);
    daw.call(
        "map_macro",
        {{"rack", rackId}, {"macro", 1}, {"module", util}, {"parameter", "gain"}, {"from", -24}, {"to", 0}});

    // Two snapshots: the first two are Morph's A and B.
    daw.call("set_macro", {{"rack", rackId}, {"macro", 1}, {"value", 20}});
    rack = daw.call("store_snapshot", {{"rack", rackId}});
    CHECK(daw.session->undoDescription() == "Assistant: store snapshot");
    daw.call("set_macro", {{"rack", rackId}, {"macro", 1}, {"value", 80}});
    rack = daw.call("store_snapshot", {{"rack", rackId}});
    REQUIRE(rack["snapshots"].size() == 2);
    CHECK(rack["snapshots"][0]["name"] == "Snapshot 1");
    CHECK(rack["snapshots"][0]["macros"][0] == 20.0);
    CHECK(rack["currentSnapshot"] == 2);
    CHECK(rack["morph"]["a"] == 1);
    CHECK(rack["morph"]["b"] == 2);
    rack = daw.call("set_snapshot", {{"rack", rackId}, {"snapshot", 1}, {"name", "Verse"}});
    rack = daw.call("set_snapshot", {{"rack", rackId}, {"snapshot", 2}, {"name", "Drop"}, {"macros", {90}}});
    CHECK(rack["snapshots"][0]["name"] == "Verse");
    CHECK(rack["snapshots"][1]["macros"][0] == 90.0);

    // Recalled: the macros where it has them.
    rack = daw.call("recall_snapshot", {{"rack", rackId}, {"snapshot", 1}});
    CHECK(rack["macros"][0]["value"] == 20.0);
    CHECK(rack["currentSnapshot"] == 1);
    CHECK(rack["snapshotChanged"] == false);
    CHECK(daw.fails("recall_snapshot", {{"rack", rackId}, {"snapshot", 3}}).find("2 snapshots") !=
          std::string::npos);

    // Morph on, half way: the macro between, set by it; set_macro and set_parameters refuse it.
    rack = daw.call("set_morph", {{"rack", rackId}, {"on", true}, {"position", 50}});
    CHECK(rack["morph"]["on"] == true);
    CHECK(rack["morph"]["position"] == 50.0);
    CHECK(rack["macros"][0]["value"] == Catch::Approx(55.0).margin(0.2));
    CHECK(rack["macros"][0]["setByMorph"] == true);
    CHECK(daw.fails("set_macro", {{"rack", rackId}, {"macro", 1}, {"value", 10}}).find("Morph sets") !=
          std::string::npos);
    const auto refused = daw.call("set_parameters", {{"module", rackId}, {"values", {{"macro1", 10}}}});
    REQUIRE(refused["problems"].size() == 1);
    CHECK(refused["problems"][0]["problem"].get<std::string>().find("set by Morph") != std::string::npos);
    CHECK(daw.call("get_module", {{"module", rackId}})["parameters"][0]["setByMorph"] == true);
    // Its position as the rack's parameter.
    daw.call("set_parameters", {{"module", rackId}, {"values", {{"morph", 100}}}});
    CHECK(daw.call("get_rack", {{"rack", rackId}})["macros"][0]["value"] == Catch::Approx(90.0).margin(0.2));

    // Vary refuses while Morph is on; off, it moves the macro.
    CHECK(daw.fails("vary_module", {{"module", rackId}}).find("Morph sets the macros") != std::string::npos);
    daw.call("set_morph", {{"rack", rackId}, {"on", false}});
    rack = daw.call("vary_module", {{"module", rackId}, {"more", true}});
    CHECK(daw.session->undoDescription() == "Assistant: vary module");
    CHECK(std::abs(rack["macros"][0]["value"].get<double>() - 90.0) <= 40.1);

    // Locked: Vary has nothing to move.
    rack = daw.call("set_macro", {{"rack", rackId}, {"macro", 1}, {"locked", true}});
    CHECK(rack["macros"][0]["locked"] == true);
    CHECK(daw.fails("vary_module", {{"module", rackId}}).find("Nothing to vary") != std::string::npos);

    // Removed: B goes, so Morph can't be on.
    rack = daw.call("remove_snapshot", {{"rack", rackId}, {"snapshot", 2}});
    REQUIRE(rack["snapshots"].size() == 1);
    CHECK(rack["morph"]["b"].is_null());
    CHECK(daw.fails("set_morph", {{"rack", rackId}, {"on", true}}).find("two snapshots") !=
          std::string::npos);
}

TEST_CASE("mcp tools: modulators added, routed, set, removed and kept (modulators.md §16)",
          "[integration][mcp]")
{
    Daw daw;
    const auto project = daw.project();
    const auto t0 = project["tracks"][0]["id"].get<std::string>();
    const auto t1 = project["tracks"][1]["id"].get<std::string>();

    // The five, as module types of their own kind.
    const auto types = daw.call("list_module_types", {{"kind", "modulator"}})["types"];
    std::vector<std::string> ids;
    for (const auto& t : types)
        ids.push_back(t["type"].get<std::string>());
    CHECK(ids ==
          std::vector<std::string>{"mod-lfo", "mod-random", "mod-envelope", "mod-follower", "mod-steps"});
    CHECK(types[0]["kind"] == "modulator");
    CHECK_FALSE(types[0]["description"].get<std::string>().empty());
    const auto all = daw.call("list_module_types")["types"];
    CHECK(std::any_of(all.begin(), all.end(), [](const Json& t) { return t["type"] == "mod-steps"; }));
    CHECK(daw.read("segno://module-type/mod-lfo")["sections"][0]["id"] == "shape");

    // Added as a module, by type or name, where it's asked for.
    const auto filter = daw.call("add_module", {{"track", t0}, {"type", "filter"}})["id"].get<std::string>();
    auto lfo = daw.call("add_module", {{"track", t0}, {"type", "mod-lfo"}, {"position", 0}});
    CHECK(daw.session->undoDescription() == "Assistant: add module");
    CHECK(lfo["type"] == "mod-lfo");
    CHECK(lfo["modulator"]["routes"].empty());
    CHECK(lfo["position"] == 0);
    CHECK(lfo["sections"][0]["id"] == "shape");
    const auto lfoId = lfo["id"].get<std::string>();
    const auto random = daw.call("add_module", {{"track", t0}, {"type", "Random"}})["id"].get<std::string>();
    CHECK(daw.call("add_module", {{"track", project["master"]["id"]}, {"type", "mod-lfo"}})["type"] ==
          "mod-lfo");

    // Set up as any module.
    const auto set = daw.call("set_parameters", {{"module", lfoId}, {"values", {{"rate", "2 Hz"}}}});
    REQUIRE(set["set"].size() == 1);
    CHECK(set["set"][0]["value"] == Approx(2.0));

    // Routed to the Filter's Freq at 50 %, then 120 %; the track's volume at -30 %.
    const Json freq{{"module", filter}, {"parameter", "freq"}};
    auto route = daw.call("route_modulator", {{"modulator", lfoId}, {"target", freq}});
    CHECK(route["amount"] == Approx(50.0));
    CHECK(route["target"]["parameter"] == "freq");
    CHECK(daw.session->undoDescription() == "Assistant: route modulator");
    route = daw.call("set_route", {{"modulator", lfoId}, {"target", freq}, {"amount", 120}});
    CHECK(route["amount"] == Approx(120.0));
    daw.call("route_modulator",
             {{"modulator", lfoId}, {"target", {{"track", t0}, {"parameter", "volume"}}}, {"amount", -30}});
    auto mods = daw.call("get_modulators", {{"track", t0}})["modulators"];
    REQUIRE(mods.size() == 2);
    CHECK(mods[0]["id"] == lfoId);
    CHECK(mods[0]["type"] == "mod-lfo");
    CHECK(mods[0]["settings"]["rate"].get<std::string>().find("2.00") != std::string::npos);
    REQUIRE(mods[0]["routes"].size() == 2);
    std::map<std::string, Json> routes; // in the track's parameter order: its mixer's first
    for (const auto& r : mods[0]["routes"])
        routes[r["target"]["parameter"].get<std::string>()] = r;
    CHECK(routes["freq"]["target"]["module"] == filter);
    CHECK(routes["freq"]["amount"] == Approx(120.0));
    CHECK(routes["volume"]["target"]["track"] == t0);
    CHECK(routes["volume"]["amount"] == Approx(-30.0));
    CHECK_FALSE(mods[0].contains("kept")); // an LFO rolls no dice
    CHECK(mods[1]["kept"] == false);
    CHECK(daw.call("get_module", {{"module", lfoId}})["modulator"]["routes"].size() == 2);

    // Refused, saying why: its own parameter, another track's, a loop.
    CHECK(daw.fails("route_modulator",
                    {{"modulator", lfoId}, {"target", {{"module", lfoId}, {"parameter", "rate"}}}})
              .find("its own") != std::string::npos);
    const auto other = daw.call("add_module", {{"track", t1}, {"type", "filter"}})["id"].get<std::string>();
    CHECK(daw.fails("route_modulator",
                    {{"modulator", lfoId}, {"target", {{"module", other}, {"parameter", "freq"}}}})
              .find("own track") != std::string::npos);
    daw.call("route_modulator",
             {{"modulator", random}, {"target", {{"module", lfoId}, {"parameter", "rate"}}}});
    CHECK(daw.fails("route_modulator",
                    {{"modulator", lfoId}, {"target", {{"module", random}, {"parameter", "rate"}}}})
              .find("loop") != std::string::npos);
    CHECK(daw.fails("route_modulator", {{"modulator", filter}, {"target", freq}}).find("isn't a modulator") !=
          std::string::npos);

    // Removed; undone.
    daw.call("remove_route", {{"modulator", lfoId}, {"target", freq}});
    CHECK(daw.call("get_modulators", {{"track", t0}})["modulators"][0]["routes"].size() == 1);
    CHECK(daw.fails("set_route", {{"modulator", lfoId}, {"target", freq}, {"amount", 10}})
              .find("doesn't move") != std::string::npos);
    daw.call("undo");
    CHECK(daw.call("get_modulators", {{"track", t0}})["modulators"][0]["routes"].size() == 2);

    // Keep: one modulator's roll, then every one in the song let roll.
    CHECK(daw.fails("keep_roll", {{"modulator", lfoId}, {"keep", true}}).find("rolls no dice") !=
          std::string::npos);
    auto kept = daw.call("keep_roll", {{"modulator", random}, {"keep", true}})["modulators"];
    REQUIRE(kept.size() == 1);
    CHECK(kept[0]["kept"] == true);
    CHECK(daw.session->undoDescription() == "Assistant: keep roll");
    kept = daw.call("keep_roll", {{"song", true}, {"keep", false}})["modulators"];
    CHECK(kept[0]["kept"] == false);
    CHECK(daw.fails("keep_roll", {{"keep", true}}).find("song") != std::string::npos);

    // A clip's chance (piano-roll.md §26): kept, and Re-roll then; refused while it rolls.
    const auto midi =
        daw.call("create_midi_clip",
                 {{"track", t0},
                  {"start", 0},
                  {"notes", {{{"pitch", 60}, {"start", 0}, {"length", 1}, {"chance", 50}}}}})["clip"]["id"];
    CHECK(daw.fails("reroll_chance", {{"clip", midi}}).find("keep_roll") != std::string::npos);
    auto both = daw.call("keep_roll", {{"clip", midi}, {"keep", true}});
    REQUIRE(both["clips"].size() == 1);
    CHECK(both["clips"][0]["kept"] == true);
    CHECK(daw.call("get_clip", {{"clip", midi}})["chanceKept"] == true);
    CHECK(daw.call("reroll_chance", {{"clip", midi}, {"seed", 5}})["seed"] == 5);
    both = daw.call("keep_roll", {{"song", true}, {"keep", false}});
    CHECK(both["clips"][0]["kept"] == false);
    const auto plain = daw.call("create_midi_clip", {{"track", t1}, {"start", 0}})["clip"]["id"];
    CHECK(daw.fails("keep_roll", {{"clip", plain}, {"keep", true}}).find("left to chance") !=
          std::string::npos);

    // A Follower listens to another track as a sidechain (§12.4); get_module says which.
    const auto follower = daw.call("add_module", {{"track", t0}, {"type", "mod-follower"}})["id"];
    CHECK(daw.call("get_module", {{"module", follower}})["sidechain"]["source"].is_null());
    CHECK(daw.call("set_module_sidechain", {{"module", follower}, {"source", t1}})["source"] == t1);
    CHECK(daw.call("get_module", {{"module", follower}})["sidechain"]["source"] == t1);
    CHECK(daw.fails("set_module_sidechain", {{"module", lfoId}, {"source", t1}}).find("no sidechain") !=
          std::string::npos);
    daw.call("remove_module", {{"module", follower}});

    // The LFO's drawn shape (§12.1): named, or points; Shape set to Drawn; get_module says so.
    auto drawn =
        daw.call("set_lfo_curve", {{"modulator", lfoId}, {"points", {{0, 0, 0}, {0.5, 1, 0.3}, {1, 0, 0}}}});
    CHECK(drawn["curve"]["points"].size() == 3);
    CHECK(daw.call("get_module", {{"module", lfoId}})["modulator"]["curve"]["points"].size() == 3);
    drawn = daw.call("set_lfo_curve", {{"modulator", lfoId}, {"shape", "steps"}});
    CHECK(drawn["curve"]["shape"] == "steps");
    CHECK(daw.fails("set_lfo_curve", {{"modulator", random}, {"shape", "sine"}}).find("isn't an LFO") !=
          std::string::npos);
    CHECK(daw.fails("set_lfo_curve", {{"modulator", lfoId}, {"points", {{0.6, 0, 0}, {0.2, 1, 0}}}})
              .find("left to right") != std::string::npos);

    // Moved, folded, turned off and removed as any module.
    CHECK(daw.call("move_module", {{"module", lfoId}, {"position", 2}})["chain"][2]["id"] == lfoId);
    CHECK(daw.call("set_module_view", {{"module", lfoId}, {"collapsed", {"shape"}}})["collapsed"][0] ==
          "shape");
    daw.call("set_module_on", {{"module", lfoId}, {"on", false}});
    CHECK(daw.call("get_module", {{"module", lfoId}})["on"] == false);
    daw.call("remove_module", {{"module", lfoId}});
    mods = daw.call("get_modulators", {{"track", t0}})["modulators"];
    REQUIRE(mods.size() == 1);
    CHECK(mods[0]["id"] == random);
}

TEST_CASE("mcp tools: an audio clip's sound, its gain line, its ranges and Normalise (P6a)",
          "[integration][mcp][audio-sound]")
{
    Daw daw;
    const auto t0 = daw.project()["tracks"][0]["id"].get<std::string>();
    // A 4 s stereo sine at -6 dBFS.
    fd::testkit::AudioBuffer sine(2, 4 * 48000);
    for (int c = 0; c < 2; ++c)
        for (int i = 0; i < sine.numSamples(); ++i)
            sine.at(c, i) = 0.5f * std::sin(static_cast<float>(i) * 0.05f);
    const auto file = daw.folder.getChildFile("sine.wav");
    REQUIRE(fd::testkit::writeWav(file.getFullPathName().toStdString(), sine, 48000.0));
    const auto clip =
        daw.call("insert_audio_file",
                 {{"track", t0}, {"start", 0}, {"path", file.getFullPathName().toStdString()}})["clip"]["id"]
            .get<std::string>();
    auto got = daw.call("get_clip", {{"clip", clip}});
    CHECK(got["sound"]["gain"] == 0.0);
    CHECK(got["sound"]["channels"] == "stereo");
    CHECK(got["file"]["channels"] == 2);
    CHECK(got["file"]["sampleRate"] == 48000.0);
    CHECK(got["warp"]["on"] == false); // a held tone has no beat to follow

    // Gain, polarity, channels, reverse: one step each.
    auto r = daw.call("set_audio",
                      {{"clips", {clip}}, {"gain", "-3 dB"}, {"inverted", true}, {"channels", "mono"}});
    CHECK(r["clips"][0]["sound"]["gain"] == -3.0);
    CHECK(r["clips"][0]["sound"]["inverted"] == true);
    CHECK(r["clips"][0]["sound"]["channels"] == "mono");
    CHECK(daw.session->undoDescription() == "Assistant: set audio");
    r = daw.call("set_audio", {{"clips", {clip}}, {"gainBy", -1.5}});
    CHECK(r["clips"][0]["sound"]["gain"] == -4.5);
    r = daw.call("set_audio", {{"clips", {clip}}, {"reverse", true}});
    CHECK(r["clips"][0]["sound"]["reverse"] == true);
    CHECK(r["clips"][0]["source"].get<std::string>().find("Reversed") != std::string::npos);
    CHECK(daw.fails("set_audio", {{"clips", {clip}}, {"channels", "sideways"}}).find("stereo") !=
          std::string::npos);
    CHECK(daw.fails("set_audio", {{"clips", {clip}}, {"gain", "loud"}}).find("dB") != std::string::npos);

    // The gain line: in dB, at times in the file.
    r = daw.call("set_gain_line",
                 {{"clip", clip},
                  {"points", {{{"time", 1.0}, {"db", 0}}, {{"time", 2.0}, {"db", nullptr}, {"bend", 0.5}}}}});
    REQUIRE(r["points"].size() == 2);
    CHECK(r["points"][0]["db"] == 0.0);
    CHECK(r["points"][1]["db"].is_null());
    CHECK(r["points"][1]["bend"] == Approx(0.5));
    CHECK(daw.call("get_gain_line", {{"clip", clip}})["points"].size() == 2);
    CHECK(daw.call("get_clip", {{"clip", clip}})["sound"]["gainLinePoints"] == 2);
    CHECK(daw.fails("set_gain_line", {{"clip", clip}, {"points", {{{"time", -1.0}, {"db", 0}}}}})
              .find("negative") != std::string::npos);
    daw.call("set_gain_line", {{"clip", clip}, {"points", Json::array()}});
    CHECK(daw.call("get_gain_line", {{"clip", clip}})["points"].empty());

    // A range: silenced, then split into a clip of its own; a fade from the middle is refused.
    r = daw.call("edit_audio_range",
                 {{"clip", clip}, {"from", "1.2.1"}, {"to", "1.3.1"}, {"action", "silence"}});
    CHECK(daw.call("get_gain_line", {{"clip", clip}})["points"].size() == 4);
    CHECK(daw.session->undoDescription() == "Assistant: edit audio range");
    CHECK(daw.fails("edit_audio_range",
                    {{"clip", clip}, {"from", "1.2.1"}, {"to", "1.3.1"}, {"action", "fade"}})
              .find("start") != std::string::npos);
    CHECK(daw.fails("edit_audio_range",
                    {{"clip", clip}, {"from", "1.2.1"}, {"to", "1.3.1"}, {"action", "gain"}})
              .find("db") != std::string::npos);
    CHECK(daw.fails("edit_audio_range",
                    {{"clip", clip}, {"from", "1.2.1"}, {"to", "1.3.1"}, {"action", "fold"}})
              .find("split") != std::string::npos);
    r = daw.call("edit_audio_range",
                 {{"clip", clip}, {"from", "1.2.1"}, {"to", "1.3.1"}, {"action", "split"}});
    CHECK(r["clips"].size() == 3);

    // The tools: Normalise each to -1 dBFS.
    const auto tools = daw.call("list_audio_tools")["tools"];
    CHECK(std::any_of(tools.begin(), tools.end(), [](const Json& t) { return t["tool"] == "normalise"; }));
    std::vector<std::string> parts;
    for (const auto& c : r["clips"])
        parts.push_back(c["id"].get<std::string>());
    daw.call("set_audio",
             {{"clips", parts}, {"reverse", false}, {"inverted", false}, {"channels", "stereo"}});
    r = daw.call("transform_audio",
                 {{"clips", parts}, {"tool", "normalise"}, {"settings", {{"to", "peak"}, {"peak", -1}}}});
    CHECK(r["changed"] == 3);
    // The parts either side play the sine at -6 dBFS: +5 dB brings them to -1. The middle one is
    // the silenced range, only its ramps heard: quieter, so raised more.
    std::map<std::string, double> gains;
    for (const auto& c : r["clips"])
        gains[c["id"].get<std::string>()] = c["sound"]["gain"].get<double>();
    CHECK(gains[parts[1]] == Approx(5.0).margin(0.1));
    CHECK(gains[parts[2]] == Approx(5.0).margin(0.1));
    CHECK(gains[parts[0]] > 5.5);
    CHECK(daw.fails("transform_audio", {{"clips", parts}, {"tool", "normalise"}, {"settings", {{"peak", 3}}}})
              .find("from") != std::string::npos);
    CHECK(daw.fails("transform_audio", {{"clips", parts}, {"tool", "explode"}}).find("list_audio_tools") !=
          std::string::npos);
}

TEST_CASE(
    "mcp tools: warping: a loop placed in time, its tempo, mode and pitch, markers, transients, Quantise and "
    "the Tempo tool (P6b)",
    "[integration][mcp][warp]")
{
    Daw daw;
    const auto t0 = daw.project()["tracks"][0]["id"].get<std::string>();
    // A drum loop: a hit on every beat, 2 bars at 92 BPM.
    const double beat = 60.0 / 92.0;
    fd::testkit::AudioBuffer loop(2, static_cast<int>(8 * beat * 48000.0));
    std::uint32_t x = 7;
    for (int k = 0; k < 8; ++k)
        for (int i = 0; i < 6000; ++i)
        {
            const int at = static_cast<int>(k * beat * 48000.0) + i;
            if (at >= loop.numSamples())
                break;
            x = x * 1664525u + 1013904223u;
            const float v = 0.7f * std::exp(-static_cast<float>(i) / 2000.0f) *
                            (static_cast<float>(x >> 8) / 8388608.0f - 1.0f);
            loop.at(0, at) = loop.at(1, at) = v;
        }
    const auto file = daw.folder.getChildFile("break.wav");
    REQUIRE(fd::testkit::writeWav(file.getFullPathName().toStdString(), loop, 48000.0));
    const auto placed = daw.call(
        "insert_audio_file", {{"track", t0}, {"start", 0}, {"path", file.getFullPathName().toStdString()}});
    const auto clip = placed["clip"]["id"].get<std::string>();
    auto got = daw.call("get_clip", {{"clip", clip}});
    // Placed in time (§7.2): its tempo found, Beats for its hits.
    CHECK(got["warp"]["on"] == true);
    CHECK(got["warp"]["tempo"].get<double>() == Approx(92.0).margin(0.01));
    CHECK(got["warp"]["mode"] == "beats");
    CHECK(got["file"]["seconds"].get<double>() == Approx(8 * beat).margin(1e-3));

    // What's found: the tempo, its downbeat; the transients, one on each hit.
    const auto found = daw.call("detect_tempo", {{"clip", clip}});
    CHECK(found["found"] == true);
    CHECK(found["tempo"].get<double>() == Approx(92.0).margin(0.01));
    CHECK(found["downbeat"].get<double>() == Approx(0.0).margin(0.01));
    const auto transients = daw.call("get_transients", {{"clip", clip}})["transients"];
    REQUIRE(transients.size() == 8);
    CHECK(transients[3].get<double>() == Approx(3 * beat).margin(0.003));

    // Transients by hand: one added between two hits, one removed; each call one undo step.
    auto edited = daw.call("edit_transients", {{"clip", clip}, {"add", {3.5 * beat}}})["transients"];
    REQUIRE(edited.size() == 9);
    CHECK(edited[4].get<double>() == Approx(3.5 * beat).margin(1e-4));
    CHECK(daw.session->undoDescription() == "Assistant: edit transients");
    edited =
        daw.call("edit_transients", {{"clip", clip}, {"remove", {edited[4], transients[3]}}})["transients"];
    CHECK(edited.size() == 7);
    CHECK(daw.fails("edit_transients", {{"clip", clip}, {"remove", {3.5 * beat}}}).find("get_transients") !=
          std::string::npos);
    CHECK(daw.fails("edit_transients", {{"clip", clip}, {"add", {60.0}}}).find("long") != std::string::npos);
    CHECK(daw.fails("edit_transients", {{"clip", clip}}).find("add or remove") != std::string::npos);
    daw.session->undo();
    daw.session->undo();
    REQUIRE(daw.call("get_transients", {{"clip", clip}})["transients"].size() == 8);

    // Its mode, tempo and pitch.
    auto r = daw.call("set_audio", {{"clips", {clip}}, {"mode", "tones"}, {"transpose", 2}, {"fine", -10}});
    CHECK(r["clips"][0]["warp"]["mode"] == "tones");
    CHECK(r["clips"][0]["pitch"]["transpose"] == 2);
    CHECK(r["clips"][0]["pitch"]["fine"] == -10);
    CHECK(daw.session->undoDescription() == "Assistant: set audio");
    r = daw.call("set_audio", {{"clips", {clip}}, {"tempo", 184.0}});
    CHECK(r["clips"][0]["length"].get<double>() == Approx(16.0).margin(1e-3)); // the same audio, 16 beats
    daw.call("set_audio", {{"clips", {clip}}, {"tempo", 92.0}});
    CHECK(daw.fails("set_audio", {{"clips", {clip}}, {"mode", "elastique"}}).find("tones") !=
          std::string::npos);

    // Markers: at the transients, one tempo from the second on, then set by hand.
    r = daw.call("set_warp_markers", {{"clip", clip}, {"atTransients", true}});
    CHECK(r["clip"]["warp"]["markers"].size() == 7); // every hit but the file's start
    r = daw.call("set_warp_markers", {{"clip", clip}, {"warpFrom", 1}});
    CHECK(r["clip"]["warp"]["markers"].size() == 2);
    r = daw.call("set_warp_markers", {{"clip", clip}, {"markers", {{{"file", 2 * beat}, {"beat", 2.5}}}}});
    REQUIRE(r["clip"]["warp"]["markers"].size() == 1);
    CHECK(r["clip"]["warp"]["markers"][0]["beat"] == Approx(2.5));
    CHECK(daw.fails("set_warp_markers", {{"clip", clip}, {"markers", {{{"file", 1.0}, {"beat", 40.0}}}}})
              .find("x4") != std::string::npos);
    CHECK(daw.fails("set_warp_markers", {{"clip", clip}}).find("one of") != std::string::npos);
    r = daw.call("set_warp_markers", {{"clip", clip}, {"remove", {0}}});
    CHECK(r["clip"]["warp"]["markers"].empty());

    // The tools: Quantise to 1/8; the Tempo tool at what's found, the song's tempo from it.
    const auto tools = daw.call("list_audio_tools");
    std::set<std::string> names;
    for (const auto& t : tools["tools"])
        names.insert(t["tool"].get<std::string>());
    CHECK(names == std::set<std::string>{"quantise", "tempo", "normalise", "slice"});
    r = daw.call("transform_audio", {{"clips", {clip}}, {"tool", "quantise"}, {"grid", "1/8"}});
    CHECK(daw.session->undoDescription() == "Assistant: transform audio");
    CHECK(daw.fails("transform_audio", {{"clips", {clip}}, {"tool", "quantise"}, {"grid", "1/5"}})
              .find("1/16") != std::string::npos);
    r = daw.call("transform_audio", {{"clips", {clip}}, {"tool", "tempo"}, {"settings", {{"then", "song"}}}});
    CHECK(daw.project()["tempo"].get<double>() == Approx(92.0).margin(0.01));
    CHECK(r["clips"][0]["warp"]["tempo"].get<double>() == Approx(92.0).margin(0.01));
}

TEST_CASE("mcp tools: an audio clip's key, Follow key with the song's, detect_key on audio, and Slice (P6c)",
          "[integration][mcp][audio-key]")
{
    Daw daw;
    const auto t0 = daw.project()["tracks"][0]["id"].get<std::string>();
    // A held A whose name says A major; and a click every half second for 2 s.
    fd::testkit::AudioBuffer tone(2, 2 * 48000), clicks(2, 2 * 48000);
    for (int i = 0; i < tone.numSamples(); ++i)
        tone.at(0, i) = tone.at(1, i) =
            0.3f * std::sin(2.0f * 3.14159265f * 440.0f * static_cast<float>(i) / 48000.0f);
    for (int k = 0; k < 4; ++k)
        for (int i = 0; i < 480; ++i)
            clicks.at(0, k * 24000 + i) = clicks.at(1, k * 24000 + i) =
                0.8f * std::exp(-static_cast<float>(i) / 100.0f) * std::sin(static_cast<float>(i) * 0.3f);
    const auto toneFile = daw.folder.getChildFile("Lead_Amaj.wav");
    const auto clickFile = daw.folder.getChildFile("Hits.wav");
    REQUIRE(fd::testkit::writeWav(toneFile.getFullPathName().toStdString(), tone, 48000.0));
    REQUIRE(fd::testkit::writeWav(clickFile.getFullPathName().toStdString(), clicks, 48000.0));
    const auto lead =
        daw.call("insert_audio_file", {{"track", t0},
                                       {"start", 0},
                                       {"path", toneFile.getFullPathName().toStdString()}})["clip"]["id"]
            .get<std::string>();
    const auto hits =
        daw.call("insert_audio_file", {{"track", t0},
                                       {"start", "3.1.1"},
                                       {"path", clickFile.getFullPathName().toStdString()}})["clip"]["id"]
            .get<std::string>();

    // detect_key reads audio clips: the lead's key, found now.
    auto keys = daw.call("detect_key", {{"clips", {lead}}})["keys"];
    REQUIRE(!keys.empty());
    CHECK(keys[0]["name"] == "A major");
    auto got = daw.call("get_clip", {{"clip", lead}});
    CHECK(got["key"]["name"] == "A major");
    CHECK(got["keyFound"] == true);
    CHECK(got["followKey"]["on"] == false);
    // The song has no key yet: nothing to follow, refused whole (the inspector dims Follow).
    CHECK(
        daw.fails("set_audio", {{"clips", {lead}}, {"followKey", true}, {"transpose", 3}}).find("set_key") !=
        std::string::npos);
    got = daw.call("get_clip", {{"clip", lead}});
    CHECK(got["followKey"]["on"] == false);
    CHECK(got["pitch"]["transpose"] == 0);

    // Following G major: two semitones down; set_key moves it again, in its own step.
    daw.call("set_key", {{"root", "G"}, {"scale", "major"}});
    auto r = daw.call("set_audio", {{"clips", {lead}}, {"followKey", true}});
    CHECK(r["clips"][0]["followKey"]["on"] == true);
    CHECK(r["clips"][0]["followKey"]["steps"] == -2);
    CHECK(daw.session->undoDescription() == "Assistant: set audio");
    daw.call("set_key", {{"root", "Bb"}, {"scale", "major"}});
    CHECK(daw.call("get_clip", {{"clip", lead}})["followKey"]["steps"] == 1);
    daw.session->undo();
    CHECK(daw.call("get_clip", {{"clip", lead}})["followKey"]["steps"] == -2);
    // Its key set by hand, then back to what's found; none can't be followed.
    r = daw.call("set_audio", {{"clips", {lead}}, {"key", "D major"}});
    CHECK(r["clips"][0]["key"]["name"] == "D major");
    CHECK(r["clips"][0]["keyFound"] == false);
    CHECK(r["clips"][0]["followKey"]["steps"] == 5);
    r = daw.call("set_audio", {{"clips", {lead}}, {"key", "F#m"}});
    CHECK(r["clips"][0]["key"]["name"] == "F# minor");
    r = daw.call("set_audio", {{"clips", {lead}}, {"key", "found"}});
    CHECK(r["clips"][0]["key"]["name"] == "A major");
    CHECK(r["clips"][0]["keyFound"] == true);
    r = daw.call("set_audio", {{"clips", {lead}}, {"key", "none"}});
    CHECK(r["clips"][0]["key"].is_null());
    CHECK(r["clips"][0]["followKey"]["on"] == false);
    CHECK(daw.fails("set_audio", {{"clips", {lead}}, {"key", "Q flat"}}).find("A minor") !=
          std::string::npos);
    // A clip with no key found can't follow: said so.
    r = daw.call("set_audio", {{"clips", {hits}}, {"followKey", true}});
    CHECK(r["clips"][0]["followKey"]["on"] == false);
    CHECK(r["note"].get<std::string>().find(hits) != std::string::npos);
    // In Repitch it follows, but its speed sets its pitch: said so.
    r = daw.call(
        "set_audio",
        {{"clips", {lead}}, {"key", "found"}, {"warp", true}, {"mode", "repitch"}, {"followKey", true}});
    CHECK(r["clips"][0]["followKey"]["on"] == true);
    CHECK(r["clips"][0]["followKey"]["steps"] == 0);
    CHECK(r["note"].get<std::string>().find("Repitch") != std::string::npos);

    // Slice: listed with its settings; the hits cut at their transients, every part listed.
    const auto tools = daw.call("list_audio_tools")["tools"];
    CHECK(std::any_of(tools.begin(), tools.end(), [](const Json& t) { return t["tool"] == "slice"; }));
    r = daw.call("transform_audio",
                 {{"clips", {hits}}, {"tool", "slice"}, {"settings", {{"at", "transients"}}}});
    CHECK(r["changed"] == 1);
    REQUIRE(r["clips"].size() == 4);
    CHECK(r["clips"][0]["id"] == hits);
    CHECK(daw.session->undoDescription() == "Assistant: transform audio");
    daw.session->undo();
    // By bars: a bar at 120 BPM is 2 s, the clip's length: nothing to cut, said so.
    CHECK(daw.fails("transform_audio", {{"clips", {hits}}, {"tool", "slice"}, {"settings", {{"at", "bars"}}}})
              .find("bar") != std::string::npos);
    r = daw.call("transform_audio",
                 {{"clips", {hits}}, {"tool", "slice"}, {"settings", {{"at", "grid"}}}, {"grid", "1/4"}});
    CHECK(r["clips"].size() == 4);
}

TEST_CASE("mcp tools: the Drum Kit: added, samples onto pads, pads set, moved and cleared, modules on pads, "
          "its macros (drum-kit.md §13)",
          "[integration][mcp]")
{
    Daw daw;
    const auto t0 = daw.project()["tracks"][0]["id"].get<std::string>();
    // Listed with the instruments.
    const auto types = daw.call("list_module_types", {{"kind", "instrument"}})["types"];
    CHECK(std::any_of(types.begin(), types.end(), [](const Json& t) { return t["type"] == "drum-kit"; }));

    const auto kit = daw.call("add_module", {{"track", t0}, {"type", "drum-kit"}})["id"].get<std::string>();
    CHECK(daw.call("get_kit", {{"kit", kit}})["pads"].empty());
    const auto sample = [&](const std::string& name, const fd::testkit::AudioBuffer& sound)
    {
        const auto file = daw.folder.getChildFile(name + ".wav");
        REQUIRE(fd::testkit::writeWav(file.getFullPathName().toStdString(), sound, 48000.0));
        return file.getFullPathName().toStdString();
    };
    const auto kick = sample("Kick", fd::testkit::sine(60.0, 48000.0, 9600, 0.5f, 1));
    const auto hat = sample("Hat", fd::testkit::whiteNoise(4800, 2, 0.2f, 1));

    // Samples onto pads from C1 up, one step; each a Sampler.
    auto got = daw.call("load_pads", {{"kit", kit}, {"pad", "C1"}, {"paths", {kick, hat}}});
    CHECK(daw.session->undoDescription() == "Assistant: load pads");
    REQUIRE(got["pads"].size() == 2);
    CHECK(got["pads"][0]["pad"] == "C1");
    CHECK(got["pads"][0]["note"] == 36);
    CHECK(got["pads"][0]["name"] == "Kick");
    CHECK(got["pads"][0]["plays"] == "C3");
    CHECK(got["pads"][0]["modules"][0]["type"] == "sampler");
    CHECK(got["pads"][1]["pad"] == "C\xe2\x99\xaf"
                                   "1");

    // Its settings; the pad by name or number.
    got = daw.call("set_pad", {{"kit", kit}, {"pad", 37}, {"choke", 1}, {"name", "Closed"}, {"plays", "D3"}});
    CHECK(got["pads"][1]["choke"] == 1);
    CHECK(got["pads"][1]["name"] == "Closed");
    CHECK(got["pads"][1]["plays"] == "D3");
    got = daw.call("set_pad", {{"kit", kit}, {"pad", "C1"}, {"pan", "25L"}, {"volumeDb", -6.0}});
    CHECK(got["pads"][0]["pan"] == "25L");
    CHECK(daw.fails("set_pad", {{"kit", kit}, {"pad", "E1"}, {"choke", 2}}).find("empty") !=
          std::string::npos);
    CHECK(daw.fails("set_pad", {{"kit", kit}, {"pad", "H2"}}).find("note") != std::string::npos);
    got = daw.call("set_pad", {{"kit", kit}, {"pad", "C#1"}, {"selected", true}});
    CHECK(got["selectedPad"] == "C\xe2\x99\xaf"
                                "1");

    // A module onto a pad, and moved to another; an instrument onto a pad in place of its own.
    const auto sat =
        daw.call("add_module", {{"track", t0}, {"type", "saturator"}, {"kit", kit}, {"pad", "C1"}});
    got = daw.call("get_kit", {{"kit", kit}});
    REQUIRE(got["pads"][0]["modules"].size() == 2);
    CHECK(got["pads"][0]["modules"][1]["id"] == sat["id"]);
    daw.call("add_module", {{"track", t0}, {"type", "subtractive-synth"}, {"kit", kit}, {"pad", "G1"}});
    got = daw.call("get_kit", {{"kit", kit}});
    REQUIRE(got["pads"].size() == 3);
    CHECK(got["pads"][2]["pad"] == "G1");
    CHECK(got["pads"][2]["name"] == "Subtractive");
    daw.call("move_module", {{"module", sat["id"]}, {"position", 0}, {"kit", kit}, {"pad", "G1"}});
    got = daw.call("get_kit", {{"kit", kit}});
    CHECK(got["pads"][0]["modules"].size() == 1);
    CHECK(got["pads"][2]["modules"][1]["id"] == sat["id"]);
    CHECK(daw.fails("add_module", {{"track", t0}, {"type", "drum-kit"}, {"kit", kit}, {"pad", "A1"}})
              .find("couldn't") != std::string::npos);

    // Moved (swapped), copied, cleared.
    got = daw.call("move_pad", {{"kit", kit}, {"from", "C1"}, {"to", "C#1"}});
    CHECK(got["pads"][0]["name"] == "Closed");
    CHECK(got["pads"][1]["name"] == "Kick");
    got = daw.call("move_pad", {{"kit", kit}, {"from", "C#1"}, {"to", "D1"}, {"copy", true}});
    CHECK(got["pads"].size() == 4);
    got = daw.call("clear_pad", {{"kit", kit}, {"pad", "D1"}});
    CHECK(got["pads"].size() == 3);
    CHECK(daw.fails("clear_pad", {{"kit", kit}, {"pad", "D1"}}).find("empty") != std::string::npos);
    CHECK(daw.fails("get_kit", {{"kit", sat["id"]}}).find("isn't a Drum Kit") != std::string::npos);

    // Its macros (step 2, §10), as a rack's: one maps the Saturator on G1 and the Sampler on C♯1.
    got = daw.call("get_kit", {{"kit", kit}});
    const auto kickSampler = got["pads"][1]["modules"][0]["id"];
    daw.call("map_macro", {{"rack", kit}, {"macro", 1}, {"module", sat["id"]}, {"parameter", "drive"}});
    got =
        daw.call("map_macro", {{"rack", kit}, {"macro", 1}, {"module", kickSampler}, {"parameter", "gain"}});
    CHECK(daw.session->undoDescription() == "Assistant: map macro");
    REQUIRE(got["macros"][0]["mappings"].size() == 2);
    daw.call("set_macro", {{"rack", kit}, {"macro", 1}, {"name", "Punch"}, {"value", 50}});
    got = daw.call("get_kit", {{"kit", kit}});
    CHECK(got["macros"][0]["name"] == "Punch");
    CHECK(got["macros"][0]["mappings"].size() == 2);
    // A rack's alone: chains, snapshots, Morph, Vary and locks.
    CHECK(daw.fails("set_macro", {{"rack", kit}, {"macro", 1}, {"locked", true}}).find("aren't locked") !=
          std::string::npos);
    for (const auto& [tool, args] :
         std::vector<std::pair<std::string, Json>>{{"store_snapshot", {{"rack", kit}}},
                                                   {"set_morph", {{"rack", kit}, {"on", true}}},
                                                   {"add_rack_chain", {{"rack", kit}}},
                                                   {"vary_module", {{"module", kit}}}})
        CHECK(daw.fails(tool, args).find("is a Drum Kit") != std::string::npos);
}

TEST_CASE("mcp tools: a Drum Kit's clip names its pads; notes are written, chosen and generated by pad name "
          "(drum-kit.md §23.5)",
          "[integration][mcp]")
{
    Daw daw;
    const auto t0 = daw.project()["tracks"][0]["id"].get<std::string>();
    const auto kit = daw.call("add_module", {{"track", t0}, {"type", "drum-kit"}})["id"].get<std::string>();
    std::vector<std::string> paths;
    for (const auto* name : {"Kick", "Rim", "Snare"})
    {
        const auto file = daw.folder.getChildFile(juce::String(name) + ".wav");
        REQUIRE(fd::testkit::writeWav(file.getFullPathName().toStdString(),
                                      fd::testkit::sine(80.0, 48000.0, 4800, 0.5f, 1), 48000.0));
        paths.push_back(file.getFullPathName().toStdString());
    }
    daw.call("load_pads", {{"kit", kit}, {"pad", "C1"}, {"paths", paths}});
    // Written by pad name, as by number.
    const auto created =
        daw.call("create_midi_clip", {{"track", t0},
                                      {"start", "1.1.1"},
                                      {"length", "1 bar"},
                                      {"notes",
                                       {{{"pitch", "Kick"}, {"start", 0}, {"length", "1/16"}},
                                        {{"pitch", "snare"}, {"start", 1}, {"length", "1/16"}},
                                        {{"pitch", 40}, {"start", 2}, {"length", "1/16"}}}}});
    const auto clip = created["clip"]["id"].get<std::string>();
    auto got = daw.call("get_clip", {{"clip", clip}});
    REQUIRE(got["pads"].size() == 3);
    CHECK(got["pads"][0] == Json{{"pad", "C1"}, {"note", 36}, {"name", "Kick"}});
    CHECK(got["pads"][2]["name"] == "Snare");
    REQUIRE(got["notes"].size() == 3);
    CHECK(got["notes"][0]["pitch"] == 36);
    CHECK(got["notes"][0]["pad"] == "Kick");
    CHECK(got["notes"][1]["pad"] == "Snare");
    CHECK_FALSE(got["notes"][2].contains("pad")); // E1: no pad there
    // Chosen by pad name.
    daw.call("remove_notes", {{"clip", clip}, {"where", {{"pitch", "Kick"}}}});
    got = daw.call("get_clip", {{"clip", clip}});
    CHECK(got["notes"].size() == 2);
    // Generated on a pad by name: a rhythm on the rim.
    daw.call(
        "generate_notes",
        {{"clip", clip}, {"tool", "rhythm"}, {"settings", {{"pitch", "Rim"}, {"pulses", 4}}}, {"length", 4}});
    got = daw.call("get_clip", {{"clip", clip}});
    CHECK(std::count_if(got["notes"].begin(), got["notes"].end(),
                        [](const Json& n) { return n.value("pad", "") == "Rim"; }) == 4);
    // A name that's neither a note nor a pad says so.
    CHECK(daw.fails("add_notes",
                    {{"clip", clip}, {"notes", {{{"pitch", "Cowbell"}, {"start", 0}, {"length", 1}}}}})
              .find("pads") != std::string::npos);
    // Off a kit's track, a clip has no pads.
    const auto t1 = daw.call("add_track", {})["track"].get<std::string>();
    const auto plain = daw.call("create_midi_clip", {{"track", t1}, {"start", "1.1.1"}, {"length", "1 bar"}});
    CHECK_FALSE(daw.call("get_clip", {{"clip", plain["clip"]["id"]}}).contains("pads"));
}

TEST_CASE("mcp tools: a Granular's sources: a file and a clip's part loaded, read back, its roll kept",
          "[integration][mcp]")
{
    Daw daw("fd-mcp-granular-tests");
    const auto t0 = daw.call("add_track", {{"name", "Clouds"}})["track"];
    const auto g = daw.call("add_module", {{"track", t0}, {"type", "granular"}})["id"];
    CHECK(daw.call("get_granular_source", {{"module", g}})["kind"] == "empty");

    // A sample named for its root (scientific A2: Segno's A1, MIDI 45), into Source 2.
    const auto wav = daw.folder.getChildFile("Pad A2.wav");
    REQUIRE(fd::testkit::writeWav(wav.getFullPathName().toStdString(),
                                  fd::testkit::sine(110.0, 48000.0, 4 * 48000, 0.5f, 1), 48000.0));
    const auto path = wav.getFullPathName().toStdString();
    const auto loaded = daw.call("load_granular_source", {{"module", g}, {"source", 2}, {"path", path}});
    CHECK(loaded["kind"] == "file");
    CHECK(loaded["source"] == 2);
    CHECK(loaded["seconds"] == Approx(4.0));
    CHECK(loaded["root"] == "A1");
    CHECK(loaded["rootFound"] == true);
    CHECK(loaded["part"]["end"] == Approx(4.0));
    CHECK(daw.session->undoDescription() == "Assistant: load granular source");
    const auto module = daw.call("get_module", {{"module", g}});
    CHECK(module["state"]["src2.path"] == path);
    CHECK(daw.call("get_granular_source", {{"module", g}, {"source", 2}})["name"] == "Pad A2");
    CHECK(daw.call("get_granular_source", {{"module", g}})["kind"] == "empty"); // Source 1 untouched
    daw.call("undo");
    CHECK(daw.call("get_granular_source", {{"module", g}, {"source", 2}})["kind"] == "empty");

    // An audio clip's part: the second second of the file (beats at 120 bpm).
    const auto clip =
        daw.call("insert_audio_file", {{"track", t0}, {"start", 0}, {"path", path}})["clip"]["id"];
    daw.call("trim_clip", {{"clip", clip}, {"start", "1.3.1"}, {"end", "2.1.1"}});
    const auto part = daw.call("load_granular_source", {{"module", g}, {"clip", clip}})["part"];
    CHECK(part["start"] == Approx(1.0));
    CHECK(part["end"] == Approx(2.0));

    // What isn't a Granular, or isn't a sound.
    const auto eq = daw.call("add_module", {{"track", t0}, {"type", "eq"}})["id"];
    CHECK(daw.fails("load_granular_source", {{"module", eq}, {"path", path}}).find("isn't a Granular") !=
          std::string::npos);
    const auto text = daw.folder.getChildFile("notes.txt");
    REQUIRE(text.replaceWithText("not a sound"));
    CHECK(daw.fails("load_granular_source", {{"module", g}, {"path", text.getFullPathName().toStdString()}})
              .find("isn't a sound") != std::string::npos);
    CHECK_FALSE(daw.fails("load_granular_source", {{"module", g}}).empty()); // neither path nor clip

    // Its roll (ADR 0062): kept, listed, let go with the song's.
    auto kept = daw.call("keep_roll", {{"module", g}, {"keep", true}});
    REQUIRE(kept["modules"].size() == 1);
    CHECK(kept["modules"][0] == Json{{"id", g}, {"kept", true}});
    CHECK(daw.call("get_module", {{"module", g}})["state"].contains("roll"));
    kept = daw.call("keep_roll", {{"song", true}, {"keep", false}});
    CHECK(kept["modules"][0]["kept"] == false);
    CHECK(daw.fails("keep_roll", {{"module", eq}, {"keep", true}}).find("rolls no dice") !=
          std::string::npos);
}

TEST_CASE("mcp tools: a stretch of time on some rows edited, copied and moved; time inserted, deleted, "
          "duplicated and copied in the whole song (time-selection.md §18)",
          "[integration][mcp]")
{
    Daw daw;
    auto& s = *daw.session;
    s.updateSnapshotNow();
    const auto t0 = s.snapshot()->tracks[0].id, t1 = s.snapshot()->tracks[1].id;
    // A MIDI clip four bars long on each of the first two tracks (16 beats, 8 s at 120 BPM).
    REQUIRE(s.createMidiClip(t0, 0.0, 8.0) != 0);
    REQUIRE(s.createMidiClip(t1, 0.0, 8.0) != 0);
    const auto track0 = fd::mcp::formatId(fd::mcp::IdKind::Track, t0),
               track1 = fd::mcp::formatId(fd::mcp::IdKind::Track, t1);
    auto starts = [&](std::size_t track)
    {
        std::vector<double> out;
        const auto project = daw.project();
        for (const auto& clip : project["tracks"][track]["clips"])
            out.push_back(clip["start"]["beats"].get<double>());
        return out;
    };

    // Bars 2 .. 3 of the first track deleted: the gap stays.
    auto r = daw.call("edit_range",
                      {{"from", "2.1.1"}, {"to", "3.1.1"}, {"rows", {track0}}, {"action", "delete"}});
    CHECK(daw.session->undoDescription() == "Assistant: edit range");
    CHECK(starts(0) == std::vector<double>{0.0, 8.0});
    CHECK(starts(1) == std::vector<double>{0.0});
    // Split on both tracks at bar 4 and bar 5: the parts inside handed back.
    r = daw.call("edit_range",
                 {{"from", "4.1.1"}, {"to", "5.1.1"}, {"rows", {track0, track1}}, {"action", "split"}});
    CHECK(r["clips"].size() == 2);
    CHECK(r["range"]["from"]["bbt"] == "4.1.1");
    // Nothing crosses there: refused, saying why.
    CHECK(
        daw.fails("edit_range", {{"from", "2.1.1"}, {"to", "2.3.1"}, {"rows", {track0}}, {"action", "crop"}})
            .find("edges") != std::string::npos);
    CHECK(daw.fails("edit_range",
                    {{"from", "2.1.1"}, {"to", "1.1.1"}, {"rows", {track0}}, {"action", "delete"}})
              .find("after") != std::string::npos);

    // The first bar of the second track copied onto the first track at bar 2, filling the gap.
    r = daw.call("copy_range",
                 {{"from", "1.1.1"}, {"to", "2.1.1"}, {"rows", {track1}}, {"at", "2.1.1"}, {"onto", track0}});
    CHECK(daw.session->undoDescription() == "Assistant: copy range");
    CHECK(r["clips"].size() == 1);
    CHECK(starts(0) == std::vector<double>{0.0, 4.0, 8.0, 12.0});

    // The whole song: two bars put in at bar 3, then taken out; bar 1 copied to bar 5.
    r = daw.call("edit_time", {{"action", "insert"}, {"at", "3.1.1"}, {"length", "2 bars"}});
    CHECK(daw.session->undoDescription() == "Assistant: edit time");
    CHECK(starts(1).back() == Catch::Approx(20.0));
    daw.call("edit_time", {{"action", "delete"}, {"from", "3.1.1"}, {"to", "5.1.1"}});
    CHECK(starts(1).back() == Catch::Approx(12.0));
    daw.call("edit_time", {{"action", "copy"}, {"from", "1.1.1"}, {"to", "2.1.1"}, {"at", "5.1.1"}});
    CHECK(daw.fails("edit_time", {{"action", "insert"}, {"at", "1.1.1"}}).find("length") !=
          std::string::npos);
}

TEST_CASE(
    "mcp tools: edit_clip_time puts time in, takes it out, duplicates it and splits at it inside a MIDI clip",
    "[integration][mcp]")
{
    Daw daw;
    const auto t0 = daw.project()["tracks"][0]["id"].get<std::string>();
    // A bar from bar 2: C3 on its first beat, E3 over its last two.
    const Json notes = Json::array(
        {{{"pitch", "C3"}, {"start", 0}, {"length", 1}}, {{"pitch", "E3"}, {"start", 2}, {"length", 2}}});
    const auto clip =
        daw.call("create_midi_clip", {{"track", t0}, {"start", "2.1.1"}, {"notes", notes}})["clip"]["id"]
            .get<std::string>();
    const auto shape = [&]
    {
        const auto c = daw.call("get_clip", {{"clip", clip}});
        std::vector<std::pair<double, double>> out;
        for (const auto& n : c["notes"])
            out.emplace_back(n["start"].get<double>(), n["length"].get<double>());
        return std::pair{out, c["length"].get<double>()};
    };
    using Notes = std::vector<std::pair<double, double>>;

    // A beat put in at its second beat: E3 a beat later, the clip a beat longer.
    auto r = daw.call("edit_clip_time",
                      {{"clip", clip}, {"action", "insert"}, {"from", "2.2.1"}, {"to", "2.3.1"}});
    CHECK(r["clip"]["length"] == Catch::Approx(5.0));
    CHECK(daw.session->undoDescription() == "Assistant: edit clip time");
    CHECK(shape() == std::pair{Notes{{0.0, 1.0}, {3.0, 2.0}}, 5.0});
    daw.call("undo");

    // Its third beat taken out: E3 loses the half of it there, the clip a beat shorter.
    daw.call("edit_clip_time", {{"clip", clip}, {"action", "delete"}, {"from", "2.3.1"}, {"to", "2.4.1"}});
    CHECK(shape() == std::pair{Notes{{0.0, 1.0}, {2.0, 1.0}}, 3.0});
    daw.call("undo");

    // Its first beat duplicated: a copy of C3 right after it, E3 and the clip's end later.
    daw.call("edit_clip_time", {{"clip", clip}, {"action", "duplicate"}, {"from", "2.1.1"}, {"to", "2.2.1"}});
    CHECK(shape() == std::pair{Notes{{0.0, 1.0}, {1.0, 1.0}, {3.0, 2.0}}, 5.0});
    daw.call("undo");

    // Split: E3 cut at both edges.
    daw.call("edit_clip_time", {{"clip", clip}, {"action", "split"}, {"from", "2.3.2"}, {"to", "2.4.1"}});
    CHECK(shape() == std::pair{Notes{{0.0, 1.0}, {2.0, 0.25}, {2.25, 0.75}, {3.0, 1.0}}, 4.0});

    // Refused, saying why: outside the clip, nothing to split, an unknown action.
    CHECK(daw.fails("edit_clip_time",
                    {{"clip", clip}, {"action", "insert"}, {"from", "1.1.1"}, {"to", "2.2.1"}})
              .find("isn't inside") != std::string::npos);
    CHECK(
        daw.fails("edit_clip_time", {{"clip", clip}, {"action", "split"}, {"from", "2.2.1"}, {"to", "2.2.3"}})
            .find("No note") != std::string::npos);
    CHECK(daw.fails("edit_clip_time",
                    {{"clip", clip}, {"action", "stretch"}, {"from", "2.1.1"}, {"to", "2.2.1"}})
              .find("one of") != std::string::npos);
}

TEST_CASE("mcp tools: groups made, listed, moved into and out of, coloured, mixed, given modules and "
          "sidechains, exported, bounced, ungrouped and deleted",
          "[integration][mcp]")
{
    Daw daw;
    while (daw.project()["tracks"].size() < 3)
        daw.call("add_track");
    while (daw.project()["tracks"].size() > 3)
        daw.call("delete_tracks", {{"tracks", {daw.project()["tracks"].back()["id"]}}});
    const auto tracks = daw.project()["tracks"];
    const auto t0 = tracks[0]["id"].get<std::string>(), t1 = tracks[1]["id"].get<std::string>(),
               t2 = tracks[2]["id"].get<std::string>();
    daw.call("add_module", {{"track", t0}, {"type", "subtractive-synth"}});
    const Json notes = Json::array({{{"pitch", "C3"}, {"start", 0}, {"length", 2}}});
    daw.call("create_midi_clip", {{"track", t0}, {"start", 0}, {"notes", notes}});

    // Grouped, named and coloured in one step; listed with its kind, members and each one's group.
    const auto made = daw.call("group_tracks", {{"tracks", {t1, t0}}, {"name", "Drums"}, {"colour", 5}});
    const auto drums = made["group"].get<std::string>();
    CHECK(made["name"] == "Drums");
    auto project = daw.project();
    REQUIRE(project["tracks"][0]["id"] == drums);
    CHECK(project["tracks"][0]["kind"] == "group");
    CHECK(project["tracks"][0]["colour"] == 5);
    CHECK(project["tracks"][0]["members"] == Json::array({t0, t1}));
    CHECK(project["tracks"][0]["group"].is_null());
    CHECK(project["tracks"][1]["group"] == drums);
    CHECK(daw.session->canUndo());
    // A group holds no clips; a clip tool says so.
    CHECK(daw.fails("create_midi_clip", {{"track", drums}, {"start", 0}}).find("group") != std::string::npos);

    // Its colour given to its tracks.
    daw.call("set_track", {{"track", drums}, {"colourMembers", true}});
    CHECK(daw.project()["tracks"][1]["colour"] == 5);
    CHECK(daw.fails("set_track", {{"track", t2}, {"colourMembers", true}}).find("isn't a group") !=
          std::string::npos);

    // Moved into Drums at its end, out to the end of everything; a group never into itself.
    daw.call("move_track", {{"track", t2}, {"group", drums}});
    CHECK(daw.project()["tracks"][3]["group"] == drums);
    daw.call("move_track", {{"track", t2}, {"group", nullptr}});
    CHECK(daw.project()["tracks"][3]["group"].is_null());
    CHECK(daw.fails("move_track", {{"track", drums}, {"group", drums}}).find("itself") != std::string::npos);

    // Its mix, a send, effects but no instrument, and a group as a sidechain source.
    const auto ret = daw.call("add_return_track")["track"].get<std::string>();
    daw.call("set_mix", {{"track", drums}, {"volume", -6}});
    daw.call("set_send", {{"track", drums}, {"return", ret}, {"level", -12}});
    project = daw.project();
    CHECK(project["tracks"][0]["mix"]["volume"] == -6.0);
    REQUIRE(project["tracks"][0]["mix"]["sends"].size() == 1);
    CHECK(project["tracks"][0]["mix"]["sends"][0]["level"] == -12.0);
    daw.call("add_module", {{"track", drums}, {"type", "compressor"}});
    CHECK(daw.fails("add_module", {{"track", drums}, {"type", "sampler"}}).find("no notes") !=
          std::string::npos);
    CHECK(daw.fails("add_module", {{"track", drums}, {"type", "arpeggiator"}}).find("MIDI effect") !=
          std::string::npos);
    // Nor does it make the song's harmony (midi-effects.md §5.1): a track's notes do.
    CHECK(daw.fails("set_track_harmony", {{"track", drums}, {"harmony", "on"}}).find("is a group") !=
          std::string::npos);
    for (const auto& t : daw.call("get_song_context")["tracks"])
        CHECK(t["track"] != drums);
    const auto comp =
        daw.call("add_module", {{"track", t2}, {"type", "compressor"}})["id"].get<std::string>();
    daw.call("set_module_sidechain", {{"module", comp}, {"source", drums}});

    // Stems: groups whole (Drums, the third track, the return), or every track and group alone.
    auto r = daw.callAway("export_audio", {{"what", "tracks"}, {"stems", "groups"}});
    INFO(r.dump().substr(0, 2000));
    REQUIRE(r["isError"] == false);
    CHECK(r["structuredContent"]["files"].size() == 3);
    r = daw.callAway("export_audio", {{"what", "tracks"}, {"stems", "tracks"}});
    REQUIRE(r["isError"] == false);
    CHECK(r["structuredContent"]["files"].size() == 5);

    // Bounced to a new track after it, which copies its mix; the group muted.
    r = daw.callAway("bounce_group", {{"group", drums}});
    INFO(r.dump().substr(0, 2000));
    REQUIRE(r["isError"] == false);
    REQUIRE(r["structuredContent"]["tracks"].size() == 1);
    const auto bounced = r["structuredContent"]["tracks"][0].get<std::string>();
    project = daw.project();
    CHECK(project["tracks"][0]["mix"]["mute"] == true);
    CHECK(project["tracks"][3]["id"] == bounced);
    CHECK(project["tracks"][3]["group"].is_null());
    CHECK(project["tracks"][3]["mix"]["volume"] == -6.0);
    CHECK(project["tracks"][3]["clips"].size() == 1);

    // Duplicated with copies of what it holds; ungrouped saying its modules went; deleted with
    // everything in it.
    const auto copy = daw.call("duplicate_track", {{"track", drums}})["track"].get<std::string>();
    CHECK(daw.call("get_project")["tracks"].size() == 8); // Drums, 2 tracks, Drums 2, 2 copies, bounce, t2
    const auto ungrouped = daw.callResult("ungroup_tracks", {{"group", copy}});
    REQUIRE(ungrouped["isError"] == false);
    CHECK(ungrouped["structuredContent"]["tracks"].size() == 2);
    CHECK(ungrouped["content"][0]["text"].get<std::string>().find("went with it") != std::string::npos);
    CHECK(daw.call("delete_tracks", {{"tracks", {drums, t0}}})["deleted"] == 1);
    project = daw.project();
    for (const auto& t : project["tracks"])
    {
        CHECK(t["id"] != drums);
        CHECK(t["id"] != t0);
    }
}

TEST_CASE("mcp tools: MIDI effects are listed as such, and added before the instrument", "[integration][mcp]")
{
    Daw daw;
    const auto types = daw.call("list_module_types", {{"kind", "midi-effect"}})["types"];
    std::vector<std::string> names;
    for (const auto& t : types)
    {
        CHECK(t["kind"] == "midi-effect");
        names.push_back(t["type"].get<std::string>());
    }
    CHECK(names == std::vector<std::string>{"arpeggiator", "chord", "pitch"});
    for (const auto& t : daw.call("list_module_types", {{"kind", "effect"}})["types"])
        CHECK(t["type"] != "pitch");

    const auto t0 = daw.project()["tracks"][0]["id"].get<std::string>();
    const auto synth = daw.call("add_module", {{"track", t0}, {"type", "subtractive-synth"}})["id"];
    const auto pitch = daw.call("add_module", {{"track", t0}, {"type", "pitch"}, {"position", 1}})["id"];
    const auto modules = daw.project()["tracks"][0]["chain"];
    REQUIRE(modules.size() == 2);
    CHECK(modules[0]["id"] == pitch);
    CHECK(modules[1]["id"] == synth);
}

TEST_CASE("mcp tools: what MIDI effects follow, read and set (midi-effects.md §14)", "[integration][mcp]")
{
    Daw daw;
    const auto t0 = daw.project()["tracks"][0]["id"].get<std::string>();
    daw.call("set_key", {{"root", "A"}, {"scale", "minor"}});
    // Am for a bar, then F: a track of chords.
    Json notes = Json::array();
    for (const int p : {57, 60, 64})
        notes.push_back({{"pitch", p}, {"start", 0}, {"length", 4}});
    for (const int p : {53, 57, 60})
        notes.push_back({{"pitch", p}, {"start", 4}, {"length", 4}});
    daw.call("create_midi_clip", {{"track", t0}, {"start", 0}, {"length", 8}, {"notes", notes}});

    auto context = daw.call("get_song_context");
    CHECK(context["key"]["name"] == "A minor");
    CHECK(context["chordsChange"] == "beat");
    REQUIRE(context["chords"].size() == 2);
    CHECK(context["chords"][0]["name"] == "Am");
    CHECK(context["chords"][0]["root"] == "A");
    CHECK(context["chords"][0]["bass"] == "A2");
    CHECK(context["chords"][1]["name"] == "F");
    REQUIRE(context["tracks"].size() >= 1);
    CHECK(context["tracks"][0]["harmony"] == "auto");
    CHECK(context["tracks"][0]["makesChords"] == true);
    // A span: the second bar only.
    CHECK(daw.call("get_song_context", {{"start", 5}, {"length", 2}})["chords"].size() == 1);

    // Off: its notes make no chords; one undo step.
    daw.call("set_track_harmony", {{"track", t0}, {"harmony", "off"}});
    CHECK(daw.session->undoDescription() == "Assistant: set track harmony");
    context = daw.call("get_song_context");
    CHECK(context["chords"].empty());
    CHECK(context["tracks"][0]["makesChords"] == false);
    CHECK(daw.project()["tracks"][0]["harmony"] == "off");
    daw.session->undo();
    daw.session->updateSnapshotNow();
    CHECK(daw.call("get_song_context")["chords"].size() == 2);

    // Chords may change once a bar.
    daw.call("set_chords_change", {{"every", "bar"}});
    CHECK(daw.call("get_song_context")["chordsChange"] == "bar");
    CHECK(daw.fails("set_chords_change", {{"every", "fortnight"}}).find("every") != std::string::npos);
}

TEST_CASE("mcp tools: a MIDI effect's Listen, set and read", "[integration][mcp]")
{
    Daw daw;
    const auto t0 = daw.project()["tracks"][0]["id"].get<std::string>();
    const auto t1 = daw.project()["tracks"][1]["id"].get<std::string>();
    const auto pitch = daw.call("add_module", {{"track", t0}, {"type", "pitch"}})["id"].get<std::string>();
    auto module = daw.call("get_module", {{"module", pitch}});
    CHECK(module["listen"]["track"].is_null());
    bool canHear = false;
    for (const auto& t : module["listen"]["possibleTracks"])
        canHear = canHear || t == t1;
    CHECK(canHear);
    daw.call("set_module_listen", {{"module", pitch}, {"track", t1}});
    CHECK(daw.call("get_module", {{"module", pitch}})["listen"]["track"] == t1);
    CHECK(daw.fails("set_module_listen", {{"module", pitch}, {"track", t0}}).find("own track") !=
          std::string::npos);
    daw.call("set_module_listen", {{"module", pitch}, {"track", nullptr}});
    CHECK(daw.call("get_module", {{"module", pitch}})["listen"]["track"].is_null());
    const auto eq = daw.call("add_module", {{"track", t0}, {"type", "eq"}})["id"].get<std::string>();
    CHECK(daw.fails("set_module_listen", {{"module", eq}, {"track", t1}}).find("MIDI effects") !=
          std::string::npos);
}

TEST_CASE("mcp tools: keep what MIDI effects play as notes", "[integration][mcp]")
{
    Daw daw;
    const auto t0 = daw.project()["tracks"][0]["id"].get<std::string>();
    const auto clip = daw.call("create_midi_clip",
                               {{"track", t0},
                                {"start", 0},
                                {"length", 4},
                                {"notes", {{{"pitch", 60}, {"start", 0}, {"length", 1}}}}})["clip"]["id"];
    const auto pitch = daw.call("add_module", {{"track", t0}, {"type", "pitch"}})["id"].get<std::string>();
    daw.call("set_parameters", {{"module", pitch}, {"values", {{"semitones", 7}}}});
    CHECK(daw.call("keep_as_notes", {{"track", t0}})["clips"].size() == 1);
    CHECK(daw.call("get_clip", {{"clip", clip}})["notes"][0]["pitch"] == 67);
    CHECK(daw.call("get_module", {{"module", pitch}})["on"] == false);
    // Nothing of the kind on another track.
    const auto t1 = daw.project()["tracks"][1]["id"].get<std::string>();
    CHECK(daw.fails("keep_as_notes", {{"track", t1}}).find("MIDI effect") != std::string::npos);
}
