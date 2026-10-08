namespace fd::ui
{
DevicePanel::DevicePanel(DeviceUiContext& context) : context_(context)
{
    power_.setToggleState(true, juce::dontSendNotification);
    power_.setTooltip("Device on/off");
    power_.onClick = [this] {
        if (onEnabledChange)
            onEnabledChange(power_.getToggleState());
    };
    addAndMakeVisible(power_);
    startTimerHz(30);
}

DevicePanel::~DevicePanel()
{
    stopTimer();
}

void DevicePanel::setEnabledState(bool enabled)
{
    power_.setToggleState(enabled, juce::dontSendNotification);
}

void DevicePanel::timerCallback()
{
    for (auto& c : controls_)
        if (auto* pc = dynamic_cast<ParamControl*>(c.get()))
            pc->refresh();
    updateDimming();
}

void DevicePanel::dimWhen(std::vector<juce::Component*> controls, std::function<bool()> inactive)
{
    dimming_.push_back({std::move(controls), std::move(inactive)});
    updateDimming();
}

void DevicePanel::updateDimming()
{
    for (const auto& d : dimming_)
    {
        const float alpha = d.inactive() ? 0.35f : 1.0f;
        for (auto* c : d.controls)
            if (c->getAlpha() != alpha)
                c->setAlpha(alpha);
    }
}

void DevicePanel::paint(juce::Graphics& g)
{
    const auto& t = Theme::current();

    // Flat body; separation from the chain background comes from the tone step (ADR 0014).
    g.setColour(t.surface2);
    g.fillRoundedRectangle(getLocalBounds().toFloat(), t.radiusPanel);

    const auto header = getLocalBounds().removeFromTop(t.deviceHeaderHeight).reduced(t.grid * 2, 0);
    g.setColour(t.textPrimary);
    g.setFont(fonts::font(t.textSm, fonts::Weight::SemiBold));
    g.drawText(juce::String(std::string(context_.info.name)), header.withTrimmedLeft(onEnabledChange ? 18 : 0),
               juce::Justification::centredLeft, true);
}

void DevicePanel::resized()
{
    const auto& t = Theme::current();
    auto area = getLocalBounds();
    auto header = area.removeFromTop(t.deviceHeaderHeight).reduced(t.grid * 2, 0);
    power_.setVisible(static_cast<bool>(onEnabledChange));
    power_.setBounds(header.removeFromLeft(14).withSizeKeepingCentre(14, 14));
    layoutBody(area.reduced(t.grid * 2, t.grid));
}
} // namespace fd::ui
