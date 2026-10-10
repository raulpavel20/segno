#pragma once

namespace fd::ui
{
/// The icon set (ui-principles §3): line icons drawn in code on a 16 px grid with a 1.5 px
/// stroke; transport glyphs are filled. Add icons here only; every icon needs a tooltip.
enum class Icon
{
    Play,
    Stop,
    Record,
    Loop,
    Metronome,
    PanelLeft,   // browser
    PanelRight,  // inspector
    PanelBottom, // detail panel
    Plus,
    Settings,
    Undo,
    Redo,
    Keyboard,    // computer keyboard as MIDI input
    ChevronLeft,  // fold a module
    ChevronRight, // unfold
    Assistant,    // assistants connected over MCP (a spark)
    AutomationArm, // automation arm: a lane's line with a point (C4)
    AutomationOn,  // turn automation back on: the line with a turning arrow
    Close,         // remove or close: a cross
    Star,          // a favourite (a take, C5b): outlined; filled when on
    Capture,       // capture MIDI (C5b): a turning arrow back round a dot, recording what was played
    Follow,        // follow the playhead: the playhead line with an arrow going on
    CountIn,       // count-in: beats leading into the downbeat
    ChevronDown    // opens a menu (a clip's takes)
};

/// Draws `icon` fitted (aspect kept) into `area` in `colour`.
void drawIcon(juce::Graphics& g, Icon icon, juce::Rectangle<float> area, juce::Colour colour);
/// The same, filled (a starred star).
void fillIcon(juce::Graphics& g, Icon icon, juce::Rectangle<float> area, juce::Colour colour);

/// Flat icon button (ui-principles §4): dim when off, `active` fill when on (toggles),
/// `surface3` on hover. Tooltip text should include the shortcut.
class IconButton : public juce::Button
{
public:
    explicit IconButton(Icon icon, const juce::String& name = {});

    void setIcon(Icon icon);
    /// On in the `record` colour instead of `active` (the transport's Record button).
    void setRecordAccent(bool record);
    /// Shows its on state in the icon instead of a fill: a star is filled in `active` when on.
    void setIconShowsState(bool iconShowsState);
    /// A right-click: the button's menu (it doesn't click).
    std::function<void()> onPopupMenu;

    void paintButton(juce::Graphics& g, bool highlighted, bool down) override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;

private:
    Icon icon_;
    bool record_ = false, popup_ = false, iconShowsState_ = false;
};
} // namespace fd::ui
