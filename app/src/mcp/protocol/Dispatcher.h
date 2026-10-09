#pragma once

#include "mcp/protocol/Registry.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <vector>

namespace fd::mcp
{
/// The revision served statelessly (per-request metadata, server/discover).
inline constexpr const char* modernVersion = "2026-07-28";
/// Every version served: the modern one, then the legacy ones (an `initialize` handshake).
[[nodiscard]] const std::vector<std::string>& supportedVersions();

namespace codes
{
inline constexpr int parseError = -32700;
inline constexpr int invalidRequest = -32600;
inline constexpr int methodNotFound = -32601;
inline constexpr int invalidParams = -32602;
inline constexpr int internalError = -32603;
inline constexpr int headerMismatch = -32020;
inline constexpr int unsupportedVersion = -32022;
inline constexpr int legacyResourceNotFound = -32002; // 2025-11-25 and earlier
} // namespace codes

struct ServerIdentity
{
    std::string name;
    std::string title;
    std::string version;
    std::string instructions;
};

/// A request's response stream (SSE on HTTP): notifications before the response, the
/// notifications of a subscription, and (legacy clients) requests to the client.
class Stream
{
public:
    virtual ~Stream() = default;
    /// Sends one message; false once the client has gone.
    virtual bool send(const Json& message) = 0;
    /// Keeps a quiet stream alive (an SSE comment); false once the client has gone.
    virtual bool keepAlive() = 0;
    [[nodiscard]] virtual bool isOpen() const = 0;
};

/// What the transport knows about a message beyond its body.
struct Incoming
{
    std::string session; // a legacy session id (HTTP Mcp-Session-Id), if any
};

/// The answer to one message.
struct Outcome
{
    std::optional<Json> message; // the JSON-RPC response; none for notifications
    int status = 200;            // the HTTP status to send it with
    std::string newSession;      // set when `initialize` opened a legacy session
};

/// What the transport must prepare before a request runs.
struct Plan
{
    std::optional<Outcome> immediate; // the answer is already known (a bad request)
    bool stream = false;              // the request needs a response stream
};

/// Serves the MCP protocol over any transport: JSON-RPC 2.0 messages in, responses out, for
/// the 2026-07-28 revision (stateless, each request carries its metadata) and the 2025-11-25
/// one (an `initialize` handshake and sessions). Thread-safe: transports call it from their
/// own threads, many at a time. See docs/design/mcp.md.
class Dispatcher
{
public:
    Dispatcher(ServerIdentity identity, const Registry& registry);
    ~Dispatcher();
    Dispatcher(const Dispatcher&) = delete;
    Dispatcher& operator=(const Dispatcher&) = delete;

    /// Checks a message without running it: the transport learns whether the answer is already
    /// known (with its HTTP status) and whether the request wants a response stream.
    [[nodiscard]] Plan plan(const Json& message, const Incoming& incoming) const;

    /// Handles one message: a request, a notification, or a client's response to one of our
    /// requests. Blocks until the request is done; a subscription blocks until its stream
    /// closes. `stream` may be null when the transport answers with a single JSON object.
    Outcome handle(const Json& message, const Incoming& incoming, Stream* stream = nullptr);

    [[nodiscard]] bool hasSession(const std::string& session) const;
    /// A legacy session's notification stream (HTTP GET); blocks while it's open. False if
    /// the session doesn't exist.
    bool serveSessionStream(const std::string& session, Stream& stream);
    /// Ends a legacy session (HTTP DELETE). False if it doesn't exist.
    bool endSession(const std::string& session);

    /// A resource's content changed: tells its subscribers.
    void resourceUpdated(const std::string& uri);
    /// The resource URIs subscribed to now.
    [[nodiscard]] std::set<std::string> watchedResources() const;
    /// Called (any thread) when a subscription starts watching resource URIs.
    std::function<void(const std::vector<std::string>& uris)> onWatch;
    /// Called (any thread) for every request: the client's info and the method.
    std::function<void(const Json& clientInfo, const std::string& method)> onActivity;

    /// Read-only mode: only tools annotated read-only run.
    void setReadOnly(bool readOnly) noexcept { readOnly_ = readOnly; }
    [[nodiscard]] bool isReadOnly() const noexcept { return readOnly_; }

    /// How often a quiet stream gets a keep-alive (tests shorten it).
    void setKeepAliveInterval(std::chrono::milliseconds interval) { keepAlive_ = interval; }
    /// How long to wait for a legacy client's answer to one of our requests.
    void setClientRequestTimeout(std::chrono::milliseconds timeout) { clientTimeout_ = timeout; }

    /// Ends every subscription (with its closing response) and stops waiting for clients;
    /// requests after this fail. Call before the transports stop.
    void shutdown();

    static constexpr const char* subscriptionIdKey = "io.modelcontextprotocol/subscriptionId";

private:
    struct Caller
    {
        bool modern = true;
        std::string version;
        Json clientInfo = Json::object();
        Json capabilities = Json::object();
        std::string session;
    };
    struct Subscription
    {
        Json id;            // the subscriptions/listen request's id; null for a legacy session
        bool modern = true; // modern notifications carry the subscription id
        std::set<std::string> resources;
        std::deque<Json> queue;
        bool ended = false;
        bool streaming = false; // a legacy session's stream is open
    };
    struct Session
    {
        std::string version;
        Json clientInfo = Json::object();
        Json capabilities = Json::object();
        std::shared_ptr<Subscription> subscription;
    };
    struct Confirmation
    {
        std::string tool;
        Json arguments;
        std::chrono::steady_clock::time_point expires;
    };
    struct ClientRequest
    {
        std::optional<Json> answer;
        bool done = false;
    };

    [[nodiscard]] std::optional<Outcome> resolve(const Json& message, const Incoming& incoming, Caller& caller) const;
    [[nodiscard]] Json result(const Json& id, Json body, const Caller& caller) const;
    [[nodiscard]] static Outcome error(const Json& id, int code, const std::string& message, int status = 200,
                                       Json data = nullptr);
    [[nodiscard]] static bool knownMethod(const std::string& method, bool modern);
    [[nodiscard]] static bool supportsElicitation(const Caller& caller);
    [[nodiscard]] Json capabilities() const;
    [[nodiscard]] Json serverInfo() const;

    Outcome initialize(const Json& id, const Json& params);
    Outcome callTool(const Json& id, const Json& params, const Caller& caller, Stream* stream);
    Outcome readResource(const Json& id, const Json& params, const Caller& caller);
    Outcome getPrompt(const Json& id, const Json& params, const Caller& caller);
    Outcome listen(const Json& id, const Json& params, const Caller& caller, Stream* stream);
    Outcome subscribe(const Json& id, const Json& params, const Caller& caller, bool add);
    void handleClientResponse(const Json& message);
    /// Sends `method` to a legacy client on `stream` and waits for its answer.
    std::optional<Json> askClient(Stream& stream, const std::string& method, Json params);
    /// Delivers a subscription's notifications until the stream closes (false) or the
    /// subscription is ended by the server (true).
    bool pump(Subscription& subscription, Stream& stream);
    void addSubscription(const std::shared_ptr<Subscription>& subscription);
    void removeSubscription(const std::shared_ptr<Subscription>& subscription);

    ServerIdentity identity_;
    const Registry& registry_;
    std::atomic<bool> readOnly_{false};
    std::chrono::milliseconds keepAlive_{15000};
    std::chrono::milliseconds clientTimeout_{std::chrono::minutes(10)};

    mutable std::mutex mutex_;
    std::condition_variable wake_;
    bool shutDown_ = false;
    std::map<std::string, Session> sessions_;
    std::vector<std::shared_ptr<Subscription>> subscriptions_;
    std::map<std::string, Confirmation> confirmations_;
    std::map<std::string, std::shared_ptr<ClientRequest>> clientRequests_;
    std::uint64_t nextRequest_ = 1;
};
} // namespace fd::mcp
