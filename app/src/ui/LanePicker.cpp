#include "ui/LanePicker.h"

namespace fd::app
{
LanePicker::LanePicker(engine::Session& session) : session_(session)
{
    const auto& t = ui::Theme::current();
    search_.setFont(ui::font(ui::TextStyle::Field));
    search_.setTextToShowWhenEmpty("Type a parameter", t.textTertiary);
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

void LanePicker::open(juce::Component& parent, std::uint64_t channel, std::uint64_t after)
{
    channel_ = channel;
    after_ = after;
    choices_ = session_.automatableParameters(channel);
    items_.clear();
    for (const auto& c : choices_)
        items_.push_back({c.key, c.name, c.module});
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

void LanePicker::update()
{
    shown_ = model::rankPalette(search_.getText().toStdString(), items_, {});
    if (shown_.size() > static_cast<size_t>(maxRows))
        shown_.resize(static_cast<size_t>(maxRows));
    selected_ = std::clamp(selected_, 0, std::max(0, static_cast<int>(shown_.size()) - 1));
    if (auto* parent = getParentComponent())
    {
        const auto& sh = ui::Theme::current().shell;
        const int h = rowArea(0).getY() + static_cast<int>(std::max<size_t>(1, shown_.size())) * sh.listRowHeight + sh.barPadding;
        setBounds(juce::Rectangle<int>(sh.popoverWidth, h).withCentre({parent->getWidth() / 2, 0}).withY(sh.popoverTop));
    }
    repaint();
}

void LanePicker::chooseSelected()
{
    if (shown_.empty())
        return;
    const engine::Session::ParamTarget target{channel_, items_[shown_[static_cast<size_t>(selected_)]].id};
    close();
    if (const auto lane = session_.addAutomationLane(after_, target); lane != 0 && onChosen)
        onChosen(lane);
}

bool LanePicker::keyPressed(const juce::KeyPress& key, juce::Component*)
{
    if (key == juce::KeyPress::downKey || key == juce::KeyPress::upKey)
    {
        const int n = static_cast<int>(shown_.size());
        if (n > 0)
            selected_ = (selected_ + (key == juce::KeyPress::downKey ? 1 : n - 1)) % n;
        repaint();
        return true;
    }
    return false;
}

juce::Rectangle<int> LanePicker::rowArea(int row) const
{
    const auto& sh = ui::Theme::current().shell;
    const int top = 2 * sh.barPadding + sh.listRowHeight + 8;
    return {sh.barPadding, top + row * sh.listRowHeight, getWidth() - 2 * sh.barPadding, sh.listRowHeight};
}

void LanePicker::resized()
{
    const auto& sh = ui::Theme::current().shell;
    search_.setBounds(getLocalBounds().reduced(sh.barPadding).removeFromTop(sh.listRowHeight + 8));
}

void LanePicker::paint(juce::Graphics& g)
{
    ui::paintPopover(g, getLocalBounds());
    if (shown_.empty())
    {
        ui::paintListRow(g, rowArea(0), {.name = "No parameter by that name", .enabled = false});
        return;
    }
    for (int i = 0; i < static_cast<int>(shown_.size()); ++i)
    {
        const auto& c = choices_[shown_[static_cast<size_t>(i)]];
        ui::paintListRow(g, rowArea(i),
                         {.name = juce::String::fromUTF8(c.name.c_str()),
                          .caption = juce::String::fromUTF8(c.module.c_str()),
                          .shortcut = juce::String::fromUTF8(c.valueText.c_str()),
                          .toggled = c.lane != 0,
                          .selected = i == selected_});
    }
}

void LanePicker::mouseMove(const juce::MouseEvent& e)
{
    for (int i = 0; i < static_cast<int>(shown_.size()); ++i)
        if (rowArea(i).contains(e.getPosition()) && selected_ != i)
        {
            selected_ = i;
            repaint();
        }
}

void LanePicker::mouseDown(const juce::MouseEvent& e)
{
    for (int i = 0; i < static_cast<int>(shown_.size()); ++i)
        if (rowArea(i).contains(e.getPosition()))
        {
            selected_ = i;
            chooseSelected();
            return;
        }
}
} // namespace fd::app
