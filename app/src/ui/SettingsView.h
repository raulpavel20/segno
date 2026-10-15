#pragma once

#include "engine/Session.h"
#include "model/AppSettings.h"
#include "model/SettingsCatalogue.h"
#include "ui/ActionRegistry.h"

#include <fd_ui/fd_ui.h>
#include <map>
#include <memory>

namespace fd::app
{
/// Settings (docs/design/settings.md, ADR 0045): one searchable page in the centre of the window,
/// beside the arrangement and the mixer. Every change applies and is saved at once; each choice
/// marks its default; a section's title row shows Reset only while something in it differs from
/// its defaults. Message thread.
class SettingsView final : public juce::Component,
                           private juce::TextEditor::Listener,
                           private juce::Timer,
                           private juce::AsyncUpdater
{
public:
    SettingsView(engine::Session& session, ActionRegistry& actions);
    ~SettingsView() override;

    void setStore(model::SettingsStore& store);
    /// Linux and Windows: the system title bar is an option.
    void setSystemTitleBarOption(bool available);
    /// Reads every setting again (the store or the engine changed).
    void refresh();
    /// Scrolls to a setting (a catalogue id, or "key:<action id>") and marks it for a moment.
    void showSetting(const std::string& id);
    /// The ✕, or Escape: the view wants to close.
    std::function<void()> onClose;

    // ---- for tests ---------------------------------------------------------------------------

    [[nodiscard]] juce::TextEditor& searchField() noexcept { return search_; }
    void setChangedOnly(bool on);
    /// The settings shown now, in order (search and Changed only applied).
    [[nodiscard]] std::vector<std::string> shownSettings() const;
    [[nodiscard]] bool resetShown(model::SettingsSection section) const;
    void resetSection(model::SettingsSection section);
    /// A row's place in the page, and the part of the page in view (empty: not shown).
    [[nodiscard]] juce::Rectangle<int> rowBounds(const std::string& id) const;
    [[nodiscard]] juce::Rectangle<int> visibleArea() const;
    /// The page's column (its rows' controls, Reset, the conflict's answers are children of it).
    [[nodiscard]] juce::Component& page() noexcept;
    /// A setting's controls (its combo, toggle, field…), in order.
    [[nodiscard]] std::vector<juce::Component*> controlsOf(const std::string& id) const;
    /// The panel a setting has open under it (Audio offset's Measure…), else null.
    [[nodiscard]] juce::Component* openPanel(const std::string& id) const;
    /// Keys: start changing an action's shortcut; the next key pressed sets it.
    void startKeyCapture(const std::string& actionId);
    /// A key pressed while capturing. Returns whether it was taken.
    bool captureKey(const juce::KeyPress& key);
    /// The conflict waiting for an answer ("Ctrl+D is Duplicate's"), empty if none; answer it.
    [[nodiscard]] juce::String keyConflict() const { return conflict_; }
    void answerKeyConflict(bool replace);

    void paint(juce::Graphics& g) override;
    void resized() override;
    bool keyPressed(const juce::KeyPress& key) override;
    void visibilityChanged() override;

private:
    struct Row;
    class Content;
    class Index;
    class KeyButton;

    void textEditorTextChanged(juce::TextEditor&) override;
    void textEditorEscapeKeyPressed(juce::TextEditor&) override;
    void timerCallback() override;
    void handleAsyncUpdate() override; // rebuild, outside the callback of a control it removes

    /// Makes every row again (the lists in it changed: devices, folders, actions).
    void rebuild();
    /// What decides whether rows must be made again: the lists' contents.
    [[nodiscard]] std::string signature() const;
    void filter();
    void layoutContent();
    /// Whether anything in `section` differs from its defaults (its Reset shows).
    [[nodiscard]] bool sectionChanged(model::SettingsSection section) const;

    // Rows of each section.
    void addAudio();
    void addMidi();
    void addControllers();
    void addRecording();
    void addLibrary();
    void addPlugins();
    void addExport();
    void addKeys();
    void addDisplay();
    void addAssistants();

    Row& addRow(const std::string& id, model::SettingsSection section);
    /// Opens or closes the panel under a row, and lays out again.
    void setPanelOpen(Row& row, bool open);
    juce::ComboBox& combo(Row& row);
    ui::Toggle& toggle(Row& row, const juce::String& text, bool onOff);
    void change(const std::function<void(model::AppSettings&)>& edit);
    [[nodiscard]] const model::AppSettings& settings() const { return store_->get(); }
    [[nodiscard]] juce::File segnoFolder() const;
    void moveSegnoFolder(const juce::File& to);
    void chooseFolder(const juce::String& title, std::function<void(const juce::File&)> chosen);

    engine::Session& session_;
    ActionRegistry& actions_;
    model::SettingsStore ownStore_{{}};
    model::SettingsStore* store_ = &ownStore_;
    bool titleBarOption_ = false;

    ui::SearchField search_;
    ui::ViewOption changedOnly_{"Changed only"};
    ui::IconButton close_{ui::Icon::Close, "close settings"};
    std::unique_ptr<Index> index_;
    std::unique_ptr<Content> content_;
    /// The column's viewport; the index follows it as it scrolls.
    struct Scroller final : juce::Viewport
    {
        std::function<void()> onScroll;
        void visibleAreaChanged(const juce::Rectangle<int>&) override
        {
            if (onScroll)
                onScroll();
        }
    };
    Scroller viewport_;

    std::vector<std::unique_ptr<Row>> rows_;
    std::vector<std::unique_ptr<juce::Component>> components_;
    std::map<model::SettingsSection, std::unique_ptr<ui::Toggle>> resets_;
    ui::SectionStack stack_;
    std::vector<model::SettingsSection> shownSections_;
    std::string signature_;

    std::string capturing_;  // the action whose keys are being set
    juce::KeyPress pending_; // a key another action has, waiting for Replace
    juce::String conflict_;
    ui::Toggle replace_{"Replace"}, cancel_{"Cancel"}; // the conflict's answers
    std::string flash_;                                // the row marked by showSetting
    double flashUntil_ = 0.0;
    int xruns_ = 0; // dropouts when Settings opened
    std::unique_ptr<juce::FileChooser> chooser_;
};
} // namespace fd::app
