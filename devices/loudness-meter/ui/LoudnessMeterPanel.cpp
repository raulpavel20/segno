#include "LoudnessMeterPanel.h"

#include "LoudnessMeter.h"
#include "fd/dsp/Loudness.h"

#include <cmath>

namespace fd::devices
{
namespace
{
using M = LoudnessMeter;

/// The scale the bars and the history share (EBU Mode): from the target -18 to +9 LU, or -36 to
/// +18; ticks every 3 LU or 6, labels every 6 or 12, in LUFS or LU.
struct LoudnessScale
{
    float target = -14.0f;
    bool wide = false;     // +18
    bool relative = false; // LU

    [[nodiscard]] float low() const noexcept { return target - (wide ? 36.0f : 18.0f); }
    [[nodiscard]] float high() const noexcept { return target + (wide ? 18.0f : 9.0f); }
    [[nodiscard]] float labelStep() const noexcept { return wide ? 12.0f : 6.0f; }
    [[nodiscard]] float yOf(juce::Rectangle<float> area, float lufs) const noexcept
    {
        const float k = (std::clamp(lufs, low(), high()) - low()) / (high() - low());
        return area.getBottom() - k * area.getHeight();
    }
    /// A loudness in the units shown, with its unit: "-14.2 LUFS", "+0.8 LU".
    [[nodiscard]] juce::String text(float lufs) const
    {
        if (!std::isfinite(lufs) || lufs <= -70.0f)
            return ui::Readouts::none();
        if (!relative)
            return juce::String(lufs, 1) + " LUFS";
        const float lu = lufs - target;
        return (lu > 0.05f ? "+" : "") + juce::String(lu, 1) + " LU";
    }
    /// A tick's label: the loudness or its distance from the target, whole.
    [[nodiscard]] juce::String label(float lufs) const
    {
        const auto v = static_cast<int>(std::lround(relative ? lufs - target : lufs));
        return (relative && v > 0 ? "+" : "") + juce::String(v);
    }
};

LoudnessScale scaleOf(const ui::DeviceUiContext& ctx)
{
    return {ctx.params.getPlain(M::Target), ctx.params.getPlain(M::Scale) >= 0.5f,
            ctx.params.getPlain(M::Units) >= 0.5f};
}

float slot(const ui::DeviceUiContext& ctx, int s)
{
    return ctx.analysis != nullptr ? ctx.analysis->read(s) : NAN;
}

/// A whole-number slot (a position, a state), or `none` without a reading.
int slotInt(const ui::DeviceUiContext& ctx, int s, int none)
{
    const float v = slot(ctx, s);
    return std::isfinite(v) ? static_cast<int>(v) : none;
}

/// Momentary and short-term loudness as two bars on the scale, the target across them and each
/// one's loudest held.
class LoudnessBars final : public juce::Component, public juce::TooltipClient, public ui::ParamControl
{
public:
    explicit LoudnessBars(ui::DeviceUiContext& ctx) : ctx_(ctx) {}

    void refresh() override
    {
        const std::array<float, 5> now{slot(ctx_, M::Momentary), slot(ctx_, M::ShortTerm),
                                       slot(ctx_, M::MaxMomentary), slot(ctx_, M::MaxShortTerm),
                                       ctx_.params.getPlain(M::Target)};
        const auto scale = scaleOf(ctx_);
        if (now != values_ || scale.wide != scale_.wide || scale.relative != scale_.relative)
        {
            values_ = now;
            scale_ = scale;
            repaint();
        }
    }

    void paint(juce::Graphics& g) override
    {
        const auto& t = ui::Theme::current();
        const auto& grid = t.deviceGrid;
        g.setColour(t.surface0);
        g.fillRoundedRectangle(getLocalBounds().toFloat(), t.radiusControl);
        auto area = getLocalBounds().toFloat().reduced(static_cast<float>(grid.displayInset));
        const float text = static_cast<float>(grid.displayTextHeight);
        auto captions = area.removeFromBottom(text);
        area.removeFromBottom(static_cast<float>(grid.displayInset));
        // Half a line of text above and below the plot, so the end labels sit inside the well.
        const auto plot = area.reduced(0.0f, text * 0.5f);

        // Tick labels at the left, the bars evenly in the rest.
        g.setFont(ui::font(ui::TextStyle::ValueSmall));
        const float labels = juce::GlyphArrangement::getStringWidth(g.getCurrentFont(), "-36");
        const auto bars = plot.withTrimmedLeft(labels + static_cast<float>(grid.displayInset));
        const float barWidth = static_cast<float>(grid.meterWidth);
        const auto barAt = [&](int i)
        {
            const float centre = bars.getX() + bars.getWidth() * (static_cast<float>(i) + 1.0f) / 3.0f;
            return juce::Rectangle<float>(centre - barWidth * 0.5f, plot.getY(), barWidth, plot.getHeight());
        };
        g.setColour(t.textTertiary);
        for (float l = scale_.target; l >= scale_.low() - 0.01f; l -= scale_.labelStep())
            g.drawText(scale_.label(l),
                       juce::Rectangle<float>(area.getX(), scale_.yOf(plot, l) - text * 0.5f, labels, text),
                       juce::Justification::centredRight, false);
        for (float l = scale_.target + scale_.labelStep(); l <= scale_.high() + 0.01f;
             l += scale_.labelStep())
            g.drawText(scale_.label(l),
                       juce::Rectangle<float>(area.getX(), scale_.yOf(plot, l) - text * 0.5f, labels, text),
                       juce::Justification::centredRight, false);

        const std::array<juce::Colour, 2> colours{t.roles.loudnessMomentary, t.roles.loudnessShortTerm};
        const float targetY = scale_.yOf(plot, scale_.target);
        for (int i = 0; i < 2; ++i)
        {
            const auto bar = barAt(i);
            g.setColour(t.surface3);
            g.fillRect(bar);
            const float now = values_[static_cast<size_t>(i)];
            if (std::isfinite(now) && now > scale_.low())
            {
                const float top = scale_.yOf(plot, now);
                g.setColour(colours[static_cast<size_t>(i)]);
                g.fillRect(bar.withTop(std::max(top, targetY)));
                if (top < targetY) // louder than the target
                {
                    g.setColour(t.roles.warning);
                    g.fillRect(bar.withTop(top).withBottom(targetY));
                }
            }
            if (const float held = values_[static_cast<size_t>(i + 2)];
                std::isfinite(held) && held > scale_.low())
            {
                g.setColour(t.roles.marker);
                g.fillRect(bar.withTop(scale_.yOf(plot, held)).withHeight(1.0f));
            }
        }
        g.setColour(t.roles.loudnessTarget);
        g.fillRect(
            juce::Rectangle<float>(barAt(0).getX(), targetY, barAt(1).getRight() - barAt(0).getX(), 1.0f));

        g.setColour(t.textTertiary);
        g.setFont(ui::font(ui::TextStyle::Caption));
        for (int i = 0; i < 2; ++i)
            g.drawText(i == 0 ? "M" : "S",
                       barAt(i).withY(captions.getY()).withHeight(text).expanded(barWidth, 0.0f),
                       juce::Justification::centred, false);
    }

    juce::String getTooltip() override
    {
        const bool shortTerm = getMouseXYRelative().x > getWidth() * 7 / 12;
        const float now = values_[shortTerm ? 1u : 0u], held = values_[shortTerm ? 3u : 2u];
        return juce::String(shortTerm ? "Short-term (3 s): " : "Momentary (400 ms): ") + scale_.text(now) +
               ". Loudest: " + scale_.text(held) + ".";
    }

private:
    ui::DeviceUiContext& ctx_;
    std::array<float, 5> values_{};
    LoudnessScale scale_;
};

/// Short-term loudness over the song (Song) or since Reset (Running), momentary fainter behind it,
/// the target and its band, true peaks over the ceiling ticked along the top, the playhead; parts
/// not heard yet empty. Its label line says what the meter is doing, or what's under the pointer.
class LoudnessHistory final : public juce::Component, public ui::ParamControl
{
public:
    explicit LoudnessHistory(ui::DeviceUiContext& ctx) : ctx_(ctx) { setRepaintsOnMouseActivity(true); }

    void refresh() override
    {
        const auto* meter = dynamic_cast<const M*>(ctx_.device);
        const int extent = meter != nullptr ? meter->store().extent() : 0;
        const int heard = meter != nullptr ? meter->store().heardCount() : 0;
        const auto now = std::array<float, 6>{static_cast<float>(extent),
                                              static_cast<float>(heard),
                                              static_cast<float>(slotInt(ctx_, M::Position, -1)),
                                              static_cast<float>(slotInt(ctx_, M::Counting, 0)),
                                              ctx_.params.getPlain(M::Ceiling),
                                              ctx_.params.getPlain(M::Measure)};
        const auto scale = scaleOf(ctx_);
        if (now != state_ || scale.target != scale_.target || scale.wide != scale_.wide ||
            scale.relative != scale_.relative)
        {
            state_ = now;
            scale_ = scale;
            repaint();
        }
    }

    void mouseMove(const juce::MouseEvent& e) override { hover_ = e.position.x, repaint(); }
    void mouseExit(const juce::MouseEvent&) override { hover_.reset(), repaint(); }

    void paint(juce::Graphics& g) override
    {
        const auto& t = ui::Theme::current();
        const auto& grid = t.deviceGrid;
        g.setColour(t.surface0);
        g.fillRoundedRectangle(getLocalBounds().toFloat(), t.radiusControl);
        auto area = getLocalBounds().toFloat().reduced(static_cast<float>(grid.displayInset));
        const float text = static_cast<float>(grid.displayTextHeight);
        auto label = area.removeFromTop(text);
        auto axis = area.removeFromBottom(text);
        const auto plot = area.reduced(0.0f, static_cast<float>(grid.displayInset) * 0.5f);

        const auto* meter = dynamic_cast<const M*>(ctx_.device);
        if (meter == nullptr)
            return;
        const auto& store = meter->store();
        const bool song = ctx_.params.getPlain(M::Measure) < 0.5f;
        const int position = static_cast<int>(state_[2]);
        constexpr int minute = 600;
        const int span = std::max({store.extent(), song ? position + 1 : 0, minute});
        const auto xOf = [&](double step)
        { return plot.getX() + static_cast<float>(step / span) * plot.getWidth(); };

        // The time axis: a mark and a label every minute (or every five, for long songs).
        const int every = span > 20 * minute ? 5 * minute : minute;
        g.setFont(ui::font(ui::TextStyle::ValueSmall));
        for (int s = 0; s <= span; s += every)
        {
            const float x = xOf(s);
            g.setColour(t.roles.gridBar);
            g.fillRect(juce::Rectangle<float>(x, plot.getY(), 1.0f, plot.getHeight()));
            g.setColour(t.textTertiary);
            const auto minutes = juce::String(s / minute) + ":00";
            const float w = juce::GlyphArrangement::getStringWidth(g.getCurrentFont(), minutes);
            g.drawText(minutes,
                       juce::Rectangle<float>(std::min(x, plot.getRight() - w), axis.getY(), w, text),
                       juce::Justification::centredLeft, false);
        }

        // The target and its band, 1 LU either side.
        g.setColour(t.roles.loudnessTargetBand);
        g.fillRect(juce::Rectangle<float>(
            plot.getX(), scale_.yOf(plot, scale_.target + 1.0f), plot.getWidth(),
            scale_.yOf(plot, scale_.target - 1.0f) - scale_.yOf(plot, scale_.target + 1.0f)));
        g.setColour(t.roles.loudnessTarget);
        g.fillRect(
            juce::Rectangle<float>(plot.getX(), scale_.yOf(plot, scale_.target), plot.getWidth(), 1.0f));

        // One column per pixel: the loudest momentary and short-term values of the steps it covers,
        // from sliding sums over the steps (one pass, whatever the length).
        const int width = std::max(1, static_cast<int>(plot.getWidth()));
        const float ceilingGain = std::pow(10.0f, state_[4] / 20.0f);
        juce::Path shortTerm, shortFill, momentary;
        bool shortOn = false, momentaryOn = false;
        double sumM = 0.0, sumS = 0.0;
        int heardM = 0, heardS = 0, step = 0;
        const auto push = [&](int s)
        {
            const float ms = store.meanSquareAt(s);
            const auto add = [&](double& sum, int& heard, int length)
            {
                if (ms >= 0.0f)
                    sum += ms, ++heard;
                if (const int old = s - length; old >= 0)
                    if (const float o = store.meanSquareAt(old); o >= 0.0f)
                        sum -= o, --heard;
            };
            add(sumM, heardM, dsp::LoudnessStore::stepsMomentary);
            add(sumS, heardS, dsp::LoudnessStore::stepsShortTerm);
        };
        std::vector<int> overs;
        float fillStart = 0.0f;
        for (int x = 0; x < width; ++x)
        {
            const int to =
                std::min(store.extent(), static_cast<int>(static_cast<std::int64_t>(x + 1) * span / width));
            float loudM = NAN, loudS = NAN;
            bool over = false;
            for (; step < to; ++step)
            {
                push(step);
                if (heardM == dsp::LoudnessStore::stepsMomentary)
                    loudM = std::isnan(loudM)
                                ? static_cast<float>(dsp::lufsOf(sumM / heardM))
                                : std::max(loudM, static_cast<float>(dsp::lufsOf(sumM / heardM)));
                if (heardS == dsp::LoudnessStore::stepsShortTerm)
                    loudS = std::isnan(loudS)
                                ? static_cast<float>(dsp::lufsOf(sumS / heardS))
                                : std::max(loudS, static_cast<float>(dsp::lufsOf(sumS / heardS)));
                over = over || (store.heard(step) && store.truePeakAt(step) > ceilingGain);
            }
            const float px = plot.getX() + static_cast<float>(x) + 0.5f;
            if (over)
                overs.push_back(x);
            const bool hasM = std::isfinite(loudM) && loudM > scale_.low();
            if (hasM)
                momentaryOn ? momentary.lineTo(px, scale_.yOf(plot, loudM))
                            : momentary.startNewSubPath(px, scale_.yOf(plot, loudM));
            momentaryOn = hasM;
            const bool hasS = std::isfinite(loudS) && loudS > scale_.low();
            if (hasS)
            {
                const auto p = juce::Point<float>(px, scale_.yOf(plot, loudS));
                if (!shortOn)
                {
                    shortTerm.startNewSubPath(p);
                    shortFill.startNewSubPath(px, plot.getBottom());
                    fillStart = px;
                }
                shortTerm.lineTo(p);
                shortFill.lineTo(p);
            }
            else if (shortOn)
            {
                shortFill.lineTo(px - 1.0f, plot.getBottom());
                shortFill.lineTo(fillStart, plot.getBottom());
                shortFill.closeSubPath();
            }
            shortOn = hasS;
        }
        if (shortOn)
        {
            shortFill.lineTo(plot.getX() + static_cast<float>(width), plot.getBottom());
            shortFill.lineTo(fillStart, plot.getBottom());
            shortFill.closeSubPath();
        }
        {
            juce::Graphics::ScopedSaveState clip(g);
            g.reduceClipRegion(plot.toNearestInt());
            g.setColour(t.roles.curveFill);
            g.fillPath(shortFill);
            g.setColour(t.roles.loudnessMomentary);
            g.strokePath(momentary, juce::PathStrokeType(1.0f));
            g.setColour(t.roles.loudnessShortTerm);
            g.strokePath(shortTerm, juce::PathStrokeType(1.5f));
        }
        g.setColour(t.meterClip);
        for (const int x : overs)
            g.fillRect(juce::Rectangle<float>(plot.getX() + static_cast<float>(x), plot.getY(), 1.0f,
                                              static_cast<float>(grid.displayInset)));
        if (song && position >= 0 && position <= span)
        {
            g.setColour(t.roles.playhead);
            g.fillRect(juce::Rectangle<float>(xOf(position), plot.getY(), 1.0f, plot.getHeight()));
        }

        // The label line: the pointer's moment, or what the meter is doing.
        g.setFont(ui::font(ui::TextStyle::Caption));
        juce::String line;
        auto colour = t.textTertiary;
        if (hover_ && plot.getHorizontalRange().contains(*hover_))
        {
            const int s = std::clamp(
                static_cast<int>((*hover_ - plot.getX()) / plot.getWidth() * static_cast<float>(span)), 0,
                span);
            const int seconds = s / 10;
            line = juce::String(seconds / 60) + ":" + juce::String(seconds % 60).paddedLeft('0', 2);
            if (store.heard(s))
                line +=
                    "   S " + scale_.text(store.shortTermAt(s)) + "   M " + scale_.text(store.momentaryAt(s));
            else
                line += "   not heard yet";
            colour = t.textSecondary;
            g.setColour(t.roles.marker);
            g.fillRect(juce::Rectangle<float>(*hover_, plot.getY(), 1.0f, plot.getHeight()));
        }
        else
            switch (static_cast<M::CountingState>(static_cast<int>(state_[3])))
            {
            case M::CountingState::Paused:
                line = "Paused";
                break;
            case M::CountingState::Full:
                line = "Past 3 hours: not counted";
                break;
            case M::CountingState::WaitingForSong:
                line = "Counting while the song plays";
                break;
            case M::CountingState::Counting:
                if (store.heardCount() == 0)
                    line = song ? "Play the song to measure it" : "Counting";
                break;
            }
        g.setColour(colour);
        g.drawText(line, label, juce::Justification::centredLeft, true);
    }

private:
    ui::DeviceUiContext& ctx_;
    std::array<float, 6> state_{};
    LoudnessScale scale_;
    std::optional<float> hover_;
};

/// A reading from -1 to +1 (correlation) or left to right (balance) as a bar from the centre, its
/// caption and value above, the scale's ends and centre below; correlation holds its lowest.
class StereoBar final : public juce::Component, public juce::SettableTooltipClient, public ui::ParamControl
{
public:
    enum class Kind
    {
        Correlation,
        Balance
    };

    StereoBar(ui::DeviceUiContext& ctx, Kind kind) : ctx_(ctx), kind_(kind)
    {
        setTooltip(
            kind == Kind::Correlation
                ? "How alike the two sides are: +1 is mono, 0 unrelated, below 0 out of phase (it thins or "
                  "cancels in mono). The line marks the lowest since Reset."
                : "Which side is louder, over 3 seconds: the left or the right, in LU.");
    }

    void refresh() override
    {
        const std::array<float, 2> now =
            kind_ == Kind::Correlation
                ? std::array<float, 2>{slot(ctx_, M::Correlation), slot(ctx_, M::CorrelationMin)}
                : std::array<float, 2>{slot(ctx_, M::Balance), NAN};
        const auto rounded = [](float v) { return std::isfinite(v) ? std::round(v * 100.0f) : -1e9f; };
        if (rounded(now[0]) != rounded(values_[0]) || rounded(now[1]) != rounded(values_[1]))
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
        auto area = getLocalBounds().toFloat().reduced(static_cast<float>(grid.displayInset));
        const float text = static_cast<float>(grid.displayTextHeight);
        const bool correlation = kind_ == Kind::Correlation;
        const float v = values_[0];

        auto top = area.removeFromTop(text);
        g.setColour(t.textTertiary);
        g.setFont(ui::font(ui::TextStyle::Caption));
        g.drawText(correlation ? "Correlation" : "Balance", top, juce::Justification::centredLeft, false);
        juce::String value = ui::Readouts::none();
        if (std::isfinite(v))
            value = correlation ? (v > 0.005f ? "+" : "") + juce::String(v, 2)
                    : std::abs(v) < 0.05f
                        ? juce::String("Centre")
                        : juce::String(v > 0.0f ? "L " : "R ") + juce::String(std::abs(v), 1) + " LU";
        g.setColour(std::isfinite(v) ? (correlation && v < 0.0f ? t.roles.warning : t.textPrimary)
                                     : t.textTertiary);
        g.setFont(ui::font(ui::TextStyle::ValueSmall));
        g.drawText(value, top, juce::Justification::centredRight, false);

        auto scale = area.removeFromBottom(text);
        const float barHeight = static_cast<float>(grid.meterWidth);
        const auto bar = area.withSizeKeepingCentre(area.getWidth(), barHeight);
        g.setColour(t.surface3);
        g.fillRect(bar);
        // From the centre to the value: correlation right of the centre is +, balance's left is the left.
        const float range = correlation ? 1.0f : 6.0f;
        const auto xOf = [&](float r)
        {
            const float k = std::clamp(r / range, -1.0f, 1.0f) * (correlation ? 1.0f : -1.0f);
            return bar.getCentreX() + k * bar.getWidth() * 0.5f;
        };
        if (std::isfinite(v))
        {
            const float x = xOf(v);
            g.setColour(correlation && v < 0.0f ? t.roles.warning : t.value);
            g.fillRect(juce::Rectangle<float>(std::min(x, bar.getCentreX()), bar.getY(),
                                              std::abs(x - bar.getCentreX()), bar.getHeight()));
        }
        if (correlation && std::isfinite(values_[1]))
        {
            g.setColour(values_[1] < 0.0f ? t.roles.warning : t.roles.marker);
            g.fillRect(juce::Rectangle<float>(xOf(values_[1]), bar.getY(), 1.0f, bar.getHeight()));
        }
        g.setColour(t.textTertiary);
        g.fillRect(juce::Rectangle<float>(bar.getCentreX(), bar.getY(), 1.0f, bar.getHeight()));

        g.setFont(ui::font(ui::TextStyle::ValueSmall));
        g.drawText(correlation ? "-1" : "L", scale, juce::Justification::centredLeft, false);
        g.drawText(correlation ? "0" : "C", scale, juce::Justification::centred, false);
        g.drawText(correlation ? "+1" : "R", scale, juce::Justification::centredRight, false);
    }

private:
    ui::DeviceUiContext& ctx_;
    Kind kind_;
    std::array<float, 2> values_{NAN, NAN};
};

/// The readings: integrated, large; then short-term and the true peak, momentary and peak to
/// loudness, the range and what normalising does.
class LoudnessReadings final : public ui::Readouts, public ui::ParamControl
{
public:
    explicit LoudnessReadings(ui::DeviceUiContext& ctx) : Readouts(2, true), ctx_(ctx) {}

    void refresh() override
    {
        const auto& t = ui::Theme::current();
        const auto scale = scaleOf(ctx_);
        const float integrated = slot(ctx_, M::Integrated), range = slot(ctx_, M::Range);
        const float peak = slot(ctx_, M::TruePeakMax), plr = slot(ctx_, M::PeakToLoudness),
                    normalised = slot(ctx_, M::Normalised);
        const auto lu = [](float v) { return std::isfinite(v) ? juce::String(v, 1) + " LU" : none(); };
        juce::Colour onTarget;
        if (std::isfinite(integrated))
            onTarget = std::abs(integrated - scale.target) <= 1.0f ? t.roles.loudnessTarget
                       : integrated > scale.target                 ? t.roles.warning
                                                                   : juce::Colour();
        // EBU Mode: the range isn't stable for its first minute of counting.
        const bool settling = slot(ctx_, M::Counted) < 60.0f;
        set({
            {"Integrated", scale.text(integrated), onTarget,
             "The whole song's loudness, as the services measure it (gated, BS.1770)"},
            {"Short-term", scale.text(slot(ctx_, M::ShortTerm)), {}, "The last 3 seconds"},
            {"True peak", std::isfinite(peak) ? juce::String(peak, 1) + " dBTP" : none(),
             std::isfinite(peak) && peak > ctx_.params.getPlain(M::Ceiling) ? t.meterClip : juce::Colour(),
             "The highest peak, between samples too, counted so far"},
            {"Momentary", scale.text(slot(ctx_, M::Momentary)), {}, "The last 400 ms"},
            {"Peak to loudness",
             lu(plr),
             {},
             "How far the peaks reach above the integrated loudness: under about 8 LU, it's heavily limited"},
            {"Range", lu(range), settling && std::isfinite(range) ? t.textTertiary : juce::Colour(),
             settling
                 ? "How much the loudness moves (EBU Tech 3342); not settled for its first minute"
                 : "How much the loudness moves between the quieter and the louder parts (EBU Tech 3342)"},
            {"Normalised",
             std::isfinite(normalised) ? (normalised > 0.05f ? "+" : "") + juce::String(normalised, 1) + " dB"
                                       : none(),
             {},
             "What a service playing everything at the target does to this song: turned down, or up as far "
             "as the "
             "peaks allow"},
        });
    }

private:
    ui::DeviceUiContext& ctx_;
};

/// Pause and Resume: on while the meter is paused (a display command, not state).
class PauseToggle final : public ui::Toggle, public ui::ParamControl
{
public:
    explicit PauseToggle(ui::DeviceUiContext& ctx) : Toggle("Pause"), ctx_(ctx)
    {
        setClickingTogglesState(false);
        setTooltip("Hold the integrated loudness and the range; the bars keep moving");
        onClick = [this]
        {
            if (ctx_.analysis != nullptr)
                ctx_.analysis->request(getToggleState() ? M::Resume : M::Pause);
        };
    }

    void refresh() override
    {
        setToggleState(slotInt(ctx_, M::Counting, 0) == static_cast<int>(M::CountingState::Paused),
                       juce::dontSendNotification);
    }

private:
    ui::DeviceUiContext& ctx_;
};

/// Sections per devices/loudness-meter/SPEC.md, on the module grid (docs/design/device-panels.md).
class LoudnessMeterPanel final : public ui::DevicePanel
{
public:
    explicit LoudnessMeterPanel(ui::DeviceUiContext& ctx) : DevicePanel(ctx)
    {
        auto& loudness = addSection("loudness", "Loudness", 6);
        loudness.place(display(std::make_unique<LoudnessBars>(ctx)), 0, 0, 2, 6);
        loudness.place(display(std::make_unique<LoudnessReadings>(ctx)), 2, 0, 4, 6);

        auto& history = addSection("history", "History", 8);
        history.place(display(std::make_unique<LoudnessHistory>(ctx)), 0, 0, 8, 6);

        auto& stereo = addSection("stereo", "Stereo", 3);
        stereo.place(display(std::make_unique<StereoBar>(ctx, StereoBar::Kind::Correlation)), 0, 0, 3, 3);
        stereo.place(display(std::make_unique<StereoBar>(ctx, StereoBar::Kind::Balance)), 0, 3, 3, 3);

        auto& measure = addSection("measure", "Measure", 4);
        measure.bar(M::Target, 0, 0, 4);
        measure.bar(M::Ceiling, 0, 1, 4);
        auto& reset = own(std::make_unique<ui::Toggle>("Reset"));
        reset.setTooltip(
            "Start counting again: clears the integrated loudness, the range, the maximums and the history");
        reset.onClick = [&ctx]
        {
            if (ctx.analysis != nullptr)
                ctx.analysis->request(M::Reset);
        };
        measure.place(reset, 0, 2, 2, 1, ui::PanelSection::Kind::Line);
        measure.place(display(std::make_unique<PauseToggle>(ctx)), 2, 2, 2, 1, ui::PanelSection::Kind::Line);
        measure.segmented(M::Measure, 0, 3, 4, 1);
        measure.label("Scale", 0, 4, 2);
        measure.segmented(M::Scale, 2, 4, 2, 1);
        measure.label("Units", 0, 5, 2);
        measure.segmented(M::Units, 2, 5, 2, 1);
        for (auto* d : displays_) // drawn from the start, before the first frame
            d->refresh();
    }

private:
    /// Owns a display that follows the device, so the frame clock can refresh it.
    template <typename T> T& display(std::unique_ptr<T> component)
    {
        auto& d = own(std::move(component));
        displays_.push_back(&d);
        return d;
    }

    /// Every frame on screen (the panel's 30 Hz refresh covers hosts without a frame clock).
    void tick()
    {
        if (isShowing())
            for (auto* d : displays_)
                d->refresh();
    }

    std::vector<ui::ParamControl*> displays_;
    juce::VBlankAttachment vblank_{this, [this] { tick(); }};
};
} // namespace

const ui::PanelFactory& loudnessMeterPanelFactory()
{
    static const ui::PanelFactory factory{
        .deviceId = "loudness-meter",
        .create = [](ui::DeviceUiContext& ctx) -> std::unique_ptr<ui::DevicePanel>
        { return std::make_unique<LoudnessMeterPanel>(ctx); },
    };
    return factory;
}
} // namespace fd::devices
