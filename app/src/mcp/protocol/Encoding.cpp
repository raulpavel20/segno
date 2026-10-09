#include "mcp/protocol/Encoding.h"

#include <algorithm>
#include <random>

namespace fd::mcp
{
namespace
{
constexpr std::string_view kAlphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

int decodeChar(char c) noexcept
{
    if (c >= 'A' && c <= 'Z')
        return c - 'A';
    if (c >= 'a' && c <= 'z')
        return c - 'a' + 26;
    if (c >= '0' && c <= '9')
        return c - '0' + 52;
    if (c == '+')
        return 62;
    if (c == '/')
        return 63;
    return -1;
}
} // namespace

std::string base64Encode(std::span<const std::uint8_t> bytes)
{
    std::string out;
    out.reserve((bytes.size() + 2) / 3 * 4);
    size_t i = 0;
    for (; i + 2 < bytes.size(); i += 3)
    {
        const std::uint32_t v = (std::uint32_t(bytes[i]) << 16) | (std::uint32_t(bytes[i + 1]) << 8) | bytes[i + 2];
        out += kAlphabet[(v >> 18) & 63];
        out += kAlphabet[(v >> 12) & 63];
        out += kAlphabet[(v >> 6) & 63];
        out += kAlphabet[v & 63];
    }
    if (i < bytes.size())
    {
        std::uint32_t v = std::uint32_t(bytes[i]) << 16;
        if (i + 1 < bytes.size())
            v |= std::uint32_t(bytes[i + 1]) << 8;
        out += kAlphabet[(v >> 18) & 63];
        out += kAlphabet[(v >> 12) & 63];
        out += i + 1 < bytes.size() ? kAlphabet[(v >> 6) & 63] : '=';
        out += '=';
    }
    return out;
}

std::string base64Encode(std::string_view text)
{
    return base64Encode(std::span(reinterpret_cast<const std::uint8_t*>(text.data()), text.size()));
}

std::optional<std::string> base64Decode(std::string_view text)
{
    while (!text.empty() && text.back() == '=')
        text.remove_suffix(1);
    if (text.size() % 4 == 1)
        return std::nullopt;
    std::string out;
    std::uint32_t buffer = 0;
    int bits = 0;
    for (char c : text)
    {
        const int v = decodeChar(c);
        if (v < 0)
            return std::nullopt;
        buffer = (buffer << 6) | static_cast<std::uint32_t>(v);
        bits += 6;
        if (bits >= 8)
        {
            bits -= 8;
            out += static_cast<char>((buffer >> bits) & 0xff);
        }
    }
    return out;
}

std::optional<std::string> decodeHeaderValue(std::string_view value)
{
    constexpr std::string_view prefix = "=?base64?", suffix = "?=";
    if (value.size() >= prefix.size() + suffix.size() && value.starts_with(prefix) && value.ends_with(suffix))
        return base64Decode(value.substr(prefix.size(), value.size() - prefix.size() - suffix.size()));
    return std::string(value);
}

std::string encodeHeaderValue(std::string_view value)
{
    const bool plain = !value.empty() && value.front() != ' ' && value.back() != ' ' &&
                       std::all_of(value.begin(), value.end(), [](char c) { return c >= 0x20 && c <= 0x7e; }) &&
                       !(value.starts_with("=?base64?") && value.ends_with("?="));
    return plain ? std::string(value) : "=?base64?" + base64Encode(value) + "?=";
}

std::string randomHex(std::size_t bytes)
{
    static constexpr std::string_view digits = "0123456789abcdef";
    std::random_device device;
    std::string out;
    out.reserve(bytes * 2);
    for (std::size_t i = 0; i < bytes; ++i)
    {
        const auto b = static_cast<unsigned>(device()) & 0xffu;
        out += digits[b >> 4];
        out += digits[b & 15];
    }
    return out;
}

bool constantTimeEquals(std::string_view a, std::string_view b) noexcept
{
    unsigned diff = a.size() == b.size() ? 0u : 1u;
    for (size_t i = 0; i < a.size(); ++i)
        diff |= static_cast<unsigned>(a[i] ^ (i < b.size() ? b[i] : 0));
    return diff == 0;
}
} // namespace fd::mcp
