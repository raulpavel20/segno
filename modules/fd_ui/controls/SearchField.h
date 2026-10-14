#pragma once

namespace fd::ui
{
/// A search field (docs/design/ui-foundations.md): the magnifier at its left, then what's typed,
/// in `Field` on `surface2`; while empty, its hint in `textTertiary`. Every search in the app is
/// one: the browser's, the command palette's, Settings', the Key popover's, the lane picker's and
/// the preset list's.
class SearchField : public juce::TextEditor
{
public:
    explicit SearchField(const juce::String& hint = {});
    /// What it says while empty ("Search settings").
    void setHint(const juce::String& hint);
    /// Where the magnifier is (for the layout checks): `listIconSize` square, `fieldTextInset` in.
    [[nodiscard]] juce::Rectangle<int> iconArea() const;

    void paintOverChildren(juce::Graphics& g) override;
};
} // namespace fd::ui
