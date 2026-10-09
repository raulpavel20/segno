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

ParamKnob::ParamKnob(DeviceUiContext& context, size_t paramIndex) : context_(context), index_(paramIndex)
{
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
    refresh();
}

void ParamKnob::refresh()
{
    const ParamSpec& spec = context_.info.params[index_];
    setValue(spec.toNormalized(context_.params.getPlain(index_)), juce::dontSendNotification);
}

ParamToggle::ParamToggle(DeviceUiContext& context, size_t paramIndex)
    : Toggle(juce::String(std::string(context.info.params[paramIndex].shortName.empty()
                                          ? context.info.params[paramIndex].name
                                          : context.info.params[paramIndex].shortName))),
      context_(context), index_(paramIndex)
{
    setTooltip(tooltipFor(context.info.params[paramIndex]));
    refresh();
}

void ParamToggle::refresh()
{
    setToggleState(context_.params.getPlain(index_) >= 0.5f, juce::dontSendNotification);
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
        addAndMakeVisible(b);
    }
    refresh();
}

void ParamSegmented::refresh()
{
    const int selected = static_cast<int>(context_.params.getPlain(index_));
    for (int i = 0; i < buttons_.size(); ++i)
        buttons_[i]->setToggleState(i == selected, juce::dontSendNotification);
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
}
} // namespace fd::ui
