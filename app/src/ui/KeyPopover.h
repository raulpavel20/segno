#pragma once

#include "engine/Session.h"

#include <fd_ui/fd_ui.h>

namespace fd::app
{
/// The Key popover (docs/design/key-and-scale.md §6, ADR 0056): the song's key, chosen with the
/// mouse. The root on one octave of keys, the scale from a searchable list under its groups
/// (hovering one previews its notes), and the keys found from the song's notes. Everything it
/// changes while open is one undo step (Session::KeyChoice).
class KeyPopover final : public juce::Component, private juce::TextEditor::Listener, private juce::KeyListener
{
public:
    explicit KeyPopover(engine::Session& session);
    ~KeyPopover() override;

    /// Shows inside `parent` under `anchor` (the top bar's key field, in `parent`'s coordinates),
    /// with the keys found in `clips` (every MIDI clip when empty).
    void open(juce::Component& parent, juce::Rectangle<int> anchor,
              const std::vector<std::uint64_t>& clips = {});
    void close();
    [[nodiscard]] bool isOpen() const noexcept { return isVisible(); }

    /// The key it shows: the song's, as changed here.
    [[nodiscard]] const std::optional<music::Key>& key() const noexcept { return key_; }
    /// What a click on a key, a scale, a found key or No key does.
    void chooseRoot(int pitchClass);
    void chooseScale(const std::string& scaleId);
    void chooseKey(const std::optional<music::Key>& key);
    /// Shows a scale's notes on the keys without choosing it (a hovered row); empty: none.
    void previewScale(const std::string& scaleId);

    /// The scales the list shows, in order (the search filters them).
    [[nodiscard]] std::vector<std::string> shownScales() const;
    void setQuery(const juce::String& text);
    /// The keys found from the notes, best first.
    [[nodiscard]] const std::vector<music::KeyCandidate>& found() const noexcept { return found_; }
    [[nodiscard]] ui::OctaveKeys& keys() noexcept { return keys_; }

    /// It opened or closed (the key field stays lit while it shows).
    std::function<void(bool open)> onOpenChanged;
    /// A press on this component never closes it (the key field, which closes it itself).
    std::function<bool(const juce::Component&)> ignoreClicksOn;

    void paint(juce::Graphics& g) override;
    void resized() override;

    using juce::Component::keyPressed;

private:
    class Rows;

    void textEditorTextChanged(juce::TextEditor&) override;
    void textEditorReturnKeyPressed(juce::TextEditor&) override;
    void textEditorEscapeKeyPressed(juce::TextEditor&) override { close(); }
    bool keyPressed(const juce::KeyPress& key, juce::Component*) override;

    /// The key the keys and the spelled line show: the hovered scale's, else the chosen one.
    [[nodiscard]] std::optional<music::Key> shownKey() const;
    void showKey();
    void rebuildFound();
    void place();
    [[nodiscard]] juce::Rectangle<int> inner() const;
    [[nodiscard]] int preferredHeight() const;
    [[nodiscard]] juce::Rectangle<int> spelledArea() const;
    [[nodiscard]] juce::Rectangle<int> foundArea() const;

    engine::Session& session_;
    std::unique_ptr<engine::Session::KeyChoice> choice_;
    std::optional<music::Key> key_;
    std::string preview_;
    ui::Toggle noKey_{"No key"};
    ui::OctaveKeys keys_;
    juce::TextEditor search_;
    juce::Viewport viewport_;
    std::unique_ptr<Rows> rows_;
    std::vector<music::KeyCandidate> found_;
    std::vector<std::unique_ptr<ui::Toggle>> foundButtons_;
    juce::Component::SafePointer<juce::Component> parent_;
    juce::Rectangle<int> anchor_;
    ui::OutsideClick outside_;
};
} // namespace fd::app
