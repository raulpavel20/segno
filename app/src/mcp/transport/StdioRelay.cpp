#include "mcp/transport/StdioRelay.h"

#include "mcp/protocol/Encoding.h"
#include "mcp/protocol/Registry.h"

#include <httplib.h>

#include <atomic>
#include <istream>
#include <map>
#include <memory>
#include <mutex>
#include <ostream>
#include <thread>

namespace fd::mcp
{
namespace
{
const Json& member(const Json& object, const char* key)
{
    static const Json none;
    if (!object.is_object())
        return none;
    const auto it = object.find(key);
    return it == object.end() ? none : *it;
}

std::string textOf(const Json& object, const char* key)
{
    const auto& v = member(object, key);
    return v.is_string() ? v.get<std::string>() : std::string();
}

/// Splits an SSE byte stream into the data of each event (comments and other fields ignored).
class SseParser
{
public:
    template <typename OnData>
    void feed(std::string_view bytes, OnData&& onData)
    {
        for (char c : bytes)
            if (c != '\r')
                buffer_ += c;
        for (auto end = buffer_.find("\n\n"); end != std::string::npos; end = buffer_.find("\n\n"))
        {
            const auto event = buffer_.substr(0, end + 1);
            buffer_.erase(0, end + 2);
            std::string data;
            bool any = false;
            for (size_t start = 0, next = 0; start < event.size(); start = next + 1)
            {
                next = event.find('\n', start);
                const auto line = std::string_view(event).substr(start, next - start);
                if (!line.starts_with("data:"))
                    continue;
                auto value = line.substr(5);
                if (value.starts_with(' '))
                    value.remove_prefix(1);
                if (any)
                    data += '\n';
                data += value;
                any = true;
            }
            if (any)
                onData(data);
        }
    }

private:
    std::string buffer_;
};

struct Flight
{
    std::atomic<bool> cancelled{false};
};

class Relay final : public std::enable_shared_from_this<Relay>
{
public:
    Relay(RelayTarget target, std::ostream& out) : target_(std::move(target)), out_(out) {}

    void run(std::istream& in)
    {
        std::string line;
        while (std::getline(in, line))
        {
            if (!line.empty() && line.back() == '\r')
                line.pop_back();
            if (line.find_first_not_of(" \t") == std::string::npos)
                continue;
            auto message = Json::parse(line, nullptr, false);
            if (message.is_discarded() || !message.is_object())
            {
                writeError(nullptr, -32700, "Parse error: each line must be one JSON-RPC message");
                continue;
            }
            if (textOf(message, "method") == "notifications/cancelled")
            {
                cancel(member(member(message, "params"), "requestId"));
                continue; // on HTTP, closing the stream is the cancellation
            }
            auto flight = std::make_shared<Flight>();
            const bool request = message.contains("method") && message.contains("id");
            if (request)
            {
                std::lock_guard lock(mutex_);
                flights_[message["id"].dump()] = flight;
            }
            std::thread([self = shared_from_this(), message = std::move(message), flight, request] {
                self->send(message, *flight);
                if (request)
                {
                    std::lock_guard lock(self->mutex_);
                    self->flights_.erase(message["id"].dump());
                }
            }).detach();
        }

        // The client has gone: stop what's in flight, end a legacy session, and return.
        stopping_ = true;
        {
            std::lock_guard lock(mutex_);
            for (const auto& [id, flight] : flights_)
                flight->cancelled = true;
        }
        for (int i = 0; i < 40; ++i)
        {
            {
                std::lock_guard lock(mutex_);
                if (flights_.empty())
                    break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        std::string session;
        {
            std::lock_guard lock(mutex_);
            session = session_;
        }
        if (!session.empty())
        {
            auto client = makeClient();
            client.set_read_timeout(std::chrono::seconds(2));
            (void)client.Delete(target_.path,
                                httplib::Headers{{"Authorization", "Bearer " + target_.token}, {"Mcp-Session-Id", session}});
        }
    }

private:
    httplib::Client makeClient() const
    {
        httplib::Client client(target_.host, target_.port);
        client.set_connection_timeout(std::chrono::seconds(2));
        client.set_read_timeout(std::chrono::hours(1)); // long renders; subscriptions keep alive
        client.set_write_timeout(std::chrono::seconds(30));
        return client;
    }

    httplib::Headers headersFor(const Json& message) const
    {
        httplib::Headers h{{"Authorization", "Bearer " + target_.token},
                           {"Accept", "application/json, text/event-stream"}};
        const auto method = textOf(message, "method");
        const auto& params = member(message, "params");
        const auto version = textOf(member(params, "_meta"), "io.modelcontextprotocol/protocolVersion");
        if (!version.empty())
        {
            h.emplace("MCP-Protocol-Version", version);
            h.emplace("Mcp-Method", method);
            const char* field = method == "tools/call" || method == "prompts/get" ? "name"
                                : method == "resources/read"                    ? "uri"
                                                                                : nullptr;
            if (field != nullptr)
                h.emplace("Mcp-Name", encodeHeaderValue(textOf(params, field)));
            return h;
        }
        std::lock_guard lock(mutex_);
        if (!legacyVersion_.empty())
            h.emplace("MCP-Protocol-Version", legacyVersion_);
        if (!session_.empty())
            h.emplace("Mcp-Session-Id", session_);
        return h;
    }

    /// POSTs one message; for a request, writes everything that comes back.
    void send(const Json& message, Flight& flight)
    {
        const auto& id = member(message, "id");
        const bool request = message.contains("method") && message.contains("id");
        auto client = makeClient();
        auto handle = client.open_stream("POST", target_.path, {}, headersFor(message), message.dump(), "application/json");
        if (!handle.is_valid())
        {
            if (request)
                writeError(id, -32603,
                           "The DAW isn't answering on port " + std::to_string(target_.port) + ". Is it still running?");
            return;
        }
        const int status = handle.response->status;
        const auto type = handle.response->get_header_value("Content-Type");
        const auto session = handle.response->get_header_value("Mcp-Session-Id");
        bool answered = false;
        const auto deliver = [&](const std::string& data) {
            auto m = Json::parse(data, nullptr, false);
            if (m.is_discarded())
                return;
            if (request && member(m, "id") == id && (m.contains("result") || m.contains("error")))
            {
                answered = true;
                if (textOf(message, "method") == "initialize" && m.contains("result"))
                    learnSession(session, textOf(m["result"], "protocolVersion"));
            }
            write(m);
        };

        char buffer[8192];
        if (type.find("text/event-stream") != std::string::npos)
        {
            SseParser parser;
            while (!flight.cancelled)
            {
                const auto n = handle.read(buffer, sizeof buffer);
                if (n <= 0)
                    break;
                parser.feed(std::string_view(buffer, static_cast<size_t>(n)), deliver);
            }
        }
        else
        {
            std::string body;
            for (auto n = handle.read(buffer, sizeof buffer); n > 0; n = handle.read(buffer, sizeof buffer))
                body.append(buffer, static_cast<size_t>(n));
            if (!body.empty() && !Json::parse(body, nullptr, false).is_discarded())
                deliver(body);
            else if (request && status >= 400)
            {
                writeError(id, -32603, "The DAW answered HTTP " + std::to_string(status) + ": " + body.substr(0, 300));
                answered = true;
            }
        }
        if (request && !answered && !flight.cancelled && !stopping_)
            writeError(id, -32603, "The DAW closed the connection before answering");
    }

    /// A legacy client's session: later requests carry it, and its notifications stream back.
    void learnSession(const std::string& session, const std::string& version)
    {
        {
            std::lock_guard lock(mutex_);
            session_ = session;
            legacyVersion_ = version;
        }
        if (session.empty())
            return;
        std::thread([self = shared_from_this(), session, version] {
            auto client = self->makeClient();
            const httplib::Headers h{{"Authorization", "Bearer " + self->target_.token},
                                     {"Accept", "text/event-stream"},
                                     {"Mcp-Session-Id", session},
                                     {"MCP-Protocol-Version", version}};
            auto handle = client.open_stream("GET", self->target_.path, {}, h);
            if (!handle.is_valid() || handle.response->status != 200)
                return;
            SseParser parser;
            char buffer[8192];
            while (!self->stopping_)
            {
                const auto n = handle.read(buffer, sizeof buffer);
                if (n <= 0)
                    break;
                parser.feed(std::string_view(buffer, static_cast<size_t>(n)), [&](const std::string& data) {
                    if (auto m = Json::parse(data, nullptr, false); !m.is_discarded())
                        self->write(m);
                });
            }
        }).detach();
    }

    void cancel(const Json& requestId)
    {
        std::lock_guard lock(mutex_);
        if (const auto it = flights_.find(requestId.dump()); it != flights_.end())
            it->second->cancelled = true;
    }

    void write(const Json& message)
    {
        std::lock_guard lock(outMutex_);
        out_ << message.dump() << '\n';
        out_.flush();
    }

    void writeError(const Json& id, int code, const std::string& text)
    {
        Json m{{"jsonrpc", "2.0"}, {"error", {{"code", code}, {"message", text}}}};
        if (id.is_string() || id.is_number_integer())
            m["id"] = id;
        write(m);
    }

    const RelayTarget target_;
    std::ostream& out_;
    std::mutex outMutex_;
    mutable std::mutex mutex_;
    std::map<std::string, std::shared_ptr<Flight>> flights_;
    std::string session_, legacyVersion_;
    std::atomic<bool> stopping_{false};
};
} // namespace

bool probe(const RelayTarget& target, std::chrono::milliseconds timeout)
{
    if (target.port <= 0)
        return false;
    httplib::Client client(target.host, target.port);
    client.set_connection_timeout(timeout);
    client.set_read_timeout(timeout);
    const auto res = client.Get(target.path, httplib::Headers{{"Authorization", "Bearer " + target.token}});
    return res && res->status == 405; // our endpoint answers a plain GET with 405
}

void relay(const RelayTarget& target, std::istream& in, std::ostream& out)
{
    std::make_shared<Relay>(target, out)->run(in);
}
} // namespace fd::mcp
