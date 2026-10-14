#include "ui/DeviceChain.h"

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
        setEnabledState(slot.enabled);
    }

    void setEnabledState(bool on) { power_.setToggleState(on, juce::dontSendNotification); }

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
    }

private:
    engine::Session& session_;
    std::uint64_t id_;
    std::string name_;
    ui::Toggle power_;
    ui::Toggle open_{"Open editor"};
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
            if (!r.emptyHint.isEmpty())
                ui::paintEmptyState(g, r.emptyHint, "Drop modules here");
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
            });
    };
    content_->addAndMakeVisible(add_);
    session_.addListener(this);
    session_.macroMenu = [this](std::uint64_t module, const std::string& param, juce::PopupMenu& menu)
    { addMacroItems(module, param, menu); };
}

DeviceChain::~DeviceChain()
{
    session_.macroMenu = nullptr;
    session_.setMacroPointer({});
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
        if (d.rack != nullptr && !d.rack->chains.empty())
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
        if (auto it = old.find(d.id);
            it != old.end() && (it->second.info.rack != nullptr) == (d.rack != nullptr))
        {
            slot = std::move(it->second);
            old.erase(it);
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
            Rack r{.id = d.id, .info = d.rack};
            if (auto it = caps.find(d.id); it != caps.end())
                r.endCap = std::move(it->second);
            else
            {
                r.endCap = std::make_unique<ui::IconButton>(ui::Icon::Plus);
                r.endCap->setWantsKeyboardFocus(false);
                content_->addAndMakeVisible(*r.endCap);
            }
            r.endCap->setTooltip("Add a module to the rack's chain shown, or a chain beside it");
            const auto id = d.id;
            r.endCap->onClick = [this, id]
            {
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
                    });
            };
            racks_.push_back(std::move(r));
        }
    caps.clear();

    if (selected_ && slotOf(*selected_) == nullptr)
        selected_.reset();
    for (auto it = picked_.begin(); it != picked_.end();)
        it = slotOf(*it) == nullptr ? picked_.erase(it) : std::next(it);
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
    panel.onRenameMacro = [this, rack](int macro, const juce::String& name)
    { session_.setMacro(rack, macro, std::nullopt, name.toStdString()); };
    panel.onRenameRack = [this, rack](const juce::String& name)
    {
        session_.renameRack(rack, name.toStdString());
        if (std::exchange(saveOnRename_, false) && onSaveRack)
            onSaveRack(rack);
    };
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
            // A chain that's off is faded with its modules (racks.md §5a).
            const auto& chains = rack->info->chains;
            const bool on =
                inner.chain < static_cast<int>(chains.size()) && chains[static_cast<size_t>(inner.chain)].on;
            inner.view->setAlpha(on || chains.size() < 2 ? 1.0f : t.roles.fadedAlpha);
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
        const bool several = info.chains.size() > 1 && !rack->folded;
        const auto shown =
            static_cast<size_t>(std::clamp(info.shownChain, 0, static_cast<int>(info.chains.size()) - 1));
        rack->frame.text = several ? chainLabel(info, static_cast<int>(shown)) : juce::String();
        rack->frame.chainOff = several && !info.chains[shown].on;
        rack->frame.soloed = several && info.chains[shown].solo;
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
                              std::function<void(juce::PopupMenu&)> extra)
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
    section("Audio effects", [](const auto& d) { return d.native && !d.instrument; });
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
            return {r.id, r.info->shownChain, index};
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
    // A rack's header: its menu on a right-click.
    if (e.mods.isPopupMenu())
    {
        if (slot.info.rack != nullptr)
        {
            selected_ = slot.info.id;
            showRackMenu(slot.info.id);
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
        session_.moveDevice(slots_[static_cast<size_t>(dragSlot_)].info.id,
                            engine::Session::ChainPlace{
                                .channel = *track_, .rack = d.rack, .chain = d.chain, .index = d.index});
    }
    dragging_ = false;
    dragSlot_ = -1;
    drop_.reset();
    content_->repaint();
}

void DeviceChain::mouseMove(const juce::MouseEvent& e)
{
    // On a knob a rack's macro sets: that macro's name lights (racks.md §5a).
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
        if (auto* panel = rackPanelOf(r.id))
            panel->setLitMacro(r.id == litRack ? lit : -1);
}

void DeviceChain::mouseExit(const juce::MouseEvent&)
{
    for (const auto& r : racks_)
        if (auto* panel = rackPanelOf(r.id))
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

void DeviceChain::itemDragMove(const SourceDetails& details)
{
    const auto item = browser::itemOf(details.description);
    const auto inContent = content_->getLocalPoint(this, details.localPosition);
    browserDrag_ = true;
    browserOn_.reset();
    drop_ = dropAt(inContent, std::nullopt);
    if (item)
        for (const auto& s : slots_)
            if (s.view->isVisible() && s.view->getBounds().contains(inContent) &&
                browser::landsOn(*item, s.info))
            {
                browserOn_ = s.info.id;
                drop_.reset();
            }
    content_->repaint();
}

void DeviceChain::itemDragExit(const SourceDetails&)
{
    browserDrag_ = false;
    browserOn_.reset();
    drop_.reset();
    content_->repaint();
}

void DeviceChain::itemDropped(const SourceDetails& details)
{
    itemDragMove(details);
    const auto on = browserOn_;
    const auto drop = drop_;
    itemDragExit(details);
    const auto item = browser::itemOf(details.description);
    if (!item || !track_)
        return;
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
            onBrowserDropInRack(
                *item, {.channel = *track_, .rack = drop->rack, .chain = drop->chain, .index = drop->index});
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
} // namespace fd::app
