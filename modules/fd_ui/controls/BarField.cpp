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

int BarField::gap() const
{
    return label_.isEmpty() ? 0 : Theme::current().shell.itemGap; // no label: the field alone
}

void BarField::fit()
{
    setSize(labelWidth() + gap() + field_.widthFor(field_.text()), getHeight());
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
    field_.setBounds(getLocalBounds().withTrimmedLeft(labelWidth() + gap()));
}
} // namespace fd::ui
