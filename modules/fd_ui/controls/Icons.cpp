namespace fd::ui
{
namespace
{
constexpr float kStroke = 1.5f;

/// Icons are defined on a 16 x 16 grid; `filled` paths are filled, the rest stroked.
struct Glyph
{
    juce::Path path;
    bool filled = false;
};

Glyph glyph(Icon icon)
{
    Glyph g;
    auto& p = g.path;
    switch (icon)
    {
    case Icon::Play:
        p.addTriangle(4.5f, 3.0f, 4.5f, 13.0f, 13.0f, 8.0f);
        g.filled = true;
        break;
    case Icon::Stop:
        p.addRectangle(4.0f, 4.0f, 8.0f, 8.0f);
        g.filled = true;
        break;
    case Icon::Record:
        p.addEllipse(3.5f, 3.5f, 9.0f, 9.0f);
        g.filled = true;
        break;
    case Icon::Loop:
        // Two arcs with arrow heads: a cycle.
        p.addCentredArc(8.0f, 8.0f, 5.0f, 5.0f, 0.0f, -2.4f, 0.9f, true);
        p.startNewSubPath(12.6f, 7.0f);
        p.lineTo(12.2f, 9.9f);
        p.lineTo(9.6f, 8.6f);
        p.addCentredArc(8.0f, 8.0f, 5.0f, 5.0f, 0.0f, 3.14159f - 2.4f + 3.14159f, 3.14159f + 0.9f + 3.14159f, true);
        break;
    case Icon::Metronome:
        p.startNewSubPath(5.5f, 13.5f);
        p.lineTo(7.0f, 2.5f);
        p.lineTo(9.0f, 2.5f);
        p.lineTo(10.5f, 13.5f);
        p.closeSubPath();
        p.startNewSubPath(8.0f, 11.0f);
        p.lineTo(12.5f, 5.0f);
        break;
    case Icon::PanelLeft:
    case Icon::PanelRight:
    case Icon::PanelBottom:
        p.addRoundedRectangle(2.0f, 3.0f, 12.0f, 10.0f, 1.5f);
        if (icon == Icon::PanelLeft)
        {
            p.startNewSubPath(6.0f, 3.0f);
            p.lineTo(6.0f, 13.0f);
        }
        else if (icon == Icon::PanelRight)
        {
            p.startNewSubPath(10.0f, 3.0f);
            p.lineTo(10.0f, 13.0f);
        }
        else
        {
            p.startNewSubPath(2.0f, 9.5f);
            p.lineTo(14.0f, 9.5f);
        }
        break;
    case Icon::Plus:
        p.startNewSubPath(8.0f, 3.0f);
        p.lineTo(8.0f, 13.0f);
        p.startNewSubPath(3.0f, 8.0f);
        p.lineTo(13.0f, 8.0f);
        break;
    case Icon::Settings:
        // Three sliders.
        for (int i = 0; i < 3; ++i)
        {
            const float y = 4.0f + 4.0f * static_cast<float>(i);
            p.startNewSubPath(2.5f, y);
            p.lineTo(13.5f, y);
            const float knob = i == 1 ? 10.0f : 5.5f;
            p.addEllipse(knob - 1.5f, y - 1.5f, 3.0f, 3.0f);
        }
        break;
    case Icon::Undo:
    case Icon::Redo:
    {
        p.startNewSubPath(4.0f, 6.0f);
        p.lineTo(10.0f, 6.0f);
        p.cubicTo(15.0f, 6.0f, 15.0f, 13.0f, 10.0f, 13.0f);
        p.lineTo(6.0f, 13.0f);
        p.startNewSubPath(6.5f, 3.5f);
        p.lineTo(4.0f, 6.0f);
        p.lineTo(6.5f, 8.5f);
        if (icon == Icon::Redo)
            p.applyTransform(juce::AffineTransform::scale(-1.0f, 1.0f, 8.0f, 8.0f));
        break;
    }
    }
    return g;
}
} // namespace

void drawIcon(juce::Graphics& g, Icon icon, juce::Rectangle<float> area, juce::Colour colour)
{
    const float size = std::min(area.getWidth(), area.getHeight());
    const float scale = size / 16.0f;
    const auto box = juce::Rectangle<float>(size, size).withCentre(area.getCentre());
    auto glyphData = glyph(icon);
    glyphData.path.applyTransform(juce::AffineTransform::scale(scale).translated(box.getX(), box.getY()));
    g.setColour(colour);
    if (glyphData.filled)
        g.fillPath(glyphData.path);
    else
        g.strokePath(glyphData.path, juce::PathStrokeType(kStroke * std::max(1.0f, scale * 0.9f),
                                                          juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

IconButton::IconButton(Icon icon, const juce::String& name) : juce::Button(name), icon_(icon) {}

void IconButton::setIcon(Icon icon)
{
    icon_ = icon;
    repaint();
}

void IconButton::paintButton(juce::Graphics& g, bool highlighted, bool down)
{
    const auto& t = Theme::current();
    const auto r = getLocalBounds().toFloat();
    const bool on = getToggleState();
    if (on)
    {
        g.setColour(down ? t.active.darker(0.15f) : t.active);
        g.fillRoundedRectangle(r, t.radiusControl);
    }
    else if (highlighted || down)
    {
        g.setColour(down ? t.surface3.brighter(0.08f) : t.surface3);
        g.fillRoundedRectangle(r, t.radiusControl);
    }
    const auto colour = on ? t.onActive : (isEnabled() ? (highlighted ? t.textPrimary : t.textSecondary) : t.textTertiary);
    drawIcon(g, icon_, r.reduced(r.getHeight() * 0.2f), colour);
}
} // namespace fd::ui
