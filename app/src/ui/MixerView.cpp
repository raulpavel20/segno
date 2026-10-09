#include "ui/MixerView.h"

#include "model/Taper.h"

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
      meter_(session, channel.id), value_(session, channel.id)
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
}

void ChannelStrip::update(const model::TrackInfo& channel, const std::vector<model::TrackInfo>& returns,
                          const model::ChannelStripOptions& options, bool selected)
{
    const bool relayout = !(options.modules == options_.modules && options.sends == options_.sends &&
                            options.narrow == options_.narrow && options.sendRows == options_.sendRows);
    info_ = channel;
    options_ = options;
    if (selected != selected_)
    {
        selected_ = selected;
        repaint();
    }

    // Sends: a track's, one per return track, when the view shows them.
    std::vector<std::uint64_t> wanted;
    if (channel.role == model::TrackInfo::Role::Track && options.sends)
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
    slots_ = model::layoutChannelStrip(getHeight(), options_);
    fader_.setBounds(at(slots_.fader));
    meter_.setBounds(at(slots_.meter));
    pan_.setBounds(at(slots_.pan));
    mute_.setBounds(at(slots_.mute));
    solo_.setBounds(at(slots_.solo));
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
    if (onSelect)
        onSelect();
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

void MixerView::setSelection(std::optional<std::uint64_t> channel)
{
    if (channel == selected_)
        return;
    selected_ = channel;
    rebuild();
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
        strip->update(t, snapshot_->returns, options_, selected_ == t.id);
        return *strip;
    };
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
