namespace fd::ui
{
SearchField::SearchField(const juce::String& hint)
{
    const auto& t = Theme::current();
    setFont(font(TextStyle::Field));
    // The text after the magnifier, as a tree row's name follows its icon.
    setIndents(t.shell.fieldTextInset + t.shell.listIconSize + t.shell.itemGap, 0);
    setJustification(juce::Justification::centredLeft);
    setColour(juce::TextEditor::backgroundColourId, t.surface2);
    setColour(juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
    setColour(juce::TextEditor::focusedOutlineColourId, juce::Colours::transparentBlack);
    setHint(hint);
}

void SearchField::setHint(const juce::String& hint)
{
    setTextToShowWhenEmpty(hint, Theme::current().textTertiary);
}

juce::Rectangle<int> SearchField::iconArea() const
{
    const auto& s = Theme::current().shell;
    return juce::Rectangle<int>(s.fieldTextInset, 0, s.listIconSize, getHeight())
        .withSizeKeepingCentre(s.listIconSize, s.listIconSize);
}

void SearchField::paintOverChildren(juce::Graphics& g)
{
    juce::TextEditor::paintOverChildren(g); // the hint while empty
    drawIcon(g, Icon::Search, iconArea().toFloat(), Theme::current().textTertiary);
}
} // namespace fd::ui
