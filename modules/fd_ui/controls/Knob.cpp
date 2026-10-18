namespace fd::ui
{
namespace
{
// JUCE arc angles: 0 = 12 o'clock, clockwise. The arc runs from 7:30 to 4:30 (270 degrees).
constexpr float kStartAngle = -juce::MathConstants<float>::pi * 0.75f;
constexpr float kSweep = juce::MathConstants<float>::pi * 1.5f;

float angleFor(float normalized)
{
    return kStartAngle + kSweep * normalized;
}
} // namespace

Knob::Knob()
{
    setWantsKeyboardFocus(true);
    setMouseCursor(juce::MouseCursor::UpDownResizeCursor);
}

void Knob::setStyle(Style style)
{
    style_ = style;
    setMouseCursor(style == Style::Bar ? juce::MouseCursor::UpDownLeftRightResizeCursor
                                       : juce::MouseCursor::UpDownResizeCursor);
    resized();
    repaint();
}

void Knob::setName(const juce::String& name)
{
    name_ = name;
    Component::setName(name);
    repaint();
}

void Knob::setValue(float normalized, juce::NotificationType notification)
{
    normalized = quantize(juce::jlimit(0.0f, 1.0f, normalized));
    if (juce::approximatelyEqual(normalized, value_))
        return;
    value_ = normalized;
    repaint();
    if (notification != juce::dontSendNotification && onValueChange)
        onValueChange(value_);
}

float Knob::quantize(float normalized) const noexcept
{
    if (steps_ <= 1)
        return normalized;
    const float n = static_cast<float>(steps_ - 1);
    return std::round(normalized * n) / n;
}

void Knob::changeTo(float normalized)
{
    setValue(normalized, juce::sendNotificationSync);
}

void Knob::resized()
{
    fit_.width = -1.0f; // refit the value text
    if (editor_ == nullptr)
        return;
    if (style_ == Style::Bar)
    {
        editor_->setBounds(getLocalBounds());
        return;
    }
    // Over the value strip of the knob block (see paint()).
    const auto& grid = Theme::current().deviceGrid;
    auto block = getLocalBounds();
    if (block.getHeight() > grid.knobHeight())
        block = block.withSizeKeepingCentre(block.getWidth(), grid.knobHeight());
    editor_->setBounds(block.removeFromBottom(grid.knobValue + 2).reduced(2, 0));
}

void Knob::setAutomationState(AutomationState state)
{
    if (state == automation_)
        return;
    automation_ = state;
    repaint();
}

void Knob::paint(juce::Graphics& g)
{
    if (style_ == Style::Bar || style_ == Style::Fader)
    {
        style_ == Style::Bar ? paintBar(g) : paintFader(g);
        // A bar or fader: the indicator at its top-left corner.
        paintAutomationIndicator(g, {3.5f, 3.5f}, automation_);
        return;
    }
    const auto& t = Theme::current();
    const auto k = geometry();
    const auto nameArea = k.name, valueArea = k.value;

    const bool hot = isMouseOverOrDragging() || hasKeyboardFocus(false);

    const auto nameFont = fonts::font(t.textXs, fonts::Weight::Medium);
    g.setFont(nameFont);
    g.setColour(hot ? t.textPrimary : t.textSecondary);
    if (sourceHandle_)
    {
        // A source (a macro): its name is its handle, lit while hovered or showing what it moves;
        // a name of your own is cut to fit, before its lock if it has one.
        const auto colour =
            handleLit_ || handleHovered_ ? t.effectEnd : (hot ? t.textPrimary : t.textSecondary);
        g.setColour(colour);
        if (locked_)
        {
            // The name and the lock centred together; the lock never cut.
            const float icon = nameFont.getHeight() * 0.8f, gap = static_cast<float>(t.shell.itemGap);
            const float text = std::min(juce::GlyphArrangement::getStringWidth(nameFont, name_),
                                        nameArea.getWidth() - icon - gap);
            auto both = nameArea.withSizeKeepingCentre(text + gap + icon, nameArea.getHeight());
            g.drawText(name_, both.removeFromLeft(text), juce::Justification::centredLeft, true);
            drawIcon(g, Icon::Lock, both.removeFromRight(icon).withSizeKeepingCentre(icon, icon), colour);
        }
        else
            g.drawText(name_, nameArea, juce::Justification::centred, true);
    }
    else
        g.drawText(name_, nameArea, juce::Justification::centred, false);
    if (automation_ != AutomationState::None) // left of its name
    {
        const float nameWidth = juce::GlyphArrangement::getStringWidth(nameFont, name_);
        paintAutomationIndicator(g, {std::max(nameArea.getX() + 3.0f, nameArea.getCentreX() - nameWidth * 0.5f - 7.0f),
                                     nameArea.getCentreY()},
                                 automation_);
    }

    const float thickness = k.thickness;
    const float radius = k.radius;
    const auto centre = k.centre;
    const auto stroke = juce::PathStrokeType(thickness, juce::PathStrokeType::curved, juce::PathStrokeType::butt);
    paintRing(g, k);

    juce::Path track;
    track.addCentredArc(centre.x, centre.y, radius, radius, 0.0f, kStartAngle, kStartAngle + kSweep, true);
    g.setColour(t.surface3);
    g.strokePath(track, stroke);

    const float a0 = angleFor(juce::jmin(origin_, value_));
    const float a1 = angleFor(juce::jmax(origin_, value_));
    if (a1 - a0 > 0.001f)
    {
        juce::Path arc;
        arc.addCentredArc(centre.x, centre.y, radius, radius, 0.0f, a0, a1, true);
        g.setColour(hot ? t.roles.valueHot : t.value);
        g.strokePath(arc, stroke);
    }

    // Needle from near the centre to the arc.
    const float angle = angleFor(value_);
    const auto dir = juce::Point<float>(std::sin(angle), -std::cos(angle));
    g.setColour(t.textPrimary);
    g.drawLine(juce::Line<float>(centre + dir * (radius * 0.18f), centre + dir * (radius - thickness * 0.5f)), 1.5f);

    if (editor_ == nullptr)
    {
        g.setFont(fonts::mono(valueFit(valueArea.getWidth()).size, fonts::Weight::Regular));
        g.setColour(hot ? t.textPrimary : t.roles.valueText);
        g.drawText(valueText(), valueArea, juce::Justification::centred, false);
    }
}

Knob::Geometry Knob::geometry() const
{
    // A fixed block, centred: name, arc, value (docs/design/device-panels.md). Every knob looks
    // the same whatever cell it gets.
    const auto& t = Theme::current();
    const auto& grid = t.deviceGrid;
    auto area = getLocalBounds().toFloat();
    if (area.getHeight() > static_cast<float>(grid.knobHeight()))
        area = area.withSizeKeepingCentre(area.getWidth(), static_cast<float>(grid.knobHeight()));
    Geometry k;
    k.name = area.removeFromTop(static_cast<float>(grid.knobName));
    k.value = area.removeFromBottom(static_cast<float>(grid.knobValue));
    const float size = juce::jmin(t.knobMaxDiameter, area.getWidth() - 4.0f, area.getHeight() - 2.0f);
    k.centre = area.getCentre();
    k.thickness = t.knobArcThickness;
    k.radius = (size - k.thickness) * 0.5f;
    return k;
}

void Knob::paintRing(juce::Graphics& g, const Geometry& k) const
{
    const auto& t = Theme::current();
    // Just outside the arc: `active` over the whole sweep where a dragged source would land,
    // else the range the modulation moves it over, in the modulation gradient.
    const float r = k.radius + k.thickness * 0.5f + t.knobRingGap + t.knobRingThickness * 0.5f;
    const auto stroke =
        juce::PathStrokeType(t.knobRingThickness, juce::PathStrokeType::curved, juce::PathStrokeType::butt);
    if (dropTarget_ || dropCandidate_)
    {
        // Where a dragged source lands (`active`), or could (faint).
        juce::Path all;
        all.addCentredArc(k.centre.x, k.centre.y, r, r, 0.0f, kStartAngle, kStartAngle + kSweep, true);
        g.setColour(dropTarget_ ? t.active : t.textTertiary);
        g.strokePath(all, stroke);
        return;
    }
    if (!ring_ || ring_->to - ring_->from < 0.001f)
        return;
    const float a0 = angleFor(juce::jlimit(0.0f, 1.0f, ring_->from));
    const float a1 = angleFor(juce::jlimit(0.0f, 1.0f, ring_->to));
    juce::Path arc;
    arc.addCentredArc(k.centre.x, k.centre.y, r, r, 0.0f, a0, a1, true);
    const auto at = [&](float a) { return k.centre + juce::Point<float>(std::sin(a), -std::cos(a)) * r; };
    juce::ColourGradient gradient(t.effectStart, at(a0), t.effectEnd, at(a1), false);
    g.setGradientFill(gradient);
    g.setOpacity(ring_->dimmed ? t.roles.dimAlpha : 1.0f);
    g.strokePath(arc, stroke);
    g.setOpacity(1.0f);
    if (ring_->now)
    {
        // Where the modulation has it now (modulators.md §7.4): a dot on the ring.
        const float dot = 2.0f * ringDotRadius();
        g.setColour(ring_->dimmed ? t.textPrimary.withMultipliedAlpha(t.roles.dimAlpha) : t.textPrimary);
        g.fillEllipse(
            juce::Rectangle<float>(dot, dot).withCentre(at(angleFor(juce::jlimit(0.0f, 1.0f, *ring_->now)))));
    }
}

float Knob::ringDotRadius() noexcept
{
    return Theme::current().knobRingThickness * 0.5f + 0.5f;
}

void Knob::paintBarRing(juce::Graphics& g, juce::Rectangle<float> r) const
{
    // On a bar, modulation is a line along its bottom edge, inside it: `active` over its length
    // where a dragged source would land (faint where it could), else the range it moves over.
    const auto& t = Theme::current();
    const float thickness = t.knobRingThickness;
    const auto line = [&](float from, float to)
    {
        return juce::Rectangle<float>(r.getX() + r.getWidth() * from, r.getBottom() - thickness,
                                      r.getWidth() * (to - from), thickness);
    };
    juce::Graphics::ScopedSaveState saved(g);
    juce::Path shape;
    shape.addRoundedRectangle(r, t.radiusControl);
    g.reduceClipRegion(shape);
    if (dropTarget_ || dropCandidate_)
    {
        g.setColour(dropTarget_ ? t.active : t.textTertiary);
        g.fillRect(line(0.0f, 1.0f));
        return;
    }
    if (!ring_)
        return;
    const float from = juce::jlimit(0.0f, 1.0f, ring_->from), to = juce::jlimit(0.0f, 1.0f, ring_->to);
    if (to - from >= 0.001f)
    {
        const auto band = line(from, to);
        g.setGradientFill(
            juce::ColourGradient(t.effectStart, band.getTopLeft(), t.effectEnd, band.getTopRight(), false));
        g.setOpacity(ring_->dimmed ? t.roles.dimAlpha : 1.0f);
        g.fillRect(band);
        g.setOpacity(1.0f);
    }
    if (ring_->now)
    {
        // Where it is now: a short bright stretch of the line, clear of the bar's text.
        const float x = r.getX() + r.getWidth() * juce::jlimit(0.0f, 1.0f, *ring_->now);
        const float length = 4.0f * thickness;
        g.setColour(ring_->dimmed ? t.textPrimary.withMultipliedAlpha(t.roles.dimAlpha) : t.textPrimary);
        g.fillRect(juce::Rectangle<float>(juce::jlimit(r.getX(), r.getRight() - length, x - length * 0.5f),
                                          r.getBottom() - thickness, length, thickness));
    }
}

juce::Rectangle<float> Knob::ringBounds() const
{
    const auto& t = Theme::current();
    const auto k = geometry();
    // The ring's outer edge, or its now-dot's where that reaches further.
    const float outer = k.radius + k.thickness * 0.5f + t.knobRingGap + t.knobRingThickness * 0.5f +
                        std::max(t.knobRingThickness * 0.5f, ringDotRadius());
    return juce::Rectangle<float>(2.0f * outer, 2.0f * outer).withCentre(k.centre);
}

void Knob::setRing(std::optional<Ring> ring)
{
    if (ring != ring_)
    {
        ring_ = ring;
        repaint();
    }
}

void Knob::setDropTarget(bool target)
{
    if (target != dropTarget_)
    {
        dropTarget_ = target;
        repaint();
    }
}

void Knob::setDropCandidate(bool candidate)
{
    if (candidate != dropCandidate_)
    {
        dropCandidate_ = candidate;
        repaint();
    }
}

void Knob::setLocked(bool locked)
{
    if (locked == locked_)
        return;
    locked_ = locked;
    repaint();
}

void Knob::setSourceHandle(bool shown, bool lit)
{
    if (shown != sourceHandle_ || lit != handleLit_)
    {
        sourceHandle_ = shown;
        handleLit_ = lit;
        repaint();
    }
}

juce::Rectangle<float> Knob::handleArea() const
{
    return sourceHandle_ && style_ == Style::Rotary ? geometry().name : juce::Rectangle<float>();
}

bool Knob::isOnRing(juce::Point<float> position) const
{
    if (style_ != Style::Rotary || !ring_ || !onRingDrag)
        return false;
    const auto k = geometry();
    if (k.name.contains(position) || k.value.contains(position))
        return false;
    // The band from the arc's middle outwards: the ring, with room for a pointer.
    return position.getDistanceFrom(k.centre) >= k.radius;
}

void Knob::setHandleHovered(bool hovered)
{
    if (hovered != handleHovered_)
    {
        handleHovered_ = hovered;
        repaint();
        if (onHandleHover)
            onHandleHover(hovered);
    }
}

void Knob::mouseMove(const juce::MouseEvent& e)
{
    const bool onHandle = handleArea().contains(e.position);
    setHandleHovered(onHandle);
    setMouseCursor(onHandle                              ? hand_.get()
                   : !following_ && isOnRing(e.position) ? juce::MouseCursor::UpDownLeftRightResizeCursor
                                                         : juce::MouseCursor::NormalCursor);
}

void Knob::mouseExit(const juce::MouseEvent&)
{
    setHandleHovered(false);
    repaint();
}

juce::String Knob::valueText() const
{
    const auto text = textFromValue ? textFromValue(value_) : juce::String(value_, 2);
    return fit_.compact ? text.removeCharacters(" ") : text;
}

const Knob::ValueFit& Knob::valueFit(float width)
{
    if (fit_.width == width)
        return fit_;
    const auto& t = Theme::current();
    fit_ = {width, t.knobValueSize, false};
    if (!textFromValue || width <= 0.0f)
        return fit_;

    // Every knob's value is one size, so a panel reads evenly. The widest text over the range
    // decides, once per width: if it doesn't fit, drop the spaces before units; only then shrink.
    const int samples = steps_ > 1 ? steps_ : 65;
    const auto font = fonts::mono(t.knobValueSize, fonts::Weight::Regular);
    float widest = 0.0f, widestCompact = 0.0f;
    for (int i = 0; i < samples; ++i)
    {
        const auto text = textFromValue(static_cast<float>(i) / static_cast<float>(juce::jmax(1, samples - 1)));
        widest = juce::jmax(widest, juce::GlyphArrangement::getStringWidth(font, text));
        widestCompact = juce::jmax(widestCompact, juce::GlyphArrangement::getStringWidth(font, text.removeCharacters(" ")));
    }
    if (widest <= width + 0.5f) // the full column: the 4 px column gap keeps neighbours apart
        return fit_;
    fit_.compact = true;
    if (widestCompact > width)
        fit_.size = juce::jmax(9.0f, t.knobValueSize * width / widestCompact);
    return fit_;
}

void Knob::paintBar(juce::Graphics& g)
{
    const auto& t = Theme::current();
    const auto r = getLocalBounds().toFloat();
    const bool hot = isMouseOverOrDragging() || hasKeyboardFocus(false);

    g.setColour(t.surface3);
    g.fillRoundedRectangle(r, t.radiusControl);

    // Flat fill from the origin to the value (bipolar bars grow from the centre).
    const float x0 = r.getX() + r.getWidth() * juce::jmin(origin_, value_);
    const float x1 = r.getX() + r.getWidth() * juce::jmax(origin_, value_);
    if (x1 - x0 > 0.5f)
    {
        g.setColour((hot ? t.roles.valueHot : t.value).withAlpha(0.45f));
        g.saveState();
        g.reduceClipRegion(juce::Rectangle<float>(x0, r.getY(), x1 - x0, r.getHeight()).getSmallestIntegerContainer());
        g.fillRoundedRectangle(r, t.radiusControl);
        g.restoreState();
    }
    if (origin_ > 0.0f && origin_ < 1.0f) // centre marks on the edges, clear of the text
    {
        const float cx = r.getX() + r.getWidth() * origin_;
        g.setColour(t.textTertiary);
        g.fillRect(juce::Rectangle<float>(cx - 0.5f, r.getY(), 1.0f, 3.0f));
        g.fillRect(juce::Rectangle<float>(cx - 0.5f, r.getBottom() - 3.0f, 1.0f, 3.0f));
    }
    paintBarRing(g, r);

    if (editor_ != nullptr)
        return;
    const auto text = r.reduced(5.0f, 0.0f);
    g.setFont(fonts::font(t.textXs, fonts::Weight::Medium));
    g.setColour(hot ? t.textPrimary : t.textSecondary);
    g.drawText(name_, text, juce::Justification::centredLeft, false);
    g.setFont(fonts::mono(t.textXs, fonts::Weight::Regular));
    g.setColour(t.textPrimary);
    g.drawText(textFromValue ? textFromValue(value_) : juce::String(value_, 2), text,
               juce::Justification::centredRight, false);
}

float Knob::faderTravel() const noexcept
{
    return std::max(1.0f, static_cast<float>(getHeight()) - static_cast<float>(Theme::current().faderCapHeight));
}

void Knob::paintFader(juce::Graphics& g)
{
    const auto& t = Theme::current();
    const auto r = getLocalBounds().toFloat();
    const bool hot = isMouseOverOrDragging() || hasKeyboardFocus(false);
    const float cap = static_cast<float>(t.faderCapHeight);
    const float capY = r.getY() + (1.0f - value_) * faderTravel(); // the cap's top
    const float trackW = 4.0f;
    const auto track = juce::Rectangle<float>(r.getCentreX() - trackW * 0.5f, r.getY() + cap * 0.5f, trackW,
                                              faderTravel());
    g.setColour(t.surface3);
    g.fillRoundedRectangle(track, trackW * 0.5f);
    g.setColour(hot ? t.roles.valueHot : t.value);
    g.fillRoundedRectangle(track.withTop(capY + cap * 0.5f), trackW * 0.5f);
    const auto capArea = juce::Rectangle<float>(r.getX() + 2.0f, capY, r.getWidth() - 4.0f, cap);
    g.setColour(t.lift(t.surface3, hot, dragging_));
    g.fillRoundedRectangle(capArea, t.radiusControl);
    g.setColour(hot ? t.roles.valueHot : t.value);
    g.fillRect(capArea.withSizeKeepingCentre(capArea.getWidth() - 6.0f, 1.0f)); // the cap's mark
}

void Knob::mouseDown(const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu())
    {
        if (onPopupMenu)
            onPopupMenu();
        return;
    }
    grabKeyboardFocus();
    lastDragY_ = e.position.y;
    lastDragX_ = e.position.x;
    if (following_ && !handleArea().contains(e.position))
    {
        press_ = Press::None; // set by something else: nothing to drag
        return;
    }
    // A source's handle drags the source; the ring sets the modulation's amount.
    if (handleArea().contains(e.position))
    {
        press_ = Press::Handle;
        return;
    }
    if (isOnRing(e.position))
    {
        press_ = Press::Ring;
        if (onRingStart)
            onRingStart();
        return;
    }
    press_ = Press::Value;
    dragging_ = true;
    dragAccumulator_ = value_;
    if (onGestureStart)
        onGestureStart();
}

void Knob::mouseDrag(const juce::MouseEvent& e)
{
    if (press_ == Press::None)
        return;
    if (press_ == Press::Handle)
    {
        if (onHandleDrag)
            onHandleDrag(e);
        return;
    }
    if (press_ == Press::Ring)
    {
        const auto& input = Theme::current().input;
        const float delta = (lastDragY_ - e.position.y + e.position.x - lastDragX_) / input.valueTravel;
        lastDragY_ = e.position.y;
        lastDragX_ = e.position.x;
        if (onRingDrag)
            onRingDrag(delta * (e.mods.isShiftDown() ? input.fineFactor : 1.0f));
        return;
    }
    if (!dragging_)
        return;
    // Accumulate relative movement so pressing or releasing Shift mid-drag never jumps.
    const auto& input = Theme::current().input;
    float delta = (lastDragY_ - e.position.y) / (style_ == Style::Fader ? faderTravel() : input.valueTravel);
    if (style_ == Style::Bar)
        delta += (e.position.x - lastDragX_) / input.valueTravel;
    lastDragY_ = e.position.y;
    lastDragX_ = e.position.x;
    const float factor = e.mods.isShiftDown() ? input.fineFactor : 1.0f;
    dragAccumulator_ = juce::jlimit(0.0f, 1.0f, dragAccumulator_ + delta * factor);
    changeTo(dragAccumulator_);
}

void Knob::mouseUp(const juce::MouseEvent& e)
{
    const auto press = std::exchange(press_, Press::Value);
    if (press == Press::None)
        return;
    if (press == Press::Handle)
    {
        if (onHandleRelease)
            onHandleRelease(e);
        return;
    }
    if (press == Press::Ring)
    {
        if (onRingEnd)
            onRingEnd();
        return;
    }
    if (!dragging_)
        return;
    dragging_ = false;
    if (onGestureEnd)
        onGestureEnd();
}

void Knob::setFollowing(bool following)
{
    if (following == following_)
        return;
    following_ = following;
    if (following_ && editor_ != nullptr)
        editor_.reset();
    repaint();
}

void Knob::mouseDoubleClick(const juce::MouseEvent& e)
{
    if (handleArea().contains(e.position) && !e.mods.isPopupMenu())
    {
        if (onHandleDoubleClick)
            onHandleDoubleClick();
        return;
    }
    if (following_ || e.mods.isPopupMenu() || isOnRing(e.position))
        return;
    // The double-click's second press already began a gesture (ended on release): the reset is
    // part of it, so gestures never nest.
    const bool own = !dragging_;
    if (own && onGestureStart)
        onGestureStart();
    changeTo(default_);
    if (own && onGestureEnd)
        onGestureEnd();
}

void Knob::mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    const float step = steps_ > 1 ? 1.0f / static_cast<float>(steps_ - 1) : (e.mods.isShiftDown() ? 0.002f : 0.02f);
    const float direction = (wheel.deltaY != 0.0f ? wheel.deltaY : wheel.deltaX) * (wheel.isReversed ? -1.0f : 1.0f);
    if (direction == 0.0f || following_)
        return;
    if (onGestureStart)
        onGestureStart();
    changeTo(value_ + (direction > 0 ? step : -step));
    if (onGestureEnd)
        onGestureEnd();
}

bool Knob::keyPressed(const juce::KeyPress& key)
{
    if (following_)
        return false;
    const float step = steps_ > 1 ? 1.0f / static_cast<float>(steps_ - 1) : 0.01f;
    if (key == juce::KeyPress::upKey || key == juce::KeyPress::rightKey)
    {
        changeTo(value_ + step);
        return true;
    }
    if (key == juce::KeyPress::downKey || key == juce::KeyPress::leftKey)
    {
        changeTo(value_ - step);
        return true;
    }
    if (key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey)
    {
        changeTo(default_);
        return true;
    }
    const auto c = key.getTextCharacter();
    if (valueFromText && (juce::CharacterFunctions::isDigit(c) || c == '-' || c == '+' || c == '.'))
    {
        showEditor(juce::String::charToString(c));
        return true;
    }
    return false;
}

void Knob::showEditor(const juce::String& initialText)
{
    const auto& t = Theme::current();
    editor_ = std::make_unique<juce::TextEditor>();
    editor_->setJustification(juce::Justification::centred);
    editor_->setFont(fonts::mono(t.textSm, fonts::Weight::Regular));
    editor_->setIndents(2, 1);
    addAndMakeVisible(*editor_);
    resized();
    editor_->setText(initialText, false);
    editor_->moveCaretToEnd();
    takeFocus(*editor_); // opened by a double-click: that press doesn't end it

    auto finish = [this](bool commit) {
        if (editor_ == nullptr)
            return;
        const auto text = editor_->getText();
        juce::MessageManager::callAsync([safe = juce::Component::SafePointer<Knob>(this)] {
            if (safe != nullptr)
            {
                safe->editor_.reset();
                safe->repaint();
            }
        });
        if (commit && valueFromText)
            if (auto v = valueFromText(text))
            {
                if (onGestureStart)
                    onGestureStart();
                changeTo(*v);
                if (onGestureEnd)
                    onGestureEnd();
            }
    };
    editor_->onReturnKey = [finish] { finish(true); };
    editor_->onEscapeKey = [finish] { finish(false); };
    editor_->onFocusLost = [finish] { finish(true); };
    repaint();
}
} // namespace fd::ui
