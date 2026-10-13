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
    /// The latest dynamics frame from the device (each band's gain now, then its trigger's level).
    std::array<float, Eq::dynamicsFrame> frame{};
    bool hasFrame = false;
};

float plain(const ui::DeviceUiContext& ctx, size_t index)
{
    return ctx.params.getPlain(index);
}

/// Whether these settings run the spectral engine (and so delay the sound): Eq::usesEngine, as
/// the panel sees the settings.
bool usesEngine(const ui::DeviceUiContext& ctx)
{
    if (static_cast<int>(plain(ctx, Eq::Phase)) == static_cast<int>(Eq::Processing::LinearPhase))
        return true;
    for (int b = 0; b < Eq::numBands; ++b)
        if (plain(ctx, Eq::spectralIndex(b, Eq::Spectral)) >= 0.5f)
            return true;
    return false;
}

/// The delay (ms) with the engine on or off at a Resolution, at the rate the EQ runs at.
double delayMs(const ui::DeviceUiContext& ctx, bool engine, int resolution)
{
    const double known = ctx.analysis != nullptr ? ctx.analysis->sampleRate() : 0.0;
    const double rate = known > 0.0 ? known : 48000.0;
    return 1000.0 * Eq::latencyFor(engine, resolution, rate) / rate;
}

/// "no delay", or "about 105 ms" (to 5 ms: the badge has it exactly).
juce::String delayText(double ms)
{
    return ms <= 0.0 ? juce::String("no delay")
                     : "about " + juce::String(juce::roundToInt(ms / 5.0) * 5) + " ms";
}

/// The sidechain's name, for "Make room for Kick" ("Sidechain" in a plugin).
juce::String sidechainName(const ui::DeviceUiContext& ctx)
{
    if (ctx.sidechain != nullptr)
        if (const auto sources = ctx.sidechain->sources();
            ctx.sidechain->selected() > 0 && ctx.sidechain->selected() < static_cast<int>(sources.size()) &&
            sources[static_cast<size_t>(ctx.sidechain->selected())] != "External")
            return juce::String(sources[static_cast<size_t>(ctx.sidechain->selected())]);
    return "Sidechain";
}

// ---------------------------------------------------------------------------
// The display: spectrum, response curve, draggable band nodes.
// ---------------------------------------------------------------------------
class EqDisplay final : public juce::Component, public juce::SettableTooltipClient, public ui::ParamControl
{
public:
    EqDisplay(ui::DeviceUiContext& ctx, Selection& sel) : ctx_(ctx), sel_(sel)
    {
        setTooltip(
            "Double-click to add a band. Drag nodes; wheel = Q; right-click for type, slope, placement. "
            "Alt+drag a node to listen to its band.");
        // Listen: the selected band's range alone (a transient setting: never kept).
        listen_.setClickingTogglesState(false);
        listen_.setTooltip("Play only the selected band's range, to find the frequency to fix");
        listen_.onClick = [this]
        { set(Eq::Listen, listen_.getToggleState() ? 0.0f : static_cast<float>(sel_.band + 1)); };
        addAndMakeVisible(listen_);
        // Make room: on a collision with the compared track.
        makeRoom_.setTooltip("Add a band that turns this track down here only while the other one plays");
        makeRoom_.onClick = [this] { makeRoom(); };
        addChildComponent(makeRoom_);
    }

    void resized() override
    {
        const auto& grid = ui::Theme::current().deviceGrid;
        listen_.setBounds(grid.displayInset, grid.displayInset, ui::Toggle::widthFor("Listen"), grid.row);
    }

    /// The selection moved: Listen follows it.
    void selectionChanged()
    {
        if (plain(ctx_, Eq::Listen) > 0.5f)
            set(Eq::Listen, static_cast<float>(sel_.band + 1));
        repaint();
    }

    void refresh() override
    {
        listen_.setToggleState(plain(ctx_, Eq::Listen) > 0.5f, juce::dontSendNotification);
        // Re-design only when parameters changed.
        std::vector<float> snapshot(static_cast<size_t>(Eq::NumParams));
        for (size_t i = 0; i < snapshot.size(); ++i)
            snapshot[i] = ctx_.params.getPlain(i);
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
        // The dynamics: the newest frame (the curve moves with it, the trigger meter reads it).
        auto& frames = ctx_.analysis->fifo(Eq::DynamicsFifo);
        while (frames.available() >= static_cast<size_t>(Eq::dynamicsFrame))
        {
            frames.pop(sel_.frame.data(), sel_.frame.size());
            sel_.hasFrame = true;
        }
        const int mode = static_cast<int>(ctx_.params.getPlain(Eq::Analyzer));
        if (mode != 0)
        {
            const double rate = ctx_.analysis->sampleRate();
            pre_.pull(ctx_.analysis->fifo(Eq::PreFifo), rate);
            post_.pull(ctx_.analysis->fifo(Eq::PostFifo), rate);
            auto& side = ctx_.analysis->fifo(Eq::SidechainFifo);
            if (side.available() > 0)
                sideSeen_ = juce::Time::getMillisecondCounterHiRes();
            side_.pull(side, rate);
        }
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
        // The compared track (the sidechain) and where the two collide (the masking display).
        runs_.clear();
        if (sideShown())
        {
            const auto& main = post_.hasData() ? post_ : pre_;
            if (main.hasData())
            {
                const auto collide = dsp::spectrumCollisions(main.bands(), side_.bands(), kSpecSlope);
                for (int b = 0; b < dsp::spectrum::numBands; ++b)
                    if (collide[static_cast<size_t>(b)])
                    {
                        if (!runs_.empty() && runs_.back().second == b - 1)
                            runs_.back().second = b;
                        else
                            runs_.emplace_back(b, b);
                    }
                g.setColour(t.roles.spectrumCollision);
                for (const auto& run : runs_)
                    g.fillRect(runArea(run));
            }
            ui::SpectrumPlot::stroke(g, spectrumPlot().line(side_.bands()), t.roles.spectrumCompare);
        }

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
        // Where the dynamic bands have moved it, now.
        if (anyMoved())
        {
            g.setColour(t.roles.valueHot);
            g.strokePath(curvePath(all, true), juce::PathStrokeType(1.0f));
        }
        // The selected dynamic band's range: a hollow handle at Gain + Range, joined to its node.
        if (const auto h = rangeHandle())
        {
            const auto node = nodePos(sel_.band);
            g.setColour(t.roles.selectionCurve);
            g.fillRect(
                juce::Rectangle<float>(node.x - 0.5f, std::min(node.y, h->y), 1.0f, std::abs(h->y - node.y)));
            const auto size = static_cast<float>(t.deviceGrid.displayHandle);
            const auto box = juce::Rectangle<float>(size, size).withCentre(*h);
            g.setColour(t.surface0);
            g.fillEllipse(box);
            g.setColour(rangeDrag_ ? t.active : t.value);
            g.drawEllipse(box.reduced(0.5f), 1.2f);
        }

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
        // The selected band's range handle first: it can sit over another node.
        if (const auto h = rangeHandle();
            h && h->getDistanceFrom(e.position) <=
                     static_cast<float>(ui::Theme::current().deviceGrid.displayHandle))
        {
            rangeDrag_ = true;
            const auto range = Eq::dynIndex(sel_.band, Eq::Range);
            ctx_.params.beginGesture(range);
            dragStart_ = e.position;
            startRange_ = plain(ctx_, range);
            return;
        }
        dragBand_ = hitNode(e.position);
        if (dragBand_ >= 0 && e.mods.isAltDown() && !e.mods.isPopupMenu())
        {
            // Alt+drag: listen to the band while dragging it.
            listenBefore_ = plain(ctx_, Eq::Listen);
            set(Eq::Listen, static_cast<float>(dragBand_ + 1));
        }
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
        if (rangeDrag_)
        {
            const float fine = e.mods.isShiftDown() ? 0.1f : 1.0f;
            const double moved = dbOf(yOfDb(0.0) + (e.position.y - dragStart_.y) * fine); // dB per the drag
            ctx_.params.setPlain(Eq::dynIndex(sel_.band, Eq::Range),
                                 juce::jlimit(-30.0f, 30.0f, startRange_ + static_cast<float>(moved)));
            refresh();
            return;
        }
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
        if (rangeDrag_)
        {
            ctx_.params.endGesture(Eq::dynIndex(sel_.band, Eq::Range));
            rangeDrag_ = false;
            repaint();
            return;
        }
        if (listenBefore_)
        {
            set(Eq::Listen, *listenBefore_);
            listenBefore_.reset();
        }
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
        showMakeRoom(e.position.x);
        repaint();
    }

    void mouseExit(const juce::MouseEvent&) override
    {
        hoverText_.clear();
        // Into the Make room button: it stays.
        if (!makeRoom_.isVisible() || !makeRoom_.getBounds().contains(getMouseXYRelative()))
            makeRoom_.setVisible(false);
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
                ctx_.params.beginEdit("Add band");
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
    /// Where a band is now: its settings, plus how far its dynamics moved it.
    Eq::BandSettings liveSettings(int b) const
    {
        auto s = settings(b);
        if (sel_.hasFrame)
            s.gainDb += sel_.frame[static_cast<size_t>(b)] * ctx_.params.getPlain(Eq::Scale) * 0.01f;
        return s;
    }
    bool anyMoved() const
    {
        if (!sel_.hasFrame)
            return false;
        for (int b = 0; b < Eq::numBands; ++b)
            if (isOn(b) && std::abs(sel_.frame[static_cast<size_t>(b)]) > 0.05f)
                return true;
        for (int j = 0; j < Eq::spectralPoints; ++j)
            if (std::abs(spectralAt(j)) > 0.05f)
                return true;
        return false;
    }
    /// The spectral bands' gain at a display point now (dB), from the dynamics frame.
    float spectralAt(int point) const
    {
        return sel_
            .frame[static_cast<size_t>(2 * Eq::numBands + std::clamp(point, 0, Eq::spectralPoints - 1))];
    }
    /// ...and at any frequency, between the points.
    double spectralDb(double hz) const
    {
        const double at = 12.0 * std::log2(std::max(hz, 1.0) / Eq::spectralPointHz(0));
        const int j = static_cast<int>(std::floor(at));
        if (j < 0 || j >= Eq::spectralPoints - 1)
            return j < 0 ? spectralAt(0) : spectralAt(Eq::spectralPoints - 1);
        const double f = at - j;
        return (1.0 - f) * spectralAt(j) + f * spectralAt(j + 1);
    }
    bool isDynamic(int b) const
    {
        return isOn(b) && plain(ctx_, Eq::dynIndex(b, Eq::Dyn)) >= 0.5f && Eq::canMove(typeOf(b));
    }
    /// The selected dynamic band's range handle: at Gain + Range.
    std::optional<juce::Point<float>> rangeHandle() const
    {
        if (!isDynamic(sel_.band))
            return std::nullopt;
        const auto s = settings(sel_.band);
        const double db =
            s.gainDb + plain(ctx_, Eq::dynIndex(sel_.band, Eq::Range)) * plain(ctx_, Eq::Scale) * 0.01;
        return juce::Point<float>(xOf(s.freq), yOfDb(juce::jlimit(-kRangeDb * 1.1, kRangeDb * 1.1, db)));
    }
    bool sideShown() const
    {
        return side_.hasData() && static_cast<int>(plain(ctx_, Eq::Analyzer)) != 0 &&
               juce::Time::getMillisecondCounterHiRes() - sideSeen_ < 500.0;
    }
    /// A run of colliding bands, on screen.
    juce::Rectangle<float> runArea(std::pair<int, int> run) const
    {
        const double half = std::pow(2.0, 1.0 / 24.0); // half a band each side
        const float x0 = xOf(dsp::spectrum::bandHz(run.first) / half),
                    x1 = xOf(dsp::spectrum::bandHz(run.second) * half);
        return {x0, 0.0f, std::max(1.0f, x1 - x0), static_cast<float>(getHeight())};
    }
    /// Over a collision: its Make room button, centred on it, above the frequency labels.
    void showMakeRoom(float x)
    {
        hovered_.reset();
        for (const auto& run : runs_)
            if (const auto a = runArea(run); x >= a.getX() && x <= a.getRight())
                hovered_ = run;
        if (!hovered_)
        {
            makeRoom_.setVisible(false);
            return;
        }
        const auto& grid = ui::Theme::current().deviceGrid;
        const auto text = "Make room for " + sidechainName(ctx_);
        makeRoom_.setButtonText(text);
        const int width = ui::Toggle::widthFor(text);
        const auto a = runArea(*hovered_);
        const int left = juce::jlimit(grid.displayInset, getWidth() - grid.displayInset - width,
                                      juce::roundToInt(a.getCentreX()) - width / 2);
        // Above the frequency labels along the bottom, an inset clear of them.
        makeRoom_.setBounds(left, getHeight() - grid.displayTextHeight - grid.displayInset - grid.row, width,
                            grid.row);
        makeRoom_.setVisible(true);
    }
    /// A dynamic bell where the two collide: it ducks this track there while the other plays.
    void makeRoom()
    {
        if (!hovered_)
            return;
        const auto run = *hovered_;
        int band = -1;
        for (int b = 0; b < Eq::numBands && band < 0; ++b)
            if (!isOn(b))
                band = b;
        if (band < 0)
            return; // every band in use
        const double low = dsp::spectrum::bandHz(run.first), high = dsp::spectrum::bandHz(run.second);
        const double octaves = std::max(1.0 / 3.0, std::log2(high / low) + 1.0 / 12.0);
        const double width = std::pow(2.0, octaves);
        const float q = static_cast<float>(juce::jlimit(0.4, 6.0, std::sqrt(width) / (width - 1.0)));
        // The other track's level there, the bands' powers summed (as a peak): 6 dB under it, as RMS.
        double power = 0.0;
        for (int b = run.first; b <= run.second; ++b)
            power += std::pow(10.0, side_.bands()[static_cast<size_t>(b)] / 10.0);
        const float threshold =
            juce::jlimit(-60.0f, 0.0f, static_cast<float>(10.0 * std::log10(power + 1e-12)) - 9.0f);
        ctx_.params.beginEdit("Make room");
        set(Eq::paramIndex(band, Eq::Type), static_cast<float>(Eq::BandType::Bell));
        set(Eq::paramIndex(band, Eq::Freq), static_cast<float>(std::sqrt(low * high)));
        set(Eq::paramIndex(band, Eq::Gain), 0.0f);
        set(Eq::paramIndex(band, Eq::Q), q);
        set(Eq::paramIndex(band, Eq::Place), 0.0f);
        set(Eq::dynIndex(band, Eq::Dyn), 1.0f);
        set(Eq::dynIndex(band, Eq::Range), -6.0f);
        set(Eq::dynIndex(band, Eq::AutoThreshold), 0.0f);
        set(Eq::dynIndex(band, Eq::Threshold), threshold);
        set(Eq::dynIndex(band, Eq::Trigger), static_cast<float>(Eq::TriggerSource::Sidechain));
        set(Eq::paramIndex(band, Eq::On), 1.0f);
        makeRoom_.setVisible(false);
        select(band);
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

    juce::Path curvePath(const std::vector<int>& bands, bool live = false) const
    {
        // Live: the bands where their dynamics have them now.
        std::array<Eq::Sections, Eq::numBands> moved{};
        if (live)
            for (int b : bands)
                if (isOn(b))
                    moved[static_cast<size_t>(b)] = Eq::design(liveSettings(b), kDisplaySampleRate);
        const auto& designs = live ? moved : sections_;
        juce::Path p;
        for (int x = 0; x <= getWidth(); x += 2)
        {
            const double f = freqOf(static_cast<float>(x));
            double db = live && sel_.hasFrame ? spectralDb(f) : 0.0; // the spectral bands, live
            for (int b : bands)
                if (isOn(b) && !Eq::isFlat(live ? liveSettings(b) : settings(b)))
                    db += 20.0 * std::log10(std::max(std::abs(Eq::response(designs[static_cast<size_t>(b)], f,
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
    ui::FifoSpectrum pre_, post_, side_;    // one FIFO each, through the shared analyser
    double sideSeen_ = 0.0;                 // when the sidechain last brought sound (ms)
    std::vector<std::pair<int, int>> runs_; // collisions: runs of bands
    std::optional<std::pair<int, int>> hovered_;
    ui::Toggle listen_{"Listen"}, makeRoom_;
    std::optional<float> listenBefore_; // Alt+drag listens; what it was before
    bool rangeDrag_ = false;
    float startRange_ = 0.0f;
    int dragBand_ = -1;
    juce::Point<float> dragStart_;
    float startFreq_ = 1000.0f, startGain_ = 0.0f;
    juce::String hoverText_;
};

/// The selected band's trigger: how loud its range is (from the device's dynamics frames) on a
/// -60 to 0 dB bar, with the threshold marked (unless Auto sets it).
class TriggerMeter final : public juce::Component, public juce::SettableTooltipClient, public ui::ParamControl
{
public:
    TriggerMeter(ui::DeviceUiContext& ctx, const Selection& sel) : ctx_(ctx), sel_(sel)
    {
        setTooltip(
            "How loud the band's trigger is now, and its threshold: the band moves while the bar passes it");
    }

    void refresh() override
    {
        const int b = sel_.band;
        const std::array<float, 3> now{
            sel_.hasFrame ? sel_.frame[static_cast<size_t>(Eq::numBands + b)] : -120.0f,
            plain(ctx_, Eq::dynIndex(b, Eq::Threshold)), plain(ctx_, Eq::dynIndex(b, Eq::AutoThreshold))};
        const auto rounded = [](const std::array<float, 3>& v)
        {
            return std::array<int, 3>{juce::roundToInt(v[0] * 4.0f), juce::roundToInt(v[1] * 4.0f),
                                      juce::roundToInt(v[2])};
        };
        if (rounded(now) != rounded(values_))
        {
            values_ = now;
            repaint();
        }
    }

    void paint(juce::Graphics& g) override
    {
        const auto& t = ui::Theme::current();
        const auto& grid = t.deviceGrid;
        g.setColour(t.surface0);
        g.fillRoundedRectangle(getLocalBounds().toFloat(), t.radiusControl);
        auto area = getLocalBounds().toFloat().reduced(static_cast<float>(grid.displayInset), 0.0f);
        g.setFont(ui::font(ui::TextStyle::Caption));
        const juce::String caption("Trigger");
        const float captionWidth = juce::GlyphArrangement::getStringWidth(g.getCurrentFont(), caption);
        g.setColour(t.textTertiary);
        g.drawText(caption, area.removeFromLeft(captionWidth), juce::Justification::centredLeft, false);
        area.removeFromLeft(static_cast<float>(grid.displayInset));
        const auto bar = area.withSizeKeepingCentre(area.getWidth(), static_cast<float>(grid.meterWidth));
        const auto xOf = [&bar](float db)
        { return bar.getX() + (std::clamp(db, -60.0f, 0.0f) + 60.0f) / 60.0f * bar.getWidth(); };
        g.setColour(t.surface3);
        g.fillRect(bar);
        if (values_[0] > -60.0f)
        {
            g.setColour(t.value);
            g.fillRect(bar.withRight(xOf(values_[0])));
        }
        if (values_[2] < 0.5f) // a set threshold
        {
            g.setColour(t.textPrimary);
            g.fillRect(juce::Rectangle<float>(xOf(values_[1]), bar.getY(), 1.0f, bar.getHeight()));
        }
    }

private:
    ui::DeviceUiContext& ctx_;
    const Selection& sel_;
    std::array<float, 3> values_{-120.0f, 0.0f, 1.0f};
};

/// What Auto Gain applies now, from the device ("-1.4 dB"; a dash while it's off).
class AutoGainReadout final : public juce::Component,
                              public juce::SettableTooltipClient,
                              public ui::ParamControl
{
public:
    explicit AutoGainReadout(ui::DeviceUiContext& ctx) : ctx_(ctx)
    {
        setTooltip("The gain Auto Gain applies, from the curve, so the loudness stays about the same");
    }

    void refresh() override
    {
        const bool on = plain(ctx_, Eq::AutoGain) >= 0.5f;
        const float db = ctx_.analysis != nullptr ? ctx_.analysis->read(Eq::AutoGainDb) : 0.0f;
        const auto text = on ? (db > 0.05f ? "+" : "") + juce::String(db, 1) + " dB" : ui::Readouts::none();
        if (text != text_)
        {
            text_ = text;
            repaint();
        }
    }

    void paint(juce::Graphics& g) override
    {
        const auto& t = ui::Theme::current();
        g.setColour(text_ == ui::Readouts::none() ? t.textTertiary : t.textSecondary);
        g.setFont(ui::font(ui::TextStyle::ValueSmall));
        g.drawText(text_, getLocalBounds(), juce::Justification::centred, false);
    }

private:
    ui::DeviceUiContext& ctx_;
    juce::String text_ = ui::Readouts::none();
};

/// Sections per devices/eq/SPEC.md, on the module grid (docs/design/device-panels.md). The
/// BAND and DYNAMICS sections show the selected band; they are rebuilt when the selection changes.
class EqPanel final : public ui::DevicePanel
{
public:
    explicit EqPanel(ui::DeviceUiContext& ctx) : DevicePanel(ctx)
    {
        auto& curve = addSection("curve", "Curve", 10);
        display_ = &own(std::make_unique<EqDisplay>(ctx, selection_));
        curve.place(*display_, 0, 0, 10, 6);

        band_ = &addSection("band", "Band", 3);
        dynamics_ = &addSection("dynamics", "Dynamics", 4);
        // One trigger meter, placed again with each band; it follows the selection itself.
        meter_ = &own(std::make_unique<TriggerMeter>(ctx, selection_));
        dimWhen({meter_},
                [this, &ctx]
                {
                    return !Eq::canMove(static_cast<Eq::BandType>(
                        static_cast<int>(plain(ctx, Eq::paramIndex(selection_.band, Eq::Type)))));
                });

        auto& output = addSection("output", "Output", 2);
        output.knob(Eq::Output, 0, 0);
        output.knob(Eq::Scale, 1, 0);
        auto& autoGain = output.toggle(Eq::AutoGain, 0, 3, 2);
        autoGain.setButtonText("Auto Gain");
        output.place(own(std::make_unique<AutoGainReadout>(ctx)), 0, 4, 2, 1, ui::PanelSection::Kind::Line);
        output.toggle(Eq::Delta, 0, 5, 2);

        addHeaderCombo(Eq::Analyzer);
        // Processing and Resolution name their delay where they're chosen, and a badge shows it
        // while there's any (SPEC, "Latency, shown wherever it's chosen").
        auto& processing = addHeaderCombo(Eq::Phase);
        processing.setMenuText(
            [&ctx](int choice)
            {
                const bool linear = choice == static_cast<int>(Eq::Processing::LinearPhase);
                const auto resolution = static_cast<int>(plain(ctx, Eq::Resolution));
                bool spectral = false;
                for (int b = 0; b < Eq::numBands; ++b)
                    spectral = spectral || plain(ctx, Eq::spectralIndex(b, Eq::Spectral)) >= 0.5f;
                return juce::String(
                           std::string(ctx.info.params[Eq::Phase].choices[static_cast<size_t>(choice)])) +
                       ": " + delayText(delayMs(ctx, linear || spectral, resolution));
            });
        auto& resolution = addHeaderCombo(Eq::Resolution);
        resolution.setMenuText(
            [&ctx](int choice)
            {
                return juce::String(std::string(
                           ctx.info.params[Eq::Resolution].choices[static_cast<size_t>(choice)])) +
                       ": " + delayText(delayMs(ctx, true, choice));
            });
        showHeaderWhen(resolution, [&ctx] { return usesEngine(ctx); }); // it matters only with a delay
        showLatency([&ctx]
                    { return delayMs(ctx, usesEngine(ctx), static_cast<int>(plain(ctx, Eq::Resolution))); });
        selection_.changed = [this]
        {
            rebuildBand();
            display_->selectionChanged();
        };
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

        // DYNAMICS: the selected band's (stage 2). Types without a gain can't move: all dimmed.
        auto dyn = [b](Eq::DynParam p) { return Eq::dynIndex(b, p); };
        dynamics_->clear();
        dynamics_->setPowerParam(dyn(Eq::Dyn));
        auto& range = dynamics_->knob(dyn(Eq::Range), 0, 0);
        auto& threshold = dynamics_->knob(dyn(Eq::Threshold), 1, 0);
        auto& attack = dynamics_->knob(dyn(Eq::Attack), 2, 0);
        auto& release = dynamics_->knob(dyn(Eq::Release), 3, 0);
        auto& spectral = dynamics_->toggle(Eq::spectralIndex(b, Eq::Spectral), 0, 3, 2);
        spectral.setButtonText("Spectral");
        spectral_ = &spectral;
        updateSpectralTooltip();
        auto& autoThreshold = dynamics_->toggle(dyn(Eq::AutoThreshold), 2, 3, 2);
        autoThreshold.setButtonText("Auto");
        // A spectral band's Detail, or any other band's trigger meter: each means nothing to the
        // other, so they share a row.
        spectralShown_ = plain(ctx, Eq::spectralIndex(b, Eq::Spectral)) >= 0.5f;
        juce::Component* detail = nullptr;
        if (spectralShown_)
        {
            auto& bar = dynamics_->bar(Eq::spectralIndex(b, Eq::Detail), 0, 4, 4);
            bar.setName("Detail");
            detail = &bar;
        }
        else
            dynamics_->place(*meter_, 0, 4, 4, 1, ui::PanelSection::Kind::Line);
        auto& trigger = dynamics_->segmented(dyn(Eq::Trigger), 0, 5, 4, 1);
        const auto still = [type] { return !Eq::canMove(type()); };
        dimWhen({&range, &attack, &release, &autoThreshold, &trigger}, still);
        dimWhen({&threshold},
                [still, &ctx, dyn] { return still() || plain(ctx, dyn(Eq::AutoThreshold)) >= 0.5f; });
        // Spectral works on bells and shelves.
        dimWhen({&spectral}, [type] { return !Eq::canBeSpectral(type()); });
        if (detail != nullptr)
            dimWhen({detail}, [type] { return !Eq::canBeSpectral(type()); });
        resized();
    }

    /// The Spectral switch says what it costs: the delay at this Quality (Resolution), unless
    /// there's one already.
    void updateSpectralTooltip()
    {
        auto& ctx = context();
        const auto resolution = static_cast<int>(plain(ctx, Eq::Resolution));
        const auto& spec = ctx.info.params[Eq::spectralIndex(selection_.band, Eq::Spectral)];
        const auto cost =
            usesEngine(ctx) && plain(ctx, Eq::spectralIndex(selection_.band, Eq::Spectral)) < 0.5f
                ? juce::String(" The EQ is delayed already, so it adds no more.")
                : " It delays the sound, " + delayText(delayMs(ctx, true, resolution)) + " at this Quality.";
        spectral_->setTooltip(juce::String(std::string(spec.name)) + ": " +
                              juce::String(std::string(spec.description)) + cost);
        tooltipResolution_ = resolution;
    }

    void tickAnalyzer()
    {
        // Spectral switched on or off: Detail and the trigger meter swap.
        auto& ctx = context();
        if ((plain(ctx, Eq::spectralIndex(selection_.band, Eq::Spectral)) >= 0.5f) != spectralShown_)
            rebuildBand();
        else if (static_cast<int>(plain(ctx, Eq::Resolution)) != tooltipResolution_)
            updateSpectralTooltip();
        if (isShowing() && !findSection("curve")->isCollapsed())
            display_->tickAnalyzer();
    }

    juce::VBlankAttachment vblank_{this, [this] { tickAnalyzer(); }};
    Selection selection_;
    EqDisplay* display_ = nullptr;
    ui::PanelSection* band_ = nullptr;
    ui::PanelSection* dynamics_ = nullptr;
    TriggerMeter* meter_ = nullptr;
    ui::ParamToggle* spectral_ = nullptr; // the selected band's Spectral switch
    bool spectralShown_ = false;          // DYNAMICS shows Detail (else the trigger meter)
    int tooltipResolution_ = -1;
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
