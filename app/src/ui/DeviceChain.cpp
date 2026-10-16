#include "ui/DeviceChain.h"

#include "model/DrumKit.h"
#include "model/Racks.h"
#include "ui/BrowserDrops.h"
#include "ui/MacroEditor.h"

#include <fd/device/Registry.h>
#include <map>

namespace fd::app
{
namespace
{
/// A third-party plugin in the chain (ui-principles §7): power, name and its editor button.
/// The plugin's own editor opens in a floating window.
class PluginCard final : public juce::Component
{
public:
    PluginCard(engine::Session& session, const model::DeviceSlot& slot) : session_(session), id_(slot.id), name_(slot.name)
    {
        power_.setClickingTogglesState(true);
        power_.setTooltip("Plugin on/off");
        power_.onClick = [this] { session_.setDeviceEnabled(id_, power_.getToggleState()); };
        addAndMakeVisible(power_);
        open_.setTooltip("Open the plugin's editor");
        open_.onClick = [this] { session_.showPluginEditor(id_); };
        addAndMakeVisible(open_);
        addChildComponent(notice_);
        setEnabledState(slot.enabled);
    }

    void setEnabledState(bool on) { power_.setToggleState(on, juce::dontSendNotification); }
    /// What the DAW says about it (DevicePanel::setHostNotice), under its kind; empty: nothing.
    void setNotice(const juce::String& text, const juce::String& brief, const juce::String& tooltip)
    {
        if (text == noticeText_)
            return;
        noticeText_ = text;
        const auto& grid = ui::Theme::current().deviceGrid;
        const int room = getWidth() - 2 * ui::Theme::current().grid * 2;
        notice_.show(text.isEmpty() || ui::HeaderBadge::widthFor(text) <= room ? text : brief, tooltip);
        notice_.setVisible(text.isNotEmpty());
        notice_.setSize(notice_.preferredWidth(), grid.row);
        resized();
    }
    [[nodiscard]] const ui::HeaderBadge* notice() const { return notice_.isVisible() ? &notice_ : nullptr; }

    void paint(juce::Graphics& g) override
    {
        const auto& t = ui::Theme::current();
        g.setColour(t.surface2);
        g.fillRoundedRectangle(getLocalBounds().toFloat(), t.radiusPanel);
        auto header = getLocalBounds().removeFromTop(t.deviceHeaderHeight).reduced(t.grid * 2, 0).withTrimmedLeft(18);
        g.setColour(t.textPrimary);
        g.setFont(ui::font(ui::TextStyle::Title));
        g.drawText(juce::String::fromUTF8(name_.c_str()), header, juce::Justification::centredLeft, true);
        g.setColour(t.textTertiary);
        g.setFont(ui::font(ui::TextStyle::Caption));
        g.drawText("Plugin", getLocalBounds().withTrimmedTop(t.deviceHeaderHeight).reduced(t.grid * 2).removeFromTop(16),
                   juce::Justification::centredLeft, false);
    }

    void resized() override
    {
        const auto& t = ui::Theme::current();
        auto header = getLocalBounds().removeFromTop(t.deviceHeaderHeight).reduced(t.grid * 2, 0);
        power_.setBounds(header.removeFromLeft(14).withSizeKeepingCentre(14, 14));
        open_.setBounds(getLocalBounds().withTrimmedTop(t.deviceHeaderHeight).reduced(t.grid * 3).removeFromBottom(24));
        // The notice under "Plugin", a row's gap below it.
        auto body = getLocalBounds().withTrimmedTop(t.deviceHeaderHeight).reduced(t.grid * 2);
        body.removeFromTop(16 + t.grid);
        notice_.setBounds(body.removeFromTop(t.deviceGrid.row).withWidth(notice_.getWidth()));
    }

private:
    engine::Session& session_;
    std::uint64_t id_;
    std::string name_;
    ui::Toggle power_;
    ui::Toggle open_{"Open editor"};
    ui::HeaderBadge notice_{nullptr, nullptr, {}};
    juce::String noticeText_;
};

/// The device a module's slot is, for its parameters' text and specs (null for plugins and racks).
const DeviceInfo* infoOf(const model::DeviceSlot& slot)
{
    const auto* f = slot.kind == model::DeviceSlot::Kind::Native && slot.rack == nullptr
                        ? fd::findDevice(slot.deviceId)
                        : nullptr;
    return f != nullptr ? &f->info() : nullptr;
}

const ParamSpec* specOf(const model::DeviceSlot& slot, const std::string& param)
{
    if (const auto* info = infoOf(slot))
        for (const auto& p : info->params)
            if (p.id == param)
                return &p;
    return nullptr;
}
} // namespace

/// The scrolled strip of slots: draws the racks' frames behind them, and the selection and where
/// a drag lands over them.
class DeviceChain::Content final : public juce::Component
{
public:
    explicit Content(DeviceChain& owner) : owner_(owner) {}

    void paint(juce::Graphics& g) override
    {
        for (const auto& r : owner_.racks_)
        {
            auto frame = r.frame;
            frame.dropInside = owner_.drop_ && owner_.drop_->rack == r.id;
            paintRackFrame(g, frame);
            if (!r.emptyHint.isEmpty()) // a kit's empty pad takes a sound (drum-kit.md §6)
                ui::paintEmptyState(g, r.emptyHint,
                                    r.kit ? "Drop a sample or an instrument here" : "Drop modules here");
        }
    }

    void paintOverChildren(juce::Graphics& g) override
    {
        const auto& t = ui::Theme::current();
        for (const auto& s : owner_.slots_)
            if ((owner_.selected_ && s.info.id == *owner_.selected_) || owner_.picked_.contains(s.info.id))
                if (s.view->isVisible())
                {
                    g.setColour(t.active);
                    g.drawRoundedRectangle(s.view->getBounds().toFloat().reduced(0.5f), t.radiusPanel, 1.0f);
                }
        if (owner_.browserOn_)
            for (const auto& s : owner_.slots_)
                if (s.info.id == *owner_.browserOn_)
                {
                    g.setColour(t.active);
                    g.drawRect(s.view->getBounds(), t.shell.dropLine);
                }
        // A modulator's handle hovered: a line to each control it moves (modulators.md §7.4).
        for (const auto& c : owner_.cables())
            ui::paintModulationCable(g, c.from, c.to);
        // Its handle dragged: its chip at the pointer.
        if (owner_.draggedModulator_ != 0 && owner_.dragChip_.isValid())
        {
            const auto& chip = owner_.dragChip_;
            const float scale = 1.0f / 2.0f; // the snapshot's
            g.setOpacity(0.9f);
            g.drawImage(chip, juce::Rectangle<float>(static_cast<float>(chip.getWidth()) * scale,
                                                     static_cast<float>(chip.getHeight()) * scale)
                                  .withPosition(
                                      owner_.dragAt_.translated(t.shell.itemGap, t.shell.itemGap).toFloat()));
            g.setOpacity(1.0f);
        }
        if (!(owner_.dragging_ || owner_.browserDrag_) || !owner_.drop_)
            return;
        // Where it lands, before it lands (racks.md §5a): in a rack's chain, the line runs its
        // panels' height inside its lit frame; in the track's chain, the chain's whole height.
        const auto& d = *owner_.drop_;
        g.setColour(t.active);
        const int half = gap / 2;
        if (d.rack != 0)
        {
            const auto* r = owner_.rackOfConst(d.rack);
            if (r == nullptr)
                return;
            int x = r->endCap->getX() - half;
            int seen = 0;
            for (const auto& s : owner_.slots_)
                if (s.rack == d.rack && seen++ == d.index)
                {
                    x = s.view->getX() - half;
                    break;
                }
            const int top = t.shell.rackInset;
            g.fillRect(x - t.shell.dropLine / 2, top, t.shell.dropLine, t.devicePanelHeight);
            return;
        }
        int x = owner_.add_.getX() - half;
        int seen = 0;
        for (const auto& s : owner_.slots_)
        {
            if (s.rack != 0)
                continue;
            if (seen++ == d.index)
            {
                const auto* r = owner_.rackOfConst(s.info.id);
                x = (r != nullptr ? r->frame.bounds.getX() : s.view->getX()) - half;
                break;
            }
        }
        g.fillRect(x - t.shell.dropLine / 2, 0, t.shell.dropLine, getHeight());
    }

private:
    DeviceChain& owner_;
};

DeviceChain::DeviceChain(engine::Session& session, const ActionRegistry& actions)
    : session_(session), actions_(actions), content_(std::make_unique<Content>(*this))
{
    viewport_.setViewedComponent(content_.get(), false);
    viewport_.setScrollBarsShown(false, true);
    viewport_.setScrollBarThickness(6);
    addAndMakeVisible(viewport_);
    add_.setTooltip("Add a module");
    add_.setWantsKeyboardFocus(false);
    add_.onClick = [this]
    {
        if (!track_)
            return;
        showAddMenu(
            add_,
            [this, track = *track_](const std::string& key) { selected_ = session_.addDevice(track, key); },
            [this](juce::PopupMenu& menu)
            {
                menu.addSeparator();
                menu.addItem("Rack", [this] { addRack(); }); // an empty one (racks.md §5)
            },
            true);
    };
    content_->addAndMakeVisible(add_);
    session_.addListener(this);
    session_.macroMenu = [this](std::uint64_t module, const std::string& param, juce::PopupMenu& menu)
    { addMacroItems(module, param, menu); };
    session_.modulatorMenu = [this](const engine::Session::ParamTarget& target, juce::PopupMenu& menu,
                                    juce::PopupMenu& with) { addModulatorItems(target, menu, with); };
}

DeviceChain::~DeviceChain()
{
    closeRoutes();
    session_.macroMenu = nullptr;
    session_.modulatorMenu = nullptr;
    session_.setMacroPointer({});
    session_.setModulatorPointer({});
    session_.removeListener(this);
    for (auto& s : slots_)
    {
        s.view->removeMouseListener(this);
        if (onModuleGone)
            onModuleGone(s.info.id);
    }
    slots_.clear(); // panels before their handles' bindings
}

int DeviceChain::preferredHeight()
{
    // A rack's frame round a panel, its label strip under it, and the scrollbar's room.
    const auto& t = ui::Theme::current();
    return t.shell.rackInset + t.devicePanelHeight + t.shell.rackLabelHeight + t.shell.rackInset + 16;
}

void DeviceChain::setTrack(std::optional<model::TrackInfo> track)
{
    const auto id = track ? std::optional(track->id) : std::nullopt;
    if (id != track_)
    {
        selected_.reset();
        picked_.clear();
        track_ = id;
    }
    rebuild(track ? track->devices : std::vector<model::DeviceSlot>{});
}

void DeviceChain::rebuild(const std::vector<model::DeviceSlot>& devices)
{
    // What shows, in order: each module, and after a rack its shown chain's modules.
    struct Entry
    {
        const model::DeviceSlot* info;
        std::uint64_t rack;
        int chain;
    };
    std::vector<Entry> entries;
    for (const auto& d : devices)
    {
        entries.push_back({&d, 0, 0});
        if (d.rack != nullptr && d.rack->kit)
        {
            // A kit: its selected pad's modules, if it has a sound (drum-kit.md §8).
            for (size_t c = 0; c < d.rack->chains.size(); ++c)
                if (d.rack->chains[c].note == d.rack->selectedPad)
                    for (const auto& m : d.rack->chains[c].devices)
                        entries.push_back({&m, d.id, static_cast<int>(c)});
        }
        else if (d.rack != nullptr && !d.rack->chains.empty())
        {
            const auto shown = static_cast<size_t>(
                std::clamp(d.rack->shownChain, 0, static_cast<int>(d.rack->chains.size()) - 1));
            for (const auto& m : d.rack->chains[shown].devices)
                entries.push_back({&m, d.id, static_cast<int>(shown)});
        }
    }

    // Keep the views of devices that are still there (panels keep their state and analysers).
    std::map<std::uint64_t, Slot> old;
    for (auto& s : slots_)
        old.emplace(s.info.id, std::move(s));
    slots_.clear();
    for (const auto& e : entries)
    {
        const auto& d = *e.info;
        Slot slot;
        // A modulator that went into a rack or out of one is another object in the engine: its
        // panel is made again.
        if (auto it = old.find(d.id);
            it != old.end() && (it->second.info.rack != nullptr) == (d.rack != nullptr) &&
            (d.kind != model::DeviceSlot::Kind::Modulator || it->second.rack == e.rack))
        {
            slot = std::move(it->second);
            old.erase(it);
        }
        else if (d.kind == model::DeviceSlot::Kind::Modulator)
        {
            slot.modulator = session_.openModulator(d.id);
            if (slot.modulator != nullptr)
            {
                auto panel = std::make_unique<ModulatorPanel>(slot.modulator->context(),
                                                              slot.modulator->kind(), slot.modulator->host());
                wireModulatorPanel(*panel, d.id);
                slot.view = std::move(panel);
            }
        }
        else if (d.rack != nullptr && d.rack->kit)
        {
            slot.handle = session_.openRack(d.id); // its macros and its kits (drum-kit.md §8, §9)
            if (slot.handle != nullptr)
            {
                auto panel = std::make_unique<DrumKitPanel>(slot.handle->context());
                wireKitPanel(*panel, d.id);
                slot.view = std::move(panel);
            }
        }
        else if (d.rack != nullptr)
        {
            slot.handle = session_.openRack(d.id);
            if (slot.handle != nullptr)
            {
                auto panel = std::make_unique<RackPanel>(slot.handle->context());
                wireRackPanel(*panel, d.id);
                slot.view = std::move(panel);
            }
        }
        else if (d.kind == model::DeviceSlot::Kind::Native)
        {
            slot.handle = session_.openDevice(d.id);
            if (slot.handle != nullptr)
                slot.handle->context().showEditor =
                    [this, id = d.id](std::unique_ptr<ui::ModuleEditor> editor)
                {
                    selectDevice(id); // the module in view above its editor
                    if (onShowEditor)
                        onShowEditor(id, std::move(editor));
                };
            if (slot.handle != nullptr)
                if (const auto* factory = ui::findPanel(slot.handle->deviceId()))
                    slot.view = factory->create(slot.handle->context());
        }
        if (slot.view == nullptr)
            slot.view = std::make_unique<PluginCard>(session_, d);
        if (auto* panel = dynamic_cast<ui::DevicePanel*>(slot.view.get());
            panel != nullptr && !panel->onEnabledChange)
        {
            const auto id = d.id;
            panel->onEnabledChange = [this, id](bool on) { session_.setDeviceEnabled(id, on); };
            // View state lives on the module in the project (ADR 0022): panels are rebuilt on
            // every track switch, so the plugin tree is the source of truth.
            panel->setFoldable(true);
            panel->setViewState(session_.deviceViewState(id));
            panel->onViewChanged = [this, id, p = panel]
            {
                session_.setDeviceViewState(id, p->viewState());
                layoutSlots();
            };
        }
        slot.info = d;
        slot.rack = e.rack;
        slot.chain = e.chain;
        if (auto* rack = dynamic_cast<RackPanel*>(slot.view.get()); rack != nullptr && d.rack != nullptr)
            rack->setRack(juce::String::fromUTF8(d.name.c_str()), *d.rack);
        if (auto* kit = dynamic_cast<DrumKitPanel*>(slot.view.get()); kit != nullptr && d.rack != nullptr)
            kit->setKit(*d.rack);
        if (auto* modulator = dynamic_cast<ModulatorPanel*>(slot.view.get())) // its routes or roll changed
            modulator->refreshFromHost();
        if (auto* panel = dynamic_cast<ui::DevicePanel*>(slot.view.get()))
            panel->setEnabledState(d.enabled);
        else if (auto* card = dynamic_cast<PluginCard*>(slot.view.get()))
            card->setEnabledState(d.enabled);
        if (slot.view->getParentComponent() != content_.get())
        {
            content_->addAndMakeVisible(*slot.view);
            slot.view->addMouseListener(this, true); // header drags, selection, a knob's macro
        }
        slots_.push_back(std::move(slot));
    }
    for (auto& [id, s] : old)
    {
        s.view->removeMouseListener(this);
        if (onModuleGone)
            onModuleGone(id);
    }
    old.clear();

    // The racks shown: their frames and end caps (kept while they're there).
    std::map<std::uint64_t, std::unique_ptr<ui::IconButton>> caps;
    for (auto& r : racks_)
        caps[r.id] = std::move(r.endCap);
    racks_.clear();
    for (const auto& d : devices)
        if (d.rack != nullptr)
        {
            Rack r{.id = d.id, .info = d.rack, .kit = d.rack->kit};
            if (auto it = caps.find(d.id); it != caps.end())
                r.endCap = std::move(it->second);
            else
            {
                r.endCap = std::make_unique<ui::IconButton>(ui::Icon::Plus);
                r.endCap->setWantsKeyboardFocus(false);
                content_->addAndMakeVisible(*r.endCap);
            }
            r.endCap->setTooltip(r.kit ? "Add a module to the pad"
                                       : "Add a module to the rack's chain shown, or a chain beside it");
            const auto id = d.id;
            r.endCap->onClick = [this, id]
            {
                if (const auto* kit = rackOf(id); kit != nullptr && kit->kit && kit->info != nullptr)
                {
                    // Onto the selected pad, made if it's empty (drum-kit.md §8).
                    const int pad = kit->info->selectedPad;
                    showAddMenu(
                        *kit->endCap,
                        [this, id, pad](const std::string& key)
                        {
                            if (const auto added = session_.addDevice(
                                    engine::Session::ChainPlace{.rack = id, .pad = pad}, key))
                                selected_ = added;
                        },
                        {}, true);
                    return;
                }
                const auto* rack = rackOf(id);
                if (rack == nullptr || rack->info == nullptr)
                    return;
                const int chain = rack->info->shownChain;
                const bool room =
                    static_cast<int>(rack->info->chains.size()) < engine::Session::maxRackChains;
                showAddMenu(
                    *rack->endCap,
                    [this, id, chain](const std::string& key)
                    {
                        if (const auto added = session_.addDevice(
                                engine::Session::ChainPlace{.rack = id, .chain = chain}, key))
                            selected_ = added;
                    },
                    [this, id, room](juce::PopupMenu& menu)
                    {
                        menu.addSeparator();
                        menu.addItem("Add chain", room, false, [this, id] { session_.addRackChain(id); });
                    },
                    true); // a rack's chain takes modulators too (modulators.md §14)
            };
            racks_.push_back(std::move(r));
        }
    caps.clear();

    if (selected_ && slotOf(*selected_) == nullptr)
        selected_.reset();
    if (hoveredModulator_ != 0 && slotOf(hoveredModulator_) == nullptr)
        setHoveredModulator(0);
    if (routeListModulator_ != 0 && slotOf(routeListModulator_) == nullptr)
        closeRoutes();
    for (auto it = picked_.begin(); it != picked_.end();)
        it = slotOf(*it) == nullptr ? picked_.erase(it) : std::next(it);
    for (auto it = padLights_.begin(); it != padLights_.end();)
        it = rackOf(it->first) == nullptr ? padLights_.erase(it) : std::next(it);
    add_.setVisible(track_.has_value());
    updateMacroEditor();
    layoutSlots();
}

void DeviceChain::wireRackPanel(RackPanel& panel, std::uint64_t rack)
{
    panel.onShowChain = [this, rack](int chain) { session_.showRackChain(rack, chain); };
    panel.onChainOn = [this, rack](int chain, bool on) { session_.setRackChain(rack, chain, {.on = on}); };
    panel.onChainSolo = [this, rack](int chain, bool solo)
    { session_.setRackChain(rack, chain, {.solo = solo}); };
    panel.onChainVolume = [this, rack](int chain, float db)
    { session_.setRackChain(rack, chain, {.volumeDb = db}); };
    panel.onChainPan = [this, rack](int chain, float pan)
    { session_.setRackChain(rack, chain, {.pan = pan}); };
    panel.onChainGesture = [this](bool started) { beginGesture(started, "Chain mixer"); };
    panel.onChainMenu = [this, rack](int chain) { showChainMenu(rack, chain); };
    panel.onAddChain = [this, rack] { session_.addRackChain(rack); };
    panel.onRenameChain = [this, rack](int chain, const juce::String& name)
    { session_.setRackChain(rack, chain, {.name = name.toStdString()}); };
    panel.onRenameRack = [this, rack](const juce::String& name)
    {
        session_.renameRack(rack, name.toStdString());
        if (std::exchange(saveOnRename_, false) && onSaveRack)
            onSaveRack(rack);
    };
    wireMacros(panel, rack);
    // Snapshots, locks and Morph (racks.md §18).
    panel.onRecallSnapshot = [this, rack](int snapshot) { session_.recallSnapshot(rack, snapshot); };
    panel.onStoreSnapshot = [this, rack] { session_.storeSnapshot(rack); };
    panel.onSnapshotMenu = [this, rack](int snapshot) { showSnapshotMenu(rack, snapshot); };
    panel.onRenameSnapshot = [this, rack](int snapshot, const juce::String& name)
    { session_.setSnapshot(rack, snapshot, name.toStdString()); };
    panel.onLockMacro = [this, rack](int macro, bool locked) { session_.lockMacro(rack, macro, locked); };
    panel.onMorphOn = [this, rack](bool on) { session_.setMorph(rack, {.on = on}); };
    panel.onMorphBetween = [this, rack](int a, int b) { session_.setMorph(rack, {.a = a, .b = b}); };
}

void DeviceChain::wireMacros(MacroPanel& panel, std::uint64_t rack)
{
    panel.onRenameMacro = [this, rack](int macro, const juce::String& name)
    { session_.setMacro(rack, macro, std::nullopt, name.toStdString()); };
    panel.onEditMacros = [this, rack](int macro) { openMacroEditor(rack, macro); };
    // A macro's name: hovered, it lights what it sets (and dims the other macros' rings);
    // dragged onto a knob in the frame, it maps it (racks.md §4, §5a).
    panel.onMacroHover = [this, rack](int macro, bool hovered)
    {
        if (session_.macroPointer().dragging)
            return;
        session_.setMacroPointer(hovered ? engine::Session::MacroPointer{.rack = rack, .macro = macro}
                                         : engine::Session::MacroPointer{});
    };
    panel.onMacroDrag = [this, rack](int macro, const juce::MouseEvent& e) { macroDragged(rack, macro, e); };
    panel.onMacroDrop = [this, rack](int macro, const juce::MouseEvent&) { macroDropped(rack, macro); };
}

void DeviceChain::showSnapshotMenu(std::uint64_t rack, int snapshot)
{
    const auto* r = rackOf(rack);
    if (r == nullptr || snapshot < 0 || snapshot >= static_cast<int>(r->info->snapshots.size()))
        return;
    juce::PopupMenu menu;
    menu.addItem(juce::String::fromUTF8("Rename snapshot\xe2\x80\xa6"),
                 [this, rack, snapshot]
                 {
                     if (auto* panel = rackPanelOf(rack))
                         panel->renameSnapshot(snapshot);
                 });
    // Over it, where the macros are now; not while Morph sets them.
    menu.addItem("Update snapshot", !model::morphing(*r->info), false,
                 [this, rack, snapshot] { session_.storeSnapshot(rack, snapshot); });
    menu.addSeparator();
    menu.addItem("Remove snapshot", [this, rack, snapshot] { session_.removeSnapshot(rack, snapshot); });
    menu.showMenuAsync(juce::PopupMenu::Options());
}

const model::RackInfo* DeviceChain::selectedRackInfo() const
{
    if (const auto rack = selectedRack())
        if (const auto* r = rackOfConst(*rack))
            return r->info.get();
    return nullptr;
}

void DeviceChain::renameCurrentSnapshot()
{
    const auto rack = selectedRack();
    const auto* info = selectedRackInfo();
    if (!rack || info == nullptr || info->currentSnapshot < 0)
        return;
    if (auto* panel = rackPanelOf(*rack))
    {
        panel->setFolded(false, true);
        panel->renameSnapshot(info->currentSnapshot);
    }
}

void DeviceChain::deviceViewChanged(std::uint64_t deviceId)
{
    for (auto& s : slots_)
        if (auto* panel = dynamic_cast<ui::DevicePanel*>(s.view.get()); panel != nullptr && s.info.id == deviceId)
            if (const auto stored = session_.deviceViewState(deviceId); stored != panel->viewState())
            {
                panel->setViewState(stored);
                layoutSlots();
            }
}

void DeviceChain::layoutSlots()
{
    const auto& t = ui::Theme::current();
    const int h = t.devicePanelHeight, inset = t.shell.rackInset, top = inset;
    const int button = t.shell.buttonSize;
    int x = gap;
    const auto widthOf = [](juce::Component& view)
    {
        const auto* panel = dynamic_cast<ui::DevicePanel*>(&view);
        return panel != nullptr ? panel->preferredWidth() : cardWidth;
    };
    for (size_t i = 0; i < slots_.size();)
    {
        auto& s = slots_[i];
        auto* rack = s.info.rack != nullptr ? rackOf(s.info.id) : nullptr;
        if (rack == nullptr)
        {
            s.view->setVisible(true);
            s.view->setBounds(x, top, widthOf(*s.view), h);
            x += s.view->getWidth() + gap;
            ++i;
            continue;
        }
        // A rack: its frame round its panel, its shown chain's modules and its end cap
        // (racks.md §5a); folded, round its strip alone.
        const auto* panel = dynamic_cast<ui::DevicePanel*>(s.view.get());
        rack->folded = panel != nullptr && panel->isFolded();
        const int frameX = x;
        x += inset;
        s.view->setVisible(true);
        s.view->setBounds(x, top, widthOf(*s.view), h);
        x += s.view->getWidth() + gap;
        const int chainX = x;
        size_t j = i + 1;
        for (; j < slots_.size() && slots_[j].rack == rack->id; ++j)
        {
            auto& inner = slots_[j];
            inner.view->setVisible(!rack->folded);
            // A chain that's off is faded with its modules (racks.md §5a); a kit's pad too.
            const auto& chains = rack->info->chains;
            const bool on =
                inner.chain < static_cast<int>(chains.size()) && chains[static_cast<size_t>(inner.chain)].on;
            inner.view->setAlpha(on || (chains.size() < 2 && !rack->kit) ? 1.0f : t.roles.fadedAlpha);
            if (rack->folded)
                continue;
            inner.view->setBounds(x, top, widthOf(*inner.view), h);
            x += inner.view->getWidth() + gap;
        }
        rack->emptyHint = {};
        if (!rack->folded && j == i + 1)
        {
            rack->emptyHint = {x, top, cardWidth, h};
            x += cardWidth + gap;
        }
        rack->endCap->setVisible(!rack->folded);
        if (!rack->folded)
        {
            rack->endCap->setBounds(x, top, button, button);
            x += button;
        }
        else
            x -= gap;
        const auto& info = *rack->info;
        rack->frame.bounds = {frameX, 0, x + inset - frameX,
                              top + h + (rack->folded ? inset : t.shell.rackLabelHeight)};
        rack->frame.label = rack->folded
                                ? juce::Rectangle<int>()
                                : juce::Rectangle<int>(chainX, top + h, x - chainX, t.shell.rackLabelHeight);
        if (rack->kit)
        {
            // A kit's frame always names its pad (drum-kit.md §21), off and soloed as a chain's.
            const auto* pad = model::kit::padOn(info, info.selectedPad);
            rack->frame.text =
                rack->folded ? juce::String() : juce::String::fromUTF8(model::kit::frameLabel(info).c_str());
            rack->frame.chainOff = !rack->folded && pad != nullptr && !pad->on;
            rack->frame.soloed = !rack->folded && pad != nullptr && pad->solo;
        }
        else
        {
            const bool several = info.chains.size() > 1 && !rack->folded;
            const auto shown =
                static_cast<size_t>(std::clamp(info.shownChain, 0, static_cast<int>(info.chains.size()) - 1));
            rack->frame.text = several ? chainLabel(info, static_cast<int>(shown)) : juce::String();
            rack->frame.chainOff = several && !info.chains[shown].on;
            rack->frame.soloed = several && info.chains[shown].solo;
        }
        x = rack->frame.bounds.getRight() + gap;
        i = j;
    }
    add_.setBounds(x, top, button, button);
    content_->setSize(x + button + gap, top + h + t.shell.rackLabelHeight + inset);
    content_->repaint();
}

void DeviceChain::resized()
{
    viewport_.setBounds(getLocalBounds());
    layoutSlots();
}

void DeviceChain::paint(juce::Graphics& g)
{
    if (track_)
        return;
    ui::paintEmptyState(g, getLocalBounds(), "Select a track to see its modules");
}

void DeviceChain::showAddMenu(juce::Component& target, std::function<void(const std::string& key)> add,
                              std::function<void(juce::PopupMenu&)> extra, bool modulators)
{
    if (!track_)
        return;
    const auto devices = session_.availableDevices();
    juce::PopupMenu menu;
    auto section = [&](const juce::String& title, auto&& keep) {
        bool first = true;
        for (size_t i = 0; i < devices.size(); ++i)
            if (keep(devices[i]))
            {
                if (first)
                    menu.addSectionHeader(title);
                first = false;
                menu.addItem(static_cast<int>(i) + 1, juce::String::fromUTF8(devices[i].name.c_str()));
            }
    };
    section("Instruments", [](const auto& d) { return d.native && d.instrument; });
    section("Audio effects", [](const auto& d) { return d.native && !d.instrument && !d.modulator; });
    if (modulators) // the track's chain (not a rack's, for now): modulators.md §14
        section("Modulators", [](const auto& d) { return d.modulator; });
    if (extra)
        extra(menu);

    // Third-party plugins in one submenu, grouped by manufacturer (one submenu level at most).
    juce::PopupMenu plugins;
    std::map<std::string, std::vector<size_t>> byVendor;
    for (size_t i = 0; i < devices.size(); ++i)
        if (!devices[i].native)
            byVendor[devices[i].category].push_back(i);
    for (const auto& [vendor, items] : byVendor)
    {
        plugins.addSectionHeader(juce::String::fromUTF8(vendor.c_str()));
        for (auto i : items)
            plugins.addItem(static_cast<int>(i) + 1, juce::String::fromUTF8(devices[i].name.c_str()));
    }
    if (!byVendor.empty())
        plugins.addSeparator();
    plugins.addItem(-1, "Scan for plugins (" + actions_.shortcut("app.scanPlugins") + ")");
    menu.addSeparator();
    menu.addSubMenu("Plugins", plugins);

    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&target),
                       [this, devices, choose = std::move(add)](int result)
                       {
                           if (result == -1)
                               actions_.perform("app.scanPlugins");
                           else if (result > 0)
                               choose(devices[static_cast<size_t>(result - 1)].key);
                       });
}

ui::DevicePanel* DeviceChain::selectedPanel() const
{
    for (const auto& s : slots_)
        if (selected_ && s.info.id == *selected_)
            return dynamic_cast<ui::DevicePanel*>(s.view.get());
    return nullptr;
}

void DeviceChain::foldSelected()
{
    if (auto* panel = selectedPanel())
        panel->setFolded(!panel->isFolded(), true);
}

void DeviceChain::expandSelected()
{
    if (auto* panel = selectedPanel())
    {
        panel->setFolded(false, true);
        panel->setAllSectionsCollapsed(false, true);
    }
}

void DeviceChain::turnSelectedPage(int delta)
{
    if (auto* panel = selectedPanel())
    {
        panel->setFolded(false, true);
        panel->turnPage(delta, true);
    }
}

void DeviceChain::deleteSelected()
{
    if (selected_)
        session_.removeDevice(*selected_);
    selected_.reset();
    picked_.clear();
}

int DeviceChain::slotIndexOf(const juce::Component* c) const
{
    for (; c != nullptr; c = c->getParentComponent())
        for (size_t i = 0; i < slots_.size(); ++i)
            if (slots_[i].view.get() == c)
                return static_cast<int>(i);
    return -1;
}

DeviceChain::Slot* DeviceChain::slotOf(std::uint64_t id)
{
    for (auto& s : slots_)
        if (s.info.id == id)
            return &s;
    return nullptr;
}

const DeviceChain::Slot* DeviceChain::slotOf(std::uint64_t id) const
{
    for (const auto& s : slots_)
        if (s.info.id == id)
            return &s;
    return nullptr;
}

DeviceChain::Rack* DeviceChain::rackOf(std::uint64_t id)
{
    for (auto& r : racks_)
        if (r.id == id)
            return &r;
    return nullptr;
}

const DeviceChain::Rack* DeviceChain::rackOfConst(std::uint64_t id) const
{
    for (const auto& r : racks_)
        if (r.id == id)
            return &r;
    return nullptr;
}

RackPanel* DeviceChain::rackPanelOf(std::uint64_t id)
{
    auto* s = slotOf(id);
    return s != nullptr ? dynamic_cast<RackPanel*>(s->view.get()) : nullptr;
}

MacroPanel* DeviceChain::macroPanelOf(std::uint64_t id)
{
    auto* s = slotOf(id);
    return s != nullptr ? dynamic_cast<MacroPanel*>(s->view.get()) : nullptr;
}

DeviceChain::Drop DeviceChain::dropAt(juce::Point<int> p, std::optional<std::uint64_t> dragged) const
{
    // Inside a rack's frame, past its panel's middle: into its chain shown (racks.md §5).
    for (const auto& r : racks_)
    {
        if (r.folded || (dragged && *dragged == r.id) || !r.frame.bounds.contains(p))
            continue;
        const Slot* panel = nullptr;
        int index = 0;
        for (const auto& s : slots_)
        {
            if (s.info.id == r.id)
                panel = &s;
            else if (s.rack == r.id && p.x >= s.view->getBounds().getCentreX())
                ++index;
        }
        if (panel != nullptr && p.x >= panel->view->getBounds().getCentreX())
        {
            if (r.kit) // onto the selected pad: its chain, made if it's empty (drum-kit.md §6)
            {
                const int pad = r.info->selectedPad;
                int chain = -1;
                for (size_t c = 0; c < r.info->chains.size(); ++c)
                    if (r.info->chains[c].note == pad)
                        chain = static_cast<int>(c);
                return {r.id, chain, index, pad};
            }
            return {r.id, r.info->shownChain, index};
        }
    }
    // Along the track's chain, a rack and its frame as one.
    int index = 0;
    for (const auto& s : slots_)
    {
        if (s.rack != 0)
            continue;
        const auto* r = rackOfConst(s.info.id);
        const auto bounds = r != nullptr ? r->frame.bounds : s.view->getBounds();
        if (p.x >= bounds.getCentreX())
            ++index;
    }
    return {0, 0, index};
}

// ---- mouse: selection, reordering by the header, a knob's macro ------------------------------

void DeviceChain::mouseDown(const juce::MouseEvent& e)
{
    if (onActivated)
        onActivated();
    dragSlot_ = -1;
    dragging_ = false;
    const int index = slotIndexOf(e.eventComponent);
    if (index < 0)
        return;
    const auto& slot = slots_[static_cast<size_t>(index)];
    const auto inSlot = e.getEventRelativeTo(slot.view.get()).getPosition();
    // Our modules: the header's background or the folded strip, never a control in it. Plugin
    // cards: the header past the power toggle.
    const auto* panel = dynamic_cast<const ui::DevicePanel*>(slot.view.get());
    const bool handle = panel != nullptr ? e.eventComponent == slot.view.get() && panel->isDragHandle(inSlot)
                                         : inSlot.y < ui::Theme::current().deviceHeaderHeight && inSlot.x > 24;
    if (!handle)
        return;
    // A rack's header: its menu on a right-click; a modulator's: its own (modulators.md §7.4).
    if (e.mods.isPopupMenu())
    {
        if (slot.info.rack != nullptr && !slot.info.rack->kit)
        {
            selected_ = slot.info.id;
            showRackMenu(slot.info.id);
        }
        else if (slot.info.kind == model::DeviceSlot::Kind::Modulator)
        {
            selected_ = slot.info.id;
            content_->repaint();
            showModulatorMenu(slot.info.id);
        }
        return;
    }
    // Shift: the selection runs along the track's chain to here (Group into rack takes them).
    const auto id = slot.info.id;
    if (e.mods.isShiftDown() && selected_ && slot.rack == 0)
        if (const auto* from = slotOf(*selected_); from != nullptr && from->rack == 0)
        {
            std::vector<std::uint64_t> order;
            for (const auto& s : slots_)
                if (s.rack == 0)
                    order.push_back(s.info.id);
            auto a = std::find(order.begin(), order.end(), *selected_);
            auto b = std::find(order.begin(), order.end(), id);
            if (a > b)
                std::swap(a, b);
            picked_ = {a, b + 1};
            content_->repaint();
            return;
        }
    selected_ = id;
    picked_.clear();
    dragSlot_ = index;
    content_->repaint();
}

void DeviceChain::mouseDrag(const juce::MouseEvent& e)
{
    if (dragSlot_ < 0 || (!dragging_ && e.getDistanceFromDragStart() < 4))
        return;
    dragging_ = true;
    drop_ = dropAt(e.getEventRelativeTo(content_.get()).getPosition(),
                   slots_[static_cast<size_t>(dragSlot_)].info.id);
    content_->repaint();
}

void DeviceChain::mouseUp(const juce::MouseEvent&)
{
    if (dragging_ && dragSlot_ >= 0 && drop_ && track_)
    {
        const auto& d = *drop_;
        session_.moveDevice(
            slots_[static_cast<size_t>(dragSlot_)].info.id,
            engine::Session::ChainPlace{
                .channel = *track_, .rack = d.rack, .chain = d.chain, .index = d.index, .pad = d.pad});
        if (const int dropped = session_.lastDroppedRoutes(); dropped > 0 && d.rack != 0 && onRoutesDropped)
            onRoutesDropped(dropped);
    }
    dragging_ = false;
    dragSlot_ = -1;
    drop_.reset();
    content_->repaint();
}

void DeviceChain::mouseMove(const juce::MouseEvent& e)
{
    // A modulator's handle: its lines (modulators.md §7.4). A control: the handles of the
    // modulators that move it light.
    if (auto* handle = dynamic_cast<ui::ModulationHandle*>(e.eventComponent))
    {
        if (const int index = slotIndexOf(handle);
            index >= 0 && slots_[static_cast<size_t>(index)].info.kind == model::DeviceSlot::Kind::Modulator)
            setHoveredModulator(slots_[static_cast<size_t>(index)].info.id);
    }
    else if (hoveredModulator_ != 0 && draggedModulator_ == 0)
        setHoveredModulator(0);
    lightModulatorsOf(e.eventComponent);

    // On a knob a rack's or a kit's macro sets: that macro's name lights (racks.md §5a).
    int lit = -1;
    std::uint64_t litRack = 0;
    if (auto* knob = dynamic_cast<ui::ParamKnob*>(e.eventComponent))
        if (const int index = slotIndexOf(knob); index >= 0)
        {
            const auto& slot = slots_[static_cast<size_t>(index)];
            if (const auto* rack = slot.rack != 0 ? rackOf(slot.rack) : nullptr)
                if (const auto* info = infoOf(slot.info);
                    info != nullptr && knob->param() < info->params.size())
                    if (model::mappingOf(*rack->info, slot.info.id,
                                         std::string(info->params[knob->param()].id), &lit) != nullptr)
                        litRack = rack->id;
        }
    for (const auto& r : racks_)
        if (auto* panel = macroPanelOf(r.id))
            panel->setLitMacro(r.id == litRack ? lit : -1);
}

void DeviceChain::mouseExit(const juce::MouseEvent& e)
{
    if (dynamic_cast<ui::ModulationHandle*>(e.eventComponent) != nullptr && draggedModulator_ == 0)
        setHoveredModulator(0);
    lightModulatorsOf(nullptr);
    for (const auto& r : racks_)
        if (auto* panel = macroPanelOf(r.id))
            panel->setLitMacro(-1);
}

// ---- drops from the browser (browser.md §10) -----------------------------------------------

bool DeviceChain::isInterestedInDragSource(const SourceDetails& details)
{
    const auto item = browser::itemOf(details.description);
    if (!item || !track_)
        return false;
    const auto ref = browser::parse(*item);
    return ref && ref->kind != model::BrowserKind::Midi && ref->kind != model::BrowserKind::Project &&
           ref->kind != model::BrowserKind::Folder;
}

namespace
{
/// The modulator a browser item is, or is a preset of (modulators.md §14).
std::optional<model::ModulatorKind> modulatorOf(const std::string& item)
{
    const auto ref = browser::parse(item);
    if (!ref || (ref->kind != model::BrowserKind::Module && ref->kind != model::BrowserKind::Preset) ||
        !ref->key.starts_with("fd:"))
        return std::nullopt;
    return model::modulatorKind(std::string_view(ref->key).substr(3));
}
} // namespace

void DeviceChain::itemDragMove(const SourceDetails& details)
{
    const auto item = browser::itemOf(details.description);
    const auto inContent = content_->getLocalPoint(this, details.localPosition);
    browserDrag_ = true;
    browserOn_.reset();
    drop_ = dropAt(inContent, std::nullopt);
    // Over a kit's pad: onto it, lit as it lands (drum-kit.md §6).
    const auto pad = item && browser::fitsPad(*item) ? padAt(inContent) : std::nullopt;
    if (padDrop_ != pad)
    {
        if (padDrop_)
            if (auto* panel = kitPanelOf(padDrop_->first))
                panel->pads().setDropTarget(-1);
        padDrop_ = pad;
        if (padDrop_)
            if (auto* panel = kitPanelOf(padDrop_->first))
                panel->pads().setDropTarget(padDrop_->second);
    }
    if (padDrop_)
    {
        drop_.reset();
        content_->repaint();
        return;
    }
    if (item)
        for (const auto& s : slots_)
            if (s.view->isVisible() && s.view->getBounds().contains(inContent) &&
                browser::landsOn(*item, s.info))
            {
                browserOn_ = s.info.id;
                drop_.reset();
            }
    // A modulator (or its preset) over a control: it goes there, routed (§14); the track's controls
    // show they'd take it, as when a handle is dragged.
    if (item && track_ && modulatorOf(*item))
    {
        const auto over = browserOn_ ? std::nullopt : targetAt(inContent);
        if (over)
            drop_.reset();
        if (const engine::Session::ModulatorPointer pointer{
                .dragging = true, .over = over, .channel = *track_};
            !(session_.modulatorPointer() == pointer))
            session_.setModulatorPointer(pointer);
    }
    content_->repaint();
}

void DeviceChain::itemDragExit(const SourceDetails&)
{
    browserDrag_ = false;
    browserOn_.reset();
    drop_.reset();
    if (padDrop_)
        if (auto* panel = kitPanelOf(padDrop_->first))
            panel->pads().setDropTarget(-1);
    padDrop_.reset();
    if (session_.modulatorPointer().dragging && session_.modulatorPointer().modulator == 0)
        session_.setModulatorPointer({});
    content_->repaint();
}

void DeviceChain::itemDropped(const SourceDetails& details)
{
    itemDragMove(details);
    const auto on = browserOn_;
    const auto drop = drop_;
    const auto pad = padDrop_;
    const auto pointer = session_.modulatorPointer();
    itemDragExit(details);
    const auto item = browser::itemOf(details.description);
    if (!item || !track_)
        return;
    // Onto a kit's pad, or a sample into a kit's frame: onto its selected pad (drum-kit.md §6).
    if (pad)
    {
        session_.selectPad(pad->first, pad->second);
        browser::onPad(session_, *item, pad->first, pad->second);
        return;
    }
    if (drop && drop->pad >= 0)
        if (const auto ref = browser::parse(*item);
            ref && (ref->kind == model::BrowserKind::Sample || ref->kind == model::BrowserKind::Multisample))
        {
            browser::onPad(session_, *item, drop->rack, drop->pad);
            return;
        }
    // A modulator dropped on a control: added just before its module, with its preset, routed.
    if (const auto kind = modulatorOf(*item);
        kind && pointer.dragging && pointer.modulator == 0 && pointer.over)
    {
        const auto ref = browser::parse(*item);
        modulateWith(*pointer.over, *kind,
                     [this, ref](std::uint64_t id)
                     {
                         if (ref && ref->kind == model::BrowserKind::Preset)
                             browser::loadPreset(session_, id, *ref);
                     });
        return;
    }
    // A file on a module that takes it where it shows what it's for (a table on the Wavetable's
    // oscillator in view).
    if (on)
        if (const auto ref = browser::parse(*item))
            for (auto& s : slots_)
                if (auto* panel = dynamic_cast<ui::DevicePanel*>(s.view.get());
                    panel != nullptr && s.info.id == *on)
                {
                    if (ref->file != juce::File() && panel->takeFile(ref->file))
                        return;
                }
    if (!on && drop && drop->rack != 0)
    {
        if (onBrowserDropInRack)
            onBrowserDropInRack(*item, {.channel = *track_,
                                        .rack = drop->rack,
                                        .chain = drop->chain,
                                        .index = drop->index,
                                        .pad = drop->pad});
        return;
    }
    if (onBrowserDrop)
        onBrowserDrop(*item, *track_, drop ? drop->index : -1, on);
}

void DeviceChain::selectDevice(std::uint64_t id)
{
    // In a rack's chain not shown: that chain shows, and the rack unfolds.
    if (track_ && slotOf(id) == nullptr)
        if (const auto place = session_.placeOf(id); place && place->rack != 0)
        {
            session_.showRackChain(place->rack, place->chain);
            if (auto* panel = rackPanelOf(place->rack))
                panel->setFolded(false, true);
            selected_ = id;
            return; // shown once the snapshot follows
        }
    for (const auto& s : slots_)
        if (s.info.id == id)
        {
            selected_ = id;
            picked_.clear();
            const auto b = s.view->getBounds();
            if (b.getX() < viewport_.getViewPositionX() || b.getRight() > viewport_.getViewPositionX() + viewport_.getViewWidth())
                viewport_.setViewPosition(std::max(0, b.getX() - 8), 0);
            content_->repaint();
            return;
        }
}

// ---- racks ----------------------------------------------------------------------------------

std::vector<std::uint64_t> DeviceChain::pickedModules() const
{
    std::vector<std::uint64_t> out;
    for (const auto& s : slots_)
        if (s.rack == 0 &&
            (picked_.contains(s.info.id) || (picked_.empty() && selected_ && *selected_ == s.info.id)))
            out.push_back(s.info.id);
    return out;
}

void DeviceChain::groupPicked()
{
    const auto modules = pickedModules();
    if (const auto rack = session_.groupIntoRack(modules))
    {
        selected_ = rack;
        picked_.clear();
        if (const int dropped = session_.lastDroppedRoutes(); dropped > 0 && onRoutesDropped)
            onRoutesDropped(dropped);
    }
}

std::optional<std::uint64_t> DeviceChain::selectedRack() const
{
    if (!selected_)
        return std::nullopt;
    for (const auto& s : slots_)
        if (s.info.id == *selected_)
        {
            if (s.info.rack != nullptr)
                return s.info.id;
            if (s.rack != 0)
                return s.rack;
        }
    return std::nullopt;
}

void DeviceChain::ungroupSelectedRack()
{
    if (const auto rack = selectedRack())
    {
        const auto placed = session_.ungroupRack(*rack);
        selected_ = placed.empty() ? std::nullopt : std::optional(placed.front());
    }
}

void DeviceChain::addChainToSelectedRack()
{
    if (const auto rack = selectedRack())
        session_.addRackChain(*rack);
}

void DeviceChain::renameSelectedRack()
{
    if (const auto rack = selectedRack())
        if (auto* panel = rackPanelOf(*rack))
        {
            panel->setFolded(false, true);
            panel->renameRack();
        }
}

juce::Component& DeviceChain::content() const
{
    return *content_;
}

std::vector<RackFrame> DeviceChain::frames() const
{
    std::vector<RackFrame> out;
    for (const auto& r : racks_)
        out.push_back(r.frame);
    return out;
}

void DeviceChain::saveSelectedRackAs()
{
    saveOnRename_ = true;
    renameSelectedRack();
}

void DeviceChain::addRack()
{
    if (!track_)
        return;
    int index = -1;
    if (selected_)
        if (const auto place = session_.placeOf(*selected_); place && place->rack == 0)
            index = place->index + 1;
    if (const auto rack = session_.addRack(*track_, index))
        selected_ = rack;
}

void DeviceChain::showRackMenu(std::uint64_t rack)
{
    juce::PopupMenu menu;
    menu.addItem(juce::String::fromUTF8("Rename rack\xe2\x80\xa6"), [this] { renameSelectedRack(); });
    const auto* r = rackOf(rack);
    const bool room =
        r != nullptr && static_cast<int>(r->info->chains.size()) < engine::Session::maxRackChains;
    menu.addItem("Add chain", room, false, [this, rack] { session_.addRackChain(rack); });
    menu.addItem("Edit macros", [this, rack] { openMacroEditor(rack, 0); });
    // Snapshots and Vary (racks.md §18).
    menu.addSeparator();
    const bool roomForSnapshot =
        r != nullptr && static_cast<int>(r->info->snapshots.size()) < model::RackInfo::maxSnapshots;
    menu.addItem("Store snapshot", roomForSnapshot, false, [this, rack] { session_.storeSnapshot(rack); });
    const bool vary = r != nullptr && model::canVary(*r->info);
    menu.addItem("Vary (" + actions_.shortcut("module.vary") + ")", vary, false,
                 [this, rack] { session_.varyRack(rack, engine::Session::varyAmount); });
    menu.addItem("Vary more (" + actions_.shortcut("module.varyMore") + ")", vary, false,
                 [this, rack] { session_.varyRack(rack, engine::Session::varyMoreAmount); });
    menu.addSeparator();
    menu.addItem("Ungroup rack (" + actions_.shortcut("module.ungroupRack") + ")",
                 [this, rack]
                 {
                     selected_ = rack;
                     ungroupSelectedRack();
                 });
    if (auto* s = slotOf(rack))
        menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(s->view.get()));
}

void DeviceChain::showChainMenu(std::uint64_t rack, int chain)
{
    const auto* r = rackOf(rack);
    if (r == nullptr || chain < 0 || chain >= static_cast<int>(r->info->chains.size()))
        return;
    const auto& c = r->info->chains[static_cast<size_t>(chain)];
    juce::PopupMenu menu;
    menu.addItem(juce::String::fromUTF8("Rename chain\xe2\x80\xa6"),
                 [this, rack, chain]
                 {
                     if (auto* panel = rackPanelOf(rack))
                         panel->renameChain(chain);
                 });
    menu.addItem("Solo", true, c.solo,
                 [this, rack, chain, solo = !c.solo] { session_.setRackChain(rack, chain, {.solo = solo}); });
    menu.addItem("On", true, c.on,
                 [this, rack, chain, on = !c.on] { session_.setRackChain(rack, chain, {.on = on}); });
    menu.addSeparator();
    menu.addItem("Remove chain", r->info->chains.size() > 1, false,
                 [this, rack, chain] { session_.removeRackChain(rack, chain); });
    menu.showMenuAsync(juce::PopupMenu::Options());
}

std::optional<std::pair<std::uint64_t, std::string>> DeviceChain::knobAt(juce::Point<int> p,
                                                                         std::uint64_t rack) const
{
    for (const auto& s : slots_)
    {
        if (s.rack != rack || !s.view->isVisible() || !s.view->getBounds().contains(p))
            continue;
        auto* hit = s.view->getComponentAt(s.view->getLocalPoint(content_.get(), p));
        for (; hit != nullptr && hit != s.view.get(); hit = hit->getParentComponent())
            if (auto* knob = dynamic_cast<ui::ParamKnob*>(hit))
                if (const auto* info = infoOf(s.info); info != nullptr && knob->param() < info->params.size())
                    return std::pair{s.info.id, std::string(info->params[knob->param()].id)};
    }
    return std::nullopt;
}

void DeviceChain::macroDragged(std::uint64_t rack, int macro, const juce::MouseEvent& e)
{
    const auto at = knobAt(e.getEventRelativeTo(content_.get()).getPosition(), rack);
    engine::Session::MacroPointer pointer{.rack = rack, .macro = macro, .dragging = true};
    if (at)
    {
        pointer.module = at->first;
        pointer.param = at->second;
    }
    session_.setMacroPointer(pointer);
}

void DeviceChain::macroDropped(std::uint64_t rack, int macro)
{
    const auto pointer = session_.macroPointer();
    session_.setMacroPointer({});
    if (!pointer.dragging || pointer.rack != rack || pointer.module == 0)
        return;
    // Over the knob's whole range, From where it is now (racks.md §4).
    const float from = valueOf(pointer.module, pointer.param);
    session_.mapMacro(rack, macro, pointer.module, pointer.param, from, from < 0.999f ? 1.0f : 0.0f);
}

float DeviceChain::valueOf(std::uint64_t module, const std::string& param)
{
    for (auto& s : slots_)
        if (s.info.id == module && s.handle != nullptr)
        {
            const auto& params = s.handle->context().info.params;
            for (size_t i = 0; i < params.size(); ++i)
                if (params[i].id == param)
                    return params[i].toNormalized(s.handle->context().params.getPlain(i));
        }
    if (auto handle = session_.openDevice(module))
    {
        const auto& params = handle->context().info.params;
        for (size_t i = 0; i < params.size(); ++i)
            if (params[i].id == param)
                return params[i].toNormalized(handle->context().params.getPlain(i));
    }
    return 0.0f;
}

void DeviceChain::addMacroItems(std::uint64_t module, const std::string& param, juce::PopupMenu& menu)
{
    const auto place = session_.placeOf(module);
    if (!place || place->rack == 0)
        return;
    const auto snapshot = session_.snapshot();
    const model::DeviceSlot* rack = nullptr;
    for (const auto* list : {&snapshot->tracks, &snapshot->returns})
        for (const auto& t : *list)
            if (const auto* found = model::findDevice(t.devices, place->rack))
                rack = found;
    if (rack == nullptr || rack->rack == nullptr)
        return;
    int current = -1;
    (void)model::mappingOf(*rack->rack, module, param, &current);
    const auto rackId = place->rack;
    juce::PopupMenu macros;
    for (int i = 0; i < model::RackInfo::macroCount; ++i)
        macros.addItem(juce::String::fromUTF8(rack->rack->macros[static_cast<size_t>(i)].name.c_str()), true,
                       i == current,
                       [this, rackId, i, module, param]
                       {
                           const float from = valueOf(module, param);
                           session_.mapMacro(rackId, i, module, param, from, from < 0.999f ? 1.0f : 0.0f);
                       });
    menu.addSubMenu("Map to macro", macros);
    if (current >= 0)
        menu.addItem("Unmap from " + juce::String::fromUTF8(
                                         rack->rack->macros[static_cast<size_t>(current)].name.c_str()),
                     [this, module, param] { session_.unmapMacro(module, param); });
    menu.addItem("Edit macros", [this, rackId, current] { openMacroEditor(rackId, std::max(0, current)); });
}

bool DeviceChain::editingMacros() const
{
    return macroEditor_ != nullptr && selectedRack() == std::optional(macroEditorRack_);
}

void DeviceChain::editMacros(int macro)
{
    if (const auto rack = selectedRack())
        openMacroEditor(*rack, macro);
}

void DeviceChain::openMacroEditor(std::uint64_t rack, int first)
{
    const auto* r = rackOf(rack);
    if (r == nullptr || !onShowEditor)
        return;
    // From and To in the parameter's unit, as its knob says them.
    auto editor = std::make_unique<MacroEditor>(
        [this](const model::MacroMappingInfo& m, float v) -> juce::String
        {
            const auto snapshot = session_.snapshot();
            for (const auto& t : snapshot->tracks)
                if (const auto* d = model::findDevice(t.devices, m.module))
                    if (const auto* spec = specOf(*d, m.param))
                        return juce::String(spec->toText(spec->fromNormalized(v)));
            return juce::String(juce::roundToInt(v * 100.0f)) + " %";
        });
    auto* raw = editor.get();
    const auto mapping = [this, rack](int macro, int index) -> std::optional<model::MacroMappingInfo>
    {
        const auto* rr = rackOf(rack);
        if (rr == nullptr || macro < 0 || macro >= model::RackInfo::macroCount)
            return std::nullopt;
        const auto& list = rr->info->macros[static_cast<size_t>(macro)].mappings;
        return index >= 0 && index < static_cast<int>(list.size())
                   ? std::optional(list[static_cast<size_t>(index)])
                   : std::nullopt;
    };
    raw->onMappingChange = [this, rack, mapping](int macro, int index, float from, float to, float curve)
    {
        if (const auto m = mapping(macro, index))
            session_.mapMacro(rack, macro, m->module, m->param, from, to, curve);
    };
    raw->onRemoveMapping = [this, mapping](int macro, int index)
    {
        if (const auto m = mapping(macro, index))
            session_.unmapMacro(m->module, m->param);
    };
    raw->onMacroValue = [this, rack](int macro, float value) { session_.setMacro(rack, macro, value); };
    raw->onLockMacro = [this, rack](int macro, bool locked) { session_.lockMacro(rack, macro, locked); };
    raw->onGesture = [this](bool started) { beginGesture(started, "Edit macro"); };
    raw->onAddMapping = [this, rack, raw](int macro)
    {
        // The rack's modules, each with the parameters a macro can set.
        const auto* rr = rackOf(rack);
        if (rr == nullptr)
            return;
        juce::PopupMenu menu;
        for (const auto& chain : rr->info->chains)
            for (const auto& d : chain.devices)
            {
                juce::PopupMenu params;
                if (const auto* info = infoOf(d))
                {
                    for (const auto& p : info->params)
                        if (p.hasFlag(ParamFlags::Automatable) && !p.hasFlag(ParamFlags::Hidden) &&
                            !p.hasFlag(ParamFlags::ChangesLatency))
                            params.addItem(juce::String(std::string(p.name)),
                                           [this, rack, macro, module = d.id, id = std::string(p.id)]
                                           {
                                               const float from = valueOf(module, id);
                                               session_.mapMacro(rack, macro, module, id, from,
                                                                 from < 0.999f ? 1.0f : 0.0f);
                                           });
                }
                else
                    for (const auto& p : session_.pluginParameters(d.id))
                        params.addItem(juce::String::fromUTF8(p.name.c_str()),
                                       [this, rack, macro, module = d.id, id = p.id, from = p.value]
                                       {
                                           session_.mapMacro(rack, macro, module, id, from,
                                                             from < 0.999f ? 1.0f : 0.0f);
                                       });
                menu.addSubMenu(juce::String::fromUTF8(d.name.c_str()), params, params.getNumItems() > 0);
            }
        if (menu.getNumItems() == 0)
            menu.addItem("No modules in the rack yet", false, false, [] {});
        menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(raw));
    };
    raw->setRack(juce::String::fromUTF8(slotOf(rack)->info.name.c_str()), *r->info);
    raw->selectMacro(first);
    macroEditor_ = raw;
    macroEditorRack_ = rack;
    onShowEditor(rack, std::move(editor));
}

const juce::String DeviceChain::bypassedText = "Bypassed while monitoring";
const juce::String DeviceChain::bypassedBrief = "Bypassed";
const juce::String DeviceChain::bypassedTooltip =
    "Reduced latency when monitoring is on (Options): this module delays its track 3 ms or more, so it's "
    "bypassed while the track is monitored. Its power stays as you set it.";

void DeviceChain::updateNotices()
{
    updatePadLights();
    for (const auto& s : slots_)
    {
        const auto text = session_.bypassedWhileMonitoring(s.info.id) ? bypassedText : juce::String();
        if (auto* panel = dynamic_cast<ui::DevicePanel*>(s.view.get()))
            panel->setHostNotice(text, bypassedBrief, bypassedTooltip);
        else if (auto* card = dynamic_cast<PluginCard*>(s.view.get()))
            card->setNotice(text, bypassedBrief, bypassedTooltip);
    }
}

void DeviceChain::updateMacroEditor()
{
    if (macroEditor_ == nullptr)
        return;
    if (const auto* r = rackOf(macroEditorRack_))
        macroEditor_->setRack(juce::String::fromUTF8(slotOf(macroEditorRack_)->info.name.c_str()), *r->info);
}

void DeviceChain::beginGesture(bool started, const std::string& name)
{
    if (started)
        gesture_.emplace(session_, name);
    else
        gesture_.reset();
}

// ---- modulators (docs/design/modulators.md §6, §7, §15) ------------------------------------------

void DeviceChain::wireModulatorPanel(ModulatorPanel& panel, std::uint64_t modulator)
{
    panel.onHandleDrag = [this, modulator](const juce::MouseEvent& e) { modulatorDragged(modulator, e); };
    panel.onHandleRelease = [this, modulator](const juce::MouseEvent&, bool cancelled)
    { modulatorReleased(modulator, cancelled); };
    panel.onHandleCancel = [this, modulator] { modulatorReleased(modulator, true); };
    panel.onRoutes = [this, modulator] { showRoutes(modulator); };
}

ModulatorPanel* DeviceChain::modulatorPanel(std::uint64_t modulator) const
{
    for (const auto& s : slots_)
        if (s.info.id == modulator)
            return dynamic_cast<ModulatorPanel*>(s.view.get());
    return nullptr;
}

const model::ModulatorInfo* DeviceChain::selectedModulator() const
{
    if (selected_)
        for (const auto& s : slots_)
            if (s.info.id == *selected_)
                return s.info.modulator.get();
    return nullptr;
}

void DeviceChain::addModulator(model::ModulatorKind kind)
{
    if (!track_)
        return;
    // After the selected module along the track's chain, else at its end (§6).
    int index = -1;
    if (selected_)
        if (const auto place = session_.placeOf(*selected_); place && place->rack == 0)
            index = place->index + 1;
    if (const auto id = session_.addModulator(*track_, kind, index))
    {
        selected_ = id;
        picked_.clear();
    }
}

std::optional<engine::Session::ParamTarget> DeviceChain::targetOf(const juce::Component& control) const
{
    const int index = slotIndexOf(&control);
    if (index < 0 || !track_)
        return std::nullopt;
    const auto param = ui::paramOf(control);
    const auto& slot = slots_[static_cast<size_t>(index)];
    auto* context = slot.handle != nullptr      ? &slot.handle->context()
                    : slot.modulator != nullptr ? &slot.modulator->context()
                                                : nullptr;
    if (!param || context == nullptr || *param >= context->info.params.size())
        return std::nullopt;
    return engine::Session::ParamTarget{*track_, "m" + std::to_string(slot.info.id) + "/" +
                                                     std::string(context->info.params[*param].id)};
}

std::optional<engine::Session::ParamTarget> DeviceChain::targetAt(juce::Point<int> p) const
{
    for (const auto& s : slots_)
    {
        if (!s.view->isVisible() || !s.view->getBounds().contains(p))
            continue;
        for (auto* hit = s.view->getComponentAt(s.view->getLocalPoint(content_.get(), p));
             hit != nullptr && hit != s.view.get(); hit = hit->getParentComponent())
            if (auto target = targetOf(*hit))
                return target;
    }
    return std::nullopt;
}

namespace
{
/// Each component under `root` that shows within it (it and its parents up to `root` visible).
template <typename Visit> void eachShown(juce::Component& root, Visit&& visit)
{
    for (auto* c : root.getChildren())
        if (c->isVisible())
        {
            visit(*c);
            eachShown(*c, visit);
        }
}
} // namespace

juce::Component* DeviceChain::controlFor(const engine::Session::ParamTarget& target) const
{
    if (!track_ || target.channel != *track_)
        return nullptr;
    for (const auto& s : slots_)
    {
        const auto prefix = "m" + std::to_string(s.info.id) + "/";
        if (!s.view->isVisible() || target.key.rfind(prefix, 0) != 0)
            continue;
        juce::Component* found = nullptr;
        eachShown(*s.view,
                  [&](juce::Component& c)
                  {
                      if (found == nullptr && targetOf(c) == std::optional(target))
                          found = &c;
                  });
        return found != nullptr ? found : s.view.get();
    }
    return nullptr;
}

std::vector<DeviceChain::Cable> DeviceChain::cables() const
{
    std::vector<Cable> out;
    auto* panel =
        hoveredModulator_ != 0 && draggedModulator_ == 0 ? modulatorPanel(hoveredModulator_) : nullptr;
    if (panel == nullptr || !panel->isVisible())
        return out;
    const model::ModulatorInfo* info = nullptr;
    for (const auto& s : slots_)
        if (s.info.id == hoveredModulator_)
            info = s.info.modulator.get();
    if (info == nullptr)
        return out;
    auto& handle = panel->handle();
    const auto chip = content_->getLocalArea(&handle, handle.getLocalBounds()).toFloat();
    for (const auto& r : info->routes)
        if (auto* control = controlFor({r.channel, r.key}))
            out.push_back({{chip.getCentreX(), chip.getBottom()},
                           content_->getLocalArea(control, control->getLocalBounds()).toFloat().getCentre()});
    return out;
}

void DeviceChain::setHoveredModulator(std::uint64_t modulator)
{
    if (modulator == hoveredModulator_)
        return;
    hoveredModulator_ = modulator;
    if (draggedModulator_ == 0)
        session_.setModulatorPointer({.modulator = modulator}); // the other modulators' rings dim
    content_->repaint();
}

void DeviceChain::lightModulatorsOf(const juce::Component* control)
{
    const auto target = control != nullptr ? targetOf(*control) : std::nullopt;
    for (const auto& s : slots_)
        if (auto* panel = dynamic_cast<ModulatorPanel*>(s.view.get()); panel != nullptr && s.info.modulator)
        {
            const auto& routes = s.info.modulator->routes;
            panel->setLit(target &&
                          std::any_of(routes.begin(), routes.end(), [&](const auto& r)
                                      { return r.channel == target->channel && r.key == target->key; }));
        }
}

void DeviceChain::modulatorDragged(std::uint64_t modulator, const juce::MouseEvent& e)
{
    auto* panel = modulatorPanel(modulator);
    if (panel == nullptr)
        return;
    if (draggedModulator_ != modulator)
    {
        // The drag starts: its chip follows the pointer, and the drag repeats while the pointer
        // rests at an edge, so the chain keeps scrolling (§6).
        draggedModulator_ = modulator;
        auto& handle = panel->handle();
        dragChip_ = handle.createComponentSnapshot(handle.getLocalBounds(), true, 2.0f);
        panel->setDragging(true);
        juce::Desktop::getInstance().beginDragAutoRepeat(30);
    }
    const auto inViewport = e.getEventRelativeTo(&viewport_).getPosition();
    const auto& t = ui::Theme::current();
    viewport_.autoScroll(inViewport.x, inViewport.y, t.shell.dragScrollEdge, t.shell.dragScrollSpeed);
    dragAt_ = e.getEventRelativeTo(content_.get()).getPosition();
    const auto over = targetAt(dragAt_);
    session_.setModulatorPointer({.modulator = modulator,
                                  .dragging = true,
                                  .over = over && session_.canRoute(modulator, *over) ? over : std::nullopt});
    content_->repaint();
}

void DeviceChain::modulatorReleased(std::uint64_t modulator, bool cancelled)
{
    const auto pointer = session_.modulatorPointer();
    session_.setModulatorPointer({});
    if (auto* panel = modulatorPanel(modulator))
        panel->setDragging(false);
    const bool wasDragged = draggedModulator_ == modulator;
    draggedModulator_ = 0;
    dragChip_ = {};
    hoveredModulator_ = 0;
    content_->repaint();
    // Let go on a control: a route at 50 % (§7.1); Escape, or nowhere it can go, changes nothing.
    if (!cancelled && wasDragged && pointer.dragging && pointer.modulator == modulator && pointer.over)
        session_.routeModulator(modulator, *pointer.over, 0.5f);
}

void DeviceChain::showModulatorMenu(std::uint64_t modulator)
{
    const auto* slot = slotOf(modulator);
    if (slot == nullptr || slot->info.modulator == nullptr)
        return;
    const auto& info = *slot->info.modulator;
    juce::PopupMenu menu;
    menu.addItem(juce::String::fromUTF8("Routes\xe2\x80\xa6"), [this, modulator] { showRoutes(modulator); });
    menu.addItem("Remove routes", !info.routes.empty(), false,
                 [this, modulator] { session_.removeRoutes(modulator); });
    if (model::hasKeep(info.kind))
    {
        menu.addSeparator();
        menu.addItem(info.kept ? "Let it roll" : "Keep as played",
                     [this, modulator, keep = !info.kept] { session_.keepRoll(modulator, keep); });
    }
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(slot->view.get()));
}

std::vector<RouteRows::Route> DeviceChain::routeRowsOf(std::uint64_t modulator) const
{
    std::vector<RouteRows::Route> rows;
    const auto* slot = slotOf(modulator);
    if (slot == nullptr || slot->info.modulator == nullptr)
        return rows;
    for (const auto& r : slot->info.modulator->routes)
    {
        // The parameter as its control names it ("Mix", not "Dry/Wet"), where the chain shows it.
        auto param = r.param;
        for (const auto& s : slots_)
            if (const auto prefix = "m" + std::to_string(s.info.id) + "/"; r.key.rfind(prefix, 0) == 0)
                if (auto* context = s.handle != nullptr      ? &s.handle->context()
                                    : s.modulator != nullptr ? &s.modulator->context()
                                                             : nullptr)
                    for (const auto& spec : context->info.params)
                        if (spec.id == r.key.substr(prefix.size()))
                            param = std::string(spec.shortName.empty() ? spec.name : spec.shortName);
        rows.push_back({juce::String::fromUTF8((r.module + " \xc2\xb7 " + param).c_str()), r.amount});
    }
    return rows;
}

DeviceChain::RouteCallbacks DeviceChain::routeCallbacks(std::uint64_t modulator,
                                                        std::function<void()> afterRemove)
{
    const auto* slot = slotOf(modulator);
    const auto routes = slot != nullptr && slot->info.modulator != nullptr
                            ? slot->info.modulator->routes
                            : std::vector<model::ModulatorRouteInfo>{};
    const auto targetOfRow = [routes](int i)
    {
        const auto& r = routes[static_cast<size_t>(i)];
        return engine::Session::ParamTarget{r.channel, r.key};
    };
    RouteCallbacks c;
    c.amount = [this, modulator, targetOfRow](int i, float amount)
    { session_.setRouteAmount(modulator, targetOfRow(i), amount); };
    c.gesture = [this](bool started) { beginGesture(started, "Route amount"); };
    // Show and Remove act once the click is over: the list closes or opens again.
    c.show = [this, targetOfRow](int i)
    {
        juce::MessageManager::callAsync(
            [safe = juce::Component::SafePointer<DeviceChain>(this), target = targetOfRow(i)]
            {
                if (safe != nullptr)
                {
                    safe->closeRoutes();
                    safe->showTarget(target);
                }
            });
    };
    c.remove = [this, modulator, targetOfRow, afterRemove](int i)
    {
        session_.removeRoute(modulator, targetOfRow(i));
        juce::MessageManager::callAsync(
            [safe = juce::Component::SafePointer<DeviceChain>(this), afterRemove]
            {
                if (safe == nullptr)
                    return;
                safe->session_.updateSnapshotNow();
                if (const auto* channel = safe->session_.snapshot()->channel(safe->track_.value_or(0)))
                    safe->setTrack(*channel);
                if (afterRemove)
                    afterRemove();
            });
    };
    return c;
}

void DeviceChain::showRoutes(std::uint64_t modulator)
{
    closeRoutes();
    auto* slot = slotOf(modulator);
    auto* panel = modulatorPanel(modulator);
    auto* top = getTopLevelComponent();
    if (slot == nullptr || slot->info.modulator == nullptr || panel == nullptr || top == nullptr)
        return;
    routeList_ =
        std::make_unique<RouteList>(juce::String::fromUTF8(slot->info.name.c_str()), routeRowsOf(modulator));
    routeListModulator_ = modulator;
    // Removing one opens it again, its rows the routes left.
    auto c = routeCallbacks(modulator,
                            [safe = juce::Component::SafePointer<DeviceChain>(this), modulator]
                            {
                                if (safe != nullptr)
                                    safe->showRoutes(modulator);
                            });
    routeList_->onAmount = std::move(c.amount);
    routeList_->onAmountGesture = std::move(c.gesture);
    routeList_->onShow = std::move(c.show);
    routeList_->onRemove = std::move(c.remove);
    routeList_->onClose = [this] { closeRoutes(); };

    // Above the modulator's panel, the handle's side lined up (the chain is the window's foot, and
    // the panel stays in view: its count follows a Remove), else under it; inside the window.
    auto& handle = panel->handle();
    routeListAnchor_ = top->getLocalArea(&handle, handle.getLocalBounds());
    const auto panelArea = top->getLocalArea(panel, panel->getLocalBounds());
    const auto& s = ui::Theme::current().shell;
    const auto size = routeList_->preferredBounds();
    const int margin = juce::roundToInt(s.shadowRadius);
    const auto area = top->getLocalBounds().reduced(s.itemGap);
    int y = panelArea.getY() - s.itemGap - (size.getHeight() - margin);
    if (y < area.getY())
        y = panelArea.getBottom() + s.itemGap - margin;
    const int x = std::clamp(routeListAnchor_.getX() - margin, area.getX() - margin,
                             std::max(area.getX() - margin, area.getRight() + margin - size.getWidth()));
    routeList_->setBounds(size.withPosition(x, y));
    top->addAndMakeVisible(*routeList_);
    routeList_->toFront(true);
    routeList_->grabKeyboardFocus();
    routeListOutside_ = std::make_unique<ui::OutsideClick>(*routeList_, [this] { closeRoutes(); });
    routeListOutside_->attach();
}

void DeviceChain::closeRoutes()
{
    if (routeListOutside_ != nullptr)
        routeListOutside_->detach();
    routeListOutside_.reset();
    routeList_.reset();
    routeListModulator_ = 0;
}

void DeviceChain::showTarget(const engine::Session::ParamTarget& target)
{
    // "m<module>/<param>": the module selected and scrolled to, its control's page shown.
    if (target.key.rfind('m', 0) != 0)
        return;
    const auto slash = target.key.find('/');
    if (slash == std::string::npos)
        return;
    const auto module = std::stoull(target.key.substr(1, slash - 1));
    const auto param = target.key.substr(slash + 1);
    selectDevice(module);
    if (auto* s = slotOf(module))
        if (auto* panel = dynamic_cast<ui::DevicePanel*>(s->view.get()))
        {
            panel->setFolded(false, true);
            const auto* context = s->handle != nullptr      ? &s->handle->context()
                                  : s->modulator != nullptr ? &s->modulator->context()
                                                            : nullptr;
            if (context != nullptr)
                for (size_t i = 0; i < context->info.params.size(); ++i)
                    if (context->info.params[i].id == param)
                        panel->showParameter(i);
        }
}

void DeviceChain::addModulatorItems(const engine::Session::ParamTarget& target, juce::PopupMenu& menu,
                                    juce::PopupMenu& with)
{
    if (!track_ || target.channel != *track_)
        return;
    // The routes that move it, each with Remove (as a module's own routes are listed).
    const auto snapshot = session_.snapshot();
    const auto* channel = snapshot->channel(target.channel);
    if (channel == nullptr)
        return;
    std::vector<const model::DeviceSlot*> modulators;
    for (const auto& d : channel->devices)
        if (d.modulator != nullptr)
            modulators.push_back(&d);
    for (const auto* m : modulators)
        for (const auto& r : m->modulator->routes)
            if (r.key == target.key)
            {
                juce::PopupMenu route;
                route.addItem("Remove", [this, id = m->id, target] { session_.removeRoute(id, target); });
                menu.addSubMenu(juce::String::fromUTF8(m->name.c_str()) + juce::String::fromUTF8(" → ") +
                                    juce::String(juce::roundToInt(r.amount * 100.0f)) + " %",
                                route);
            }
    // Modulate with: the track's modulators, then New (§7.1): added just before the control's
    // module, in the track's chain, and routed, in one step.
    for (const auto* m : modulators)
    {
        const bool routed = std::any_of(m->modulator->routes.begin(), m->modulator->routes.end(),
                                        [&](const auto& r) { return r.key == target.key; });
        with.addItem(juce::String::fromUTF8(m->name.c_str()), session_.canRoute(m->id, target), routed,
                     [this, id = m->id, target] { session_.routeModulator(id, target, 0.5f); });
    }
    if (!modulators.empty())
        with.addSeparator();
    juce::PopupMenu fresh;
    for (const auto kind : model::allModulators)
        fresh.addItem(juce::String(std::string(model::modulatorInfo(kind).name)),
                      [this, kind, target] { modulateWith(target, kind); });
    with.addSubMenu("New", fresh);
}

void DeviceChain::modulateWith(const engine::Session::ParamTarget& target, model::ModulatorKind kind,
                               const std::function<void(std::uint64_t)>& setUp)
{
    // The control's module: in the track's chain, or the rack holding it.
    int index = -1;
    if (const auto slash = target.key.find('/'); target.key.rfind('m', 0) == 0 && slash != std::string::npos)
        if (const auto place = session_.placeOf(std::stoull(target.key.substr(1, slash - 1))))
            index = place->rack == 0 ? place->index : session_.placeOf(place->rack).value_or(*place).index;
    const engine::Session::EditGroup group(session_,
                                           "Modulate with " + std::string(model::modulatorInfo(kind).name));
    if (const auto id = session_.addModulator(target.channel, kind, index))
    {
        if (setUp)
            setUp(id);
        session_.routeModulator(id, target, 0.5f);
        selected_ = id;
        picked_.clear();
    }
}
} // namespace fd::app

// ---- the Drum Kit (docs/design/drum-kit.md §5, §8, §12) --------------------------------------

namespace fd::app
{
void DeviceChain::wireKitPanel(DrumKitPanel& panel, std::uint64_t kit)
{
    // A pad pressed plays it, as the computer keyboard does, on the kit's track (the one shown).
    panel.onPlayPad = [this](int note) { session_.computerKeyboardNote(note, 100.0f / 127.0f); };
    panel.onStopPad = [this](int note) { session_.computerKeyboardNote(note, 0.0f); };
    panel.onSelectPad = [this, kit](int note)
    {
        // The kit selected with its pad, so the Module menu's pad items act on it.
        selected_ = kit;
        picked_.clear();
        if (onActivated)
            onActivated();
        content_->repaint();
        session_.selectPad(kit, note);
    };
    panel.onShowPads = [this, kit](int from) { session_.showPads(kit, from); };
    panel.onPadMenu = [this, kit](int note) { showPadMenu(kit, note); };
    panel.onMovePad = [this, kit](int from, int to, bool copy) { session_.movePad(kit, from, to, copy); };
    panel.onRenamePad = [this, kit](int note, const juce::String& name)
    { session_.setPad(kit, note, {.name = name.toStdString()}); };
    panel.onPadOn = [this, kit](int note, bool on) { session_.setPad(kit, note, {.on = on}); };
    panel.onPadSolo = [this, kit](int note, bool solo) { session_.setPad(kit, note, {.solo = solo}); };
    panel.onPadVolume = [this, kit](int note, float db) { session_.setPad(kit, note, {.volumeDb = db}); };
    panel.onPadPan = [this, kit](int note, float pan) { session_.setPad(kit, note, {.pan = pan}); };
    panel.onPadPlays = [this, kit](int note, int plays) { session_.setPad(kit, note, {.plays = plays}); };
    panel.onPadChoke = [this, kit](int note, int group) { session_.setPad(kit, note, {.choke = group}); };
    panel.onPadGesture = [this](bool started) { beginGesture(started, "Pad mixer"); };
    wireMacros(panel, kit);
}

DrumKitPanel* DeviceChain::kitPanelOf(std::uint64_t id)
{
    auto* s = slotOf(id);
    return s != nullptr ? dynamic_cast<DrumKitPanel*>(s->view.get()) : nullptr;
}

std::optional<std::pair<std::uint64_t, int>> DeviceChain::padAt(juce::Point<int> p) const
{
    for (const auto& r : racks_)
        if (r.kit && !r.folded)
            if (const auto* s = slotOf(r.id))
                if (auto* panel = dynamic_cast<DrumKitPanel*>(s->view.get());
                    panel != nullptr && panel->isVisible())
                {
                    auto& pads = panel->pads();
                    const auto at = pads.getLocalPoint(content_.get(), p);
                    if (pads.getLocalBounds().contains(at))
                        if (const int note = pads.padAt(at); note >= 0)
                            return std::pair{r.id, note};
                }
    return std::nullopt;
}

void DeviceChain::showPadMenu(std::uint64_t kit, int note)
{
    const auto* r = rackOf(kit);
    if (r == nullptr || r->info == nullptr)
        return;
    const auto* pad = model::kit::padOn(*r->info, note);
    const bool sound = pad != nullptr;
    juce::PopupMenu menu;
    menu.addItem("Select pad", [this, kit, note] { session_.selectPad(kit, note); });
    menu.addItem(juce::String::fromUTF8("Rename pad\xe2\x80\xa6"), sound, false,
                 [this, kit, note]
                 {
                     if (auto* panel = kitPanelOf(kit))
                         panel->renamePad(note);
                 });
    menu.addItem(juce::String::fromUTF8("Load sample\xe2\x80\xa6"),
                 [this, kit, note] { loadSampleOnPad(kit, note); });
    menu.addSeparator();
    menu.addItem("Copy pad", sound, false, [this, kit, note] { session_.copyPad(kit, note); });
    menu.addItem("Paste pad", session_.canPastePad(), false,
                 [this, kit, note] { session_.pastePad(kit, note); });
    menu.addItem("Clear pad", sound, false, [this, kit, note] { session_.clearPad(kit, note); });
    menu.addSeparator();
    menu.addItem("On", sound, sound && pad->on,
                 [this, kit, note, on = sound && pad->on] { session_.setPad(kit, note, {.on = !on}); });
    menu.addItem("Solo", sound, sound && pad->solo, [this, kit, note, solo = sound && pad->solo]
                 { session_.setPad(kit, note, {.solo = !solo}); });
    juce::PopupMenu choke;
    for (int group = 0; group <= model::kit::chokeGroups; ++group)
        choke.addItem(juce::String::fromUTF8(model::kit::chokeName(group).c_str()), sound,
                      sound && pad->choke == group,
                      [this, kit, note, group] { session_.setPad(kit, note, {.choke = group}); });
    menu.addSubMenu("Choke group", choke, sound);
    menu.showMenuAsync(juce::PopupMenu::Options());
}

namespace
{
std::vector<juce::File> samplesIn(const juce::StringArray& files)
{
    std::vector<juce::File> out;
    for (const auto& f : files)
        if (const auto kind = model::fileKind(f.toStdString());
            kind == model::BrowserKind::Sample || kind == model::BrowserKind::Multisample)
            out.emplace_back(f);
    return out;
}
} // namespace

bool DeviceChain::isInterestedInFileDrag(const juce::StringArray& files)
{
    return track_.has_value() && !samplesIn(files).empty();
}

void DeviceChain::fileDragMove(const juce::StringArray&, int x, int y)
{
    const auto inContent = content_->getLocalPoint(this, juce::Point<int>(x, y));
    const auto pad = padAt(inContent);
    if (padDrop_ != pad)
    {
        if (padDrop_)
            if (auto* panel = kitPanelOf(padDrop_->first))
                panel->pads().setDropTarget(-1);
        padDrop_ = pad;
        if (padDrop_)
            if (auto* panel = kitPanelOf(padDrop_->first))
                panel->pads().setDropTarget(padDrop_->second);
    }
    browserDrag_ = !pad.has_value();
    if (pad)
        drop_.reset();
    else
        drop_ = dropAt(inContent, std::nullopt);
    content_->repaint();
}

void DeviceChain::fileDragExit(const juce::StringArray&)
{
    if (padDrop_)
        if (auto* panel = kitPanelOf(padDrop_->first))
            panel->pads().setDropTarget(-1);
    padDrop_.reset();
    browserDrag_ = false;
    drop_.reset();
    content_->repaint();
}

void DeviceChain::filesDropped(const juce::StringArray& files, int x, int y)
{
    const auto at = content_->getLocalPoint(this, juce::Point<int>(x, y));
    fileDragExit(files);
    dropFiles(samplesIn(files), at);
}

void DeviceChain::dropFiles(const std::vector<juce::File>& files, juce::Point<int> at)
{
    if (!track_ || files.empty())
        return;
    if (const auto pad = padAt(at))
    {
        session_.selectPad(pad->first, pad->second);
        session_.loadPads(pad->first, pad->second, files);
        return;
    }
    if (files.size() > 1)
    {
        const auto r = browser::kitWith(session_, files, *track_);
        if (r.module != 0)
            selected_ = r.module;
        return;
    }
    const auto drop = dropAt(at, std::nullopt);
    if (onBrowserDrop)
        onBrowserDrop("file:" + files.front().getFullPathName().toStdString(), *track_, drop.index,
                      std::nullopt);
}

void DeviceChain::loadSampleOnPad(std::uint64_t kit, int note)
{
    chooser_ = std::make_unique<juce::FileChooser>("Load a sample onto the pad", juce::File(),
                                                   "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.sfz");
    chooser_->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this, kit, note](const juce::FileChooser& fc)
                          {
                              if (const auto file = fc.getResult(); file.existsAsFile())
                              {
                                  session_.selectPad(kit, note);
                                  session_.loadPads(kit, note, {file});
                              }
                          });
}

void DeviceChain::updatePadLights()
{
    // A pad lights a moment each time it's hit: a click, a key, the song (drum-kit.md §5).
    const auto now = juce::Time::getMillisecondCounter();
    constexpr juce::uint32 lit = 150; // ms
    for (const auto& r : racks_)
    {
        if (!r.kit)
            continue;
        auto* panel = kitPanelOf(r.id);
        if (panel == nullptr)
            continue;
        auto& lights = padLights_[r.id];
        const auto hits = session_.padHits(r.id);
        std::bitset<128> on;
        for (size_t n = 0; n < hits.size(); ++n)
        {
            if (lights.seen && hits[n] != lights.hits[n])
                lights.until[n] = now + lit;
            on.set(n, lights.until[n] > now);
        }
        lights.hits = hits;
        lights.seen = true;
        panel->setPlaying(on);
    }
}

std::optional<std::uint64_t> DeviceChain::selectedKit() const
{
    if (const auto rack = selectedRack())
        if (const auto* r = rackOfConst(*rack); r != nullptr && r->kit)
            return rack;
    return std::nullopt;
}

std::optional<std::pair<std::uint64_t, int>> DeviceChain::selectedPad() const
{
    if (const auto kit = selectedKit())
        if (const auto* r = rackOfConst(*kit); r != nullptr && r->info != nullptr &&
                                               model::kit::padOn(*r->info, r->info->selectedPad) != nullptr)
            return std::pair{*kit, r->info->selectedPad};
    return std::nullopt;
}

void DeviceChain::renameSelectedPad()
{
    if (const auto pad = selectedPad())
        if (auto* panel = kitPanelOf(pad->first))
        {
            panel->setFolded(false, true);
            panel->renamePad(pad->second);
        }
}

void DeviceChain::copySelectedPad()
{
    if (const auto pad = selectedPad())
        session_.copyPad(pad->first, pad->second);
}

void DeviceChain::pasteOnSelectedPad()
{
    if (const auto kit = selectedKit())
        if (const auto* r = rackOfConst(*kit); r != nullptr && r->info != nullptr)
            session_.pastePad(*kit, r->info->selectedPad);
}

void DeviceChain::clearSelectedPad()
{
    if (const auto pad = selectedPad())
        session_.clearPad(pad->first, pad->second);
}

void DeviceChain::loadSampleOnSelectedPad()
{
    if (const auto kit = selectedKit())
        if (const auto* r = rackOfConst(*kit); r != nullptr && r->info != nullptr)
            loadSampleOnPad(*kit, r->info->selectedPad);
}
} // namespace fd::app
