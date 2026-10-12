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

CutFilterCurve::CutFilterCurve(DeviceUiContext& context, size_t power, size_t low, size_t high)
    : MiniDisplay(context, {}, {power, low, high}), context_(context), power_(power), low_(low), high_(high),
      dragged_(low)
{
    setInterceptsMouseClicks(true, false);
    setMouseCursor(juce::MouseCursor::LeftRightResizeCursor);
    setTooltip("Drag near the low or the high edge to move that cutoff (Shift: finely)");
}

float CutFilterCurve::xFor(double hz) const
{
    return area_.getX() + static_cast<float>(std::log(hz / 20.0) / std::log(1000.0)) * area_.getWidth();
}

void CutFilterCurve::mouseDown(const juce::MouseEvent& e)
{
    const float lowX = xFor(context_.params.getPlain(low_));
    const float highX = xFor(context_.params.getPlain(high_));
    dragged_ = std::abs(e.position.x - lowX) <= std::abs(e.position.x - highX) ? low_ : high_;
    start_ = context_.params.getPlain(dragged_);
    startX_ = e.position.x;
    context_.params.beginGesture(dragged_);
}

void CutFilterCurve::mouseDrag(const juce::MouseEvent& e)
{
    const float fine = e.mods.isShiftDown() ? Theme::current().input.fineFactor : 1.0f;
    const double octaves = (e.position.x - startX_) * fine / area_.getWidth() * std::log2(1000.0);
    const auto& spec = context_.info.params[dragged_];
    context_.params.setPlain(dragged_,
                             std::clamp(static_cast<float>(start_ * std::exp2(octaves)), spec.min, spec.max));
    refresh();
}

void CutFilterCurve::mouseUp(const juce::MouseEvent&)
{
    context_.params.endGesture(dragged_);
}

void CutFilterCurve::drawContent(juce::Graphics& g, juce::Rectangle<float> area)
{
    area_ = area;
    constexpr double topDb = 3.0, bottomDb = -24.0, sampleRate = 48000.0;
    const auto& t = Theme::current();
    const auto yFor = [&](double db)
    {
        return area.getY() +
               static_cast<float>((topDb - std::clamp(db, bottomDb, topDb)) / (topDb - bottomDb)) *
                   area.getHeight();
    };
    g.setColour(t.surface3);
    g.drawHorizontalLine(juce::roundToInt(yFor(0.0)), area.getX(), area.getRight());
    const double low = param(low_), high = param(high_);
    const int n = std::max(16, static_cast<int>(area.getWidth()));
    juce::Path curve;
    for (int i = 0; i <= n; ++i)
    {
        const double f = 20.0 * std::pow(1000.0, static_cast<double>(i) / n);
        const double m =
            dsp::FeedbackPath::magnitude(f, low, std::min(high, 0.45 * sampleRate - 1.0), sampleRate);
        const juce::Point<float> pt(area.getX() +
                                        static_cast<float>(i) / static_cast<float>(n) * area.getWidth(),
                                    yFor(20.0 * std::log10(std::max(m, 1e-6))));
        i == 0 ? curve.startNewSubPath(pt) : curve.lineTo(pt);
    }
    const bool on = param(power_) >= 0.5f;
    const auto colour = on ? t.value : t.textTertiary;
    auto fill = curve;
    fill.lineTo(area.getBottomRight());
    fill.lineTo(area.getBottomLeft());
    fill.closeSubPath();
    g.setColour(t.fill(colour));
    g.fillPath(fill);
    g.setColour(colour);
    g.strokePath(curve, juce::PathStrokeType(1.5f, juce::PathStrokeType::curved));
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
    g.setColour(t.roles.curveFill);
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
    g.setColour(t.fill(colour));
    g.fillPath(fill);
    g.setColour(colour);
    g.strokePath(curve, juce::PathStrokeType(1.5f, juce::PathStrokeType::curved));
}

// ---- DynamicsHistory ---------------------------------------------------------------------

DynamicsHistory::DynamicsHistory(DeviceUiContext& context, int fifo, std::function<float()> referenceDb, float floorDb)
    : context_(context), fifo_(fifo), referenceDb_(std::move(referenceDb)), floorDb_(floorDb),
      reference_(referenceDb_()), frames_(kFrames, {floorDb, floorDb, 0.0f})
{
}

void DynamicsHistory::refresh()
{
    if (const float reference = referenceDb_(); reference != reference_)
    {
        reference_ = reference;
        repaint();
    }
}

void DynamicsHistory::tick(bool visible)
{
    if (context_.analysis == nullptr)
        return;
    auto& fifo = context_.analysis->fifo(fifo_);
    bool any = false;
    float frame[3];
    while (fifo.pop(frame, 3) == 3)
    {
        frames_[static_cast<size_t>(write_)] = {frame[0], frame[1], frame[2]};
        write_ = (write_ + 1) % kFrames;
        any = true;
    }
    if (any && visible)
        repaint();
}

float DynamicsHistory::yOf(juce::Rectangle<float> area, float db) const
{
    return area.getBottom() - juce::jlimit(0.0f, 1.0f, (db - floorDb_) / -floorDb_) * area.getHeight();
}

void DynamicsHistory::paint(juce::Graphics& g)
{
    const auto& t = Theme::current();
    const auto r = getLocalBounds().toFloat();
    g.setColour(t.surface0);
    g.fillRoundedRectangle(r, t.radiusControl);
    const auto area = r.reduced(1.0f);

    g.setColour(t.line);
    for (float db = -12.0f; db > floorDb_; db -= 12.0f)
        g.fillRect(area.getX(), yOf(area, db), area.getWidth(), 1.0f);

    // The reference, for reading the input against it.
    g.setColour(t.textTertiary);
    const float ry = yOf(area, reference_);
    const float dashes[] = {3.0f, 3.0f};
    g.drawDashedLine({area.getX(), ry, area.getRight(), ry}, dashes, 2, 1.0f);

    // One column per pixel: the loudest frame it covers. The output and reduction lines break
    // where there is nothing to show, so silence draws nothing at the edges.
    const int width = static_cast<int>(area.getWidth());
    juce::Path input, output, reduction, reductionLine;
    input.startNewSubPath(area.getX(), area.getBottom());
    reduction.startNewSubPath(area.getX(), area.getY());
    bool outputOn = false, reductionOn = false;
    float currentReduction = 0.0f;
    for (int x = 0; x < width; ++x)
    {
        const int from = x * kFrames / width, to = std::max(from + 1, (x + 1) * kFrames / width);
        float in = floorDb_, out = floorDb_, gr = 0.0f;
        for (int f = from; f < to; ++f)
        {
            const auto& frame = frames_[static_cast<size_t>((write_ + f) % kFrames)];
            in = std::max(in, frame[0]);
            out = std::max(out, frame[1]);
            gr = std::max(gr, frame[2]);
        }
        const float px = area.getX() + static_cast<float>(x);
        input.lineTo(px, yOf(area, in));
        const auto o = juce::Point<float>(px, yOf(area, out));
        const bool hasOutput = out > floorDb_;
        if (hasOutput)
            outputOn ? output.lineTo(o) : output.startNewSubPath(o);
        outputOn = hasOutput;
        const float grY = area.getY() + std::min(gr, -floorDb_) / -floorDb_ * area.getHeight();
        const auto rp = juce::Point<float>(px, grY);
        reduction.lineTo(rp);
        const bool hasReduction = gr >= 0.05f;
        if (hasReduction)
            reductionOn ? reductionLine.lineTo(rp) : reductionLine.startNewSubPath(rp);
        reductionOn = hasReduction;
        currentReduction = gr;
    }
    input.lineTo(area.getRight(), area.getBottom());
    input.closeSubPath();
    g.setColour(t.roles.spectrumFill);
    g.fillPath(input);
    g.setColour(t.textSecondary);
    g.strokePath(output, juce::PathStrokeType(1.0f));

    reduction.lineTo(area.getRight(), area.getY());
    reduction.closeSubPath();
    g.setColour(t.roles.curveFill);
    g.fillPath(reduction);
    g.setColour(t.value);
    g.strokePath(reductionLine, juce::PathStrokeType(1.5f));

    g.setColour(t.textSecondary);
    g.setFont(font(TextStyle::ValueSmall));
    const auto text =
        currentReduction >= 0.05f ? "-" + juce::String(currentReduction, 1) + " dB" : juce::String("0.0 dB");
    g.drawText(text, r.reduced(6.0f, 4.0f), juce::Justification::bottomRight, false);
}
// ---- SpectrumPlot ---------------------------------------------------------------------------

float SpectrumPlot::xOf(double hz) const noexcept
{
    const double t = std::log(std::max(hz, minHz) / minHz) / std::log(maxHz / minHz);
    return area.getX() + static_cast<float>(t) * area.getWidth();
}

double SpectrumPlot::hzAt(float x) const noexcept
{
    const double t = (x - area.getX()) / std::max(1.0f, area.getWidth());
    return minHz * std::pow(maxHz / minHz, std::clamp(t, 0.0, 1.0));
}

float SpectrumPlot::yOf(float db) const noexcept
{
    const float t = std::clamp((db - bottomDb) / std::max(1.0f, topDb - bottomDb), 0.0f, 1.0f);
    return area.getBottom() - t * area.getHeight();
}

juce::Point<float> SpectrumPlot::pointOf(const dsp::SpectrumCurve& curve, int band, float offsetDb) const noexcept
{
    const double hz = dsp::spectrum::bandHz(band);
    const float v = curve[static_cast<size_t>(band)];
    const float db = v <= dsp::spectrum::floorDb ? bottomDb : v + offsetDb + dsp::spectrum::tiltDb(hz, slope);
    return {xOf(hz), yOf(db)};
}

juce::Path SpectrumPlot::line(const dsp::SpectrumCurve& curve, float offsetDb) const
{
    juce::Path p;
    for (int b = 0; b < dsp::spectrum::numBands; ++b)
    {
        const auto pt = pointOf(curve, b, offsetDb);
        b == 0 ? p.startNewSubPath(pt) : p.lineTo(pt);
    }
    return p;
}

juce::Path SpectrumPlot::fill(const juce::Path& line) const
{
    juce::Path p(line);
    p.lineTo(xOf(dsp::spectrum::bandHz(dsp::spectrum::numBands - 1)), area.getBottom());
    p.lineTo(xOf(dsp::spectrum::bandHz(0)), area.getBottom());
    p.closeSubPath();
    return p;
}

void SpectrumPlot::paintGrid(juce::Graphics& g, float dbStep) const
{
    const auto& t = Theme::current();
    const auto inset = static_cast<float>(t.deviceGrid.displayInset);
    const auto text = static_cast<float>(t.deviceGrid.displayTextHeight);
    g.setFont(font(TextStyle::ValueSmall));
    for (const double hz : {50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0})
    {
        const float x = std::round(xOf(hz));
        g.setColour(t.line);
        g.fillRect(x, area.getY(), 1.0f, area.getHeight());
        g.setColour(t.textTertiary);
        g.drawText(hz >= 1000.0 ? juce::String(hz / 1000.0, 0) + "k" : juce::String(hz, 0),
                   juce::Rectangle<float>(x + inset / 2.0f, area.getBottom() - text, 4.0f * text, text),
                   juce::Justification::centredLeft, false);
    }
    if (dbStep <= 0.0f)
        return;
    for (float db = topDb - dbStep; db > bottomDb + dbStep / 2.0f; db -= dbStep)
    {
        const float y = std::round(yOf(db));
        g.setColour(t.roles.gridMinor);
        g.fillRect(area.getX(), y, area.getWidth(), 1.0f);
        g.setColour(t.textTertiary);
        g.drawText(juce::String(juce::roundToInt(db)),
                   juce::Rectangle<float>(area.getRight() - 4.0f * text - inset, y - text, 4.0f * text, text),
                   juce::Justification::bottomRight, false);
    }
}

void SpectrumPlot::stroke(juce::Graphics& g, const juce::Path& path, juce::Colour colour, Dash dash, float width)
{
    g.setColour(colour);
    const juce::PathStrokeType type(width);
    if (dash == Dash::Solid)
    {
        g.strokePath(path, type);
        return;
    }
    // In units of the stroke's width, so the dashes keep their look at any scale.
    static constexpr float dashed[] = {4.0f, 3.0f}, longDashed[] = {9.0f, 4.0f}, dotted[] = {1.0f, 3.0f};
    const float* pattern = dash == Dash::Dashed ? dashed : dash == Dash::LongDashed ? longDashed : dotted;
    const float lengths[] = {pattern[0] * width, pattern[1] * width};
    juce::Path dashedPath;
    type.createDashedStroke(dashedPath, path, lengths, 2);
    g.fillPath(dashedPath);
}
} // namespace fd::ui
