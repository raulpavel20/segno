#include "ui/MixerView.h"

#include "model/Groups.h"
#include "model/Taper.h"
#include "ui/BrowserDrops.h"

#include <cmath>
#include <set>

namespace fd::app
{
namespace
{
constexpr std::array<float, 7> kScaleMarks{6.0f, 0.0f, -6.0f, -12.0f, -24.0f, -48.0f, model::minusInfinityDb};
constexpr int kDividerGap = 8; // between the tracks and the returns, and before the master
} // namespace

// ---- ChannelStrip -----------------------------------------------------------------------------

ChannelStrip::ChannelStrip(engine::Session& session, const model::TrackInfo& channel)
    : session_(session), info_(channel), fader_(session, channel.id, ui::Knob::Style::Fader), pan_(session, channel.id),
      mute_(session, ChannelToggle::Kind::Mute, channel.id), solo_(session, ChannelToggle::Kind::Solo, channel.id),
      arm_(session, ChannelToggle::Kind::Arm, channel.id), input_(session), meter_(session, channel.id),
      value_(session, channel.id)
{
    fader_.setName({});
    for (juce::Component* c : {static_cast<juce::Component*>(&fader_), static_cast<juce::Component*>(&pan_),
                               static_cast<juce::Component*>(&meter_), static_cast<juce::Component*>(&value_)})
        addAndMakeVisible(c);
    if (channel.role != model::TrackInfo::Role::Master) // the master has no mute or solo
    {
        addAndMakeVisible(mute_);
        addAndMakeVisible(solo_);
    }
    if (channel.holdsClips()) // tracks record
    {
        addAndMakeVisible(arm_);
        addAndMakeVisible(input_);
    }
}

void ChannelStrip::update(const model::TrackInfo& channel, const std::vector<model::TrackInfo>& returns,
                          const model::ChannelStripOptions& options, bool selected, const Groups& groups)
{
    const bool relayout = !(options.modules == options_.modules && options.sends == options_.sends &&
                            options.narrow == options_.narrow && options.sendRows == options_.sendRows &&
                            options.groupRows == options_.groupRows) ||
                          groups.colours.size() != groups_.colours.size() ||
                          groups.folded.has_value() != groups_.folded.has_value();
    info_ = channel;
    options_ = options;
    groups_ = groups;
    if (selected != selected_)
    {
        selected_ = selected;
        repaint();
    }

    // Sends: a track's or a group's, one per return track, when the view shows them.
    std::vector<std::uint64_t> wanted;
    if ((channel.role == model::TrackInfo::Role::Track || channel.isGroup()) && options.sends)
        for (const auto& r : returns)
            wanted.push_back(r.id);
    if (wanted != sendReturns_)
    {
        sends_.clear();
        for (auto id : wanted)
        {
            Send s;
            s.level = std::make_unique<SendBar>(session_, channel.id, id);
            s.pre = std::make_unique<ChannelToggle>(session_, ChannelToggle::Kind::PreFader, channel.id, id);
            addAndMakeVisible(*s.level);
            addChildComponent(*s.pre);
            sends_.push_back(std::move(s));
        }
        sendReturns_ = std::move(wanted);
        resized();
    }
    for (size_t i = 0; i < sends_.size() && i < channel.mix.sends.size(); ++i)
    {
        sends_[i].level->update(channel.mix.sends[i], juce::String::fromUTF8(returns[i].name.c_str()), false);
        sends_[i].pre->update(channel.mix.sends[i].preFader);
    }

    pan_.setName(options.narrow ? juce::String() : juce::String("Pan"));
    value_.setCompact(options.narrow);
    fader_.update(channel);
    pan_.update(channel);
    mute_.update(channel.mix.mute);
    solo_.update(channel.mix.solo);
    mute_.setPartlyOn(groups.mutedByGroup); // half lit by its groups (groups.md §7.3)
    solo_.setPartlyOn(groups.holdsSolo);
    arm_.updateArm(channel.input);
    input_.update(channel);
    value_.update(channel);
    if (relayout)
        resized();
    repaint();
}

juce::Rectangle<int> ChannelStrip::at(const model::HeaderRect& r) const
{
    return juce::Rectangle<double>(r.x, r.y, r.w, r.h).toNearestInt();
}

void ChannelStrip::resized()
{
    slots_ = model::layoutChannelStrip(getHeight(), options_, {}, static_cast<int>(groups_.colours.size()),
                                       groups_.folded.has_value());
    fader_.setBounds(at(slots_.fader));
    meter_.setBounds(at(slots_.meter));
    pan_.setBounds(at(slots_.pan));
    mute_.setBounds(at(slots_.mute));
    solo_.setBounds(at(slots_.solo));
    arm_.setVisible(info_.holdsClips() && !slots_.arm.isEmpty());
    arm_.setBounds(at(slots_.arm));
    input_.setBounds(at(slots_.input));
    value_.setBounds(at(slots_.value));
    for (size_t i = 0; i < sends_.size(); ++i)
    {
        const bool shown = i < slots_.sends.size();
        sends_[i].level->setVisible(shown);
        if (shown)
            sends_[i].level->setBounds(at(slots_.sends[i]));
        const bool pre = shown && !slots_.sendPre[i].isEmpty();
        sends_[i].pre->setVisible(pre);
        if (pre)
            sends_[i].pre->setBounds(at(slots_.sendPre[i]));
    }
}

void ChannelStrip::paint(juce::Graphics& g)
{
    const auto& t = ui::Theme::current();
    const bool master = info_.role == model::TrackInfo::Role::Master;
    g.fillAll(selected_ ? t.surface2 : t.surface1);
    g.setColour(t.line);
    g.fillRect(getWidth() - 1, 0, 1, getHeight());
    g.setColour(master ? t.textTertiary : t.clipColour(info_.colour));
    g.fillRect(at(slots_.colour));
    // Its groups' bars, across the top of their members' strips (groups.md §9).
    for (size_t k = 0; k < slots_.groupBars.size() && k < groups_.colours.size(); ++k)
    {
        g.setColour(t.clipColour(groups_.colours[k]));
        g.fillRect(at(slots_.groupBars[k]));
    }
    if (groups_.folded && !slots_.fold.isEmpty())
        ui::drawIcon(g, *groups_.folded ? ui::Icon::FoldClosed : ui::Icon::FoldOpen,
                     at(slots_.fold).toFloat(), t.textSecondary);

    g.setColour(t.textPrimary);
    g.setFont(ui::font(ui::TextStyle::Name));
    g.drawText(juce::String::fromUTF8(info_.name.c_str()), at(slots_.name), juce::Justification::centredLeft, true);

    // Module names, dimmed when off; the last row says how many more there are.
    g.setFont(ui::font(ui::TextStyle::Caption));
    const auto rows = slots_.modules.size();
    for (size_t i = 0; i < rows && i < info_.devices.size(); ++i)
    {
        const bool more = i + 1 == rows && info_.devices.size() > rows;
        const auto& d = info_.devices[i];
        g.setColour(more || !d.enabled ? t.textTertiary : t.textSecondary);
        g.drawText(more ? "+" + juce::String(static_cast<int>(info_.devices.size() - rows) + 1) + " more"
                        : juce::String::fromUTF8(d.name.c_str()),
                   at(slots_.modules[i]), juce::Justification::centredLeft, true);
    }

    // The dB scale beside the fader, on the same taper.
    if (!slots_.scale.isEmpty())
    {
        const auto scale = at(slots_.scale).toFloat();
        const float cap = static_cast<float>(t.faderCapHeight);
        const float travel = std::max(1.0f, scale.getHeight() - cap);
        g.setFont(ui::font(ui::TextStyle::Micro));
        for (float db : kScaleMarks)
        {
            const float y = scale.getY() + cap * 0.5f + (1.0f - static_cast<float>(model::volumePosition(db))) * travel;
            g.setColour(t.textTertiary);
            g.drawText(db <= model::minusInfinityDb ? juce::String("-inf") : juce::String(juce::roundToInt(db)),
                       juce::Rectangle<float>(scale.getX(), y - 6.0f, scale.getWidth(), 12.0f),
                       juce::Justification::centred, false);
        }
    }

    // The peak readout: the loudest peak since it was last clicked.
    const float peak = meter_.heldPeak();
    g.setFont(ui::font(ui::TextStyle::ValueSmall));
    g.setColour(peak > 1.0f ? t.meterClip : peak > 0.0f ? t.textSecondary : t.textTertiary);
    g.drawText(peak > 1e-5f ? juce::String(20.0f * std::log10(peak), 1) : juce::String::fromUTF8("\xE2\x80\x93"),
               at(slots_.readout), juce::Justification::centred, false);
}

void ChannelStrip::mouseDown(const juce::MouseEvent& e)
{
    const auto pos = e.getPosition();
    if (at(slots_.readout).contains(pos))
    {
        meter_.resetHeld();
        shownPeak_ = -1.0f;
        repaint(at(slots_.readout));
        return;
    }
    for (size_t i = 0; i < slots_.modules.size() && i < info_.devices.size(); ++i)
        if (at(slots_.modules[i]).contains(pos) && onSelectModule)
        {
            onSelectModule(info_.devices[i].id);
            return;
        }
    // Alone, or with others (groups.md §8): a press on one of several keeps them, for a drag.
    pendingAlone_ = false;
    if (onChoose)
        pendingAlone_ = onChoose(e.mods);
    else if (onSelect)
        onSelect();
    if (e.mods.isPopupMenu() && onMenu)
        onMenu();
    if ((e.mods.isCommandDown() || e.mods.isShiftDown()) && !e.mods.isPopupMenu())
        return; // a choice, not a drag or a rename
    // Its name: a double-click renames it, a drag moves it (not the master's).
    nameDrag_ = nameMoving_ = false;
    if (info_.role == model::TrackInfo::Role::Master || !at(slots_.name).contains(pos) || !e.mods.isLeftButtonDown())
        return;
    if (e.getNumberOfClicks() >= 2)
    {
        if (onRename)
            onRename();
        return;
    }
    nameDrag_ = true;
}

void ChannelStrip::mouseDrag(const juce::MouseEvent& e)
{
    if (!nameDrag_ || (!nameMoving_ && e.getDistanceFromDragStart() < ui::Theme::current().input.dragThreshold))
        return;
    nameMoving_ = true;
    if (onNameDrag)
        onNameDrag(e);
}

void ChannelStrip::mouseUp(const juce::MouseEvent& e)
{
    if (nameMoving_ && onNameDrop)
        onNameDrop(e);
    else if (std::exchange(pendingAlone_, false) && onSelect)
        onSelect(); // a click on one of several: it alone
    nameDrag_ = nameMoving_ = false;
}

void ChannelStrip::tick()
{
    if (std::abs(meter_.heldPeak() - shownPeak_) > 1e-6f)
    {
        shownPeak_ = meter_.heldPeak();
        repaint(at(slots_.readout));
    }
}

// ---- MixerView --------------------------------------------------------------------------------

void MixerView::showAddMenu()
{
    juce::PopupMenu menu;
    if (onAddMenu)
        onAddMenu(menu);
    if (menu.getNumItems() > 0)
        menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this).withMousePosition());
}

void MixerView::Content::paint(juce::Graphics& g)
{
    const auto& t = ui::Theme::current();
    g.fillAll(t.surface0);
    if (dividerX >= 0)
    {
        g.setColour(t.line);
        g.fillRect(dividerX + kDividerGap / 2 - 1, 0, 2, getHeight());
    }
}

MixerView::MixerView(engine::Session& session) : session_(session)
{
    header_.setTitle("Mixer");
    addAndMakeVisible(header_);
    content_.onMouseDown = [this](const juce::MouseEvent& e) {
        if (e.mods.isPopupMenu())
            showAddMenu(); // the empty space beside the strips
    };
    for (auto* option : {&modules_, &sends_, &narrow_})
    {
        option->onClick = [this] {
            setOptions(modules_.getToggleState(), sends_.getToggleState(), narrow_.getToggleState());
            if (onViewChanged)
                onViewChanged();
        };
        header_.addOption(*option);
    }
    modules_.setTooltip("Show the modules' names");
    sends_.setTooltip("Show the sends");
    narrow_.setTooltip("Narrow strips, for many tracks");
    setOptions(true, true, false);
    viewport_.setViewedComponent(&content_, false);
    viewport_.setScrollBarsShown(false, true);
    viewport_.setScrollBarThickness(6);
    viewport_.setWantsKeyboardFocus(false); // the view takes the arrow keys, not the viewport
    addAndMakeVisible(viewport_);
    setWantsKeyboardFocus(true);
}

void MixerView::setOptions(bool modules, bool sends, bool narrow)
{
    modules_.setToggleState(modules, juce::dontSendNotification);
    sends_.setToggleState(sends, juce::dontSendNotification);
    narrow_.setToggleState(narrow, juce::dontSendNotification);
    options_.modules = modules;
    options_.sends = sends;
    options_.narrow = narrow;
    rebuild();
}

int MixerView::scrollX() const
{
    return viewport_.getViewPositionX();
}

void MixerView::setScrollX(int x)
{
    viewport_.setViewPosition(x, 0);
}

void MixerView::setSnapshot(std::shared_ptr<const model::Snapshot> snapshot)
{
    snapshot_ = std::move(snapshot);
    rebuild();
}

void MixerView::setFoldedGroups(std::set<std::uint64_t> folded)
{
    if (folded == folded_)
        return;
    folded_ = std::move(folded);
    rebuild();
}

void MixerView::setSelection(std::optional<std::uint64_t> channel, std::vector<std::uint64_t> together)
{
    if (channel == selected_ && together == together_)
        return;
    selected_ = channel;
    together_ = std::move(together);
    rebuild();
}

std::vector<std::uint64_t> MixerView::shownStrips() const
{
    std::vector<std::uint64_t> out;
    if (snapshot_ != nullptr)
        for (const auto& t : snapshot_->tracks)
            if (const auto it = strips_.find(t.id); it != strips_.end() && it->second->isVisible())
                out.push_back(t.id);
    return out;
}

// ---- drops from the browser (browser.md §10) -----------------------------------------------

std::uint64_t MixerView::stripAt(juce::Point<int> position) const
{
    for (const auto& [channel, strip] : strips_)
        if (strip->isShowing() && getLocalArea(strip.get(), strip->getLocalBounds()).contains(position))
            return channel;
    if (master_ != nullptr && getLocalArea(master_.get(), master_->getLocalBounds()).contains(position))
        return master_->channel();
    return 0;
}

bool MixerView::isInterestedInDragSource(const SourceDetails& details)
{
    return browser::itemOf(details.description).has_value();
}

void MixerView::itemDragMove(const SourceDetails& details)
{
    const auto target = stripAt(details.localPosition);
    if (browserTarget_ != target)
    {
        browserTarget_ = target;
        repaint();
    }
}

void MixerView::itemDragExit(const SourceDetails&)
{
    browserTarget_.reset();
    repaint();
}

void MixerView::itemDropped(const SourceDetails& details)
{
    const auto target = stripAt(details.localPosition);
    browserTarget_.reset();
    repaint();
    if (const auto item = browser::itemOf(details.description); item && onBrowserDrop)
        onBrowserDrop(*item, target);
}

void MixerView::paintOverChildren(juce::Graphics& g)
{
    if (!browserTarget_ || *browserTarget_ == 0)
        return;
    const juce::Component* strip =
        master_ != nullptr && master_->channel() == *browserTarget_ ? master_.get() : nullptr;
    if (const auto it = strips_.find(*browserTarget_); it != strips_.end())
        strip = it->second.get();
    if (strip == nullptr)
        return;
    const auto& t = ui::Theme::current();
    g.setColour(t.active);
    g.drawRect(getLocalArea(strip, strip->getLocalBounds()), t.shell.dropLine);
}

void MixerView::Content::paintOverChildren(juce::Graphics& g)
{
    if (dropX < 0)
        return;
    const auto& t = ui::Theme::current();
    g.setColour(t.active);
    g.fillRect(dropX - t.shell.dropLine / 2, 0, t.shell.dropLine, getHeight());
}

bool MixerView::renameChannel(std::uint64_t channel)
{
    const auto it = strips_.find(channel);
    const auto* info = snapshot_ != nullptr ? snapshot_->channel(channel) : nullptr;
    if (it == strips_.end() || info == nullptr)
        return false;
    auto& strip = *it->second;
    // In sight, then the field over its name, its text where the name was drawn.
    viewport_.setViewPosition(std::clamp(viewport_.getViewPositionX(), strip.getRight() - viewport_.getViewWidth(), strip.getX()), 0);
    const auto& sh = ui::Theme::current().shell;
    const auto name = strip.nameArea().translated(strip.getX(), strip.getY());
    const auto field = name.withLeft(name.getX() - sh.fieldTextInset).withSizeKeepingCentre(name.getWidth() + sh.fieldTextInset, sh.rowHeight);
    names_.show(field.constrainedWithin(strip.getBounds()), juce::String::fromUTF8(info->name.c_str()), ui::TextStyle::Name,
                [this, channel](const juce::String& n) { session_.renameTrack(channel, n.toStdString()); });
    return true;
}

void MixerView::dragStrip(std::uint64_t channel, const juce::MouseEvent& e, bool drop)
{
    const auto* info = snapshot_ != nullptr ? snapshot_->channel(channel) : nullptr;
    if (info == nullptr)
        return;
    // Among its kind: the edge nearest the pointer, a strip's left or the last one's right.
    const auto& kind = info->role == model::TrackInfo::Role::Return ? snapshot_->returns : snapshot_->tracks;
    const float x = e.getEventRelativeTo(&content_).position.x;
    std::uint64_t before = 0;
    int edge = -1;
    float distance = std::numeric_limits<float>::max();
    for (size_t i = 0; i <= kind.size() && !kind.empty(); ++i)
    {
        const auto at = strips_.find(i < kind.size() ? kind[i].id : kind.back().id);
        if (at == strips_.end())
            continue;
        const int ex = i < kind.size() ? at->second->getX() : at->second->getRight();
        if (const auto d = std::abs(static_cast<float>(ex) - x); d < distance)
        {
            distance = d;
            edge = ex;
            before = i < kind.size() ? kind[i].id : 0;
        }
    }
    content_.dropX = drop ? -1 : edge;
    content_.repaint();
    // Its own edges leave it where it is: the strip after it is no place to land, since the lanes
    // after it in the arrangement would then sit above it (ADR 0042).
    const auto own = std::find_if(kind.begin(), kind.end(), [channel](const auto& k) { return k.id == channel; });
    const auto next = own == kind.end() || std::next(own) == kind.end() ? std::uint64_t{0} : std::next(own)->id;
    // Several chosen together move together, in their order (groups.md §8), one step; dropped
    // among them, before the first strip after the drop that isn't one of them.
    const auto among = [this](std::uint64_t id)
    { return std::find(together_.begin(), together_.end(), id) != together_.end(); };
    if (drop && together_.size() > 1 && among(channel))
    {
        if (before != 0 && among(before))
        {
            auto it =
                std::find_if(kind.begin(), kind.end(), [before](const auto& k) { return k.id == before; });
            while (it != kind.end() && among(it->id))
                ++it;
            before = it == kind.end() ? 0 : it->id;
        }
        const engine::Session::EditGroup step(session_, "Move tracks");
        for (const auto id : together_)
            session_.moveTrack(id, before);
        return;
    }
    if (drop && before != channel && before != next)
        session_.moveTrack(channel, before);
}

void MixerView::clear()
{
    strips_.clear();
    master_.reset();
    snapshot_.reset();
    selected_.reset();
}

std::vector<std::uint64_t> MixerView::order() const
{
    std::vector<std::uint64_t> ids;
    if (snapshot_ == nullptr)
        return ids;
    for (const auto* list : {&snapshot_->tracks, &snapshot_->returns})
        for (const auto& t : *list)
            ids.push_back(t.id);
    ids.push_back(snapshot_->master.id);
    return ids;
}

void MixerView::rebuild()
{
    if (snapshot_ == nullptr)
        return;
    options_.sendRows = static_cast<int>(snapshot_->returns.size());
    std::set<std::uint64_t> wanted;
    auto make = [&](const model::TrackInfo& t) -> ChannelStrip& {
        auto& strip = t.role == model::TrackInfo::Role::Master ? master_ : strips_[t.id];
        if (strip == nullptr || strip->channel() != t.id)
        {
            strip = std::make_unique<ChannelStrip>(session_, t);
            const auto id = t.id;
            strip->onSelect = [this, id] {
                if (isShowing())
                    grabKeyboardFocus();
                if (onSelect)
                    onSelect(id);
            };
            strip->onChoose = [this, id](juce::ModifierKeys mods)
            {
                if (isShowing())
                    grabKeyboardFocus();
                if (onChoose)
                    return onChoose(id, mods);
                if (onSelect)
                    onSelect(id);
                return false;
            };
            strip->onMenu = [this] { showAddMenu(); };
            if (t.role != model::TrackInfo::Role::Master)
            {
                strip->onRename = [this, id] { renameChannel(id); };
                strip->onNameDrag = [this, id](const juce::MouseEvent& e) { dragStrip(id, e, false); };
                strip->onNameDrop = [this, id](const juce::MouseEvent& e) { dragStrip(id, e, true); };
            }
            strip->onSelectModule = [this, id](std::uint64_t module) {
                if (isShowing())
                    grabKeyboardFocus(); // Left and Right keep moving the selection
                if (onSelectModule)
                    onSelectModule(id, module);
            };
            if (t.role == model::TrackInfo::Role::Master)
                addAndMakeVisible(*strip);
            else
                content_.addAndMakeVisible(*strip);
        }
        ChannelStrip::Groups groups;
        for (const auto* g : model::groupsAbove(*snapshot_, t))
            groups.colours.push_back(g->colour);
        if (t.isGroup())
            groups.folded = folded_.contains(t.id);
        groups.mutedByGroup = model::inMutedGroup(*snapshot_, t);
        groups.holdsSolo = model::holdsSolo(*snapshot_, t);
        const bool lit =
            selected_ == t.id || std::find(together_.begin(), together_.end(), t.id) != together_.end();
        strip->update(t, snapshot_->returns, options_, lit, groups);
        return *strip;
    };
    // The bars' rows: as many as the deepest strip shown has.
    options_.groupRows = 0;
    for (const auto& t : snapshot_->tracks)
        if (!model::hiddenByFolding(*snapshot_, t, folded_))
            options_.groupRows = std::max(options_.groupRows, model::groupDepth(*snapshot_, t));
    for (const auto* list : {&snapshot_->tracks, &snapshot_->returns})
        for (const auto& t : *list)
        {
            make(t);
            wanted.insert(t.id);
        }
    for (auto it = strips_.begin(); it != strips_.end();)
        it = wanted.contains(it->first) ? std::next(it) : strips_.erase(it);
    make(snapshot_->master);
    layoutStrips();
}

void MixerView::layoutStrips()
{
    if (snapshot_ == nullptr)
        return;
    const int width = static_cast<int>(model::channelStripWidth(options_));
    const int height = viewport_.getHeight() - viewport_.getScrollBarThickness();
    int x = 0;
    content_.dividerX = -1;
    for (const auto* list : {&snapshot_->tracks, &snapshot_->returns})
    {
        if (list == &snapshot_->returns && !list->empty())
        {
            content_.dividerX = x;
            x += kDividerGap;
        }
        for (const auto& t : *list)
            if (auto it = strips_.find(t.id); it != strips_.end())
            {
                // A folded group's members are hidden (groups.md §9).
                const bool shown = !model::hiddenByFolding(*snapshot_, t, folded_);
                it->second->setVisible(shown);
                if (!shown)
                    continue;
                it->second->setBounds(x, 0, width, height);
                x += width;
            }
    }
    content_.setSize(std::max(x, viewport_.getWidth()), height);
    content_.repaint();
}

void MixerView::resized()
{
    const auto& t = ui::Theme::current();
    auto r = getLocalBounds();
    header_.setBounds(r.removeFromTop(t.shell.panelHeaderHeight));
    const int width = static_cast<int>(model::channelStripWidth(options_));
    if (master_ != nullptr)
        master_->setBounds(r.removeFromRight(width).withTrimmedBottom(viewport_.getScrollBarThickness()));
    r.removeFromRight(kDividerGap);
    viewport_.setBounds(r);
    layoutStrips();
}

void MixerView::paint(juce::Graphics& g)
{
    const auto& t = ui::Theme::current();
    g.fillAll(t.surface0);
    g.setColour(t.surface1);
    g.fillRect(getLocalBounds().removeFromTop(t.shell.panelHeaderHeight));
    if (master_ != nullptr) // the divider before the pinned master
    {
        g.setColour(t.line);
        g.fillRect(master_->getX() - kDividerGap / 2 - 1, master_->getY(), 2, master_->getHeight());
    }
}

bool MixerView::keyPressed(const juce::KeyPress& key)
{
    const bool left = key == juce::KeyPress::leftKey, right = key == juce::KeyPress::rightKey;
    if (!left && !right)
        return false;
    const auto ids = order();
    if (ids.empty())
        return true;
    auto it = selected_ ? std::find(ids.begin(), ids.end(), *selected_) : ids.end();
    size_t index = it == ids.end() ? 0 : static_cast<size_t>(it - ids.begin());
    if (it != ids.end())
        index = left ? (index == 0 ? 0 : index - 1) : std::min(ids.size() - 1, index + 1);
    if (onSelect)
        onSelect(ids[index]);
    // Keep the selected strip in view.
    if (auto s = strips_.find(ids[index]); s != strips_.end())
    {
        const auto b = s->second->getBounds();
        if (b.getX() < scrollX())
            setScrollX(b.getX());
        else if (b.getRight() > scrollX() + viewport_.getViewWidth())
            setScrollX(b.getRight() - viewport_.getViewWidth());
    }
    return true;
}
} // namespace fd::app
