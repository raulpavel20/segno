#include "FilterPanel.h"

#include "Filter.h"

#include <utility>

namespace fd::devices
{
namespace
{
using F = Filter;

/// The response hardly depends on the sample rate where the display draws it.
constexpr double kDisplayRate = 48000.0;
constexpr double kTopDb = 24.0, kBottomDb = -36.0;                            // the response's scale
constexpr float kSpecTopDb = 0.0f, kSpecBottomDb = -90.0f, kSpecSlope = 4.5f; // the spectrum's, as the EQ's
constexpr const char* kVowels[] = {"a", "e", "i", "o", "u"};

F::TypeKind typeOf(const ui::DeviceUiContext& ctx)
{
    return static_cast<F::TypeKind>(static_cast<int>(ctx.params.getPlain(F::Type)));
}

F::Settings settingsOf(const ui::DeviceUiContext& ctx)
{
    const auto& p = ctx.params;
    return {.type = typeOf(ctx),
            .circuit = static_cast<F::CircuitKind>(static_cast<int>(p.getPlain(F::Circuit))),
            .steep = static_cast<int>(p.getPlain(F::Slope)) == 1,
            .res = p.getPlain(F::Res) * 0.01,
            .morph = p.getPlain(F::Morph) * 0.01};
}

/// The main control's place now, as set (no modulation).
double placeOf(const ui::DeviceUiContext& ctx)
{
    const auto type = typeOf(ctx);
    return F::placeOf(type, ctx.params.getPlain(F::mainParam(type)));
}

/// How far the LFO and the envelope (while on) can move the main control, each way.
std::pair<double, double> reachOf(const ui::DeviceUiContext& ctx)
{
    const auto& p = ctx.params;
    const double lfo = p.getPlain(F::Lfo) >= 0.5f ? p.getPlain(F::LfoAmount) * 0.01 : 0.0;
    const double env = p.getPlain(F::Env) >= 0.5f ? p.getPlain(F::EnvAmount) * 0.01 : 0.0;
    const double place = placeOf(ctx);
    return {F::modulated(place, lfo, -1.0, std::min(env, 0.0), 1.0),
            F::modulated(place, lfo, 1.0, std::max(env, 0.0), 1.0)};
}

bool hasCutoff(F::TypeKind type)
{
    return type != F::TypeKind::Dj && type != F::TypeKind::Vowel;
}

juce::String hzText(double hz)
{
    return hz < 1000.0 ? juce::String(juce::roundToInt(hz)) + " Hz" : juce::String(hz / 1000.0, 2) + " kHz";
}

/// "244–369 Hz", "1.2–3.4 kHz" or "283 Hz–2.3 kHz": the unit once where both ends share it.
juce::String hzRange(double lo, double hi)
{
    const auto number = [](double hz)
    { return hz < 1000.0 ? juce::String(juce::roundToInt(hz)) : juce::String(hz / 1000.0, 1); };
    const auto unit = [](double hz) { return juce::String(hz < 1000.0 ? " Hz" : " kHz"); };
    const auto dash = juce::String::fromUTF8("\xE2\x80\x93");
    if ((lo < 1000.0) == (hi < 1000.0))
        return number(lo) + dash + number(hi) + unit(hi);
    return number(lo) + unit(lo) + dash + number(hi) + unit(hi);
}

/// "a", or "a–e" between two.
juce::String vowelText(double place)
{
    const double x = std::clamp(place, 0.0, 1.0) * 4.0;
    const int i = std::min(3, static_cast<int>(x));
    const double t = x - i;
    if (t < 0.15)
        return kVowels[i];
    if (t > 0.85)
        return kVowels[i + 1];
    return juce::String(kVowels[i]) + juce::String::fromUTF8("\xE2\x80\x93") + kVowels[i + 1];
}

/// The response (SPEC: UI layout): each side's, where the modulation puts it now, over the output's
/// spectrum, with the modulation's reach shaded and a handle at the cutoff. Its line reads the main
/// control. Drag sideways for the main control (Freq, Sweep or Vowel), up and down for Resonance.
class ResponseDisplay final : public ui::MiniDisplay, public juce::SettableTooltipClient
{
public:
    explicit ResponseDisplay(ui::DeviceUiContext& ctx)
        : MiniDisplay(ctx, {},
                      {F::Type, F::Circuit, F::Slope, F::Freq, F::Res, F::Sweep, F::Vowel, F::Morph, F::Lfo,
                       F::LfoAmount, F::Env, F::EnvAmount}),
          ctx_(ctx)
    {
        setInterceptsMouseClicks(true, false);
        setMouseCursor(juce::MouseCursor::UpDownLeftRightResizeCursor);
        setTooltip("Drag sideways to move the filter, up or down for its resonance (Shift: finely)");
        setTitle("Response");
        update();
    }

    void refresh() override
    {
        MiniDisplay::refresh();
        if (auto* bus = ctx_.analysis)
        {
            spectrum_.pull(bus->fifo(F::OutputFifo), bus->sampleRate());
            const std::array<float, 2> now{bus->read(F::MainLeft), bus->read(F::MainRight)};
            if (now != now_ || spectrum_.hasData())
            {
                now_ = now;
                repaint();
            }
        }
        update();
    }

    void mouseDown(const juce::MouseEvent& e) override
    {
        main_ = F::mainParam(typeOf(ctx_));
        start_ = e.position;
        startMain_ = spec(main_).toNormalized(ctx_.params.getPlain(main_));
        startRes_ = spec(F::Res).toNormalized(ctx_.params.getPlain(F::Res));
        ctx_.params.beginGesture(main_);
        ctx_.params.beginGesture(F::Res);
    }

    void mouseDrag(const juce::MouseEvent& e) override
    {
        const auto& input = ui::Theme::current().input;
        const float scale = (e.mods.isShiftDown() ? input.fineFactor : 1.0f) / input.valueTravel;
        const auto to = [this](size_t param, float normalized)
        { ctx_.params.setPlain(param, spec(param).fromNormalized(std::clamp(normalized, 0.0f, 1.0f))); };
        to(main_, startMain_ + (e.position.x - start_.x) * scale);
        to(F::Res, startRes_ + (start_.y - e.position.y) * scale);
        refresh();
    }

    void mouseUp(const juce::MouseEvent&) override
    {
        ctx_.params.endGesture(main_);
        ctx_.params.endGesture(F::Res);
    }

private:
    [[nodiscard]] const ParamSpec& spec(size_t param) const { return ctx_.info.params[param]; }

    /// The readout: the main control, or the range the modulation covers.
    void update()
    {
        const auto type = typeOf(ctx_);
        const auto [lo, hi] = reachOf(ctx_);
        juce::String text;
        if (type == F::TypeKind::Dj)
        {
            const double sweep = ctx_.params.getPlain(F::Sweep) / 100.0;
            const auto dj = F::djFilter(sweep, kDisplayRate);
            text = dj.amount < 1.0 ? juce::String("flat")
                                   : (dj.highPass ? "high pass " : "low pass ") + hzText(dj.hz);
        }
        else if (type == F::TypeKind::Vowel)
            text = hi - lo > 1e-6 ? vowelText(lo) + " to " + vowelText(hi) : vowelText(placeOf(ctx_));
        else if (hi - lo > 1e-6)
            text = hzRange(F::valueAt(type, lo), F::valueAt(type, hi));
        else
            text = hzText(F::valueAt(type, placeOf(ctx_)));
        if (text != readout_)
        {
            readout_ = text;
            setDescription(readout_);
            repaint();
        }
    }

    void drawContent(juce::Graphics& g, juce::Rectangle<float> area) override
    {
        const auto& t = ui::Theme::current();
        const auto inset = static_cast<float>(t.deviceGrid.displayInset);
        const auto text = static_cast<float>(t.deviceGrid.displayTextHeight);
        g.setFont(ui::font(ui::TextStyle::ValueSmall));
        g.setColour(t.textSecondary);
        g.drawText(readout_, area.removeFromTop(text), juce::Justification::centredLeft, false);
        const auto plot = area.withTrimmedTop(inset / 2.0f).reduced(0.0f, inset / 2.0f);
        const ui::SpectrumPlot axis{.area = plot};

        // The output's spectrum, faint, behind everything (as the EQ's).
        if (spectrum_.hasData())
        {
            const ui::SpectrumPlot spec{
                .area = plot, .topDb = kSpecTopDb, .bottomDb = kSpecBottomDb, .slope = kSpecSlope};
            const auto line = spec.line(spectrum_.bands());
            g.setColour(t.roles.spectrumFill);
            g.fillPath(spec.fill(line));
            ui::SpectrumPlot::stroke(g, line, t.roles.spectrumLine);
        }

        const auto s = settingsOf(ctx_);
        const auto yOf = [&](double level)
        {
            const double d = std::clamp(20.0 * std::log10(std::max(level, 1e-6)), kBottomDb, kTopDb);
            return plot.getY() + static_cast<float>((kTopDb - d) / (kTopDb - kBottomDb)) * plot.getHeight();
        };
        // How far the modulation reaches: the band of cutoffs it covers.
        const auto [lo, hi] = reachOf(ctx_);
        if (hasCutoff(s.type) && hi - lo > 1e-6)
        {
            const float x0 = axis.xOf(F::valueAt(s.type, lo)), x1 = axis.xOf(F::valueAt(s.type, hi));
            g.setColour(t.fill(t.value));
            g.fillRect(juce::Rectangle<float>(x0, plot.getY(), std::max(1.0f, x1 - x0), plot.getHeight()));
        }
        g.setColour(t.surface3);
        g.drawHorizontalLine(juce::roundToInt(yOf(1.0)), plot.getX(), plot.getRight());

        const double place = placeOf(ctx_);
        if (s.type == F::TypeKind::Resampling)
        {
            // Below Freq the sound is as it was; above it, what folds back.
            const float x = axis.xOf(F::valueAt(s.type, place));
            g.setColour(t.fill(t.value));
            g.fillRect(juce::Rectangle<float>(x, plot.getY(), plot.getRight() - x, plot.getHeight()));
            g.setColour(t.value);
            g.drawLine(plot.getX(), yOf(1.0), x, yOf(1.0), 1.5f);
        }
        else
        {
            const int n = std::max(16, static_cast<int>(plot.getWidth()));
            const float step = plot.getWidth() / static_cast<float>(n);
            // The right side only where the modulation can take it elsewhere than the left.
            for (const int side : moving() ? std::vector<int>{1, 0} : std::vector<int>{0})
            {
                // Where the modulation puts each side now; as set where nothing plays it.
                const double at =
                    ctx_.analysis != nullptr && moving() ? now_[static_cast<size_t>(side)] : place;
                // A comb's teeth get narrower than a pixel up high: each column draws the highest
                // and lowest the response reaches across it, the band between them filled.
                const double pitch = s.type == F::TypeKind::Comb
                                         ? std::clamp(F::valueAt(s.type, at), F::lowestHz, F::combHighestHz)
                                         : 0.0;
                juce::Path top, band;
                std::vector<juce::Point<float>> bottom;
                for (int i = 0; i <= n; ++i)
                {
                    const float x = plot.getX() + static_cast<float>(i) * step;
                    const double from = axis.hzAt(x - 0.5f * step), to = axis.hzAt(x + 0.5f * step);
                    double most = 0.0, least = 1e9;
                    const auto take = [&](double hz)
                    {
                        const double level = F::magnitude(s, at, hz, kDisplayRate);
                        most = std::max(most, level);
                        least = std::min(least, level);
                    };
                    take(axis.hzAt(x));
                    if (pitch > 0.0)
                    {
                        // The comb repeats every pitch, its peaks and troughs on the half pitches.
                        // Where its teeth come closer than a third of a column, the column takes
                        // a whole period's highest and lowest (the next peak and trough).
                        const double half = 0.5 * pitch, first = std::ceil(from / half) * half;
                        const bool dense = to - from >= pitch / 3.0;
                        for (double hz = first; hz < first + pitch && (dense || hz <= to); hz += half)
                            take(hz);
                        take(from);
                        take(to);
                    }
                    const juce::Point<float> pt(x, yOf(most));
                    i == 0 ? top.startNewSubPath(pt) : top.lineTo(pt);
                    bottom.emplace_back(x, yOf(least));
                }
                if (pitch > 0.0)
                {
                    band = top;
                    for (auto it = bottom.rbegin(); it != bottom.rend(); ++it)
                        band.lineTo(*it);
                    band.closeSubPath();
                    g.setColour(t.fill(side == 0 ? t.value : t.textSecondary));
                    g.fillPath(band);
                }
                g.setColour(side == 0 ? t.value : t.textSecondary);
                g.strokePath(top, juce::PathStrokeType(1.5f, juce::PathStrokeType::curved));
            }
        }

        // The handle, at the cutoff as set.
        if (hasCutoff(s.type))
        {
            const double hz = F::valueAt(s.type, place);
            const double level =
                s.type == F::TypeKind::Resampling ? 1.0 : F::magnitude(s, place, hz, kDisplayRate);
            const auto handle = static_cast<float>(t.deviceGrid.displayHandle);
            // Kept whole inside the plot: in a notch, or at either end of the range.
            const auto inside = plot.reduced(0.5f * handle);
            const juce::Point<float> centre(std::clamp(axis.xOf(hz), inside.getX(), inside.getRight()),
                                            std::clamp(yOf(level), inside.getY(), inside.getBottom()));
            const auto box = juce::Rectangle<float>(handle, handle).withCentre(centre);
            g.setColour(isMouseButtonDown() || isMouseOver() ? t.active : t.surface3);
            g.fillEllipse(box);
            if (!isMouseButtonDown() && !isMouseOver())
            {
                g.setColour(t.value);
                g.drawEllipse(box.reduced(0.5f), 1.2f);
            }
        }
    }

    /// Whether the LFO or the envelope can move the filter.
    [[nodiscard]] bool moving() const
    {
        const auto [lo, hi] = reachOf(ctx_);
        return hi - lo > 1e-6;
    }

    void mouseEnter(const juce::MouseEvent&) override { repaint(); }
    void mouseExit(const juce::MouseEvent&) override { repaint(); }

    ui::DeviceUiContext& ctx_;
    ui::FifoSpectrum spectrum_;
    juce::String readout_;
    std::array<float, 2> now_{}; // each side's main control now
    size_t main_ = F::Freq;
    juce::Point<float> start_;
    float startMain_ = 0.0f, startRes_ = 0.0f;
};

/// Sections per devices/filter/SPEC.md, on the module grid (docs/design/device-panels.md).
class FilterPanel final : public ui::DevicePanel
{
public:
    explicit FilterPanel(ui::DeviceUiContext& ctx) : DevicePanel(ctx)
    {
        auto& filter = addSection("filter", "Filter", 6);
        filter.place(own(std::make_unique<ResponseDisplay>(ctx)), 0, 0, 6, 3);
        auto& main = filter.knob(F::Freq, 0, 3);
        auto& res = filter.knob(F::Res, 1, 3);
        auto& morph = filter.knob(F::Morph, 2, 3);
        filter.knob(F::Drive, 3, 3);
        filter.combo(F::Type, 4, 3, 2);
        auto& circuit = filter.combo(F::Circuit, 4, 4, 2);
        auto& slope = filter.segmented(F::Slope, 4, 5, 2, 1);
        // Freq, or the DJ filter's Sweep, or the Vowel.
        main.follow([&ctx] { return F::mainParam(typeOf(ctx)); });

        auto& lfo = addSection("lfo", "LFO", 3, F::Lfo);
        auto& rate = lfo.knob(F::LfoRate, 0, 0);
        lfo.knob(F::LfoAmount, 1, 0);
        auto& stereo = lfo.knob(F::LfoOffset, 2, 0);
        lfo.combo(F::LfoShape, 0, 3, 2);
        lfo.toggle(F::LfoSync, 2, 3);
        lfo.segmented(F::LfoStereo, 0, 4, 2, 1);
        auto& phase = lfo.bar(F::LfoPhase, 0, 5, 3);
        // The rate in Hz, or as a note value while Sync is on; Offset, or Spin.
        rate.follow([&ctx] { return ctx.params.getPlain(F::LfoSync) >= 0.5f ? F::LfoDiv : F::LfoRate; });
        stereo.follow(
            [&ctx]
            { return static_cast<int>(ctx.params.getPlain(F::LfoStereo)) == 1 ? F::LfoSpin : F::LfoOffset; });

        auto& envelope = addSection("envelope", "Envelope", 3, F::Env);
        envelope.knob(F::EnvAmount, 0, 0);
        envelope.knob(F::EnvAttack, 1, 0);
        envelope.knob(F::EnvRelease, 2, 0);

        auto& output = addSection("output", "Output", 2);
        output.knob(F::Mix, 0, 0);
        output.knob(F::Output, 1, 0);
        output.toggle(F::Clip, 0, 3, 2);

        using TK = F::TypeKind;
        dimWhen({&res}, [&ctx] { return typeOf(ctx) == TK::Resampling; });
        dimWhen({&morph},
                [&ctx]
                {
                    const auto t = typeOf(ctx);
                    return t != TK::Morph && t != TK::Comb && t != TK::Vowel && t != TK::NotchLowPass;
                });
        dimWhen({&circuit, &slope},
                [&ctx]
                {
                    const auto t = typeOf(ctx);
                    return t == TK::Comb || t == TK::Vowel || t == TK::Resampling;
                });
        dimWhen({&phase}, [&ctx] { return ctx.params.getPlain(F::LfoSync) < 0.5f; });

        if (auto* bus = ctx.analysis)
        {
            meterIn_.setLevelSource([bus](int c) { return bus->takePeak(c == 0 ? F::PeakInL : F::PeakInR); });
            meterOut_.setLevelSource([bus](int c)
                                     { return bus->takePeak(c == 0 ? F::PeakOutL : F::PeakOutR); });
        }
        setMeters({{&meterIn_, "In"}, {&meterOut_, "Out"}});
    }

private:
    ui::Meter meterIn_{2}, meterOut_{2};
};
} // namespace

const ui::PanelFactory& filterPanelFactory()
{
    static const ui::PanelFactory factory{
        .deviceId = "filter",
        .create = [](ui::DeviceUiContext& ctx) -> std::unique_ptr<ui::DevicePanel>
        { return std::make_unique<FilterPanel>(ctx); },
    };
    return factory;
}
} // namespace fd::devices
