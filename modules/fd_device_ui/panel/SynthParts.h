#pragma once

/// The parts a synth's panel shares with every synth built on the same families of parameters
/// (devices/wavetable/SPEC.md, devices/granular/SPEC.md): its modulation as the shared
/// modulation UI sees it, both filters' responses, an envelope drawn and dragged, an LFO's shape
/// or a random source drawn, and the modulation matrix. One look and one behaviour for each, in
/// the Wavetable and in Granular alike (ui-foundations.md, "One look per thing").
///
/// `T` describes the synth: `T::Synth` is its device class, laid out with the Wavetable's
/// families and names:
/// - routes: `numRoutes`, `route(n, RouteParam)` with RouteSource, RouteTarget, RouteAmount,
///   RouteCurve, RouteScale, RouteParamCount; `targetParam(target)`, `targetOf(param)`,
///   `TargetNone`; a `Source` enum with `Lfo1`, `Random1`, `Macro1` and `Count`;
/// - filters: `numFilters`, `filter(n, FilterParam)` with FilterOn, FilterType (the VoiceFilter's
///   types, in order), FilterCutoff, FilterRes, FilterShape;
/// - envelopes: `env(n, EnvParam)` with EnvAttack, EnvHold, EnvDecay, EnvSustain, EnvRelease, the
///   three curves, EnvMode and EnvParamCount;
/// - LFOs and random sources: `numLfos`, `Curve`, `lfoShape(n)`, `random(n, RandomParam)` with
///   RandomMode; the analysis slots `ActiveVoices` and `Lfo1Phase` (each LFO's phase after it);
/// - `macroName(n)`.
///
/// And `T` itself gives what differs from synth to synth:
/// - `sourceGroup(source)`: the group a source is listed under ("Envelopes", "LFOs"…);
/// - `bipolar(source)`: whether it swings both ways;
/// - `envSource(n)`: envelope n's source;
/// - `randomMode(choice)`: how a random source's mode moves, drawn.
namespace fd::ui::synth
{
namespace detail
{
inline float plain(const DeviceUiContext& ctx, size_t index)
{
    return ctx.params.getPlain(index);
}

/// A parameter set as one gesture.
inline void set(DeviceUiContext& ctx, size_t index, float value)
{
    ctx.params.beginGesture(index);
    ctx.params.setPlain(index, value);
    ctx.params.endGesture(index);
}

/// A changed set of values (what a display draws from): true when any moved.
inline bool changed(std::vector<float>& last, const std::vector<float>& now)
{
    if (now == last)
        return false;
    last = now;
    return true;
}

inline float inset()
{
    return static_cast<float>(Theme::current().deviceGrid.displayInset);
}
} // namespace detail

/// A drawn curve's state keys, for an LFO ("lfo3") or a Remap ("osc1.remap2"): its points, or
/// its named shape.
inline nlohmann::json curvePatch(const std::string& key, const dsp::DrawnCurve& c)
{
    nlohmann::json patch;
    if (c.points.empty())
    {
        patch[key + ".points"] = nullptr;
        patch[key + ".shape"] = c.shape;
    }
    else
    {
        patch[key + ".points"] = c.points;
        patch[key + ".shape"] = nullptr;
    }
    return patch;
}

/// The synth's matrix (its routes in its parameters) for rings, handles and dragged sources.
template <class T> class SynthModulation final : public ModulationModel
{
public:
    using S = typename T::Synth;

    explicit SynthModulation(DeviceUiContext& context) : ctx_(context) {}

    [[nodiscard]] std::vector<Route> routes() const override
    {
        std::vector<Route> out;
        for (int n = 0; n < S::numRoutes; ++n)
        {
            const auto source = static_cast<int>(detail::plain(ctx_, S::route(n, S::RouteSource)));
            const auto target = static_cast<int>(detail::plain(ctx_, S::route(n, S::RouteTarget)));
            if (source <= 0 || target <= 0)
                continue;
            if (const auto param = S::targetParam(target))
                out.push_back({n, source, *param, detail::plain(ctx_, S::route(n, S::RouteAmount)) * 0.01f,
                               bipolar(source)});
        }
        return out;
    }

    [[nodiscard]] bool canModulate(size_t param) const override { return S::targetOf(param).has_value(); }

    [[nodiscard]] juce::String sourceName(int source) const override
    {
        const auto& choices = ctx_.info.params[S::route(0, S::RouteSource)].choices;
        return source >= 0 && static_cast<size_t>(source) < choices.size()
                   ? juce::String(std::string(choices[static_cast<size_t>(source)]))
                   : juce::String();
    }

    bool addRoute(int source, size_t param) override
    {
        const int n = freeSlot();
        const auto target = S::targetOf(param);
        if (n < 0 || !target)
            return false;
        const auto& spec = ctx_.info.params[param];
        ctx_.params.beginEdit("Modulate " + juce::String(std::string(spec.name)) + " with " +
                              sourceName(source));
        detail::set(ctx_, S::route(n, S::RouteSource), static_cast<float>(source));
        detail::set(ctx_, S::route(n, S::RouteTarget), static_cast<float>(*target));
        detail::set(ctx_, S::route(n, S::RouteAmount), 50.0f);
        detail::set(ctx_, S::route(n, S::RouteCurve), 0.0f);
        detail::set(ctx_, S::route(n, S::RouteScale), 0.0f);
        return true;
    }

    [[nodiscard]] bool isFull() const override { return freeSlot() < 0; }

    void removeRoute(int index) override
    {
        ctx_.params.beginEdit("Remove a modulation route");
        for (const auto p : {S::RouteSource, S::RouteTarget, S::RouteAmount, S::RouteCurve, S::RouteScale})
            detail::set(ctx_, S::route(index, p), ctx_.info.params[S::route(index, p)].def);
    }

    void beginAmount(int index) override { ctx_.params.beginGesture(S::route(index, S::RouteAmount)); }
    void setAmount(int index, float amount) override
    {
        ctx_.params.setPlain(S::route(index, S::RouteAmount), std::clamp(amount, -1.0f, 1.0f) * 100.0f);
    }
    void endAmount(int index) override { ctx_.params.endGesture(S::route(index, S::RouteAmount)); }

    [[nodiscard]] std::optional<int> routeOfParam(size_t param) const override
    {
        const size_t first = S::route(0, S::RouteSource), end = S::route(S::numRoutes, S::RouteSource);
        if (param < first || param >= end)
            return std::nullopt;
        return static_cast<int>((param - first) / S::RouteParamCount);
    }

    [[nodiscard]] std::vector<SourceInfo> sources() const override
    {
        // In the matrix's order, by group; a macro by its name.
        const auto* s = dynamic_cast<const S*>(ctx_.device);
        const int macro1 = static_cast<int>(S::Source::Macro1);
        std::vector<SourceInfo> out;
        for (int id = 1; id < static_cast<int>(S::Source::Count); ++id)
        {
            auto name = sourceName(id);
            if (id >= macro1 && id < macro1 + S::numMacros && s != nullptr)
                name = juce::String(s->macroName(id - macro1));
            out.push_back({id, name, T::sourceGroup(id)});
        }
        return out;
    }

    /// A route from the matrix's add button: the source last hovered (else LFO 1), no target yet.
    bool addFromMatrix()
    {
        const int n = freeSlot();
        if (n < 0)
            return false;
        const int source = lastSource > 0 ? lastSource : static_cast<int>(S::Source::Lfo1);
        ctx_.params.beginEdit("Add a modulation route");
        detail::set(ctx_, S::route(n, S::RouteSource), static_cast<float>(source));
        detail::set(ctx_, S::route(n, S::RouteTarget), static_cast<float>(S::TargetNone));
        detail::set(ctx_, S::route(n, S::RouteAmount), 50.0f);
        detail::set(ctx_, S::route(n, S::RouteCurve), 0.0f);
        detail::set(ctx_, S::route(n, S::RouteScale), 0.0f);
        return true;
    }

    /// The routes the matrix lists: those with a source, in slot order.
    [[nodiscard]] std::vector<int> listed() const
    {
        std::vector<int> out;
        for (int n = 0; n < S::numRoutes; ++n)
            if (detail::plain(ctx_, S::route(n, S::RouteSource)) >= 0.5f)
                out.push_back(n);
        return out;
    }

    /// Whether a source swings both ways (an LFO, a random source, the note, the bend).
    [[nodiscard]] static bool bipolar(int source) noexcept { return T::bipolar(source); }

private:
    [[nodiscard]] int freeSlot() const
    {
        for (int n = 0; n < S::numRoutes; ++n)
            if (detail::plain(ctx_, S::route(n, S::RouteSource)) < 0.5f)
                return n;
        return -1;
    }

    DeviceUiContext& ctx_;
};

/// Both filters' responses (the selected one bright, the other faint), 20 Hz to 20 kHz. Dragged,
/// the selected filter's Cutoff sideways and Resonance up and down.
template <class T>
class SynthFilterResponse final : public juce::Component,
                                  public ParamControl,
                                  public juce::SettableTooltipClient
{
public:
    using S = typename T::Synth;

    explicit SynthFilterResponse(DeviceUiContext& context) : ctx_(context)
    {
        setMouseCursor(juce::MouseCursor::UpDownLeftRightResizeCursor);
        setTooltip("Drag sideways for the cutoff, up and down for the resonance (Shift: finely)");
    }

    void setFilter(int n)
    {
        filter_ = n;
        last_.clear();
        refresh();
        repaint();
    }

    void refresh() override
    {
        std::vector<float> now;
        for (int n = 0; n < S::numFilters; ++n)
            for (const auto p : {S::FilterOn, S::FilterType, S::FilterCutoff, S::FilterRes, S::FilterShape})
                now.push_back(detail::plain(ctx_, S::filter(n, p)));
        if (detail::changed(last_, now))
            repaint();
    }

    void paint(juce::Graphics& g) override
    {
        constexpr double kDisplayRate = 48000.0;
        const auto& t = Theme::current();
        paintDisplayWell(g, getLocalBounds().toFloat());
        const auto p = plot();
        g.setColour(t.surface3);
        g.drawHorizontalLine(juce::roundToInt(p.yOf(0.0f)), p.area.getX(), p.area.getRight());
        // The other filter faint behind, the selected one in the value colour; one that's off dimmed.
        for (const int n : {1 - filter_, filter_})
        {
            const bool selected = n == filter_, on = detail::plain(ctx_, S::filter(n, S::FilterOn)) >= 0.5f;
            if (!selected && !on)
                continue;
            const auto type = static_cast<dsp::VoiceFilter::Type>(
                static_cast<int>(detail::plain(ctx_, S::filter(n, S::FilterType))));
            const double cutoff = detail::plain(ctx_, S::filter(n, S::FilterCutoff)),
                         res = detail::plain(ctx_, S::filter(n, S::FilterRes)) * 0.01,
                         shape = detail::plain(ctx_, S::filter(n, S::FilterShape)) * 0.01;
            const int points = std::max(32, static_cast<int>(p.area.getWidth()));
            juce::Path line;
            for (int i = 0; i <= points; ++i)
            {
                const double hz = SpectrumPlot::minHz * std::pow(SpectrumPlot::maxHz / SpectrumPlot::minHz,
                                                                 static_cast<double>(i) / points);
                const double m = dsp::VoiceFilter::magnitude(type, hz, cutoff, res, shape, kDisplayRate);
                const juce::Point<float> pt(p.xOf(hz),
                                            p.yOf(static_cast<float>(20.0 * std::log10(std::max(m, 1e-6)))));
                i == 0 ? line.startNewSubPath(pt) : line.lineTo(pt);
            }
            const auto colour = selected && on ? t.value : t.textTertiary;
            if (selected)
            {
                auto fill = line;
                fill.lineTo(p.area.getBottomRight());
                fill.lineTo(p.area.getBottomLeft());
                fill.closeSubPath();
                g.setColour(t.fill(colour));
                g.fillPath(fill);
            }
            g.setColour(colour);
            g.strokePath(line, juce::PathStrokeType(selected ? 1.5f : 1.0f, juce::PathStrokeType::curved));
            if (selected)
            {
                const double m = dsp::VoiceFilter::magnitude(type, cutoff, cutoff, res, shape, kDisplayRate);
                paintDisplayHandle(
                    g, {p.xOf(cutoff), p.yOf(static_cast<float>(20.0 * std::log10(std::max(m, 1e-6))))},
                    isMouseButtonDown());
            }
        }
    }

    void mouseDown(const juce::MouseEvent&) override
    {
        startCutoff_ = detail::plain(ctx_, S::filter(filter_, S::FilterCutoff));
        startRes_ = detail::plain(ctx_, S::filter(filter_, S::FilterRes));
        ctx_.params.beginGesture(S::filter(filter_, S::FilterCutoff));
        ctx_.params.beginGesture(S::filter(filter_, S::FilterRes));
        repaint();
    }

    void mouseDrag(const juce::MouseEvent& e) override
    {
        const auto& input = Theme::current().input;
        const float fine = e.mods.isShiftDown() ? input.fineFactor : 1.0f;
        const auto p = plot();
        const double octaves =
            static_cast<double>(e.getDistanceFromDragStartX()) * fine / p.area.getWidth() * std::log2(1000.0);
        const auto& cutoff = ctx_.info.params[S::filter(filter_, S::FilterCutoff)];
        const auto& res = ctx_.info.params[S::filter(filter_, S::FilterRes)];
        ctx_.params.setPlain(
            S::filter(filter_, S::FilterCutoff),
            std::clamp(static_cast<float>(startCutoff_ * std::exp2(octaves)), cutoff.min, cutoff.max));
        ctx_.params.setPlain(S::filter(filter_, S::FilterRes),
                             std::clamp(startRes_ - static_cast<float>(e.getDistanceFromDragStartY()) * fine /
                                                        input.valueTravel * 100.0f,
                                        res.min, res.max));
        refresh();
    }

    void mouseUp(const juce::MouseEvent&) override
    {
        ctx_.params.endGesture(S::filter(filter_, S::FilterCutoff));
        ctx_.params.endGesture(S::filter(filter_, S::FilterRes));
        repaint();
    }

private:
    [[nodiscard]] SpectrumPlot plot() const
    {
        return {getLocalBounds().toFloat().reduced(detail::inset()), 18.0f, -30.0f};
    }

    DeviceUiContext& ctx_;
    int filter_ = 0;
    float startCutoff_ = 0.0f, startRes_ = 0.0f;
    std::vector<float> last_;
};

/// Envelope n drawn from its times, sustain and curves, and dragged: the attack's peak sideways
/// (Attack), the decay's end both ways (Decay, Sustain), the release's end sideways (Release); a
/// segment dragged up or down bends it (its curve). Its source handle in the corner.
template <class T>
class SynthEnvelopeEditor final : public juce::Component,
                                  public ParamControl,
                                  public juce::SettableTooltipClient
{
public:
    using S = typename T::Synth;

    SynthEnvelopeEditor(DeviceUiContext& context, SynthModulation<T>& modulation)
        : ctx_(context), handle_(modulation, T::envSource(0)), drag_(context, dragParams(0))
    {
        addAndMakeVisible(handle_);
        setTooltip("Drag the points for the times and the sustain; drag a segment up or down to bend it");
    }

    void setEnvelope(int n)
    {
        env_ = n;
        drag_.setParams(dragParams(n));
        handle_.setSource(T::envSource(n));
        resized(); // the chip's name changed
        last_.clear();
        refresh();
        repaint();
    }

    void refresh() override
    {
        std::vector<float> now;
        for (size_t p = 0; p < S::EnvParamCount; ++p)
            now.push_back(param(static_cast<typename S::EnvParam>(p)));
        if (detail::changed(last_, now))
            repaint();
    }

    void resized() override
    {
        // The source's handle, a chip, in the top-right corner, inside the well.
        const int size = Theme::current().deviceGrid.sourceHandle;
        handle_.setBounds(getLocalBounds()
                              .reduced(static_cast<int>(detail::inset()))
                              .removeFromTop(size)
                              .removeFromRight(handle_.preferredWidth()));
    }

    void paint(juce::Graphics& g) override
    {
        const auto& t = Theme::current();
        paintDisplayWell(g, getLocalBounds().toFloat());
        const auto l = layout();
        const auto& a = l.area;
        const float sustain = param(S::EnvSustain) * 0.01f;
        juce::Path line;
        line.startNewSubPath(a.getX(), a.getBottom());
        // Each segment from the envelope's own shape: rising, falling to the sustain, falling to zero.
        const auto segment = [&](float x0, float x1, float from, float to, float curve)
        {
            const int n = std::max(2, static_cast<int>(x1 - x0));
            for (int i = 1; i <= n; ++i)
            {
                const double u = static_cast<double>(i) / n;
                const auto y = static_cast<float>(from + (to - from) * dsp::CurvedEnvelope::shape(u, curve));
                line.lineTo(x0 + (x1 - x0) * static_cast<float>(u), a.getBottom() - y * a.getHeight());
            }
        };
        segment(a.getX(), l.attackEnd, 0.0f, 1.0f, param(S::EnvAttackCurve) * 0.01f);
        line.lineTo(l.holdEnd, a.getY());
        segment(l.holdEnd, l.decayEnd, 1.0f, sustain, param(S::EnvDecayCurve) * 0.01f);
        line.lineTo(l.sustainEnd, a.getBottom() - sustain * a.getHeight());
        segment(l.sustainEnd, l.releaseEnd, sustain, 0.0f, param(S::EnvReleaseCurve) * 0.01f);
        auto fill = line;
        fill.lineTo(a.getBottomRight());
        fill.closeSubPath();
        g.setColour(t.fill(t.value));
        g.fillPath(fill);
        g.setColour(t.value);
        g.strokePath(line, juce::PathStrokeType(1.5f, juce::PathStrokeType::curved));
        drag_.paint(g, {pointOf(0, l), pointOf(1, l), pointOf(2, l)});
    }

    void mouseMove(const juce::MouseEvent& e) override
    {
        setMouseCursor(EnvelopeDrag::cursorFor(partAt(e.position)));
    }
    void mouseDown(const juce::MouseEvent& e) override
    {
        drag_.begin(partAt(e.position));
        repaint();
    }
    void mouseDrag(const juce::MouseEvent& e) override
    {
        drag_.drag(e.position - e.mouseDownPosition, e.mods.isShiftDown(), layout().area.getHeight());
        refresh();
    }
    void mouseUp(const juce::MouseEvent&) override
    {
        drag_.end();
        repaint();
    }

    /// What a press at `p` drags: a point (0 attack, 1 decay, 2 release) or a segment's bend
    /// (3 attack, 4 decay, 5 release), or -1.
    [[nodiscard]] int partAt(juce::Point<float> p) const
    {
        const auto l = layout();
        return drag_.partAt({pointOf(0, l), pointOf(1, l), pointOf(2, l)},
                            {juce::Range(l.area.getX(), l.attackEnd), juce::Range(l.holdEnd, l.decayEnd),
                             juce::Range(l.sustainEnd, l.releaseEnd + 1.0f)},
                            l.area, p);
    }
    [[nodiscard]] ModulationHandle& handle() noexcept { return handle_; }

private:
    struct Layout
    {
        juce::Rectangle<float> area;
        float attackEnd = 0.0f, holdEnd = 0.0f, decayEnd = 0.0f, sustainEnd = 0.0f, releaseEnd = 0.0f;
    };

    [[nodiscard]] Layout layout() const
    {
        // Below the handle's row; the segments as wide as the log of their times, the sustain a fixed
        // share (as the shared envelope display draws them). The points' handles sit whole inside the
        // well.
        Layout l;
        const auto& grid = Theme::current().deviceGrid;
        l.area = getLocalBounds()
                     .toFloat()
                     .reduced(detail::inset() + static_cast<float>(grid.displayHandle) * 0.5f)
                     .withTrimmedTop(static_cast<float>(grid.sourceHandle));
        const auto w = [](float ms) { return std::log1p(std::max(0.0f, ms) / 5.0f); };
        const float a = w(param(S::EnvAttack)), h = w(param(S::EnvHold)), d = w(param(S::EnvDecay)),
                    r = w(param(S::EnvRelease));
        const float sustain = std::max(0.15f * (a + h + d + r), 0.5f);
        const float total = std::max(1e-3f, a + h + d + sustain + r);
        const float x0 = l.area.getX(), width = l.area.getWidth();
        l.attackEnd = x0 + width * a / total;
        l.holdEnd = l.attackEnd + width * h / total;
        l.decayEnd = l.holdEnd + width * d / total;
        l.sustainEnd = l.decayEnd + width * sustain / total;
        l.releaseEnd = x0 + width;
        return l;
    }

    [[nodiscard]] juce::Point<float> pointOf(int part, const Layout& l) const
    {
        const float sustainY = l.area.getBottom() - param(S::EnvSustain) * 0.01f * l.area.getHeight();
        switch (part)
        {
        case 0:
            return {l.attackEnd, l.area.getY()};
        case 1:
            return {l.decayEnd, sustainY};
        default:
            return {l.releaseEnd, l.area.getBottom()};
        }
    }

    [[nodiscard]] float param(typename S::EnvParam p) const { return detail::plain(ctx_, S::env(env_, p)); }

    /// Envelope n's attack, decay, sustain and release.
    [[nodiscard]] static EnvelopeDrag::Params dragParams(int n)
    {
        return {S::env(n, S::EnvAttack), S::env(n, S::EnvDecay), S::env(n, S::EnvSustain),
                S::env(n, S::EnvRelease),
                std::array{S::env(n, S::EnvAttackCurve), S::env(n, S::EnvDecayCurve),
                           S::env(n, S::EnvReleaseCurve)}};
    }

    DeviceUiContext& ctx_;
    ModulationHandle handle_;
    EnvelopeDrag drag_; // its points and bends, as every envelope's
    int env_ = 0;
    std::vector<float> last_;
};

/// LFO n's shape drawn and edited with the shared curve editor (points dragged, a double-click
/// adds or removes one, a segment bent, a named shape from its menu), or random source n's way of
/// moving drawn. Its phase while one plays; its source handle in the corner.
template <class T> class SynthShapeEditor final : public CurveEditor
{
public:
    using S = typename T::Synth;

    SynthShapeEditor(DeviceUiContext& context, SynthModulation<T>& modulation)
        : ctx_(context), handle_(modulation, static_cast<int>(S::Source::Lfo1))
    {
        curve = [this]
        {
            const auto* s = dynamic_cast<const S*>(ctx_.device);
            return s != nullptr && isLfo() ? s->lfoShape(source_) : typename S::Curve{};
        };
        onChange = [this](const typename S::Curve& c)
        {
            if (!isLfo())
                return;
            ctx_.params.beginEdit("Draw LFO " + juce::String(source_ + 1));
            ctx_.params.applyExtraState(curvePatch("lfo" + std::to_string(source_ + 1), c));
            refresh();
        };
        phase = [this]
        {
            if (!isLfo() || ctx_.analysis == nullptr || ctx_.analysis->read(S::ActiveVoices) <= 0.0f)
                return -1.0f;
            return ctx_.analysis->read(S::Lfo1Phase + source_);
        };
        setSource(0);
    }

    /// The LFOs first, then the random sources.
    void setSource(int n)
    {
        source_ = n;
        handle_.setSource(isLfo() ? static_cast<int>(S::Source::Lfo1) + n
                                  : static_cast<int>(S::Source::Random1) + n - S::numLfos);
        setAccessory(&handle_, handle_.preferredWidth()); // the chip's name changed
        setEditable(isLfo());
        setTooltip(
            isLfo() ? "Drag a point; double-click to add or remove one; drag a segment up or down to bend it"
                    : "How the random source moves");
        lastRandom_.clear();
        refresh();
        repaint();
    }

    void refresh() override
    {
        if (isLfo())
        {
            CurveEditor::refresh();
            return;
        }
        // A random source: what it draws is its mode.
        const auto state = std::to_string(source_) + "m" +
                           std::to_string(static_cast<int>(
                               detail::plain(ctx_, S::random(source_ - S::numLfos, S::RandomMode))));
        if (state != lastRandom_)
        {
            lastRandom_ = state;
            repaint();
        }
    }

    [[nodiscard]] ModulationHandle& handle() noexcept { return handle_; }

protected:
    void paintContent(juce::Graphics& g, juce::Rectangle<float> r) override
    {
        if (isLfo())
        {
            CurveEditor::paintContent(g, r);
            return;
        }
        // The random source itself, seeded, a few of its cycles across.
        const auto& t = Theme::current();
        dsp::RandomSource random;
        random.reset(7);
        const auto mode = T::randomMode(
            static_cast<int>(detail::plain(ctx_, S::random(source_ - S::numLfos, S::RandomMode))));
        const int n = std::max(16, static_cast<int>(r.getWidth()));
        const double perPoint = 6.0 / n;
        juce::Path line;
        for (int i = 0; i <= n; ++i)
        {
            if (mode == dsp::RandomSource::Mode::PerNote && i % (n / 3 + 1) == 0)
                random.noteOn();
            const float v = random.advance(mode, 1, perPoint);
            const juce::Point<float> pt(r.getX() +
                                            r.getWidth() * static_cast<float>(i) / static_cast<float>(n),
                                        r.getCentreY() - v * r.getHeight() * 0.5f);
            i == 0 ? line.startNewSubPath(pt) : line.lineTo(pt);
        }
        g.setColour(t.value);
        g.strokePath(line, juce::PathStrokeType(1.5f));
    }

private:
    [[nodiscard]] bool isLfo() const noexcept { return source_ < S::numLfos; }

    DeviceUiContext& ctx_;
    ModulationHandle handle_;
    int source_ = 0;
    std::string lastRandom_;
};

/// The modulation matrix: a caption row with Add, then a row a route (source, target, amount,
/// curve, scale) on the module grid, five showing and the wheel scrolling the rest (the section's
/// title says which). Right-click a route's control to remove the route.
template <class T> class SynthMatrixList final : public juce::Component, public ParamControl
{
public:
    using S = typename T::Synth;
    static constexpr int rowsShown = 5; // routes showing at once, below the captions

    SynthMatrixList(DeviceUiContext& context, SynthModulation<T>& modulation)
        : ctx_(context), modulation_(modulation)
    {
        add_.setTooltip("Add a modulation route (or drag a source's handle onto a knob)");
        add_.onClick = [this]
        {
            if (!modulation_.addFromMatrix())
                add_.setTooltip("The matrix is full: remove a route first");
            refresh();
        };
        addAndMakeVisible(add_);
        rebuild();
    }

    void refresh() override
    {
        if (modulation_.listed() != listed_)
        {
            rebuild();
            return;
        }
        for (auto& c : controls_)
            if (auto* pc = dynamic_cast<ParamControl*>(c.get()))
                pc->refresh();
        add_.setEnabled(!modulation_.isFull());
    }

    void paint(juce::Graphics& g) override
    {
        const auto& t = Theme::current();
        for (const auto& col : columns())
            paintCapsLabel(g, col.caption, PanelSection::cellBounds(col.column, 0, col.columns, 1).toFloat());
        if (listed_.empty())
        {
            g.setColour(t.textTertiary);
            g.setFont(font(TextStyle::Caption));
            g.drawText(juce::String::fromUTF8("Drag a source’s chip onto a knob, right-click a knob: "
                                              "Modulate with, or add a route with +"),
                       PanelSection::cellBounds(0, 1, 11, rowsShown), juce::Justification::centred, false);
        }
    }

    void resized() override
    {
        // Add at the end of the caption row.
        const int size = Theme::current().deviceGrid.headerButton;
        add_.setBounds(
            PanelSection::cellBounds(10, 0, 1, 1).removeFromRight(size).withSizeKeepingCentre(size, size));
    }

    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails& wheel) override
    {
        const int was = first_;
        first_ += wheel.deltaY < 0.0f ? 1 : (wheel.deltaY > 0.0f ? -1 : 0);
        first_ = std::clamp(first_, 0, std::max(0, static_cast<int>(listed_.size()) - rowsShown));
        if (first_ != was)
            rebuild();
    }

    /// The routes listed (with a source), and the first one showing (for tests).
    [[nodiscard]] const std::vector<int>& rows() const noexcept { return listed_; }
    [[nodiscard]] int firstShown() const noexcept { return first_; }
    [[nodiscard]] juce::Button& addButton() noexcept { return add_; }

private:
    struct Column
    {
        const char* caption;
        typename S::RouteParam param;
        int column, columns;
    };
    static constexpr std::array<Column, 5> columns()
    {
        return {Column{"Source", S::RouteSource, 0, 2}, Column{"Target", S::RouteTarget, 2, 3},
                Column{"Amount", S::RouteAmount, 5, 2}, Column{"Curve", S::RouteCurve, 7, 2},
                Column{"Scale", S::RouteScale, 9, 2}};
    }

    void rebuild()
    {
        for (auto& c : controls_)
            removeChildComponent(c.get());
        controls_.clear();
        listed_ = modulation_.listed();
        first_ = std::clamp(first_, 0, std::max(0, static_cast<int>(listed_.size()) - rowsShown));
        for (int row = 0; row < rowsShown && first_ + row < static_cast<int>(listed_.size()); ++row)
        {
            const int n = listed_[static_cast<size_t>(first_ + row)];
            for (const auto& col : columns())
            {
                const auto index = S::route(n, col.param);
                std::unique_ptr<juce::Component> control;
                if (col.param == S::RouteAmount || col.param == S::RouteCurve)
                {
                    auto bar = std::make_unique<ParamKnob>(ctx_, index);
                    bar->setStyle(Knob::Style::Bar);
                    bar->setName({}); // the caption names the column
                    control = std::move(bar);
                }
                else
                    control = std::make_unique<ParamComboBox>(ctx_, index);
                control->setBounds(PanelSection::cellBounds(col.column, row + 1, col.columns, 1));
                addAndMakeVisible(*control);
                controls_.push_back(std::move(control));
            }
        }
        add_.setEnabled(!modulation_.isFull());
        // Which routes show, when there are more than fit: in the section's title.
        if (auto* section = findParentComponentOfClass<PanelSection>())
            section->setSectionTitle(listed_.size() > static_cast<size_t>(rowsShown)
                                         ? "Matrix: " + juce::String(first_ + 1) +
                                               juce::String::fromUTF8("–") +
                                               juce::String(first_ + rowsShown) + " of " +
                                               juce::String(static_cast<int>(listed_.size()))
                                         : juce::String("Matrix"));
        repaint();
    }

    DeviceUiContext& ctx_;
    SynthModulation<T>& modulation_;
    IconButton add_{Icon::Plus};
    std::vector<std::unique_ptr<juce::Component>> controls_;
    std::vector<int> listed_;
    int first_ = 0;
};
} // namespace fd::ui::synth
