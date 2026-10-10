#pragma once

#include "ui/Shell.h"
#include "ui/TitleBar.h"

namespace fd::app
{
/// The main window's content (ADR 0040, docs/design/menu-bar.md §4): our title bar over the shell
/// on Linux and Windows; on macOS, where the menus are the system's, the shell alone.
class WindowContent final : public juce::Component
{
public:
    /// `titleBar`: the window has a title bar row of ours (not on macOS).
    WindowContent(engine::Session& session, mcp::McpServer* assistants, bool titleBar);

    [[nodiscard]] Shell& shell() noexcept { return shell_; }
    /// Null on macOS.
    [[nodiscard]] TitleBar* titleBar() noexcept { return titleBar_.get(); }

    /// Our own title bar (name and window buttons), or the menus alone under the system's.
    void setOwnsWindow(bool owns);
    void setWindowState(bool maximised, bool fullScreen);
    /// The kind of window control at `p` (local coordinates), for the system's hit-testing:
    /// the title bar's caption and buttons; everything else is the client's.
    [[nodiscard]] juce::Component::WindowControlKind controlAt(juce::Point<float> p) const;
    /// The window's title (the system's: taskbar, window switcher).
    std::function<void(const juce::String&)> onTitleChanged;

    void resized() override;

private:
    std::unique_ptr<TitleBar> titleBar_;
    Shell shell_;
};
} // namespace fd::app
