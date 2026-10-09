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

/// Sections per devices/saturator/SPEC.md, on the module grid (docs/design/device-panels.md).
class SaturatorPanel final : public ui::DevicePanel
{
public:
    explicit SaturatorPanel(ui::DeviceUiContext& ctx) : DevicePanel(ctx)
    {
        // The curve edits Drive and Bias by dragging, so they sit beside it.
        auto& shape = addSection("shape", "Shape", 4);
        shape.place(own(std::make_unique<TransferCurve>(ctx)), 0, 0, 3, 4);
        shape.segmented(Saturator::Curve, 0, 4, 3, 2);
        shape.knob(Saturator::Drive, 3, 0);
        shape.knob(Saturator::Bias, 3, 3);

        // Freq and Gain only act while Color is on: the section's power dims them.
        auto& color = addSection("color", "Color", 1, Saturator::Color);
        color.knob(Saturator::ColorFreq, 0, 0);
        color.knob(Saturator::ColorGain, 0, 3);

        auto& output = addSection("output", "Output", 1);
        output.knob(Saturator::Output, 0, 0);
        output.knob(Saturator::Mix, 0, 3);

        addHeaderCombo(Saturator::Quality);

        if (auto* bus = ctx.analysis)
        {
            meterIn_.setLevelSource([bus](int c) { return bus->takePeak(c == 0 ? Saturator::PeakInL : Saturator::PeakInR); });
            meterOut_.setLevelSource([bus](int c) { return bus->takePeak(c == 0 ? Saturator::PeakOutL : Saturator::PeakOutR); });
        }
        setMeters({{&meterIn_, "In"}, {&meterOut_, "Out"}});
    }

private:
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
