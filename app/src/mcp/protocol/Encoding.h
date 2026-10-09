#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace fd::mcp
{
[[nodiscard]] std::string base64Encode(std::span<const std::uint8_t> bytes);
[[nodiscard]] std::string base64Encode(std::string_view text);
/// Standard alphabet, padding optional; nullopt if `text` isn't Base64.
[[nodiscard]] std::optional<std::string> base64Decode(std::string_view text);

/// An MCP header value (Mcp-Name, Mcp-Param-*): plain ASCII, or the Base64 sentinel form
/// "=?base64?...?=" (Streamable HTTP, value encoding). nullopt if the sentinel form is broken.
[[nodiscard]] std::optional<std::string> decodeHeaderValue(std::string_view value);
/// The header form of `value`: as is when it's plain printable ASCII, else the Base64 sentinel.
[[nodiscard]] std::string encodeHeaderValue(std::string_view value);

/// `bytes` random bytes as lowercase hex, from the system's random source (tokens, ids).
[[nodiscard]] std::string randomHex(std::size_t bytes);

/// Compares two secrets in time independent of where they differ.
[[nodiscard]] bool constantTimeEquals(std::string_view a, std::string_view b) noexcept;
} // namespace fd::mcp
