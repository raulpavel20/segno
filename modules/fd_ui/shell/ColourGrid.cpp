namespace fd::ui
{
ColourGrid::ColourGrid(int current, std::function<void(int)> chosen)
    : juce::PopupMenu::CustomComponent(false), current_(current), chosen_(std::move(chosen))
{
}

juce::Rectangle<int> ColourGrid::swatch(int index) const
{
    const auto& sh = Theme::current().shell;
    const int size = sh.rowHeight, step = size + sh.itemGap;
    return {sh.buttonTextPadding + (index % columns) * step, sh.buttonTextPadding + (index / columns) * step,
            size, size};
}

int ColourGrid::swatchAt(juce::Point<int> p) const
{
    for (int i = 0; i < Theme::current().clipColours.size(); ++i)
        if (swatch(i).contains(p))
            return i;
    return -1;
}

void ColourGrid::getIdealSize(int& width, int& height)
{
    const auto& sh = Theme::current().shell;
    const auto last = swatch(Theme::current().clipColours.size() - 1);
    width = swatch(columns - 1).getRight() + sh.buttonTextPadding;
    height = last.getBottom() + sh.buttonTextPadding;
}

void ColourGrid::paint(juce::Graphics& g)
{
    const auto& t = Theme::current();
    for (int i = 0; i < t.clipColours.size(); ++i)
    {
        const auto r = swatch(i).toFloat();
        g.setColour(t.lift(t.clipColour(i), i == hovered_, false));
        g.fillRoundedRectangle(r, t.radiusControl);
        if (i == current_) // ringed, inside it so neighbours never touch
        {
            g.setColour(t.textPrimary);
            g.drawRoundedRectangle(r.reduced(1.0f), t.radiusControl, 2.0f);
        }
    }
}

void ColourGrid::mouseMove(const juce::MouseEvent& e)
{
    if (const int at = swatchAt(e.getPosition()); at != hovered_)
    {
        hovered_ = at;
        repaint();
    }
}

void ColourGrid::mouseExit(const juce::MouseEvent&)
{
    hovered_ = -1;
    repaint();
}

void ColourGrid::mouseUp(const juce::MouseEvent& e)
{
    const int at = swatchAt(e.getPosition());
    if (at < 0)
        return;
    if (chosen_)
        chosen_(at);
    if (getParentComponent() != nullptr) // in a menu (its item holds it): close it
        triggerMenuItem();
}

void addColourGrid(juce::PopupMenu& menu, int current, std::function<void(int)> chosen)
{
    menu.addCustomItem(-1, std::make_unique<ColourGrid>(current, std::move(chosen)), nullptr, "Colour");
}

ColourSwatch::ColourSwatch()
{
    setMouseCursor(juce::MouseCursor::PointingHandCursor);
    setTooltip("Colour: click for the palette");
}

void ColourSwatch::setColour(int colour)
{
    if (colour == colour_)
        return;
    colour_ = colour;
    repaint();
}

void ColourSwatch::paint(juce::Graphics& g)
{
    const auto& t = Theme::current();
    const auto r = getLocalBounds().toFloat();
    if (hovered_)
    {
        g.setColour(t.roles.hoverWash);
        g.fillRoundedRectangle(r, t.radiusControl);
    }
    // A square swatch at the left.
    const auto square =
        r.withWidth(r.getHeight()).reduced(static_cast<float>(Theme::current().shell.propertyFieldInset));
    if (colour_ >= 0)
    {
        g.setColour(t.clipColour(colour_));
        g.fillRoundedRectangle(square, t.radiusControl);
    }
}

void ColourSwatch::mouseEnter(const juce::MouseEvent&)
{
    hovered_ = true;
    repaint();
}

void ColourSwatch::mouseExit(const juce::MouseEvent&)
{
    hovered_ = false;
    repaint();
}

void ColourSwatch::mouseUp(const juce::MouseEvent& e)
{
    if (!getLocalBounds().contains(e.getPosition()))
        return;
    juce::PopupMenu menu;
    juce::Component::SafePointer<ColourSwatch> self(this);
    addColourGrid(menu, colour_,
                  [self](int c)
                  {
                      if (self != nullptr && self->onChosen)
                          self->onChosen(c);
                  });
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this));
}
} // namespace fd::ui
