#pragma once

namespace fd::ui
{
/// A Drum Kit's pads (docs/design/drum-kit.md §5, ADR 0064): 16 at a time, 4 × 4, the lowest note
/// at the bottom left, as on Push and hardware pads. Shared, so whatever shows a kit's pads shows
/// them the same.
///   - A pad with a sound is raised (`surface3`, lifted while hovered), its name over its note; an
///     empty one is a well (`surface0`) with its note alone.
///   - A pad that's off is dimmed; a soloed one shows S in `active`; one in a choke group shows the
///     group's number in its corner; a pad whose note sounds is lit.
///   - The selected pad is outlined in `active`; where a drag would land, the outline is the drop
///     line's width.
/// The host decides what a click does: a press calls onPress (play it, select it), a release
/// onRelease. A pad with a sound dragged onto another calls onMovePad (Ctrl copies). Drops from
/// outside are the host's: padAt says which pad is under the pointer, setDropTarget lights it.
/// A pad's tooltip names it whole, as its name is cut to fit.
class PadGrid final : public juce::Component, public juce::TooltipClient
{
public:
    static constexpr int columns = 4, rows = 4, shown = columns * rows;

    struct Pad
    {
        juce::String name; // its sound's name, or its own
        juce::String note; // its note's name, as the host spells it ("C♯1")
        bool sound = false;
        bool on = true, solo = false;
        int choke = 0;
        bool operator==(const Pad&) const = default;
    };

    PadGrid();

    /// What it shows: the pads from `from` (the lowest note shown) up, and the selected pad.
    void setPads(int from, const std::array<Pad, shown>& pads, int selected);
    /// The notes sounding now: their pads are lit.
    void setPlaying(const std::bitset<128>& notes);
    /// Where a drag from outside would land (−1: nowhere).
    void setDropTarget(int note);

    [[nodiscard]] int from() const noexcept { return from_; }
    [[nodiscard]] int selected() const noexcept { return selected_; }
    /// The note of the pad under a point (−1: none).
    [[nodiscard]] int padAt(juce::Point<int> p) const;
    /// A pad's area (empty for a note not shown).
    [[nodiscard]] juce::Rectangle<int> padBounds(int note) const;
    /// Where a pad's name and note are drawn, inside it.
    [[nodiscard]] juce::Rectangle<int> nameArea(int note) const;
    [[nodiscard]] juce::Rectangle<int> noteArea(int note) const;

    std::function<void(int note)> onPress, onRelease;
    std::function<void(int note)> onDoubleClick; // rename it
    std::function<void(int note)> onMenu;        // its right-click
    std::function<void(int from, int to, bool copy)> onMovePad;
    /// The wheel: the pads shown move by rows (up: higher notes).
    std::function<void(int rows)> onScroll;

    /// The pad under the pointer: its whole name and its note ("Hat closed · F♯1"); an empty
    /// pad says what it takes.
    juce::String getTooltip() override;

    void paint(juce::Graphics& g) override;
    void mouseMove(const juce::MouseEvent& e) override;
    void mouseExit(const juce::MouseEvent& e) override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    void mouseDoubleClick(const juce::MouseEvent& e) override;
    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override;

private:
    [[nodiscard]] const Pad& pad(int note) const;
    void setHovered(int note);

    int from_ = 36, selected_ = 36;
    std::array<Pad, shown> pads_{};
    std::bitset<128> playing_;
    int hovered_ = -1, pressed_ = -1, dropTarget_ = -1;
    bool dragging_ = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PadGrid)
};

/// All 128 pads of a Drum Kit, 4 a row like the pads, the lowest at the bottom (drum-kit.md §5):
/// a cell lit where a pad has a sound, the selected pad's in `active`, the 16 shown framed. A
/// click or a drag moves the frame, a row at a time.
class PadOverview final : public juce::Component
{
public:
    PadOverview();

    void setPads(const std::bitset<128>& sounds, int from, int selected);
    [[nodiscard]] int from() const noexcept { return from_; }
    /// A row's area (0: the lowest, notes 0 … 3), and the frame round the 16 shown.
    [[nodiscard]] juce::Rectangle<int> rowBounds(int row) const;
    [[nodiscard]] juce::Rectangle<int> frameBounds() const;

    /// The pads shown should start at `from` (a row's first note).
    std::function<void(int from)> onShow;

    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;

private:
    [[nodiscard]] juce::Rectangle<int> cellBounds(int note) const;
    void showAt(int y);

    std::bitset<128> sounds_;
    int from_ = 36, selected_ = 36;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PadOverview)
};
} // namespace fd::ui
