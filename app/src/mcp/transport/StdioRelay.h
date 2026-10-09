#pragma once

#include <chrono>
#include <iosfwd>
#include <string>

namespace fd::mcp
{
/// Where the DAW's MCP endpoint is (from the endpoint file in the user data folder).
struct RelayTarget
{
    std::string host = "127.0.0.1";
    int port = 0;
    std::string token;
    std::string path = "/mcp";
};

/// True if an MCP endpoint answers at `target` (any HTTP answer).
[[nodiscard]] bool probe(const RelayTarget& target, std::chrono::milliseconds timeout = std::chrono::milliseconds(500));

/// The stdio bridge (`FossDAW --mcp-stdio`, mcp.md §1): reads newline-delimited JSON-RPC
/// messages from `in`, POSTs each to the DAW's Streamable HTTP endpoint, and writes everything
/// the DAW answers (a JSON response, or the messages of an SSE stream) to `out`, one per line.
/// Requests run concurrently (a subscription stays open while other requests go through);
/// `notifications/cancelled` closes the request it names. Legacy clients get their session and
/// its notification stream. Returns when `in` ends. JUCE-free.
void relay(const RelayTarget& target, std::istream& in, std::ostream& out);
} // namespace fd::mcp
