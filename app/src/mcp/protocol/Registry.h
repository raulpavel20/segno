#pragma once

#include <nlohmann/json.hpp>

#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace fd::mcp
{
/// The MCP server's protocol layer (ADR 0021, docs/design/mcp.md): JUCE-free. The tools, resources
/// and prompts are registered by the app (app/src/mcp/tools); the Dispatcher serves them.
using Json = nlohmann::json;

/// What a tool says about itself (MCP tool annotations). Honest: clients may ask the user before
/// calling a tool that isn't read-only.
struct ToolAnnotations
{
    bool readOnly = false;
    bool destructive = false; // can remove or overwrite something (still undoable unless noted)
    bool idempotent = false;  // calling it twice with the same arguments does nothing more
};

/// The outcome of one tool call.
struct ToolResult
{
    Json structured = Json::object(); // structuredContent; matches the tool's output schema
    std::string summary;              // an optional sentence for the model, sent before the JSON
    bool isError = false;             // the tool couldn't do it: `summary` says why and what to do
    std::vector<Json> content;        // more content blocks (e.g. audio)
    /// Set when the call needs the user's confirmation first: the question. Nothing was done.
    std::optional<std::string> confirmation;

    [[nodiscard]] static ToolResult ok(Json structured, std::string summary = {})
    {
        ToolResult r;
        r.structured = std::move(structured);
        r.summary = std::move(summary);
        return r;
    }
    [[nodiscard]] static ToolResult error(std::string message)
    {
        ToolResult r;
        r.isError = true;
        r.summary = std::move(message);
        return r;
    }
    [[nodiscard]] static ToolResult confirm(std::string question)
    {
        ToolResult r;
        r.confirmation = std::move(question);
        return r;
    }
};

/// What a tool sees of the call besides its arguments. The functions are always set.
struct CallContext
{
    /// True when the user has confirmed this call (after a ToolResult::confirm).
    bool confirmed = false;
    /// Reports progress on a long call (sent when the client asked for it).
    std::function<void(double done, std::optional<double> total, const std::string& message)> progress =
        [](double, std::optional<double>, const std::string&) {};
    /// True once the client has gone: the call should stop.
    std::function<bool()> cancelled = [] { return false; };
    Json clientInfo = Json::object(); // the client's name and version, for display
};

struct Tool
{
    std::string name;  // permanent once released (ADR 0021)
    std::string title;
    std::string description;
    Json inputSchema;  // a JSON Schema object (see Schema.h for the subset we use)
    Json outputSchema; // null when the tool has none
    ToolAnnotations annotations;
    /// Runs the tool on the transport's thread (tools marshal to the app's own thread).
    std::function<ToolResult(const Json& arguments, CallContext& context)> call;
};

struct Resource
{
    std::string uri;
    std::string name;
    std::string title;
    std::string description;
    std::string mimeType = "application/json";
};

struct ResourceTemplate
{
    std::string uriTemplate; // RFC 6570, e.g. "fossdaw://track/{id}"
    std::string name;
    std::string title;
    std::string description;
    std::string mimeType = "application/json";
};

struct ResourceContents
{
    std::string uri;
    std::string mimeType = "application/json";
    std::string text;
};

struct PromptArgument
{
    std::string name;
    std::string description;
    bool required = false;
};

struct Prompt
{
    std::string name;
    std::string title;
    std::string description;
    std::vector<PromptArgument> arguments;
    /// The prompt's messages (MCP PromptMessage objects) for the given arguments (strings).
    std::function<Json(const Json& arguments)> get;
};

/// Everything the server offers. Filled once at start-up, then read-only (thread-safe).
struct Registry
{
    std::vector<Tool> tools;
    std::vector<Resource> resources;
    std::vector<ResourceTemplate> templates;
    std::vector<Prompt> prompts;
    /// Reads any resource URI (listed or from a template); nullopt if it names nothing.
    std::function<std::optional<ResourceContents>(const std::string& uri)> readResource;

    [[nodiscard]] const Tool* findTool(const std::string& name) const
    {
        for (const auto& t : tools)
            if (t.name == name)
                return &t;
        return nullptr;
    }
    [[nodiscard]] const Prompt* findPrompt(const std::string& name) const
    {
        for (const auto& p : prompts)
            if (p.name == name)
                return &p;
        return nullptr;
    }
};
} // namespace fd::mcp
