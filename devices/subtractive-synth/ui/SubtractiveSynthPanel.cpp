#include "SubtractiveSynthPanel.h"

#include "SubtractiveSynth.h"

namespace fd::devices
{
namespace
{
using S = SubtractiveSynth;

/// One cycle of oscillator 1's morphed waveform.
class OscDisplay final : public ui::MiniDisplay
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
        filterView_ = &bind(std::make_unique<ui::FilterDisplay>(ctx, S::FilterType, S::FilterCutoff, S::FilterRes));
        filter_ = {knob(S::FilterCutoff), knob(S::FilterRes), knob(S::FilterKey), knob(S::FilterEnv)};

        ampView_ = &bind(std::make_unique<ui::EnvelopeDisplay>(ctx, "AMP", S::AmpA));
        amp_ = {knob(S::AmpA), knob(S::AmpD), knob(S::AmpS), knob(S::AmpR)};
        modView_ = &bind(std::make_unique<ui::EnvelopeDisplay>(ctx, "MOD", S::ModA, S::ModCycle));
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
        g.setFont(ui::fonts::mono(t.textXs - 1.0f, ui::fonts::Weight::SemiBold));
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
    ui::FilterDisplay* filterView_ = nullptr;
    ui::EnvelopeDisplay *ampView_ = nullptr, *modView_ = nullptr;
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
