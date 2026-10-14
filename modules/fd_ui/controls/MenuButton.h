#pragma once

namespace fd::ui
{
/// A command that opens a menu (docs/design/ui-foundations.md, "Shared components"): a command's
/// look (a Toggle used as a button) with a small chevron after its label. The clip view's Notes
/// opens the whole Notes menu under it (docs/design/piano-roll.md §4). A click calls onClick,
/// where its owner builds the menu and opens it with showMenu.
class MenuButton final : public juce::Button
{
public:
    explicit MenuButton(const juce::String& text);

    /// The width that fits its label and the chevron.
    [[nodiscard]] int preferredWidth() const;

    /// Opens `menu` under the button; the button looks pressed while it's open.
    void showMenu(juce::PopupMenu menu);
    [[nodiscard]] bool isMenuOpen() const noexcept { return open_; }

    void paintButton(juce::Graphics& g, bool highlighted, bool down) override;

private:
    static constexpr int kChevronPadding = 6; // after the chevron (a ViewChoice's)
    static constexpr int kGap = 4;            // between the label and the chevron
    static constexpr float kChevron = 8.0f;

    bool open_ = false;
};
} // namespace fd::ui
