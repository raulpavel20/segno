#pragma once

#include <fd_ui/fd_ui.h>

namespace fd::app
{
/// Help > About Segno (M2 step C10, docs/design/menu-bar.md §5): the brand and version, the
/// licence notice the AGPL asks an interactive program to show (free software, no warranty, where
/// the licence is), and the third-party credits. A popover at the top of the window; Escape or a
/// click elsewhere closes it.
class AboutPopover final : public juce::Component
{
public:
    AboutPopover();

    void open(juce::Component& parent);
    void close();
    [[nodiscard]] bool isOpen() const noexcept { return isVisible(); }
    void setVersion(const juce::String& version);

    /// The paragraphs it shows (tests).
    [[nodiscard]] juce::StringArray paragraphs() const;

    void paint(juce::Graphics& g) override;
    bool keyPressed(const juce::KeyPress& key) override;

private:
    [[nodiscard]] int preferredHeight() const;
    /// Each paragraph's area for a popover `width` wide, from the top.
    [[nodiscard]] std::vector<juce::Rectangle<int>> layout(int width) const;

    juce::String version_;
    ui::OutsideClick outside_{*this, [this] { close(); }};
};
} // namespace fd::app
