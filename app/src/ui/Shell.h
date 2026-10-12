#pragma once

#include "model/AppSettings.h"
#include "ui/AboutPopover.h"
#include "ui/ActionRegistry.h"
#include "ui/ArrangementView.h"
#include "ui/MixerView.h"
#include "ui/AssistantsPopover.h"
#include "ui/BrowserDrops.h"
#include "ui/BrowserPanel.h"
#include "ui/CommandPalette.h"
#include "ui/ExportPopover.h"
#include "ui/LanePicker.h"
#include "ui/Panels.h"
#include "ui/SettingsView.h"
#include "ui/ShellMenus.h"

#include <map>

namespace fd::app
{
/// The window content (docs/design/daw-shell.md, C1): transport bar, browser, arrangement,
/// inspector and detail panel; adaptive layout; every action in the registry; per-frame updates.
class Shell final : public juce::Component, public juce::DragAndDropContainer
{
public:
    static constexpr int compactWidth = 1440;

    /// `assistants`: the app's MCP server (the Assistants popover); null hides it.
    explicit Shell(engine::Session& session, mcp::McpServer* assistants = nullptr);
    ~Shell() override;

    [[nodiscard]] const ActionRegistry& actions() const noexcept { return actions_; }
    [[nodiscard]] CommandPalette& palette() noexcept { return palette_; }
    /// Shows a warning in the window (engine warnings; never a modal dialog).
    void showWarning(const juce::String& message);
    /// Shows a message with an action ("Show"): a click on the toast runs it.
    void showToast(const juce::String& message, const juce::String& action, std::function<void()> run);
    /// The export panel (export.md §6).
    [[nodiscard]] ExportPopover& exportPanel() noexcept { return exportPanel_; }
    /// The warning shown last (tests).
    [[nodiscard]] const juce::String& lastWarning() const noexcept { return warning_; }

    /// Runs `then` once unsaved changes are saved or discarded; nothing if cancelled. The one
    /// modal dialog the UI principles allow besides confirming destruction.
    void confirmDiscard(std::function<void()> then);
    /// The window's title ("name * - Segno": the document first) changed, or the unsaved mark.
    std::function<void(const juce::String&)> onTitleChanged;
    /// The project's name ("Untitled" before it's saved), for the title bar.
    [[nodiscard]] juce::String projectName() const;
    [[nodiscard]] bool hasUnsavedChanges() const;

    /// The menu bar (ADR 0040, docs/design/menu-bar.md): every action in File ... Help.
    [[nodiscard]] ShellMenus& menus() noexcept { return *menus_; }
    /// F10 or Alt+letter: open menu `index` (the title bar's; none on macOS).
    std::function<void(int index)> onOpenMenu;
    /// Alt held or let go: the menu bar underlines each menu's letter while it's held.
    std::function<void(bool shown)> onMnemonicsShown;
    /// What the window does for the shell (the main window sets them; without them, as in tests,
    /// the actions that need them are dimmed).
    struct WindowHooks
    {
        std::function<void()> quit;
        std::function<bool()> isFullScreen;
        std::function<void(bool)> setFullScreen;
        /// The system's title bar instead of ours (Linux, Windows).
        std::function<void(bool)> setSystemTitleBar;
    };
    WindowHooks window;

    /// Settings (ADR 0045): the app's, shared with the assistants' server. Without one, as in
    /// tests, the shell keeps its own and saves nothing.
    void setSettingsStore(model::SettingsStore& store);
    [[nodiscard]] const model::AppSettings& settings() const noexcept { return store_->get(); }
    [[nodiscard]] model::SettingsStore& settingsStore() noexcept { return *store_; }
    /// Settings, in the centre in place of the arrangement or the mixer (settings.md §4); `at`:
    /// the setting to scroll to and mark (a catalogue id). Closing brings back the view before.
    void showSettings(const std::string& at = {});
    void closeSettings();
    [[nodiscard]] bool settingsShown() const noexcept { return settingsShown_; }
    [[nodiscard]] SettingsView& settingsView() noexcept { return settingsView_; }
    /// The browser (ADR 0046).
    [[nodiscard]] BrowserPanel& browser() noexcept { return browser_; }
    /// Puts something from the browser on the selection (double-click or Enter, §10).
    void loadFromBrowser(const std::string& item);
    /// File > Collect files and save; File > Remove unused files… (browser.md §12).
    void collectFiles();
    void removeUnusedFiles();
    /// At compact widths: the browser over the arrangement, at a part, until a click elsewhere.
    void showBrowserOverlay(BrowserPanel::Part part);
    void closeBrowserOverlay();
    [[nodiscard]] bool browserOverlayShown() const noexcept { return browserOverlay_; }
    /// Help > About Segno.
    [[nodiscard]] AboutPopover& about() noexcept { return about_; }
    void setVersion(const juce::String& version) { about_.setVersion(version); }

    /// Ends every eased layout change at once (tests and snapshots, which get no frames).
    void finishAnimations();
    [[nodiscard]] DetailPanel& detail() noexcept { return detail_; }
    [[nodiscard]] ArrangementView& arrangement() noexcept { return arrangement_; }
    [[nodiscard]] InspectorPanel& inspector() noexcept { return inspector_; }
    [[nodiscard]] MixerView& mixer() noexcept { return mixer_; }
    [[nodiscard]] LanePicker& lanePicker() noexcept { return lanePicker_; }
    /// Ends typing in a field on a press elsewhere (tests drive its rule).
    [[nodiscard]] ui::EndEditingOnClickAway& clickAway() noexcept { return clickAway_; }

    /// Frame statistics overlay (paint time of the arrangement, frame rate): --stats.
    void setStatsVisible(bool visible);
    /// Scripted benchmark (--benchmark): fills the project with 32 tracks of clips, zooms in and
    /// out for `seconds`, then calls `done` with a one-line report.
    void runBenchmark(double seconds, std::function<void(juce::String)> done);

    void paint(juce::Graphics& g) override;
    void paintOverChildren(juce::Graphics& g) override;
    void resized() override;
    bool keyPressed(const juce::KeyPress& key) override;
    bool keyStateChanged(bool isKeyDown) override;
    /// The keys stop reaching the shell: the computer keyboard's held notes end, since their
    /// releases won't arrive here.
    void focusLost(FocusChangeType) override;
    void focusOfChildComponentChanged(FocusChangeType) override;
    /// Ends every note the computer keyboard holds.
    void releaseComputerKeyboard();
    /// Whether the computer keyboard holds a note (tests).
    [[nodiscard]] bool computerKeyboardHolds() const noexcept { return !heldKeys_.empty(); }

private:
    struct Panel
    {
        bool open = true;
        float shown = 1.0f; // eased towards open
        float size = 0.0f;  // px when fully open
    };

    void registerActions();
    void openProject();
    void saveProjectAs(std::function<void()> then = {});
    [[nodiscard]] std::string viewState() const;
    void applyViewState(const std::string& json);
    void updateTitle();
    std::unique_ptr<juce::FileChooser> chooser_;
    juce::String title_;
    std::optional<juce::File> titleFile_; // the project file the title was made for (recent projects)
    model::SettingsStore ownStore_{{}};
    model::SettingsStore* store_ = &ownStore_;
    int storeListener_ = 0;
    /// What the settings say for the shell, the window and the engine (at once, and on a change).
    void applySettings(const model::AppSettings& settings);
    /// The title bar option the window was last given: it's given again only when it changes,
    /// since every other setting (a browser folder opened) would otherwise lay the window out.
    std::optional<bool> systemTitleBar_;
    /// Opens `file`, after asking about unsaved changes (Open recent).
    void openFile(const juce::File& file);
    /// File > Open recent: the projects still on disk, then Clear the list.
    [[nodiscard]] juce::PopupMenu recentMenu();
    std::unique_ptr<ShellMenus> menus_;
    AboutPopover about_;

    // Tracks and clips (C11, ADR 0041, docs/design/tracks-and-clips.md).
    /// The track or return the track commands act on: the selected one (never the master).
    [[nodiscard]] std::optional<std::uint64_t> commandChannel() const;
    /// Its header was clicked last: Delete and Duplicate act on it (§5, §7).
    [[nodiscard]] bool trackHeaderActive() const;
    /// Deletes a channel, selecting the one above it; the toast says what went, with Undo.
    void deleteChannel(std::uint64_t channel);
    void duplicateChannel(std::uint64_t channel);
    /// Track > Colour and Clip > Colour (and the right-click menus): the palette for the selection.
    [[nodiscard]] juce::PopupMenu trackColourMenu();
    [[nodiscard]] juce::PopupMenu clipColourMenu();
    // The song's sections (ShellSong.cpp, ADR 0049): the Song menu's commands, a section's menu.
    void registerSongActions();
    [[nodiscard]] juce::PopupMenu sectionColourMenu();
    void sectionMenu(juce::PopupMenu& menu, std::uint64_t section);
    [[nodiscard]] bool sectionsActive() const; // sections are selected, in the arrangement used last
    /// The clipboard (§9): what Cut and Copy kept from the area used last.
    enum class Held { None, Clips, Notes, Points };
    Held held_ = Held::None;
    engine::Session::ClipClipboard heldClips_;
    std::vector<model::Note> heldNotes_;
    model::AutomationPoints heldPoints_;
    [[nodiscard]] bool canCopy() const;
    bool copySelection();
    void paste();
    bool mnemonicsShown_ = false;
    void onFrame(double timestamp);
    /// The selection or the project changed: the context panels follow the subject, per view
    /// (docs/design/selection.md).
    void selectionChanged();
    /// The detail panel's mode for the current view: the arrangement's (Modules or Clip), or
    /// Modules in the mixer view.
    void applyDetailMode();
    void setMixerShown(bool shown);
    void toggle(Panel& p);
    [[nodiscard]] std::optional<model::TrackInfo> selectedTrackInfo() const;

    engine::Session& session_;
    mcp::McpServer* assistants_ = nullptr;
    std::unique_ptr<AssistantsPopover> assistantsPopover_;
    ActionRegistry actions_;
    std::unique_ptr<TransportBar> transport_;
    BrowserPanel browser_{session_, actions_};
    bool browserOverlay_ = false;
    ui::OutsideClick browserOutside_{browser_, [this] { closeBrowserOverlay(); }};
    /// What a drop or a double-click from the browser did: select what it made, say what it replaced.
    void browserResult(const browser::Result& result);
    InspectorPanel inspector_{session_};
    DetailPanel detail_{session_, actions_};
    enum class Area { Arrangement, Detail, Clip, Lane };
    Area lastArea_ = Area::Arrangement; // where Delete, Duplicate and Select all apply
    [[nodiscard]] bool notesActive() const; // the piano roll was used last and shows a clip
    [[nodiscard]] bool laneActive() const;   // an automation lane was used last and is selected
    [[nodiscard]] bool pointsActive() const; // ... with points selected
    ArrangementView arrangement_;
    MixerView mixer_{session_};
    bool mixerShown_ = false; // the centre shows the mixer view instead of the arrangement
    SettingsView settingsView_{session_, actions_};
    bool settingsShown_ = false; // Settings in the centre, over whichever view was there
    Splitter browserSplit_, inspectorSplit_;
    Splitter detailSplit_{true}; // the clip view's top edge
    float deviceViewHeight_ = 0.0f, clipViewHeight_ = 320.0f; // the detail panel's height per mode
    // The arrangement's detail mode: picked when the subject changes, then Tab's (selection.md §4).
    DetailPanel::Mode arrangeMode_ = DetailPanel::Mode::Devices;
    std::optional<model::PanelContext::Key> subject_;
    /// For a selected lane: the channel whose module the detail panel shows, when one of its other
    /// assignments was clicked in the inspector (else its first assignment's).
    std::optional<std::uint64_t> laneDetailChannel_;
    /// The channel selected last: the parameter picker's first channel for a lane that drives nothing.
    std::optional<std::uint64_t> recentChannel_;
    model::CountIn lastCountIn_ = model::CountIn::OneBar; // what the count-in toggle turns back on
    CommandPalette palette_{actions_};
    ExportPopover exportPanel_{session_};
    LanePicker lanePicker_{session_};
    ui::EndEditingOnClickAway clickAway_{*this}; // a press elsewhere keeps what was typed
    /// The picker, assigning a parameter to `lane` (for its first assignment's channel, else the
    /// track it follows).
    void assignToLane(std::uint64_t lane);

    Panel browserPanel_{.open = true, .shown = 1.0f, .size = 220.0f};
    Panel inspectorPanel_{.open = true, .shown = 1.0f, .size = 260.0f};
    Panel detailPanel_{.open = true, .shown = 1.0f, .size = 0.0f};
    bool focusMode_ = false;
    std::optional<bool> compact_;

    struct Stats
    {
        std::vector<double> paintMs, frameMs;
        void clear()
        {
            paintMs.clear();
            frameMs.clear();
        }
    };
    [[nodiscard]] juce::String statsReport() const;

    bool statsVisible_ = false;
    Stats stats_;
    struct Benchmark
    {
        double until = 0.0;
        int frame = 0;
        std::function<void(juce::String)> done;
    };
    std::optional<Benchmark> benchmark_;
    juce::TemporaryFile benchmarkFile_{".wav"};

    // Computer keyboard as MIDI input (C7): A W S E D F T G Y H U J K O L P ; play an octave
    // and a third from C; Z / X shift octaves, C / V velocity.
    bool computerKeyboard_ = false;
    int keyboardOctave_ = 0;
    float keyboardVelocity_ = 0.8f;
    std::map<int, int> heldKeys_; // key code -> note
    bool handleComputerKeyboard(const juce::KeyPress& key);
    void shiftKeyboardOctave(int octaves);
    void shiftKeyboardVelocity(int steps);

    juce::String warning_;
    double warningUntil_ = 0.0;
    // A toast's action ("Show"), and the click that runs it while it shows.
    juce::String toastAction_;
    std::function<void()> toastRun_;
    struct ToastClick final : juce::Component
    {
        std::function<void()> onClick;
        void mouseUp(const juce::MouseEvent&) override
        {
            if (onClick)
                onClick();
        }
    };
    ToastClick toastClick_;
    bool exportProgressShown_ = false;
    double lastFrame_ = 0.0;
    juce::VBlankAttachment vblank_{this, [this](double t) { onFrame(t); }};
};
} // namespace fd::app
