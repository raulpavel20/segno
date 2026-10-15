namespace fd::ui
{
void paintDisplayWell(juce::Graphics& g, juce::Rectangle<float> r)
{
    const auto& t = Theme::current();
    g.setColour(t.surface0);
    g.fillRoundedRectangle(r, t.radiusControl);
}

void paintDisplayHandle(juce::Graphics& g, juce::Point<float> at, bool hot)
{
    const auto& t = Theme::current();
    const auto size = static_cast<float>(t.deviceGrid.displayHandle);
    const auto r = juce::Rectangle<float>(size, size).withCentre(at);
    g.setColour(hot ? t.active : t.surface3);
    g.fillEllipse(r);
    g.setColour(hot ? t.active : t.value);
    g.drawEllipse(r.reduced(0.5f), 1.0f);
}

namespace
{
juce::Point<float> pointOn(const std::array<float, 3>& p, juce::Rectangle<float> r)
{
    return {r.getX() + p[0] * r.getWidth(), r.getBottom() - p[1] * r.getHeight()};
}

/// The curve's line across `r`, from the table the audio plays.
juce::Path curveLine(const dsp::DrawnCurve& c, juce::Rectangle<float> r)
{
    const auto table = c.table();
    juce::Path p;
    const auto n = table.size() - 1;
    for (size_t i = 0; i <= n; ++i)
    {
        const juce::Point<float> pt(r.getX() + r.getWidth() * static_cast<float>(i) / static_cast<float>(n),
                                    r.getBottom() - table[i] * r.getHeight());
        i == 0 ? p.startNewSubPath(pt) : p.lineTo(pt);
    }
    return p;
}
} // namespace

void paintDrawnCurve(juce::Graphics& g, const dsp::DrawnCurve& c, juce::Rectangle<float> r, bool handles,
                     int hot)
{
    const auto& t = Theme::current();
    const auto line = curveLine(c, r);
    auto fill = line;
    fill.lineTo(r.getBottomRight());
    fill.lineTo(r.getBottomLeft());
    fill.closeSubPath();
    g.setColour(t.fill(t.value));
    g.fillPath(fill);
    g.setColour(t.value);
    g.strokePath(line, juce::PathStrokeType(1.5f, juce::PathStrokeType::curved));
    if (handles)
        for (size_t i = 0; i < c.points.size(); ++i)
            paintDisplayHandle(g, pointOn(c.points[i], r), static_cast<int>(i) == hot);
}

namespace curves
{
int pointNear(const std::vector<std::array<float, 3>>& points, juce::Rectangle<float> r,
              juce::Point<float> at)
{
    const float reach = static_cast<float>(Theme::current().deviceGrid.displayHandle);
    int best = -1;
    float nearest = reach;
    for (size_t i = 0; i < points.size(); ++i)
        if (const float d = pointOn(points[i], r).getDistanceFrom(at); d <= nearest)
        {
            nearest = d;
            best = static_cast<int>(i);
        }
    return best;
}

void movePoint(std::vector<std::array<float, 3>>& points, size_t i, juce::Rectangle<float> r,
               juce::Point<float> at)
{
    float x = (at.x - r.getX()) / r.getWidth();
    const float lo = i == 0 ? 0.0f : points[i - 1][0], hi = i + 1 == points.size() ? 1.0f : points[i + 1][0];
    x = i == 0 ? 0.0f : (i + 1 == points.size() ? 1.0f : std::clamp(x, lo, hi));
    points[i][0] = x;
    points[i][1] = std::clamp((r.getBottom() - at.y) / r.getHeight(), 0.0f, 1.0f);
}

void addOrRemove(dsp::DrawnCurve& c, juce::Rectangle<float> r, juce::Point<float> at)
{
    c.points = dsp::editablePoints(c);
    if (const int i = pointNear(c.points, r, at); i > 0 && static_cast<size_t>(i) + 1 < c.points.size())
    {
        c.points.erase(c.points.begin() + i);
        return;
    }
    const float x = std::clamp((at.x - r.getX()) / r.getWidth(), 0.0f, 1.0f);
    const float y = dsp::lookup(c.table(), x);
    const auto it = std::upper_bound(c.points.begin(), c.points.end(), x,
                                     [](float v, const std::array<float, 3>& p) { return v < p[0]; });
    c.points.insert(it, {x, y, 0.0f});
}

juce::String shapeName(std::string_view id)
{
    auto name = juce::String(std::string(id)).replaceCharacter('-', ' ');
    return name.substring(0, 1).toUpperCase() + name.substring(1);
}
} // namespace curves

CurveEditor::CurveEditor()
{
    shapes_.setTooltip("A shape to start from");
    shapes_.onClick = [this] { showShapes(); };
    addAndMakeVisible(shapes_);
    setTooltip("Drag a point; double-click to add or remove one; drag a segment up or down to bend it");
    setMouseCursor(juce::MouseCursor::CrosshairCursor);
}

CurveEditor::~CurveEditor() = default;

void CurveEditor::setAccessory(juce::Component* accessory, int width)
{
    if (accessory_ != nullptr && accessory_ != accessory)
        removeChildComponent(accessory_);
    accessory_ = accessory;
    accessoryWidth_ = width;
    if (accessory_ != nullptr)
        addAndMakeVisible(*accessory_);
    resized();
}

void CurveEditor::setEditable(bool editable)
{
    editable_ = editable;
    shapes_.setVisible(editable && chrome_);
    setMouseCursor(editable ? juce::MouseCursor::CrosshairCursor : juce::MouseCursor::NormalCursor);
    lastState_.clear();
    refresh();
    repaint();
}

void CurveEditor::setChrome(bool chrome, juce::Rectangle<float> plot)
{
    chrome_ = chrome;
    plot_ = plot;
    shapes_.setVisible(chrome && editable_);
    resized();
    repaint();
}

void CurveEditor::resized()
{
    // The accessory, a chip, in the top-right corner; the shapes' menu at the left.
    const auto& grid = Theme::current().deviceGrid;
    auto top = getLocalBounds().reduced(grid.displayInset).removeFromTop(grid.sourceHandle);
    if (accessory_ != nullptr)
        accessory_->setBounds(top.removeFromRight(accessoryWidth_));
    shapes_.setBounds(top.removeFromLeft(grid.sourceHandle));
}

juce::Rectangle<float> CurveEditor::plot() const
{
    const auto& grid = Theme::current().deviceGrid;
    if (!chrome_)
        return plot_.isEmpty() ? getLocalBounds().toFloat() : plot_;
    return getLocalBounds()
        .toFloat()
        .reduced(static_cast<float>(grid.displayInset) + static_cast<float>(grid.displayHandle) * 0.5f)
        .withTrimmedTop(static_cast<float>(grid.sourceHandle));
}

dsp::DrawnCurve CurveEditor::current() const
{
    return curve ? curve() : dsp::DrawnCurve{};
}

void CurveEditor::refresh()
{
    // What it draws: the curve, and where it plays while it does.
    const auto c = current();
    std::string state = c.shape;
    for (const auto& p : c.points)
        for (const float v : p)
            state += "," + std::to_string(v);
    phase_ = phase ? phase() : -1.0f;
    state += "@" + std::to_string(juce::roundToInt(phase_ * 500.0f));
    if (state != lastState_)
    {
        lastState_ = state;
        repaint();
    }
}

void CurveEditor::paint(juce::Graphics& g)
{
    if (chrome_)
        paintDisplayWell(g, getLocalBounds().toFloat());
    paintContent(g, plot());
}

void CurveEditor::paintContent(juce::Graphics& g, juce::Rectangle<float> r)
{
    const auto& t = Theme::current();
    const bool editing = dragged_ >= 0 || bending_ >= 0;
    const auto c = editing ? editing_ : current();
    paintDrawnCurve(g, c, r, editable_ && !c.points.empty(), dragged_);
    if (phase_ >= 0.0f)
    {
        // Where it is now: a line through the curve, and a dot on it.
        const float x = r.getX() + phase_ * r.getWidth();
        g.setColour(t.textSecondary);
        g.drawVerticalLine(juce::roundToInt(x), r.getY(), r.getBottom());
        const float y = r.getBottom() - dsp::lookup(c.table(), phase_) * r.getHeight();
        const float dot = static_cast<float>(t.deviceGrid.displayHandle) * 0.5f;
        g.setColour(t.textPrimary);
        g.fillEllipse(juce::Rectangle<float>(dot, dot).withCentre({x, y}));
    }
}

void CurveEditor::mouseDown(const juce::MouseEvent& e)
{
    if (!editable_)
        return;
    const auto c = current();
    editing_ = dsp::DrawnCurve{c.shape, dsp::editablePoints(c)};
    dragged_ = curves::pointNear(editing_.points, plot(), e.position);
    bending_ = -1;
    if (dragged_ < 0)
    {
        // Between two points: bend that segment.
        const float x = (e.position.x - plot().getX()) / plot().getWidth();
        for (size_t k = 0; k + 1 < editing_.points.size(); ++k)
            if (x >= editing_.points[k][0] && x <= editing_.points[k + 1][0])
            {
                bending_ = static_cast<int>(k);
                bendStart_ = editing_.points[k][2];
                bendY_ = e.position.y;
                break;
            }
    }
    repaint();
}

void CurveEditor::mouseDrag(const juce::MouseEvent& e)
{
    if (dragged_ >= 0)
        curves::movePoint(editing_.points, static_cast<size_t>(dragged_), plot(), e.position);
    else if (bending_ >= 0)
    {
        // Up bulges the segment up, whichever way it goes.
        const auto& p = editing_.points;
        const auto k = static_cast<size_t>(bending_);
        const float way = p[k + 1][1] >= p[k][1] ? 1.0f : -1.0f;
        editing_.points[k][2] = std::clamp(
            bendStart_ + way * (bendY_ - e.position.y) / Theme::current().input.bendTravel, -1.0f, 1.0f);
    }
    repaint();
}

void CurveEditor::mouseUp(const juce::MouseEvent& e)
{
    if ((dragged_ >= 0 || bending_ >= 0) && e.mouseWasDraggedSinceMouseDown() && onChange)
        onChange(editing_); // one change for the whole drag
    dragged_ = bending_ = -1;
    lastState_.clear();
    refresh();
    repaint();
}

void CurveEditor::mouseDoubleClick(const juce::MouseEvent& e)
{
    if (!editable_ || !onChange)
        return;
    auto c = current();
    curves::addOrRemove(c, plot(), e.position);
    onChange(c);
}

void CurveEditor::showShapes()
{
    juce::PopupMenu menu;
    const auto named = dsp::namedCurveShapes();
    const auto c = current();
    for (size_t i = 0; i < named.size(); ++i)
        menu.addItem(static_cast<int>(i) + 1, curves::shapeName(named[i]), true,
                     c.points.empty() && c.shape == named[i]);
    juce::Component::SafePointer<CurveEditor> self(this);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&shapes_),
                       [self](int chosen)
                       {
                           if (self == nullptr || chosen <= 0 || !self->onChange)
                               return;
                           self->onChange(dsp::DrawnCurve{
                               std::string(dsp::namedCurveShapes()[static_cast<size_t>(chosen - 1)]), {}});
                       });
}
} // namespace fd::ui
