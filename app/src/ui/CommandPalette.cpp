#include "ui/CommandPalette.h"

namespace fd::app
{
CommandPalette::CommandPalette(const ActionRegistry& actions) : actions_(actions)
{
    const auto& t = ui::Theme::current();
    search_.setFont(ui::fonts::font(t.textMd));
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
    search_.grabKeyboardFocus();
}

void CommandPalette::close()
{
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
        const int h = 52 + static_cast<int>(std::max<size_t>(1, shown_.size())) * rowHeight + 8;
        setBounds(juce::Rectangle<int>(width, h).withCentre({parent->getWidth() / 2, 0}).withY(56));
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

void CommandPalette::textEditorFocusLost(juce::TextEditor&)
{
    // Clicking elsewhere closes the popover (after a click inside has been handled).
    juce::MessageManager::callAsync([safe = juce::Component::SafePointer<CommandPalette>(this)] {
        if (safe != nullptr && !safe->search_.hasKeyboardFocus(true))
            safe->close();
    });
}

juce::Rectangle<int> CommandPalette::rowArea(int row) const
{
    return {8, 52 + row * rowHeight, getWidth() - 16, rowHeight};
}

void CommandPalette::resized()
{
    search_.setBounds(getLocalBounds().reduced(8).removeFromTop(36));
}

void CommandPalette::paint(juce::Graphics& g)
{
    const auto& t = ui::Theme::current();
    const auto r = getLocalBounds().toFloat();
    // A floating popover may cast one subtle shadow (ui-principles §4).
    juce::DropShadow(juce::Colours::black.withAlpha(0.5f), 18, {0, 6}).drawForRectangle(g, getLocalBounds());
    g.setColour(t.surface1);
    g.fillRoundedRectangle(r, t.radiusPanel);
    g.setColour(t.line);
    g.drawRoundedRectangle(r.reduced(0.5f), t.radiusPanel, 1.0f);

    if (shown_.empty())
    {
        g.setColour(t.textTertiary);
        g.setFont(ui::fonts::font(t.textSm));
        g.drawText("No matching command", rowArea(0).reduced(10, 0), juce::Justification::centredLeft, false);
        return;
    }
    for (int i = 0; i < static_cast<int>(shown_.size()); ++i)
    {
        const auto& item = items_[shown_[static_cast<size_t>(i)]];
        const auto* action = actions_.find(item.id);
        const bool enabled = action != nullptr && (!action->isEnabled || action->isEnabled());
        auto row = rowArea(i);
        if (i == selected_)
        {
            g.setColour(t.surface3);
            g.fillRoundedRectangle(row.toFloat(), t.radiusControl);
        }
        row.reduce(10, 0);
        if (action != nullptr && !action->keys.empty())
        {
            g.setColour(t.textTertiary);
            g.setFont(ui::fonts::font(t.textXs, ui::fonts::Weight::Medium));
            g.drawText(action->keys.front().getTextDescriptionWithIcons(), row.removeFromRight(120),
                       juce::Justification::centredRight, false);
        }
        if (action != nullptr && action->isToggled && action->isToggled())
        {
            g.setColour(t.active);
            g.fillEllipse(row.removeFromLeft(14).toFloat().withSizeKeepingCentre(6.0f, 6.0f));
        }
        g.setFont(ui::fonts::font(t.textSm, ui::fonts::Weight::Medium));
        g.setColour(enabled ? t.textPrimary : t.textTertiary);
        const auto name = juce::String::fromUTF8(item.name.c_str());
        const int nameWidth = static_cast<int>(juce::GlyphArrangement::getStringWidth(g.getCurrentFont(), name)) + 12;
        g.drawText(name, row.removeFromLeft(nameWidth), juce::Justification::centredLeft, false);
        g.setColour(t.textTertiary);
        g.setFont(ui::fonts::font(t.textXs));
        g.drawText(juce::String::fromUTF8(item.category.c_str()), row, juce::Justification::centredLeft, true);
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
