#pragma once

namespace fd::ui
{
class PresetBar;

/// Where your presets live (ADR 0044): Documents/Segno/Presets, or the folder the
/// SEGNO_PRESETS_FOLDER environment variable names (the tests' own; a portable setup).
[[nodiscard]] juce::File defaultPresetsFolder();

/// The preset's name in a module's header (docs/design/presets.md §6): a combo box's look, the
/// name cut to fit and then the modified mark, never cut. A click opens the preset list; a
/// right-click, the preset commands.
class PresetField final : public juce::ComboBox
{
public:
    PresetField();
    void setPreset(const juce::String& name, bool modified);
    [[nodiscard]] const juce::String& presetName() const noexcept { return name_; }
    [[nodiscard]] bool isModified() const noexcept { return modified_; }
    std::function<void()> onOpen, onMenu;

    void showPopup() override;
    /// One click, one toggle of the list, on the press. JUCE's combo box opens its menu on the
    /// release too unless that menu is still showing; the list isn't its menu, so the release of
    /// a press held a moment would toggle it a second time.
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent&) override {}
    void mouseUp(const juce::MouseEvent&) override {}
    void paintOverChildren(juce::Graphics& g) override;

private:
    juce::String name_;
    bool modified_ = false;
};

/// A module's presets, as a popover over the host's window (presets.md §7): search, tags,
/// favourites, the list (browsing loads at once, so you hear each), Save and Save as… with the
/// form in place. Enter or a click elsewhere keeps what's loaded; Escape goes back.
class PresetList final : public juce::Component, private juce::TextEditor::Listener, private juce::KeyListener
{
public:
    explicit PresetList(PresetBar& bar);
    ~PresetList() override;

    /// Shows it over `top` (a window's content or a plugin's editor), under or above `anchor`
    /// (the preset field, in `top`'s coordinates), and starts browsing.
    void show(juce::Component& top, juce::Rectangle<int> anchor);
    /// Closes it: `keep` false goes back to what was there before it opened.
    void close(bool keep);
    /// Turns to the save form (Save as…).
    void showSaveForm();
    [[nodiscard]] bool isShowingForm() const noexcept { return form_; }

    /// The presets shown, in order, and the highlighted one (-1: none).
    [[nodiscard]] const std::vector<const Preset*>& shown() const noexcept { return shown_; }
    [[nodiscard]] int highlighted() const noexcept { return highlighted_; }
    /// Highlights the preset at `index` of shown() and loads it (browsing is listening).
    void highlight(int index);
    /// The tags offered as filters, and whether each is on.
    [[nodiscard]] std::vector<std::pair<juce::String, bool>> tagFilters() const;
    void setTagFilter(const juce::String& tag, bool on);
    void setFavouritesOnly(bool on);
    /// Stars or unstars the preset at `index` of shown().
    void setFavourite(int index, bool favourite);

    // For tests: the fields and buttons.
    [[nodiscard]] juce::TextEditor& searchField() noexcept { return search_; }
    [[nodiscard]] juce::TextEditor& nameField() noexcept { return name_; }
    [[nodiscard]] juce::TextEditor& tagsField() noexcept { return tags_; }
    [[nodiscard]] juce::TextEditor& descriptionField() noexcept { return description_; }
    [[nodiscard]] juce::TextEditor& authorField() noexcept { return author_; }
    [[nodiscard]] juce::Button& saveButton() noexcept { return save_; }
    [[nodiscard]] juce::Button& saveAsButton() noexcept { return saveAs_; }
    [[nodiscard]] juce::Button& confirmButton() noexcept { return confirm_; }
    [[nodiscard]] juce::Button& cancelButton() noexcept { return cancel_; }
    [[nodiscard]] const juce::String& problem() const noexcept { return problem_; }
    /// Renames the preset at `index` of shown() (the row's in-place editor commits here).
    void rename(int index, const juce::String& name);
    /// The menu of the preset at `index` of shown() (a right-click on its row).
    void showRowMenu(int index);

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    class Rows;

    /// A row's caption: its tags.
    [[nodiscard]] static std::string tagCaption(const Preset& p);
    void textEditorTextChanged(juce::TextEditor&) override;
    void textEditorReturnKeyPressed(juce::TextEditor&) override;
    void textEditorEscapeKeyPressed(juce::TextEditor&) override;
    using juce::Component::keyPressed;
    bool keyPressed(const juce::KeyPress& key, juce::Component* origin) override;

    /// Reads the library again and rebuilds the tags and the list.
    void reload();
    /// Filters the list by the search, the tags and favourites.
    void refilter();
    void layoutTags();
    void confirmForm();
    void endForm();
    [[nodiscard]] int preferredHeight() const;
    [[nodiscard]] juce::Rectangle<int> inner() const;
    void place();

    PresetBar& bar_;
    SearchField search_;
    juce::TextEditor name_, tags_, description_, author_;
    std::vector<std::unique_ptr<Toggle>> tagButtons_;
    ViewOption favourites_{"Favourites"};
    std::unique_ptr<Rows> rows_;
    juce::Viewport viewport_;
    Toggle save_{"Save"}, saveAs_{juce::String::fromUTF8("Save as\xE2\x80\xA6")};
    Toggle cancel_{"Cancel"}, confirm_{"Save"};
    OutsideClick outside_;

    juce::Component* top_ = nullptr;
    juce::Rectangle<int> anchor_;
    std::vector<const Preset*> all_, shown_;
    std::vector<juce::String> tagNames_;
    int highlighted_ = -1;
    bool form_ = false, formOnly_ = false, replace_ = false, browsing_ = false;
    juce::String problem_;
};

/// A module's presets in its header, the same in both hosts (presets.md §6–8): previous and next
/// around the preset's name, then the A/B button. It opens the list and holds the commands.
class PresetBar final : public juce::Component
{
public:
    /// `context.presets` must be set.
    explicit PresetBar(DeviceUiContext& context);
    ~PresetBar() override;

    /// The narrowest it lays out to (the field at its minimum), and the widest.
    [[nodiscard]] int minimumWidth() const;
    [[nodiscard]] int maximumWidth() const;
    /// The name, its mark and A/B, from the panel's timer.
    void refresh();

    /// Opens the list (on the save form with `saveAs`); closes it.
    void openList(bool saveAs = false);
    void closeList(bool keep);
    [[nodiscard]] bool isListOpen() const;
    [[nodiscard]] PresetList& list() noexcept { return *list_; }
    [[nodiscard]] PresetController& controller() noexcept { return controller_; }
    [[nodiscard]] DeviceUiContext& context() noexcept { return context_; }

    /// The preset commands: the name's right-click menu (the DAW's Module menu lists the same).
    void addCommands(juce::PopupMenu& menu);
    /// Steps to the next (+1) or previous (-1) preset of the list as last filtered.
    void step(int direction);

    /// What the list was last filtered by: previous and next step through it.
    struct Filter
    {
        juce::String query;
        std::set<std::string> tags;
        bool favourites = false;
    };
    Filter filter;
    [[nodiscard]] std::vector<const Preset*> filtered() const;

    [[nodiscard]] PresetField& field() noexcept { return field_; }
    [[nodiscard]] IconButton& previousButton() noexcept { return previous_; }
    [[nodiscard]] IconButton& nextButton() noexcept { return next_; }
    [[nodiscard]] Toggle& compareButton() noexcept { return compare_; }

    void resized() override;

private:
    [[nodiscard]] int compareWidth() const;

    DeviceUiContext& context_;
    PresetController controller_;
    IconButton previous_{Icon::ChevronLeft, "previous preset"}, next_{Icon::ChevronRight, "next preset"};
    PresetField field_;
    Toggle compare_{"A"};
    std::unique_ptr<PresetList> list_;
    int ticks_ = 0;
};
} // namespace fd::ui
