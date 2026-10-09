#include "SubtractiveSynthPanel.h"

#include "SubtractiveSynth.h"

namespace fd::devices
{
namespace
{
using S = SubtractiveSynth;

/// One cycle of an oscillator's waveform, from the same function the audio path uses. The
/// section title names it, so the well has no label of its own.
class OscDisplay final : public ui::MiniDisplay
{
public:
    /// Oscillator 1 (the morph) when `osc2` is false, else oscillator 2's wave.
    OscDisplay(ui::DeviceUiContext& ctx, bool osc2)
        : MiniDisplay(ctx, {}, osc2 ? std::vector<size_t>{S::Osc2Wave} : std::vector<size_t>{S::Osc1Shape, S::Osc1Pw}),
          osc2_(osc2)
    {
    }

private:
    void drawContent(juce::Graphics& g, juce::Rectangle<float> area) override
    {
        const auto mix = osc2_ ? S::osc2Mix(static_cast<int>(param(S::Osc2Wave)))
                               : S::osc1Mix(param(S::Osc1Shape), param(S::Osc1Pw));
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

    bool osc2_;
};

/// Sections per devices/subtractive-synth/SPEC.md, on the module grid
/// (docs/design/device-panels.md).
class SubtractiveSynthPanel final : public ui::DevicePanel
{
public:
    explicit SubtractiveSynthPanel(ui::DeviceUiContext& ctx) : DevicePanel(ctx)
    {
        auto& osc1 = addSection("osc1", "Osc 1", 4);
        osc1.place(own(std::make_unique<OscDisplay>(ctx, false)), 0, 0, 2, 3);
        osc1.knob(S::Osc1Shape, 2, 0);
        osc1.knob(S::Osc1Pw, 3, 0);
        osc1.knob(S::Osc1Oct, 0, 3);
        osc1.knob(S::MixOsc1, 1, 3);
        osc1.knob(S::MixNoise, 2, 3);

        auto& osc2 = addSection("osc2", "Osc 2", 4);
        osc2.place(own(std::make_unique<OscDisplay>(ctx, true)), 0, 0, 2, 2);
        osc2.combo(S::Osc2Wave, 0, 2, 2);
        osc2.knob(S::Osc2Oct, 2, 0);
        osc2.knob(S::Osc2Semi, 3, 0);
        osc2.knob(S::Osc2Detune, 0, 3);
        osc2.knob(S::MixOsc2, 1, 3);

        auto& filter = addSection("filter", "Filter", 4);
        filter.segmented(S::FilterType, 0, 0, 4, 1);
        filter.place(own(std::make_unique<ui::FilterDisplay>(ctx, S::FilterType, S::FilterCutoff, S::FilterRes)), 0, 1, 4, 2);
        filter.knob(S::FilterCutoff, 0, 3);
        filter.knob(S::FilterRes, 1, 3);
        filter.knob(S::FilterKey, 2, 3);
        filter.knob(S::FilterEnv, 3, 3);

        auto& env = addSection("env", "Envelopes", 6);
        env.place(own(std::make_unique<ui::EnvelopeDisplay>(ctx, "Amp", S::AmpA)), 0, 0, 2, 3);
        auto& modView = own(std::make_unique<ui::EnvelopeDisplay>(ctx, "Mod", S::ModA, S::ModCycle));
        modView.setAccessory(own(std::make_unique<ui::ParamToggle>(ctx, S::ModCycle)), 40);
        env.place(modView, 0, 3, 2, 3);
        for (size_t i = 0; i < 4; ++i)
        {
            env.knob(S::AmpA + i, 2 + static_cast<int>(i), 0);
            env.knob(S::ModA + i, 2 + static_cast<int>(i), 3);
        }

        // Three slots (source and target, then the amount), and the LFO beside them.
        auto& mod = addSection("mod", "Modulation", 6);
        for (int n = 0; n < 3; ++n)
        {
            const size_t base = S::M1Src + static_cast<size_t>(n) * 3;
            mod.combo(base, 0, 2 * n, 2);
            mod.combo(base + 1, 2, 2 * n, 2);
            mod.bar(base + 2, 0, 2 * n + 1, 4);
        }
        mod.label("LFO", 4, 0, 2);
        mod.combo(S::LfoWave, 4, 1, 2);
        mod.combo(S::LfoSync, 4, 2, 2);
        mod.knob(S::LfoRate, 4, 3);

        auto& voice = addSection("voice", "Voice", 3);
        voice.combo(S::VoiceMode, 0, 0, 2);
        voice.bar(S::VoiceCount, 0, 1, 2);
        voice.bar(S::VoiceBend, 0, 2, 2);
        voice.knob(S::OutVolume, 2, 0);
        voice.knob(S::VoiceGlide, 0, 3);
        voice.knob(S::VoiceDrift, 1, 3);

        if (auto* bus = ctx.analysis)
            meter_.setLevelSource([bus](int) { return bus->takePeak(S::PeakOut); });
        setMeters({{&meter_, "Out"}});
    }

private:
    ui::Meter meter_{1};
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
