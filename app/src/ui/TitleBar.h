#pragma once

#include <fd_ui/fd_ui.h>
#include <functional>

namespace fd::app
{
/// The window's top on Linux and Windows (M2 step C10, ADR 0040, docs/design/menu-bar.md §4): the
/// brand and the menu bar on the left, the project's name in the middle, the window buttons on the
/// right, on one `shell.titleBarHeight` row laid out by model::layoutTopBar. With the system's
/// title bar it holds only the brand and the menus. Dragging the rest moves the window, through
/// the system (the main window wires that).
class TitleBar final : public juce::Component
{
public:
    explicit TitleBar(juce::MenuBarModel& menus);

    /// The project's name, with a dot after it while there are unsaved changes.
    void setTitle(const juce::String& name, bool unsaved);
    /// Our own title bar (the name and the window buttons), or the menus alone under the system's.
    void setOwnsWindow(bool owns);
    [[nodiscard]] bool ownsWindow() const noexcept { return owns_; }
    /// Maximised: Restore instead of Maximise. Full screen: no window buttons.
    void setWindowState(bool maximised, bool fullScreen);
    /// Opens menu `index` (F10, Alt+letter).
    void showMenu(int index);
    /// Underlines each menu's Alt letter.
    void setMnemonicsShown(bool shown);

    /// What is at `p` (local coordinates), for the system's hit-testing (Windows) and the drags
    /// here (Linux). Plain rectangle tests: safe to call from the system's hit-test.
    enum class Part
    {
        Client, // the menus: they take their own clicks
        Caption,
        Minimise,
        Maximise,
        Close,
    };
    [[nodiscard]] Part partAt(juce::Point<int> p) const;

    std::function<void()> onMinimise, onMaximise, onClose;
    /// The caption dragged (Linux): hand the move to the window manager.
    std::function<void(const juce::MouseEvent&)> onCaptionDrag;
    std::function<void()> onCaptionDoubleClick;
    /// A right-click on the caption (Linux; Windows shows its own menu).
    std::function<void()> onCaptionMenu;

    // Where things are (tests).
    [[nodiscard]] juce::Rectangle<int> brandArea() const noexcept { return brand_; }
    [[nodiscard]] juce::Rectangle<int> titleArea() const noexcept { return title_; }
    [[nodiscard]] juce::MenuBarComponent& menuBar() noexcept { return menuBar_; }
    [[nodiscard]] ui::WindowButtons& windowButtons() noexcept { return buttons_; }

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;

private:
    [[nodiscard]] int menusWidth();

    juce::MenuBarComponent menuBar_;
    ui::WindowButtons buttons_;
    juce::String name_;
    bool unsaved_ = false, owns_ = true, fullScreen_ = false, markOnly_ = false;
    bool dragStarted_ = false;
    // Laid out by resized(): the painted parts.
    juce::Rectangle<int> brand_, title_;
};
} // namespace fd::app
