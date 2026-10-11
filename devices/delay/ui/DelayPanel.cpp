#include "DelayPanel.h"

#include "Delay.h"

#include <array>

namespace fd::devices
{
namespace
{
/// The tempo and bar the device last played at (120 and 4/4 where it hasn't: snapshots).
struct Song
{
    double tempo = 120.0, beatsPerBar = 4.0;
};

Song songOf(const ui::DeviceUiContext& ctx)
{
    Song s;
    if (ctx.analysis != nullptr)
    {
        if (const float t = ctx.analysis->read(Delay::TempoBpm); t > 0.0f)
            s.tempo = t;
        if (const float b = ctx.analysis->read(Delay::BeatsPerBar); b > 0.0f)
            s.beatsPerBar = b;
    }
    return s;
}

Delay::TimeSettings timeSettings(const ui::DeviceUiContext& ctx)
{
    const auto& p = ctx.params;
    return {p.getPlain(Delay::Sync) >= 0.5f,
            p.getPlain(Delay::TimeLeft),
            p.getPlain(Delay::TimeRight),
            static_cast<int>(p.getPlain(Delay::DivLeft)),
            static_cast<int>(p.getPlain(Delay::DivRight)),
            p.getPlain(Delay::Link) >= 0.5f,
            p.getPlain(Delay::Swing)};
}

/// The echoes on a time line (SPEC: UI layout): a bar for each, left above the line and right
/// below, at its level, from the same functions the audio path uses; the beat grid while synced.
/// Drag a side's first bar to change its time (snapping to note values while synced).
class RepeatsDisplay final : public ui::MiniDisplay, public juce::SettableTooltipClient
{
public:
    explicit RepeatsDisplay(ui::DeviceUiContext& ctx)
        : MiniDisplay(ctx, {},
                      {Delay::Sync, Delay::TimeLeft, Delay::TimeRight, Delay::DivLeft, Delay::DivRight,
                       Delay::Link, Delay::Swing, Delay::Stereo, Delay::Feedback, Delay::Filter,
                       Delay::FilterLow, Delay::FilterHigh, Delay::Mode}),
          ctx_(ctx)
    {
        setInterceptsMouseClicks(true, false);
        setMouseCursor(juce::MouseCursor::LeftRightResizeCursor);
        setTooltip("The echoes: drag a side's first one to change its time (Shift: finely)");
    }

    void refresh() override
    {
        MiniDisplay::refresh();
        const auto song = songOf(ctx_);
        if (song.tempo != song_.tempo || song.beatsPerBar != song_.beatsPerBar)
        {
            song_ = song;
            repaint();
        }
    }

    void mouseDown(const juce::MouseEvent& e) override
    {
        right_ = e.position.y > plot_.getCentreY() && ctx_.params.getPlain(Delay::Link) < 0.5f;
        dragHorizon_ = horizon_;
        startX_ = e.position.x;
        const auto t = Delay::times(timeSettings(ctx_), song_.tempo, song_.beatsPerBar);
        startSeconds_ = right_ ? t.right : t.left;
        ctx_.params.beginGesture(param());
    }

    void mouseDrag(const juce::MouseEvent& e) override
    {
        const float fine = e.mods.isShiftDown() ? 0.1f : 1.0f;
        const double seconds = std::max(0.001, startSeconds_ + (e.position.x - startX_) * fine /
                                                                   plot_.getWidth() * dragHorizon_);
        if (synced())
        {
            // The note value nearest the pointer (Swing aside: it moves the right side further).
            const double swing = right_ ? 1.0 + ctx_.params.getPlain(Delay::Swing) / 100.0 : 1.0;
            int best = 0;
            double bestError = 1e9;
            for (int i = 0; i < Delay::numDivisions; ++i)
            {
                const double s = Delay::divisionBeats(i, song_.beatsPerBar) * 60.0 / song_.tempo * swing;
                if (const double err = std::abs(std::log(s / seconds)); err < bestError)
                {
                    bestError = err;
                    best = i;
                }
            }
            ctx_.params.setPlain(param(), static_cast<float>(best));
        }
        else
            ctx_.params.setPlain(param(), static_cast<float>(std::clamp(seconds * 1000.0, 1.0, 5000.0)));
        refresh();
    }

    void mouseUp(const juce::MouseEvent&) override { ctx_.params.endGesture(param()); }

private:
    [[nodiscard]] bool synced() const { return ctx_.params.getPlain(Delay::Sync) >= 0.5f; }
    [[nodiscard]] size_t param() const
    {
        return synced() ? (right_ ? Delay::DivRight : Delay::DivLeft)
                        : (right_ ? Delay::TimeRight : Delay::TimeLeft);
    }

    void drawContent(juce::Graphics& g, juce::Rectangle<float> full) override
    {
        const auto& t = ui::Theme::current();
        // Which lane is which, in a gutter left of the time line.
        const auto& p = ctx_.params;
        const auto stereo = static_cast<Delay::StereoKind>(static_cast<int>(p.getPlain(Delay::Stereo)));
        const bool midSide = stereo == Delay::StereoKind::MidSide;
        const float gutter = juce::GlyphArrangement::getStringWidth(ui::capsLabelFont(), "M") +
                             static_cast<float>(t.deviceGrid.displayInset);
        const auto area = full.withTrimmedLeft(gutter);
        plot_ = area;
        ui::paintCapsLabel(g, midSide ? "M" : "L", full.withHeight(full.getHeight() * 0.5f),
                           juce::Justification::centredLeft);
        ui::paintCapsLabel(g, midSide ? "S" : "R", full.withTrimmedTop(full.getHeight() * 0.5f),
                           juce::Justification::centredLeft);

        const auto times = Delay::times(timeSettings(ctx_), song_.tempo, song_.beatsPerBar);
        const double pass =
            Delay::passGain(static_cast<Delay::ModeKind>(static_cast<int>(p.getPlain(Delay::Mode))),
                            p.getPlain(Delay::Filter) >= 0.5f, p.getPlain(Delay::FilterLow),
                            p.getPlain(Delay::FilterHigh), 48000.0);
        // At least four seconds, or eight repeats of the longer side.
        horizon_ = std::clamp(8.0 * std::max(times.left, times.right), 4.0, 40.0);
        std::array<Delay::Repeat, 64> repeats{};
        const int count =
            Delay::repeats(times, stereo, p.getPlain(Delay::Feedback) * 0.01, pass, horizon_, repeats);

        const auto xFor = [&](double seconds)
        { return area.getX() + static_cast<float>(seconds / horizon_) * area.getWidth(); };
        // The beat grid while synced: bars stronger than beats.
        if (synced())
        {
            const double beat = 60.0 / song_.tempo;
            const int beats = static_cast<int>(std::ceil(horizon_ / beat)) - 1; // none on the right edge
            for (int b = 1; b <= beats && beats <= 400; ++b)
            {
                const bool bar = std::fmod(b, song_.beatsPerBar) < 1e-6;
                g.setColour(bar ? t.roles.gridBar : t.roles.gridBeat);
                g.drawVerticalLine(juce::roundToInt(xFor(b * beat)), area.getY(), area.getBottom());
            }
        }
        g.setColour(t.surface3);
        g.drawHorizontalLine(juce::roundToInt(area.getCentreY()), area.getX(), area.getRight());
        // The echoes: left (or mid) up, right (or side) down.
        const float half = area.getHeight() * 0.5f - 1.0f;
        for (int i = 0; i < count; ++i)
        {
            const auto& r = repeats[static_cast<size_t>(i)];
            const float x = std::round(xFor(r.seconds));
            const float h = static_cast<float>(r.level) * half;
            g.setColour(t.value);
            if (r.lane == 0)
                g.drawLine(x, area.getCentreY() - 1.0f, x, area.getCentreY() - 1.0f - h, 2.0f);
            else
                g.drawLine(x, area.getCentreY() + 1.0f, x, area.getCentreY() + 1.0f + h, 2.0f);
        }
    }

    ui::DeviceUiContext& ctx_;
    Song song_;
    juce::Rectangle<float> plot_;
    double horizon_ = 4.0, dragHorizon_ = 4.0, startSeconds_ = 0.0;
    float startX_ = 0.0f;
    bool right_ = false;
};

/// One pass through the feedback filter (`FeedbackPath::magnitude`, as the audio path filters),
/// 20 Hz to 20 kHz; drag near a cutoff to move it.
class FilterCurve final : public ui::MiniDisplay, public juce::SettableTooltipClient
{
public:
    explicit FilterCurve(ui::DeviceUiContext& ctx)
        : MiniDisplay(ctx, {}, {Delay::Filter, Delay::FilterLow, Delay::FilterHigh}), ctx_(ctx)
    {
        setInterceptsMouseClicks(true, false);
        setMouseCursor(juce::MouseCursor::LeftRightResizeCursor);
        setTooltip("Drag near the low or the high edge to move that cutoff (Shift: finely)");
    }

    void mouseDown(const juce::MouseEvent& e) override
    {
        const float lowX = xFor(ctx_.params.getPlain(Delay::FilterLow));
        const float highX = xFor(ctx_.params.getPlain(Delay::FilterHigh));
        param_ = std::abs(e.position.x - lowX) <= std::abs(e.position.x - highX) ? Delay::FilterLow
                                                                                 : Delay::FilterHigh;
        start_ = ctx_.params.getPlain(param_);
        startX_ = e.position.x;
        ctx_.params.beginGesture(param_);
    }
    void mouseDrag(const juce::MouseEvent& e) override
    {
        const float fine = e.mods.isShiftDown() ? 0.1f : 1.0f;
        const double octaves = (e.position.x - startX_) * fine / area_.getWidth() * std::log2(1000.0);
        const auto& spec = ctx_.info.params[param_];
        ctx_.params.setPlain(param_,
                             std::clamp(static_cast<float>(start_ * std::exp2(octaves)), spec.min, spec.max));
        refresh();
    }
    void mouseUp(const juce::MouseEvent&) override { ctx_.params.endGesture(param_); }

private:
    [[nodiscard]] float xFor(double hz) const
    {
        return area_.getX() + static_cast<float>(std::log(hz / 20.0) / std::log(1000.0)) * area_.getWidth();
    }

    void drawContent(juce::Graphics& g, juce::Rectangle<float> area) override
    {
        area_ = area;
        constexpr double topDb = 3.0, bottomDb = -24.0, sampleRate = 48000.0;
        const auto& t = ui::Theme::current();
        const auto yFor = [&](double db)
        {
            return area.getY() +
                   static_cast<float>((topDb - std::clamp(db, bottomDb, topDb)) / (topDb - bottomDb)) *
                       area.getHeight();
        };
        g.setColour(t.surface3);
        g.drawHorizontalLine(juce::roundToInt(yFor(0.0)), area.getX(), area.getRight());
        const double low = ctx_.params.getPlain(Delay::FilterLow),
                     high = ctx_.params.getPlain(Delay::FilterHigh);
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
        const bool on = ctx_.params.getPlain(Delay::Filter) >= 0.5f;
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

    ui::DeviceUiContext& ctx_;
    juce::Rectangle<float> area_;
    size_t param_ = Delay::FilterLow;
    double start_ = 0.0;
    float startX_ = 0.0f;
};

/// Sections per devices/delay/SPEC.md, on the module grid (docs/design/device-panels.md).
class DelayPanel final : public ui::DevicePanel
{
public:
    explicit DelayPanel(ui::DeviceUiContext& ctx) : DevicePanel(ctx)
    {
        auto& time = addSection("time", "Time", 5);
        time.place(own(std::make_unique<RepeatsDisplay>(ctx)), 0, 0, 5, 3);
        auto& left = time.knob(Delay::TimeLeft, 0, 3);
        auto& right = time.knob(Delay::TimeRight, 1, 3);
        auto& swing = time.knob(Delay::Swing, 2, 3);
        time.toggle(Delay::Sync, 3, 3, 2);
        time.toggle(Delay::Link, 3, 4, 2);
        time.combo(Delay::Stereo, 3, 5, 2);
        // The times in milliseconds, or in note values while Sync is on.
        const auto synced = [&ctx] { return ctx.params.getPlain(Delay::Sync) >= 0.5f; };
        left.follow([synced] { return synced() ? Delay::DivLeft : Delay::TimeLeft; });
        right.follow([synced] { return synced() ? Delay::DivRight : Delay::TimeRight; });
        dimWhen({&right}, [&ctx] { return ctx.params.getPlain(Delay::Link) >= 0.5f; });
        dimWhen({&swing}, [synced] { return !synced(); });

        auto& feedback = addSection("feedback", "Feedback", 3);
        feedback.knob(Delay::Feedback, 0, 0);
        feedback.knob(Delay::Drive, 1, 0);
        feedback.knob(Delay::Wobble, 2, 0);
        feedback.segmented(Delay::Mode, 0, 3, 3, 1);
        feedback.toggle(Delay::Freeze, 0, 4, 3);

        auto& filter = addSection("filter", "Filter", 2, Delay::Filter);
        filter.place(own(std::make_unique<FilterCurve>(ctx)), 0, 0, 2, 3);
        filter.knob(Delay::FilterLow, 0, 3);
        filter.knob(Delay::FilterHigh, 1, 3);

        auto& modulation = addSection("modulation", "Modulation", 3);
        modulation.knob(Delay::ModRate, 0, 0);
        modulation.knob(Delay::ModTime, 1, 0);
        modulation.knob(Delay::ModFilter, 2, 0);
        modulation.segmented(Delay::ModShape, 0, 3, 3, 1);

        auto& ducking = addSection("ducking", "Ducking", 1);
        ducking.knob(Delay::Duck, 0, 0);
        ducking.knob(Delay::DuckRelease, 0, 3);

        auto& output = addSection("output", "Output", 1);
        output.knob(Delay::Mix, 0, 0);
        output.knob(Delay::Output, 0, 3);

        addHeaderCombo(Delay::Transition);

        if (auto* bus = ctx.analysis)
        {
            meterIn_.setLevelSource([bus](int c)
                                    { return bus->takePeak(c == 0 ? Delay::PeakInL : Delay::PeakInR); });
            meterOut_.setLevelSource([bus](int c)
                                     { return bus->takePeak(c == 0 ? Delay::PeakOutL : Delay::PeakOutR); });
        }
        setMeters({{&meterIn_, "In"}, {&meterOut_, "Out"}});
    }

private:
    ui::Meter meterIn_{2}, meterOut_{2};
};
} // namespace

const ui::PanelFactory& delayPanelFactory()
{
    static const ui::PanelFactory factory{
        .deviceId = "delay",
        .create = [](ui::DeviceUiContext& ctx) -> std::unique_ptr<ui::DevicePanel>
        { return std::make_unique<DelayPanel>(ctx); },
    };
    return factory;
}
} // namespace fd::devices
