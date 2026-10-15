namespace fd::ui
{
BarField::BarField(const juce::String& label) : label_(label)
{
    addAndMakeVisible(field_);
}

int BarField::labelWidth() const
{
    return static_cast<int>(
        std::ceil(juce::GlyphArrangement::getStringWidth(font(TextStyle::Label), label_)));
}

void BarField::fit()
{
    setSize(labelWidth() + Theme::current().shell.itemGap + field_.widthFor(field_.text()), getHeight());
}

void BarField::setTooltip(const juce::String& tip)
{
    SettableTooltipClient::setTooltip(tip);
    field_.setTooltip(tip);
}

void BarField::paint(juce::Graphics& g)
{
    const auto& t = Theme::current();
    g.setColour(isEnabled() ? t.textSecondary : t.textSecondary.withMultipliedAlpha(t.roles.dimAlpha));
    g.setFont(font(TextStyle::Label));
    g.drawText(label_, getLocalBounds().removeFromLeft(labelWidth()), juce::Justification::centredLeft,
               false);
}

void BarField::resized()
{
    field_.setBounds(getLocalBounds().withTrimmedLeft(labelWidth() + Theme::current().shell.itemGap));
}
} // namespace fd::ui
