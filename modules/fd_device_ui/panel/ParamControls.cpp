namespace fd::ui
{
ParamKnob::ParamKnob(DeviceUiContext& context, size_t paramIndex) : context_(context), index_(paramIndex)
{
    const ParamSpec& spec = context_.info.params[index_];
    setName(juce::String(std::string(spec.shortName.empty() ? spec.name : spec.shortName)));
    setTooltip(juce::String(std::string(spec.name)));
    setDefaultValue(spec.toNormalized(spec.def));
    setNumSteps(spec.numSteps());
    const bool bipolar = spec.min < 0.0f && spec.max > 0.0f && spec.unit != Unit::Decibels;
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
    setTooltip(juce::String(std::string(context.info.params[paramIndex].name)));
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
} // namespace fd::ui
