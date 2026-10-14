namespace fd::ui
{
MenuButton::MenuButton(const juce::String& text) : juce::Button(text)
{
    setWantsKeyboardFocus(false); // the view keeps its keys
    setTriggeredOnMouseDown(true); // a menu opens on the press, as the menu bar's do
}

int MenuButton::preferredWidth() const
{
    const auto& s = Theme::current().shell;
    const auto text = juce::GlyphArrangement::getStringWidth(font(TextStyle::Control), getButtonText());
    return s.buttonTextPadding + static_cast<int>(std::ceil(text)) + kGap + static_cast<int>(kChevron) + kChevronPadding;
}

void MenuButton::showMenu(juce::PopupMenu menu)
{
    open_ = true;
    repaint();
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this),
                       [safe = juce::Component::SafePointer(this)](int)
                       {
                           if (safe != nullptr)
                           {
                               safe->open_ = false;
                               safe->repaint();
                           }
                       });
}

void MenuButton::paintButton(juce::Graphics& g, bool highlighted, bool down)
{
    const auto& t = Theme::current();
    const auto bounds = getLocalBounds().toFloat();
    const bool pressed = down || open_;
    g.setColour(t.lift(t.surface3, highlighted, pressed));
    g.fillRoundedRectangle(bounds, t.radiusControl);

    const auto colour = highlighted || pressed ? t.textPrimary : t.textSecondary;
    auto area = bounds.withTrimmedLeft(static_cast<float>(t.shell.buttonTextPadding))
                    .withTrimmedRight(static_cast<float>(kChevronPadding));
    const auto chevron = area.removeFromRight(kChevron).withSizeKeepingCentre(kChevron, kChevron);
    area.removeFromRight(static_cast<float>(kGap));
    g.setColour(colour);
    g.setFont(font(TextStyle::Control));
    g.drawText(getButtonText(), area.toNearestInt(), juce::Justification::centredLeft, false);
    drawIcon(g, Icon::ChevronDown, chevron, colour);
}
} // namespace fd::ui
