#include "SubtractiveSynthPanel.h"

#include "SubtractiveSynth.h"

#include <limits>

namespace fd::devices
{
namespace
{
using S = SubtractiveSynth;

/// A flat well with a corner label that draws from pure DSP functions and repaints only when
/// the parameters it depends on change.
class MiniDisplay : public juce::Component, public ui::ParamControl
{
public:
    MiniDisplay(ui::DeviceUiContext& ctx, juce::String label, std::vector<size_t> params)
        : ctx_(ctx), label_(std::move(label)), params_(std::move(params)),
          last_(params_.size(), std::numeric_limits<float>::quiet_NaN())
    {
        setInterceptsMouseClicks(false, false);
    }

    void refresh() override
    {
        bool changed = false;
        for (size_t i = 0; i < params_.size(); ++i)
        {
            const float v = ctx_.params.getPlain(params_[i]);
            if (!(v == last_[i]))
            {
                last_[i] = v;
                changed = true;
            }
        }
        if (changed)
            repaint();
    }

    void paint(juce::Graphics& g) override
    {
        const auto& t = ui::Theme::current();
        const auto r = getLocalBounds().toFloat();
        g.setColour(t.surface0);
        g.fillRoundedRectangle(r, t.radiusControl);
        drawContent(g, r.reduced(5.0f, 4.0f).withTrimmedTop(12.0f));
        g.setColour(t.textTertiary);
        g.setFont(ui::fonts::font(t.textXs, ui::fonts::Weight::SemiBold));
        g.drawText(label_, r.reduced(5.0f, 3.0f).removeFromTop(12.0f), juce::Justification::centredLeft, false);
    }

protected:
    virtual void drawContent(juce::Graphics& g, juce::Rectangle<float> area) = 0;
    [[nodiscard]] float param(size_t index) const { return ctx_.params.getPlain(index); }

private:
    ui::DeviceUiContext& ctx_;
    juce::String label_;
    std::vector<size_t> params_;
    std::vector<float> last_;
};

/// One cycle of oscillator 1's morphed waveform.
class OscDisplay final : public MiniDisplay
{
public:
    explicit OscDisplay(ui::DeviceUiContext& ctx) : MiniDisplay(ctx, "OSC 1", {S::Osc1Shape, S::Osc1Pw}) {}

private:
    void drawContent(juce::Graphics& g, juce::Rectangle<float> area) override
    {
        const auto mix = S::osc1Mix(param(S::Osc1Shape), param(S::Osc1Pw));
        const int n = juce::jmax(16, static_cast<int>(area.getWidth()) * 2);
        juce::Path p;
        for (int i = 0; i <= n; ++i)
        {
            const double phase = static_cast<double>(i) / n;
            const float y = static_cast<float>(dsp::Oscillator::naiveAt(mix, juce::jmin(phase, 0.99999)));
            const juce::Point<float> pt(area.getX() + static_cast<float>(phase) * area.getWidth(),
                                        area.getCentreY() - y * area.getHeight() * 0.45f);
            i == 0 ? p.startNewSubPath(pt) : p.lineTo(pt);
        }
        g.setColour(ui::Theme::current().value);
        g.strokePath(p, juce::PathStrokeType(1.5f, juce::PathStrokeType::curved));
    }
};

/// Filter magnitude response at the panel's cutoff and resonance (small-signal).
class FilterDisplay final : public MiniDisplay
{
public:
    explicit FilterDisplay(ui::DeviceUiContext& ctx)
        : MiniDisplay(ctx, "", {S::FilterType, S::FilterCutoff, S::FilterRes})
    {
    }

private:
    static constexpr double kTopDb = 30.0, kBottomDb = -42.0, kSampleRate = 48000.0;

    void drawContent(juce::Graphics& g, juce::Rectangle<float> area) override
    {
        const auto& t = ui::Theme::current();
        area = area.withTop(area.getY() - 10.0f); // no label: use the full height
        auto yFor = [&](double db) {
            const double c = juce::jlimit(kBottomDb, kTopDb, db);
            return area.getY() + static_cast<float>((kTopDb - c) / (kTopDb - kBottomDb)) * area.getHeight();
        };
        g.setColour(t.surface3);
        g.drawHorizontalLine(juce::roundToInt(yFor(0.0)), area.getX(), area.getRight());

        const int type = juce::roundToInt(param(S::FilterType));
        const double cutoff = param(S::FilterCutoff);
        const double res = param(S::FilterRes) * 0.01;
        const int n = juce::jmax(16, static_cast<int>(area.getWidth()));
        juce::Path curve;
        for (int i = 0; i <= n; ++i)
        {
            const double f = 20.0 * std::pow(1000.0, static_cast<double>(i) / n); // 20 Hz .. 20 kHz
            const double mag = S::filterMagnitude(type, f, cutoff, res, kSampleRate);
            const juce::Point<float> pt(area.getX() + static_cast<float>(i) / static_cast<float>(n) * area.getWidth(),
                                        yFor(20.0 * std::log10(juce::jmax(mag, 1e-6))));
            i == 0 ? curve.startNewSubPath(pt) : curve.lineTo(pt);
        }
        auto fill = curve;
        fill.lineTo(area.getBottomRight());
        fill.lineTo(area.getBottomLeft());
        fill.closeSubPath();
        g.setColour(t.value.withAlpha(0.14f));
        g.fillPath(fill);
        g.setColour(t.value);
        g.strokePath(curve, juce::PathStrokeType(1.5f, juce::PathStrokeType::curved));
    }
};

/// ADSR shape, drawn by running the DSP envelope itself. Segment widths grow with the log of
/// their times (so short and long envelopes both read clearly) and fill the display.
class EnvelopeDisplay final : public MiniDisplay
{
public:
    EnvelopeDisplay(ui::DeviceUiContext& ctx, juce::String label, size_t attack, std::optional<size_t> cycle)
        : MiniDisplay(ctx, std::move(label), paramsFor(attack, cycle)), attack_(attack), cycle_(cycle)
    {
    }

private:
    static std::vector<size_t> paramsFor(size_t attack, std::optional<size_t> cycle)
    {
        std::vector<size_t> p{attack, attack + 1, attack + 2, attack + 3};
        if (cycle)
            p.push_back(*cycle);
        return p;
    }

    void drawContent(juce::Graphics& g, juce::Rectangle<float> area) override
    {
        const double a = param(attack_), d = param(attack_ + 1), s = param(attack_ + 2), r = param(attack_ + 3);
        const bool cycle = cycle_ && param(*cycle_) > 0.5f;
        const auto settings = S::envelopeSettings(a, d, s, r, cycle);

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

        const auto& t = ui::Theme::current();
        auto fill = p;
        fill.lineTo(x, area.getBottom());
        fill.closeSubPath();
        g.setColour(t.value.withAlpha(0.14f));
        g.fillPath(fill);
        g.setColour(t.value);
        g.strokePath(p, juce::PathStrokeType(1.5f, juce::PathStrokeType::curved));
    }

    size_t attack_;
    std::optional<size_t> cycle_;
};

/// Layout per devices/subtractive-synth/SPEC.md: two rows (one per oscillator / envelope),
/// grouped left to right: oscillators, filter, envelopes, modulation, voice and output.
class SubtractiveSynthPanel final : public ui::DevicePanel
{
public:
    explicit SubtractiveSynthPanel(ui::DeviceUiContext& ctx) : DevicePanel(ctx)
    {
        auto knob = [&](size_t i) { return &bind(std::make_unique<ui::ParamKnob>(ctx, i)); };
        auto bar = [&](size_t i) {
            auto* k = knob(i);
            k->setStyle(ui::Knob::Style::Bar);
            return k;
        };
        auto combo = [&](size_t i) { return &bind(std::make_unique<ui::ParamComboBox>(ctx, i)); };

        osc1View_ = &bind(std::make_unique<OscDisplay>(ctx));
        osc1_ = {knob(S::Osc1Shape), knob(S::Osc1Pw), knob(S::Osc1Oct), knob(S::MixOsc1), knob(S::MixNoise)};
        osc2Wave_ = combo(S::Osc2Wave);
        osc2_ = {knob(S::Osc2Oct), knob(S::Osc2Semi), knob(S::Osc2Detune), knob(S::MixOsc2), knob(S::VoiceDrift)};

        filterType_ = &bind(std::make_unique<ui::ParamSegmented>(ctx, S::FilterType, 4));
        filterView_ = &bind(std::make_unique<FilterDisplay>(ctx));
        filter_ = {knob(S::FilterCutoff), knob(S::FilterRes), knob(S::FilterKey), knob(S::FilterEnv)};

        ampView_ = &bind(std::make_unique<EnvelopeDisplay>(ctx, "AMP", S::AmpA, std::nullopt));
        amp_ = {knob(S::AmpA), knob(S::AmpD), knob(S::AmpS), knob(S::AmpR)};
        modView_ = &bind(std::make_unique<EnvelopeDisplay>(ctx, "MOD", S::ModA, S::ModCycle));
        mod_ = {knob(S::ModA), knob(S::ModD), knob(S::ModS), knob(S::ModR)};
        cycle_ = &bind(std::make_unique<ui::ParamToggle>(ctx, S::ModCycle));

        for (size_t n = 0; n < slots_.size(); ++n)
        {
            const size_t base = S::M1Src + n * 3;
            slots_[n] = {combo(base), combo(base + 1), bar(base + 2)};
        }
        lfoWave_ = combo(S::LfoWave);
        lfoSync_ = combo(S::LfoSync);
        lfoRate_ = knob(S::LfoRate);

        voiceMode_ = combo(S::VoiceMode);
        voices_ = bar(S::VoiceCount);
        bend_ = bar(S::VoiceBend);
        glide_ = knob(S::VoiceGlide);
        volume_ = knob(S::OutVolume);

        if (auto* bus = ctx.analysis)
            meter_.setLevelSource([bus](int) { return bus->takePeak(S::PeakOut); });
        addAndMakeVisible(meter_);
    }

    int preferredWidth() const override { return 1164; }

    void paint(juce::Graphics& g) override
    {
        DevicePanel::paint(g);
        const auto& t = ui::Theme::current();
        g.setColour(t.textTertiary);
        g.setFont(ui::fonts::font(t.textXs, ui::fonts::Weight::SemiBold));
        g.drawText("OSC 2", osc2Label_, juce::Justification::centredLeft, false);
        g.drawText("LFO", lfoLabel_, juce::Justification::centredLeft, false);
    }

    void layoutBody(juce::Rectangle<int> body) override
    {
        constexpr int gap = 16, knobW = 42, cellW = 64, envCellW = 76, smallGap = 4, srcW = 86, dstW = 88, filterKnobW = 48;
        const int rowH = body.getHeight() / 2;
        auto rows = [&](int width) {
            auto block = body.removeFromLeft(width);
            body.removeFromLeft(gap);
            return std::pair{block.removeFromTop(rowH), block};
        };
        auto knobs = [&](juce::Rectangle<int> r, auto& list, int w) {
            for (auto* k : list)
                k->setBounds(r.removeFromLeft(w));
        };

        meter_.setBounds(body.removeFromRight(9).reduced(0, 4));
        body.removeFromRight(8);

        // Oscillators: one row each; the fifth column holds Noise and Drift.
        {
            auto [r1, r2] = rows(cellW + smallGap + 5 * knobW);
            osc1View_->setBounds(r1.removeFromLeft(cellW).reduced(0, 3));
            r1.removeFromLeft(smallGap);
            knobs(r1, osc1_, knobW);
            auto cell = r2.removeFromLeft(cellW).reduced(0, 3);
            osc2Label_ = cell.removeFromTop(16).withTrimmedLeft(1);
            osc2Wave_->setBounds(cell.removeFromTop(20));
            r2.removeFromLeft(smallGap);
            knobs(r2, osc2_, knobW);
        }
        // Filter: type and response on top, knobs below.
        {
            auto [r1, r2] = rows(4 * filterKnobW);
            r1.reduce(0, 3);
            filterType_->setBounds(r1.removeFromTop(18));
            r1.removeFromTop(smallGap);
            filterView_->setBounds(r1);
            knobs(r2, filter_, filterKnobW);
        }
        // Envelopes: amp on top, mod below.
        {
            auto [r1, r2] = rows(envCellW + smallGap + 4 * knobW);
            ampView_->setBounds(r1.removeFromLeft(envCellW).reduced(0, 3));
            r1.removeFromLeft(smallGap);
            knobs(r1, amp_, knobW);
            auto modCell = r2.removeFromLeft(envCellW).reduced(0, 3);
            modView_->setBounds(modCell);
            cycle_->setBounds(modCell.removeFromTop(18).removeFromRight(40).reduced(3, 3));
            r2.removeFromLeft(smallGap);
            knobs(r2, mod_, knobW);
        }
        // Modulation: three matrix slots on top, the LFO below.
        {
            auto [r1, r2] = rows(srcW + dstW + 64 + 2 * smallGap);
            const int lineH = (r1.getHeight() - 2 * smallGap) / 3;
            for (auto& slot : slots_)
            {
                auto line = r1.removeFromTop(lineH).reduced(0, 1);
                r1.removeFromTop(smallGap);
                slot.src->setBounds(line.removeFromLeft(srcW));
                line.removeFromLeft(smallGap);
                slot.dst->setBounds(line.removeFromLeft(dstW));
                line.removeFromLeft(smallGap);
                slot.amount->setBounds(line);
            }
            lfoRate_->setBounds(r2.removeFromRight(44));
            r2.removeFromRight(smallGap);
            r2.reduce(0, 3);
            lfoLabel_ = r2.removeFromTop(16).withTrimmedLeft(1);
            auto line = r2.removeFromTop(20);
            const int w = (line.getWidth() - smallGap) / 2;
            lfoWave_->setBounds(line.removeFromLeft(w));
            line.removeFromLeft(smallGap);
            lfoSync_->setBounds(line);
        }
        // Voice and output.
        {
            auto [r1, r2] = rows(body.getWidth());
            r1.reduce(0, 3);
            const int lineH = (r1.getHeight() - 2 * smallGap) / 3;
            voiceMode_->setBounds(r1.removeFromTop(lineH));
            r1.removeFromTop(smallGap);
            voices_->setBounds(r1.removeFromTop(lineH));
            r1.removeFromTop(smallGap);
            bend_->setBounds(r1.removeFromTop(lineH));
            const int w = r2.getWidth() / 2;
            glide_->setBounds(r2.removeFromLeft(w));
            volume_->setBounds(r2);
        }
    }

private:
    struct Slot
    {
        ui::ParamComboBox* src = nullptr;
        ui::ParamComboBox* dst = nullptr;
        ui::ParamKnob* amount = nullptr;
    };

    OscDisplay* osc1View_ = nullptr;
    FilterDisplay* filterView_ = nullptr;
    EnvelopeDisplay *ampView_ = nullptr, *modView_ = nullptr;
    std::array<ui::ParamKnob*, 5> osc1_{}, osc2_{};
    std::array<ui::ParamKnob*, 4> filter_{}, amp_{}, mod_{};
    std::array<Slot, 3> slots_{};
    ui::ParamComboBox *osc2Wave_ = nullptr, *lfoWave_ = nullptr, *lfoSync_ = nullptr, *voiceMode_ = nullptr;
    ui::ParamSegmented* filterType_ = nullptr;
    ui::ParamToggle* cycle_ = nullptr;
    ui::ParamKnob *lfoRate_ = nullptr, *voices_ = nullptr, *bend_ = nullptr, *glide_ = nullptr, *volume_ = nullptr;
    ui::Meter meter_{1};
    juce::Rectangle<int> osc2Label_, lfoLabel_;
};
} // namespace

const ui::PanelFactory& subtractiveSynthPanelFactory()
{
    static const ui::PanelFactory factory{
        .deviceId = "subtractive-synth",
        .create = [](ui::DeviceUiContext& ctx) -> std::unique_ptr<ui::DevicePanel> {
            return std::make_unique<SubtractiveSynthPanel>(ctx);
        },
    };
    return factory;
}
} // namespace fd::devices
