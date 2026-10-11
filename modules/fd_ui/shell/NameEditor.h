#pragma once

namespace fd::ui
{
/// Edits a name in place (docs/design/tracks-and-clips.md §4): a field over the name's own area
/// in its text style, on `surface2`, with the text selected. Return or a click elsewhere keeps
/// it, Escape restores it, and an empty name restores it too. Tab and Shift+Tab keep it and ask
/// for the next or the one before (renaming a set of tracks in one go). One per view.
class NameEditor final : private juce::KeyListener
{
public:
    /// `host` holds the field (the view whose names it edits).
    explicit NameEditor(juce::Component& host);
    ~NameEditor() override;
    NameEditor(const NameEditor&) = delete;
    NameEditor& operator=(const NameEditor&) = delete;

    /// Shows the field over `area` (host coordinates). `commit` gets the new name when it's kept
    /// and differs; `tab` (optional) gets +1 or -1 after Tab or Shift+Tab kept it.
    void show(juce::Rectangle<int> area, const juce::String& name, TextStyle style,
              std::function<void(const juce::String&)> commit, std::function<void(int)> tab = {});
    /// Hides it without keeping what was typed.
    void cancel();
    [[nodiscard]] bool isEditing() const noexcept { return field_.isVisible(); }
    /// The field (tests type into it).
    [[nodiscard]] juce::TextEditor& field() noexcept { return field_; }

private:
    bool keyPressed(const juce::KeyPress& key, juce::Component* origin) override;
    void finish(bool keep, int tab = 0);

    juce::Component& host_;
    juce::TextEditor field_;
    juce::String original_;
    std::function<void(const juce::String&)> commit_;
    std::function<void(int)> tab_;
    bool finishing_ = false;
};
} // namespace fd::ui
