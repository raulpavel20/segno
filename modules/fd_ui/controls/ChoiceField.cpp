namespace fd::ui
{
ChoiceField::ChoiceField()
{
    setWantsKeyboardFocus(true);
}

void ChoiceField::setValue(const juce::String& value, bool placeholder)
{
    if (value == value_ && placeholder == placeholder_)
        return;
    value_ = value;
    placeholder_ = placeholder;
    repaint();
}

juce::Font ChoiceField::font() const
{
    return fonts::mono(fontSize_, fonts::Weight::Medium);
}

int ChoiceField::widthFor(const juce::String& text) const
{
    return static_cast<int>(std::ceil(juce::GlyphArrangement::getStringWidth(font(), text))) +
           2 * NumberField::textInset;
}

void ChoiceField::setOpen(bool open)
{
    if (open_ == open)
        return;
    open_ = open;
    repaint();
}

void ChoiceField::paint(juce::Graphics& g)
{
    const auto& t = Theme::current();
    if (open_ || isMouseOver() || hasKeyboardFocus(false))
    {
        g.setColour(t.surface3);
        g.fillRoundedRectangle(getLocalBounds().toFloat(), t.radiusControl);
    }
    g.setColour(placeholder_ ? t.textTertiary : t.textPrimary);
    g.setFont(font());
    // Cut short with an ellipsis rather than touching its edge.
    g.drawFittedText(value_, getLocalBounds().reduced(NumberField::textInset, 0),
                     juce::Justification::centredLeft, 1, 1.0f);
}

void ChoiceField::mouseDown(const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu())
    {
        if (onPopupMenu)
            onPopupMenu();
        return;
    }
    if (onOpen)
        onOpen();
}

bool ChoiceField::keyPressed(const juce::KeyPress& key)
{
    if (key == juce::KeyPress::returnKey || key == juce::KeyPress::spaceKey)
    {
        if (onOpen)
            onOpen();
        return true;
    }
    return false;
}
} // namespace fd::ui
