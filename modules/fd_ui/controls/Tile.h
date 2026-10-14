#pragma once

namespace fd::ui
{
/// A tool's tile (docs/design/ui-foundations.md, "Shared components"): its icon and its name, one
/// of a set all shown at once (the clip view's Tools pane, piano-roll.md §25). Off: `surface2`
/// with `textSecondary`; chosen (its toggle state): the `active` fill with `onActive`, as a chosen
/// segment; hovered: lifted; disabled: `textTertiary`, its tooltip saying why. Compact, it shows
/// its icon alone, square, its name in its tooltip (where the pane is short). Its owner decides
/// what a click does (a chosen tile clicked again chooses none).
class Tile final : public juce::Button
{
public:
    Tile(Icon icon, const juce::String& name);

    /// The width that fits its icon and name.
    [[nodiscard]] int preferredWidth() const;
    /// Its icon alone (its owner makes it square and names it in its tooltip).
    void setCompact(bool compact);
    [[nodiscard]] bool isCompact() const noexcept { return compact_; }

    void paintButton(juce::Graphics& g, bool highlighted, bool down) override;

private:
    Icon icon_;
    bool compact_ = false;
};
} // namespace fd::ui
