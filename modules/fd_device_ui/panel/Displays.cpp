namespace fd::ui
{
MiniDisplay::MiniDisplay(DeviceUiContext& context, juce::String label, std::vector<size_t> params)
    : context_(context), label_(std::move(label)), params_(std::move(params)),
      last_(params_.size(), std::numeric_limits<float>::quiet_NaN())
{
    setInterceptsMouseClicks(false, true); // an accessory takes clicks
}

void MiniDisplay::setAccessory(juce::Component& accessory, int width)
{
    accessory_ = &accessory;
    accessoryWidth_ = width;
    addAndMakeVisible(accessory);
    resized();
}

void MiniDisplay::resized()
{
    if (accessory_ != nullptr)
        accessory_->setBounds(getLocalBounds().reduced(3).removeFromTop(14).removeFromRight(accessoryWidth_));
}

void MiniDisplay::refresh()
{
    bool changed = false;
    for (size_t i = 0; i < params_.size(); ++i)
    {
        const float v = context_.params.getPlain(params_[i]);
        if (!(v == last_[i]))
        {
            last_[i] = v;
            changed = true;
        }
    }
    if (changed)
        repaint();
}

void MiniDisplay::paint(juce::Graphics& g)
{
    const auto& t = Theme::current();
    const auto r = getLocalBounds().toFloat();
    g.setColour(t.surface0);
    g.fillRoundedRectangle(r, t.radiusControl);
    drawContent(g, label_.isEmpty() ? r.reduced(5.0f, 4.0f) : r.reduced(5.0f, 4.0f).withTrimmedTop(12.0f));
    if (label_.isNotEmpty())
        paintCapsLabel(g, label_, r.reduced(5.0f, 3.0f).removeFromTop(12.0f));
}

namespace
{
std::vector<size_t> filterParams(size_t type, size_t cutoff, size_t resonance, std::optional<size_t> enabled)
{
    std::vector<size_t> p{type, cutoff, resonance};
    if (enabled)
        p.push_back(*enabled);
    return p;
}

std::vector<size_t> envelopeParams(size_t attack, std::optional<size_t> cycle)
{
    std::vector<size_t> p{attack, attack + 1, attack + 2, attack + 3};
    if (cycle)
        p.push_back(*cycle);
    return p;
}
} // namespace

EnvelopeDisplay::EnvelopeDisplay(DeviceUiContext& context, juce::String label, size_t attackIndex,
                                 std::optional<size_t> cycleIndex)
    : MiniDisplay(context, std::move(label), envelopeParams(attackIndex, cycleIndex)), attack_(attackIndex),
      cycle_(cycleIndex)
{
}

void EnvelopeDisplay::drawContent(juce::Graphics& g, juce::Rectangle<float> area)
{
    const double a = param(attack_), d = param(attack_ + 1), s = param(attack_ + 2), r = param(attack_ + 3);
    const bool cycle = cycle_ && param(*cycle_) > 0.5f;
    const dsp::Adsr::Settings settings{a * 0.001, d * 0.001, s * 0.01, r * 0.001, cycle};

    // Width per segment: log-scaled time, normalised so the segments fill the display beside
    // a fixed sustain section.
    auto weight = [](double ms) { return juce::jmax(0.08, std::log1p(ms / 5.0) / std::log1p(20000.0 / 5.0)); };
    constexpr float sustainShare = 0.2f;
    const double total = cycle ? 2.0 * (weight(a) + weight(d)) : weight(a) + weight(d) + weight(r);
    const float timeWidth = area.getWidth() * (cycle ? 1.0f : 1.0f - sustainShare);
    auto width = [&](double ms) { return static_cast<float>(weight(ms) / total) * timeWidth; };

    dsp::Adsr env;
    juce::Path p;
    float x = area.getX();
    auto yFor = [&](double level) { return area.getBottom() - static_cast<float>(level) * area.getHeight(); };
    p.startNewSubPath(x, yFor(0.0));
    // Runs one segment with the envelope's own coefficients, sampled once per pixel.
    auto segment = [&](double ms, float px) {
        const int steps = juce::jmax(2, juce::roundToInt(px));
        env.setSampleRate(steps / (ms * 0.001));
        env.set(settings);
        for (int i = 1; i <= steps; ++i)
            p.lineTo(x + px * static_cast<float>(i) / static_cast<float>(steps), yFor(env.next()));
        x += px;
    };

    env.noteOn();
    segment(a, width(a));
    segment(d, width(d));
    if (cycle)
    {
        segment(a, width(a));
        segment(d, width(d));
    }
    else
    {
        x += area.getWidth() * sustainShare;
        p.lineTo(x, yFor(env.level()));
        env.noteOff();
        segment(r, width(r));
    }

    const auto& t = Theme::current();
    auto fill = p;
    fill.lineTo(x, area.getBottom());
    fill.closeSubPath();
    g.setColour(t.value.withAlpha(0.14f));
    g.fillPath(fill);
    g.setColour(t.value);
    g.strokePath(p, juce::PathStrokeType(1.5f, juce::PathStrokeType::curved));
}

FilterDisplay::FilterDisplay(DeviceUiContext& context, size_t typeIndex, size_t cutoffIndex, size_t resonanceIndex,
                             std::optional<size_t> enabledIndex)
    : MiniDisplay(context, "", filterParams(typeIndex, cutoffIndex, resonanceIndex, enabledIndex)), type_(typeIndex),
      cutoff_(cutoffIndex), resonance_(resonanceIndex), enabled_(enabledIndex)
{
}

void FilterDisplay::drawContent(juce::Graphics& g, juce::Rectangle<float> area)
{
    constexpr double topDb = 30.0, bottomDb = -42.0, sampleRate = 48000.0;
    const auto& t = Theme::current();
    auto yFor = [&](double db) {
        const double c = juce::jlimit(bottomDb, topDb, db);
        return area.getY() + static_cast<float>((topDb - c) / (topDb - bottomDb)) * area.getHeight();
    };
    g.setColour(t.surface3);
    g.drawHorizontalLine(juce::roundToInt(yFor(0.0)), area.getX(), area.getRight());

    const auto type = static_cast<dsp::SynthFilter::Type>(juce::roundToInt(param(type_)));
    const double cutoff = param(cutoff_);
    const double res = param(resonance_) * 0.01;
    const int n = juce::jmax(16, static_cast<int>(area.getWidth()));
    juce::Path curve;
    for (int i = 0; i <= n; ++i)
    {
        const double f = 20.0 * std::pow(1000.0, static_cast<double>(i) / n); // 20 Hz .. 20 kHz
        const double mag = dsp::SynthFilter::magnitude(type, f, cutoff, res, sampleRate);
        const juce::Point<float> pt(area.getX() + static_cast<float>(i) / static_cast<float>(n) * area.getWidth(),
                                    yFor(20.0 * std::log10(juce::jmax(mag, 1e-6))));
        i == 0 ? curve.startNewSubPath(pt) : curve.lineTo(pt);
    }
    const bool on = !enabled_ || param(*enabled_) > 0.5f;
    const auto colour = on ? t.value : t.textTertiary;
    auto fill = curve;
    fill.lineTo(area.getBottomRight());
    fill.lineTo(area.getBottomLeft());
    fill.closeSubPath();
    g.setColour(colour.withAlpha(0.14f));
    g.fillPath(fill);
    g.setColour(colour);
    g.strokePath(curve, juce::PathStrokeType(1.5f, juce::PathStrokeType::curved));
}
} // namespace fd::ui
