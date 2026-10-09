#pragma once

#include "mcp/protocol/Dispatcher.h"

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace fd::mcp
{
struct HttpOptions
{
    std::string host = "127.0.0.1"; // loopback only (mcp.md §1)
    int port = 7377;                 // the first port to try; 0 = any free port (tests)
    int portAttempts = 20;           // then the next ones
    std::string path = "/mcp";
    std::string token; // every request needs "Authorization: Bearer <token>"; empty = no check (tests)
};

/// The Streamable HTTP transport (2026-07-28, and the 2025-11-25 form for legacy clients):
/// one endpoint taking POSTed JSON-RPC messages, answering with JSON or an SSE stream.
/// Checks the token, the Origin and Host headers (DNS rebinding) and the request metadata
/// headers. Serves on its own threads. JUCE-free (cpp-httplib).
class HttpServer
{
public:
    explicit HttpServer(Dispatcher& dispatcher);
    /// Stops; shut the dispatcher down first so open streams end.
    ~HttpServer();
    HttpServer(const HttpServer&) = delete;
    HttpServer& operator=(const HttpServer&) = delete;

    /// Binds the first free port from `options.port` on and starts serving. Returns the port,
    /// or 0 if none was free.
    int start(const HttpOptions& options);
    /// Stops serving and waits for the threads (requests in flight finish first).
    void stop();
    [[nodiscard]] int port() const noexcept { return port_; }
    [[nodiscard]] bool isRunning() const noexcept { return port_ != 0; }
    /// A new token (the old one stops working at once).
    void setToken(std::string token);

private:
    void configure(const HttpOptions& options);

    struct Impl;
    Dispatcher& dispatcher_;
    std::unique_ptr<Impl> impl_;
    std::thread thread_;
    std::atomic<int> port_{0};
};
} // namespace fd::mcp
