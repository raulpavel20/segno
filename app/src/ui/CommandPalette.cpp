#include "ui/CommandPalette.h"

namespace fd::app
{
CommandPalette::CommandPalette(const ActionRegistry& actions) : actions_(actions)
{
    const auto& t = ui::Theme::current();
    search_.setFont(ui::font(ui::TextStyle::Field));
    search_.setTextToShowWhenEmpty("Type a command", t.textTertiary);
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
    // The top bar's search button toggles it itself.
    outside_.ignore = [](const juce::Component& c) {
        return dynamic_cast<const ui::IconButton*>(&c) != nullptr && c.getName() == "palette";
    };
}

void CommandPalette::open(juce::Component& parent)
{
    // The palette lists the registry as it is now (actions can be added later).
    items_.clear();
    for (const auto& a : actions_.all())
        if (a.id != "app.commandPalette")
            items_.push_back({a.id, a.name.toStdString(), a.category.toStdString()});
    if (getParentComponent() != &parent)
        parent.addChildComponent(this);
    search_.clear();
    selected_ = 0;
    update();
    setVisible(true);
    toFront(false);
    outside_.attach();
    search_.grabKeyboardFocus();
}

void CommandPalette::close()
{
    outside_.detach();
    if (!isVisible())
        return;
    setVisible(false);
    if (auto* parent = getParentComponent())
        parent->grabKeyboardFocus(); // shortcuts work again at once
}

void CommandPalette::setQuery(const juce::String& text)
{
    search_.setText(text, true);
    update();
}

std::vector<std::string> CommandPalette::shownIds() const
{
    std::vector<std::string> out;
    for (auto i : shown_)
        out.push_back(items_[i].id);
    return out;
}

void CommandPalette::update()
{
    shown_ = model::rankPalette(search_.getText().toStdString(), items_, recent_);
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

void CommandPalette::performSelected()
{
    if (shown_.empty())
        return;
    const auto id = items_[shown_[static_cast<size_t>(selected_)]].id;
    close();
    if (actions_.perform(id))
    {
        std::erase(recent_, id);
        recent_.insert(recent_.begin(), id);
        if (recent_.size() > 20)
            recent_.pop_back();
    }
}

bool CommandPalette::keyPressed(const juce::KeyPress& key, juce::Component*)
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

juce::Rectangle<int> CommandPalette::rowArea(int row) const
{
    // Below the search field: padding, the field (a list row plus 8), padding, then the rows.
    const auto& sh = ui::Theme::current().shell;
    const int top = 2 * sh.barPadding + sh.listRowHeight + 8;
    return {sh.barPadding, top + row * sh.listRowHeight, getWidth() - 2 * sh.barPadding, sh.listRowHeight};
}

void CommandPalette::resized()
{
    const auto& sh = ui::Theme::current().shell;
    search_.setBounds(getLocalBounds().reduced(sh.barPadding).removeFromTop(sh.listRowHeight + 8));
}

void CommandPalette::paint(juce::Graphics& g)
{
    ui::paintPopover(g, getLocalBounds());
    if (shown_.empty())
    {
        ui::paintListRow(g, rowArea(0), {.name = "No matching command", .enabled = false});
        return;
    }
    for (int i = 0; i < static_cast<int>(shown_.size()); ++i)
    {
        const auto& item = items_[shown_[static_cast<size_t>(i)]];
        const auto* action = actions_.find(item.id);
        ui::paintListRow(g, rowArea(i),
                         {.name = juce::String::fromUTF8(item.name.c_str()),
                          .caption = juce::String::fromUTF8(item.category.c_str()),
                          .shortcut = actions_.shortcut(item.id),
                          .toggled = action != nullptr && action->isToggled && action->isToggled(),
                          .enabled = action != nullptr && (!action->isEnabled || action->isEnabled()),
                          .selected = i == selected_});
    }
}

void CommandPalette::mouseMove(const juce::MouseEvent& e)
{
    for (int i = 0; i < static_cast<int>(shown_.size()); ++i)
        if (rowArea(i).contains(e.getPosition()) && selected_ != i)
        {
            selected_ = i;
            repaint();
        }
}

void CommandPalette::mouseDown(const juce::MouseEvent& e)
{
    for (int i = 0; i < static_cast<int>(shown_.size()); ++i)
        if (rowArea(i).contains(e.getPosition()))
        {
            selected_ = i;
            performSelected();
            return;
        }
}
} // namespace fd::app
