#include "ui/LanePicker.h"

namespace fd::app
{
namespace
{
juce::String utf8(const std::string& s)
{
    return juce::String::fromUTF8(s.c_str());
}

juce::String kindOf(const model::TrackInfo& channel)
{
    switch (channel.role)
    {
    case model::TrackInfo::Role::Track: return "Track";
    case model::TrackInfo::Role::Return: return "Return track";
    case model::TrackInfo::Role::Master: return "Master";
    }
    return {};
}
} // namespace

LanePicker::LanePicker(engine::Session& session) : session_(session)
{
    const auto& t = ui::Theme::current();
    search_.setFont(ui::font(ui::TextStyle::Field));
    search_.setIndents(10, 0);
    search_.setJustification(juce::Justification::centredLeft);
    search_.setColour(juce::TextEditor::backgroundColourId, t.surface2);
    search_.setColour(juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
    search_.setColour(juce::TextEditor::focusedOutlineColourId, juce::Colours::transparentBlack);
    search_.addListener(this);
    search_.addKeyListener(this);
    addAndMakeVisible(search_);
    setVisible(false);
    setWantsKeyboardFocus(false);
}

void LanePicker::open(juce::Component& parent, std::uint64_t lane, std::uint64_t channel)
{
    lane_ = lane;
    const auto* l = session_.snapshot()->automationLane(lane);
    search_.setTextToShowWhenEmpty("Type a parameter for " + (l != nullptr ? utf8(l->displayName()) : juce::String("the lane")),
                                   ui::Theme::current().textTertiary);
    setChannel(channel);
    if (getParentComponent() != &parent)
        parent.addChildComponent(this);
    search_.clear();
    selected_ = 0;
    update();
    setVisible(true);
    toFront(false);
    outside_.attach();
    if (isShowing())
        search_.grabKeyboardFocus();
}

void LanePicker::close()
{
    outside_.detach();
    if (!isVisible())
        return;
    setVisible(false);
    if (auto* parent = getParentComponent(); parent != nullptr && parent->isShowing())
        parent->grabKeyboardFocus();
}

void LanePicker::setChannel(std::uint64_t channel)
{
    channel_ = channel;
    choices_ = session_.automatableParameters(channel);
    items_.clear();
    for (const auto& c : choices_)
        items_.push_back({c.key, c.name, c.module});
    selected_ = 0;
    update();
}

void LanePicker::setQuery(const juce::String& text)
{
    search_.setText(text, true);
    update();
}

std::vector<std::string> LanePicker::shownLanes() const
{
    std::vector<std::string> out;
    for (auto i : shown_)
        out.push_back(items_[i].id);
    return out;
}

void LanePicker::setSelectedRow(int row)
{
    selected_ = std::clamp(row, 0, std::max(0, rowCount() - 1));
    repaint();
}

void LanePicker::update()
{
    shown_ = model::rankPalette(search_.getText().toStdString(), items_, {});
    if (shown_.size() > static_cast<size_t>(maxRows))
        shown_.resize(static_cast<size_t>(maxRows));
    selected_ = std::clamp(selected_, 0, std::max(0, rowCount() - 1));
    if (auto* parent = getParentComponent())
    {
        const auto& sh = ui::Theme::current().shell;
        const int h = rowArea(0).getY() + std::max(1, rowCount()) * sh.listRowHeight + sh.barPadding;
        setBounds(juce::Rectangle<int>(sh.popoverWidth, h).withCentre({parent->getWidth() / 2, 0}).withY(sh.popoverTop));
    }
    repaint();
}

void LanePicker::chooseSelected()
{
    if (selected_ >= rowCount())
        return;
    const engine::Session::ParamTarget target{channel_, items_[shown_[static_cast<size_t>(selected_)]].id};
    close();
    if (session_.assign(lane_, target) && onChosen)
        onChosen(lane_);
}

void LanePicker::showChannelMenu()
{
    // Every channel: the tracks, the return tracks, the master.
    const auto snapshot = session_.snapshot();
    juce::PopupMenu menu;
    auto add = [&](const model::TrackInfo& c) {
        menu.addItem(utf8(c.name), true, c.id == channel_, [this, safe = juce::Component::SafePointer(this), id = c.id] {
            if (safe != nullptr && isOpen())
            {
                setChannel(id);
                search_.grabKeyboardFocus();
            }
        });
    };
    for (const auto& t : snapshot->tracks)
        add(t);
    if (!snapshot->returns.empty())
        menu.addSeparator();
    for (const auto& r : snapshot->returns)
        add(r);
    menu.addSeparator();
    add(snapshot->master);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this).withTargetScreenArea(
        localAreaToGlobal(channelArea())));
}

bool LanePicker::keyPressed(const juce::KeyPress& key, juce::Component*)
{
    if (key == juce::KeyPress::downKey || key == juce::KeyPress::upKey)
    {
        const int n = rowCount();
        if (n > 0)
            selected_ = (selected_ + (key == juce::KeyPress::downKey ? 1 : n - 1)) % n;
        repaint();
        return true;
    }
    return false;
}

juce::Rectangle<int> LanePicker::channelArea() const
{
    const auto& sh = ui::Theme::current().shell;
    const int top = 2 * sh.barPadding + sh.listRowHeight + 8;
    return {sh.barPadding, top, getWidth() - 2 * sh.barPadding, sh.listRowHeight};
}

juce::Rectangle<int> LanePicker::rowArea(int row) const
{
    const auto& sh = ui::Theme::current().shell;
    const auto channel = channelArea();
    const int top = channel.getBottom() + sh.itemGap;
    return {sh.barPadding, top + row * sh.listRowHeight, getWidth() - 2 * sh.barPadding, sh.listRowHeight};
}

void LanePicker::resized()
{
    const auto& sh = ui::Theme::current().shell;
    search_.setBounds(getLocalBounds().reduced(sh.barPadding).removeFromTop(sh.listRowHeight + 8));
}

void LanePicker::paint(juce::Graphics& g)
{
    const auto& t = ui::Theme::current();
    ui::paintPopover(g, getLocalBounds());
    // The channel row: whose parameters these are; a click switches.
    const auto snapshot = session_.snapshot();
    if (const auto* c = snapshot->channel(channel_))
        ui::paintListRow(g, channelArea(),
                         {.name = utf8(c->name), .caption = kindOf(*c), .shortcut = juce::String::fromUTF8("Switch \xE2\x96\xBE")});
    g.setColour(t.line);
    g.fillRect(channelArea().withY(channelArea().getBottom() + t.shell.itemGap / 2).withHeight(1));

    const auto* target = snapshot->automationLane(lane_);
    for (int i = 0; i < rowCount(); ++i)
    {
        const auto& c = choices_[shown_[static_cast<size_t>(i)]];
        auto caption = utf8(c.module);
        if (const auto* on = snapshot->automationLane(c.lane); on != nullptr && on != target)
            caption += juce::String::fromUTF8(" \xC2\xB7 on ") + utf8(on->displayName());
        ui::paintListRow(g, rowArea(i),
                         {.name = utf8(c.name),
                          .caption = caption,
                          .shortcut = utf8(c.valueText),
                          .toggled = target != nullptr && c.lane == target->id,
                          .selected = i == selected_});
    }
    if (shown_.empty())
        ui::paintListRow(g, rowArea(0), {.name = "No parameter by that name", .enabled = false});
}

void LanePicker::mouseMove(const juce::MouseEvent& e)
{
    for (int i = 0; i < rowCount(); ++i)
        if (rowArea(i).contains(e.getPosition()) && selected_ != i)
        {
            selected_ = i;
            repaint();
        }
}

void LanePicker::mouseDown(const juce::MouseEvent& e)
{
    if (channelArea().contains(e.getPosition()))
    {
        showChannelMenu();
        return;
    }
    for (int i = 0; i < rowCount(); ++i)
        if (rowArea(i).contains(e.getPosition()))
        {
            selected_ = i;
            chooseSelected();
            return;
        }
}
} // namespace fd::app
