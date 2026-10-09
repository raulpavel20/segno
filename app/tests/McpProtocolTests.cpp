// The MCP protocol layer (ADR 0021, docs/design/mcp.md §2): both protocol eras, tools, resources,
// prompts, confirmations, subscriptions and errors, over fake tools. JUCE-free.

#include "McpTestSupport.h"

#include "mcp/protocol/Encoding.h"
#include "mcp/protocol/Schema.h"

#include <catch2/catch_test_macros.hpp>

#include <future>

using namespace fd::mcp;
using namespace fd::mcp::test;

namespace
{
Json resultOf(const Outcome& o)
{
    REQUIRE(o.message);
    REQUIRE(o.message->contains("result"));
    return (*o.message)["result"];
}

int errorCode(const Outcome& o)
{
    REQUIRE(o.message);
    REQUIRE(o.message->contains("error"));
    return (*o.message)["error"]["code"].get<int>();
}

Json callParams(const std::string& name, Json args = Json::object())
{
    return {{"name", name}, {"arguments", std::move(args)}};
}
} // namespace

TEST_CASE("mcp: server/discover lists the versions and capabilities", "[fast][mcp]")
{
    const auto registry = fakeRegistry();
    Dispatcher d(identity(), registry);
    const auto out = d.handle(modern("server/discover"), {});
    CHECK(out.status == 200);
    const auto r = resultOf(out);
    CHECK(r["resultType"] == "complete");
    CHECK(r["supportedVersions"][0] == modernVersion);
    CHECK(std::find(r["supportedVersions"].begin(), r["supportedVersions"].end(), "2025-11-25") !=
          r["supportedVersions"].end());
    CHECK(r["capabilities"]["tools"].is_object());
    CHECK(r["capabilities"]["resources"]["subscribe"] == true);
    CHECK(r["capabilities"]["prompts"].is_object());
    CHECK(r["instructions"] == "Test instructions.");
    CHECK(r["_meta"]["io.modelcontextprotocol/serverInfo"]["name"] == "segno-test");
    CHECK((*out.message)["id"] == 1);
}

TEST_CASE("mcp: request metadata is checked", "[fast][mcp]")
{
    const auto registry = fakeRegistry();
    Dispatcher d(identity(), registry);

    auto request = modern("tools/list");
    request["params"]["_meta"]["io.modelcontextprotocol/protocolVersion"] = "1900-01-01";
    auto out = d.handle(request, {});
    CHECK(out.status == 400);
    CHECK(errorCode(out) == codes::unsupportedVersion);
    CHECK((*out.message)["error"]["data"]["requested"] == "1900-01-01");
    CHECK((*out.message)["error"]["data"]["supported"][0] == modernVersion);

    request = modern("tools/list");
    request["params"]["_meta"].erase("io.modelcontextprotocol/clientCapabilities");
    out = d.handle(request, {});
    CHECK(out.status == 400);
    CHECK(errorCode(out) == codes::invalidParams);

    out = d.handle(modern("no/such/method"), {});
    CHECK(out.status == 404);
    CHECK(errorCode(out) == codes::methodNotFound);
    CHECK(errorCode(d.handle(modern("resources/subscribe", {{"uri", "test://a"}}), {})) == codes::methodNotFound);
    CHECK(errorCode(d.handle(legacy("server/discover"), {})) == codes::methodNotFound);

    // The plan gives the same answers without running anything.
    request = modern("tools/list");
    request["params"]["_meta"]["io.modelcontextprotocol/protocolVersion"] = "1900-01-01";
    CHECK(d.plan(request, {}).immediate);
    CHECK_FALSE(d.plan(modern("tools/list"), {}).immediate);
    CHECK(d.plan(modern("subscriptions/listen"), {}).stream);
    CHECK_FALSE(d.plan(modern("tools/call", callParams("echo", {{"text", "x"}})), {}).stream);
    auto withProgress = modern("tools/call", callParams("slow"));
    withProgress["params"]["_meta"]["progressToken"] = "p1";
    CHECK(d.plan(withProgress, {}).stream);
}

TEST_CASE("mcp: malformed messages, notifications and client responses", "[fast][mcp]")
{
    const auto registry = fakeRegistry();
    Dispatcher d(identity(), registry);
    CHECK(d.handle(Json::array(), {}).status == 400);
    CHECK(errorCode(d.handle(Json{{"jsonrpc", "2.0"}, {"id", 1}}, {})) == codes::invalidRequest);
    CHECK(errorCode(d.handle(Json{{"jsonrpc", "2.0"}, {"id", nullptr}, {"method", "ping"}}, {})) == codes::invalidRequest);
    CHECK(errorCode(d.handle(Json{{"jsonrpc", "2.0"}, {"id", 3}, {"method", "ping"}, {"params", 5}}, {})) ==
          codes::invalidParams);

    const auto note = d.handle(Json{{"jsonrpc", "2.0"}, {"method", "notifications/initialized"}}, {});
    CHECK(note.status == 202);
    CHECK_FALSE(note.message);
    const auto response = d.handle(Json{{"jsonrpc", "2.0"}, {"id", "x"}, {"result", Json::object()}}, {});
    CHECK(response.status == 202);
    CHECK_FALSE(response.message);

    const auto ping = d.handle(modern("ping", Json::object(), "abc"), {});
    CHECK((*ping.message)["id"] == "abc");
    CHECK(resultOf(ping)["resultType"] == "complete");
}

TEST_CASE("mcp: tools list with schemas and honest annotations", "[fast][mcp]")
{
    const auto registry = fakeRegistry();
    Dispatcher d(identity(), registry);
    const auto tools = resultOf(d.handle(modern("tools/list"), {}))["tools"];
    REQUIRE(tools.size() == registry.tools.size());
    CHECK(tools[0]["name"] == "echo");
    CHECK(tools[0]["inputSchema"]["type"] == "object");
    CHECK(tools[0]["outputSchema"]["required"][0] == "text");
    CHECK(tools[0]["annotations"]["readOnlyHint"] == true);
    CHECK(tools[0]["annotations"]["idempotentHint"] == true);
    CHECK(tools[2]["annotations"]["destructiveHint"] == true);
    CHECK(tools[1]["annotations"]["readOnlyHint"] == false);
    CHECK_FALSE(tools[1].contains("outputSchema"));
}

TEST_CASE("mcp: tool calls: results, argument errors, unknown tools, read-only mode", "[fast][mcp]")
{
    const auto registry = fakeRegistry();
    Dispatcher d(identity(), registry);

    const auto ok = resultOf(d.handle(modern("tools/call", callParams("echo", {{"text", "hi"}})), {}));
    CHECK(ok["isError"] == false);
    CHECK(ok["structuredContent"]["text"] == "hi");
    CHECK(Json::parse(ok["content"].back()["text"].get<std::string>()) == ok["structuredContent"]);
    CHECK(validate(registry.tools[0].outputSchema, ok["structuredContent"]) == std::nullopt);

    const auto summary = resultOf(d.handle(modern("tools/call", callParams("touch")), {}));
    CHECK(summary["content"][0]["text"] == "Touched it.");
    CHECK(summary["content"].size() == 2);

    // Argument problems are tool errors the model can act on.
    const auto bad = resultOf(d.handle(modern("tools/call", callParams("echo", {{"text", 3}})), {}));
    CHECK(bad["isError"] == true);
    CHECK(bad["content"][0]["text"].get<std::string>().find("text: expected a string") != std::string::npos);
    const auto missing = resultOf(d.handle(modern("tools/call", callParams("echo")), {}));
    CHECK(missing["content"][0]["text"].get<std::string>().find("missing \"text\"") != std::string::npos);
    const auto extra = resultOf(d.handle(modern("tools/call", callParams("echo", {{"text", "a"}, {"loud", true}})), {}));
    CHECK(extra["content"][0]["text"].get<std::string>().find("Unknown argument \"loud\"") != std::string::npos);

    CHECK(errorCode(d.handle(modern("tools/call", callParams("nope")), {})) == codes::invalidParams);
    CHECK(errorCode(d.handle(modern("tools/call", {{"arguments", Json::object()}}), {})) == codes::invalidParams);

    d.setReadOnly(true);
    const auto refused = resultOf(d.handle(modern("tools/call", callParams("touch")), {}));
    CHECK(refused["isError"] == true);
    CHECK(refused["content"][0]["text"].get<std::string>().find("read-only") != std::string::npos);
    CHECK(resultOf(d.handle(modern("tools/call", callParams("echo", {{"text", "still"}})), {}))["isError"] == false);
}

TEST_CASE("mcp: confirmations use multi round-trip requests", "[fast][mcp]")
{
    const auto registry = fakeRegistry();
    Dispatcher d(identity(), registry);
    const Json elicitation{{"elicitation", {{"form", Json::object()}}}};
    const Json args{{"what", "all"}};

    const auto asked = resultOf(d.handle(modern("tools/call", callParams("reset", args), 1, elicitation), {}));
    CHECK(asked["resultType"] == "input_required");
    CHECK(asked["inputRequests"]["confirm"]["method"] == "elicitation/create");
    CHECK(asked["inputRequests"]["confirm"]["params"]["message"] == "This throws everything away.");
    CHECK(asked["inputRequests"]["confirm"]["params"]["requestedSchema"]["type"] == "object");
    const auto state = asked["requestState"].get<std::string>();

    auto retry = [&](const std::string& s, const std::string& action, Json a, int id) {
        auto params = callParams("reset", std::move(a));
        params["requestState"] = s;
        params["inputResponses"] = {{"confirm", {{"action", action}}}};
        return d.handle(modern("tools/call", params, id, elicitation), {});
    };
    // Changed arguments don't match the state: asked again.
    CHECK(resultOf(retry(state, "accept", {{"what", "less"}}, 2))["resultType"] == "input_required");

    const auto again = resultOf(d.handle(modern("tools/call", callParams("reset", args), 3, elicitation), {}));
    const auto done = resultOf(retry(again["requestState"].get<std::string>(), "accept", args, 4));
    CHECK(done["resultType"] == "complete");
    CHECK(done["structuredContent"]["reset"] == true);

    // State is used once.
    CHECK(resultOf(retry(again["requestState"].get<std::string>(), "accept", args, 5))["resultType"] == "input_required");

    const auto third = resultOf(d.handle(modern("tools/call", callParams("reset", args), 6, elicitation), {}));
    const auto declined = resultOf(retry(third["requestState"].get<std::string>(), "decline", args, 7));
    CHECK(declined["isError"] == true);
    CHECK(declined["content"][0]["text"].get<std::string>().find("declined") != std::string::npos);

    // Tampered state: asked again, never run.
    CHECK(resultOf(retry("0123456789abcdef", "accept", args, 8))["resultType"] == "input_required");
}

TEST_CASE("mcp: clients without elicitation confirm with an argument", "[fast][mcp]")
{
    const auto registry = fakeRegistry();
    Dispatcher d(identity(), registry);
    const auto asked = resultOf(d.handle(modern("tools/call", callParams("reset")), {}));
    CHECK(asked["isError"] == true);
    CHECK(asked["content"][0]["text"].get<std::string>().find("\"confirm\": true") != std::string::npos);
    const auto done = resultOf(d.handle(modern("tools/call", callParams("reset", {{"confirm", true}})), {}));
    CHECK(done["structuredContent"]["reset"] == true);

    // With elicitation the user confirms in the client: the argument alone isn't enough.
    const Json elicitation{{"elicitation", Json::object()}};
    CHECK(resultOf(d.handle(modern("tools/call", callParams("reset", {{"confirm", true}}), 2, elicitation), {}))
              ["resultType"] == "input_required");
}

TEST_CASE("mcp: legacy clients: initialize, sessions, and results without result types", "[fast][mcp]")
{
    const auto registry = fakeRegistry();
    Dispatcher d(identity(), registry);
    const auto init = d.handle(legacy("initialize", {{"protocolVersion", "2025-11-25"},
                                                     {"capabilities", Json::object()},
                                                     {"clientInfo", {{"name", "old"}, {"version", "1"}}}}),
                               {});
    const auto r = resultOf(init);
    CHECK(r["protocolVersion"] == "2025-11-25");
    CHECK(r["serverInfo"]["name"] == "segno-test");
    CHECK(r["capabilities"]["tools"].is_object());
    CHECK_FALSE(r.contains("resultType"));
    REQUIRE_FALSE(init.newSession.empty());
    CHECK(d.hasSession(init.newSession));

    const auto older = d.handle(legacy("initialize", {{"protocolVersion", "2025-06-18"}}), {});
    CHECK(resultOf(older)["protocolVersion"] == "2025-06-18");
    const auto unknown = d.handle(legacy("initialize", {{"protocolVersion", "2024-01-01"}}), {});
    CHECK(resultOf(unknown)["protocolVersion"] == "2025-11-25");

    const auto list = d.handle(legacy("tools/list"), {init.newSession});
    CHECK(resultOf(list)["tools"].size() == registry.tools.size());
    CHECK_FALSE(resultOf(list).contains("resultType"));
    CHECK(d.handle(legacy("tools/list"), {"no-such-session"}).status == 404);

    CHECK(resultOf(d.handle(legacy("resources/subscribe", {{"uri", "test://a"}}), {init.newSession})).is_object());
    CHECK(d.watchedResources().contains("test://a"));
    CHECK(errorCode(d.handle(legacy("resources/subscribe", {{"uri", "test://a"}}), {})) == codes::invalidRequest);
    CHECK(d.endSession(init.newSession));
    CHECK_FALSE(d.hasSession(init.newSession));
    CHECK_FALSE(d.watchedResources().contains("test://a"));
}

TEST_CASE("mcp: legacy clients confirm through elicitation/create on the stream", "[fast][mcp]")
{
    const auto registry = fakeRegistry();
    Dispatcher d(identity(), registry);
    const auto init = d.handle(
        legacy("initialize", {{"protocolVersion", "2025-11-25"}, {"capabilities", {{"elicitation", Json::object()}}}}), {});
    const Incoming in{init.newSession};
    CHECK(d.plan(legacy("tools/call", callParams("reset")), in).stream);

    for (const auto* action : {"accept", "decline"})
    {
        RecordingStream stream;
        stream.onSend = [&](const Json& m) {
            if (m.value("method", "") == "elicitation/create")
                d.handle({{"jsonrpc", "2.0"}, {"id", m["id"]}, {"result", {{"action", action}}}}, {});
        };
        const auto out = d.handle(legacy("tools/call", callParams("reset")), in, &stream);
        const auto sent = stream.waitFor(1);
        REQUIRE(sent.size() == 1);
        CHECK(sent[0]["params"]["message"] == "This throws everything away.");
        if (std::string(action) == "accept")
            CHECK(resultOf(out)["structuredContent"]["reset"] == true);
        else
            CHECK(resultOf(out)["isError"] == true);
    }

    // Nobody answers: the call gives up without doing anything.
    d.setClientRequestTimeout(std::chrono::milliseconds(50));
    RecordingStream silent;
    CHECK(resultOf(d.handle(legacy("tools/call", callParams("reset")), in, &silent))["isError"] == true);
}

TEST_CASE("mcp: progress notifications go on the request's stream", "[fast][mcp]")
{
    const auto registry = fakeRegistry();
    Dispatcher d(identity(), registry);
    auto request = modern("tools/call", callParams("slow"));
    request["params"]["_meta"]["progressToken"] = 42;
    RecordingStream stream;
    const auto out = d.handle(request, {}, &stream);
    const auto sent = stream.waitFor(3);
    REQUIRE(sent.size() == 3);
    CHECK(sent[0]["method"] == "notifications/progress");
    CHECK(sent[0]["params"]["progressToken"] == 42);
    CHECK(sent[2]["params"]["progress"] == 3);
    CHECK(sent[2]["params"]["total"] == 3);
    CHECK(sent[1]["params"]["message"] == "step 2");
    CHECK(resultOf(out)["structuredContent"]["steps"] == 3);
}

TEST_CASE("mcp: resources and templates", "[fast][mcp]")
{
    const auto registry = fakeRegistry();
    Dispatcher d(identity(), registry);
    CHECK(resultOf(d.handle(modern("resources/list"), {}))["resources"][0]["uri"] == "test://a");
    CHECK(resultOf(d.handle(modern("resources/templates/list"), {}))["resourceTemplates"][0]["uriTemplate"] ==
          "test://item/{n}");
    const auto read = resultOf(d.handle(modern("resources/read", {{"uri", "test://item/7"}}), {}));
    CHECK(read["contents"][0]["uri"] == "test://item/7");
    CHECK(read["contents"][0]["mimeType"] == "application/json");
    CHECK(Json::parse(read["contents"][0]["text"].get<std::string>())["uri"] == "test://item/7");

    CHECK(errorCode(d.handle(modern("resources/read", {{"uri", "test://none"}}), {})) == codes::invalidParams);
    CHECK(errorCode(d.handle(legacy("resources/read", {{"uri", "test://none"}}), {})) == codes::legacyResourceNotFound);
    CHECK(errorCode(d.handle(modern("resources/read"), {})) == codes::invalidParams);
}

TEST_CASE("mcp: prompts", "[fast][mcp]")
{
    const auto registry = fakeRegistry();
    Dispatcher d(identity(), registry);
    const auto list = resultOf(d.handle(modern("prompts/list"), {}))["prompts"];
    CHECK(list[0]["name"] == "greet");
    CHECK(list[0]["arguments"][0]["required"] == true);
    const auto got = resultOf(d.handle(modern("prompts/get", {{"name", "greet"}, {"arguments", {{"name", "Ana"}}}}), {}));
    CHECK(got["messages"][0]["content"]["text"] == "Hello Ana");
    CHECK(errorCode(d.handle(modern("prompts/get", {{"name", "greet"}}), {})) == codes::invalidParams);
    CHECK(errorCode(d.handle(modern("prompts/get", {{"name", "nope"}}), {})) == codes::invalidParams);
}

TEST_CASE("mcp: subscriptions/listen delivers resource updates until closed", "[fast][mcp]")
{
    const auto registry = fakeRegistry();
    Dispatcher d(identity(), registry);
    d.setKeepAliveInterval(std::chrono::milliseconds(20));
    std::vector<std::string> watched;
    std::mutex watchedMutex;
    d.onWatch = [&](const std::vector<std::string>& uris) {
        std::lock_guard lock(watchedMutex);
        watched.insert(watched.end(), uris.begin(), uris.end());
    };

    SECTION("the client closes the stream")
    {
        RecordingStream stream;
        auto done = std::async(std::launch::async, [&] {
            return d.handle(modern("subscriptions/listen",
                                   {{"notifications", {{"resourceSubscriptions", {"test://a"}}, {"toolsListChanged", true}}}},
                                   7),
                            {}, &stream);
        });
        auto sent = stream.waitFor(1);
        REQUIRE(sent.size() == 1);
        CHECK(sent[0]["method"] == "notifications/subscriptions/acknowledged");
        CHECK(sent[0]["params"]["_meta"][Dispatcher::subscriptionIdKey] == 7);
        CHECK(sent[0]["params"]["notifications"]["resourceSubscriptions"][0] == "test://a");
        CHECK_FALSE(sent[0]["params"]["notifications"].contains("toolsListChanged")); // never sent
        CHECK(d.watchedResources().contains("test://a"));
        {
            std::lock_guard lock(watchedMutex);
            CHECK(watched == std::vector<std::string>{"test://a"});
        }

        d.resourceUpdated("test://other");
        d.resourceUpdated("test://a");
        sent = stream.waitFor(2);
        REQUIRE(sent.size() == 2);
        CHECK(sent[1]["method"] == "notifications/resources/updated");
        CHECK(sent[1]["params"]["uri"] == "test://a");
        CHECK(sent[1]["params"]["_meta"][Dispatcher::subscriptionIdKey] == 7);

        std::this_thread::sleep_for(std::chrono::milliseconds(60));
        CHECK(stream.keepAlives() >= 1);
        stream.close();
        d.resourceUpdated("test://a"); // wakes it
        const auto out = done.get();
        CHECK_FALSE(out.message);
        CHECK(d.watchedResources().empty());
    }
    SECTION("the server shuts down: a closing response")
    {
        RecordingStream stream;
        auto done = std::async(std::launch::async, [&] {
            return d.handle(modern("subscriptions/listen", {{"notifications", {{"resourceSubscriptions", {"test://a"}}}}}, "s1"),
                            {}, &stream);
        });
        stream.waitFor(1);
        d.shutdown();
        const auto out = done.get();
        const auto r = resultOf(out);
        CHECK(r["resultType"] == "complete");
        CHECK(r["_meta"][Dispatcher::subscriptionIdKey] == "s1");
        CHECK(d.handle(modern("ping"), {}).status == 503);
    }
}

TEST_CASE("mcp: legacy session streams carry resource updates", "[fast][mcp]")
{
    const auto registry = fakeRegistry();
    Dispatcher d(identity(), registry);
    d.setKeepAliveInterval(std::chrono::milliseconds(20));
    const auto init = d.handle(legacy("initialize", {{"protocolVersion", "2025-11-25"}}), {});
    resultOf(d.handle(legacy("resources/subscribe", {{"uri", "test://a"}}), {init.newSession}));

    RecordingStream stream;
    auto served = std::async(std::launch::async, [&] { return d.serveSessionStream(init.newSession, stream); });
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    d.resourceUpdated("test://a");
    const auto sent = stream.waitFor(1);
    REQUIRE(sent.size() == 1);
    CHECK(sent[0]["method"] == "notifications/resources/updated");
    CHECK_FALSE(sent[0]["params"].contains("_meta"));
    d.endSession(init.newSession);
    CHECK(served.get());
    RecordingStream other;
    CHECK_FALSE(d.serveSessionStream("nope", other));
}

TEST_CASE("mcp: schema validation of tool arguments", "[fast][mcp]")
{
    const Json schema{{"type", "object"},
                      {"properties",
                       {{"pitch", {{"anyOf", {{{"type", "integer"}, {"minimum", 0}, {"maximum", 127}}, {{"type", "string"}}}}}},
                        {"notes",
                         {{"type", "array"},
                          {"minItems", 1},
                          {"items", {{"type", "object"}, {"required", {"start"}}, {"properties", {{"start", {{"type", "number"}}}}}}}}},
                        {"mode", {{"enum", {"all", "selection"}}}},
                        {"flag", {{"type", "boolean"}}}}},
                      {"required", {"pitch"}},
                      {"additionalProperties", false}};
    CHECK(validate(schema, {{"pitch", 60}}) == std::nullopt);
    CHECK(validate(schema, {{"pitch", "C3"}}) == std::nullopt);
    CHECK(validate(schema, {{"pitch", 60.0}}) == std::nullopt); // an integral number is an integer
    CHECK(validate(schema, {{"pitch", 200}}).value().find("pitch") != std::string::npos);
    CHECK(validate(schema, {{"pitch", 1}, {"notes", Json::array()}}).value() == "notes: needs at least 1 item");
    CHECK(validate(schema, {{"pitch", 1}, {"notes", {{{"start", "x"}}}}}).value() ==
          "notes[0].start: expected a number, got a string");
    CHECK(validate(schema, {{"pitch", 1}, {"notes", {Json::object()}}}).value() == "notes[0]: missing \"start\"");
    CHECK(validate(schema, {{"pitch", 1}, {"mode", "some"}}).value() == "mode: must be one of \"all\", \"selection\"");
    CHECK(validate(schema, {{"pitch", 1}, {"flag", 1}}).value() == "flag: expected a boolean, got a number");
    CHECK(validate(schema, Json::object()).value() == "missing \"pitch\"");
    CHECK(validate(schema, {{"pitch", 1}, {"x", 1}}).value() == "Unknown argument \"x\"");
    CHECK(validate({{"type", {"number", "string"}}}, true).value() == "The arguments: expected a number or a string, got a boolean");

    CHECK(unsupportedKeywords(schema).empty());
    CHECK(unsupportedKeywords({{"type", "string"}, {"pattern", "^a"}}) == std::vector<std::string>{"pattern"});
    CHECK(unsupportedKeywords({{"properties", {{"a", {{"$ref", "#/x"}}}}}}) == std::vector<std::string>{"$ref"});
}

TEST_CASE("mcp: encodings", "[fast][mcp]")
{
    CHECK(base64Encode(std::string_view("Hello, 世界")) == "SGVsbG8sIOS4lueVjA==");
    CHECK(base64Decode("SGVsbG8sIOS4lueVjA==").value() == "Hello, 世界");
    CHECK(base64Decode("IHBhZGRlZCA").value() == " padded ");
    CHECK_FALSE(base64Decode("@@@"));
    CHECK(decodeHeaderValue("us-west1").value() == "us-west1");
    CHECK(decodeHeaderValue("=?base64?bGluZTEKbGluZTI=?=").value() == "line1\nline2");
    CHECK(encodeHeaderValue("get_project") == "get_project");
    CHECK(encodeHeaderValue(" padded ") == "=?base64?IHBhZGRlZCA=?=");
    CHECK(encodeHeaderValue("=?base64?literal?=") == "=?base64?PT9iYXNlNjQ/bGl0ZXJhbD89?=");
    CHECK(decodeHeaderValue(encodeHeaderValue("segno://track/t:1")).value() == "segno://track/t:1");
    CHECK(randomHex(16).size() == 32);
    CHECK(randomHex(16) != randomHex(16));
    CHECK(constantTimeEquals("abc", "abc"));
    CHECK_FALSE(constantTimeEquals("abc", "abd"));
    CHECK_FALSE(constantTimeEquals("abc", "abcd"));
}
