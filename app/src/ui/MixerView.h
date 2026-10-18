#pragma once

#include "model/ChannelStripLayout.h"
#include "ui/ChannelControls.h"
#include "ui/TrackInputs.h"

#include <map>
#include <optional>
#include <set>

namespace fd::app
{
/// Where a strip stands among groups (groups.md §9): the colours of the groups it's in, outermost
/// first; a group's fold state; mute and solo half lit by its groups.
struct StripGroups
{
    std::vector<int> colours;
    std::optional<bool> folded; // a group's strip: its fold triangle, folded or not
    bool mutedByGroup = false;  // in a muted group
    bool holdsSolo = false;     // a group with something soloed in it
};

/// One channel as a strip (docs/design/mixer.md §6): its name, its modules, its sends, pan, mute
/// and solo, the peak readout, the fader with a dB scale and the meter, and the volume typed.
/// Placed by model::layoutChannelStrip, so strips line up.
class ChannelStrip final : public juce::Component
{
public:
    ChannelStrip(engine::Session& session, const model::TrackInfo& channel);

    [[nodiscard]] std::uint64_t channel() const noexcept { return info_.id; }
    using Groups = StripGroups;
    /// The channel as the snapshot has it, the project's return tracks, how strips look, and
    /// whether it is the selected one.
    void update(const model::TrackInfo& channel, const std::vector<model::TrackInfo>& returns,
                const model::ChannelStripOptions& options, bool selected, const Groups& groups = {});

    std::function<void()> onSelect;
    /// A press with its keys (groups.md §8): true when it kept several chosen together, and its
    /// release without a drag then selects it alone (onSelect).
    std::function<bool(juce::ModifierKeys)> onChoose;
    std::function<void(std::uint64_t module)> onSelectModule;
    /// A right-click on the strip's background (its controls have their own menus).
    std::function<void()> onMenu;
    /// Its name (tracks-and-clips.md §4, §6): a double-click renames it; dragging it moves the
    /// strip (the events are the strip's).
    std::function<void()> onRename;
    std::function<void(const juce::MouseEvent&)> onNameDrag, onNameDrop;
    /// A group's fold triangle clicked (groups.md §6.4): `all` with Alt, every group.
    std::function<void(bool all)> onFold;
    [[nodiscard]] juce::Rectangle<int> nameArea() const { return at(slots_.name); }
    /// A group's fold triangle (empty for anything else).
    [[nodiscard]] juce::Rectangle<int> foldArea() const { return at(slots_.fold); }

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;

private:
    bool nameDrag_ = false, nameMoving_ = false, pendingAlone_ = false;
    struct Send
    {
        std::unique_ptr<SendBar> level;
        std::unique_ptr<ChannelToggle> pre;
    };
    [[nodiscard]] juce::Rectangle<int> at(const model::HeaderRect& r) const;
    void tick(); // the peak readout follows the meter

    engine::Session& session_;
    model::TrackInfo info_;
    model::ChannelStripOptions options_;
    bool selected_ = false;
    Groups groups_;
    model::ChannelStripSlots slots_;
    VolumeBar fader_;
    PanBar pan_;
    ChannelToggle mute_, solo_, arm_;
    InputButton input_; // tracks: what it listens to (recording.md §3)
    ChannelMeter meter_;
    VolumeField value_;
    std::vector<Send> sends_;
    std::vector<std::uint64_t> sendReturns_;
    float shownPeak_ = -1.0f;
    juce::VBlankAttachment vblank_{this, [this] { tick(); }};
};

/// The mixer view (C3c, ADR 0026): every channel as a strip, side by side, in place of the
/// arrangement. Tracks, then the return tracks after a divider, scroll horizontally; the master
/// is pinned at the right. The header's view options show or hide module names and sends and
/// make strips narrow; they are view state (saved with the project, never undo steps).
class MixerView final : public juce::Component, public juce::DragAndDropTarget
{
public:
    explicit MixerView(engine::Session& session);

    void setSnapshot(std::shared_ptr<const model::Snapshot> snapshot);
    /// The selected channel, and the tracks and groups chosen together with it (groups.md §8):
    /// their strips lit, and dragged together.
    void setSelection(std::optional<std::uint64_t> channel, std::vector<std::uint64_t> together = {});
    /// The tracks' and groups' strips shown, in order: what Shift+click takes a run of.
    [[nodiscard]] std::vector<std::uint64_t> shownStrips() const;
    /// Drops every strip (before the project is replaced: they hold the engine's meters).
    void clear();

    /// A strip was clicked: select that channel (and, for a module name, that module).
    std::function<void(std::uint64_t channel)> onSelect;
    /// A strip pressed with its keys (groups.md §8): Ctrl adds or takes out, Shift the run. True
    /// when it kept several chosen together (its release alone then selects it alone, onSelect).
    std::function<bool(std::uint64_t channel, juce::ModifierKeys mods)> onChoose;
    std::function<void(std::uint64_t channel, std::uint64_t module)> onSelectModule;
    /// The view's options changed (the shell saves them with its view state).
    std::function<void()> onViewChanged;
    /// Adds the items that make channels (Add track, Add return track) to a right-click menu: a
    /// strip's background's, and the empty space's beside the strips.
    std::function<void(juce::PopupMenu&)> onAddMenu;
    /// Something from the browser dropped (browser.md §10): on a channel's strip, or beside the
    /// strips (0: a new track).
    std::function<void(const std::string& item, std::uint64_t channel)> onBrowserDrop;
    bool isInterestedInDragSource(const SourceDetails& details) override;
    void itemDragMove(const SourceDetails& details) override;
    void itemDragExit(const SourceDetails& details) override;
    void itemDropped(const SourceDetails& details) override;
    void paintOverChildren(juce::Graphics& g) override;

    [[nodiscard]] const model::ChannelStripOptions& options() const noexcept { return options_; }
    void setOptions(bool modules, bool sends, bool narrow);
    /// The groups folded in the mixer, apart from the arrangement's (groups.md §6.4): their
    /// members' strips are hidden. The view's state, kept here alone; never an undo step.
    void setFoldedGroups(std::set<std::uint64_t> folded);
    [[nodiscard]] const std::set<std::uint64_t>& foldedGroups() const noexcept { return folded_; }
    /// Folds a group here, or unfolds it; with `all`, every group the same way (§6.4).
    void toggleFold(std::uint64_t group, bool all = false);
    [[nodiscard]] int scrollX() const;
    void setScrollX(int x);
    /// Renames a channel's strip in place (F2, tracks-and-clips.md §4). False for the master.
    bool renameChannel(std::uint64_t channel);
    [[nodiscard]] ui::NameEditor& nameEditor() noexcept { return names_; }
    /// Where a dragged strip would land (content x; -1: none being dragged; tests).
    [[nodiscard]] int dropX() const noexcept { return content_.dropX; }

    void paint(juce::Graphics& g) override;
    void resized() override;
    bool keyPressed(const juce::KeyPress& key) override;

private:
    struct Content : juce::Component
    {
        int dividerX = -1; // before the first return
        int dropX = -1;    // where a dragged strip lands (tracks-and-clips.md §6)
        juce::Rectangle<int> dropGroup; // the group it lands in, lit (groups.md §5.3)
        void paintOverChildren(juce::Graphics& g) override;
        std::function<void(const juce::MouseEvent&)> onMouseDown;
        void paint(juce::Graphics& g) override;
        void mouseDown(const juce::MouseEvent& e) override
        {
            if (onMouseDown)
                onMouseDown(e);
        }
    };
    void showAddMenu();
    /// A strip's name dragged: where it would land among its kind, then dropped there.
    void dragStrip(std::uint64_t channel, const juce::MouseEvent& e, bool drop);
    void dragReturn(std::uint64_t channel, float x, bool drop);
    void rebuild();
    void layoutStrips();
    [[nodiscard]] std::vector<std::uint64_t> order() const;

    engine::Session& session_;
    std::shared_ptr<const model::Snapshot> snapshot_;
    std::optional<std::uint64_t> selected_;
    std::vector<std::uint64_t> together_; // chosen with it (groups.md §8)
    std::set<std::uint64_t> folded_;
    model::ChannelStripOptions options_;
    ui::PanelHeader header_;
    ui::ViewOption modules_{"Modules"}, sends_{"Sends"}, narrow_{"Narrow"};
    juce::Viewport viewport_;
    Content content_;
    std::map<std::uint64_t, std::unique_ptr<ChannelStrip>> strips_;
    std::unique_ptr<ChannelStrip> master_;
    /// The strip a browser drag is over (0: the empty space, a new track); none while not dragging.
    std::optional<std::uint64_t> browserTarget_;
    [[nodiscard]] std::uint64_t stripAt(juce::Point<int> position) const;
    ui::NameEditor names_{content_};
};
} // namespace fd::app
