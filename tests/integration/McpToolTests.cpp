// The MCP tools against a real Session (ADR 0021: every DAW feature is driven through MCP in a
// test). An in-process client calls the dispatcher as a transport would; each edit must be one
// named undo step, read-only mode must hold, and confirmations must be asked.

#include "engine/DevicePlugin.h"
#include "engine/Session.h"
#include "mcp/McpServer.h"
#include "mcp/protocol/Encoding.h"
#include "mcp/protocol/Schema.h"
#include "fd/testkit/Signals.h"
#include "fd/testkit/WavIO.h"

#include <tracktion_engine/tracktion_engine.h>

#include <httplib.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <future>
#include <regex>

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
    tracktion::Engine engine{"fd-mcp-tests"};
    std::unique_ptr<fd::engine::Session> session;
    juce::TemporaryFile folderName;
    juce::File folder;
    std::unique_ptr<fd::mcp::McpServer> server;

    Daw()
    {
        fd::engine::registerDevices(engine);
        session = std::make_unique<fd::engine::Session>(engine);
        folder = folderName.getFile();
        REQUIRE(folder.createDirectory());
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
    for (const auto* name :
         {"get_project", "get_clip", "get_module", "list_module_types", "play", "stop", "set_position", "set_loop",
          "set_metronome", "set_tempo", "set_time_signature", "add_track", "create_midi_clip", "insert_audio_file",
          "insert_midi_file", "move_clips", "trim_clip", "duplicate_clips", "split_clips", "delete_clips", "add_notes",
          "set_notes", "remove_notes", "edit_notes", "add_module", "remove_module", "move_module", "set_module_on",
          "set_parameters", "load_sample", "set_module_view", "scan_plugins", "render", "undo", "redo",
          "save_project", "new_project", "open_project"})
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
    CHECK(created["addedInstrument"] == true);
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

    // One undo takes back the clip, its notes and the instrument it brought.
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
    CHECK(daw.project()["tracks"][0]["chain"].size() == 2);

    const auto reference = daw.read("segno://module-type/saturator");
    CHECK_FALSE(reference["description"].get<std::string>().empty());
    CHECK_FALSE(reference["sections"].empty());
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

    // Saving: to a new file, then over another existing one only when confirmed.
    const auto path = daw.folder.getChildFile("song").getFullPathName().toStdString();
    const auto saved = daw.call("save_project", {{"path", path}});
    CHECK(saved["file"] == path + ".segno");
    CHECK(daw.project()["unsaved"] == false);
    const auto other = daw.folder.getChildFile("other.segno");
    REQUIRE(other.replaceWithText("x"));
    CHECK(daw.fails("save_project", {{"path", other.getFullPathName().toStdString()}}).find("Overwrite") != std::string::npos);
    CHECK(daw.call("save_project", {{"path", other.getFullPathName().toStdString()}, {"confirm", true}})["file"] ==
          other.getFullPathName().toStdString());
    const auto opened = daw.call("open_project", {{"path", path + ".segno"}});
    CHECK(opened["file"] == path + ".segno");
}

TEST_CASE("mcp tools: render measures a range without touching the project", "[integration][mcp]")
{
    Daw daw;
    const auto t0 = daw.project()["tracks"][0]["id"].get<std::string>();
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
