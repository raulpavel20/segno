// The MCP transports (docs/design/mcp.md §1): Streamable HTTP on the loopback interface (token,
// Origin, metadata headers, SSE) and the stdio relay, over fake tools. JUCE-free.

#include "McpTestSupport.h"

#include "mcp/transport/HttpServer.h"
#include "mcp/transport/StdioRelay.h"

#include <httplib.h>

#include <catch2/catch_test_macros.hpp>

#include <future>
#include <sstream>

using namespace fd::mcp;
using namespace fd::mcp::test;

namespace
{
constexpr const char* kToken = "secret-token";

struct Fixture
{
    Registry registry = fakeRegistry();
    Dispatcher dispatcher{identity(), registry};
    HttpServer server{dispatcher};
    int port = 0;

    Fixture()
    {
        dispatcher.setKeepAliveInterval(std::chrono::milliseconds(50));
        HttpOptions options;
        options.port = 0;
        options.token = kToken;
        port = server.start(options);
        REQUIRE(port > 0);
    }
    ~Fixture()
    {
        dispatcher.shutdown();
        server.stop();
    }

    [[nodiscard]] httplib::Client client() const
    {
        httplib::Client c("127.0.0.1", port);
        c.set_read_timeout(std::chrono::seconds(5));
        return c;
    }
};

httplib::Headers modernHeaders(const Json& message)
{
    httplib::Headers h{{"Authorization", std::string("Bearer ") + kToken},
                       {"Accept", "application/json, text/event-stream"},
                       {"MCP-Protocol-Version", modernVersion},
                       {"Mcp-Method", message["method"].get<std::string>()}};
    const auto& params = message["params"];
    if (params.contains("name"))
        h.emplace("Mcp-Name", params["name"].get<std::string>());
    if (params.contains("uri"))
        h.emplace("Mcp-Name", params["uri"].get<std::string>());
    return h;
}

httplib::Result post(httplib::Client& c, const Json& message, const httplib::Headers& headers)
{
    return c.Post("/mcp", headers, message.dump(), "application/json");
}

/// The data of every SSE event in `body`.
std::vector<Json> events(const std::string& body)
{
    std::vector<Json> out;
    std::istringstream in(body);
    for (std::string line; std::getline(in, line);)
        if (line.starts_with("data: "))
            out.push_back(Json::parse(line.substr(6)));
    return out;
}
} // namespace

TEST_CASE("mcp http: token, origin and host checks", "[fast][mcp]")
{
    Fixture f;
    auto c = f.client();
    const auto request = modern("server/discover");

    auto headers = modernHeaders(request);
    headers.erase("Authorization");
    auto res = post(c, request, headers);
    REQUIRE(res);
    CHECK(res->status == 401);
    CHECK(res->get_header_value("WWW-Authenticate").starts_with("Bearer"));

    headers = modernHeaders(request);
    headers.erase("Authorization");
    headers.emplace("Authorization", "Bearer wrong");
    CHECK(post(c, request, headers)->status == 401);

    headers = modernHeaders(request);
    headers.emplace("Origin", "https://evil.example");
    CHECK(post(c, request, headers)->status == 403);
    headers = modernHeaders(request);
    headers.emplace("Origin", "http://localhost:3000");
    CHECK(post(c, request, headers)->status == 200);

    headers = modernHeaders(request);
    headers.emplace("Host", "rebound.example:7377");
    CHECK(post(c, request, headers)->status == 403);

    // A new token works at once; the old one doesn't.
    f.server.setToken("new-token");
    CHECK(post(c, request, modernHeaders(request))->status == 401);
    headers = modernHeaders(request);
    headers.erase("Authorization");
    headers.emplace("Authorization", "Bearer new-token");
    CHECK(post(c, request, headers)->status == 200);
}

TEST_CASE("mcp http: requests, metadata headers and status codes", "[fast][mcp]")
{
    Fixture f;
    auto c = f.client();

    const auto discover = modern("server/discover");
    auto res = post(c, discover, modernHeaders(discover));
    REQUIRE(res);
    CHECK(res->status == 200);
    CHECK(res->get_header_value("Content-Type").starts_with("application/json"));
    CHECK(Json::parse(res->body)["result"]["supportedVersions"][0] == modernVersion);

    const auto call = modern("tools/call", {{"name", "echo"}, {"arguments", {{"text", "hi"}}}});
    res = post(c, call, modernHeaders(call));
    CHECK(Json::parse(res->body)["result"]["structuredContent"]["text"] == "hi");

    // Headers must mirror the body.
    auto headers = modernHeaders(call);
    headers.erase("Mcp-Name");
    headers.emplace("Mcp-Name", "touch");
    res = post(c, call, headers);
    CHECK(res->status == 400);
    CHECK(Json::parse(res->body)["error"]["code"] == codes::headerMismatch);
    headers.erase("Mcp-Name");
    headers.emplace("Mcp-Name", "=?base64?ZWNobw==?="); // "echo", encoded
    CHECK(post(c, call, headers)->status == 200);
    headers = modernHeaders(call);
    headers.erase("MCP-Protocol-Version");
    CHECK(Json::parse(post(c, call, headers)->body)["error"]["code"] == codes::headerMismatch);
    headers = modernHeaders(call);
    headers.erase("Mcp-Method");
    headers.emplace("Mcp-Method", "tools/list");
    CHECK(post(c, call, headers)->status == 400);
    headers = modernHeaders(discover);
    auto legacyBody = legacy("tools/list");
    CHECK(Json::parse(post(c, legacyBody, headers)->body)["error"]["code"] == codes::headerMismatch);

    auto old = modern("tools/list");
    old["params"]["_meta"]["io.modelcontextprotocol/protocolVersion"] = "1900-01-01";
    headers = modernHeaders(old);
    headers.erase("MCP-Protocol-Version");
    headers.emplace("MCP-Protocol-Version", "1900-01-01");
    res = post(c, old, headers);
    CHECK(res->status == 400);
    CHECK(Json::parse(res->body)["error"]["code"] == codes::unsupportedVersion);

    const auto unknown = modern("no/such");
    res = post(c, unknown, modernHeaders(unknown));
    CHECK(res->status == 404);
    CHECK(Json::parse(res->body)["error"]["code"] == codes::methodNotFound);

    res = c.Post("/mcp", modernHeaders(discover), "{not json", "application/json");
    CHECK(res->status == 400);
    CHECK(Json::parse(res->body)["error"]["code"] == codes::parseError);
    CHECK(c.Post("/mcp", modernHeaders(discover), discover.dump(), "text/plain")->status == 415);

    const Json note{{"jsonrpc", "2.0"}, {"method", "notifications/initialized"}};
    res = post(c, note, {{"Authorization", std::string("Bearer ") + kToken}});
    CHECK(res->status == 202);
    CHECK(res->body.empty());

    const httplib::Headers auth{{"Authorization", std::string("Bearer ") + kToken}};
    CHECK(c.Get("/mcp", auth)->status == 405);
    CHECK(c.Delete("/mcp", auth)->status == 405);
}

TEST_CASE("mcp http: progress and subscriptions stream as SSE", "[fast][mcp]")
{
    Fixture f;

    auto call = modern("tools/call", {{"name", "slow"}, {"arguments", Json::object()}});
    call["params"]["_meta"]["progressToken"] = "p";
    auto c = f.client();
    auto res = post(c, call, modernHeaders(call));
    REQUIRE(res);
    CHECK(res->get_header_value("Content-Type").starts_with("text/event-stream"));
    CHECK(res->get_header_value("X-Accel-Buffering") == "no");
    auto got = events(res->body);
    REQUIRE(got.size() == 4);
    CHECK(got[0]["method"] == "notifications/progress");
    CHECK(got[3]["result"]["structuredContent"]["steps"] == 3);

    const auto listen = modern("subscriptions/listen", {{"notifications", {{"resourceSubscriptions", {"test://a"}}}}}, 9);
    auto listener = std::async(std::launch::async, [&] {
        auto lc = f.client();
        return post(lc, listen, modernHeaders(listen));
    });
    for (int i = 0; i < 200 && !f.dispatcher.watchedResources().contains("test://a"); ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    f.dispatcher.resourceUpdated("test://a");
    std::this_thread::sleep_for(std::chrono::milliseconds(120)); // a keep-alive or two
    f.dispatcher.shutdown();
    res = listener.get();
    REQUIRE(res);
    got = events(res->body);
    REQUIRE(got.size() == 3);
    CHECK(got[0]["method"] == "notifications/subscriptions/acknowledged");
    CHECK(got[1]["params"]["uri"] == "test://a");
    CHECK(got[1]["params"]["_meta"]["io.modelcontextprotocol/subscriptionId"] == 9);
    CHECK(got[2]["result"]["_meta"]["io.modelcontextprotocol/subscriptionId"] == 9);
    CHECK(res->body.find(":\n\n") != std::string::npos);
}

TEST_CASE("mcp http: legacy sessions", "[fast][mcp]")
{
    Fixture f;
    auto c = f.client();
    const httplib::Headers auth{{"Authorization", std::string("Bearer ") + kToken}};
    auto res = post(c, legacy("initialize", {{"protocolVersion", "2025-11-25"}, {"capabilities", Json::object()}}), auth);
    REQUIRE(res);
    CHECK(res->status == 200);
    const auto session = res->get_header_value("Mcp-Session-Id");
    REQUIRE_FALSE(session.empty());
    CHECK(Json::parse(res->body)["result"]["protocolVersion"] == "2025-11-25");

    auto withSession = auth;
    withSession.emplace("Mcp-Session-Id", session);
    withSession.emplace("MCP-Protocol-Version", "2025-11-25");
    res = post(c, legacy("tools/list"), withSession);
    CHECK(Json::parse(res->body)["result"]["tools"].size() == f.registry.tools.size());

    CHECK(c.Delete("/mcp", withSession)->status == 200);
    CHECK(c.Delete("/mcp", withSession)->status == 404);
    CHECK(post(c, legacy("tools/list"), withSession)->status == 404);
    CHECK(c.Get("/mcp", withSession)->status == 404);
}

TEST_CASE("mcp http: two servers never share a port", "[fast][mcp]")
{
    Fixture first;
    Registry registry = fakeRegistry();
    Dispatcher d(identity(), registry);
    HttpServer second(d);
    HttpOptions options;
    options.port = first.port;
    options.portAttempts = 5;
    const int port = second.start(options);
    CHECK(port > first.port);
    CHECK(port < first.port + 5);
    d.shutdown();
}

TEST_CASE("mcp stdio relay: lines in, answers out", "[fast][mcp]")
{
    Fixture f;
    const RelayTarget target{"127.0.0.1", f.port, kToken, "/mcp"};
    CHECK(probe(target));
    CHECK_FALSE(probe({"127.0.0.1", f.port, kToken, "/elsewhere"}));

    std::istringstream in(modern("server/discover", Json::object(), 1).dump() + "\n\n" +
                          modern("tools/call", {{"name", "echo"}, {"arguments", {{"text", "relayed"}}}}, 2).dump() + "\n" +
                          "not json\n" + modern("resources/read", {{"uri", "test://item/3"}}, 3).dump() + "\n");
    std::ostringstream out;
    relay(target, in, out);

    std::map<int, Json> answers;
    int parseErrors = 0;
    std::istringstream lines(out.str());
    for (std::string line; std::getline(lines, line);)
    {
        const auto m = Json::parse(line);
        if (m.contains("id"))
            answers[m["id"].get<int>()] = m;
        else if (m["error"]["code"] == -32700)
            ++parseErrors;
    }
    CHECK(parseErrors == 1);
    REQUIRE(answers.size() == 3);
    CHECK(answers[1]["result"]["supportedVersions"][0] == modernVersion);
    CHECK(answers[2]["result"]["structuredContent"]["text"] == "relayed");
    CHECK(answers[3]["result"]["contents"][0]["uri"] == "test://item/3");

    // A legacy client through the relay: the session is kept for later requests.
    std::istringstream legacyIn(legacy("initialize", {{"protocolVersion", "2025-11-25"}}, 1).dump() + "\n");
    std::ostringstream legacyOut;
    relay(target, legacyIn, legacyOut);
    CHECK(Json::parse(legacyOut.str())["result"]["protocolVersion"] == "2025-11-25");

    // Nothing listening: each request gets an error saying so.
    std::istringstream lonelyIn(modern("ping", Json::object(), 5).dump() + "\n");
    std::ostringstream lonelyOut;
    relay({"127.0.0.1", 1, kToken, "/mcp"}, lonelyIn, lonelyOut);
    CHECK(Json::parse(lonelyOut.str())["error"]["message"].get<std::string>().find("isn't answering") != std::string::npos);
}
