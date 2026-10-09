#pragma once

// Shared fixtures for the MCP protocol and transport tests: a small registry of fake tools,
// request builders for both protocol eras, and a recording stream.

#include "mcp/protocol/Dispatcher.h"

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>

namespace fd::mcp::test
{
inline Json modernMeta(Json capabilities = Json::object())
{
    return {{"io.modelcontextprotocol/protocolVersion", modernVersion},
            {"io.modelcontextprotocol/clientInfo", {{"name", "test-client"}, {"version", "1.0"}}},
            {"io.modelcontextprotocol/clientCapabilities", std::move(capabilities)}};
}

inline Json modern(const std::string& method, Json params = Json::object(), Json id = 1,
                   Json capabilities = Json::object())
{
    params["_meta"] = modernMeta(std::move(capabilities));
    return {{"jsonrpc", "2.0"}, {"id", std::move(id)}, {"method", method}, {"params", std::move(params)}};
}

inline Json legacy(const std::string& method, Json params = Json::object(), Json id = 1)
{
    return {{"jsonrpc", "2.0"}, {"id", std::move(id)}, {"method", method}, {"params", std::move(params)}};
}

/// Tools: echo (read-only), touch (edits), reset (asks to confirm), slow (reports progress).
inline Registry fakeRegistry()
{
    Registry r;
    Tool echo;
    echo.name = "echo";
    echo.title = "Echo";
    echo.description = "Returns its text.";
    echo.inputSchema = {{"type", "object"},
                        {"properties", {{"text", {{"type", "string"}, {"minLength", 1}}}, {"times", {{"type", "integer"}, {"minimum", 1}}}}},
                        {"required", {"text"}},
                        {"additionalProperties", false}};
    echo.outputSchema = {{"type", "object"}, {"properties", {{"text", {{"type", "string"}}}}}, {"required", {"text"}}};
    echo.annotations.readOnly = true;
    echo.annotations.idempotent = true;
    echo.call = [](const Json& args, CallContext&) { return ToolResult::ok({{"text", args["text"]}}); };
    r.tools.push_back(echo);

    Tool touch;
    touch.name = "touch";
    touch.title = "Touch";
    touch.description = "Changes something.";
    touch.inputSchema = {{"type", "object"}, {"additionalProperties", false}};
    touch.call = [](const Json&, CallContext&) { return ToolResult::ok({{"touched", true}}, "Touched it."); };
    r.tools.push_back(touch);

    Tool reset;
    reset.name = "reset";
    reset.title = "Reset";
    reset.description = "Throws everything away (asks first).";
    reset.inputSchema = {{"type", "object"},
                         {"properties", {{"confirm", {{"type", "boolean"}}}, {"what", {{"type", "string"}}}}},
                         {"additionalProperties", false}};
    reset.annotations.destructive = true;
    reset.call = [](const Json&, CallContext& ctx) {
        if (!ctx.confirmed)
            return ToolResult::confirm("This throws everything away.");
        return ToolResult::ok({{"reset", true}});
    };
    r.tools.push_back(reset);

    Tool slow;
    slow.name = "slow";
    slow.title = "Slow";
    slow.description = "Takes three steps.";
    slow.inputSchema = {{"type", "object"}};
    slow.annotations.readOnly = true;
    slow.call = [](const Json&, CallContext& ctx) {
        for (int i = 1; i <= 3; ++i)
            ctx.progress(i, 3.0, "step " + std::to_string(i));
        return ToolResult::ok({{"steps", 3}});
    };
    r.tools.push_back(slow);

    r.resources.push_back({"test://a", "a", "Resource A", "The first resource", "application/json"});
    r.templates.push_back({"test://item/{n}", "item", "Item", "An item by number", "application/json"});
    r.readResource = [](const std::string& uri) -> std::optional<ResourceContents> {
        if (uri == "test://a" || uri.starts_with("test://item/"))
            return ResourceContents{uri, "application/json", Json{{"uri", uri}}.dump()};
        return std::nullopt;
    };

    Prompt greet;
    greet.name = "greet";
    greet.title = "Greet";
    greet.description = "Says hello.";
    greet.arguments.push_back({"name", "Who to greet", true});
    greet.get = [](const Json& args) {
        return Json::array(
            {{{"role", "user"}, {"content", {{"type", "text"}, {"text", "Hello " + args["name"].get<std::string>()}}}}});
    };
    r.prompts.push_back(greet);
    return r;
}

/// A response stream that records what it's sent; `onSend` can answer requests.
class RecordingStream final : public Stream
{
public:
    bool send(const Json& message) override
    {
        std::function<void(const Json&)> hook;
        {
            std::lock_guard lock(mutex_);
            if (!open_)
                return false;
            sent_.push_back(message);
            hook = onSend;
        }
        changed_.notify_all();
        if (hook)
            hook(message);
        return true;
    }
    bool keepAlive() override
    {
        std::lock_guard lock(mutex_);
        ++keepAlives_;
        return open_;
    }
    [[nodiscard]] bool isOpen() const override
    {
        std::lock_guard lock(mutex_);
        return open_;
    }
    void close()
    {
        {
            std::lock_guard lock(mutex_);
            open_ = false;
        }
        changed_.notify_all();
    }
    /// Waits until at least `count` messages were sent; returns them.
    std::vector<Json> waitFor(size_t count, std::chrono::milliseconds timeout = std::chrono::seconds(5))
    {
        std::unique_lock lock(mutex_);
        changed_.wait_for(lock, timeout, [&] { return sent_.size() >= count; });
        return sent_;
    }
    [[nodiscard]] int keepAlives() const
    {
        std::lock_guard lock(mutex_);
        return keepAlives_;
    }

    std::function<void(const Json&)> onSend;

private:
    mutable std::mutex mutex_;
    std::condition_variable changed_;
    std::vector<Json> sent_;
    int keepAlives_ = 0;
    bool open_ = true;
};

inline ServerIdentity identity()
{
    return {"segno-test", "Segno (test)", "0.0.1", "Test instructions."};
}
} // namespace fd::mcp::test
