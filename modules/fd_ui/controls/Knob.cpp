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
    auto area = getLocalBounds().toFloat();

    // A fixed block, centred: name, arc, value (docs/design/device-panels.md). Every knob looks
    // the same whatever cell it gets.
    const auto& grid = t.deviceGrid;
    if (area.getHeight() > static_cast<float>(grid.knobHeight()))
        area = area.withSizeKeepingCentre(area.getWidth(), static_cast<float>(grid.knobHeight()));
    const auto nameArea = area.removeFromTop(static_cast<float>(grid.knobName));
    const auto valueArea = area.removeFromBottom(static_cast<float>(grid.knobValue));
    const float size = juce::jmin(t.knobMaxDiameter, area.getWidth() - 4.0f, area.getHeight() - 2.0f);
    const auto knobArea = juce::Rectangle<float>(size, size).withCentre(area.getCentre());

    const bool hot = isMouseOverOrDragging() || hasKeyboardFocus(false);

    g.setFont(fonts::font(t.textXs, fonts::Weight::Medium));
    g.setColour(hot ? t.textPrimary : t.textSecondary);
    g.drawText(name_, nameArea, juce::Justification::centred, false);
    if (automation_ != AutomationState::None) // left of its name
    {
        const float nameWidth = juce::GlyphArrangement::getStringWidth(fonts::font(t.textXs, fonts::Weight::Medium), name_);
        paintAutomationIndicator(g, {std::max(nameArea.getX() + 3.0f, nameArea.getCentreX() - nameWidth * 0.5f - 7.0f),
                                     nameArea.getCentreY()},
                                 automation_);
    }

    const float thickness = t.knobArcThickness;
    const float radius = (size - thickness) * 0.5f;
    const auto centre = knobArea.getCentre();
    const auto stroke = juce::PathStrokeType(thickness, juce::PathStrokeType::curved, juce::PathStrokeType::butt);

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
    dragging_ = true;
    dragAccumulator_ = value_;
    lastDragY_ = e.position.y;
    lastDragX_ = e.position.x;
    if (onGestureStart)
        onGestureStart();
}

void Knob::mouseDrag(const juce::MouseEvent& e)
{
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

void Knob::mouseUp(const juce::MouseEvent&)
{
    if (!dragging_)
        return;
    dragging_ = false;
    if (onGestureEnd)
        onGestureEnd();
}

void Knob::mouseDoubleClick(const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu())
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
    if (direction == 0.0f)
        return;
    if (onGestureStart)
        onGestureStart();
    changeTo(value_ + (direction > 0 ? step : -step));
    if (onGestureEnd)
        onGestureEnd();
}

bool Knob::keyPressed(const juce::KeyPress& key)
{
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
