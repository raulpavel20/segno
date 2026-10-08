#include "SaturatorPanel.h"

#include "Saturator.h"

namespace fd::devices
{
namespace
{
/// The transfer curve at the current curve, drive and bias, from the same function the audio
/// path uses. Drag vertically for drive, horizontally for bias.
class TransferCurve final : public juce::Component, public ui::ParamControl
{
public:
    explicit TransferCurve(ui::DeviceUiContext& ctx) : ctx_(ctx)
    {
        setMouseCursor(juce::MouseCursor::UpDownLeftRightResizeCursor);
        setTooltip("Drag up/down for Drive, left/right for Bias");
    }

    void setTooltip(const juce::String& text) { tooltip_ = text; }

    void refresh() override
    {
        const auto key = std::make_tuple(ctx_.params.getPlain(Saturator::Curve), ctx_.params.getPlain(Saturator::Drive),
                                         ctx_.params.getPlain(Saturator::Bias));
        if (key != last_)
        {
            last_ = key;
            repaint();
        }
    }

    void paint(juce::Graphics& g) override
    {
        const auto& t = ui::Theme::current();
        const auto r = getLocalBounds().toFloat();
        g.setColour(t.surface0);
        g.fillRoundedRectangle(r, t.radiusControl);

        const auto plot = r.reduced(6.0f);
        g.setColour(t.surface3);
        g.drawLine(plot.getX(), plot.getCentreY(), plot.getRight(), plot.getCentreY(), 1.0f);
        g.drawLine(plot.getCentreX(), plot.getY(), plot.getCentreX(), plot.getBottom(), 1.0f);
        g.setColour(t.line);
        g.drawLine(plot.getX(), plot.getBottom(), plot.getRight(), plot.getY(), 1.0f); // unity

        const auto curve = static_cast<dsp::ShaperCurve>(static_cast<int>(ctx_.params.getPlain(Saturator::Curve)));
        const float drive = ctx_.params.getPlain(Saturator::Drive);
        const float bias = ctx_.params.getPlain(Saturator::Bias);
        juce::Path p;
        for (int i = 0; i <= 128; ++i)
        {
            const float x = -1.0f + 2.0f * static_cast<float>(i) / 128.0f;
            const float y = juce::jlimit(-1.2f, 1.2f, Saturator::transfer(curve, drive, bias, x));
            const auto pt = juce::Point<float>(plot.getX() + (x + 1.0f) * 0.5f * plot.getWidth(),
                                               plot.getCentreY() - y * 0.5f * plot.getHeight());
            i == 0 ? p.startNewSubPath(pt) : p.lineTo(pt);
        }
        g.setColour(t.value);
        g.strokePath(p, juce::PathStrokeType(1.5f));
    }

    void mouseDown(const juce::MouseEvent& e) override
    {
        start_ = e.position;
        startDrive_ = ctx_.params.getPlain(Saturator::Drive);
        startBias_ = ctx_.params.getPlain(Saturator::Bias);
        ctx_.params.beginGesture(Saturator::Drive);
        ctx_.params.beginGesture(Saturator::Bias);
    }

    void mouseDrag(const juce::MouseEvent& e) override
    {
        const float fine = e.mods.isShiftDown() ? 0.1f : 1.0f;
        const auto d = e.position - start_;
        ctx_.params.setPlain(Saturator::Drive, juce::jlimit(0.0f, 36.0f, startDrive_ - d.y * 0.2f * fine));
        ctx_.params.setPlain(Saturator::Bias, juce::jlimit(-1.0f, 1.0f, startBias_ + d.x * 0.01f * fine));
        refresh();
    }

    void mouseUp(const juce::MouseEvent&) override
    {
        ctx_.params.endGesture(Saturator::Drive);
        ctx_.params.endGesture(Saturator::Bias);
    }

private:
    ui::DeviceUiContext& ctx_;
    juce::String tooltip_;
    std::tuple<float, float, float> last_{-1.0f, -1.0f, -1.0f};
    juce::Point<float> start_;
    float startDrive_ = 0.0f, startBias_ = 0.0f;
};

/// Layout per devices/saturator/SPEC.md.
class SaturatorPanel final : public ui::DevicePanel
{
public:
    explicit SaturatorPanel(ui::DeviceUiContext& ctx) : DevicePanel(ctx)
    {
        curveView_ = &bind(std::make_unique<TransferCurve>(ctx));
        drive_ = &bind(std::make_unique<ui::ParamKnob>(ctx, Saturator::Drive));
        bias_ = &bind(std::make_unique<ui::ParamKnob>(ctx, Saturator::Bias));
        output_ = &bind(std::make_unique<ui::ParamKnob>(ctx, Saturator::Output));
        mix_ = &bind(std::make_unique<ui::ParamKnob>(ctx, Saturator::Mix));
        curves_ = &bind(std::make_unique<ui::ParamSegmented>(ctx, Saturator::Curve, 3));
        color_ = &bind(std::make_unique<ui::ParamToggle>(ctx, Saturator::Color));
        colorFreq_ = &bind(std::make_unique<ui::ParamKnob>(ctx, Saturator::ColorFreq));
        colorGain_ = &bind(std::make_unique<ui::ParamKnob>(ctx, Saturator::ColorGain));
        // Freq and Gain only act while Color is on.
        dimWhen({colorFreq_, colorGain_}, [&ctx] { return ctx.params.getPlain(Saturator::Color) < 0.5f; });
        quality_ = &bind(std::make_unique<ui::ParamComboBox>(ctx, Saturator::Quality));

        if (auto* bus = ctx.analysis)
        {
            meterIn_.setLevelSource([bus](int c) { return bus->takePeak(c == 0 ? Saturator::PeakInL : Saturator::PeakInR); });
            meterOut_.setLevelSource([bus](int c) { return bus->takePeak(c == 0 ? Saturator::PeakOutL : Saturator::PeakOutR); });
        }
        addAndMakeVisible(meterIn_);
        addAndMakeVisible(meterOut_);
    }

    int preferredWidth() const override { return 520; }

    void layoutBody(juce::Rectangle<int> body) override
    {
        const auto& t = ui::Theme::current();
        const int gap = t.grid * 2;

        auto meters = body.removeFromRight(22);
        meterIn_.setBounds(meters.removeFromLeft(9).reduced(0, t.grid));
        meters.removeFromLeft(4);
        meterOut_.setBounds(meters.removeFromLeft(9).reduced(0, t.grid));
        body.removeFromRight(gap);

        curveView_->setBounds(body.removeFromLeft(body.getHeight()));
        body.removeFromLeft(gap * 2);

        // Right block: curve buttons + quality on top, colour row at the bottom.
        auto right = body.removeFromRight(170);
        curves_->setBounds(right.removeFromTop(44));
        right.removeFromTop(gap);
        auto colorRow = right.removeFromBottom(right.getHeight() - 26);
        quality_->setBounds(right.removeFromTop(20).removeFromRight(70));
        color_->setBounds(colorRow.removeFromLeft(48).withSizeKeepingCentre(48, 20));
        colorRow.removeFromLeft(t.grid);
        const int kw = colorRow.getWidth() / 2;
        colorFreq_->setBounds(colorRow.removeFromLeft(kw));
        colorGain_->setBounds(colorRow);
        body.removeFromRight(gap);

        // Middle: 2 x 2 knobs.
        const int kw2 = body.getWidth() / 2;
        auto top = body.removeFromTop(body.getHeight() / 2);
        drive_->setBounds(top.removeFromLeft(kw2));
        bias_->setBounds(top);
        output_->setBounds(body.removeFromLeft(kw2));
        mix_->setBounds(body);
    }

private:
    TransferCurve* curveView_ = nullptr;
    ui::ParamKnob *drive_ = nullptr, *bias_ = nullptr, *output_ = nullptr, *mix_ = nullptr;
    ui::ParamKnob *colorFreq_ = nullptr, *colorGain_ = nullptr;
    ui::ParamSegmented* curves_ = nullptr;
    ui::ParamToggle* color_ = nullptr;
    ui::ParamComboBox* quality_ = nullptr;
    ui::Meter meterIn_{2}, meterOut_{2};
};
} // namespace

const ui::PanelFactory& saturatorPanelFactory()
{
    static const ui::PanelFactory factory{
        .deviceId = "saturator",
        .create = [](ui::DeviceUiContext& ctx) -> std::unique_ptr<ui::DevicePanel> {
            return std::make_unique<SaturatorPanel>(ctx);
        },
    };
    return factory;
}
} // namespace fd::devices
