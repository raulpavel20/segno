#pragma once

#include <atomic>
#include <functional>
#include <memory>

namespace fd::mcp
{
/// Runs MCP work on the message thread, where the Session lives (mcp.md §7): transports call
/// run() from their own threads and wait. close() (on the message thread, when the server
/// stops) makes waiting callers give up and queued work never run, so no transport thread
/// waits on a message loop that is shutting down.
class MainThread
{
public:
    MainThread();
    ~MainThread();

    /// Runs `work` on the message thread (at once when called there) and waits for it.
    /// Exceptions are passed back. False if closed before it ran.
    bool run(const std::function<void()>& work);
    /// Message thread only.
    void close();
    /// Opens again after close() (the server restarting). Only while no transport runs.
    void reopen();
    [[nodiscard]] bool isOpen() const noexcept;

private:
    struct Shared;
    std::shared_ptr<Shared> shared_;
};
} // namespace fd::mcp
