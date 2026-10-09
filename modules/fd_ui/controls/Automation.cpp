namespace fd::ui
{
void paintAutomationIndicator(juce::Graphics& g, juce::Point<float> centre, AutomationState state)
{
    if (state == AutomationState::None)
        return;
    const auto& t = Theme::current();
    const auto dot = juce::Rectangle<float>(4.0f, 4.0f).withCentre(centre);
    g.setColour(t.roles.automation);
    if (state == AutomationState::On)
        g.fillEllipse(dot);
    else
        g.drawEllipse(dot.reduced(0.5f), 1.0f);
}
} // namespace fd::ui
