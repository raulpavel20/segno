namespace fd::ui
{
namespace
{
/// The segno on a 16 x 16 grid: an S of two bowls joined by a diagonal spine, a slash crossing
/// the spine, and a dot on either side of the slash. The spine is tangent to both bowls and
/// passes through the centre, so the sign is symmetric under a half turn.
struct MarkGeometry
{
    juce::Path stroked;
    juce::Path dots;
};

constexpr float kMarkStroke = 1.6f;
// The sign's visible extent on the grid (stroke included), for placing it beside text.
constexpr float kMarkLeft = 2.9f, kMarkRight = 13.1f;

MarkGeometry markGeometry()
{
    MarkGeometry m;
    const auto deg = [](float d) { return juce::degreesToRadians(d); };
    m.stroked.addCentredArc(6.4f, 4.7f, 2.7f, 2.7f, 0.0f, deg(30.0f), deg(-163.28f), true);
    m.stroked.lineTo(10.377f, 8.714f); // the spine, through the centre
    m.stroked.addCentredArc(9.6f, 11.3f, 2.7f, 2.7f, 0.0f, deg(16.72f), deg(210.0f), false);
    m.stroked.startNewSubPath(5.0f, 15.4f); // the slash
    m.stroked.lineTo(11.0f, 0.6f);
    m.dots.addEllipse(10.5f, 5.1f, 2.0f, 2.0f);
    m.dots.addEllipse(3.5f, 8.9f, 2.0f, 2.0f);
    return m;
}

const juce::String& wordmarkText()
{
    static const juce::String text = juce::String(brand::name).toUpperCase();
    return text;
}

float markWidth(float height)
{
    return height * (kMarkRight - kMarkLeft) / 16.0f;
}

float gapAfterMark(float height)
{
    return std::round(height * 0.4f);
}
} // namespace

void paintMark(juce::Graphics& g, juce::Rectangle<float> area, juce::Colour colour)
{
    const float size = std::min(area.getWidth(), area.getHeight());
    const float scale = size / 16.0f;
    const auto box = juce::Rectangle<float>(size, size).withCentre(area.getCentre());
    auto m = markGeometry();
    const auto toArea = juce::AffineTransform::scale(scale).translated(box.getX(), box.getY());
    m.stroked.applyTransform(toArea);
    m.dots.applyTransform(toArea);
    g.setColour(colour);
    g.strokePath(m.stroked,
                 juce::PathStrokeType(kMarkStroke * scale, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.fillPath(m.dots);
}

int brandWidth(int height)
{
    const auto h = static_cast<float>(height);
    const float text = juce::GlyphArrangement::getStringWidth(font(TextStyle::Wordmark), wordmarkText());
    return static_cast<int>(std::ceil(markWidth(h) + gapAfterMark(h) + text));
}

void paintBrand(juce::Graphics& g, juce::Rectangle<int> area)
{
    const auto& t = Theme::current();
    const auto r = area.toFloat();
    const float h = r.getHeight();
    // The mark's square, shifted so the visible sign starts at the area's left edge.
    const auto square = juce::Rectangle<float>(r.getX() - h * kMarkLeft / 16.0f, r.getY(), h, h);
    paintMark(g, square, t.active);
    g.setColour(t.textPrimary);
    g.setFont(font(TextStyle::Wordmark));
    const float textX = r.getX() + markWidth(h) + gapAfterMark(h);
    g.drawText(wordmarkText(), juce::Rectangle<float>(textX, r.getY(), r.getRight() - textX, h),
               juce::Justification::centredLeft, false);
}

juce::Image appIcon(int size)
{
    const auto& t = Theme::current();
    juce::Image image(juce::Image::ARGB, size, size, true);
    juce::Graphics g(image);
    const auto s = static_cast<float>(size);
    const auto tile = juce::Rectangle<float>(s, s).reduced(s * 0.06f);
    g.setColour(t.surface2);
    g.fillRoundedRectangle(tile, tile.getWidth() * 0.22f);
    g.setColour(t.line);
    g.drawRoundedRectangle(tile.reduced(s * 0.004f), tile.getWidth() * 0.22f, std::max(1.0f, s * 0.008f));
    paintMark(g, tile.reduced(tile.getWidth() * 0.16f), t.active);
    return image;
}
} // namespace fd::ui
