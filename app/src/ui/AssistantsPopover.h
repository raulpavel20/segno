#pragma once

#include "mcp/McpServer.h"

#include <fd_ui/fd_ui.h>

namespace fd::app
{
/// The Assistants popover (docs/design/mcp.md §1): turns the MCP server on or off, read-only
/// mode, the setup lines to copy for each kind of client, who's connected, and a new token.
/// Opened from the transport bar's spark; Escape or a click elsewhere closes it.
class AssistantsPopover final : public juce::Component
{
public:
    explicit AssistantsPopover(mcp::McpServer& server);
    ~AssistantsPopover() override;

    /// Shows it inside `parent`, its top right corner under `anchor` (parent coordinates).
    void open(juce::Component& parent, juce::Rectangle<int> anchor);
    void close();
    [[nodiscard]] bool isOpen() const noexcept { return isVisible(); }
    /// The server's status changed: shows it.
    void refresh();
    /// Called when a start failed (no free port), for the shell to say so.
    std::function<void(const juce::String&)> onMessage;

    void paint(juce::Graphics& g) override;
    void resized() override;
    bool keyPressed(const juce::KeyPress& key) override;

private:
    void mouseDown(const juce::MouseEvent& e) override; // also global while open: closes when outside
    [[nodiscard]] int preferredHeight() const;
    [[nodiscard]] juce::String status() const;

    mcp::McpServer& server_;
    ui::Toggle on_{"On"}, readOnly_{"Read only"};
    juce::TextEditor httpLine_, stdioLine_;
    juce::TextButton copyHttp_{"Copy"}, copyStdio_{"Copy"}, newToken_{"New token"};
    bool failed_ = false;

    // Laid out by resized(): the painted parts.
    juce::Rectangle<int> title_, status_, readOnlyHint_, httpLabel_, stdioLabel_, clientsLabel_, clients_, tokenHint_;
    std::vector<int> separators_;
};
} // namespace fd::app
