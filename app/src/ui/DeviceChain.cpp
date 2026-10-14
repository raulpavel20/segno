#include "ui/DeviceChain.h"

#include "ui/BrowserDrops.h"

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
} // namespace

/// The scrolled strip of slots; draws the drop marker while reordering.
class DeviceChain::Content final : public juce::Component
{
public:
    explicit Content(DeviceChain& owner) : owner_(owner) {}
    void paintOverChildren(juce::Graphics& g) override
    {
        const auto& t = ui::Theme::current();
        if (owner_.selected_)
            for (const auto& s : owner_.slots_)
                if (s.info.id == *owner_.selected_)
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
        if ((owner_.dragging_ || owner_.browserDrag_) && owner_.insertAt_ >= 0)
        {
            int x = gap / 2;
            for (int i = 0; i < owner_.insertAt_ && i < static_cast<int>(owner_.slots_.size()); ++i)
                x = owner_.slots_[static_cast<size_t>(i)].view->getRight() + gap / 2;
            g.setColour(t.active);
            g.fillRect(x - t.shell.dropLine / 2, 0, t.shell.dropLine, getHeight());
        }
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
    add_.onClick = [this] { showAddMenu(); };
    content_->addAndMakeVisible(add_);
    session_.addListener(this);
}

DeviceChain::~DeviceChain()
{
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
    // The panel, the gap below it and the scrollbar's room.
    return ui::Theme::current().devicePanelHeight + 24;
}

void DeviceChain::setTrack(std::optional<model::TrackInfo> track)
{
    const auto id = track ? std::optional(track->id) : std::nullopt;
    if (id != track_)
    {
        selected_.reset();
        track_ = id;
    }
    rebuild(track ? track->devices : std::vector<model::DeviceSlot>{});
}

void DeviceChain::rebuild(const std::vector<model::DeviceSlot>& devices)
{
    // Keep the views of devices that are still there (panels keep their state and analysers).
    std::map<std::uint64_t, Slot> old;
    for (auto& s : slots_)
        old.emplace(s.info.id, std::move(s));
    slots_.clear();
    for (const auto& d : devices)
    {
        Slot slot;
        if (auto it = old.find(d.id); it != old.end())
        {
            slot = std::move(it->second);
            old.erase(it);
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
                {
                    auto panel = factory->create(slot.handle->context());
                    const auto id = d.id;
                    panel->onEnabledChange = [this, id](bool on) { session_.setDeviceEnabled(id, on); };
                    // View state lives on the module in the project (ADR 0022): panels are
                    // rebuilt on every track switch, so the plugin tree is the source of truth.
                    panel->setFoldable(true);
                    panel->setViewState(session_.deviceViewState(id));
                    panel->onViewChanged = [this, id, p = panel.get()] {
                        session_.setDeviceViewState(id, p->viewState());
                        layoutSlots();
                    };
                    slot.view = std::move(panel);
                }
        }
        if (slot.view == nullptr)
            slot.view = std::make_unique<PluginCard>(session_, d);
        slot.info = d;
        if (auto* panel = dynamic_cast<ui::DevicePanel*>(slot.view.get()))
            panel->setEnabledState(d.enabled);
        else if (auto* card = dynamic_cast<PluginCard*>(slot.view.get()))
            card->setEnabledState(d.enabled);
        if (slot.view->getParentComponent() != content_.get())
        {
            content_->addAndMakeVisible(*slot.view);
            slot.view->addMouseListener(this, true); // header drags and selection
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
    if (selected_ && std::none_of(slots_.begin(), slots_.end(), [&](const Slot& s) { return s.info.id == *selected_; }))
        selected_.reset();
    add_.setVisible(track_.has_value());
    layoutSlots();
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
    const int h = t.devicePanelHeight;
    int x = gap;
    for (auto& s : slots_)
    {
        const auto* panel = dynamic_cast<ui::DevicePanel*>(s.view.get());
        const int w = panel != nullptr ? panel->preferredWidth() : cardWidth;
        s.view->setBounds(x, 0, w, h);
        x += w + gap;
    }
    add_.setBounds(x, 0, 32, 32);
    content_->setSize(x + 32 + gap, h + 8);
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

void DeviceChain::showAddMenu()
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

    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&add_),
                       [this, devices, track = *track_](int result) {
                           if (result == -1)
                               actions_.perform("app.scanPlugins");
                           else if (result > 0)
                               selected_ = session_.addDevice(track, devices[static_cast<size_t>(result - 1)].key);
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
}

int DeviceChain::slotIndexOf(const juce::Component* c) const
{
    for (; c != nullptr; c = c->getParentComponent())
        for (size_t i = 0; i < slots_.size(); ++i)
            if (slots_[i].view.get() == c)
                return static_cast<int>(i);
    return -1;
}

int DeviceChain::insertIndexAt(int contentX) const
{
    for (size_t i = 0; i < slots_.size(); ++i)
        if (contentX < slots_[i].view->getBounds().getCentreX())
            return static_cast<int>(i);
    return static_cast<int>(slots_.size());
}

// ---- mouse: selection and reordering by the header ----------------------------------------

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
    if (handle)
    {
        selected_ = slot.info.id;
        dragSlot_ = index;
        content_->repaint();
    }
}

void DeviceChain::mouseDrag(const juce::MouseEvent& e)
{
    if (dragSlot_ < 0 || (!dragging_ && e.getDistanceFromDragStart() < 4))
        return;
    dragging_ = true;
    insertAt_ = insertIndexAt(e.getEventRelativeTo(content_.get()).getPosition().x);
    content_->repaint();
}

void DeviceChain::mouseUp(const juce::MouseEvent&)
{
    if (dragging_ && dragSlot_ >= 0 && insertAt_ >= 0)
    {
        // Dropping after itself means the index shifts by one once it is taken out.
        const int target = insertAt_ > dragSlot_ ? insertAt_ - 1 : insertAt_;
        if (target != dragSlot_)
            session_.moveDevice(slots_[static_cast<size_t>(dragSlot_)].info.id, target);
    }
    dragging_ = false;
    dragSlot_ = -1;
    insertAt_ = -1;
    content_->repaint();
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
    insertAt_ = insertIndexAt(inContent.x);
    if (item)
        for (const auto& s : slots_)
            if (s.view->getBounds().contains(inContent) && browser::landsOn(*item, s.info))
            {
                browserOn_ = s.info.id;
                insertAt_ = -1;
            }
    content_->repaint();
}

void DeviceChain::itemDragExit(const SourceDetails&)
{
    browserDrag_ = false;
    browserOn_.reset();
    insertAt_ = -1;
    content_->repaint();
}

void DeviceChain::itemDropped(const SourceDetails& details)
{
    itemDragMove(details);
    const auto on = browserOn_;
    const int index = insertAt_;
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
    if (onBrowserDrop)
        onBrowserDrop(*item, *track_, index, on);
}

void DeviceChain::selectDevice(std::uint64_t id)
{
    for (const auto& s : slots_)
        if (s.info.id == id)
        {
            selected_ = id;
            const auto b = s.view->getBounds();
            if (b.getX() < viewport_.getViewPositionX() || b.getRight() > viewport_.getViewPositionX() + viewport_.getViewWidth())
                viewport_.setViewPosition(std::max(0, b.getX() - 8), 0);
            content_->repaint();
            return;
        }
}
} // namespace fd::app
