#include "GatePanel.h"

#include "Gate.h"
#include "fd/dsp/Math.h"

#include <array>

namespace fd::devices
{
namespace
{
constexpr float kFloorDb = -80.0f; // the display shows -80 .. 0 dBFS, the threshold's range

/// About four seconds of the level the gate reads (dim, from the bottom) and the output level
/// (a line), shaded where the gate was open, scrolling right to left. The threshold is a solid
/// line and the return (where it closes) a dashed one below it; drag either.
class GateHistory final : public juce::Component, public juce::SettableTooltipClient, public ui::ParamControl
{
public:
    explicit GateHistory(ui::DeviceUiContext& ctx) : ctx_(ctx)
    {
        setTooltip("Drag the solid line for Threshold, the dashed one for Return. Double-click resets.");
        frames_.fill({kFloorDb, kFloorDb, 0.0f});
    }

    void refresh() override
    {
        const auto key = std::make_tuple(get(Gate::Threshold), get(Gate::Return), get(Gate::Mode));
        if (key != last_)
        {
            last_ = key;
            repaint();
        }
    }

    /// Every frame: take the new history frames. `visible` false only drains the FIFO.
    void tick(bool visible)
    {
        if (ctx_.analysis == nullptr)
            return;
        auto& fifo = ctx_.analysis->fifo(Gate::historyFifo);
        bool any = false;
        float frame[3];
        while (fifo.pop(frame, 3) == 3)
        {
            frames_[static_cast<size_t>(write_)] = {frame[0], frame[1], frame[2]};
            write_ = (write_ + 1) % kFrames;
            any = true;
        }
        if (any && visible)
            repaint();
    }

    void paint(juce::Graphics& g) override
    {
        const auto& t = ui::Theme::current();
        const auto r = getLocalBounds().toFloat();
        g.setColour(t.surface0);
        g.fillRoundedRectangle(r, t.radiusControl);
        const auto area = r.reduced(1.0f);

        g.setColour(t.line);
        for (float db = -20.0f; db > kFloorDb; db -= 20.0f)
            g.fillRect(area.getX(), yOf(db), area.getWidth(), 1.0f);

        // One column per pixel: the loudest frame it covers; shaded where the gate was open.
        const int width = static_cast<int>(area.getWidth());
        juce::Path level, output;
        level.startNewSubPath(area.getX(), area.getBottom());
        bool outputOn = false;
        for (int x = 0; x < width; ++x)
        {
            const int from = x * kFrames / width, to = std::max(from + 1, (x + 1) * kFrames / width);
            float in = kFloorDb, out = kFloorDb, open = 0.0f;
            for (int f = from; f < to; ++f)
            {
                const auto& frame = frames_[static_cast<size_t>((write_ + f) % kFrames)];
                in = std::max(in, frame[0]);
                out = std::max(out, frame[1]);
                open = std::max(open, frame[2]);
            }
            const float px = area.getX() + static_cast<float>(x);
            if (open > 0.0f)
            {
                g.setColour(t.roles.curveFill);
                g.fillRect(px, area.getY(), 1.0f, area.getHeight());
            }
            level.lineTo(px, yOf(in));
            const bool hasOutput = out > kFloorDb;
            const auto o = juce::Point<float>(px, yOf(out));
            if (hasOutput)
                outputOn ? output.lineTo(o) : output.startNewSubPath(o);
            outputOn = hasOutput;
        }
        level.lineTo(area.getRight(), area.getBottom());
        level.closeSubPath();
        g.setColour(t.roles.spectrumFill);
        g.fillPath(level);
        g.setColour(t.textSecondary);
        g.strokePath(output, juce::PathStrokeType(1.0f));

        // The threshold, and the return below it (gate mode only).
        const bool lit = drag_ != Line::None || hover_ != Line::None;
        g.setColour(lit && (drag_ == Line::Threshold || hover_ == Line::Threshold) ? t.active : t.value);
        g.fillRect(area.getX(), yOf(get(Gate::Threshold)) - 0.75f, area.getWidth(), 1.5f);
        if (isGateMode())
        {
            g.setColour(lit && (drag_ == Line::Return || hover_ == Line::Return) ? t.active : t.value);
            const float y = yOf(returnDb());
            const float dashes[] = {4.0f, 3.0f};
            g.drawDashedLine({area.getX(), y, area.getRight(), y}, dashes, 2, 1.0f);
        }

        if (!readout_.isEmpty())
        {
            g.setColour(t.textSecondary);
            g.setFont(ui::font(ui::TextStyle::ValueSmall));
            g.drawText(readout_, r.reduced(6.0f, 4.0f), juce::Justification::topLeft, false);
        }
    }

    void mouseDown(const juce::MouseEvent& e) override
    {
        drag_ = lineAt(e.position.y);
        if (drag_ == Line::None)
            drag_ = Line::Threshold;
        ctx_.params.beginGesture(param());
        startY_ = e.position.y;
        startValue_ = get(param());
        showReadout();
    }

    void mouseDrag(const juce::MouseEvent& e) override
    {
        if (drag_ == Line::None)
            return;
        const float fine = e.mods.isShiftDown() ? 0.1f : 1.0f;
        const float db = (e.position.y - startY_) * fine * -kFloorDb / static_cast<float>(getHeight()); // down = lower
        if (drag_ == Line::Threshold)
            set(Gate::Threshold, juce::jlimit(kFloorDb, 0.0f, startValue_ - db));
        else
            set(Gate::Return, juce::jlimit(0.0f, 24.0f, startValue_ + db)); // the line moves down
        showReadout();
    }

    void mouseUp(const juce::MouseEvent&) override
    {
        if (drag_ == Line::None)
            return;
        ctx_.params.endGesture(param());
        drag_ = Line::None;
        repaint();
    }

    void mouseDoubleClick(const juce::MouseEvent& e) override
    {
        const auto line = lineAt(e.position.y);
        const size_t p = line == Line::Return ? Gate::Return : Gate::Threshold;
        ctx_.params.beginGesture(p);
        set(p, ctx_.info.params[p].def);
        ctx_.params.endGesture(p);
    }

    void mouseMove(const juce::MouseEvent& e) override
    {
        const auto line = lineAt(e.position.y);
        if (line != hover_)
        {
            hover_ = line;
            if (hover_ == Line::None)
                readout_.clear();
            else
                showReadout(hover_);
            repaint();
        }
    }

    void mouseExit(const juce::MouseEvent&) override
    {
        hover_ = Line::None;
        readout_.clear();
        repaint();
    }

private:
    enum class Line { None, Threshold, Return };
    static constexpr int kFrames = 400; // 4 s at one frame per 10 ms

    [[nodiscard]] float get(size_t p) const { return ctx_.params.getPlain(p); }
    [[nodiscard]] bool isGateMode() const
    {
        return static_cast<int>(get(Gate::Mode)) == static_cast<int>(Gate::ModeType::Gate);
    }
    [[nodiscard]] float returnDb() const { return get(Gate::Threshold) - get(Gate::Return); }
    [[nodiscard]] size_t param() const { return drag_ == Line::Return ? Gate::Return : Gate::Threshold; }
    [[nodiscard]] float yOf(float db) const
    {
        const auto area = getLocalBounds().toFloat().reduced(1.0f);
        return area.getBottom() - juce::jlimit(0.0f, 1.0f, (db - kFloorDb) / -kFloorDb) * area.getHeight();
    }
    /// The line within reach of `y` (the return wins when they overlap: it can only go down).
    [[nodiscard]] Line lineAt(float y) const
    {
        constexpr float reach = 5.0f;
        if (isGateMode() && std::abs(y - yOf(returnDb())) <= reach && y >= yOf(get(Gate::Threshold)))
            return Line::Return;
        if (std::abs(y - yOf(get(Gate::Threshold))) <= reach)
            return Line::Threshold;
        return Line::None;
    }

    void set(size_t p, float value)
    {
        ctx_.params.setPlain(p, value);
        refresh();
    }

    void showReadout(Line line = Line::None)
    {
        const size_t p = (line == Line::None ? drag_ : line) == Line::Return ? Gate::Return : Gate::Threshold;
        const auto& spec = ctx_.info.params[p];
        readout_ = juce::String(std::string(spec.name)) + " " + juce::String(spec.toText(get(p)));
        repaint();
    }

    ui::DeviceUiContext& ctx_;
    std::array<std::array<float, 3>, kFrames> frames_{};
    int write_ = 0;
    std::tuple<float, float, float> last_{1.0f, -1.0f, -1.0f};
    Line drag_ = Line::None, hover_ = Line::None;
    float startY_ = 0.0f, startValue_ = 0.0f;
    juce::String readout_;
};

/// Sections per devices/gate/SPEC.md, on the module grid (docs/design/device-panels.md).
class GatePanel final : public ui::DevicePanel
{
public:
    explicit GatePanel(ui::DeviceUiContext& ctx) : DevicePanel(ctx)
    {
        auto& history = addSection("history", "History", 6);
        history_ = &own(std::make_unique<GateHistory>(ctx));
        history.place(*history_, 0, 0, 6, 6);

        auto& gate = addSection("gate", "Gate", 3);
        gate.knob(Gate::Threshold, 0, 0);
        auto& ret = gate.knob(Gate::Return, 1, 0);
        gate.knob(Gate::Range, 2, 0);
        gate.segmented(Gate::Mode, 0, 3, 2, 2); // stacked: "Expander" needs two cells
        auto& ratio = gate.knob(Gate::Ratio, 2, 3);
        gate.toggle(Gate::Flip, 0, 5, 2);

        auto& time = addSection("time", "Time", 3);
        time.knob(Gate::Attack, 0, 0);
        time.knob(Gate::Hold, 1, 0);
        time.knob(Gate::Release, 2, 0);
        time.label("Lookahead", 0, 3, 2);
        time.combo(Gate::Lookahead, 0, 4, 2);

        auto& sidechain = addSection("sidechain", "Sidechain", 3);
        sidechain.label("Filter", 0, 0, 1);
        sidechain.combo(Gate::ScFilter, 1, 0, 2);
        sidechain.toggle(Gate::ScListen, 1, 1, 2);
        auto& freq = sidechain.knob(Gate::ScFreq, 0, 3);
        auto& q = sidechain.knob(Gate::ScQ, 1, 3);
        auto& gain = sidechain.knob(Gate::ScGain, 2, 3);

        // Settings the current mode or filter ignores recede (ui-principles §3).
        auto expander = [&ctx] {
            return static_cast<int>(ctx.params.getPlain(Gate::Mode)) == static_cast<int>(Gate::ModeType::Expander);
        };
        dimWhen({&ratio}, [expander] { return !expander(); });
        dimWhen({&ret}, expander);
        auto filter = [&ctx] {
            return static_cast<Gate::ScFilterType>(static_cast<int>(ctx.params.getPlain(Gate::ScFilter)));
        };
        dimWhen({&freq, &q}, [filter] { return filter() == Gate::ScFilterType::Off; });
        dimWhen({&gain}, [filter] { return filter() != Gate::ScFilterType::Bell; });

        if (auto* bus = ctx.analysis)
        {
            meterIn_.setLevelSource([bus](int c) { return bus->takePeak(c == 0 ? Gate::PeakInL : Gate::PeakInR); });
            meterOut_.setLevelSource([bus](int c) { return bus->takePeak(c == 0 ? Gate::PeakOutL : Gate::PeakOutR); });
        }
        setMeters({{&meterIn_, "In"}, {&meterOut_, "Out"}});
    }

private:
    void tick()
    {
        if (isShowing())
            history_->tick(!findSection("history")->isCollapsed());
    }

    juce::VBlankAttachment vblank_{this, [this] { tick(); }};
    GateHistory* history_ = nullptr;
    ui::Meter meterIn_{2}, meterOut_{2};
};
} // namespace

const ui::PanelFactory& gatePanelFactory()
{
    static const ui::PanelFactory factory{
        .deviceId = "gate",
        .create = [](ui::DeviceUiContext& ctx) -> std::unique_ptr<ui::DevicePanel> {
            return std::make_unique<GatePanel>(ctx);
        },
    };
    return factory;
}
} // namespace fd::devices
