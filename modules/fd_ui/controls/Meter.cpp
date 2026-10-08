namespace fd::ui
{
namespace
{
constexpr float kMinDb = -60.0f;
constexpr float kMaxDb = 6.0f;
constexpr float kDecayDbPerSecond = 24.0f;

float gainToDb(float gain)
{
    return gain > 0.0f ? juce::jmax(kMinDb, 20.0f * std::log10(gain)) : kMinDb;
}

float toProportion(float db)
{
    return juce::jlimit(0.0f, 1.0f, (db - kMinDb) / (kMaxDb - kMinDb));
}
} // namespace

Meter::Meter(int numChannels) : levelsDb_(static_cast<size_t>(numChannels), kMinDb) {}

void Meter::setLevels(std::initializer_list<float> linear)
{
    size_t i = 0;
    for (float v : linear)
        if (i < levelsDb_.size())
            levelsDb_[i++] = gainToDb(v);
    repaint();
}

void Meter::update()
{
    if (!source_)
        return;
    const double now = juce::Time::getMillisecondCounterHiRes();
    const float elapsed = lastUpdateMs_ > 0.0 ? static_cast<float>((now - lastUpdateMs_) * 0.001) : 0.0f;
    lastUpdateMs_ = now;

    bool changed = false;
    for (size_t c = 0; c < levelsDb_.size(); ++c)
    {
        const float peakDb = gainToDb(source_(static_cast<int>(c)));
        const float decayed = juce::jmax(kMinDb, levelsDb_[c] - kDecayDbPerSecond * elapsed);
        const float next = juce::jmax(peakDb, decayed);
        changed |= !juce::approximatelyEqual(next, levelsDb_[c]);
        levelsDb_[c] = next;
    }
    if (changed)
        repaint();
}

void Meter::paint(juce::Graphics& g)
{
    const auto& t = Theme::current();
    const auto bounds = getLocalBounds().toFloat();
    const int n = static_cast<int>(levelsDb_.size());
    const float gap = 2.0f;
    const float barWidth = (bounds.getWidth() - gap * static_cast<float>(n - 1)) / static_cast<float>(juce::jmax(1, n));

    // Flat zones (ADR 0014): normal up to -6 dB, hot up to 0 dB, clip above.
    const float yHot = bounds.getBottom() - bounds.getHeight() * toProportion(-6.0f);
    const float yClip = bounds.getBottom() - bounds.getHeight() * toProportion(0.0f);

    for (int c = 0; c < n; ++c)
    {
        const auto bar = juce::Rectangle<float>(bounds.getX() + static_cast<float>(c) * (barWidth + gap), bounds.getY(),
                                                barWidth, bounds.getHeight());
        g.setColour(t.surface0);
        g.fillRect(bar);

        const float top = bar.getBottom() - bar.getHeight() * toProportion(levelsDb_[static_cast<size_t>(c)]);
        if (top >= bar.getBottom() - 0.5f)
            continue;
        auto fillZone = [&](float zoneTop, float zoneBottom, juce::Colour colour) {
            const float y0 = juce::jmax(top, zoneTop);
            if (y0 < zoneBottom)
            {
                g.setColour(colour);
                g.fillRect(bar.withTop(y0).withBottom(zoneBottom));
            }
        };
        fillZone(yHot, bar.getBottom(), t.meterNormal);
        fillZone(yClip, yHot, t.meterHot);
        fillZone(bar.getY(), yClip, t.meterClip);
    }
}
} // namespace fd::ui
