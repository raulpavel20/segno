#include "EqPanel.h"

#include "Eq.h"
#include "fd/dsp/Spectrum.h"

namespace fd::devices
{
namespace
{
constexpr double kMinHz = 20.0, kMaxHz = 20000.0;
constexpr double kRangeDb = 18.0;                     // curve: +-18 dB
constexpr double kSpecTopDb = 0.0, kSpecBottomDb = -90.0; // spectrum scale
constexpr float kSpecSlope = 4.5f;                        // dB/oct around 1 kHz, as before
constexpr double kDisplaySampleRate = 48000.0;        // curves are drawn at a reference rate

bool usesGain(Eq::BandType t)
{
    return t == Eq::BandType::Bell || t == Eq::BandType::LowShelf || t == Eq::BandType::HighShelf ||
           t == Eq::BandType::Tilt;
}

/// Shared state between the display and the band editor.
struct Selection
{
    int band = 1;
    std::function<void()> changed;
};

// ---------------------------------------------------------------------------
// The display: spectrum, response curve, draggable band nodes.
// ---------------------------------------------------------------------------
class EqDisplay final : public juce::Component, public juce::SettableTooltipClient, public ui::ParamControl
{
public:
    EqDisplay(ui::DeviceUiContext& ctx, Selection& sel) : ctx_(ctx), sel_(sel)
    {
        setTooltip("Double-click to add a band. Drag nodes; wheel = Q; right-click for type, slope, placement.");
    }

    void refresh() override
    {
        // Re-design only when band parameters changed.
        std::vector<float> snapshot(static_cast<size_t>(Eq::Output) + 1);
        for (size_t i = 0; i < snapshot.size(); ++i)
            snapshot[i] = ctx_.params.getPlain(i);
        snapshot.push_back(ctx_.params.getPlain(Eq::Scale));
        if (snapshot != lastParams_)
        {
            lastParams_ = std::move(snapshot);
            for (int b = 0; b < Eq::numBands; ++b)
                sections_[static_cast<size_t>(b)] = Eq::design(settings(b), kDisplaySampleRate);
            repaint();
        }
        tickAnalyzer(); // the frame clock drives it on screen; this keeps it going where there's none
    }

    void tickAnalyzer()
    {
        if (ctx_.analysis == nullptr)
            return;
        const int mode = static_cast<int>(ctx_.params.getPlain(Eq::Analyzer));
        if (mode == 0)
            return;
        pre_.pull(ctx_.analysis->fifo(Eq::PreFifo), ctx_.analysis->sampleRate());
        post_.pull(ctx_.analysis->fifo(Eq::PostFifo), ctx_.analysis->sampleRate());
        repaint();
    }

    void paint(juce::Graphics& g) override
    {
        const auto& t = ui::Theme::current();
        const auto r = getLocalBounds().toFloat();
        g.setColour(t.surface0);
        g.fillRoundedRectangle(r, t.radiusControl);

        // Grid: the analysers' frequencies, and 0 dB.
        spectrumPlot().paintGrid(g);
        g.setColour(t.surface3);
        g.fillRect(0.0f, yOfDb(0.0), r.getWidth(), 1.0f);

        // Spectrum (flat).
        const int mode = static_cast<int>(ctx_.params.getPlain(Eq::Analyzer));
        if (ctx_.analysis != nullptr && (mode == 2 || mode == 3))
            paintSpectrum(g, post_, t.roles.spectrumFill, t.roles.spectrumLine);
        if (ctx_.analysis != nullptr && (mode == 1 || mode == 3))
            paintSpectrum(g, pre_, juce::Colours::transparentBlack, t.roles.spectrumPre);

        // Selected band's own curve.
        if (isOn(sel_.band))
        {
            g.setColour(t.roles.selectionCurve);
            g.strokePath(curvePath({sel_.band}), juce::PathStrokeType(1.0f));
        }

        // Total curve: solid line + flat translucent fill to 0 dB (ADR 0014).
        std::vector<int> all(Eq::numBands);
        std::iota(all.begin(), all.end(), 0);
        auto curve = curvePath(all);
        juce::Path fill(curve);
        fill.lineTo(r.getWidth(), yOfDb(0.0));
        fill.lineTo(0.0f, yOfDb(0.0));
        fill.closeSubPath();
        g.setColour(t.roles.curveFill);
        g.fillPath(fill);
        g.setColour(t.value);
        g.strokePath(curve, juce::PathStrokeType(1.5f));

        // Nodes.
        for (int b = 0; b < Eq::numBands; ++b)
        {
            if (!isOn(b))
                continue;
            const auto c = nodePos(b);
            const bool selected = b == sel_.band;
            const auto box = juce::Rectangle<float>(13.0f, 13.0f).withCentre(c);
            g.setColour(selected ? t.active : t.surface3);
            g.fillEllipse(box);
            g.setColour(selected ? t.onActive : t.value);
            if (!selected)
                g.drawEllipse(box.reduced(0.5f), 1.2f);
            g.setFont(ui::font(ui::TextStyle::Micro));
            g.setColour(selected ? t.onActive : t.textPrimary);
            g.drawText(juce::String(b + 1), box, juce::Justification::centred, false);
        }

        if (!hoverText_.isEmpty())
        {
            g.setColour(t.textSecondary);
            g.setFont(ui::font(ui::TextStyle::Value));
            g.drawText(hoverText_, r.reduced(6.0f, 4.0f), juce::Justification::topRight, false);
        }
    }

    void mouseDown(const juce::MouseEvent& e) override
    {
        dragBand_ = hitNode(e.position);
        if (dragBand_ >= 0)
        {
            select(dragBand_);
            if (e.mods.isPopupMenu())
            {
                showMenu(dragBand_);
                dragBand_ = -1;
                return;
            }
            ctx_.params.beginGesture(Eq::paramIndex(dragBand_, Eq::Freq));
            ctx_.params.beginGesture(Eq::paramIndex(dragBand_, Eq::Gain));
            dragStart_ = e.position;
            startFreq_ = ctx_.params.getPlain(Eq::paramIndex(dragBand_, Eq::Freq));
            startGain_ = ctx_.params.getPlain(Eq::paramIndex(dragBand_, Eq::Gain));
        }
    }

    void mouseDrag(const juce::MouseEvent& e) override
    {
        if (dragBand_ < 0)
            return;
        const float fine = e.mods.isShiftDown() ? 0.1f : 1.0f;
        const auto d = (e.position - dragStart_) * fine;
        const double f = freqOf(xOf(startFreq_) + d.x);
        ctx_.params.setPlain(Eq::paramIndex(dragBand_, Eq::Freq), static_cast<float>(f));
        if (usesGain(typeOf(dragBand_)))
            ctx_.params.setPlain(Eq::paramIndex(dragBand_, Eq::Gain),
                                 static_cast<float>(dbOf(yOfDb(startGain_) + d.y)));
        updateHover(dragBand_);
        refresh();
    }

    void mouseUp(const juce::MouseEvent&) override
    {
        if (dragBand_ < 0)
            return;
        ctx_.params.endGesture(Eq::paramIndex(dragBand_, Eq::Freq));
        ctx_.params.endGesture(Eq::paramIndex(dragBand_, Eq::Gain));
        dragBand_ = -1;
    }

    void mouseMove(const juce::MouseEvent& e) override
    {
        const int b = hitNode(e.position);
        if (b >= 0)
            updateHover(b);
        else
            hoverText_ = juce::String(freqOf(e.position.x), 0) + " Hz";
        repaint();
    }

    void mouseExit(const juce::MouseEvent&) override
    {
        hoverText_.clear();
        repaint();
    }

    void mouseDoubleClick(const juce::MouseEvent& e) override
    {
        if (const int b = hitNode(e.position); b >= 0)
        {
            set(Eq::paramIndex(b, Eq::On), 0.0f); // remove (switch off)
            return;
        }
        for (int b = 0; b < Eq::numBands; ++b)
            if (!isOn(b))
            {
                set(Eq::paramIndex(b, Eq::Type), static_cast<float>(Eq::BandType::Bell));
                set(Eq::paramIndex(b, Eq::Freq), static_cast<float>(freqOf(e.position.x)));
                set(Eq::paramIndex(b, Eq::Gain), static_cast<float>(dbOf(e.position.y)));
                set(Eq::paramIndex(b, Eq::Q), 0.71f);
                set(Eq::paramIndex(b, Eq::Place), 0.0f);
                set(Eq::paramIndex(b, Eq::On), 1.0f);
                select(b);
                return;
            }
    }

    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override
    {
        const int b = hitNode(e.position) >= 0 ? hitNode(e.position) : sel_.band;
        if (!isOn(b))
            return;
        const auto qi = Eq::paramIndex(b, Eq::Q);
        const float factor = std::pow(1.15f, (w.deltaY != 0.0f ? w.deltaY : w.deltaX) * 4.0f);
        set(qi, ctx_.params.getPlain(qi) * factor);
        updateHover(b);
    }

private:
    Eq::BandSettings settings(int b) const
    {
        const float scale = ctx_.params.getPlain(Eq::Scale) * 0.01f;
        return {typeOf(b), ctx_.params.getPlain(Eq::paramIndex(b, Eq::Freq)),
                ctx_.params.getPlain(Eq::paramIndex(b, Eq::Gain)) * scale, ctx_.params.getPlain(Eq::paramIndex(b, Eq::Q)),
                static_cast<int>(ctx_.params.getPlain(Eq::paramIndex(b, Eq::Slope)))};
    }
    Eq::BandType typeOf(int b) const
    {
        return static_cast<Eq::BandType>(static_cast<int>(ctx_.params.getPlain(Eq::paramIndex(b, Eq::Type))));
    }
    bool isOn(int b) const { return b >= 0 && ctx_.params.getPlain(Eq::paramIndex(b, Eq::On)) >= 0.5f; }

    void set(size_t index, float value)
    {
        ctx_.params.beginGesture(index);
        ctx_.params.setPlain(index, value);
        ctx_.params.endGesture(index);
        refresh();
    }

    void select(int b)
    {
        if (sel_.band != b)
        {
            sel_.band = b;
            if (sel_.changed)
                sel_.changed();
            repaint();
        }
    }

    float xOf(double hz) const
    {
        return static_cast<float>(std::log(hz / kMinHz) / std::log(kMaxHz / kMinHz) * getWidth());
    }
    double freqOf(float x) const
    {
        return kMinHz * std::pow(kMaxHz / kMinHz, juce::jlimit(0.0, 1.0, static_cast<double>(x) / getWidth()));
    }
    float yOfDb(double db) const
    {
        return static_cast<float>(getHeight() * 0.5 - db / kRangeDb * getHeight() * 0.45);
    }
    double dbOf(float y) const { return juce::jlimit(-30.0, 30.0, (getHeight() * 0.5 - y) / (getHeight() * 0.45) * kRangeDb); }

    juce::Point<float> nodePos(int b) const
    {
        const auto s = settings(b);
        const double db = usesGain(s.type) ? s.gainDb : 0.0;
        return {xOf(s.freq), yOfDb(juce::jlimit(-kRangeDb * 1.1, kRangeDb * 1.1, db))};
    }

    int hitNode(juce::Point<float> p) const
    {
        for (int b = Eq::numBands; --b >= 0;)
            if (isOn(b) && nodePos(b).getDistanceFrom(p) < 9.0f)
                return b;
        return -1;
    }

    juce::Path curvePath(const std::vector<int>& bands) const
    {
        juce::Path p;
        for (int x = 0; x <= getWidth(); x += 2)
        {
            const double f = freqOf(static_cast<float>(x));
            double db = 0.0;
            for (int b : bands)
                if (isOn(b) && !Eq::isFlat(settings(b)))
                    db += 20.0 * std::log10(std::max(std::abs(Eq::response(sections_[static_cast<size_t>(b)], f,
                                                                            kDisplaySampleRate)),
                                                     1e-9));
            const auto pt = juce::Point<float>(static_cast<float>(x),
                                               yOfDb(juce::jlimit(-kRangeDb * 1.6, kRangeDb * 1.6, db)));
            x == 0 ? p.startNewSubPath(pt) : p.lineTo(pt);
        }
        return p;
    }

    /// The spectrum's axes: the display's width, -90 to 0 dB, tilted 4.5 dB/oct around 1 kHz.
    [[nodiscard]] ui::SpectrumPlot spectrumPlot() const
    {
        return {.area = getLocalBounds().toFloat(), .topDb = static_cast<float>(kSpecTopDb),
                .bottomDb = static_cast<float>(kSpecBottomDb), .slope = kSpecSlope};
    }

    void paintSpectrum(juce::Graphics& g, const ui::FifoSpectrum& a, juce::Colour fill,
                       juce::Colour line) const
    {
        if (!a.hasData())
            return;
        const auto plot = spectrumPlot();
        const auto path = plot.line(a.bands());
        if (!fill.isTransparent())
        {
            g.setColour(fill);
            g.fillPath(plot.fill(path));
        }
        ui::SpectrumPlot::stroke(g, path, line);
    }

    void updateHover(int b)
    {
        const auto s = settings(b);
        const auto& spec = ctx_.info.params[Eq::paramIndex(b, Eq::Freq)];
        hoverText_ = "Band " + juce::String(b + 1) + "  " + juce::String(spec.toText(static_cast<float>(s.freq)));
        if (usesGain(s.type))
            hoverText_ << "  " << juce::String(s.gainDb, 1) << " dB";
        hoverText_ << "  Q " << juce::String(s.q, 2);
    }

    void showMenu(int b)
    {
        juce::PopupMenu menu, types, slopes, places;
        const auto& tSpec = ctx_.info.params[Eq::paramIndex(b, Eq::Type)];
        for (size_t i = 0; i < tSpec.choices.size(); ++i)
            types.addItem(100 + static_cast<int>(i), juce::String(std::string(tSpec.choices[i])), true,
                          static_cast<int>(typeOf(b)) == static_cast<int>(i));
        const auto& sSpec = ctx_.info.params[Eq::paramIndex(b, Eq::Slope)];
        const int slope = static_cast<int>(ctx_.params.getPlain(Eq::paramIndex(b, Eq::Slope)));
        for (size_t i = 0; i < sSpec.choices.size(); ++i)
            slopes.addItem(200 + static_cast<int>(i), juce::String(std::string(sSpec.choices[i])) + "/oct", true,
                           slope == static_cast<int>(i));
        const auto& pSpec = ctx_.info.params[Eq::paramIndex(b, Eq::Place)];
        const int place = static_cast<int>(ctx_.params.getPlain(Eq::paramIndex(b, Eq::Place)));
        for (size_t i = 0; i < pSpec.choices.size(); ++i)
            places.addItem(300 + static_cast<int>(i), juce::String(std::string(pSpec.choices[i])), true,
                           place == static_cast<int>(i));
        menu.addSubMenu("Type", types);
        const bool cut = typeOf(b) == Eq::BandType::LowCut || typeOf(b) == Eq::BandType::HighCut;
        menu.addSubMenu("Slope", slopes, cut);
        menu.addSubMenu("Placement", places);
        menu.addSeparator();
        menu.addItem(1, "Remove band");
        menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this).withMousePosition(),
                           [this, b](int r) {
                               if (r == 1)
                                   set(Eq::paramIndex(b, Eq::On), 0.0f);
                               else if (r >= 300)
                                   set(Eq::paramIndex(b, Eq::Place), static_cast<float>(r - 300));
                               else if (r >= 200)
                                   set(Eq::paramIndex(b, Eq::Slope), static_cast<float>(r - 200));
                               else if (r >= 100)
                                   set(Eq::paramIndex(b, Eq::Type), static_cast<float>(r - 100));
                           });
    }

    ui::DeviceUiContext& ctx_;
    Selection& sel_;
    std::array<Eq::Sections, Eq::numBands> sections_{};
    std::vector<float> lastParams_;
    ui::FifoSpectrum pre_, post_; // one FIFO each, through the shared analyser
    int dragBand_ = -1;
    juce::Point<float> dragStart_;
    float startFreq_ = 1000.0f, startGain_ = 0.0f;
    juce::String hoverText_;
};

/// Sections per devices/eq/SPEC.md, on the module grid (docs/design/device-panels.md). The
/// BAND section shows the selected band; it is rebuilt when the selection changes.
class EqPanel final : public ui::DevicePanel
{
public:
    explicit EqPanel(ui::DeviceUiContext& ctx) : DevicePanel(ctx)
    {
        auto& curve = addSection("curve", "Curve", 10);
        display_ = &own(std::make_unique<EqDisplay>(ctx, selection_));
        curve.place(*display_, 0, 0, 10, 6);

        band_ = &addSection("band", "Band", 3);

        auto& output = addSection("output", "Output", 1);
        output.knob(Eq::Output, 0, 0);
        output.knob(Eq::Scale, 0, 3);

        addHeaderCombo(Eq::Analyzer);
        selection_.changed = [this] { rebuildBand(); };
        rebuildBand();
    }

private:
    void rebuildBand()
    {
        const int b = selection_.band;
        auto index = [b](Eq::BandParam p) { return Eq::paramIndex(b, p); };
        band_->clear();
        band_->setSectionTitle("Band " + juce::String(b + 1));
        band_->setPowerParam(index(Eq::On));

        band_->knob(index(Eq::Freq), 0, 0).setName("Freq");
        auto& gain = band_->knob(index(Eq::Gain), 1, 0);
        gain.setName("Gain");
        band_->knob(index(Eq::Q), 2, 0).setName("Q");
        band_->label("Type", 0, 3, 1);
        band_->combo(index(Eq::Type), 1, 3, 2);
        band_->label("Slope", 0, 4, 1);
        auto& slope = band_->combo(index(Eq::Slope), 1, 4, 2);
        band_->label("Place", 0, 5, 1);
        band_->combo(index(Eq::Place), 1, 5, 2);

        // Settings the band's type ignores dim instead of disappearing, so nothing moves.
        auto& ctx = context();
        auto type = [&ctx, index] { return static_cast<Eq::BandType>(static_cast<int>(ctx.params.getPlain(index(Eq::Type)))); };
        dimWhen({&gain}, [type] { return !usesGain(type()); });
        dimWhen({&slope}, [type] { return type() != Eq::BandType::LowCut && type() != Eq::BandType::HighCut; });
        resized();
    }

    void tickAnalyzer()
    {
        if (isShowing() && !findSection("curve")->isCollapsed())
            display_->tickAnalyzer();
    }

    juce::VBlankAttachment vblank_{this, [this] { tickAnalyzer(); }};
    Selection selection_;
    EqDisplay* display_ = nullptr;
    ui::PanelSection* band_ = nullptr;
};
} // namespace

const ui::PanelFactory& eqPanelFactory()
{
    static const ui::PanelFactory factory{
        .deviceId = "eq",
        .create = [](ui::DeviceUiContext& ctx) -> std::unique_ptr<ui::DevicePanel> {
            return std::make_unique<EqPanel>(ctx);
        },
    };
    return factory;
}
} // namespace fd::devices
