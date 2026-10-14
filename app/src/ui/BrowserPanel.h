#pragma once

#include "engine/Session.h"
#include "model/AppSettings.h"
#include "model/Browser.h"
#include "ui/ActionRegistry.h"
#include "ui/WaveformCache.h"

#include <fd_ui/fd_ui.h>
#include <map>
#include <memory>
#include <set>

namespace fd::app
{
/// The browser (docs/design/browser.md, ADR 0046): the panel on the left. One search over
/// modules, presets, plugins and the files of your places; a tree of Favourites, Instruments,
/// Audio effects, Plugins and Places, where each module is a folder of its presets; previews on
/// selection; drags into the song. At compact widths, a rail of its parts. Message thread.
class BrowserPanel final : public juce::Component, private juce::TextEditor::Listener, private juce::Timer
{
public:
    BrowserPanel(engine::Session& session, const ActionRegistry& actions);
    ~BrowserPanel() override;

    /// Where favourites, open folders, places and the preview switch are kept.
    void setStore(model::SettingsStore& store);
    /// Reads what there is again (the index, presets, plugins or settings changed).
    void refresh();
    /// The rail at compact widths (ui-principles §2).
    void setRail(bool rail);
    [[nodiscard]] bool isRail() const noexcept { return rail_; }
    /// Over the arrangement (compact widths): an edge where it ends.
    void setOverlay(bool overlay);
    /// Search the browser (Ctrl+F): the keyboard in the search.
    void focusSearch();

    /// The browser's parts, as the rail lists them.
    enum class Part
    {
        Search,
        Favourites,
        Instruments,
        Effects,
        Plugins,
        Places,
    };
    /// Opens a part and scrolls to it (the rail's buttons, after the shell shows the panel).
    void showPart(Part part);

    /// Double-click or Enter on an item (an id as model::BrowserItem has it): the shell puts it
    /// on the selection.
    std::function<void(const std::string& item)> onLoad;
    /// Double-click on a project.
    std::function<void(const juce::File& project)> onOpenProject;
    /// A rail button: the shell shows the panel over the arrangement, at that part.
    std::function<void(Part part)> onRailOpen;
    /// A drag left the panel (the overlay at compact widths closes).
    std::function<void()> onDragOut;
    /// Escape with nothing to clear: the keyboard goes back to the arrangement.
    std::function<void()> onDone;
    /// The track a MIDI file's preview plays on (the selected one).
    std::function<std::optional<std::uint64_t>()> selectedTrack;

    // ---- for tests ---------------------------------------------------------------------------

    [[nodiscard]] juce::TextEditor& searchField() noexcept { return search_; }
    /// The rows shown now, by id ("cat:instruments", "module:fd:eq", "preset:fd:eq|factory/…",
    /// "place:<path>", "file:<path>", "group:SAMPLES", "more:sample"…), and their names.
    [[nodiscard]] std::vector<std::string> rowIds() const;
    [[nodiscard]] std::vector<juce::String> rowNames() const;
    [[nodiscard]] juce::Rectangle<int> rowBounds(const std::string& id) const;
    /// Selects a row as a click would (previewing it, if Preview is on); false if not shown.
    bool select(const std::string& id);
    [[nodiscard]] std::string selected() const;
    /// Opens or closes a row.
    void toggle(const std::string& id);
    /// What Enter does with the selection.
    void loadSelected();
    [[nodiscard]] bool stripShown() const;
    [[nodiscard]] std::vector<std::string> tagsShown() const;
    void setTags(std::set<std::string> tags);
    void setFavourite(const std::string& item, bool favourite);
    [[nodiscard]] bool isFavourite(const std::string& item) const;
    /// The drag a row starts (what targets receive).
    [[nodiscard]] juce::var dragDescription(const std::string& id) const;

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    class List;
    class Strip;
    struct Row
    {
        enum class Type
        {
            Heading,      // PLACES
            Category,     // Favourites, Instruments, Audio effects, Plugins
            Module,       // one of ours: loads; opens to its presets
            PresetFolder, // Yours, Bass…
            Preset,
            Maker, // a plugin maker
            Plugin,
            Place,
            Folder,
            File,
            AddPlace,
            Group, // a kind of search result
            More,  // "All 240 samples"
            Empty, // "Nothing matches"
        };
        Type type = Type::Empty;
        std::string id;   // the row's own (open state, selection)
        std::string item; // the browser item it puts in the song, if any
        juce::String name, caption;
        int depth = 0;
        bool opens = false, open = false;
        bool missing = false; // a place that's gone
        bool library = false; // the Library's place (ADR 0055)
        bool flat = false;    // a search result: no disclosure column
        model::BrowserKind kind = model::BrowserKind::Module;
        juce::File file; // places, folders, files
    };

    void textEditorTextChanged(juce::TextEditor&) override;
    void textEditorReturnKeyPressed(juce::TextEditor&) override;
    void textEditorEscapeKeyPressed(juce::TextEditor&) override;
    bool keyPressed(const juce::KeyPress& key) override;
    void timerCallback() override;

    [[nodiscard]] const model::AppSettings& settings() const { return store_->get(); }
    void rebuildItems();
    void rebuildRows();
    void buildTree();
    void buildResults();
    void addModuleRows(const engine::AvailableDevice& device, int depth);
    void addPresetFolder(const model::PresetFolder& folder, const std::vector<model::PresetEntry>& entries,
                         const std::string& deviceKey, const std::string& path, int depth);
    void addFolderRows(const juce::File& folder, int depth, bool segno);
    [[nodiscard]] const std::vector<juce::File>& listing(const juce::File& folder);
    [[nodiscard]] bool isOpen(const std::string& id) const;
    void setOpen(const std::string& id, bool open);
    [[nodiscard]] int indexOf(const std::string& id) const;
    void selectIndex(int index, bool preview);
    void preview(const Row& row);
    [[nodiscard]] bool previewable(const Row& row) const;
    void activate(const Row& row);
    void showMenu(const Row& row);
    void addPlace();
    [[nodiscard]] bool searching() const;
    void startDrag(const Row& row);
    void layoutTags();
    void scrollToRow(int index);

    engine::Session& session_;
    const ActionRegistry& actions_;
    model::SettingsStore ownStore_{{}};
    model::SettingsStore* store_ = &ownStore_;
    int storeListener_ = 0;

    ui::PanelHeader header_;
    ui::ViewOption previewOption_{"Preview"};
    juce::TextEditor search_;
    std::vector<std::unique_ptr<ui::Toggle>> tagButtons_;
    std::set<std::string> tags_;
    std::unique_ptr<List> list_;
    juce::Viewport viewport_;
    std::unique_ptr<Strip> strip_;

    bool rail_ = false, overlay_ = false;
    std::vector<std::pair<Part, std::unique_ptr<ui::IconButton>>> railButtons_;
    ui::IconButton expand_{ui::Icon::PanelLeft};

    model::BrowserSearch search_model_;
    bool itemsDirty_ = true;
    std::vector<std::string> favourites_;  // as the items were made with
    std::set<std::string> expandedGroups_; // results groups shown whole
    std::vector<Row> rows_;
    int selected_ = -1;
    std::string selectedId_;
    std::map<std::string, std::vector<juce::File>> listings_; // a folder's contents, as last read
    WaveformCache waveforms_;
    std::unique_ptr<juce::FileChooser> chooser_;
};
} // namespace fd::app
