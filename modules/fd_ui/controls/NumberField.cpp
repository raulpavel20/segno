namespace fd::ui
{
NumberField::NumberField()
{
    setWantsKeyboardFocus(true);
    setMouseCursor(juce::MouseCursor::UpDownResizeCursor);
}

void NumberField::setRange(double min, double max, double step) noexcept
{
    min_ = min;
    max_ = max;
    step_ = step;
    value_ = constrain(value_);
}

double NumberField::constrain(double v) const noexcept
{
    v = juce::jlimit(min_, max_, v);
    if (step_ > 0.0)
        v = min_ + std::round((v - min_) / step_) * step_;
    return juce::jlimit(min_, max_, v);
}

void NumberField::setValue(double value, juce::NotificationType notification)
{
    value = constrain(value);
    if (juce::approximatelyEqual(value, value_))
        return;
    value_ = value;
    repaint();
    if (notification != juce::dontSendNotification && onValueChange)
        onValueChange(value_);
}

void NumberField::change(double v)
{
    setValue(v, juce::sendNotificationSync);
}

juce::String NumberField::text() const
{
    return textFromValue ? textFromValue(value_) : juce::String(value_, 2);
}

void NumberField::paint(juce::Graphics& g)
{
    const auto& t = Theme::current();
    const bool hot = isMouseOverOrDragging() || hasKeyboardFocus(false);
    if (hot)
    {
        g.setColour(t.surface3);
        g.fillRoundedRectangle(getLocalBounds().toFloat(), t.radiusControl);
    }
    if (editor_ != nullptr)
        return;
    g.setColour(t.textPrimary);
    g.setFont(fonts::mono(fontSize_, fonts::Weight::Medium));
    g.drawText(text(), getLocalBounds().reduced(4, 0), juce::Justification::centredLeft, false);
}

void NumberField::resized()
{
    if (editor_ != nullptr)
        editor_->setBounds(getLocalBounds());
}

void NumberField::mouseDown(const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu())
        return;
    dragging_ = true;
    accumulator_ = value_;
    lastY_ = e.position.y;
    if (onGestureStart)
        onGestureStart();
}

void NumberField::mouseDrag(const juce::MouseEvent& e)
{
    if (!dragging_)
        return;
    const double delta = static_cast<double>(lastY_ - e.position.y) * perPixel_ * (e.mods.isShiftDown() ? 0.1 : 1.0);
    lastY_ = e.position.y;
    accumulator_ = juce::jlimit(min_, max_, accumulator_ + delta); // relative: Shift never jumps
    change(accumulator_);
}

void NumberField::mouseUp(const juce::MouseEvent&)
{
    if (!dragging_)
        return;
    dragging_ = false;
    if (onGestureEnd)
        onGestureEnd();
}

void NumberField::mouseDoubleClick(const juce::MouseEvent&)
{
    showEditor(text());
}

void NumberField::mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    const float direction = (wheel.deltaY != 0.0f ? wheel.deltaY : wheel.deltaX) * (wheel.isReversed ? -1.0f : 1.0f);
    if (direction == 0.0f)
        return;
    const double step = step_ > 0.0 ? step_ * (e.mods.isShiftDown() ? 1.0 : 10.0 * (step_ < 1.0 ? 1.0 : 0.1)) : perPixel_ * 4.0;
    if (onGestureStart)
        onGestureStart();
    change(value_ + (direction > 0.0f ? step : -step));
    if (onGestureEnd)
        onGestureEnd();
}

bool NumberField::keyPressed(const juce::KeyPress& key)
{
    const double step = step_ > 0.0 ? step_ : perPixel_;
    if (key == juce::KeyPress::upKey || key == juce::KeyPress::downKey)
    {
        if (onGestureStart)
            onGestureStart();
        change(value_ + (key == juce::KeyPress::upKey ? step : -step));
        if (onGestureEnd)
            onGestureEnd();
        return true;
    }
    const auto c = key.getTextCharacter();
    if (juce::CharacterFunctions::isDigit(c) || c == '.' || c == '-')
    {
        showEditor(juce::String::charToString(c));
        return true;
    }
    return false;
}

void NumberField::showEditor(const juce::String& initial)
{
    const auto& t = Theme::current();
    editor_ = std::make_unique<juce::TextEditor>();
    editor_->setFont(fonts::mono(fontSize_, fonts::Weight::Medium));
    editor_->setIndents(4, 0);
    editor_->setJustification(juce::Justification::centredLeft);
    editor_->setColour(juce::TextEditor::backgroundColourId, t.surface3);
    addAndMakeVisible(*editor_);
    resized();
    editor_->setText(initial, false);
    editor_->selectAll();
    if (initial.length() == 1)
        editor_->moveCaretToEnd();
    editor_->grabKeyboardFocus();

    auto finish = [this](bool commit) {
        if (editor_ == nullptr)
            return;
        const auto entered = editor_->getText();
        juce::MessageManager::callAsync([safe = juce::Component::SafePointer<NumberField>(this)] {
            if (safe != nullptr)
            {
                safe->editor_.reset();
                safe->repaint();
            }
        });
        std::optional<double> v = valueFromText ? valueFromText(entered) : std::nullopt;
        if (!valueFromText && entered.containsAnyOf("0123456789"))
            v = entered.getDoubleValue();
        if (commit && v)
        {
            if (onGestureStart)
                onGestureStart();
            change(*v);
            if (onGestureEnd)
                onGestureEnd();
        }
    };
    editor_->onReturnKey = [finish] { finish(true); };
    editor_->onEscapeKey = [finish] { finish(false); };
    editor_->onFocusLost = [finish] { finish(true); };
}
} // namespace fd::ui
