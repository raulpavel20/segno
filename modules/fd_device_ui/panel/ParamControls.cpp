namespace fd::ui
{
namespace
{
/// A control's tooltip: the parameter's full name and what it does (its description, B22).
juce::String tooltipFor(const ParamSpec& spec)
{
    const juce::String name(std::string(spec.name));
    return spec.description.empty() ? name : name + ": " + juce::String(std::string(spec.description));
}
} // namespace

void showParamMenu(DeviceUiContext& context, size_t index, juce::Component& control)
{
    auto& params = context.params;
    juce::Component::SafePointer<juce::Component> alive(&control); // the menu outlives a closed panel
    auto whileAlive = [alive](std::function<void()> action) {
        return [safe = alive, run = std::move(action)] {
            if (safe != nullptr)
                run();
        };
    };
    juce::PopupMenu menu;
    if (params.canAutomate(index))
    {
        params.addAutomationItems(index, menu); // they act through the session, not the panel
        if (menu.getNumItems() > 0)
            menu.addSeparator();
    }
    const auto def = context.info.params[index].def;
    menu.addItem("Reset to default", whileAlive([&params, index, def] {
                     params.beginGesture(index);
                     params.setPlain(index, def);
                     params.endGesture(index);
                 }));
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&control));
}

ParamKnob::ParamKnob(DeviceUiContext& context, size_t paramIndex) : context_(context), index_(paramIndex)
{
    bind(paramIndex);
}

void ParamKnob::bind(size_t paramIndex)
{
    index_ = paramIndex;
    const ParamSpec& spec = context_.info.params[index_];
    setName(juce::String(std::string(spec.shortName.empty() ? spec.name : spec.shortName)));
    setTooltip(tooltipFor(spec));
    setDefaultValue(spec.toNormalized(spec.def));
    setNumSteps(spec.numSteps());
    // Bipolar arcs (from the centre) for ranges around zero: pan, bias, and symmetric gains
    // such as +-12 dB. Asymmetric dB ranges (e.g. -70..+35) fill from the minimum.
    const bool symmetric = std::abs(spec.min + spec.max) < 1e-6f;
    const bool bipolar = spec.min < 0.0f && spec.max > 0.0f && (spec.unit != Unit::Decibels || symmetric);
    setArcOrigin(bipolar ? spec.toNormalized(0.0f) : 0.0f);

    textFromValue = [&spec](float n) { return juce::String(spec.toText(spec.fromNormalized(n))); };
    valueFromText = [&spec](const juce::String& text) -> std::optional<float> {
        if (auto v = spec.fromText(text.toStdString()))
            return spec.toNormalized(*v);
        return std::nullopt;
    };
    onValueChange = [this, &spec](float n) { context_.params.setPlain(index_, spec.fromNormalized(n)); };
    onGestureStart = [this] { context_.params.beginGesture(index_); };
    onGestureEnd = [this] { context_.params.endGesture(index_); };
    onPopupMenu = [this] { showParamMenu(context_, index_, *this); };
    refresh();
    repaint(); // the name and value text, where the value hasn't moved
}

void ParamKnob::follow(std::function<size_t()> which)
{
    which_ = std::move(which);
    refresh();
}

void ParamKnob::refresh()
{
    if (const size_t now = which_ ? which_() : index_; now != index_)
    {
        bind(now); // which refreshes
        return;
    }
    const ParamSpec& spec = context_.info.params[index_];
    setValue(spec.toNormalized(context_.params.getPlain(index_)), juce::dontSendNotification);
    setAutomationState(context_.params.automationState(index_));
}

ParamToggle::ParamToggle(DeviceUiContext& context, size_t paramIndex)
    : Toggle(juce::String(std::string(context.info.params[paramIndex].shortName.empty()
                                          ? context.info.params[paramIndex].name
                                          : context.info.params[paramIndex].shortName))),
      context_(context), index_(paramIndex)
{
    setTooltip(tooltipFor(context.info.params[paramIndex]));
    onPopupMenu = [this] { showParamMenu(context_, index_, *this); };
    refresh();
}

void ParamToggle::refresh()
{
    setToggleState(context_.params.getPlain(index_) >= 0.5f, juce::dontSendNotification);
    setAutomationState(context_.params.automationState(index_));
}

void ParamToggle::clicked()
{
    context_.params.beginGesture(index_);
    context_.params.setPlain(index_, getToggleState() ? 1.0f : 0.0f);
    context_.params.endGesture(index_);
}

ParamSegmented::ParamSegmented(DeviceUiContext& context, size_t paramIndex, int columns)
    : context_(context), index_(paramIndex), columns_(std::max(1, columns))
{
    const ParamSpec& spec = context_.info.params[index_];
    constexpr int groupId = 1; // radio groups are scoped to siblings, i.e. this component
    for (size_t i = 0; i < spec.choices.size(); ++i)
    {
        auto* b = buttons_.add(new Toggle(juce::String(std::string(spec.choices[i]))));
        b->setRadioGroupId(groupId);
        b->setTooltip(tooltipFor(spec));
        b->onClick = [this, i] {
            context_.params.beginGesture(index_);
            context_.params.setPlain(index_, static_cast<float>(i));
            context_.params.endGesture(index_);
        };
        b->onPopupMenu = [this] { showParamMenu(context_, index_, *this); };
        addAndMakeVisible(b);
    }
    refresh();
}

void ParamSegmented::refresh()
{
    const int selected = static_cast<int>(context_.params.getPlain(index_));
    for (int i = 0; i < buttons_.size(); ++i)
        buttons_[i]->setToggleState(i == selected, juce::dontSendNotification);
    if (const auto state = context_.params.automationState(index_); state != automation_)
    {
        automation_ = state;
        repaint();
    }
}

void ParamSegmented::paintOverChildren(juce::Graphics& g)
{
    paintAutomationIndicator(g, {3.5f, 3.5f}, automation_);
}

void ParamSegmented::resized()
{
    // The grid's own gaps (docs/design/device-panels.md): segments fill whole cells when the
    // span is a multiple of the buttons per row, and each line of buttons is one row tall.
    const auto& grid = Theme::current().deviceGrid;
    const int rows = (buttons_.size() + columns_ - 1) / columns_;
    const int w = (getWidth() - grid.columnGap * (columns_ - 1)) / columns_;
    const int h = (getHeight() - grid.rowGap * (rows - 1)) / std::max(1, rows);
    for (int i = 0; i < buttons_.size(); ++i)
        buttons_[i]->setBounds((i % columns_) * (w + grid.columnGap), (i / columns_) * (h + grid.rowGap), w, h);
}

ParamComboBox::ParamComboBox(DeviceUiContext& context, size_t paramIndex) : context_(context), index_(paramIndex)
{
    const ParamSpec& spec = context_.info.params[index_];
    for (size_t i = 0; i < spec.choices.size(); ++i)
        addItem(juce::String(std::string(spec.choices[i])), static_cast<int>(i) + 1);
    setTooltip(tooltipFor(spec));
    onChange = [this] {
        context_.params.beginGesture(index_);
        context_.params.setPlain(index_, static_cast<float>(getSelectedId() - 1));
        context_.params.endGesture(index_);
    };
    refresh();
}

void ParamComboBox::refresh()
{
    setSelectedId(static_cast<int>(context_.params.getPlain(index_)) + 1, juce::dontSendNotification);
    if (const auto state = context_.params.automationState(index_); state != automation_)
    {
        automation_ = state;
        repaint();
    }
}

void ParamComboBox::paintOverChildren(juce::Graphics& g)
{
    paintAutomationIndicator(g, {3.5f, 3.5f}, automation_);
}

void ParamComboBox::showPopup()
{
    if (!menuText_)
    {
        juce::ComboBox::showPopup();
        return;
    }
    juce::PopupMenu menu;
    menu.setLookAndFeel(&getLookAndFeel());
    for (int i = 0; i < getNumItems(); ++i)
        menu.addItem(i + 1, menuText_(i), true, getSelectedId() == i + 1);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this).withMinimumWidth(getWidth()),
                       [safe = juce::Component::SafePointer<ParamComboBox>(this)](int chosen)
                       {
                           if (safe != nullptr && chosen > 0)
                               safe->setSelectedId(chosen, juce::sendNotificationSync);
                       });
}

// ---- the latency badge --------------------------------------------------------------------

LatencyBadge::LatencyBadge(juce::String moduleName, std::function<double()> latencyMs)
    : moduleName_(std::move(moduleName)), latencyMs_(std::move(latencyMs))
{
    setName("latency");
    refresh();
}

void LatencyBadge::refresh()
{
    const int ms = latencyMs_ ? juce::roundToInt(latencyMs_()) : 0;
    const auto text = ms > 0 ? "+" + juce::String(ms) + " ms" : juce::String();
    if (text == text_)
        return;
    text_ = text;
    const auto delay = juce::String(ms) + " ms";
    setTooltip(text_.isEmpty()
                   ? juce::String()
                   : "This " + moduleName_ + " delays its track by " + delay +
                         ". The DAW keeps the tracks in time; what you play live on this track is "
                         "heard " +
                         delay + " late.");
    repaint();
    if (onChange)
        onChange();
}

int LatencyBadge::preferredWidth() const
{
    if (text_.isEmpty())
        return 0;
    const auto& grid = Theme::current().deviceGrid;
    return static_cast<int>(
               std::ceil(juce::GlyphArrangement::getStringWidth(font(TextStyle::Control), text_))) +
           2 * grid.displayInset;
}

void LatencyBadge::paint(juce::Graphics& g)
{
    if (text_.isEmpty())
        return;
    const auto& t = Theme::current();
    const auto r = getLocalBounds().toFloat();
    g.setColour(t.roles.warningFill);
    g.fillRoundedRectangle(r, t.radiusControl);
    g.setColour(t.roles.warning);
    g.setFont(font(TextStyle::Control));
    g.drawText(text_, getLocalBounds(), juce::Justification::centred, false);
}
} // namespace fd::ui
