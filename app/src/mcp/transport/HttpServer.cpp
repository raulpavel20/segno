#include "mcp/transport/HttpServer.h"

#include "mcp/protocol/Encoding.h"

#include <httplib.h>

namespace fd::mcp
{
namespace
{
/// An SSE response stream over cpp-httplib's chunked writer.
class SseStream final : public Stream
{
public:
    explicit SseStream(httplib::DataSink& sink) : sink_(sink) {}

    bool send(const Json& message) override { return write("event: message\ndata: " + message.dump() + "\n\n"); }
    bool keepAlive() override { return write(":\n\n"); }
    [[nodiscard]] bool isOpen() const override
    {
        std::lock_guard lock(mutex_);
        return open_ && sink_.is_writable();
    }
    void close()
    {
        std::lock_guard lock(mutex_);
        open_ = false;
    }

private:
    bool write(const std::string& text)
    {
        std::lock_guard lock(mutex_);
        if (open_ && !sink_.write(text.data(), text.size()))
            open_ = false;
        return open_;
    }

    httplib::DataSink& sink_;
    mutable std::mutex mutex_;
    bool open_ = true;
};

/// The host part of "host:port", "[::1]:port" or a bare host.
std::string hostOf(std::string_view hostPort)
{
    if (hostPort.starts_with('['))
    {
        const auto end = hostPort.find(']');
        return std::string(hostPort.substr(0, end == std::string_view::npos ? hostPort.size() : end + 1));
    }
    return std::string(hostPort.substr(0, hostPort.find(':')));
}

bool isLoopbackName(const std::string& host)
{
    return host == "localhost" || host == "127.0.0.1" || host == "[::1]";
}

/// DNS rebinding (Streamable HTTP, security): browsers send an Origin; only local pages pass.
bool allowedOrigin(const std::string& origin)
{
    if (origin.empty())
        return true; // not a browser
    const auto scheme = origin.find("://");
    return scheme != std::string::npos && isLoopbackName(hostOf(std::string_view(origin).substr(scheme + 3)));
}

bool allowedHost(const std::string& host)
{
    return host.empty() || isLoopbackName(hostOf(host));
}

const Json& member(const Json& object, const char* key)
{
    static const Json none;
    if (!object.is_object())
        return none;
    const auto it = object.find(key);
    return it == object.end() ? none : *it;
}

/// The request metadata headers must match the body (Streamable HTTP, server validation).
std::optional<std::string> headerProblem(const httplib::Request& req, const Json& message)
{
    const auto& params = member(message, "params");
    const auto& version = member(member(params, "_meta"), "io.modelcontextprotocol/protocolVersion");
    const auto header = req.get_header_value("MCP-Protocol-Version");
    if (!version.is_string())
    {
        if (header == modernVersion)
            return std::string("The MCP-Protocol-Version header says ") + modernVersion +
                   " but the body has no _meta protocol version";
        return std::nullopt; // a legacy client: its headers aren't mirrored
    }
    if (!req.has_header("MCP-Protocol-Version"))
        return "Missing the MCP-Protocol-Version header";
    if (header != version.get<std::string>())
        return "Header mismatch: MCP-Protocol-Version '" + header + "' doesn't match the body's '" +
               version.get<std::string>() + "'";
    const auto method = member(message, "method").get<std::string>();
    if (!req.has_header("Mcp-Method"))
        return "Missing the Mcp-Method header";
    if (req.get_header_value("Mcp-Method") != method)
        return "Header mismatch: Mcp-Method '" + req.get_header_value("Mcp-Method") + "' doesn't match the body's '" +
               method + "'";
    const char* nameField = method == "tools/call" || method == "prompts/get" ? "name"
                            : method == "resources/read"                    ? "uri"
                                                                            : nullptr;
    if (nameField != nullptr)
    {
        if (!req.has_header("Mcp-Name"))
            return "Missing the Mcp-Name header";
        const auto name = decodeHeaderValue(req.get_header_value("Mcp-Name"));
        if (!name)
            return "Malformed Mcp-Name header";
        const auto& body = member(params, nameField);
        if (!body.is_string() || *name != body.get<std::string>())
            return "Header mismatch: Mcp-Name '" + *name + "' doesn't match the body";
    }
    return std::nullopt;
}

void reply(httplib::Response& res, const Outcome& outcome)
{
    res.status = outcome.status;
    if (!outcome.newSession.empty())
        res.set_header("Mcp-Session-Id", outcome.newSession);
    if (outcome.message)
        res.set_content(outcome.message->dump(), "application/json");
}

Json errorMessage(const Json& id, int code, const std::string& message)
{
    Json m{{"jsonrpc", "2.0"}, {"error", {{"code", code}, {"message", message}}}};
    if (id.is_string() || id.is_number_integer())
        m["id"] = id;
    return m;
}
} // namespace

struct HttpServer::Impl
{
    httplib::Server server;
    std::mutex tokenMutex;
    std::string token;

    /// Origin, Host and token checks; false (with the response set) when the request is refused.
    bool admit(const httplib::Request& req, httplib::Response& res)
    {
        if (!allowedOrigin(req.get_header_value("Origin")) || !allowedHost(req.get_header_value("Host")))
        {
            res.status = 403;
            res.set_content(errorMessage(nullptr, codes::invalidRequest, "Forbidden origin").dump(), "application/json");
            return false;
        }
        std::string expected;
        {
            std::lock_guard lock(tokenMutex);
            expected = token;
        }
        if (!expected.empty() && !constantTimeEquals(httplib::get_bearer_token_auth(req), expected))
        {
            res.status = 401;
            res.set_header("WWW-Authenticate", "Bearer realm=\"segno\"");
            res.set_content("This needs the DAW's assistant token: open the Assistants popover in the DAW and copy "
                            "the setup line.",
                            "text/plain");
            return false;
        }
        return true;
    }
};

HttpServer::HttpServer(Dispatcher& dispatcher) : dispatcher_(dispatcher) {}

HttpServer::~HttpServer()
{
    stop();
}

void HttpServer::setToken(std::string token)
{
    if (impl_ != nullptr)
    {
        std::lock_guard lock(impl_->tokenMutex);
        impl_->token = std::move(token);
    }
}

int HttpServer::start(const HttpOptions& options)
{
    stop();
    // A failed bind retires a cpp-httplib server, so each port gets a fresh one.
    int bound = 0;
    for (int attempt = 0; attempt < std::max(1, options.portAttempts) && bound <= 0; ++attempt)
    {
        impl_ = std::make_unique<Impl>();
        impl_->token = options.token;
        configure(options);
        if (options.port == 0)
            bound = impl_->server.bind_to_any_port(options.host);
        else if (impl_->server.bind_to_port(options.host, options.port + attempt))
            bound = options.port + attempt;
        if (options.port == 0)
            break;
    }
    if (bound <= 0)
    {
        impl_.reset();
        return 0;
    }
    port_ = bound;
    thread_ = std::thread([this] { impl_->server.listen_after_bind(); });
    impl_->server.wait_until_ready();
    return bound;
}

void HttpServer::configure(const HttpOptions& options)
{
    auto& server = impl_->server;
    // One process per port: no SO_REUSEPORT (cpp-httplib's default), so a second DAW moves on to
    // the next port instead of sharing this one.
    server.set_socket_options([](socket_t sock) {
#ifdef _WIN32
        httplib::set_socket_opt(sock, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, 1);
#else
        httplib::set_socket_opt(sock, SOL_SOCKET, SO_REUSEADDR, 1);
#endif
    });
    server.set_payload_max_length(64u * 1024u * 1024u);
    server.set_read_timeout(30);
    server.set_write_timeout(30);

    server.Post(options.path, [this](const httplib::Request& req, httplib::Response& res) {
        if (!impl_->admit(req, res))
            return;
        if (req.get_header_value("Content-Type").find("application/json") == std::string::npos)
        {
            res.status = 415;
            res.set_content(errorMessage(nullptr, codes::invalidRequest, "Send JSON (Content-Type: application/json)").dump(),
                            "application/json");
            return;
        }
        const auto message = Json::parse(req.body, nullptr, false);
        if (message.is_discarded())
        {
            reply(res, {errorMessage(nullptr, codes::parseError, "The body isn't valid JSON"), 400, {}});
            return;
        }
        if (message.is_object() && message.contains("method") && message["method"].is_string() &&
            message.contains("id"))
            if (auto problem = headerProblem(req, message))
            {
                reply(res, {errorMessage(message["id"], codes::headerMismatch, *problem), 400, {}});
                return;
            }

        Incoming incoming;
        const bool modern =
            member(member(member(message, "params"), "_meta"), "io.modelcontextprotocol/protocolVersion").is_string();
        if (!modern)
            incoming.session = req.get_header_value("Mcp-Session-Id");
        const auto plan = dispatcher_.plan(message, incoming);
        if (plan.immediate)
        {
            reply(res, *plan.immediate);
            return;
        }
        if (!plan.stream)
        {
            reply(res, dispatcher_.handle(message, incoming, nullptr));
            return;
        }
        res.set_header("Cache-Control", "no-cache");
        res.set_header("X-Accel-Buffering", "no");
        res.set_chunked_content_provider("text/event-stream",
                                         [this, message, incoming](size_t, httplib::DataSink& sink) {
                                             SseStream stream(sink);
                                             const auto outcome = dispatcher_.handle(message, incoming, &stream);
                                             if (outcome.message)
                                                 stream.send(*outcome.message);
                                             stream.close();
                                             sink.done();
                                             return true;
                                         });
    });

    // Legacy clients (2025-11-25): a session's notification stream, and ending a session.
    // Modern clients use neither: 405 (Streamable HTTP, backward compatibility).
    server.Get(options.path, [this](const httplib::Request& req, httplib::Response& res) {
        if (!impl_->admit(req, res))
            return;
        const auto session = req.get_header_value("Mcp-Session-Id");
        if (session.empty())
        {
            res.status = 405;
            res.set_header("Allow", "POST");
            return;
        }
        if (!dispatcher_.hasSession(session))
        {
            res.status = 404;
            return;
        }
        res.set_header("Cache-Control", "no-cache");
        res.set_header("X-Accel-Buffering", "no");
        res.set_chunked_content_provider("text/event-stream", [this, session](size_t, httplib::DataSink& sink) {
            SseStream stream(sink);
            dispatcher_.serveSessionStream(session, stream);
            stream.close();
            sink.done();
            return true;
        });
    });
    server.Delete(options.path, [this](const httplib::Request& req, httplib::Response& res) {
        if (!impl_->admit(req, res))
            return;
        const auto session = req.get_header_value("Mcp-Session-Id");
        if (session.empty())
        {
            res.status = 405;
            res.set_header("Allow", "POST");
            return;
        }
        res.status = dispatcher_.endSession(session) ? 200 : 404;
    });

}

void HttpServer::stop()
{
    if (impl_ != nullptr)
        impl_->server.stop();
    if (thread_.joinable())
        thread_.join();
    impl_.reset();
    port_ = 0;
}
} // namespace fd::mcp
